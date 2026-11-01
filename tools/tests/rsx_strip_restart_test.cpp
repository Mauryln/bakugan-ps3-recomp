#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
constexpr u32 MAX_VERTICES = 64;
constexpr u32 VP_VERT_STRIDE = 256;
struct VPSlot { float v[4]; };
struct rsx_state {
    struct { int enabled; } vertex_attribs[16];
    u32 begin_epoch, index_array_dma, restart_index_enable, restart_index;
};
static VPSlot buffer[MAX_VERTICES * 16];
static struct { void* vp_vb_mapped = buffer; u32 vp_vb_offset = 0; int vp_parity = 0; u32 vp_vertex_capacity = MAX_VERTICES; } s_d3d;
uint8_t* vm_base = reinterpret_cast<uint8_t*>(buffer);
static std::vector<u32> indices;
static unsigned fetched_vertices;
static unsigned vertex_revision;
static u32 read_guest_index(const rsx_state*, u32 i) { return indices.at(i); }
static void read_vp_vertex(const rsx_state*, u32 i, VPSlot* dst)
{
    // Sentinel indices must never reach the guest vertex fetch.
    assert(i != 0xffff && i != 0xffffffff);
    ++fetched_vertices;
    memset(dst, 0, 16 * sizeof(*dst));
    dst[0].v[0] = static_cast<float>(i);
    dst[0].v[1] = static_cast<float>(vertex_revision);
}
#include "strip_upload.inc"

static void expect(const std::vector<u32>& wanted)
{
    assert(s_d3d.vp_vb_offset == wanted.size() * VP_VERT_STRIDE);
    for (size_t i = 0; i < wanted.size(); ++i)
        assert(buffer[i * 16].v[0] == wanted[i]);
}

int main()
{
    rsx_state st{};
    st.vertex_attribs[0].enabled = 1;
    st.index_array_dma = 0x10; // u16 indices
    st.restart_index_enable = 1;
    st.restart_index = 0xffff;
    st.begin_epoch = 1;
    indices = {0, 1, 2, 0xffff, 3, 4, 5, 6};
    upload_strip_vp_indexed(&st, 0, 8, 0);
    expect({0, 1, 2, 3, 4, 5, 4, 6, 5});

    // The next packet continues the strip, including winding parity.
    s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
    indices = {0, 1, 2, 3, 4, 5};
    fetched_vertices = 0;
    upload_strip_vp_indexed(&st, 0, 6, 0);
    expect({0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4});
    assert(fetched_vertices == 6); // Shared strip vertices are decoded once per packet.
    s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
    upload_strip_vp_indexed(&st, 0, 3, 0);
    ++vertex_revision; // Changed guest arrays must invalidate the packet cache.
    upload_strip_vp_indexed(&st, 3, 3, 0);
    expect({0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4});
    for (unsigned i = 0; i < 3; ++i) assert(buffer[i * 16].v[1] == 0);
    for (unsigned i = 3; i < 12; ++i) assert(buffer[i * 16].v[1] == 1);

    // Hash collisions must decode the new reference rather than reuse a slot.
    s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
    indices = {0, 16, 32, 48, 64, 80};
    upload_strip_vp_indexed(&st, 0, 6, 0);
    expect({0, 16, 32, 16, 48, 32, 32, 48, 64, 48, 80, 64});

    // Short packets, adjacent cuts and a cut at a packet boundary.
    s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
    indices = {0xffff, 0xffff, 0, 1, 2, 0xffff, 3, 4, 5, 0xffff};
    for (u32 i = 0; i < indices.size(); ++i)
        upload_strip_vp_indexed(&st, i, 1, 0);
    expect({0, 1, 2, 3, 4, 5});

    // Fan anchor restarts too, and survives a packet boundary.
    s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
    indices = {0, 1, 2, 3, 0xffff, 4, 5, 6};
    upload_strip_vp_indexed(&st, 0, 3, 1);
    upload_strip_vp_indexed(&st, 3, 5, 1);
    expect({0, 1, 2, 0, 2, 3, 4, 5, 6});

    // Disabled restart and a sentinel outside the u16 range are ordinary indices.
    for (int mode = 0; mode < 2; ++mode) {
        s_d3d.vp_vb_offset = 0; ++st.begin_epoch;
        st.restart_index_enable = mode;
        st.restart_index = mode ? 0x10000 : 1;
        indices = {0, 1, 2};
        upload_strip_vp_indexed(&st, 0, 3, 0);
        expect({0, 1, 2});
    }
    puts("PASS: restart cuts, packet continuity, fan anchors and u16 limits");
}
