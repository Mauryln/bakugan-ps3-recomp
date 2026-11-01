#define CINTERFACE
#include <initguid.h>
#include <d3d12.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u32 = uint32_t;
constexpr u32 MAX_DRAWS = 2048, RSX_MAX_TEXTURES = 16;
struct D3D12DrawRecord {
    int is_vp, is_clear;
    u32 sampled_units, sampler_table;
    struct { u32 set, sampler_address, sampler_filter, sampler_control, sampler_border; } tex[16];
};
static struct {
    ID3D12Device* device;
    u32 draw_count;
    D3D12DrawRecord draws[MAX_DRAWS];
} s_d3d;
#include "sampler_tables.inc"

static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main()
{
    require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device, (void**)&s_d3d.device)), "create D3D12 device");
    s_d3d.draw_count = MAX_DRAWS;
    for (u32 i = 0; i < MAX_DRAWS; ++i) {
        auto& d = s_d3d.draws[i];
        d.is_vp = 1; d.sampled_units = 1u << 15;
        d.tex[15] = {1, 0x40404, 0x02022000, 0x80000000, i};
    }
    require(rsx_prepare_samplers() != 0, "prepare a worst-case frame of unique samplers");
    for (u32 i = 0; i < MAX_DRAWS; ++i) {
        require(s_d3d.draws[i].sampler_table == i, "rollover retains every draw's descriptor identity");
        u32 page = i / SAMPLER_TABLES_PER_PAGE, slot = i % SAMPLER_TABLES_PER_PAGE;
        require(s_sampler_pages[page].count == SAMPLER_TABLES_PER_PAGE, "all full pages are bounded");
        auto& desc = s_sampler_pages[page].tables[slot][15];
        require(desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER &&
            desc.BorderColor[2] == (float)(i & 255u) / 255.0f,
            "highest texture unit keeps its own border value after later tables are written");
    }
    // Reusing a completed frame's storage must deduplicate identical states,
    // while sampler changes remain distinct even with the same texture source.
    s_d3d.draw_count = 5;
    s_d3d.draws[1] = s_d3d.draws[0];
    s_d3d.draws[2] = s_d3d.draws[0]; s_d3d.draws[2].tex[15].sampler_address = 0x10101;
    s_d3d.draws[3] = s_d3d.draws[0]; s_d3d.draws[3].tex[15].sampler_filter = 0x01012000;
    s_d3d.draws[4] = s_d3d.draws[0]; s_d3d.draws[4].tex[15].sampler_control = 1u << 19;
    require(rsx_prepare_samplers() != 0, "reuse sampler storage after frame completion");
    require(s_d3d.draws[0].sampler_table == s_d3d.draws[1].sampler_table,
        "identical tables share immutable descriptors");
    for (u32 i = 2; i < 5; ++i)
        require(s_d3d.draws[i].sampler_table != s_d3d.draws[0].sampler_table,
            "wrap, filter and LOD changes never share an earlier sampler table");
    for (u32 p = 0; p < SAMPLER_MAX_PAGES; ++p)
        if (s_sampler_pages[p].heap) s_sampler_pages[p].heap->lpVtbl->Release(s_sampler_pages[p].heap);
    s_d3d.device->lpVtbl->Release(s_d3d.device);
    puts("PASS: 2048 unique draws, heap rollover, unit 15, immutable tables and deduplication");
}
