// SPDX-License-Identifier: MS-PL
// REMED-GFX-092: deterministic first-failure coverage for D3D9 state, render-target, depth-
// stencil, and depth-surface-allocation HRESULT handling.  This deliberately uses the real
// Wine/DXVK9 device; the narrow renderer-local one-shot injection only substitutes a single
// checked HRESULT before that one native call, rather than attempting to destabilize the driver.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::DirectX9;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int passed = 0;
    int total = 0;

    void Check(bool value, const char* label)
    {
        ++total;
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", label);
        if (value) ++passed;
    }

    class ScopedNativeFailure
    {
    public:
        ScopedNativeFailure(DirectX9Renderer& renderer, D3D9NativeOperationEXT operation, HRESULT result)
            : renderer_(renderer)
        {
            renderer_.InjectNextNativeFailureEXT(operation, result);
        }

        ~ScopedNativeFailure()
        {
            renderer_.ClearNativeFailureInjectionEXT();
        }

        ScopedNativeFailure(const ScopedNativeFailure&) = delete;
        ScopedNativeFailure& operator=(const ScopedNativeFailure&) = delete;

    private:
        DirectX9Renderer& renderer_;
    };

    BlendWriteState AllWrites()
    {
        return {};
    }

    void ApplyBlendA(DirectX9Renderer& renderer)
    {
        renderer.ApplyBlendState(static_cast<int>(Blend::One), static_cast<int>(Blend::One),
                                static_cast<int>(Blend::Zero), static_cast<int>(Blend::Zero),
                                static_cast<int>(BlendFunction::Add),
                                static_cast<int>(BlendFunction::Add), AllWrites());
    }

    void ApplyBlendB(DirectX9Renderer& renderer)
    {
        renderer.ApplyBlendState(static_cast<int>(Blend::Zero), static_cast<int>(Blend::Zero),
                                static_cast<int>(Blend::One), static_cast<int>(Blend::One),
                                static_cast<int>(BlendFunction::Add),
                                static_cast<int>(BlendFunction::Add), AllWrites());
    }

    bool HasDiagnostic(const std::exception& exception, const char* operation, const char* result)
    {
        const std::string message = exception.what();
        return message.find(operation) != std::string::npos && message.find(result) != std::string::npos;
    }

    bool IsBound(DirectX9Renderer& renderer, IDirect3DSurface9* color, IDirect3DSurface9* depth)
    {
        Microsoft::WRL::ComPtr<IDirect3DSurface9> activeColor;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> activeDepth;
        const HRESULT colorHr = renderer.GetDeviceEXT()->GetRenderTarget(0, activeColor.GetAddressOf());
        const HRESULT depthHr = renderer.GetDeviceEXT()->GetDepthStencilSurface(activeDepth.GetAddressOf());
        return SUCCEEDED(colorHr) && SUCCEEDED(depthHr) &&
               activeColor.Get() == color && activeDepth.Get() == depth;
    }

    bool ClearSucceeds(DirectX9Renderer& renderer)
    {
        try
        {
            renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
            return true;
        }
        catch (...) { return false; }
    }

    void RunChecks(DirectX9Renderer& renderer)
    {
        // State A -> injected first native SetRenderState failure while requesting B -> A.  The
        // native source blend remains A, rendering is blocked while the category is unsafe, and a
        // complete successful A application retries the whole sequence before Clear is permitted.
        ApplyBlendA(renderer);
        DWORD sourceBlend = 0;
        const HRESULT baselineHr = renderer.GetDeviceEXT()->GetRenderState(D3DRS_SRCBLEND, &sourceBlend);
        const std::uint64_t before =
            renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::SetRenderState);

        bool stateThrew = false;
        bool stateDiagnostic = false;
        {
            ScopedNativeFailure failure(renderer, D3D9NativeOperationEXT::SetRenderState,
                                        D3DERR_INVALIDCALL);
            try { ApplyBlendB(renderer); }
            catch (const std::exception& exception)
            {
                stateThrew = true;
                stateDiagnostic = HasDiagnostic(exception, "IDirect3DDevice9::SetRenderState",
                                                "D3DERR_INVALIDCALL");
            }
            Check(!renderer.HasNativeFailureInjectionEXT(),
                  "GFX-092 fixture consumes exactly the injected SetRenderState failure");
        }
        const std::uint64_t afterFailure =
            renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::SetRenderState);
        DWORD afterFailureSourceBlend = 0;
        const HRESULT afterFailureHr =
            renderer.GetDeviceEXT()->GetRenderState(D3DRS_SRCBLEND, &afterFailureSourceBlend);
        const bool clearBlocked = !ClearSucceeds(renderer);

        ApplyBlendA(renderer);
        const std::uint64_t afterRetry =
            renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::SetRenderState);
        DWORD afterRetrySourceBlend = 0;
        const HRESULT afterRetryHr =
            renderer.GetDeviceEXT()->GetRenderState(D3DRS_SRCBLEND, &afterRetrySourceBlend);
        Check(stateThrew && stateDiagnostic && SUCCEEDED(baselineHr) &&
                  sourceBlend == D3DBLEND_ONE && SUCCEEDED(afterFailureHr) &&
                  afterFailureSourceBlend == D3DBLEND_ONE && clearBlocked &&
                  SUCCEEDED(afterRetryHr) && afterRetrySourceBlend == D3DBLEND_ONE &&
                  afterFailure == before && afterRetry > afterFailure && ClearSucceeds(renderer),
              "GFX-092 state INVALIDCALL blocks rendering, retains A, and A->failure->A retries");

        // The same state boundary maps both device-lost HRESULTs into the pre-existing D9-34 Lost
        // status/DeviceLostException route.  DebugRestoreContext exercises the established real
        // Reset path; explicit A reapplication clears the intentionally retained state-safety bit.
        for (const HRESULT lostResult : {D3DERR_DEVICELOST, D3DERR_DEVICENOTRESET})
        {
            bool lostException = false;
            {
                ScopedNativeFailure failure(renderer, D3D9NativeOperationEXT::SetRenderState, lostResult);
                try { ApplyBlendB(renderer); }
                catch (const DeviceLostException&) { lostException = true; }
            }
            const bool markedLost = renderer.IsDeviceLostEXT();
            renderer.DebugRestoreContext();
            ApplyBlendA(renderer);
            Check(lostException && markedLost && !renderer.IsDeviceLostEXT() && ClearSucceeds(renderer),
                  lostResult == D3DERR_DEVICELOST
                      ? "GFX-092 SetRenderState DEVICELOST follows D9-34 lost/reset lifecycle"
                      : "GFX-092 SetRenderState DEVICENOTRESET follows D9-34 lost/reset lifecycle");
        }

        // Allocation failure is injected after normal color resources are created but before the
        // D3D9 depth output can be consumed.  Construction throws, the one-shot is cleared, and a
        // later identical allocation/bind works, proving the failed output did not remain cached.
        bool allocationThrew = false;
        bool allocationDiagnostic = false;
        {
            ScopedNativeFailure failure(renderer, D3D9NativeOperationEXT::CreateDepthStencilSurface,
                                        D3DERR_OUTOFVIDEOMEMORY);
            try
            {
                auto discarded = renderer.CreateRenderTarget2D(
                    32, 32, static_cast<int>(DepthFormat::Depth24Stencil8));
                (void)discarded;
            }
            catch (const std::exception& exception)
            {
                allocationThrew = true;
                allocationDiagnostic = HasDiagnostic(
                    exception, "IDirect3DDevice9::CreateDepthStencilSurface", "D3DERR_OUTOFVIDEOMEMORY");
            }
        }
        auto a = renderer.CreateRenderTarget2D(32, 32, static_cast<int>(DepthFormat::Depth24Stencil8));
        auto b = renderer.CreateRenderTarget2D(32, 32, static_cast<int>(DepthFormat::Depth24Stencil8));
        auto& a9 = static_cast<D3D9RenderTargetRenderer&>(*a);
        auto& b9 = static_cast<D3D9RenderTargetRenderer&>(*b);
        renderer.SetRenderTarget2D(a.get());
        Check(allocationThrew && allocationDiagnostic && !renderer.HasNativeFailureInjectionEXT() &&
                  IsBound(renderer, a9.GetActiveColorSurfaceEXT(), a9.GetDepthStencilSurfaceEXT()),
              "GFX-092 depth allocation OOM is diagnostic, consumes no output, and later allocation is usable");

        // First color-bind failure: A remains both native and renderer-authoritative.  A valid B
        // transition immediately afterwards proves no target cache was committed on the failure.
        bool targetThrew = false;
        bool targetDiagnostic = false;
        {
            ScopedNativeFailure failure(renderer, D3D9NativeOperationEXT::SetRenderTarget,
                                        D3DERR_INVALIDCALL);
            try { renderer.SetRenderTarget2D(b.get()); }
            catch (const std::exception& exception)
            {
                targetThrew = true;
                targetDiagnostic = HasDiagnostic(exception, "IDirect3DDevice9::SetRenderTarget",
                                                 "D3DERR_INVALIDCALL");
            }
        }
        const bool targetRollback = IsBound(renderer, a9.GetActiveColorSurfaceEXT(),
                                            a9.GetDepthStencilSurfaceEXT()) && ClearSucceeds(renderer);
        renderer.SetRenderTarget2D(b.get());
        const bool targetRetry = IsBound(renderer, b9.GetActiveColorSurfaceEXT(),
                                         b9.GetDepthStencilSurfaceEXT()) && ClearSucceeds(renderer);
        Check(targetThrew && targetDiagnostic && targetRollback && targetRetry,
              "GFX-092 target INVALIDCALL restores A and a later A->B transition succeeds");

        // Color B may already be native-bound when depth binding fails.  The transaction restores
        // B's full color/depth/viewport tuple before throwing, then a later B->A succeeds.
        bool depthThrew = false;
        bool depthDiagnostic = false;
        {
            ScopedNativeFailure failure(renderer, D3D9NativeOperationEXT::SetDepthStencilSurface,
                                        D3DERR_INVALIDCALL);
            try { renderer.SetRenderTarget2D(a.get()); }
            catch (const std::exception& exception)
            {
                depthThrew = true;
                depthDiagnostic = HasDiagnostic(exception, "IDirect3DDevice9::SetDepthStencilSurface",
                                                "D3DERR_INVALIDCALL");
            }
        }
        const bool depthRollback = IsBound(renderer, b9.GetActiveColorSurfaceEXT(),
                                           b9.GetDepthStencilSurfaceEXT()) && ClearSucceeds(renderer);
        renderer.SetRenderTarget2D(a.get());
        const bool depthRetry = IsBound(renderer, a9.GetActiveColorSurfaceEXT(),
                                        a9.GetDepthStencilSurfaceEXT()) && ClearSucceeds(renderer);
        renderer.SetRenderTarget2D(nullptr);
        Check(depthThrew && depthDiagnostic && depthRollback && depthRetry && ClearSucceeds(renderer),
              "GFX-092 depth INVALIDCALL rolls back B, retries B->A, and restores the backbuffer");
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("d3d9_hresult", 64, 64, 0);
    if (!window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    try
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = SDL_GetWindowID(window);
        args.virtualWidth = 64;
        args.virtualHeight = 64;
        args.depthStencilFormat = static_cast<int>(DepthFormat::Depth24Stencil8);
        DirectX9Renderer renderer(args);
        RunChecks(renderer);
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "D3D9 HRESULT test setup failed: %s\n", exception.what());
        Check(false, "GFX-092 test setup");
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("=== %d/%d PASS (REMED-GFX-092 HRESULT checks) ===\n", passed, total);
    return passed == total ? 0 : 1;
}
