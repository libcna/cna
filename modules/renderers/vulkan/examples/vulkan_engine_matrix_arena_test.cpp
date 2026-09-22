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
//   A  every cell holds the +X normal the quad writes, and a cell no draw reached does not --
//      each draw really used its own world matrix
//   B  the frame retired at most a handful of buffers, not one per draw
//   C  the validation layer stayed silent
//   D  (STREET-0006) the prepass lands where a stock effect draws the same geometry: the prepass
//      images used to be the scene mirrored top to bottom on Vulkan, which a test reading back
//      only the prepass cannot see
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = skipped (no prepass on this device).

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
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

        // The quads face +X, which the prepass encodes as (1, 0.5, 0.5); what it clears to is
        // nothing like that.
        const auto written = [&](int x, int y) {
            const Color c = normals[static_cast<std::size_t>(y) * kSize + x];
            return c.getRProperty() > 200 && c.getGProperty() > 100 && c.getGProperty() < 160
                && c.getBProperty() > 100 && c.getBProperty() < 160;
        };
        const auto centre = [](int cell, int& x, int& y) {
            x = (cell % kColumns) * kCell + kCell / 2;
            y = (cell / kColumns) * kCell + kCell / 2;
        };
        int missing = 0;
        int firstMissing = -1;
        for (int i = 0; i < kDraws; ++i) {
            int x = 0, y = 0;
            centre(i, x, y);
            if (!written(x, y)) {
                if (firstMissing < 0) firstMissing = i;
                ++missing;
            }
        }
        int lx = 0, ly = 0;
        centre(kColumns * kColumns - 1, lx, ly);   // the last cell; no draw reaches it
        check(missing == 0 && !written(lx, ly), "A every draw landed in its own cell",
              std::to_string(kDraws - missing) + "/" + std::to_string(kDraws) + " cells written"
                  + (firstMissing >= 0 ? "; first missing draw " + std::to_string(firstMissing)
                                       : std::string())
                  + (written(lx, ly) ? "; the undrawn last cell is written too" : ""));

        const std::uint64_t retired = retiredAfter - retiredBefore;
        check(retired < 16, "B the per-draw world matrix did not cost a buffer per draw",
              std::to_string(retired) + " buffer(s) retired over "
                  + std::to_string(kDraws * prepass.getPassCount()) + " draws");

        {
            // The same first quad through BasicEffect into an ordinary render target.
            RenderTarget2D stock(dev, kSize, kSize, false, SurfaceFormat::Color,
                                 DepthFormat::Depth24);
            dev.SetRenderTarget(&stock);
            dev.Clear(Color(0, 0, 0, 255));
            BasicEffect fx(dev);
            fx.setWorldProperty(CellWorld(0));
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetRenderTarget(nullptr);
            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize);
            stock.GetData(pixels.data(), static_cast<int>(pixels.size()));
            int x = 0, y = 0;
            centre(0, x, y);
            const bool stockHere =
                pixels[static_cast<std::size_t>(y) * kSize + x].getRProperty() > 128;
            const bool stockMirrored =
                pixels[static_cast<std::size_t>(kSize - 1 - y) * kSize + x].getRProperty() > 128;
            check(stockHere && !stockMirrored && written(x, y),
                  "D the prepass and a stock effect put the same quad in the same place",
                  std::string("stock ") + (stockHere ? "at" : "not at") + " row "
                      + std::to_string(y) + (stockMirrored ? " (and mirrored)" : "")
                      + ", prepass " + (written(x, y) ? "at" : "not at") + " it");
        }

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
