// DX2-0 existence-gate spike (plans/plan_dx2.md, to be written). Deliberately NOT part of the CNA
// build -- throwaway, standalone proof that real DirectX 2 execute-buffer Direct3D works end to
// end via MinGW-w64 + Wine before any backend code is written.
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
    wc.lpszClassName = L"Dx2SpikeWindow";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"Dx2SpikeWindow", L"DX2 Spike",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { printf("SPIKE-FAIL: CreateWindowExW err=%lu\n", GetLastError()); return 1; }
    ShowWindow(hwnd, SW_SHOW);

    LPDIRECTDRAW dd = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &dd, nullptr);
    PrintHr("DirectDrawCreate", hr);
    if (FAILED(hr)) return 1;

    hr = dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    PrintHr("SetCooperativeLevel", hr);
    if (FAILED(hr)) return 1;

    // Real render-target surface for the 3D device: DDSCAPS_3DDEVICE + DDSCAPS_OFFSCREENPLAIN.
    DDSURFACEDESC rtDesc; memset(&rtDesc, 0, sizeof(rtDesc));
    rtDesc.dwSize = sizeof(rtDesc);
    rtDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    rtDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    rtDesc.dwWidth = 64;
    rtDesc.dwHeight = 64;
    LPDIRECTDRAWSURFACE rt = nullptr;
    hr = dd->CreateSurface(&rtDesc, &rt, nullptr);
    PrintHr("CreateSurface(render target, DDSCAPS_3DDEVICE)", hr);
    if (FAILED(hr)) return 1;

    // Z-buffer surface, attached to the render target.
    DDSURFACEDESC zDesc; memset(&zDesc, 0, sizeof(zDesc));
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    zDesc.dwWidth = 64;
    zDesc.dwHeight = 64;
    zDesc.dwZBufferBitDepth = 16;
    LPDIRECTDRAWSURFACE zbuf = nullptr;
    hr = dd->CreateSurface(&zDesc, &zbuf, nullptr);
    PrintHr("CreateSurface(zbuffer)", hr);
    if (SUCCEEDED(hr)) {
        hr = rt->AddAttachedSurface(zbuf);
        PrintHr("AddAttachedSurface(zbuffer)", hr);
    }

    LPDIRECT3D d3d = nullptr;
    hr = dd->QueryInterface(IID_IDirect3D, (void**)&d3d);
    PrintHr("QueryInterface(IID_IDirect3D)", hr);
    if (FAILED(hr)) return 1;

    LPDIRECT3DDEVICE device = nullptr;
    hr = rt->QueryInterface(IID_IDirect3DRGBDevice, (void**)&device);
    PrintHr("QueryInterface(IID_IDirect3DRGBDevice) on render target", hr);
    if (FAILED(hr)) return 1;

    LPDIRECT3DVIEWPORT viewport = nullptr;
    hr = d3d->CreateViewport(&viewport, nullptr);
    PrintHr("CreateViewport", hr);
    if (FAILED(hr)) return 1;

    hr = device->AddViewport(viewport);
    PrintHr("AddViewport", hr);

    D3DVIEWPORT vp; memset(&vp, 0, sizeof(vp));
    vp.dwSize = sizeof(vp);
    vp.dwX = 0; vp.dwY = 0; vp.dwWidth = 64; vp.dwHeight = 64;
    vp.dvScaleX = 32.0f; vp.dvScaleY = 32.0f; // 1 world unit = 32 pixels
    vp.dvMaxX = 1.0f; vp.dvMaxY = 1.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = viewport->SetViewport(&vp);
    PrintHr("SetViewport", hr);

    // Clear both target and z-buffer for real before drawing -- fresh surfaces have undefined
    // content, and a garbage z-buffer could fail every fragment's depth test silently.
    D3DRECT clearRect{0, 0, 64, 64};
    hr = viewport->Clear(1, &clearRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
    PrintHr("Viewport::Clear(target+zbuffer)", hr);

    // Real finding (round 2): D3DOP_PROCESSVERTICES has no implicit World/View/Projection state --
    // matrix handles must be created, set to real values, and explicitly bound via
    // D3DOP_STATETRANSFORM before any vertex processing. Identity for all 3 -- device-space
    // coordinates then equal raw vertex position, matching the viewport's own dvScaleX/Y math.
    D3DMATRIXHANDLE hWorld = 0, hView = 0, hProj = 0;
    hr = device->CreateMatrix(&hWorld); PrintHr("CreateMatrix(world)", hr);
    hr = device->CreateMatrix(&hView);  PrintHr("CreateMatrix(view)", hr);
    hr = device->CreateMatrix(&hProj);  PrintHr("CreateMatrix(proj)", hr);

    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    hr = device->SetMatrix(hWorld, &identity); PrintHr("SetMatrix(world, identity)", hr);
    hr = device->SetMatrix(hView, &identity);  PrintHr("SetMatrix(view, identity)", hr);
    hr = device->SetMatrix(hProj, &identity);  PrintHr("SetMatrix(proj, identity)", hr);

    // Execute buffer: 3 D3DLVERTEX vertices (already "lit" -- explicit vertex color, no dynamic
    // lighting) forming one triangle, D3DOP_STATETRANSFORM (bind the 3 identity matrices) +
    // D3DOP_PROCESSVERTICES + D3DOP_TRIANGLE + D3DOP_EXIT instruction stream.
    const int vertexCount = 3;
    const int instrBufSize =
        sizeof(D3DINSTRUCTION) + 3 * sizeof(D3DSTATE) +
        sizeof(D3DINSTRUCTION) + sizeof(D3DPROCESSVERTICES) +
        sizeof(D3DINSTRUCTION) + sizeof(D3DTRIANGLE) +
        sizeof(D3DINSTRUCTION); // D3DOP_EXIT, wCount=0, no payload

    D3DEXECUTEBUFFERDESC ebDesc; memset(&ebDesc, 0, sizeof(ebDesc));
    ebDesc.dwSize = sizeof(ebDesc);
    ebDesc.dwFlags = D3DDEB_BUFSIZE;
    ebDesc.dwBufferSize = sizeof(D3DLVERTEX) * vertexCount + instrBufSize;
    LPDIRECT3DEXECUTEBUFFER execBuf = nullptr;
    hr = device->CreateExecuteBuffer(&ebDesc, &execBuf, nullptr);
    PrintHr("CreateExecuteBuffer", hr);
    if (FAILED(hr)) return 1;

    D3DEXECUTEBUFFERDESC lockedDesc; memset(&lockedDesc, 0, sizeof(lockedDesc));
    lockedDesc.dwSize = sizeof(lockedDesc);
    hr = execBuf->Lock(&lockedDesc);
    PrintHr("ExecuteBuffer::Lock", hr);
    if (FAILED(hr)) return 1;

    auto* base = (uint8_t*)lockedDesc.lpData;
    auto* verts = (D3DLVERTEX*)base;
    memset(verts, 0, sizeof(D3DLVERTEX) * vertexCount);
    // A red, green, blue vertex triangle roughly centered in view (world coords, scale 32px/unit).
    verts[0].dvX = 0.0f;  verts[0].dvY = 0.6f;  verts[0].dvZ = 0.5f; verts[0].dcColor = 0x00FF0000; // Red (ARGB)
    verts[1].dvX = -0.6f; verts[1].dvY = -0.6f; verts[1].dvZ = 0.5f; verts[1].dcColor = 0x0000FF00; // Green
    verts[2].dvX = 0.6f;  verts[2].dvY = -0.6f; verts[2].dvZ = 0.5f; verts[2].dcColor = 0x000000FF; // Blue

    auto* instrPtr = base + sizeof(D3DLVERTEX) * vertexCount;

    auto* st_instr = (D3DINSTRUCTION*)instrPtr;
    st_instr->bOpcode = D3DOP_STATETRANSFORM;
    st_instr->bSize = sizeof(D3DSTATE);
    st_instr->wCount = 3;
    instrPtr += sizeof(D3DINSTRUCTION);
    auto* stWorld = (D3DSTATE*)instrPtr;
    stWorld->dtstTransformStateType = D3DTRANSFORMSTATE_WORLD;
    stWorld->dwArg[0] = hWorld;
    instrPtr += sizeof(D3DSTATE);
    auto* stView = (D3DSTATE*)instrPtr;
    stView->dtstTransformStateType = D3DTRANSFORMSTATE_VIEW;
    stView->dwArg[0] = hView;
    instrPtr += sizeof(D3DSTATE);
    auto* stProj = (D3DSTATE*)instrPtr;
    stProj->dtstTransformStateType = D3DTRANSFORMSTATE_PROJECTION;
    stProj->dwArg[0] = hProj;
    instrPtr += sizeof(D3DSTATE);

    auto* pv_instr = (D3DINSTRUCTION*)instrPtr;
    pv_instr->bOpcode = D3DOP_PROCESSVERTICES;
    pv_instr->bSize = sizeof(D3DPROCESSVERTICES);
    pv_instr->wCount = 1;
    instrPtr += sizeof(D3DINSTRUCTION);
    auto* pv = (D3DPROCESSVERTICES*)instrPtr;
    pv->dwFlags = D3DPROCESSVERTICES_TRANSFORM;
    pv->wStart = 0;
    pv->wDest = 0;
    pv->dwCount = vertexCount;
    pv->dwReserved = 0;
    instrPtr += sizeof(D3DPROCESSVERTICES);

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
    exit_instr->bOpcode = D3DOP_EXIT;
    exit_instr->bSize = 0;
    exit_instr->wCount = 0;

    hr = execBuf->Unlock();
    PrintHr("ExecuteBuffer::Unlock", hr);

    D3DEXECUTEDATA execData; memset(&execData, 0, sizeof(execData));
    execData.dwSize = sizeof(execData);
    execData.dwVertexOffset = 0;
    execData.dwVertexCount = vertexCount;
    execData.dwInstructionOffset = sizeof(D3DLVERTEX) * vertexCount;
    execData.dwInstructionLength = instrBufSize;
    hr = execBuf->SetExecuteData(&execData);
    PrintHr("ExecuteBuffer::SetExecuteData", hr);

    hr = device->BeginScene();
    PrintHr("BeginScene", hr);

    hr = device->Execute(execBuf, viewport, D3DEXECUTE_UNCLIPPED);
    PrintHr("Execute", hr);

    hr = device->EndScene();
    PrintHr("EndScene", hr);

    // Read back a pixel where the triangle's centroid should land: view center (32,32) roughly,
    // offset toward the red vertex (top).
    DDSURFACEDESC readDesc; memset(&readDesc, 0, sizeof(readDesc));
    readDesc.dwSize = sizeof(readDesc);
    hr = rt->Lock(nullptr, &readDesc, DDLOCK_WAIT, nullptr);
    PrintHr("Lock(render target for readback)", hr);
    if (SUCCEEDED(hr)) {
        auto* pixBase = (uint8_t*)readDesc.lpSurface;
        printf("Readback pixel format: bpp=%lu\n", (unsigned long)readDesc.ddpfPixelFormat.dwRGBBitCount);
        for (int y = 20; y < 44; y += 4) {
            printf("row %2d:", y);
            for (int x = 16; x < 48; x += 4) {
                uint8_t* p = pixBase + y * readDesc.lPitch + x * 4;
                printf(" (%3d,%3d,%3d)", p[2], p[1], p[0]); // guess R,G,B assuming BGRX
            }
            printf("\n");
        }
        rt->Unlock(readDesc.lpSurface);
    }

    printf("SPIKE-DONE\n");
    return 0;
}
