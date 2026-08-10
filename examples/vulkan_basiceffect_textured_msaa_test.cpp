// SPDX-License-Identifier: MS-PL
// Task 904: verify Vulkan's GetOrCreatePipelineFogTex3D (the stride-20/24 textured pipeline bundle,
// Task 899) correctly selects the MSAA-aware render pass when backbuffer MSAA is engaged.
//
// Direct port of vulkan_basiceffect_texture_enabled_test.cpp (Task 366) with backbuffer MSAA
// forced via GraphicsDevice::RecreateRendererForMultiSampleCount(8) (the same CNAEXT test-only hook
// Task 878/879's MSAA test uses, since GraphicsDeviceManager.PreferMultiSampling never actually
// reaches the Vulkan renderer -- Task 902).
//
// Every other Vulkan 3D pipeline-creation function selects its render pass via
// `(msaa && renderPassMsaa_) ? renderPassMsaa_ : renderPass_` when colorAttachmentCount<=1 --
// GetOrCreatePipelineFogTex3D (stride 20 "textured3d"/stride 24 "colored_textured3d", the pipeline
// this exact BasicEffect+Texture2D+stride-20 combination dispatches to) instead unconditionally
// used `renderPass_` (a 1-sample render pass) regardless of `msaa`, even though this same
// function's own `ms.rasterizationSamples` a few lines earlier correctly computed `sampleCount_`
// (>1) for the MSAA case -- a genuine VkPipelineMultisampleStateCreateInfo/render-pass sample-count
// mismatch, dormant until now because no existing test combined backbuffer MSAA with a textured
// BasicEffect/DualTextureEffect draw (found while implementing Task 878/879, tracked as this task).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kTexColor(200, 100, 50, 255);
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

static const Color kExpected(160, 40, 30, 255);

class VulkanBasicEffectTexturedMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        // Force real backbuffer MSAA -- see this file's header comment for why this hook is
        // needed instead of GraphicsDeviceManager.PreferMultiSampling (Task 902).
        getGraphicsDeviceProperty().RecreateRendererForMultiSampleCount(8);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setDiffuseColorProperty(kDiffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture q[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected),
              "MSAA=8 + textured BasicEffect (stride 20): pixel == TextureColor*DiffuseColor",
              got, "(160,40,30)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanBasicEffectTexturedMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanBasicEffectTexturedMsaaTest game;
    game.Run();
    return game.getResult();
}
