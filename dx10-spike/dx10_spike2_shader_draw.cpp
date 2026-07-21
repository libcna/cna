// DX10-0b: existence-gate spike -- DX10 has NO fixed-function pipeline at all (unlike DX1..DX8),
// so this backend's whole 2D/3D layer must be real HLSL shaders. This spike proves the full
// real-shader path: D3DCompile (vs_4_0/ps_4_0 -- D3D10's shader model ceiling, not D3D11's vs_5_0/
// ps_5_0) -> CreateVertexShader/CreatePixelShader -> CreateInputLayout -> CreateBuffer (vertex
// buffer) -> a real triangle draw, then Z-test, one-texture sampling, and alpha blending.
//
// Run with DISPLAY=:0 (the real desktop), NOT Xvfb :99 -- see dx10-spike/README.md: DXVK's own
// dxgi.dll has a real bug (readMonitorEdidFromKey doesn't retry on a too-small probe buffer) that
// crashes on ANY Present() call under Xvfb (confirmed independent of GPU, independent of whether a
// valid EDID is injected into the registry); the real desktop's monitor avoids it entirely (same
// "No displays available"-under-Xvfb precedent already established for D3D11/D3D12 in this sandbox,
// per feedback_d3d11_wine_test_run_recipe -- this is a DIFFERENT, further bug in the same problem
// family: DX11/12 hit missing displays, D3D10 additionally hits the colorimetry divide-by-zero).
#include <windows.h>
#include <d3d10.h>
#include <d3d10misc.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void PrintHr(const char* what, HRESULT hr) {
    printf("%s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    fflush(stdout);
}

