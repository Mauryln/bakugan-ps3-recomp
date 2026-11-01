#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
static const char* armed;
static const char* option(const char* name)
{
    return armed && !strcmp(armed, name) ? "0" : nullptr;
}
#define getenv option
static int s_vm_write_watch_enabled;
static uint32_t g_barrier_sync_watch;
static unsigned calls;
static uint32_t last_address, last_value;
static int last_width;
static void* last_return;
static void barrier_watch_hit_slow(uint32_t a, uint32_t v, int width, void* ra)
{
    ++calls; last_address = a; last_value = v; last_width = width; last_return = ra;
}
#include "write_watch.inc"
int main()
{
    assert(!ppu_write_watch_env_enabled());
    armed = "PPU_WVAL"; assert(ppu_write_watch_env_enabled());
    armed = "PPU_WWATCH"; assert(ppu_write_watch_env_enabled());
    armed = "UNRELATED"; assert(!ppu_write_watch_env_enabled());
    s_vm_write_watch_enabled = 0;
    for (unsigned i = 0; i < 1000000; ++i) barrier_watch_hit(i, i, 4, nullptr);
    assert(calls == 0);
    s_vm_write_watch_enabled = 1;
    barrier_watch_hit(0x10000, 0x1234, 8, (void*)0x5678);
    assert(calls == 1 && last_address == 0x10000 && last_value == 0x1234);
    assert(last_width == 8 && last_return == (void*)0x5678);
    s_vm_write_watch_enabled = 0;
    g_barrier_sync_watch = 0x20000;
    barrier_watch_hit(0x20040, 7, 1, nullptr);
    assert(calls == 2 && last_address == 0x20040 && last_value == 7);
    g_barrier_sync_watch = 0;
    barrier_watch_hit(0x20040, 0, 1, nullptr);
    assert(calls == 2);
    puts("PASS: inactive probes skipped; environment and runtime-armed write watches preserved");
}
