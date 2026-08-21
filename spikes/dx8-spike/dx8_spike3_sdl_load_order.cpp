// DX8 follow-up spike: isolate why SDL_InitSubSystem(SDL_INIT_VIDEO) failed with "No displays
// available" against the shared ~/.wine-cna-d3d11 Wine prefix (which has dxgi.dll overridden to
// DXVK/native for the D3D9/D3D11/D3D12 backends). This binary calls SDL_Init as literally the
// FIRST statement in main() -- confirmed via a companion sdl-only build (no D3D imports in the .exe
// import table at all) that SDL3's own Windows video backend does an internal, dynamic
// LoadLibrary("dxgi.dll") call during SDL_InitSubSystem(SDL_INIT_VIDEO) (for HDR/colorimetry
// capability detection), entirely independent of whether the game itself ever touches Direct3D.
// Under Xvfb (no real monitor EDID), DXVK 2.6.0's own EDID/colorimetry-fallback code hits a real
// bug (an integer divide-by-zero, silently swallowed by Wine's SEH) that corrupts Wine's
// per-process monitor-enumeration state -- so ANY SDL3 program run in a prefix with `dxgi`
// overridden to DXVK fails here, not just ones using D3D8. Fix: DX8 needs its OWN dedicated prefix
// (~/.wine-cna-dx8) with d3d8/d3d9/d3d10/d3d10_1/d3d10core/d3d11 overridden to native (D8VK
// genuinely needs d3d9 native to function) but `dxgi` left as Wine's own builtin -- see
// scripts/run-wine-dx8.sh's own header comment for the exact one-time setup steps.
#include <SDL3/SDL.h>
#include <windows.h>
#include <d3d8.h>
#include <cstdio>

int main(int argc, char** argv) {
    printf("main() started\n");
    fflush(stdout);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        printf("SDL_InitSubSystem(SDL_INIT_VIDEO) FAILED: %s\n", SDL_GetError());
        fflush(stdout);
        return 1;
    }
    printf("SDL_InitSubSystem(SDL_INIT_VIDEO) OK\n");
    fflush(stdout);

    IDirect3D8* d3d8 = Direct3DCreate8(D3D_SDK_VERSION);
    printf("Direct3DCreate8 %s\n", d3d8 ? "OK" : "FAILED");
    return 0;
}
