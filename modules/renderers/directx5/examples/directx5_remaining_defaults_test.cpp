// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O7 (DX2-60..DX2-66): the remaining IGraphicsRenderer entry points that are
// genuinely unavailable at this DirectX era. None of these needed any DX2-specific code -- they
// were already satisfied by simply not overriding IGraphicsRenderer's own shared defaults
// (matching DIRECTX1/DIRECTX3's identical precedent), but this test proves that claim rather than just
// asserting it.
//
// Check A -- CreateOcclusionQuery() returns nullptr (occlusion queries are DX9-only).
// Check B -- CreateTexture3D/CreateTextureCube/CreateRenderTargetCube all return nullptr (volume/
//   cube textures are DIRECTX7/DIRECTX8+).
// Check C -- CreateEffectRenderer() returns nullptr (no programmable shaders exist at this era).
// Check D -- DrawInstancedPrimitivesEx throws (no instancing concept exists).
// Check E -- DebugSimulateContextLoss()/DebugRestoreContext() are no-ops (do not throw).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "CNA/Internal/Renderers/DirectX5/DirectX5Renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX5;
using CNA::Internal::Renderers::GpuDrawParams;

static constexpr int kCanvasSize = 64;

class DirectX5RemainingDefaultsTest : public Game
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
        auto& renderer = static_cast<DirectX5Renderer&>(dev.GetRenderer());

        check(renderer.CreateOcclusionQuery() == nullptr,
              "CreateOcclusionQuery() returns nullptr (occlusion queries are DX9-only)");

        check(renderer.CreateTexture3D(2, 2, 2, false, 0) == nullptr &&
              renderer.CreateTextureCube(2, false, 0) == nullptr &&
              renderer.CreateRenderTargetCube(2, 0) == nullptr,
              "CreateTexture3D/CreateTextureCube/CreateRenderTargetCube all return nullptr (DIRECTX7/DIRECTX8+ features)");

        check(renderer.CreateEffectRenderer("", "") == nullptr,
              "CreateEffectRenderer() returns nullptr (no programmable shaders at this era)");

        {
            auto vb = renderer.CreateVertexBuffer(3);
            auto ib = renderer.CreateIndexBuffer16(3);
            const Matrix identity = Matrix::getIdentityProperty();
            GpuDrawParams params;
            bool threw = false;
            try
            {
                renderer.DrawInstancedPrimitivesEx(*vb, *ib, identity, identity, identity,
                                                  PrimitiveType::TriangleList, 1, 2, params);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawInstancedPrimitivesEx throws (no instancing concept exists)");
        }

        {
            bool threw = false;
            try
            {
                renderer.DebugSimulateContextLoss();
                renderer.DebugRestoreContext();
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("DebugSimulateContextLoss/DebugRestoreContext threw: %s\n", e.what());
            }
            check(!threw, "DebugSimulateContextLoss()/DebugRestoreContext() are no-ops (do not throw)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 5);
        result_ = (passCount_ == 5) ? 0 : 1;
        Exit();
    }

public:
    DirectX5RemainingDefaultsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX5RemainingDefaultsTest game;
    game.Run();
    return game.getResult();
}