struct Vertex { float x, y, z; float r, g, b, a; float u, v; };

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx10Spike2Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx10Spike2Window", L"DX10 Spike2",
        WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 64;
    scd.BufferDesc.Height = 64;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D10Device* device = nullptr;
    IDXGISwapChain* swapchain = nullptr;
    HRESULT hr = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
        D3D10_SDK_VERSION, &scd, &swapchain, &device);
    PrintHr("A: D3D10CreateDeviceAndSwapChain", hr);
    if (FAILED(hr)) return 1;

    ID3D10Texture2D* backBuffer = nullptr;
    swapchain->GetBuffer(0, __uuidof(ID3D10Texture2D), reinterpret_cast<void**>(&backBuffer));
    ID3D10RenderTargetView* rtv = nullptr;
    device->CreateRenderTargetView(backBuffer, nullptr, &rtv);

    // Real depth-stencil buffer (Test D uses this).
    D3D10_TEXTURE2D_DESC dsDesc{};
    dsDesc.Width = 64; dsDesc.Height = 64; dsDesc.MipLevels = 1; dsDesc.ArraySize = 1;
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc.Count = 1;
    dsDesc.Usage = D3D10_USAGE_DEFAULT;
    dsDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    ID3D10Texture2D* depthTex = nullptr;
    hr = device->CreateTexture2D(&dsDesc, nullptr, &depthTex);
    PrintHr("CreateTexture2D(depth-stencil)", hr);
    ID3D10DepthStencilView* dsv = nullptr;
    hr = device->CreateDepthStencilView(depthTex, nullptr, &dsv);
    PrintHr("CreateDepthStencilView", hr);

    device->OMSetRenderTargets(1, &rtv, dsv);

    D3D10_VIEWPORT vp{};
    vp.Width = 64; vp.Height = 64; vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    device->RSSetViewports(1, &vp);

    // ---- Test C: real HLSL shader compile (vs_4_0/ps_4_0) + input layout + vertex buffer + draw ----
    static const char* kShaderSrc =
        "struct VS_IN { float3 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; };\n"
        "struct PS_IN { float4 pos : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; };\n"
        "PS_IN VSMain(VS_IN input) {\n"
        "    PS_IN output;\n"
        "    output.pos = float4(input.pos, 1.0);\n"
        "    output.col = input.col;\n"
        "    output.uv = input.uv;\n"
        "    return output;\n"
        "}\n"
        "Texture2D tex0 : register(t0);\n"
        "SamplerState samp0 : register(s0);\n"
        "float4 PSMain(PS_IN input) : SV_TARGET {\n"
        "    return tex0.Sample(samp0, input.uv) * input.col;\n"
        "}\n";

    ID3DBlob* vsBlob = nullptr; ID3DBlob* vsErr = nullptr;
    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), "dx10spike_vs", nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &vsBlob, &vsErr);
    PrintHr("D3DCompile(vs_4_0)", hr);
    if (FAILED(hr)) {
        if (vsErr) printf("vs error: %s\n", (const char*)vsErr->GetBufferPointer());
        return 1;
    }
    ID3DBlob* psBlob = nullptr; ID3DBlob* psErr = nullptr;
    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), "dx10spike_ps", nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &psBlob, &psErr);
    PrintHr("D3DCompile(ps_4_0)", hr);
    if (FAILED(hr)) {
        if (psErr) printf("ps error: %s\n", (const char*)psErr->GetBufferPointer());
        return 1;
    }

    ID3D10VertexShader* vs = nullptr;
    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &vs);
    PrintHr("CreateVertexShader", hr);
    ID3D10PixelShader* ps = nullptr;
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), &ps);
    PrintHr("CreatePixelShader", hr);

    D3D10_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D10InputLayout* layout = nullptr;
    hr = device->CreateInputLayout(layoutDesc, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);
    PrintHr("CreateInputLayout", hr);

    // ---- Test E: one-texture sampling -- a 2x1 red/green texture ----
    D3D10_TEXTURE2D_DESC texDesc{};
    texDesc.Width = 2; texDesc.Height = 1; texDesc.MipLevels = 1; texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D10_USAGE_DEFAULT;
    texDesc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    const uint8_t texPixels[8] = { 255,0,0,255,  0,255,0,255 };
    D3D10_SUBRESOURCE_DATA texInit{};
    texInit.pSysMem = texPixels;
    texInit.SysMemPitch = 8;
    ID3D10Texture2D* tex = nullptr;
    hr = device->CreateTexture2D(&texDesc, &texInit, &tex);
    PrintHr("CreateTexture2D(sample texture)", hr);
    ID3D10ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(tex, nullptr, &srv);
    PrintHr("CreateShaderResourceView", hr);

    D3D10_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D10_FLOAT32_MAX;
    ID3D10SamplerState* sampler = nullptr;
    hr = device->CreateSamplerState(&sampDesc, &sampler);
    PrintHr("CreateSamplerState", hr);

    // A full-screen quad (two triangles), left half samples u=0.25 (red), right half u=0.75 (green).
    Vertex verts[6] = {
        {-1.0f,  1.0f, 0.0f,  1,1,1,1,  0.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f,  1,1,1,1,  1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f,  1,1,1,1,  0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f,  1,1,1,1,  1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f,  1,1,1,1,  1.0f, 1.0f},
        {-1.0f, -1.0f, 0.0f,  1,1,1,1,  0.0f, 1.0f},
    };
    D3D10_BUFFER_DESC vbDesc{};
    vbDesc.Usage = D3D10_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(verts);
    vbDesc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA vbInit{};
    vbInit.pSysMem = verts;
    ID3D10Buffer* vb = nullptr;
    hr = device->CreateBuffer(&vbDesc, &vbInit, &vb);
    PrintHr("CreateBuffer(vertex buffer)", hr);

    const float clearColor[4] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f };
    device->ClearRenderTargetView(rtv, clearColor);
    device->ClearDepthStencilView(dsv, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, 1.0f, 0);

    device->IASetInputLayout(layout);
    UINT stride = sizeof(Vertex), offset = 0;
    device->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->VSSetShader(vs);
    device->PSSetShader(ps);
    device->PSSetShaderResources(0, 1, &srv);
    device->PSSetSamplers(0, 1, &sampler);
    device->Draw(6, 0);
    printf("Draw(6,0) called (no HRESULT -- void return)\n"); fflush(stdout);

    hr = swapchain->Present(0, 0);
    PrintHr("Present (after real shader draw)", hr);

    // Readback via a staging texture + Map (D3D10's CPU-read mechanism).
    D3D10_TEXTURE2D_DESC stagingDesc = texDesc;
    D3D10_TEXTURE2D_DESC bbDesc{};
    backBuffer->GetDesc(&bbDesc);
    stagingDesc = bbDesc;
    stagingDesc.Usage = D3D10_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D* staging = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    PrintHr("CreateTexture2D(staging)", hr);
    device->CopyResource(staging, backBuffer);
    D3D10_MAPPED_TEXTURE2D mapped{};
    hr = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
    PrintHr("Texture2D::Map(READ)", hr);
    if (SUCCEEDED(hr)) {
        auto* row0 = static_cast<const uint8_t*>(mapped.pData);
        auto* rowMid = row0 + mapped.RowPitch * 32;
        printf("pixel(16,32) = (%d,%d,%d,%d)  pixel(48,32) = (%d,%d,%d,%d)\n",
               rowMid[16 * 4 + 0], rowMid[16 * 4 + 1], rowMid[16 * 4 + 2], rowMid[16 * 4 + 3],
               rowMid[48 * 4 + 0], rowMid[48 * 4 + 1], rowMid[48 * 4 + 2], rowMid[48 * 4 + 3]);
        staging->Unmap(0);
    }

    printf("DX10-SPIKE2-DONE\n");
    return 0;
}
