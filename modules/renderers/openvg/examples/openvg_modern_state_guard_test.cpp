// SPDX-License-Identifier: MS-PL
// Coverage for three related OPENVG "must fail explicitly, never silently" guards, exercised
// exclusively through PUBLIC GraphicsDevice APIs (not direct renderer calls):
//
//  * P0-6: every modern 3D Draw*/DrawIndexed*/DrawInstanced* entry point is rejected by the
//    earliest-possible guard (IGraphicsRenderer::Ensure3DSupported), before GraphicsDevice
//    touches any vertex/index buffer state or applies sampler state.
//  * P0-7: meaningful (non-default) DepthStencilState/RasterizerState is rejected; the benign
//    defaults SpriteBatch.Begin() and ordinary GraphicsDevice construction actually use
//    (DepthStencilState.None, RasterizerState.CullCounterClockwise) are accepted.
//  * P0-8: binding an unsupported RenderTarget2D is rejected TRANSACTIONALLY -- before any native
//    state changes, and GraphicsDevice's own render-target bookkeeping is left exactly as it was.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int pass = 0, fail = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass; else ++fail;
    }

    // True only if a std::runtime_error was thrown BEFORE any buffer-bound/effect-applied
    // validation -- i.e. its message names the renderer's own "does not support 3D" diagnostic,
    // not a generic "no vertex buffer is bound"/"no effect has been applied" message that would
    // fire SECOND if Ensure3DSupported() were a no-op.
    template <typename Fn>
    bool ThrowsEarly3DGuard(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const std::runtime_error& ex)
        {
            const std::string what = ex.what();
            return what.find("does not support 3D") != std::string::npos;
        }
        catch (...) { return false; }
    }

    template <typename Fn>
    bool Throws(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const std::exception&) { return true; }
    }

    template <typename Fn>
    bool DoesNotThrow(Fn&& fn)
    {
        try { fn(); return true; }
        catch (const std::exception& ex) { std::printf("    (unexpected: %s)\n", ex.what()); return false; }
    }
}

class OpenVgModernStateGuardTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        // ---- P0-6: earliest-possible 3D guard, through public GraphicsDevice APIs ----
        Check(ThrowsEarly3DGuard([&] { device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1); }),
              "DrawPrimitives rejected by the 3D guard before vertex-buffer validation");
        Check(ThrowsEarly3DGuard([&] { device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1); }),
              "DrawIndexedPrimitives rejected by the 3D guard before vertex/index-buffer validation");
        Check(ThrowsEarly3DGuard([&] { device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 2); }),
              "DrawInstancedPrimitives rejected by the 3D guard before vertex/index-buffer validation");

        struct V { float x, y, z, r, g, b, a; };
        V verts[3]{};
        Check(ThrowsEarly3DGuard([&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1); }),
              "DrawUserPrimitives rejected by the 3D guard before consuming the user vertex array");
        std::uint16_t idx[3] = {0, 1, 2};
        Check(ThrowsEarly3DGuard([&] {
                  device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 0, 3, idx, 0, 1);
              }),
              "DrawUserIndexedPrimitives rejected by the 3D guard before consuming the user arrays");

        // ---- P0-7: meaningful DepthStencilState/RasterizerState rejected; benign defaults accepted ----
        Check(DoesNotThrow([&] { device.setDepthStencilStateProperty(DepthStencilState::None); }),
              "DepthStencilState.None (SpriteBatch.Begin()'s own default) is accepted");
        Check(Throws([&] { device.setDepthStencilStateProperty(DepthStencilState::Default); }),
              "DepthStencilState.Default (DepthBufferEnable=true) is rejected: no depth buffer exists");
        Check(Throws([&] { device.setDepthStencilStateProperty(DepthStencilState::DepthRead); }),
              "DepthStencilState.DepthRead is rejected too: still requests depth testing");

        Check(DoesNotThrow([&] { device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise); }),
              "RasterizerState.CullCounterClockwise (GraphicsDevice's own device-default) is accepted");
        Check(DoesNotThrow([&] { device.setRasterizerStateProperty(RasterizerState::CullNone); }),
              "any CullMode is benign for 2D quads -- CullNone is accepted too");
        {
            RasterizerState wireframe;
            wireframe.setFillModeProperty(FillMode::WireFrame);
            Check(Throws([&] { device.setRasterizerStateProperty(wireframe); }),
                  "RasterizerState.FillMode.WireFrame is rejected: no unfilled-polygon draw path exists");
        }
        {
            RasterizerState biased;
            biased.setDepthBiasProperty(0.5f);
            Check(Throws([&] { device.setRasterizerStateProperty(biased); }),
                  "a non-zero DepthBias is rejected: no depth buffer exists for it to bias");
        }

        // ---- P0-8: unsupported RenderTarget2D binding is rejected transactionally ----
        {
            RenderTarget2D rt(device, 8, 8);
            Check(device.GetRenderTargets().empty(), "precondition: the backbuffer is bound before the attempt");
            bool threwNotSupported = false;
            try { device.SetRenderTarget(&rt); }
            catch (const System::NotSupportedException&) { threwNotSupported = true; }
            catch (const std::exception& ex) { std::printf("    (unexpected exception type: %s)\n", ex.what()); }
            Check(threwNotSupported,
                  "binding an unsupported RenderTarget2D throws System::NotSupportedException");
            Check(device.GetRenderTargets().empty(),
                  "the failed bind is transactional: GraphicsDevice still reports the backbuffer bound");
            // A draw issued right after the failed bind must still land on the real backbuffer,
            // not silently on nothing / a half-bound target.
            Check(DoesNotThrow([&] { device.Clear(Color(1, 2, 3, 255)); }),
                  "the device is still fully usable (targeting the backbuffer) after the rejected bind");
        }

        std::printf("=== %d/%d PASS ===\n", pass, pass + fail);
        Exit();
    }

public:
    OpenVgModernStateGuardTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }
};

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    OpenVgModernStateGuardTest game;
    game.Run();
    return fail == 0 ? 0 : 1;
}
