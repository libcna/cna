// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: verify that an ordinary, non-mipmapped Texture2D (Texture2D::CreateFromPixels,
// the overwhelmingly common case for real game textures) renders correctly when sampled with a
// mipmap-requiring TextureFilter (Anisotropic), instead of solid black -- reuses
// examples/easygl_texture2d_anisotropic_singlelevel_test.cpp's own scene and reasoning verbatim.
//
// Direct sibling of opengl2_texture_mip_filter_test.cpp (multi-level mip content correctness):
// this test exercises the single-level (mipMap=false) path specifically, proving OpenGL2's Tex
// constructor's GL_TEXTURE_MAX_LEVEL clamp (added alongside UpdatePixelsLevel() support) makes
// every texture -- not just explicitly mipmapped ones -- a GL-spec-complete mipmap chain under a
// Mip-suffixed TextureFilter, and that this task's ApplySamplerState() MaxAnisotropy plumbing
// (previously silently discarded) doesn't itself break the common non-mipmapped case.
//
// Root cause this guards against: without the MAX_LEVEL clamp, GL's own default (1000) makes
// every texture appear "incomplete" under a *_MIPMAP_* minification filter (which
// TextureFilter::Anisotropic maps to) unless every level from 0 to 1000 is populated -- true even
// for a single-level texture -- so a broken renderer would render solid black here.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL2Texture2DAnisotropicSingleLevelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex2_;   // 2x1: Red | Green, single mip level (mipMap=false, the common case)
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const std::vector<uint8_t> pattern2 = {
            255,   0,   0, 255,
              0, 255,   0, 255
        };
        tex2_ = Texture2D::CreateFromPixels(device, 2, 1, pattern2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 255, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        SamplerState aniso;
        aniso.setFilterProperty(TextureFilter::Anisotropic);
        aniso.setAddressUProperty(TextureAddressMode::Clamp);
        aniso.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = aniso;

        BasicEffect fx(device);
        fx.setTextureProperty(&tex2_);
        fx.setTextureEnabledProperty(true);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(W / 2, H / 2, 1, 1); // texel boundary (u=0.5)
        Color sample(0, 0, 0, 0);
        device.GetBackBufferData(&reg, &sample, 0, 1);

        const bool isBlack = sample.getRProperty() <= 10 && sample.getGProperty() <= 10
                          && sample.getBProperty() <= 10;

        std::printf("[%s] Anisotropic on a single-level Texture2D: sample=(%d,%d,%d), expected "
                    "not solid black\n",
                    isBlack ? "FAIL" : "PASS",
                    sample.getRProperty(), sample.getGProperty(), sample.getBProperty());

        result_ = isBlack ? 1 : 0;
        Exit();
    }

public:
    OpenGL2Texture2DAnisotropicSingleLevelTest()
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

    OpenGL2Texture2DAnisotropicSingleLevelTest game;
    game.Run();
    return game.getResult();
}
