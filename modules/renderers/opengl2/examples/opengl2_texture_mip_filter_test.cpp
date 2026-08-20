// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: verify real mipmap upload + mip-aware filter selection on OpenGL2 (Texture2D
// mip levels beyond 0 previously never reached the GPU at all -- IGraphicsRenderer::
// UpdatePixelsLevel() was never overridden on OpenGL2's Tex class, so it silently no-op'd via the
// shared base-class default; and GL_TEXTURE_MAX_LEVEL was never clamped to the texture's real
// level count, so even a correctly-uploaded mip chain would render solid black under a
// mip-aware GL filter (GL "incomplete mipmap texture" per spec) -- reuses
// examples/easygl_texture_mip_filter_effect_test.cpp's own scene, mip-level colouring, and
// expected values verbatim.
//
// A 128x128 mipMap texture has 8 levels (128,64,32,16,8,4,2,1). Levels 0-2 (128,64,32) are
// filled solid RED; levels 3-7 (16,8,4,2,1) are filled solid GREEN. The quad is drawn at a
// tiny 8x8-pixel on-screen size, forcing the GPU's automatic LOD calculation to
// log2(128/8)=4 -- solidly inside the GREEN range [3,7] with a wide safety margin on both
// sides against driver-specific derivative rounding.
//
// TextureFilter::LinearMipPoint is expected to sample the automatically-selected high mip level
// -> GREEN, proving real mip-level selection now works on OpenGL2 when a Mip-aware filter is
// requested.
//
// TextureFilter::Point is ALSO tested at the same tiny size, and (matching the EasyGL reference
// test's own documented, intentional CNA-wide convention -- "CNA does not generate mipmaps by
// default") is expected to stay RED: OpenGL2's ToGLMinMagFilter() deliberately keeps
// Point/Linear mapped to plain (non-mip) GL_NEAREST/GL_LINEAR, matching EasyGL's own default-case
// behavior, not a bug.
//
// Exit code 0 = PASS (both expectations hold), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
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

class OpenGL2TextureMipFilterTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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
        const Color pointOnly = DrawTinyQuadAndSample(device, TextureFilter::Point, 300.0f, W, H);

        const bool mipAwareOk = IsGreen(mipAware);
        const bool pointOnlyOk = IsRed(pointOnly);

        std::printf("[%s] LinearMipPoint at 8x8px (128->1 texture): sample=(%d,%d,%d), expect GREEN (high mip selected)\n",
                    mipAwareOk ? "PASS" : "FAIL",
                    mipAware.getRProperty(), mipAware.getGProperty(), mipAware.getBProperty());
        std::printf("[%s] Point at 8x8px (same texture): sample=(%d,%d,%d), expect RED (documented: Point never mip-selects on CNA)\n",
                    pointOnlyOk ? "PASS" : "FAIL",
                    pointOnly.getRProperty(), pointOnly.getGProperty(), pointOnly.getBProperty());

        result_ = (mipAwareOk && pointOnlyOk) ? 0 : 1;
        std::printf("=== %d/2 PASS ===\n", (mipAwareOk ? 1 : 0) + (pointOnlyOk ? 1 : 0));
        Exit();
    }

public:
    OpenGL2TextureMipFilterTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2TextureMipFilterTest game;
    game.Run();
    return game.getResult();
}
