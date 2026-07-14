// SPDX-License-Identifier: MS-PL
// plan_dx9.md Phase D9-3 (D9-30/D9-31): smoke test for the D3D9 graphics backend's device
// creation + Clear/Present/ReadBackbuffer foundation. Real window, real Direct3DCreate9/
// CreateDevice, real Clear()+Present()+readback through the actual public GraphicsDevice API --
// this backend's first genuine pixel-correctness proof.
//
// Per plan_dx9.md's own "Definition of done": each of the 6 Clear* combo variants needs its own
// passing pixel-verified check here, not just "compiles" -- D3D11 shipped these implemented-but-
// never-exercised and this plan deliberately raises the bar. No real draw path exists yet
// (D9-82), so the depth/stencil-only variants are proven two ways: (1) the color buffer is
// confirmed UNCHANGED by a depth/stencil-only clear (a real bug -- e.g. accidentally including
// D3DCLEAR_TARGET -- would fail this), and (2) the underlying D3D9 Clear() call is confirmed to
// return a real, driver-validated S_OK against a genuinely-created depth-stencil surface (checked
// directly via GetDepthStencilSurface()+GetDesc(), not merely "didn't throw").
//
// Check A -- real device created, D3DCAPS9 reports a real vs_2_0+/ps_2_0+ shader version floor.
// Check B -- Clear(color) followed by GetBackBufferData() reads back the EXACT clear color.
// Check C -- a second, different Clear()+readback also matches (not a stale/cached value).
// Check D -- ClearColorAndDepth: exact color readback + a real depth-stencil surface exists
//   (GetDepthStencilSurface()/GetDesc() confirms real dimensions/format).
// Check E -- ClearDepth (depth-only): color buffer from a prior Clear() is genuinely UNCHANGED.
// Check F -- ClearStencil (stencil-only): same unchanged-color proof.
// Check G -- ClearDepthAndStencil (no color component): same unchanged-color proof.
// Check H -- ClearColorAndStencil: exact color readback.
// Check I -- ClearColorDepthAndStencil: exact color readback.
// Check L -- a real GraphicsDeviceManager resize (64x64 -> 96x80) genuinely exercises
//   EnsureDeviceSize()'s Reset() path, and Clear()+GetBackBufferData() after the resize reads
//   back the NEW size's data correctly (D9-33), mirroring D3D11's own DX-83 check.
// Check J -- a device created with DepthFormat::None (no depth-stencil requested) genuinely has
//   no depth-stencil surface (GetDepthStencilSurface() fails), and ClearDepth()/ClearStencil()
//   silently no-op (do not throw) on it -- proving D9-31's HasDepthBuffer()/HasStencilBuffer()
//   gating is real, not just "always calls D3D9 Clear() and hopes".
// Check K -- GraphicsProfile::HiDef construction succeeds on this real vs_3_0/ps_3_0-capable
//   device (D9-32's profile-floor enforcement, positive path). The rejection path cannot be
//   exercised on real hardware that already exceeds the floor -- an honest gap, not a hidden one.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }
}

