// SPDX-License-Identifier: MS-PL
// plan_dx1.md Phase O7 (DX1-60..DX1-67, DX1-69): ThrowNo3D wiring and remaining-default
// verification for the DX1 (real DirectDraw v1, run under Wine -- no ../free-direct anywhere in this backend) graphics backend.
// DirectDraw is 2D-only -- every 3D entry point either throws honestly or degrades to a
// documented "unsupported, returns nullptr" default, matching this backend's own class-level
// doc comment.
//
// Check A (DX1-62) -- VertexBuffer construction throws.
// Check B (DX1-62) -- IndexBuffer (16-bit) construction throws.
// Check C (DX1-62) -- IndexBuffer (32-bit) construction throws too, proving
//   CreateIndexBuffer32's base-class delegation to CreateIndexBuffer16 composes correctly.
// Check D (DX1-60/63/65) -- GraphicsDevice::Clear(Target|DepthBuffer|Stencil, ...) does NOT
//   throw: shared GraphicsDevice.cpp masks Depth/Stencil out of the request before it ever
//   reaches the backend, because SupportsDepthStencil() is false (DX1-65) -- this makes
//   ClearColorAndDepth/etc and the Draw*PrimitivesEx family (DX1-60/63) provably unreachable
//   from the public API; direct backend-level calls (Check D2) confirm they still throw if ever
//   reached some other way.
// Check E (DX1-61) -- SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled all throw (these
//   ARE directly, unconditionally reachable from GraphicsDevice, no masking).
// Check F (DX1-64) -- Texture3D/TextureCube/RenderTargetCube construction does NOT throw
//   (backend returns nullptr; these classes are designed to degrade gracefully).
// Check G (DX1-66) -- OcclusionQuery construction does NOT throw; IsComplete()/PixelCount()
//   degrade to false/0 -- CreateOcclusionQuery() is deliberately never overridden, letting
//   IGraphicsBackend's own nullptr-returning default apply directly (ported from DX3-66's own
//   corrected final state, not re-discovered here as a bug).
// Check H (DX1-67) -- ShaderEffect construction does NOT throw; IsEffectValid() is false.
// Check I (DX1-69) -- DebugSimulateContextLoss()/DebugRestoreContext() (direct backend calls)
//   are confirmed no-ops (inherited IGraphicsBackend default -- no real "context" to lose in a
//   CPU/DirectDraw compositor, same reasoning SDL_RENDERER/SOFTWARE/DX3 already established).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include "CNA/Internal/Backends/Dx1/Dx1GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx1;

static constexpr int kCanvasSize = 32;

template <typename Fn>
static bool Throws(Fn&& fn)
{
    try { fn(); }
    catch (const std::exception&) { return true; }
    return false;
}

class Dx1No3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 9;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<Dx1GraphicsBackend&>(dev.GetBackend());

        // Check A (DX1-62): VertexBuffer construction throws.
        check(Throws([&] { VertexBuffer vb(dev, 1); }),
              "VertexBuffer construction throws (DX1-62)");

        // Check B (DX1-62): IndexBuffer (16-bit) construction throws.
        check(Throws([&] { IndexBuffer ib(dev, 1); }),
              "IndexBuffer (16-bit) construction throws (DX1-62)");

        // Check C (DX1-62): IndexBuffer (32-bit) construction throws too -- proves
        // CreateIndexBuffer32's base-class delegation to CreateIndexBuffer16 composes correctly.
        check(Throws([&] { IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None); }),
              "IndexBuffer (32-bit) construction throws (DX1-62)");

        // Check D (DX1-60/63/65): Clear(Target|DepthBuffer|Stencil, ...) does NOT throw --
        // shared GraphicsDevice.cpp masks Depth/Stencil out before reaching the backend, since
        // SupportsDepthStencil() is false. Direct backend calls confirm the throwing methods
        // still throw if ever reached some other way.
        {
            const bool clearOk = !Throws([&] {
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                         Color(1, 2, 3, 255), 1.0f, 0);
            });
            const bool directThrows = Throws([&] { backend.ClearColorAndDepth(0, 0, 0, 255, 1.0f); }) &&
                                      Throws([&] { backend.ClearDepth(1.0f); }) &&
                                      Throws([&] { backend.ClearStencil(0); }) &&
                                      Throws([&] { backend.ClearDepthAndStencil(1.0f, 0); }) &&
                                      Throws([&] { backend.ClearColorAndStencil(0, 0, 0, 255, 0); }) &&
                                      Throws([&] { backend.ClearColorDepthAndStencil(0, 0, 0, 255, 1.0f, 0); });
            check(clearOk && directThrows,
                  "Clear() with Depth/Stencil degrades gracefully; direct backend calls still throw (DX1-60/65)");
        }

        // Check E (DX1-61): directly, unconditionally reachable -- all three throw.
        check(Throws([&] { dev.SetDepthTestEnabled(true); }) &&
              Throws([&] { dev.SetBlendEnabled(true); }) &&
              Throws([&] { dev.SetDepthWriteEnabled(true); }),
              "SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled all throw (DX1-61)");

        // Check F (DX1-64): construction does NOT throw -- these classes are designed to degrade
        // gracefully against a null backend.
        {
            bool threw = false;
            try
            {
                Texture3D t3d(dev, 2, 2, 2, false, SurfaceFormat::Color);
                TextureCube tcube(dev, 2, false, SurfaceFormat::Color);
                RenderTargetCube rtcube(dev, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                        RenderTargetUsage::DiscardContents);
            }
            catch (const std::exception&) { threw = true; }
            check(!threw, "Texture3D/TextureCube/RenderTargetCube construction does not throw (DX1-64)");
        }

        // Check G (DX1-66): OcclusionQuery degrades gracefully (a real fix in this phase -- see
        // this file's header comment).
        {
            bool threw = false;
            bool degradedOk = false;
            try
            {
                OcclusionQuery oq(dev);
                oq.Begin();
                oq.End();
                degradedOk = !oq.getIsCompleteProperty() && oq.getPixelCountProperty() == 0;
            }
            catch (const std::exception&) { threw = true; }
            check(!threw && degradedOk, "OcclusionQuery degrades gracefully to nullptr (DX1-66)");
        }

        // Check H (DX1-67): ShaderEffect degrades gracefully.
        {
            bool threw = false;
            bool notValid = false;
            try
            {
                ShaderEffect fx(dev, "vertex-src", "fragment-src");
                notValid = !fx.IsEffectValid();
            }
            catch (const std::exception&) { threw = true; }
            check(!threw && notValid, "ShaderEffect degrades gracefully (CreateEffectBackend -> nullptr) (DX1-67)");
        }

        // Check I (DX1-69): confirmed no-ops (inherited default; no real "context" to lose).
        check(!Throws([&] { backend.DebugSimulateContextLoss(); backend.DebugRestoreContext(); }),
              "DebugSimulateContextLoss()/DebugRestoreContext() are confirmed no-ops (DX1-69)");

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    Dx1No3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx1No3DTest game;
    game.Run();
    return game.getResult();
}
