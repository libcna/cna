// SPDX-License-Identifier: MS-PL
// Task 385: verify DualTextureEffect's Alpha property forwards correctly on
// Vulkan -- ALPHA CHANNEL ONLY, deliberately not the full RGB-premultiplication
// check that examples/easygl_dualtextureeffect_alpha_test.cpp does.
//
// Why not the full check: Vulkan's BlendState support is already known to be
// almost entirely fake (Task 868/870) -- every Vulkan pipeline in this
// codebase hardcodes colorBlendFactor=SRC_ALPHA/ONE_MINUS_SRC_ALPHA
// regardless of the GraphicsDevice.BlendState actually requested. Against a
// non-black background this would make a real end-to-end RGB-premultiplication
// pixel test spuriously fail on Vulkan for a reason entirely unrelated to
// DualTextureEffect's own correctness (already fixed/verified for EasyGL/Bgfx,
// and directly formula-verified for all 3 backends via the GPU-independent
// FillGpuDrawParams() unit tests in DualTextureEffectTests.cpp) -- writing
// that test here would misattribute an already-tracked, unrelated bug to
// this effect, the same anti-pattern Task 377/378 avoided by not committing
// a Vulkan/Bgfx test that would encode a known-confounded result as if it
// were meaningful coverage.
//
// What IS safe to check on Vulkan: every one of those hardcoded Vulkan blend
// pipelines uses srcAlphaBlendFactor=ONE / dstAlphaBlendFactor=ZERO for the
// ALPHA channel specifically (this part happens to already match
// BlendState.Opaque's real semantics) -- so with an opaque black background,
// the alpha channel readback is NOT confounded by the color-channel bug, and
// genuinely verifies DualTextureEffect forwards Alpha end-to-end through the
// real GPU dispatch, not just in FillGpuDrawParams()'s C++ formula.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanDualTextureAlphaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        const Color kBlack(0, 0, 0, 255);
        const Color kWhite(255, 255, 255, 255);

        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        DualTextureEffect fx(dev);
        fx.setTextureProperty(&texWhite);
        fx.setTexture2Property(&texWhite);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(0.5f);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        Color got = readCenter(dev);
        const bool pass = std::abs(got.getAProperty() - 128) <= 20;

        if (pass)
        {
            std::printf("[PASS] Alpha=0.5 forwards to output alpha channel: got.a=%d (expect≈128)\n",
                        got.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] Alpha=0.5 forwards to output alpha channel: got.a=%d, expected≈128\n",
                        got.getAProperty());
            result_ = 1;
        }
        Exit();
    }

public:
    VulkanDualTextureAlphaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanDualTextureAlphaTest game;
    game.Run();
    return game.getResult();
}