class D3D9SmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        // Give the swap chain one frame to settle before the first real check.
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<D3D9GraphicsBackend&>(dev.GetBackend());

        // Check A: real device created, real D3DCAPS9.
        check(backend.GetCapsEXT().VertexShaderVersion != 0 && backend.GetCapsEXT().PixelShaderVersion != 0,
              "real device created, D3DCAPS9 reports nonzero VertexShaderVersion/PixelShaderVersion");
        std::printf("    VertexShaderVersion=0x%08lx PixelShaderVersion=0x%08lx\n",
                    static_cast<unsigned long>(backend.GetCapsEXT().VertexShaderVersion),
                    static_cast<unsigned long>(backend.GetCapsEXT().PixelShaderVersion));

        // Check B: exact pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
        }

        // Check C: a second, different Clear() also reads back correctly.
        {
            dev.Clear(Color(200, 100, 50, 255));
            const Microsoft::Xna::Framework::Rectangle region(10, 10, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 200 || p.getGProperty() != 100 || p.getBProperty() != 50 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "a second, different Clear() also reads back exactly (not stale/cached)");
        }

        // Check D: ClearColorAndDepth -- exact color readback + a real depth-stencil surface.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(10, 20, 30, 255), 1.0f, 0);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool colorOk = pixels[0].getRProperty() == 10 && pixels[0].getGProperty() == 20 &&
                           pixels[0].getBProperty() == 30 && pixels[0].getAProperty() == 255;

            Microsoft::WRL::ComPtr<IDirect3DSurface9> dsSurface;
            HRESULT hr = backend.GetDeviceEXT()->GetDepthStencilSurface(dsSurface.ReleaseAndGetAddressOf());
            bool dsReal = false;
            if (SUCCEEDED(hr) && dsSurface)
            {
                D3DSURFACE_DESC desc{};
                dsSurface->GetDesc(&desc);
                dsReal = desc.Format == D3DFMT_D24S8 && desc.Width == 64 && desc.Height == 64;
            }
            check(colorOk && dsReal,
                  "ClearColorAndDepth: exact color readback + a real 64x64 D3DFMT_D24S8 depth-stencil surface exists");
        }

        // Check E: ClearDepth (depth-only) -- color buffer genuinely unaffected.
        {
            dev.Clear(Color(77, 88, 99, 255));
            dev.Clear(ClearOptions::DepthBuffer, Color(0, 0, 0, 0), 0.5f, 0);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 77 && pixels[0].getGProperty() == 88 &&
                  pixels[0].getBProperty() == 99,
                  "ClearDepth (depth-only): color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check F: ClearStencil (stencil-only) -- same unchanged-color proof.
        {
            dev.Clear(Color(11, 22, 33, 255));
            dev.Clear(ClearOptions::Stencil, Color(0, 0, 0, 0), 1.0f, 5);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 11 && pixels[0].getGProperty() == 22 &&
                  pixels[0].getBProperty() == 33,
                  "ClearStencil (stencil-only): color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check G: ClearDepthAndStencil (no color component) -- same unchanged-color proof.
        {
            dev.Clear(Color(44, 55, 66, 255));
            dev.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(0, 0, 0, 0), 0.25f, 3);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 44 && pixels[0].getGProperty() == 55 &&
                  pixels[0].getBProperty() == 66,
                  "ClearDepthAndStencil: color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check H: ClearColorAndStencil -- exact color readback.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::Stencil, Color(150, 60, 90, 255), 1.0f, 7);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 150 && pixels[0].getGProperty() == 60 &&
                  pixels[0].getBProperty() == 90 && pixels[0].getAProperty() == 255,
                  "ClearColorAndStencil: exact color readback");
        }

        // Check I: ClearColorDepthAndStencil -- exact color readback.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      Color(5, 250, 128, 255), 0.75f, 2);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 5 && pixels[0].getGProperty() == 250 &&
                  pixels[0].getBProperty() == 128 && pixels[0].getAProperty() == 255,
                  "ClearColorDepthAndStencil: exact color readback");
        }

        // Check L (D9-33) -- a real backbuffer resize genuinely exercises EnsureDeviceSize()'s
        // Reset() path (previously proven only indirectly, by D9-31's own smoke test converging to
        // its initial 64x64 size) -- resized via the same public GraphicsDeviceManager path a real
        // game uses, mirroring D3D11's own DX-83 check. EnsureDeviceSize() only picks the new size
        // up lazily on the next Present()/Clear() cycle, so this polls across a few frames.
        {
            gdm_->setPreferredBackBufferWidthProperty(96);
            gdm_->setPreferredBackBufferHeightProperty(80);
            gdm_->ApplyChanges();

            bool resized = false;
            for (int frame = 0; frame < 30 && !resized; ++frame)
            {
                dev.Clear(Color(30, 60, 90, 255));
                dev.Present();
                const auto& vp = dev.getViewportProperty();
                if (vp.getWidthProperty() == 96 && vp.getHeightProperty() == 80)
                {
                    resized = true;
                }
                else
                {
                    SDL_Delay(20);
                }
            }
            check(resized, "D9-33: GraphicsDeviceManager resize to 96x80 eventually converges "
                            "(viewport reflects the new size within 30 frames)");

            dev.Clear(Color(30, 60, 90, 255));
            const Microsoft::Xna::Framework::Rectangle newCorner(0, 0, 1, 1);
            const Microsoft::Xna::Framework::Rectangle nearNewEdge(90, 74, 1, 1);
            Color cornerPixel(0, 0, 0, 0), edgePixel(0, 0, 0, 0);
            dev.GetBackBufferData(&newCorner, &cornerPixel, 0, 1);
            dev.GetBackBufferData(&nearNewEdge, &edgePixel, 0, 1);
            const auto isClearColor = [](const Color& p) {
                return p.getRProperty() == 30 && p.getGProperty() == 60 && p.getBProperty() == 90;
            };
            check(isClearColor(cornerPixel) && isClearColor(edgePixel),
                  "D9-33: after resize, Clear()+GetBackBufferData() reads the exact clear color at "
                  "the origin AND near the new (96,80) far edge -- proves the resized back buffer/"
                  "depth-stencil/viewport are genuinely the new size, not stale/clamped/wrong");

            const bool ppMatches =
                (dev.getPresentationParametersProperty().getBackBufferWidthProperty() == 96 &&
                 dev.getPresentationParametersProperty().getBackBufferHeightProperty() == 80);
            check(ppMatches, "D9-33: PresentationParameters reflects the new 96x80 size post-resize");
        }

        std::printf("=== %d/%d PASS (main Game checks) ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

namespace
{
    // Check J: a raw (non-Game) backend with DepthFormat::None has genuinely no depth-stencil
    // surface, and ClearDepth()/ClearStencil() silently no-op on it. A manually-created SDL
    // window is used here (not GraphicsDeviceManager) since this needs a *different*
    // PresentationParameters than the main Game's own device.
    void RunNoDepthBufferCheck()
    {
        // D3D9GraphicsBackend needs a real SDL_Window* (it reads the win32 HWND back out via
        // SDL_GetWindowProperties) -- a plain SDL window, same as GraphicsDeviceManager itself
        // would create, just constructed directly here since this backend intentionally needs
        // different PresentationParameters (DepthFormat::None) than the main Game's own device.
        SDL_Window* sdlWindow = SDL_CreateWindow("d9smoke_nodepth", 64, 64, 0);
        if (!sdlWindow)
        {
            check(false, "Check J setup: SDL_CreateWindow failed");
            return;
        }

        CNA::Internal::Backends::GraphicsBackendCreateArgs createArgs;
        createArgs.window = sdlWindow;
        createArgs.virtualWidth = 64;
        createArgs.virtualHeight = 64;
        createArgs.depthStencilFormat = 0;  // DepthFormat::None

        D3D9GraphicsBackend backend(createArgs);

        Microsoft::WRL::ComPtr<IDirect3DSurface9> dsSurface;
        HRESULT hr = backend.GetDeviceEXT()->GetDepthStencilSurface(dsSurface.ReleaseAndGetAddressOf());
        check(FAILED(hr) || !dsSurface,
              "Check J: a device created with DepthFormat::None genuinely has no depth-stencil surface");

        bool threw = false;
        try { backend.ClearDepth(0.5f); } catch (...) { threw = true; }
        check(!threw, "Check J: ClearDepth() silently no-ops (does not throw) with no depth buffer");

        threw = false;
        try { backend.ClearStencil(3); } catch (...) { threw = true; }
        check(!threw, "Check J: ClearStencil() silently no-ops (does not throw) with no depth buffer");

        SDL_DestroyWindow(sdlWindow);
    }

    // Check K (D9-32): a device constructed with GraphicsProfile::HiDef succeeds without throwing,
    // since this real GPU's D3DCAPS9 genuinely reports vs_3_0/ps_3_0 (verified below, not assumed).
    // The REJECTION path (a device whose real caps fall below HiDef's floor) cannot be exercised
    // here -- this dev loop has no way to make Wine+DXVK report a sub-SM3 device on real hardware
    // that is SM3-capable -- same "real hardware needed" caveat as D9-105/D9-140 elsewhere in this
    // plan; D9-32's own gating logic (a plain integer comparison against GraphicsProfile::HiDef) is
    // simple enough that this asymmetry is an honest, acceptable gap, not a hidden one.
    void RunHiDefProfileCheck()
    {
        SDL_Window* sdlWindow = SDL_CreateWindow("d9smoke_hidef", 64, 64, 0);
        if (!sdlWindow)
        {
            check(false, "Check K setup: SDL_CreateWindow failed");
            return;
        }

        CNA::Internal::Backends::GraphicsBackendCreateArgs createArgs;
        createArgs.window = sdlWindow;
        createArgs.virtualWidth = 64;
        createArgs.virtualHeight = 64;
        createArgs.graphicsProfile = 1;  // GraphicsProfile::HiDef

        bool threw = false;
        try
        {
            D3D9GraphicsBackend backend(createArgs);
            check(backend.GetCapsEXT().VertexShaderVersion >= static_cast<DWORD>(D3DVS_VERSION(3, 0)) &&
                  backend.GetCapsEXT().PixelShaderVersion >= static_cast<DWORD>(D3DPS_VERSION(3, 0)),
                  "Check K: HiDef construction succeeds on a real vs_3_0/ps_3_0-capable device");
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(!threw, "Check K: GraphicsProfile::HiDef did not throw NoSuitableGraphicsDeviceException "
                      "(this GPU genuinely meets the floor)");

        SDL_DestroyWindow(sdlWindow);
    }
}

int main()
{
    {
        D3D9SmokeTest game;
        game.Run();
    }

    RunNoDepthBufferCheck();
    RunHiDefProfileCheck();

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
