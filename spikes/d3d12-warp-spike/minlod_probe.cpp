// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0022: which mip level does a point sampler select when
// D3D11_SAMPLER_DESC::MinLOD is very large -- measured on a real driver and on WARP, with no CNA in it.
//
// An 8x8 texture with four flat levels (red, green, blue, yellow) is drawn 1:1 into an 8x8 target with
// a point/point/point sampler, so the unclamped LOD is 0 and MinLOD alone decides the level. MaxLOD is
// FLT_MAX throughout. The centre pixel names the level.
//
// Build (MSVC developer environment):
//   cl /nologo /EHsc /O2 /std:c++17 /W4 minlod_probe.cpp /link d3d11.lib d3dcompiler.lib dxgi.lib
// Run: minlod_probe.exe            (hardware adapter, then WARP)

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

namespace
{
    template <typename T>
    struct Com
    {
        T* p = nullptr;
        ~Com() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        operator T*() const { return p; }
    };

    const char* kShader = R"(
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID)
{
    V o;
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? -3.0 : 1.0);
    o.pos = float4(p, 0, 1);
    o.uv = float2((p.x + 1) * 0.5, (1 - p.y) * 0.5);
    return o;
}
float4 ps(V i) : SV_Target { return t0.Sample(s0, i.uv); }
)";

    const char* LevelName(unsigned char r, unsigned char g, unsigned char b)
    {
        if (r == 255 && g == 0 && b == 0) return "level 0 (red)";
        if (r == 0 && g == 255 && b == 0) return "level 1 (green)";
        if (r == 0 && g == 0 && b == 255) return "level 2 (blue)";
        if (r == 255 && g == 255 && b == 0) return "level 3 (yellow)";
        if (r == 255 && g == 0 && b == 255) return "NOTHING DRAWN (clear colour)";
        return "unrecognised";
    }

    bool Run(D3D_DRIVER_TYPE driver, const char* label)
    {
        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> context;
        HRESULT hr = D3D11CreateDevice(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                       &device, nullptr, &context);
        if (FAILED(hr))
        {
            std::printf("%s: D3D11CreateDevice failed 0x%08lX\n", label, static_cast<unsigned long>(hr));
            return false;
        }
        Com<IDXGIDevice> dxgiDevice;
        Com<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC adapterDesc{};
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            adapter->GetDesc(&adapterDesc);
        std::printf("%s: adapter '%ls' vendor 0x%04X device 0x%04X\n", label, adapterDesc.Description,
                    adapterDesc.VendorId, adapterDesc.DeviceId);

        const unsigned char levels[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
        unsigned char data[4][64 * 4];
        D3D11_SUBRESOURCE_DATA init[4];
        for (int level = 0; level < 4; ++level)
        {
            const int size = 8 >> level;
            for (int i = 0; i < size * size; ++i)
                std::memcpy(data[level] + i * 4, levels[level], 4);
            init[level].pSysMem = data[level];
            init[level].SysMemPitch = static_cast<UINT>(size * 4);
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = 8;
        td.MipLevels = 4;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> texture;
        Com<ID3D11ShaderResourceView> srv;
        if (FAILED(device->CreateTexture2D(&td, init, &texture)) ||
            FAILED(device->CreateShaderResourceView(texture, nullptr, &srv)))
            return false;

        td.MipLevels = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        Com<ID3D11Texture2D> target;
        Com<ID3D11RenderTargetView> rtv;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &target)) ||
            FAILED(device->CreateRenderTargetView(target, nullptr, &rtv)))
            return false;
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &staging)))
            return false;

        Com<ID3DBlob> vsBlob, psBlob, errors;
        if (FAILED(D3DCompile(kShader, std::strlen(kShader), "probe", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsBlob, &errors)) ||
            FAILED(D3DCompile(kShader, std::strlen(kShader), "probe", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psBlob, &errors)))
        {
            std::printf("%s: shader compile failed\n", label);
            return false;
        }
        Com<ID3D11VertexShader> vs;
        Com<ID3D11PixelShader> ps;
        // The full-screen triangle is counter-clockwise; the default rasterizer state would cull it.
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> rasterizer;
        if (FAILED(device->CreateRasterizerState(&rd, &rasterizer)))
            return false;
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);

        const float minLods[] = {0.0f, 1.0f, 3.0f, 14.0f, 15.0f, 99.0f, 1.0e6f, 1.0e30f, FLT_MAX};
        for (float minLod : minLods)
        {
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxAnisotropy = 1;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MinLOD = minLod;
            sd.MaxLOD = FLT_MAX;
            Com<ID3D11SamplerState> sampler;
            if (FAILED(device->CreateSamplerState(&sd, &sampler)))
            {
                std::printf("%s: MinLOD %g: CreateSamplerState failed\n", label, minLod);
                continue;
            }
            ID3D11RenderTargetView* rtvs[1] = {rtv.p};
            context->OMSetRenderTargets(1, rtvs, nullptr);
            const float sentinel[4] = {1.0f, 0.0f, 1.0f, 1.0f}; // magenta: "nothing was drawn"
            context->ClearRenderTargetView(rtv, sentinel);
            context->RSSetState(rasterizer);
            D3D11_VIEWPORT viewport{0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f};
            context->RSSetViewports(1, &viewport);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetInputLayout(nullptr);
            context->VSSetShader(vs, nullptr, 0);
            context->PSSetShader(ps, nullptr, 0);
            ID3D11ShaderResourceView* srvs[1] = {srv.p};
            context->PSSetShaderResources(0, 1, srvs);
            ID3D11SamplerState* samplers[1] = {sampler.p};
            context->PSSetSamplers(0, 1, samplers);
            context->Draw(3, 0);
            context->CopyResource(staging, target);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
                return false;
            const auto* row = static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * 4;
            const unsigned char* px = row + 4 * 4;
            std::printf("%s: MinLOD %-12g -> (%3u,%3u,%3u) %s\n", label, minLod, px[0], px[1], px[2],
                        LevelName(px[0], px[1], px[2]));
            context->Unmap(staging, 0);
        }
        return true;
    }
}

int main()
{
    const bool hardware = Run(D3D_DRIVER_TYPE_HARDWARE, "hardware");
    const bool warp = Run(D3D_DRIVER_TYPE_WARP, "warp");
    return hardware && warp ? 0 : 1;
}
