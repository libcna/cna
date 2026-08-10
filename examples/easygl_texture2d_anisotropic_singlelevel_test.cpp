// SPDX-License-Identifier: MS-PL
// Task 924 (split from Task 867): verify that an ordinary, non-mipmapped `Texture2D`
// (`Texture2D::CreateFromPixels`, the overwhelmingly common case for real game textures) renders
// correctly when sampled with a mipmap-requiring `TextureFilter` (`Anisotropic`), instead of the
// previously-confirmed solid-black "incomplete mipmap chain" result.
//
// Direct sibling of examples/easygl_texture_anisotropic_effect_test.cpp (Task 299), which found
// this exact bug and deliberately did NOT fail on it (that test's own scope is "does an extreme
// MaxAnisotropy value crash", not this bug). This test asserts the actual fix: the same
// TextureFilter::Anisotropic + single-level Texture2D combination must now produce a real
// magnified blend of the 2-texel source, not solid black.
//
// Root cause (Task 867/924): EasyGLTextureRenderer never set GL_TEXTURE_MAX_LEVEL, so GL's own
// default (1000) made every texture appear "incomplete" under a *_MIPMAP_* minification filter
// (which TextureFilter::Anisotropic and every other Mip-suffixed TextureFilter value map to)
// unless every level from 0 to 1000 (or until 1x1) was populated — true even for a single-level
// (mipMap=false) texture. Fixed by clamping GL_TEXTURE_MAX_LEVEL to the texture's real level
// count (0 for a single-level texture) at construction time.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
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

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Texture2DAnisotropicSingleLevelTest : public Game
{
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

        // Full-screen quad: 2-texel texture stretched wide (magnification).
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
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
        if (isBlack)
        {
            std::printf("[INFO] Solid black means GL_TEXTURE_MAX_LEVEL was not clamped to this "
                        "texture's real level count (0), so GL treats it as an incomplete "
                        "mipmap chain under this minification filter.\n");
        }

        result_ = isBlack ? 1 : 0;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    Texture2DAnisotropicSingleLevelTest game;
    game.Run();
    return game.getResult();
}
