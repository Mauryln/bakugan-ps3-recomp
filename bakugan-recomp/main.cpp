/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"
#if defined(BAKUGAN_SPU_SSE41)
#include <intrin.h>
#endif

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>   /* mmap: the guest arena, see alloc_guest_vm */
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* cellGame: read TITLE_ID/TITLE/CATEGORY/version from the game's PARAM.SFO.
 * Call before ppu_run -- cellGame/cellSaveData build /dev_hdd0/game/<id> from it. */
void     cellGame_init_from_paramsfo(const char* sfo_path);

/* The SPURS LF queue this title created and the first descriptors pushed into
 * it, recorded by libs/spurs/cellSpurs.c. Diagnostics (BAKUGAN_DUMP_QUEUE). */
extern uint32_t g_ps3_lfq_ea, g_ps3_lfq_buf, g_ps3_lfq_size, g_ps3_lfq_depth;
extern int      g_ps3_lfq_first_n;
extern uint8_t  g_ps3_lfq_first[8][64];

/* cellOskDialog stub: text handed back when the title asks for a player name. */
void     cellOskDialogSetDefaultResponse(const char* text);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
#ifdef _WIN32
void     cellGcm_fifo_kick_wait(unsigned ms);
#endif
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

static unsigned frame_clock_due_ticks(unsigned long long now,
                                      unsigned long long* next_tick)
{
    if (now < *next_tick) return 0;
    unsigned long long due = (now - *next_tick) / 16 + 1;
    /* A blocked render thread cannot replay seconds of display interrupts.
     * Bound catch-up work and resume at real time after a long stall. */
    if (due > 4) {
        *next_tick = now + 16;
        return 4;
    }
    *next_tick += due * 16;
    return (unsigned)due;
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "Bakugan: Battle Brawlers";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();

    for (;;) {
#ifdef _WIN32
        /* A guest waiting on an RSX reference wakes the consumer immediately.
         * A fixed sleep here adds a scheduler delay to every render-pass fence. */
        cellGcm_fifo_kick_wait(4);
#else
        Sleep(4);
#endif
        ULONGLONG now = GetTickCount64();

        unsigned due = frame_clock_due_ticks(now, &next_tick);
        for (unsigned fired = 0; fired < due; fired++) {
            if (rsx_ok && rsx_backend_pump() != 0) rsx_ok = 0;
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
        }

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
                present_guest_frame();
                last_flip = fc;
            } else if (fc == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is mapped up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left.
     *
     * mmap, not calloc: vm_commit() hands vm_base + addr to mprotect, which
     * takes page-aligned addresses only. glibc serves an allocation this big
     * from its own mmap chunk and returns a pointer 16 bytes past the page
     * start, so every commit failed with EINVAL and sys_memory_allocate
     * answered ENOMEM to every call. mmap's base is page-aligned and zeroed. */
    void* p = mmap(NULL, 0xE0000000u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * BAKUGAN_DUMP_TEX=<ms>[,<addr>,<w>,<h>]: dump a guest main-memory texture.
 *
 * The boot frame draws a full-screen quad sampling a texture in XDR at
 * 0xDF60000 (1120x584, A8R8G8B8). If that memory holds no pixels, the quad is
 * black and the question is the loader, not the renderer. Diagnostics only.
 * -----------------------------------------------------------------------*/
/* ---------------------------------------------------------------------------
 * BAKUGAN_WATCH_TEX=<hexEA>[,<bytes>]: watch a guest-memory region and report
 * the first change, with the guest PC of the thread that wrote it. The lifted
 * code stores straight into vm_base (not through vm_write*), so PPU_WWATCH
 * cannot see those writes; this polls the region instead.
 * -----------------------------------------------------------------------*/
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;

static DWORD WINAPI debug_watch_tex(LPVOID)
{
    const char* e = getenv("BAKUGAN_WATCH_TEX");
    if (!e || !*e) return 0;
    uint32_t addr = 0xDF60000u; size_t n = 0x20000u;
    { const char* c = strchr(e, ',');
      if (c) { addr = (uint32_t)strtoul(c + 1, NULL, 0);
               c = strchr(c + 1, ','); if (c) n = (size_t)strtoul(c + 1, NULL, 0); } }

    uint32_t prev = 0;
    for (size_t i = 0; i < n; i += 4)
        prev = prev * 131u + ((uint32_t)vm_base[addr+i] << 24 | (uint32_t)vm_base[addr+i+1] << 16 |
                              (uint32_t)vm_base[addr+i+2] << 8 | vm_base[addr+i+3]);

    for (int it = 0; it < 20000; it++) {
        Sleep(2);
        uint32_t h = 0;
        for (size_t i = 0; i < n; i += 4)
            h = h * 131u + ((uint32_t)vm_base[addr+i] << 24 | (uint32_t)vm_base[addr+i+1] << 16 |
                            (uint32_t)vm_base[addr+i+2] << 8 | vm_base[addr+i+3]);
        if (h != prev) {
            uint32_t cia = g_active_ctx ? (uint32_t)g_active_ctx->cia : 0;
            uint32_t lr  = g_active_ctx ? (uint32_t)g_active_ctx->lr  : 0;
            fprintf(stderr, "[watch] 0x%08X changed at iter %d: cia=0x%08X lr=0x%08X tid=%u\n",
                    addr, it, cia, lr, g_active_ctx ? (unsigned)g_active_ctx->thread_id : 0);
            fflush(stderr);
            prev = h;
            break;
        }
    }
    if (prev) {} /* keep the loop live under -O2 */
    return 0;
}

static DWORD WINAPI debug_dump_tex(LPVOID)
{
    const char* e = getenv("BAKUGAN_DUMP_TEX");
    if (!e || !*e) return 0;
    unsigned ms = (unsigned)atoi(e);
    unsigned addr = 0xDF60000u, w = 1120, h = 584;
    { const char* c = strchr(e, ',');
      if (c) { addr = (unsigned)strtoul(c + 1, NULL, 0);
               c = strchr(c + 1, ','); if (c) { w = (unsigned)atoi(c + 1);
               c = strchr(c + 1, ','); if (c) h = (unsigned)atoi(c + 1); } } }

    /* BAKUGAN_DUMP_AT=ms[,ms...] dumps the queue at each of those times too,
     * which is how the PPU-side indices are compared before/after the SPU
     * worker touches the line. */
    const char* at = getenv("BAKUGAN_DUMP_AT");
    if (at && *at) {
        char buf[256]; strncpy(buf, at, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        unsigned prev = 0;
        for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            unsigned t = (unsigned)atoi(tok);
            const char* tag = tok;
            if (t > prev) Sleep(t - prev);
            prev = t;
            unsigned q = g_ps3_lfq_ea;
            printf("[dump@%ums] tag=%s lfq=0x%08X:", t, tag, q);
            if (!q) { printf(" (not initialised)\n"); fflush(stdout); continue; }
            for (int i = 0; i < 0x20; i += 4) {
                unsigned v = ((unsigned)vm_base[q+i] << 24) | ((unsigned)vm_base[q+i+1] << 16) |
                             ((unsigned)vm_base[q+i+2] << 8) | vm_base[q+i+3];
                printf(" %08X", v);
            }
            printf("\n"); fflush(stdout);
        }
    }

    Sleep(ms);
    size_t bytes = (size_t)w * h * 4;
    FILE* f = fopen("dump_tex.bin", "wb");
    if (f) { fwrite(vm_base + addr, 1, bytes, f); fclose(f); }
    size_t nz = 0;
    for (size_t i = 0; i < bytes; i++) if (vm_base[addr + i]) nz++;
    printf("[dump] tex 0x%08X %ux%u: %zu/%zu non-zero bytes -> dump_tex.bin\n",
           addr, w, h, nz, bytes);

    /* The SPURS LF queue this run created, and the descriptors it was given:
     * both addresses come from the runtime, so the dump is valid whatever the
     * heap layout is. Reads the PPU-side indices, the first 8 elements and the
     * destination of element 0 + its head bytes. */
    if (getenv("BAKUGAN_DUMP_QUEUE")) {
        unsigned q = g_ps3_lfq_ea, buf = g_ps3_lfq_buf;
        printf("[dump] lfqueue 0x%08X depth=%u size=%u:", q, g_ps3_lfq_depth, g_ps3_lfq_size);
        for (int i = 0; i < 0x80; i += 4) {
            unsigned v = ((unsigned)vm_base[q+i] << 24) | ((unsigned)vm_base[q+i+1] << 16) |
                         ((unsigned)vm_base[q+i+2] << 8) | vm_base[q+i+3];
            printf(" %08X", v);
        }
        printf("\n");
        for (int e = 0; e < g_ps3_lfq_first_n; e++) {
            const uint8_t* p = g_ps3_lfq_first[e];
            unsigned w0 = ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
            unsigned w1 = ((unsigned)p[4]<<24)|((unsigned)p[5]<<16)|((unsigned)p[6]<<8)|p[7];
            unsigned w2 = ((unsigned)p[8]<<24)|((unsigned)p[9]<<16)|((unsigned)p[10]<<8)|p[11];
            unsigned w5 = ((unsigned)p[20]<<24)|((unsigned)p[21]<<16)|((unsigned)p[22]<<8)|p[23];
            printf("[dump] elem%d src=0x%08X dst=0x%08X size=0x%X ef=0x%08X\n", e, w0, w1, w2, w5);
            if (e == 0 && w1) {
                /* Raw compressed source, for format identification on the host. */
                {
                    unsigned n = w2 ? (w2 < 0x10000 ? w2 : 0x10000) : 0x10000;
                    FILE* g = fopen("dump_src.bin", "wb");
                    if (g) { fwrite(vm_base + w0, 1, n, g); fclose(g); }
                    printf("[dump] wrote %u bytes of src 0x%08X -> dump_src.bin\n", n, w0);
                    /* Destination: deflate(raw) of the source expands to 64 KB
                     * starting 00 00 00 20 00 2E 04 20, so compare all of it. */
                    g = fopen("dump_dst.bin", "wb");
                    if (g) { fwrite(vm_base + w1, 1, 0x10000, g); fclose(g); }
                    size_t nz = 0;
                    for (int i = 0; i < 0x10000; i++) if (vm_base[w1 + i]) nz++;
                    printf("[dump] wrote 64 KB of dst 0x%08X -> dump_dst.bin (%zu non-zero)\n",
                           w1, nz);
                }
                printf("[dump] dst[0] 0x%08X:", w1);
                for (int i = 0; i < 32; i += 4) {
                    unsigned v = ((unsigned)vm_base[w1+i] << 24) | ((unsigned)vm_base[w1+i+1] << 16) |
                                 ((unsigned)vm_base[w1+i+2] << 8) | vm_base[w1+i+3];
                    printf(" %08X", v);
                }
                printf("\n[dump] src[0] 0x%08X:", w0);
                for (int i = 0; i < 32; i += 4) {
                    unsigned v = ((unsigned)vm_base[w0+i] << 24) | ((unsigned)vm_base[w0+i+1] << 16) |
                                 ((unsigned)vm_base[w0+i+2] << 8) | vm_base[w0+i+3];
                    printf(" %08X", v);
                }
                printf("\n");
            }
        }
        {
            unsigned ef = 0x10016F00u;   /* workload event flag (deterministic heap) */
            printf("[dump] evflag 0x%08X:", ef);
            for (int i = 0; i < 0x80; i += 4) {
                unsigned v = ((unsigned)vm_base[ef+i] << 24) | ((unsigned)vm_base[ef+i+1] << 16) |
                             ((unsigned)vm_base[ef+i+2] << 8) | vm_base[ef+i+3];
                printf(" %08X", v);
            }
            printf("\n");
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

int main(int argc, char** argv)
{
#if defined(BAKUGAN_SPU_SSE41)
    int cpu_features[4];
    __cpuid(cpu_features, 1);
    if (!(cpu_features[2] & (1 << 19))) {
        fprintf(stderr, "This build requires SSE4.1. Rebuild with -DPORT_SPU_SSE41=OFF for this CPU.\n");
        return 2;
    }
#if defined(BAKUGAN_SPU_AVX2)
    int cpu_extended[4];
    __cpuidex(cpu_extended, 7, 0);
    if (!(cpu_extended[1] & (1 << 5)) || !(cpu_features[2] & (1 << 27)) ||
        !(cpu_features[2] & (1 << 28))) {
        fprintf(stderr, "This build requires AVX2. Rebuild with -DPORT_SPU_AVX2=OFF for this CPU.\n");
        return 2;
    }
#endif
#endif
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* Multistream generates SPU mixer kernels in high local store. They have
     * no static image to lift; execute live bytes and rejoin compiled code.
     * Keep an explicit 0 override for diagnosis. Set before any worker starts. */
    if (!getenv("SPU_INTERP_UNLIFTED")) _putenv_s("SPU_INTERP_UNLIFTED", "1");
    /* This title has complete FIFO flip markers. A clear is a render pass,
     * not a reliable frame boundary: arena draw counts must not make later
     * menu frames accumulate. Preserve the game's own 30 Hz pacing. */
    if (!getenv("RSX_ACCUM_FRAME")) _putenv_s("RSX_ACCUM_FRAME", "1");
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* Real title id (BLES00758) and title name, from the game's own PARAM.SFO.
     * cellGame/cellSaveData/sceNpTrophy build their /dev_hdd0/game/<id> paths
     * from it; without this call the runtime keeps its BLES00000 placeholder. */
    {
        static const char* const kSfoLayouts[] = {
            "%s/PS3_GAME/PARAM.SFO",   /* vfs root = disc root */
            "%s/../PARAM.SFO",         /* vfs root = .../USRDIR */
            "%s/PARAM.SFO",            /* vfs root = the game dir */
        };
        char sfo[1100];
        int found = 0;
        for (size_t i = 0; i < sizeof kSfoLayouts / sizeof kSfoLayouts[0]; i++) {
            snprintf(sfo, sizeof sfo, kSfoLayouts[i], ppu_vfs_root);
            FILE* f = fopen(sfo, "rb");
            if (f) { fclose(f); found = 1; break; }
        }
        if (!found) snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    /* cellOskDialog is a stub that returns a preset string. On first boot the
     * title asks for the player's name and refuses an empty one ("You have not
     * entered a name"), so give it something valid. */
    cellOskDialogSetDefaultResponse(getenv("PS3_PLAYER_NAME") ? getenv("PS3_PLAYER_NAME")
                                                             : "PLAYER");

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */
#ifdef _WIN32
    extern void bakugan_install_video();
    bakugan_install_video();
#endif

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
    CreateThread(NULL, 256u * 1024, debug_dump_tex, NULL, 0, NULL);
    CreateThread(NULL, 256u * 1024, debug_watch_tex, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
