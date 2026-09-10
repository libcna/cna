// SPDX-License-Identifier: MS-PL
// Task 299: verify anisotropic filtering caps and fallback on a 3D stock effect
// (DualTextureEffect).
//
// Audit findings this task confirmed via code reading (see plans/plan_graphics.md Task 299 for the
// full writeup): Vulkan correctly queries the real device anisotropy cap
// (VkPhysicalDeviceProperties::limits.maxSamplerAnisotropy) and clamps SamplerState.MaxAnisotropy
// to it before creating the sampler — the only renderer where the requested level has any real
// effect. EasyGL's TextureFilter::Anisotropic silently falls back to plain trilinear filtering
// with NO anisotropic effect at all (the underlying easy-gl library has no anisotropy support
// whatsoever). Bgfx enables its ANISOTROPIC sampler flags but ignores the requested
// MaxAnisotropy level entirely (the parameter is unused in BgfxRenderer::ApplySamplerState).
//
// A true visual anisotropic-quality pixel test (comparing detail preservation under oblique/
// aspect-skewed minification) is inherently driver-dependent and fragile to assert precisely.
// This test instead verifies the deterministic caps path: an absurdly over-cap MaxAnisotropy
// request must neither throw nor suppress sampling from an ordinary single-level texture. At the
// exact Red/Green boundary, DualTextureEffect's doubling produces a saturated yellow pixel.
//
// Exit code 0 = PASS (no crash and expected pixel), 1 = FAIL.

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
#include <exception>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class TextureAnisotropicEffectTest : public Game
{
    Texture2D tex2_;   // 2x1: Red | Green
    Texture2D whiteTex_;
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

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
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

        bool threw = false;
        Color sample(0, 0, 0, 0);
        try
        {
            SamplerState aniso;
            aniso.setFilterProperty(TextureFilter::Anisotropic);
            aniso.setAddressUProperty(TextureAddressMode::Clamp);
            aniso.setAddressVProperty(TextureAddressMode::Clamp);
            aniso.setMaxAnisotropyProperty(9999); // far beyond any real GPU's cap
            device.getSamplerStatesProperty()[0] = aniso;

            DualTextureEffect fx(device);
            fx.setTextureProperty(&tex2_);
            fx.setTexture2Property(&whiteTex_);
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
            device.GetBackBufferData(&reg, &sample, 0, 1);
        }
        catch (const std::exception& e)
        {
            threw = true;
            std::printf("[FAIL] MaxAnisotropy=9999 threw: %s\n", e.what());
        }

        const bool isExpected = !threw
            && sample.getRProperty() >= 235
            && sample.getGProperty() >= 235
            && sample.getBProperty() <= 20;

        if (!threw)
        {
            std::printf("[%s] Anisotropic, MaxAnisotropy=9999: sample=(%d,%d,%d), "
                        "expected saturated yellow\n",
                        isExpected ? "PASS" : "FAIL",
                        sample.getRProperty(), sample.getGProperty(), sample.getBProperty());
        }

        result_ = isExpected ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    TextureAnisotropicEffectTest game;
    game.Run();
    return game.getResult();
}
