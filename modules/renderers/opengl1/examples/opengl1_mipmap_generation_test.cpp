// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: automatic mipmap generation (plans/plan_opengl1.md phase 6).
//
// OpenGL1TextureRenderer regenerates every mip level whenever level 0 is (re)uploaded and the
// texture was created with mipMap=true, via (in priority order) an explicit glGenerateMipmap()
// call, the older GL_GENERATE_MIPMAP/SGIS_generate_mipmap texture parameter, or a CPU box-filter
// fallback -- see OpenGL1Renderer.cpp's RegenerateMips()/GenerateMipsCPU(). ApplySamplerState
// also now maps XNA's full 9-value TextureFilter enum to the matching GL min/mag filter (including
// the _MIPMAP_ variants), the same mapping EasyGLRenderer already uses.
//
// This test cannot inspect the GPU-side mip levels directly (no public Texture2D API exposes a
// driver-auto-generated level's content -- Texture2D::GetData(level>0) only ever returns what a
// game explicitly wrote there). Instead it proves mip generation end-to-end the same way any real
// GPU's mipmapping is observable: a single-pixel checkerboard texture aliases sharply at native
// resolution but blurs to a flat, uniform mid-tone gray once minified far enough that the GPU's
// LOD computation selects a higher (correctly pre-averaged) mip level -- a checkerboard's 2x2 box
// average is EXACTLY uniform gray already at level 1 and stays that way at every level beyond,
// making this discriminator robust to exact driver LOD-selection/rounding differences.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 64;
    constexpr int kTexSize  = 8; // 8x8 checkerboard -> levels 8,4,2,1; level 1 is already exact gray.
}

class OpenGL1MipmapGenerationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kViewport / 2, kViewport / 2, 1, 1);
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
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Single-pixel checkerboard: every 2x2 block is exactly 2 black + 2 white texels, so the
        // box-filtered mip chain is EXACT uniform (127,127,127)-ish gray from level 1 onward,
        // regardless of the specific averaging kernel a driver/CPU-fallback implementation uses.
        Texture2D tex(dev, kTexSize, kTexSize, /*mipMap=*/true, SurfaceFormat::Color);
        const Color kWhite(255, 255, 255, 255), kBlack(0, 0, 0, 255);
        std::vector<Color> texels;
        texels.reserve(kTexSize * kTexSize);
        for (int y = 0; y < kTexSize; ++y)
            for (int x = 0; x < kTexSize; ++x)
                texels.push_back(((x + y) % 2 == 0) ? kWhite : kBlack);
        tex.SetData(texels.data(), kTexSize * kTexSize);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        auto drawQuadAndReadCenter = [&](float size, TextureFilter filter) -> Color
        {
            dev.Clear(Color(64, 64, 64, 255));
            const float half = size / 2.0f;
            const float cx = kViewport / 2.0f, cy = kViewport / 2.0f;
            const VertexPositionTexture q[6] = {
                { Vector3(cx - half, cy - half, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3(cx - half, cy + half, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(cx + half, cy + half, 0.0f), Vector2(1.0f, 1.0f) },
                { Vector3(cx - half, cy - half, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3(cx + half, cy + half, 0.0f), Vector2(1.0f, 1.0f) },
                { Vector3(cx + half, cy - half, 0.0f), Vector2(1.0f, 0.0f) },
            };

            dev.getSamplerStatesProperty()[0].setFilterProperty(filter);

            BasicEffect fx(dev);
            fx.setWorldProperty(world);
            fx.setViewProperty(view);
            fx.setProjectionProperty(proj);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAlphaProperty(1.0f);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            return ReadCenter(dev);
        };

        // Native size (8x8 world units == the texture's own resolution): LOD selects level 0, the
        // raw high-frequency checkerboard -- must NOT read as uniform gray.
        Color nativeGot = drawQuadAndReadCenter((float)kTexSize, TextureFilter::LinearMipPoint);
        std::printf("native (size=%d) got=(%d,%d,%d)\n", kTexSize,
                    nativeGot.getRProperty(), nativeGot.getGProperty(), nativeGot.getBProperty());
        const int nativeDeviationFromGray = std::abs(nativeGot.getRProperty() - 127);
        Check(nativeDeviationFromGray > 60,
              "native size (level 0): checkerboard aliases, center is far from mid-gray");

        // Minified 4x (2 world units, same 8x8 texture): LOD selects a higher mip level (level 2
        // for an exact 4x minification, though any level >=1 already reads as exact gray) -- must
        // read as uniform gray, proving the mip chain was actually generated and sampled.
        Color minifiedGot = drawQuadAndReadCenter(2.0f, TextureFilter::LinearMipPoint);
        std::printf("minified (size=2) got=(%d,%d,%d)\n",
                    minifiedGot.getRProperty(), minifiedGot.getGProperty(), minifiedGot.getBProperty());
        const int minifiedDeviationFromGray = std::abs(minifiedGot.getRProperty() - 127);
        Check(minifiedDeviationFromGray <= 20,
              "minified 4x + LinearMipPoint: samples a pre-averaged higher mip level, reads as mid-gray");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1MipmapGenerationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1MipmapGenerationTest game;
    game.Run();
    return game.getResult();
}
