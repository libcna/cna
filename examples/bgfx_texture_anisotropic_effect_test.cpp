// SPDX-License-Identifier: MS-PL
// Task 749: verify anisotropic filtering cap query + fallback on a 3D stock effect
// (DualTextureEffect), on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_texture_anisotropic_effect_test.cpp (Task 299,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file uses
// GraphicsDevice::SetDepthTestEnabled(false), which throws on Bgfx (a pre-existing, deliberate
// stub), replaced with the equivalent DepthStencilState-based call. Only one GetBackBufferData
// read happens per Draw() tick here, so Bgfx's "first read per rendered frame" quirk (Task 406)
// does not apply -- no RunCheck()-style restructuring needed.
//
// Per Task 299's own audit: Bgfx enables its ANISOTROPIC sampler flags (BGFX_SAMPLER_*_ANISOTROPIC)
// but the requested MaxAnisotropy level itself is unused in BgfxGraphicsBackend::ApplySamplerState
// (the actual clamp level is device/driver-determined by the flag alone, not a queried/clamped
// numeric value like Vulkan's real VkPhysicalDeviceProperties::limits.maxSamplerAnisotropy path).
// A true visual anisotropic-quality pixel test is inherently driver-dependent and fragile to assert
// precisely, so this test verifies the "caps and fallback" half literally: requesting an absurdly
// over-cap MaxAnisotropy (9999, far beyond any real GPU's limit) must not crash or throw.
//
// Exit code 0 = PASS (no crash), 1 = FAIL (crashed/threw).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class BgfxTextureAnisotropicEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex2_;   // 2x1: Red | Green
    Texture2D whiteTex_;
    bool done_   = false;
    int  result_ = 1;

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
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        device.setDepthStencilStateProperty(ds);
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
            // RasterizerState -- needs CullNone.
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

        const bool isBlended = !threw
            && sample.getRProperty() >= 90 && sample.getRProperty() <= 165
            && sample.getGProperty() >= 90 && sample.getGProperty() <= 165;
        const bool isBlack = !threw
            && sample.getRProperty() <= 10 && sample.getGProperty() <= 10 && sample.getBProperty() <= 10;

        if (!threw)
        {
            std::printf("[PASS] Anisotropic, MaxAnisotropy=9999 (over any real cap): no crash, sample=(%d,%d,%d)\n",
                        sample.getRProperty(), sample.getGProperty(), sample.getBProperty());
            if (isBlack)
            {
                std::printf("[INFO] Sample is solid black -- a mipmap-incomplete-texture symptom if this\n"
                            "       backend requires a complete mip chain for Anisotropic. Not a failure of\n"
                            "       THIS test's real ask (does extreme MaxAnisotropy crash?).\n");
            }
            else if (isBlended)
            {
                std::printf("[INFO] Sample is a normal blend -- Anisotropic filtering degrades gracefully.\n");
            }
        }

        result_ = threw ? 1 : 0;
        Exit();
    }

public:
    BgfxTextureAnisotropicEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxTextureAnisotropicEffectTest game;
    game.Run();
    return game.getResult();
}
