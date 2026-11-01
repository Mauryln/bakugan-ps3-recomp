#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
constexpr u32 MAX_DRAWS = 2048;
struct rsx_state { u32 surface_format; };
struct D3D12DrawRecord {
    int is_clear, is_vp, tex_slot;
    u32 rt_off, rt_mrt[3], rt_w, rt_h, rt_fmt, cmask, clear_mask;
    float cc[4], clear_z;
    u8 clear_s;
};
static struct {
    D3D12DrawRecord draws[MAX_DRAWS];
    u32 draw_count, vb_offset, vp_vb_offset, fps;
    u64 frame_count, last_fps_time;
    bool initialized = true;
    float clear_color[4];
    rsx_state* current_rsx_state;
} s_d3d;
static u32 s_dbg_clears_since_present, s_clear_presents;
static unsigned flip_seq, presents;
static int blink_dbg() { return 0; }
unsigned cellGcm_flip_request_count() { return flip_seq; }
static u32 current_rt_off(u32* w, u32* h, u32* mrt)
{
    *w = 1280; *h = 720;
    memset(mrt, 0, 3 * sizeof(u32));
    return 0;
}
static void render_frame()
{
    ++presents;
    ++s_d3d.frame_count;
    s_d3d.draw_count = 0;
}
#include "frame_boundary.inc"
static void submit_frame(u32 draws)
{
    s_d3d.draw_count = draws;
    memset(s_d3d.draws, 0, sizeof(s_d3d.draws));
    s_d3d.draws[0].is_vp = 1;
    s_d3d.draws[0].cmask = 15;
    ++flip_seq;
    const unsigned before = presents;
    // Multiple clears of the display may be passes in one guest frame.
    d3d12_clear(nullptr, 0xf0, 0, 1, 0);
    d3d12_clear(nullptr, 0xf0, 0, 1, 0);
    assert(presents == before);
    assert(s_d3d.draw_count == draws);
    // Only the actual flip presents the completed batch.
    d3d12_present(nullptr, 0);
    assert(presents == before + 1);
}
int main()
{
    _putenv_s("RSX_ACCUM_FRAME", "1");
    submit_frame(1800);
    for (unsigned i = 0; i < 120; ++i) submit_frame(40);
    assert(presents == 121 && s_clear_presents == 0);
    puts("PASS: a large arena followed by 120 small menu frames presents exactly once per flip");
}
