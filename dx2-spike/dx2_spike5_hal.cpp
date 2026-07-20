// DX2-0 spike round 2: isolate whether basic rasterization works at all by submitting
// PRE-TRANSFORMED D3DTLVERTEX data directly (no D3DOP_PROCESSVERTICES step).
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
    wc.lpszClassName = L"Dx2Spike2Window";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2Spike2Window", L"DX2 Spike2",
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
    hr = rt->QueryInterface(IID_IDirect3DHALDevice, (void**)&device);
    PrintHr("QueryInterface(RGBDevice)", hr);

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

    // Pre-transformed D3DTLVERTEX: sx,sy directly in SCREEN pixel space, sz in [0,1], rhw=1.
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
    verts[0].sx = -1.0f; verts[0].sy = -1.0f; verts[0].sz = 0.5f; verts[0].rhw = 1.0f; verts[0].color = 0xFFFF0000;
    verts[1].sx = -1.0f; verts[1].sy = 3.0f;  verts[1].sz = 0.5f; verts[1].rhw = 1.0f; verts[1].color = 0xFF00FF00;
    verts[2].sx = 3.0f;  verts[2].sy = 3.0f;  verts[2].sz = 0.5f; verts[2].rhw = 1.0f; verts[2].color = 0xFF0000FF;

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
    texState->dwArg[0] = 0;
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

    // Diagnostic: force a real Blt-path sync into a fresh plain sysmem surface, in case
    // Lock() on the 3D-device render target returns a stale/unsynced sysmem shadow copy.
    DDSURFACEDESC copyDesc; memset(&copyDesc, 0, sizeof(copyDesc));
    copyDesc.dwSize = sizeof(copyDesc);
    copyDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    copyDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    copyDesc.dwWidth = 64; copyDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE copy = nullptr;
    hr = dd->CreateSurface(&copyDesc, &copy, nullptr);
    PrintHr("CreateSurface(copy)", hr);
    hr = copy->BltFast(0, 0, rt, nullptr, DDBLTFAST_WAIT);
    PrintHr("BltFast(rt->copy)", hr);

    DDSURFACEDESC copyReadDesc; memset(&copyReadDesc, 0, sizeof(copyReadDesc));
    copyReadDesc.dwSize = sizeof(copyReadDesc);
    hr = copy->Lock(nullptr, &copyReadDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(copy readback)", hr);
    if (SUCCEEDED(hr)) {
        auto* pixBase = (uint8_t*)copyReadDesc.lpSurface;
        for (int y = 8; y < 56; y += 6) {
            printf("copyrow %2d:", y);
            for (int x = 8; x < 56; x += 6) {
                uint8_t* p = pixBase + y * copyReadDesc.lPitch + x * 4;
                printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]);
            }
            printf("\n");
        }
        copy->Unlock(copyReadDesc.lpSurface);
    }

    printf("SPIKE2-DONE\n");
    return 0;
}
