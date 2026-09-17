// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0024: point sampling a 2x2 texture one-to-one into a 2x2 target
// under CNA's XNA pixel-centre shift, per address mode -- measured on a real driver and on WARP, with
// no CNA in it.
//
// descriptor_capacity_contract_test draws a four-texel identity texture through BasicEffect onto a 2x2
// render target. D3DCommon::ApplyXnaPixelCenter moves clip-space geometry 63/64 of a pixel's NDC half
// width right and down, so D3D10+ pixel centres land just inside each texel (u, v = 1/256 and 129/256).
// Every point sampler must then reproduce the texels exactly, whatever its address mode. This probe
// repeats that draw with Point filtering and Clamp, Wrap and Mirror, with and without the shift.
//
// Build (MSVC developer environment):
//   cl /nologo /EHsc /O2 /std:c++17 /W4 pixelcenter_probe.cpp /link d3d11.lib d3dcompiler.lib dxgi.lib
// Run: pixelcenter_probe.exe       (hardware adapter, then WARP)

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

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

    // Two triangles covering NDC [-1,1]^2, uv (0,0) top-left to (1,1) bottom-right, translated by the
    // shift the constant buffer carries.
    const char* kShader = R"(
cbuffer Shift : register(b0) { float2 shift; float2 pad; };
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID)
{
    static const float2 corners[6] = { float2(-1, 1), float2(1, 1), float2(-1, -1),
                                       float2(-1, -1), float2(1, 1), float2(1, -1) };
    V o;
    float2 p = corners[id];
    o.pos = float4(p + shift, 0, 1);
    o.uv = float2((p.x + 1) * 0.5, (1 - p.y) * 0.5);
    return o;
}
float4 ps(V i) : SV_Target { return t0.Sample(s0, i.uv); }
)";

    const unsigned char kTexels[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}};

    int TexelIndex(const unsigned char* px)
    {
        for (int i = 0; i < 4; ++i)
            if (px[0] == kTexels[i][0] && px[1] == kTexels[i][1] && px[2] == kTexels[i][2])
                return i;
        return -1;
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

        unsigned char texels[16];
        for (int i = 0; i < 4; ++i)
            std::memcpy(texels + i * 4, kTexels[i], 4);
        D3D11_SUBRESOURCE_DATA init{texels, 8, 0};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = 2;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> texture;
        Com<ID3D11ShaderResourceView> srv;
        if (FAILED(device->CreateTexture2D(&td, &init, &texture)) ||
            FAILED(device->CreateShaderResourceView(texture, nullptr, &srv)))
            return false;

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
            std::printf("%s: shader compile failed: %s\n", label,
                        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
            return false;
        }
        Com<ID3D11VertexShader> vs;
        Com<ID3D11PixelShader> ps;
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> rasterizer;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Com<ID3D11Buffer> constants;
        if (FAILED(device->CreateRasterizerState(&rd, &rasterizer)) ||
            FAILED(device->CreateBuffer(&bd, nullptr, &constants)))
            return false;

        struct Mode { D3D11_TEXTURE_ADDRESS_MODE mode; const char* name; };
        const Mode modes[] = {{D3D11_TEXTURE_ADDRESS_CLAMP, "clamp"}, {D3D11_TEXTURE_ADDRESS_WRAP, "wrap"},
                              {D3D11_TEXTURE_ADDRESS_MIRROR, "mirror"}};
        // CNA: translation (63/64)/viewportWidth in NDC, i.e. 63/128 of a pixel; and the unshifted draw.
        const float shifts[] = {63.0f / 64.0f / 2.0f, 0.0f};
        bool allExact = true;
        for (float shift : shifts)
        {
            for (const Mode& u : modes)
            {
                for (const Mode& v : modes)
                {
                    D3D11_SAMPLER_DESC sd{};
                    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                    sd.AddressU = u.mode;
                    sd.AddressV = v.mode;
                    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                    sd.MaxAnisotropy = 1;
                    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
                    sd.MaxLOD = D3D11_FLOAT32_MAX;
                    Com<ID3D11SamplerState> sampler;
                    if (FAILED(device->CreateSamplerState(&sd, &sampler)))
                        return false;

                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                    const float data[4] = {shift, -shift, 0.0f, 0.0f};
                    std::memcpy(mapped.pData, data, sizeof(data));
                    context->Unmap(constants, 0);

                    ID3D11RenderTargetView* rtvs[1] = {rtv.p};
                    context->OMSetRenderTargets(1, rtvs, nullptr);
                    const float sentinel[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                    context->ClearRenderTargetView(rtv, sentinel);
                    D3D11_VIEWPORT viewport{0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f};
                    context->RSSetViewports(1, &viewport);
                    context->RSSetState(rasterizer);
                    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    context->IASetInputLayout(nullptr);
                    context->VSSetShader(vs, nullptr, 0);
                    ID3D11Buffer* cbs[1] = {constants.p};
                    context->VSSetConstantBuffers(0, 1, cbs);
                    context->PSSetShader(ps, nullptr, 0);
                    ID3D11ShaderResourceView* srvs[1] = {srv.p};
                    context->PSSetShaderResources(0, 1, srvs);
                    ID3D11SamplerState* samplers[1] = {sampler.p};
                    context->PSSetSamplers(0, 1, samplers);
                    context->Draw(6, 0);
                    context->CopyResource(staging, target);
                    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
                        return false;
                    int got[4];
                    for (int y = 0; y < 2; ++y)
                        for (int x = 0; x < 2; ++x)
                            got[y * 2 + x] = TexelIndex(static_cast<const unsigned char*>(mapped.pData) +
                                                        mapped.RowPitch * y + 4 * x);
                    context->Unmap(staging, 0);
                    const bool exact = got[0] == 0 && got[1] == 1 && got[2] == 2 && got[3] == 3;
                    if (shift != 0.0f)
                        allExact = allExact && exact;
                    std::printf("%s: shift %-9g u %-6s v %-6s -> texels %2d %2d %2d %2d %s\n", label, shift,
                                u.name, v.name, got[0], got[1], got[2], got[3], exact ? "exact" : "WRONG");
                }
            }
        }
        std::printf("%s: CNA-shifted point sampling exact for every address mode: %s\n", label,
                    allExact ? "yes" : "NO");

        // The same 1:1 draw with the mixed filters. The footprint is one texel per pixel, so the LOD is
        // zero up to derivative precision, and which half of a MIN_x_MAG_y filter applies is decided by
        // that boundary: "exact" means the point half was used, "blended" the linear half.
        struct Filter { D3D11_FILTER filter; const char* name; };
        const Filter filters[] = {
            {D3D11_FILTER_MIN_MAG_MIP_POINT, "MIN_MAG_MIP_POINT"},
            {D3D11_FILTER_MIN_MAG_MIP_LINEAR, "MIN_MAG_MIP_LINEAR"},
            {D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR, "MIN_LINEAR_MAG_POINT_MIP_LINEAR"},
            {D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT, "MIN_LINEAR_MAG_MIP_POINT"},
            {D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR, "MIN_POINT_MAG_MIP_LINEAR"},
            {D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT, "MIN_POINT_MAG_LINEAR_MIP_POINT"},
        };
        for (float shift : shifts)
        {
            for (const Filter& f : filters)
            {
                D3D11_SAMPLER_DESC sd{};
                sd.Filter = f.filter;
                sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                sd.MaxAnisotropy = 1;
                sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
                sd.MaxLOD = D3D11_FLOAT32_MAX;
                Com<ID3D11SamplerState> sampler;
                if (FAILED(device->CreateSamplerState(&sd, &sampler)))
                    return false;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                const float data[4] = {shift, -shift, 0.0f, 0.0f};
                std::memcpy(mapped.pData, data, sizeof(data));
                context->Unmap(constants, 0);
                const float sentinel[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                context->ClearRenderTargetView(rtv, sentinel);
                ID3D11SamplerState* samplers[1] = {sampler.p};
                context->PSSetSamplers(0, 1, samplers);
                context->Draw(6, 0);
                context->CopyResource(staging, target);
                if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
                    return false;
                int got[4];
                const unsigned char* second = static_cast<const unsigned char*>(mapped.pData) + 4;
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x)
                        got[y * 2 + x] = TexelIndex(static_cast<const unsigned char*>(mapped.pData) +
                                                    mapped.RowPitch * y + 4 * x);
                std::printf("%s: shift %-9g %-32s -> texels %2d %2d %2d %2d, pixel (1,0) = (%3u,%3u,%3u) %s\n",
                            label, shift, f.name, got[0], got[1], got[2], got[3], second[0], second[1],
                            second[2], (got[0] == 0 && got[1] == 1 && got[2] == 2 && got[3] == 3) ? "exact" : "blended");
                context->Unmap(staging, 0);
            }
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
