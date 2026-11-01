#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstdint>
#include <cstring>
using u32 = uint32_t;
static HANDLE s_gcm_kick_ev = NULL;
#include "fifo_kick.inc"
#define VM_HLE_INJECT_BASE 0x10000u
static unsigned char guest_memory[0x14000];
static unsigned char* vm_base = guest_memory;
static int g_pt_val = -2;
static void barrier_watch_hit(uint32_t, uint32_t, int, void*) {}
static bool vm_oob(uint32_t a, unsigned n) { return a + n > sizeof(guest_memory); }
static bool vm_null_store(uint32_t, uint32_t, int, void*) { return false; }
static void pt_restore(uint32_t) {}
static bool spu_raw_is_reg(uint32_t) { return false; }
static void spu_raw_reg_store(uint32_t, uint32_t, unsigned) {}
#define VM_WRITE_COH(a, p, n) memcpy(vm_base + (a), (p), (n))
#include "fifo_put.inc"
int main()
{
    _putenv_s("GCM_FIFO_KICK", "1");
    // Producer and consumer may first access the event concurrently.
    HANDLE start_gate = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE handles[16] = {};
    std::thread initializers[16];
    for (unsigned i = 0; i < 16; ++i) initializers[i] = std::thread([&, i] {
        WaitForSingleObject(start_gate, INFINITE);
        handles[i] = (HANDLE)cellGcm_fifo_kick_event();
    });
    SetEvent(start_gate);
    for (auto& initializer : initializers) initializer.join();
    CloseHandle(start_gate);
    HANDLE event = (HANDLE)cellGcm_fifo_kick_event();
    if (!event) return 1;
    for (HANDLE handle : handles) if (handle != event) return 8;
    // Pending signals must survive until the consumer reaches its idle wait.
    SetEvent(event);
    ULONGLONG start = GetTickCount64();
    cellGcm_fifo_kick_wait(1000);
    if (GetTickCount64() - start > 500) return 2;
    // A producer arriving during the wait must wake the consumer too.
    std::thread producer([event] { Sleep(30); SetEvent(event); });
    start = GetTickCount64();
    cellGcm_fifo_kick_wait(1000);
    ULONGLONG elapsed = GetTickCount64() - start;
    producer.join();
    if (elapsed > 500) return 3;
    // Auto-reset consumes the signal: no permanent busy loop after a kick.
    if (WaitForSingleObject(event, 0) != WAIT_TIMEOUT) return 4;
    // GET/reference publication must not feed back into the consumer wakeup.
    vm_write32(VM_HLE_INJECT_BASE + 0x2004, 123);
    vm_write32(VM_HLE_INJECT_BASE + 0x2008, 456);
    if (WaitForSingleObject(event, 0) != WAIT_TIMEOUT) return 5;
    vm_write32(VM_HLE_INJECT_BASE + 0x2000, 0x12345678);
    if (WaitForSingleObject(event, 0) != WAIT_OBJECT_0) {
        puts("FAIL: publishing FIFO PUT does not wake the consumer");
        return 6;
    }
    const unsigned char wanted[] = {0x12, 0x34, 0x56, 0x78};
    if (memcmp(vm_base + VM_HLE_INJECT_BASE + 0x2000, wanted, 4)) return 7;
    CloseHandle(event);
    puts("PASS: pending and concurrent FIFO kicks interrupt the idle wait");
}
