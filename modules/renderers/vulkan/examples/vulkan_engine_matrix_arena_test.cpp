// SPDX-License-Identifier: MS-PL
// plans/plan_street.md STREET-0005: a ShaderEffect whose engine matrices change on every draw must
// not allocate device memory on every draw.
//
// The engine-layer programs (shadow casters, the depth/normal prepass) take their world matrix
// from binding 19, a block of named engine matrices. Setting `uWorld` used to mark the effect's
// whole uniform-array block dirty, and the next draw copied all of it into a freshly created
// VkBuffer -- one vkAllocateMemory per draw. cna-street's shadow pass is ~900 caster draws a
// cascade: 14.8 s a frame on RADV, and a 29-probe reflection bake of ten minutes. The matrices
// are now suballocated from a shared arena.
//
// 1500 prepass draws in one frame, each placed in its own 4x4 cell of a 256x256 target by
// its `uWorld` engine matrix alone:
//
//   A  every cell was written -- each draw really used its own world matrix
//   B  the frame retired at most a handful of buffers, not one per draw
//   C  the validation layer stayed silent
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = skipped (no prepass on this device).

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize    = 256;
    constexpr int kCell    = 4;
    constexpr int kColumns = kSize / kCell;
    constexpr int kDraws   = 1500;

    Matrix CellWorld(int cell)
    {
        const float x = static_cast<float>((cell % kColumns) * kCell);
        const float y = static_cast<float>((cell / kColumns) * kCell);
        return Matrix::CreateScale(static_cast<float>(kCell))
             * Matrix::CreateTranslation(x, y, -5.0f);
    }
}

class VulkanEngineMatrixArenaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    int result_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        DepthNormalPrepass prepass(dev, kSize, kSize);
        ShaderEffect* effect = prepass.getPrepassEffect();
        if (!prepass.isSupported(dev) || effect == nullptr || !effect->IsEffectValid()) {
            std::printf("[SKIP] the depth/normal prepass is unavailable on this device\n");
            result_ = 77;
            Exit();
            return;
        }

        // A unit square facing +X, so its encoded normal is nothing a cleared texel holds.
        const Vector3 n(1.0f, 0.0f, 0.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(0, 0, 0), n, Vector2(0, 0)}, {Vector3(0, 1, 0), n, Vector2(0, 1)},
            {Vector3(1, 1, 0), n, Vector2(1, 1)}, {Vector3(0, 0, 0), n, Vector2(0, 0)},
            {Vector3(1, 1, 0), n, Vector2(1, 1)}, {Vector3(1, 0, 0), n, Vector2(1, 0)},
        };
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6,
                        BufferUsage::None);
        vb.SetData(quad, 6);

        const Matrix view = Matrix::getIdentityProperty();
        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.1f, 100.0f);

        const std::uint64_t retiredBefore = Renderer().GetRetiredBufferCountEXT();
        for (int p = 0; p < prepass.getPassCount(); ++p) {
            prepass.begin(p, view, projection, 0.1f, 100.0f);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetVertexBuffer(&vb);
            for (int i = 0; i < kDraws; ++i) {
                const Matrix world = CellWorld(i);
                effect->Apply();
                effect->SetUniformMat4("uWorld", &world.M11);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            }
            prepass.end();
        }

        std::vector<Color> normals(static_cast<std::size_t>(kSize) * kSize);
        prepass.getNormalTexture()->GetData(normals.data(), static_cast<int>(normals.size()));
        const std::uint64_t retiredAfter = Renderer().GetRetiredBufferCountEXT();

        // Anything never drawn over is the cleared value; the last row of cells is never drawn.
        const Color cleared = normals[static_cast<std::size_t>(kSize - 2) * kSize + kSize - 2];
        int missing = 0;
        int firstMissing = -1;
        for (int i = 0; i < kDraws; ++i) {
            const int x = (i % kColumns) * kCell + kCell / 2;
            const int y = (i / kColumns) * kCell + kCell / 2;
            if (normals[static_cast<std::size_t>(y) * kSize + x] == cleared) {
                if (firstMissing < 0) firstMissing = i;
                ++missing;
            }
        }
        check(missing == 0, "A every draw landed in its own cell",
              std::to_string(kDraws - missing) + "/" + std::to_string(kDraws) + " cells written"
                  + (firstMissing >= 0 ? "; first missing draw " + std::to_string(firstMissing)
                                       : std::string()));

        const std::uint64_t retired = retiredAfter - retiredBefore;
        check(retired < 16, "B the per-draw world matrix did not cost a buffer per draw",
              std::to_string(retired) + " buffer(s) retired over "
                  + std::to_string(kDraws * prepass.getPassCount()) + " draws");

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "C no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        result_ = fail_ > 0 ? 1 : 0;
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanEngineMatrixArenaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // The engine layer is HiDef work (VMG-0005); a prepass target is a HiDef resource.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanEngineMatrixArenaTest g;
    g.Run();
    return g.getResult();
}
