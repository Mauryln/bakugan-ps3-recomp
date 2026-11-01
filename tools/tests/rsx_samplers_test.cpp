#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "rsx_commands.h"
#include "sampler_decode.inc"

static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static ID3DBlob* compile(const char* text, const char* profile)
{
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(text, strlen(text), nullptr, nullptr, nullptr,
        "main", profile, 0, 0, &code, &errors);
    if (FAILED(hr) && errors) fprintf(stderr, "%s\n", (char*)errors->GetBufferPointer());
    require(SUCCEEDED(hr), "compile sampler regression shader");
    if (errors) errors->Release();
    return code;
}

int main()
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "create WARP device");
    auto* vs_code = compile("float4 main(uint id:SV_VertexID):SV_Position {"
        "float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}", "vs_5_0");
    // UV 1.25 is the same point as .25 for a scrolling repeat texture.
    auto* ps_code = compile("Texture2D t:register(t0);SamplerState s:register(s0);"
        "float4 main():SV_Target{return t.SampleLevel(s,float2(1.25,.5),0);}", "ps_5_0");
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    require(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs)), "create VS");
    require(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps)), "create PS");
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 2; td.Height = td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const unsigned pixels[] = {0xFF0000FF, 0xFF00FF00}; // red, green
    D3D11_SUBRESOURCE_DATA data = {pixels, sizeof(pixels), 0};
    ID3D11Texture2D* texture = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    require(SUCCEEDED(device->CreateTexture2D(&td, &data, &texture)), "create scrolling texture");
    require(SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &srv)), "create SRV");
    td.Width = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &target)), "create target");
    require(SUCCEEDED(device->CreateRenderTargetView(target, nullptr, &rtv)), "create RTV");
    td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* readback = nullptr;
    require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &readback)), "create readback");
    context->VSSetShader(vs, nullptr, 0); context->PSSetShader(ps, nullptr, 0);
    context->PSSetShaderResources(0, 1, &srv); context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, 1, 1, 0, 1}; context->RSSetViewports(1, &vp);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (unsigned wrap : {1u, 3u, 2u}) {
        D3D12_SAMPLER_DESC decoded = rsx_sampler_desc(wrap | (wrap << 8) | (wrap << 16), 0x02022000, 0x80058000, 0);
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = (D3D11_FILTER)decoded.Filter;
        sd.AddressU = (D3D11_TEXTURE_ADDRESS_MODE)decoded.AddressU;
        sd.AddressV = (D3D11_TEXTURE_ADDRESS_MODE)decoded.AddressV;
        sd.AddressW = (D3D11_TEXTURE_ADDRESS_MODE)decoded.AddressW;
        sd.MinLOD = decoded.MinLOD; sd.MaxLOD = decoded.MaxLOD;
        sd.MaxAnisotropy = decoded.MaxAnisotropy;
        ID3D11SamplerState* sampler = nullptr;
        require(SUCCEEDED(device->CreateSamplerState(&sd, &sampler)), "create translated sampler");
        context->PSSetSamplers(0, 1, &sampler); context->Draw(3, 0);
        context->CopyResource(readback, target);
        D3D11_MAPPED_SUBRESOURCE map = {};
        require(SUCCEEDED(context->Map(readback, 0, D3D11_MAP_READ, 0, &map)), "read sampling result");
        const auto* p = (const unsigned char*)map.pData;
        require(p[wrap == 1 ? 0 : 1] == 255 && p[wrap == 1 ? 1 : 0] == 0,
            "scrolling UVs must repeat with WRAP and retain CLAMP/MIRROR edge behavior");
        context->Unmap(readback, 0); sampler->Release();
    }
    auto point = rsx_sampler_desc(0x30303, 0x01012000, 0x80058000, 0);
    require(point.Filter == D3D12_FILTER_MIN_MAG_MIP_POINT && point.MaxLOD == 0,
        "nonmip point textures preserve point filtering and base level");
    auto linear = rsx_sampler_desc(0x10101, 0x02062000, 0x80058000, 0);
    require(linear.Filter == D3D12_FILTER_MIN_MAG_MIP_LINEAR && linear.MaxLOD == 11,
        "trilinear textures preserve guest mip range");
    context->ClearState(); readback->Release(); rtv->Release(); target->Release();
    srv->Release(); texture->Release(); ps->Release(); vs->Release();
    ps_code->Release(); vs_code->Release(); context->Release(); device->Release();
    puts("PASS: scrolling water UVs, wrap/clamp/mirror, point/trilinear and LOD range");
}
