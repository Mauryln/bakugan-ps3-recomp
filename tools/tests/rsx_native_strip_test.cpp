#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
constexpr u32 VP_VERT_STRIDE = 256, MAX_VERTICES = 4096;
struct VPSlot { float v[4]; };
struct rsx_state {
    struct { int enabled; } vertex_attribs[16];
    u32 begin_epoch, index_array_dma, restart_index_enable, restart_index;
};
static VPSlot buffer[MAX_VERTICES * 16];
static struct { void* vp_vb_mapped = buffer; u32 vp_vb_offset = 0; int vp_parity = 0; u32 vp_vertex_capacity = MAX_VERTICES; } s_d3d;
uint8_t* vm_base = reinterpret_cast<uint8_t*>(buffer);
static std::vector<u32> indices;
static unsigned revision;
static unsigned fetched_vertices;
static u32 read_guest_index(const rsx_state*, u32 i) { return indices.at(i); }
static void read_vp_vertex(const rsx_state*, u32 index, VPSlot* dst)
{
    ++fetched_vertices;
    for (u32 a = 0; a < 16; ++a)
        for (u32 k = 0; k < 4; ++k)
            dst[a].v[k] = float(index * 64 + a * 4 + k + revision * 1000000);
    dst[0].v[0] = (float((index * 37) % 101) - 50.0f) / 55.0f;
    dst[0].v[1] = (float((index * 13) % 97) - 48.0f) / 52.0f;
    dst[0].v[2] = 0.5f;
    dst[0].v[3] = 1.0f;
    for (u32 k = 0; k < 3; ++k) dst[3].v[k] = float((index + k) % 7) / 7.0f;
    dst[3].v[3] = 1.0f;
}
#include "native_strip.inc"
using Vertex = std::array<float, 64>;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11Texture2D> target, readback;
static ComPtr<ID3D11RenderTargetView> rtv;
static void gpu_init()
{
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    const char* vs = "struct I{float4 p:ATTR0;float4 c:ATTR3;};struct O{float4 p:SV_Position;float4 c:COLOR0;};O main(I i){O o;o.p=i.p;o.c=i.c;return o;}";
    const char* ps = "float4 main(float4 p:SV_Position,float4 c:COLOR0):SV_Target{return c;}";
    ComPtr<ID3DBlob> vb, pb;
    assert(SUCCEEDED(D3DCompile(vs, strlen(vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vb, nullptr)));
    assert(SUCCEEDED(D3DCompile(ps, strlen(ps), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pb, nullptr)));
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11InputLayout> layout;
    assert(SUCCEEDED(device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vertex_shader)));
    assert(SUCCEEDED(device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &pixel_shader)));
    D3D11_INPUT_ELEMENT_DESC inputs[] = {
        {"ATTR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"ATTR", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    assert(SUCCEEDED(device->CreateInputLayout(inputs, 2, vb->GetBufferPointer(), vb->GetBufferSize(), &layout)));
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    context->IASetInputLayout(layout.Get());
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 64; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &target)));
    assert(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv)));
    td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &readback)));
    D3D11_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
    context->RSSetViewports(1, &viewport);
}
static std::vector<u8> gpu_draw(const std::vector<Vertex>& vertices, bool strip, bool front)
{
    const float clear[4] = {};
    context->ClearRenderTargetView(rtv.Get(), clear);
    ID3D11RenderTargetView* output = rtv.Get();
    context->OMSetRenderTargets(1, &output, nullptr);
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = front ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterizer;
    assert(SUCCEEDED(device->CreateRasterizerState(&rd, &rasterizer)));
    context->RSSetState(rasterizer.Get());
    if (!vertices.empty()) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = UINT(vertices.size() * sizeof(Vertex));
        bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {vertices.data(), 0, 0};
        ComPtr<ID3D11Buffer> buffer;
        assert(SUCCEEDED(device->CreateBuffer(&bd, &data, &buffer)));
        ID3D11Buffer* input = buffer.Get(); UINT stride = 256, offset = 0;
        context->IASetVertexBuffers(0, 1, &input, &stride, &offset);
        context->IASetPrimitiveTopology(strip ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(UINT(vertices.size()), 0);
    }
    context->CopyResource(readback.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    assert(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    std::vector<u8> pixels(64 * 64 * 4);
    for (u32 y = 0; y < 64; ++y) memcpy(pixels.data() + y * 256, (u8*)mapped.pData + y * mapped.RowPitch, 256);
    context->Unmap(readback.Get(), 0);
    return pixels;
}
static Vertex vertex(u32 i)
{
    Vertex result;
    memcpy(result.data(), buffer + i * 16, 256);
    return result;
}
static unsigned epoch;
static unsigned old_total, native_total, colored_pixels;
static void compare(const std::vector<u32>& stream, const std::vector<u32>& packets,
                    bool restart = true, u32 marker = 0xffff, u32 dma = 0x10)
{
    indices = stream;
    rsx_state st{};
    st.vertex_attribs[0].enabled = 1;
    st.begin_epoch = ++epoch;
    st.index_array_dma = dma;
    st.restart_index_enable = restart;
    st.restart_index = marker;
    u32 first = 0;
    old_total = native_total = 0;
    for (u32 count : packets) {
        ++revision; // Arrays may change between packets; all attributes matter.
        s_d3d.vp_vb_offset = 0;
        u32 old_count = upload_strip_vp_indexed(&st, first, count, 0);
        std::vector<Vertex> wanted;
        for (u32 i = 0; i < old_count; i += 3) {
            Vertex a = vertex(i), b = vertex(i + 1), c = vertex(i + 2);
            if (a != b && a != c && b != c) {
                wanted.push_back(a); wanted.push_back(b); wanted.push_back(c);
            }
        }
        s_d3d.vp_vb_offset = 0;
        u32 emitted = upload_native_strip_vp_indexed(&st, first, count);
        old_total += old_count; native_total += emitted;
        assert(emitted <= count * 3 + 3);
        assert(s_d3d.vp_vb_offset == emitted * 256);
        std::vector<Vertex> actual;
        for (u32 i = 2; i < emitted; ++i) {
            Vertex a = vertex(i - 2), b = vertex(i - 1), c = vertex(i);
            if ((i - 2) & 1) std::swap(b, c);
            if (a != b && a != c && b != c) {
                actual.push_back(a); actual.push_back(b); actual.push_back(c);
            }
        }
        assert(actual == wanted);
        if (epoch <= 12) {
            std::vector<Vertex> native;
            for (u32 i = 0; i < emitted; ++i) native.push_back(vertex(i));
            for (bool front : {false, true}) {
                auto before = gpu_draw(wanted, false, front);
                auto after = gpu_draw(native, true, front);
                for (size_t i = 0; i < before.size(); ++i) {
                    assert(abs(int(before[i]) - int(after[i])) <= 1);
                    if ((i & 3) == 3 && before[i]) ++colored_pixels;
                }
            }
        }
        first += count;
    }
    assert(first == stream.size());
}
int main()
{
    gpu_init();
    compare({0, 1, 2, 3, 4, 5}, {6});
    compare({0, 1, 2, 3, 4, 5}, {3, 3}); // odd continuation
    compare({0, 1, 2, 3, 4, 5}, {4, 2}); // even continuation
    compare({0, 1, 2, 3, 4, 5}, {1, 1, 1, 1, 1, 1});
    compare({0, 1, 2, 0xffff, 3, 4, 5, 6}, {8});
    compare({0xffff, 0, 1, 2, 0xffff, 0xffff, 3, 4, 5, 0xffff}, {3, 2, 1, 4});
    compare({0, 1, 0xffff, 2, 3, 4, 0xffff, 5, 0xffff}, {9});
    compare({0, 1, 2, 3, 0xffff, 4, 5, 6}, {8});
    compare({0, 1, 0xffff, 2, 3, 4}, {6}, false);
    compare({0, 1, 2, 0x7fff, 3, 4, 5}, {7}, true, 0x7fff);
    compare({0, 1, 2, 0xffffffff, 3, 4, 5}, {7}, true, 0xffffffff, 0);
    // Exhaust small restart patterns, including every packet boundary parity.
    for (unsigned bits = 0; bits < 256; ++bits) {
        std::vector<u32> stream;
        for (u32 i = 0; i < 8; ++i) stream.push_back(bits & (1u << i) ? 0xffff : i);
        compare(stream, {8});
        compare(stream, {1, 2, 1, 3, 1});
    }
    std::vector<u32> long_strip;
    for (u32 i = 0; i < 512; ++i) long_strip.push_back(i);
    compare(long_strip, {256, 256});
    assert(native_total < old_total / 2);
    assert(colored_pixels > 0);
    printf("UPLOAD: %u expanded vertices -> %u native vertices\n", old_total, native_total);
    indices = {0, 1, 2, 3, 0, 1, 2, 3};
    rsx_state reuse{};
    reuse.vertex_attribs[0].enabled = 1;
    reuse.begin_epoch = ++epoch;
    reuse.index_array_dma = 0x10;
    s_d3d.vp_vb_offset = 0;
    fetched_vertices = 0;
    upload_native_strip_vp_indexed(&reuse, 0, 8);
    assert(fetched_vertices == 4);
    puts("PASS: D3D WARP pixels match for front/back culling across strip cuts and packet boundaries");
    puts("PASS: native strips preserve oriented triangles and all 16 attributes across cuts and packets");
}
