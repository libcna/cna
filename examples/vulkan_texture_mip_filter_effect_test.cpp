// SPDX-License-Identifier: MS-PL
// Task 298/925: verify mipmap filter behavior (MipPoint/MipLinear/etc.) on a 3D stock effect
// (DualTextureEffect), Vulkan renderer.
//
// Vulkan-specific adaptation of examples/easygl_texture_mip_filter_effect_test.cpp (Task 298).
// Not a verbatim reuse: that file's own "Point" check asserts Point NEVER selects a higher mip
// level, documenting a real EasyGL-specific limitation (TextureFilter::Point maps to plain
// GL_NEAREST/GL_NEAREST with no _MIPMAP_ suffix at all, since EasyGL's own filter table hasn't
// been switched to a mip-aware GL enum yet). Vulkan's ApplySamplerState has never shared that
// limitation — Point has always mapped to VK_FILTER_NEAREST/VK_FILTER_NEAREST with
// VK_SAMPLER_MIPMAP_MODE_NEAREST (mip-aware nearest-level selection, matching real XNA/D3D9
// TextureFilter::Point semantics exactly: point filtering on all 3 axes, including mip
// selection — it was never "no mip selection at all"). Before Task 925, this was invisible
// because VkImageCreateInfo::mipLevels/VkImageViewCreateInfo::subresourceRange.levelCount were
// both hardcoded to 1 regardless of the texture's real level count, so there was no higher level
// for the sampler to select even though its own mapping already asked for one. Task 925 fixed
// that image-side allocation gap, which makes Point's pre-existing mip-aware mapping actually
// take effect for the first time — a genuine improvement in XNA-faithfulness on this renderer,
// not a regression to paper over. Both checks below therefore expect GREEN (real mip selection).
//
// Exit code 0 = PASS (both expectations hold), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState ss;
        ss.setFilterProperty(filter);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        return ss;
    }

    bool IsRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 40; }
    bool IsGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 40; }
}

class VulkanTextureMipFilterEffectTest : public Game
{
    Texture2D mipTex_;
    Texture2D whiteTex_;
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        mipTex_ = Texture2D(device, 128, 128, /*mipMap=*/true, SurfaceFormat::Color);
        for (int level = 0; level <= 7; ++level)
        {
            const int dim = 128 >> level;
            const bool red = level <= 2;
            std::vector<Color> px(static_cast<std::size_t>(dim) * dim,
                                   red ? Color(255, 0, 0, 255) : Color(0, 255, 0, 255));
            mipTex_.SetData(level, nullptr, px.data(), 0, static_cast<int>(px.size()));
        }

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    Color DrawTinyQuadAndSample(GraphicsDevice& device, TextureFilter filter,
                                float xLeftPx, int screenW, int screenH)
    {
        device.Clear(Color(0, 0, 255, 255)); // blue background (neither expected result)
        device.getSamplerStatesProperty()[0] = MakeSampler(filter);

        DualTextureEffect fx(device);
        fx.setTextureProperty(&mipTex_);
        fx.setTexture2Property(&whiteTex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const float xRightPx = xLeftPx + 8.0f; // 8x8-pixel quad: forces heavy minification
        const float ndcLeft  = (xLeftPx  / static_cast<float>(screenW)) * 2.0f - 1.0f;
        const float ndcRight = (xRightPx / static_cast<float>(screenW)) * 2.0f - 1.0f;
        const VertexPositionTexture verts[6] = {
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcLeft,  -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcRight,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(static_cast<int>(xLeftPx) + 4, screenH / 2, 1, 1);
        Color c(0, 0, 0, 0);
        device.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        const Color mipAware = DrawTinyQuadAndSample(device, TextureFilter::LinearMipPoint, 100.0f, W, H);
        const Color pointMip = DrawTinyQuadAndSample(device, TextureFilter::Point, 300.0f, W, H);

        const bool mipAwareOk = IsGreen(mipAware);
        const bool pointMipOk = IsGreen(pointMip);

        std::printf("[%s] LinearMipPoint at 8x8px (128->1 texture): sample=(%d,%d,%d), expect GREEN (high mip selected)\n",
                    mipAwareOk ? "PASS" : "FAIL",
                    mipAware.getRProperty(), mipAware.getGProperty(), mipAware.getBProperty());
        std::printf("[%s] Point at 8x8px (same texture): sample=(%d,%d,%d), expect GREEN (Vulkan's Point mapping is already mip-aware)\n",
                    pointMipOk ? "PASS" : "FAIL",
                    pointMip.getRProperty(), pointMip.getGProperty(), pointMip.getBProperty());

        result_ = (mipAwareOk && pointMipOk) ? 0 : 1;
        std::printf("=== %d/2 PASS ===\n", (mipAwareOk ? 1 : 0) + (pointMipOk ? 1 : 0));
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanTextureMipFilterEffectTest game;
    game.Run();
    return game.getResult();
}
