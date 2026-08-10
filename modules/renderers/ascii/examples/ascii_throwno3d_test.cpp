// SPDX-License-Identifier: MS-PL
// plan_ascii.md Phase G7 (ASCII-60): every 3D-pipeline IGraphicsRenderer method on
// AsciiRenderer uses the wrapper's own unsupported-3D policy. This test verifies its
// default Throw/null behavior; unsupported_3d_call_behavior_test.cpp covers WarnAndStub.
//
// Checks A-K -- each of the 11 directly-reachable 3D-pipeline entry points throws
//   std::runtime_error: ClearColorAndDepth, ClearDepth, ClearStencil, ClearDepthAndStencil,
//   ClearColorAndStencil, ClearColorDepthAndStencil, SetDepthTestEnabled, SetBlendEnabled,
//   SetDepthWriteEnabled, CreateVertexBuffer, CreateIndexBuffer16, CreateOcclusionQuery.
// Check L -- SupportsDepthStencil() is false.
// Check M -- CreateTexture3D/CreateTextureCube/CreateRenderTargetCube/CreateEffectRenderer all
//   preserve their established nullptr result under the default policy.
//
// DrawColoredPrimitives/DrawIndexedColoredPrimitives/DrawInstancedPrimitivesEx are covered by
// unsupported_3d_call_behavior_test.cpp, which can obtain real null-object buffers after switching
// to WarnAndStub. They are structurally unreachable under this test's default Throw policy because
// CreateVertexBuffer/CreateIndexBuffer16 throw first.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/Ascii/AsciiRenderer.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Ascii;

namespace
{
    template <typename Fn>
    bool Throws(Fn&& fn)
    {
        try { fn(); }
        catch (const std::runtime_error&) { return true; }
        catch (...) { return false; }
        return false;
    }
}

class AsciiThrowNo3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
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
        auto& renderer = static_cast<AsciiRenderer&>(dev.GetRenderer());

        check(Throws([&] { renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f); }), "ClearColorAndDepth throws");
        check(Throws([&] { renderer.ClearDepth(1.0f); }), "ClearDepth throws");
        check(Throws([&] { renderer.ClearStencil(0); }), "ClearStencil throws");
        check(Throws([&] { renderer.ClearDepthAndStencil(1.0f, 0); }), "ClearDepthAndStencil throws");
        check(Throws([&] { renderer.ClearColorAndStencil(0, 0, 0, 1, 0); }), "ClearColorAndStencil throws");
        check(Throws([&] { renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0); }), "ClearColorDepthAndStencil throws");
        check(Throws([&] { renderer.SetDepthTestEnabled(true); }), "SetDepthTestEnabled throws");
        check(Throws([&] { renderer.SetBlendEnabled(true); }), "SetBlendEnabled throws");
        check(Throws([&] { renderer.SetDepthWriteEnabled(true); }), "SetDepthWriteEnabled throws");
        check(Throws([&] { renderer.CreateVertexBuffer(3); }), "CreateVertexBuffer throws");
        check(Throws([&] { renderer.CreateIndexBuffer16(3); }), "CreateIndexBuffer16 throws");
        check(Throws([&] { renderer.CreateOcclusionQuery(); }), "CreateOcclusionQuery throws");

        check(!renderer.SupportsDepthStencil(), "SupportsDepthStencil() is false");

        check(renderer.CreateTexture3D(4, 4, 4, false, 0) == nullptr, "CreateTexture3D returns nullptr");
        check(renderer.CreateTextureCube(4, false, 0) == nullptr, "CreateTextureCube returns nullptr");
        check(renderer.CreateRenderTargetCube(4, 0) == nullptr, "CreateRenderTargetCube returns nullptr");
        check(renderer.CreateEffectRenderer("vs", "fs") == nullptr, "CreateEffectRenderer returns nullptr");

        std::printf("=== %d/%d PASS ===\n", passCount_, 17);
        result_ = (passCount_ == 17) ? 0 : 1;
        Exit();
    }

public:
    AsciiThrowNo3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    AsciiThrowNo3DTest game;
    game.Run();
    return game.getResult();
}
