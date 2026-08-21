// DX2-0 spike round 4: test the hypothesis that an unbound texture stage 0 (D3DTLVERTEX
// always carries tu/tv, so the FF-emulation pipe treats stage 0 as "active") samples black
// instead of passing vertex color through, by binding a real 2x2 opaque WHITE texture.
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void PrintHr(const char* what, HRESULT hr) {
    printf("%s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL");
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Dx2Spike4Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike4Window", L"DX2 Spike4",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW dd = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &dd, nullptr);
    PrintHr("DirectDrawCreate", hr);
    dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);

    DDSURFACEDESC rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64; rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE rt = nullptr;
    hr = dd->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("CreateSurface(rt)", hr);

    DDSURFACEDESC zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64; zDesc.dwHeight = 64; zDesc.dwZBufferBitDepth = 16;
    LPDIRECTDRAWSURFACE zbuf = nullptr;
    hr = dd->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("CreateSurface(zbuf)", hr);
    rt->AddAttachedSurface(zbuf);

    LPDIRECT3D d3d = nullptr;
    dd->QueryInterface(IID_IDirect3D, (void**)&d3d);
    LPDIRECT3DDEVICE device = nullptr;
    hr = rt->QueryInterface(IID_IDirect3DRGBDevice, (void**)&device);
    PrintHr("QueryInterface(RGBDevice)", hr);

    // Create a tiny 2x2 opaque white texture and bind it via D3DRENDERSTATE_TEXTUREHANDLE.
    DDSURFACEDESC texDesc; memset(&texDesc, 0, sizeof(texDesc));
    texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    texDesc.dwWidth = 2; texDesc.dwHeight = 2;
    texDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    texDesc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    texDesc.ddpfPixelFormat.dwRGBBitCount = 32;
    texDesc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
    texDesc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
    texDesc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
    texDesc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
    LPDIRECTDRAWSURFACE texSurf = nullptr;
    hr = dd->CreateSurface(&texDesc, &texSurf, nullptr);
    PrintHr("CreateSurface(tex)", hr);

    DDSURFACEDESC texLock; memset(&texLock, 0, sizeof(texLock));
    texLock.dwSize = sizeof(texLock);
    hr = texSurf->Lock(nullptr, &texLock, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(tex)", hr);
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < 2; ++y) {
            auto* row = (uint32_t*)((uint8_t*)texLock.lpSurface + y * texLock.lPitch);
            row[0] = 0xFFFFFFFFu;
            row[1] = 0xFFFFFFFFu;
        }
        texSurf->Unlock(texLock.lpSurface);
    }

    LPDIRECT3DTEXTURE tex = nullptr;
    hr = texSurf->QueryInterface(IID_IDirect3DTexture, (void**)&tex);
    PrintHr("QueryInterface(IDirect3DTexture)", hr);
    D3DTEXTUREHANDLE texHandle = 0;
    hr = tex->GetHandle(device, &texHandle);
    PrintHr("GetHandle", hr);
    printf("texHandle=0x%08lx\n", (unsigned long)texHandle);

    LPDIRECT3DVIEWPORT viewport = nullptr;
    d3d->CreateViewport(&viewport, nullptr);
    device->AddViewport(viewport);
    D3DVIEWPORT vp; memset(&vp, 0, sizeof(vp));
    vp.dwSize = sizeof(vp);
    vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvScaleX = 32.0f; vp.dvScaleY = 32.0f;
    vp.dvMaxX = 1.0f; vp.dvMaxY = 1.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = viewport->SetViewport(&vp);
    PrintHr("SetViewport", hr);

    D3DRECT clearRect{0, 0, 64, 64};
    hr = viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
    PrintHr("Clear", hr);

    const int vertexCount = 3;
    const int instrBufSize = sizeof(D3DINSTRUCTION) + 4 * sizeof(D3DSTATE) +
                              sizeof(D3DINSTRUCTION) + sizeof(D3DTRIANGLE) + sizeof(D3DINSTRUCTION);
    D3DEXECUTEBUFFERDESC ebDesc; memset(&ebDesc, 0, sizeof(ebDesc));
    ebDesc.dwSize = sizeof(ebDesc);
    ebDesc.dwFlags = D3DDEB_BUFSIZE;
    ebDesc.dwBufferSize = sizeof(D3DTLVERTEX) * vertexCount + instrBufSize;
    LPDIRECT3DEXECUTEBUFFER execBuf = nullptr;
    hr = device->CreateExecuteBuffer(&ebDesc, &execBuf, nullptr);
    PrintHr("CreateExecuteBuffer", hr);

    D3DEXECUTEBUFFERDESC lockedDesc; memset(&lockedDesc, 0, sizeof(lockedDesc));
    lockedDesc.dwSize = sizeof(lockedDesc);
    hr = execBuf->Lock(&lockedDesc);
    PrintHr("Lock", hr);

    auto* base = (uint8_t*)lockedDesc.lpData;
    auto* verts = (D3DTLVERTEX*)base;
    memset(verts, 0, sizeof(D3DTLVERTEX) * vertexCount);
    verts[0].sx = -200.0f; verts[0].sy = -200.0f; verts[0].sz = 0.5f; verts[0].rhw = 1.0f; verts[0].color = 0xFFFF0000; verts[0].tu = 0.0f; verts[0].tv = 0.0f;
    verts[1].sx = -200.0f; verts[1].sy = 400.0f;  verts[1].sz = 0.5f; verts[1].rhw = 1.0f; verts[1].color = 0xFF00FF00; verts[1].tu = 0.0f; verts[1].tv = 1.0f;
    verts[2].sx = 400.0f;  verts[2].sy = 400.0f;  verts[2].sz = 0.5f; verts[2].rhw = 1.0f; verts[2].color = 0xFF0000FF; verts[2].tu = 1.0f; verts[2].tv = 1.0f;

    auto* instrPtr = base + sizeof(D3DTLVERTEX) * vertexCount;
    auto* cull_instr = (D3DINSTRUCTION*)instrPtr;
    cull_instr->bOpcode = D3DOP_STATERENDER;
    cull_instr->bSize = sizeof(D3DSTATE);
    cull_instr->wCount = 4;
    instrPtr += sizeof(D3DINSTRUCTION);
    auto* cullState = (D3DSTATE*)instrPtr;
    cullState->drstRenderStateType = D3DRENDERSTATE_CULLMODE;
    cullState->dwArg[0] = D3DCULL_NONE;
    instrPtr += sizeof(D3DSTATE);
    auto* zState = (D3DSTATE*)instrPtr;
    zState->drstRenderStateType = D3DRENDERSTATE_ZENABLE;
    zState->dwArg[0] = D3DZB_FALSE;
    instrPtr += sizeof(D3DSTATE);
    auto* lightState = (D3DSTATE*)instrPtr;
    lightState->drstRenderStateType = D3DRENDERSTATE_LIGHTING;
    lightState->dwArg[0] = FALSE;
    instrPtr += sizeof(D3DSTATE);
    auto* texState = (D3DSTATE*)instrPtr;
    texState->drstRenderStateType = D3DRENDERSTATE_TEXTUREHANDLE;
    texState->dwArg[0] = texHandle;
    instrPtr += sizeof(D3DSTATE);

    auto* tri_instr = (D3DINSTRUCTION*)instrPtr;
    tri_instr->bOpcode = D3DOP_TRIANGLE;
    tri_instr->bSize = sizeof(D3DTRIANGLE);
    tri_instr->wCount = 1;
    instrPtr += sizeof(D3DINSTRUCTION);
    auto* tri = (D3DTRIANGLE*)instrPtr;
    tri->wV1 = 0; tri->wV2 = 1; tri->wV3 = 2;
    tri->wFlags = D3DTRIFLAG_EDGEENABLETRIANGLE;
    instrPtr += sizeof(D3DTRIANGLE);
    auto* exit_instr = (D3DINSTRUCTION*)instrPtr;
    exit_instr->bOpcode = D3DOP_EXIT; exit_instr->bSize = 0; exit_instr->wCount = 0;

    hr = execBuf->Unlock();
    PrintHr("Unlock", hr);

    D3DEXECUTEDATA execData; memset(&execData, 0, sizeof(execData));
    execData.dwSize = sizeof(execData);
    execData.dwVertexOffset = 0;
    execData.dwVertexCount = vertexCount;
    execData.dwInstructionOffset = sizeof(D3DTLVERTEX) * vertexCount;
    execData.dwInstructionLength = instrBufSize;
    hr = execBuf->SetExecuteData(&execData);
    PrintHr("SetExecuteData", hr);

    hr = device->BeginScene();
    PrintHr("BeginScene", hr);
    hr = device->Execute(execBuf, viewport, D3DEXECUTE_UNCLIPPED);
    PrintHr("Execute", hr);
    hr = device->EndScene();
    PrintHr("EndScene", hr);

    D3DEXECUTEDATA gotData; memset(&gotData, 0, sizeof(gotData));
    gotData.dwSize = sizeof(gotData);
    execBuf->GetExecuteData(&gotData);
    printf("dsStatus.dwStatus=0x%08lx dwFlags=0x%08lx\n",
           (unsigned long)gotData.dsStatus.dwStatus, (unsigned long)gotData.dsStatus.dwFlags);

    DDSURFACEDESC readDesc; memset(&readDesc, 0, sizeof(readDesc));
    readDesc.dwSize = sizeof(readDesc);
    hr = rt->Lock(nullptr, &readDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(readback)", hr);
    if (SUCCEEDED(hr)) {
        auto* pixBase = (uint8_t*)readDesc.lpSurface;
        for (int y = 8; y < 56; y += 6) {
            printf("row %2d:", y);
            for (int x = 8; x < 56; x += 6) {
                uint8_t* p = pixBase + y * readDesc.lPitch + x * 4;
                printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]);
            }
            printf("\n");
        }
        rt->Unlock(readDesc.lpSurface);
    }
    printf("SPIKE4-DONE\n");
    return 0;
}
