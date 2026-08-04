// SPDX-License-Identifier: MS-PL
// plan_dx2.md Phase O7 (DX2-60..DX2-66): the remaining IGraphicsBackend entry points that are
// genuinely unavailable at this DirectX era. None of these needed any DX2-specific code -- they
// were already satisfied by simply not overriding IGraphicsBackend's own shared defaults
// (matching DX1/DX3's identical precedent), but this test proves that claim rather than just
// asserting it.
//
// Check A -- CreateOcclusionQuery() returns nullptr (occlusion queries are DX9-only).
// Check B -- CreateTexture3D/CreateTextureCube/CreateRenderTargetCube all return nullptr (volume/
//   cube textures are DX7/DX8+).
// Check C -- CreateEffectBackend() returns nullptr (no programmable shaders exist at this era).
// Check D -- DrawInstancedPrimitivesEx throws (no instancing concept exists).
// Check E -- DebugSimulateContextLoss()/DebugRestoreContext() are no-ops (do not throw).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "CNA/Internal/Backends/Dx5/Dx5GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx5;
using CNA::Internal::Backends::GpuDrawParams;

static constexpr int kCanvasSize = 64;

class Dx5RemainingDefaultsTest : public Game
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
        auto& backend = static_cast<Dx5GraphicsBackend&>(dev.GetBackend());

        check(backend.CreateOcclusionQuery() == nullptr,
              "CreateOcclusionQuery() returns nullptr (occlusion queries are DX9-only)");

        check(backend.CreateTexture3D(2, 2, 2, false, 0) == nullptr &&
              backend.CreateTextureCube(2, false, 0) == nullptr &&
              backend.CreateRenderTargetCube(2, 0) == nullptr,
              "CreateTexture3D/CreateTextureCube/CreateRenderTargetCube all return nullptr (DX7/DX8+ features)");

        check(backend.CreateEffectBackend("", "") == nullptr,
              "CreateEffectBackend() returns nullptr (no programmable shaders at this era)");

        {
            auto vb = backend.CreateVertexBuffer(3);
            auto ib = backend.CreateIndexBuffer16(3);
            const Matrix identity = Matrix::getIdentityProperty();
            GpuDrawParams params;
            bool threw = false;
            try
            {
                backend.DrawInstancedPrimitivesEx(*vb, *ib, identity, identity, identity,
                                                  PrimitiveType::TriangleList, 1, 2, params);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawInstancedPrimitivesEx throws (no instancing concept exists)");
        }

        {
            bool threw = false;
            try
            {
                backend.DebugSimulateContextLoss();
                backend.DebugRestoreContext();
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
    Dx5RemainingDefaultsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx5RemainingDefaultsTest game;
    game.Run();
    return game.getResult();
}
