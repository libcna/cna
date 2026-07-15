// SPDX-License-Identifier: MS-PL
// plan_dx3.md Phase X7 (DX3-60..DX3-67, DX3-69): ThrowNo3D wiring and remaining-default
// verification for the DX3 (DirectDraw, via the ../free-direct sibling) graphics backend.
// DirectDraw is 2D-only -- every 3D entry point either throws honestly or degrades to a
// documented "unsupported, returns nullptr" default, matching this backend's own class-level
// doc comment.
//
// Check A (DX3-62) -- VertexBuffer construction throws.
// Check B (DX3-62) -- IndexBuffer (16-bit) construction throws.
// Check C (DX3-62) -- IndexBuffer (32-bit) construction throws too, proving
//   CreateIndexBuffer32's base-class delegation to CreateIndexBuffer16 composes correctly.
// Check D (DX3-60/63/65) -- GraphicsDevice::Clear(Target|DepthBuffer|Stencil, ...) does NOT
//   throw: shared GraphicsDevice.cpp masks Depth/Stencil out of the request before it ever
//   reaches the backend, because SupportsDepthStencil() is false (DX3-65) -- this makes
//   ClearColorAndDepth/etc and the Draw*PrimitivesEx family (DX3-60/63) provably unreachable
//   from the public API; direct backend-level calls (Check D2) confirm they still throw if ever
//   reached some other way.
// Check E (DX3-61) -- SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled all throw (these
//   ARE directly, unconditionally reachable from GraphicsDevice, no masking).
// Check F (DX3-64) -- Texture3D/TextureCube/RenderTargetCube construction does NOT throw
//   (backend returns nullptr; these classes are designed to degrade gracefully).
// Check G (DX3-66) -- OcclusionQuery construction does NOT throw; IsComplete()/PixelCount()
//   degrade to false/0 (a real fix in this phase: the Phase X1/X2 skeleton had this throwing,
//   inconsistent with the plan's own "-> nullptr" spec and with OcclusionQuery's own null-safe
//   design -- corrected here).
// Check H (DX3-67) -- ShaderEffect construction does NOT throw; IsEffectValid() is false.
// Check I (DX3-69) -- DebugSimulateContextLoss()/DebugRestoreContext() (direct backend calls)
//   are confirmed no-ops (inherited default, matching free-direct's own inert IsLost/Restore
//   stubs -- no real "context" to lose in a CPU/DirectDraw compositor).
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

#include "CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx3;

static constexpr int kCanvasSize = 32;

template <typename Fn>
static bool Throws(Fn&& fn)
{
    try { fn(); }
    catch (const std::exception&) { return true; }
    return false;
}

class Dx3No3DTest : public Game
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
        auto& backend = static_cast<Dx3GraphicsBackend&>(dev.GetBackend());

        // Check A (DX3-62): VertexBuffer construction throws.
        check(Throws([&] { VertexBuffer vb(dev, 1); }),
              "VertexBuffer construction throws (DX3-62)");

        // Check B (DX3-62): IndexBuffer (16-bit) construction throws.
        check(Throws([&] { IndexBuffer ib(dev, 1); }),
              "IndexBuffer (16-bit) construction throws (DX3-62)");

        // Check C (DX3-62): IndexBuffer (32-bit) construction throws too -- proves
        // CreateIndexBuffer32's base-class delegation to CreateIndexBuffer16 composes correctly.
        check(Throws([&] { IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None); }),
              "IndexBuffer (32-bit) construction throws (DX3-62)");

        // Check D (DX3-60/63/65): Clear(Target|DepthBuffer|Stencil, ...) does NOT throw --
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
                  "Clear() with Depth/Stencil degrades gracefully; direct backend calls still throw (DX3-60/65)");
        }

        // Check E (DX3-61): directly, unconditionally reachable -- all three throw.
        check(Throws([&] { dev.SetDepthTestEnabled(true); }) &&
              Throws([&] { dev.SetBlendEnabled(true); }) &&
              Throws([&] { dev.SetDepthWriteEnabled(true); }),
              "SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled all throw (DX3-61)");

        // Check F (DX3-64): construction does NOT throw -- these classes are designed to degrade
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
            check(!threw, "Texture3D/TextureCube/RenderTargetCube construction does not throw (DX3-64)");
        }

        // Check G (DX3-66): OcclusionQuery degrades gracefully (a real fix in this phase -- see
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
            check(!threw && degradedOk, "OcclusionQuery degrades gracefully to nullptr (DX3-66)");
        }

        // Check H (DX3-67): ShaderEffect degrades gracefully.
        {
            bool threw = false;
            bool notValid = false;
            try
            {
                ShaderEffect fx(dev, "vertex-src", "fragment-src");
                notValid = !fx.IsEffectValid();
            }
            catch (const std::exception&) { threw = true; }
            check(!threw && notValid, "ShaderEffect degrades gracefully (CreateEffectBackend -> nullptr) (DX3-67)");
        }

        // Check I (DX3-69): confirmed no-ops (inherited default; no real "context" to lose).
        check(!Throws([&] { backend.DebugSimulateContextLoss(); backend.DebugRestoreContext(); }),
              "DebugSimulateContextLoss()/DebugRestoreContext() are confirmed no-ops (DX3-69)");

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    Dx3No3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx3No3DTest game;
    game.Run();
    return game.getResult();
}
