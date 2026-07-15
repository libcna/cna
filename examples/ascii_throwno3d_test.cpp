// SPDX-License-Identifier: MS-PL
// plan_ascii.md Phase G7 (ASCII-60): every 3D-pipeline IGraphicsBackend method on
// AsciiGraphicsBackend forwards directly to the wrapped SdlGraphicsBackend's own existing
// ThrowNo3D calls rather than re-declaring them (design decision 10) -- this test proves it by
// calling each one directly on a real backend instance and confirming it throws.
//
// Checks A-K -- each of the 11 directly-reachable 3D-pipeline entry points throws
//   std::runtime_error: ClearColorAndDepth, ClearDepth, ClearStencil, ClearDepthAndStencil,
//   ClearColorAndStencil, ClearColorDepthAndStencil, SetDepthTestEnabled, SetBlendEnabled,
//   SetDepthWriteEnabled, CreateVertexBuffer, CreateIndexBuffer16, CreateOcclusionQuery.
// Check L -- SupportsDepthStencil() is false.
// Check M -- CreateTexture3D/CreateTextureCube/CreateRenderTargetCube/CreateEffectBackend all
//   return nullptr (the shared IGraphicsBackend defaults -- never overridden by SdlGraphicsBackend
//   either, so AsciiGraphicsBackend correctly leaves them un-overridden too, per design decision
//   2's "same net behavior, less code" choice).
//
// DrawColoredPrimitives/DrawIndexedColoredPrimitives/DrawInstancedPrimitivesEx are NOT exercised
// directly here: they need a real IVertexBufferBackend&, but CreateVertexBuffer/CreateIndexBuffer16
// already throw on this backend (Checks J/K), so no real buffer can ever exist to pass them --
// they are structurally unreachable via any real call path, not just untested. Confirmed correct
// by code review instead: both forward to inner_ with the exact same one-line pattern as every
// other method in this file, and SdlGraphicsBackend's own DrawColoredPrimitives/
// DrawIndexedColoredPrimitives already throw via ThrowNo3D (DrawInstancedPrimitivesEx isn't
// overridden by SdlGraphicsBackend either, so it uses IGraphicsBackend's own default throw).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Ascii;

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
        auto& backend = static_cast<AsciiGraphicsBackend&>(dev.GetBackend());

        check(Throws([&] { backend.ClearColorAndDepth(0, 0, 0, 1, 1.0f); }), "ClearColorAndDepth throws");
        check(Throws([&] { backend.ClearDepth(1.0f); }), "ClearDepth throws");
        check(Throws([&] { backend.ClearStencil(0); }), "ClearStencil throws");
        check(Throws([&] { backend.ClearDepthAndStencil(1.0f, 0); }), "ClearDepthAndStencil throws");
        check(Throws([&] { backend.ClearColorAndStencil(0, 0, 0, 1, 0); }), "ClearColorAndStencil throws");
        check(Throws([&] { backend.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0); }), "ClearColorDepthAndStencil throws");
        check(Throws([&] { backend.SetDepthTestEnabled(true); }), "SetDepthTestEnabled throws");
        check(Throws([&] { backend.SetBlendEnabled(true); }), "SetBlendEnabled throws");
        check(Throws([&] { backend.SetDepthWriteEnabled(true); }), "SetDepthWriteEnabled throws");
        check(Throws([&] { backend.CreateVertexBuffer(3); }), "CreateVertexBuffer throws");
        check(Throws([&] { backend.CreateIndexBuffer16(3); }), "CreateIndexBuffer16 throws");
        check(Throws([&] { backend.CreateOcclusionQuery(); }), "CreateOcclusionQuery throws");

        check(!backend.SupportsDepthStencil(), "SupportsDepthStencil() is false");

        check(backend.CreateTexture3D(4, 4, 4, false, 0) == nullptr, "CreateTexture3D returns nullptr");
        check(backend.CreateTextureCube(4, false, 0) == nullptr, "CreateTextureCube returns nullptr");
        check(backend.CreateRenderTargetCube(4, 0) == nullptr, "CreateRenderTargetCube returns nullptr");
        check(backend.CreateEffectBackend("vs", "fs") == nullptr, "CreateEffectBackend returns nullptr");

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
