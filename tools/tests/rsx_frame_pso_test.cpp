#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
using u32 = uint32_t;
using DXGI_FORMAT = unsigned;
constexpr unsigned DXGI_FORMAT_R8G8B8A8_UNORM = 28, MAX_DRAWS = 64;
struct ID3D12PipelineState;
struct VTable {
    unsigned (*AddRef)(ID3D12PipelineState*);
    unsigned (*Release)(ID3D12PipelineState*);
};
struct ID3D12PipelineState { VTable* lpVtbl; unsigned refs; bool dead; };
static bool gpu_pending;
static unsigned add_ref(ID3D12PipelineState* p) { assert(!p->dead); return ++p->refs; }
static unsigned release(ID3D12PipelineState* p)
{
    assert(p->refs);
    if (!--p->refs) { assert(!gpu_pending); p->dead = true; }
    return p->refs;
}
static VTable vtable = {add_ref, release};
static ID3D12PipelineState pipeline = {&vtable, 1, false};
static ID3D12PipelineState* s_frame_psos[MAX_DRAWS];
struct D3D12DrawRecord {
    u32 fp_addr, rt_off, rt_fmt, blend_key, alpha_ctl, cmask, cull, depth_key;
    int vs_idx, fp_exp32, depth_bias;
    float slope_bias;
};
static bool opaque;
static const char* option(const char*) { return opaque ? "1" : nullptr; }
#define getenv option
static int atoi(const char*) { return 1; }
static int dr_num_rts(const D3D12DrawRecord*) { return 2; }
static u32 dr_cube_mask(const D3D12DrawRecord*) { return 3; }
static DXGI_FORMAT rsx_surface_dxgi(u32 fmt) { return fmt + 100; }
static unsigned lookups;
static bool expect_offscreen;
static ID3D12PipelineState* vp_get_fp_pso(int vs, u32 fp, u32 blend, int nrt,
    DXGI_FORMAT fmt, int exp, u32 cmask, u32 cull, u32 cube, u32 depth,
    u32 alpha, int bias, float slope)
{
    ++lookups;
    assert(vs == 4 && fp == 5 && nrt == 2 && exp == 1 && cmask == 7);
    assert(cull == 8 && cube == 3 && depth == 9 && bias == -3 && slope == 0.25f);
    assert(blend == (opaque && !expect_offscreen ? 0u : 10u));
    assert(alpha == (opaque && !expect_offscreen ? 0u : 11u));
    assert(fmt == (expect_offscreen ? 112u : 28u));
    return &pipeline;
}
#include "frame_pso.inc"
int main(int argc, char** argv)
{
    opaque = argc > 1 && !strcmp(argv[1], "opaque");
    D3D12DrawRecord dr = {5, 0, 12, 10, 11, 7, 8, 9, 4, 1, -3, 0.25f};
    for (unsigned frame = 0; frame < 100; ++frame) {
        pipeline.refs = 1; pipeline.dead = false; lookups = 0;
        for (unsigned d = 0; d < MAX_DRAWS; ++d) {
            expect_offscreen = (d & 1) != 0; dr.rt_off = expect_offscreen ? 1 : 0;
            vp_store_frame_pso(d, vp_prepare_draw_pso(&dr));
        }
        gpu_pending = true;
        release(&pipeline); // Evict every cache reference before command submission.
        for (unsigned d = 0; d < MAX_DRAWS; ++d)
            assert(s_frame_psos[d] == &pipeline && !pipeline.dead);
        assert(lookups == MAX_DRAWS && pipeline.refs == MAX_DRAWS);
        gpu_pending = false; // GPU fence completes before releasing frame ownership.
        vp_release_frame_psos();
        assert(pipeline.dead && pipeline.refs == 0);
        for (auto* p : s_frame_psos) assert(!p);
    }
    dr.fp_addr = 0;
    assert(!vp_prepare_draw_pso(&dr));
    puts("PASS: one lookup per draw, diagnostic keys preserved, evicted PSOs survive until GPU completion");
}
