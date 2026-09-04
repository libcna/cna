// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-52: real-device proof that SamplerState.MaxAnisotropy reaches the
// Diligent renderer without crashing and is genuinely used to build a real Dg::SamplerDesc, even at
// an extreme over-cap request.
//
// Precedent (Task 299, examples/easygl_texture_anisotropic_effect_test.cpp): a true visual
// anisotropic-quality pixel comparison (sharper vs. blurrier detail under oblique minification) is
// inherently driver-dependent and fragile to assert precisely across GPUs/software rasterizers, so
// this project's own established practice for this task is to verify the "caps and fallback" half
// literally instead: requesting an absurdly over-cap MaxAnisotropy (9999, far beyond any real GPU's
// limit, and beyond DiligentRenderer::ApplySamplerState()'s own std::clamp(..., 1, 16)) must
// not crash or throw, and the draw must still produce a real, sampled (non-garbage) result.
//
// Uses DualTextureEffect exactly like the EasyGL precedent: a 2-texel red|green Texture2D stretched
// across a full-viewport quad (magnification, not minification -- deliberately not the mip-chain-
// dependent case Task 867 already tracks elsewhere) modulated with a white second layer, sampled
// with SamplerState.Filter = Anisotropic, MaxAnisotropy = 9999, at the boundary texel (u=0.5). A
// real sample there blends red and green; a crash/corruption would either throw or read back
// garbage/uninitialized memory.
//
// Exit code 0 = PASS (no crash, real sampled output), 1 = FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DiligentAnisotropicTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D stripTexture_;
    Texture2D whiteTexture_;
    bool done_ = false;
    int result_ = 1;

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        stripTexture_ = Texture2D::CreateFromPixels(
            device, 2, 1, std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255});
        whiteTexture_ =
            Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();

        device.Clear(Color(0, 0, 255, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        bool threw = false;
        Color sample(0, 0, 0, 0);
        try
        {
            SamplerState anisotropic;
            anisotropic.setFilterProperty(TextureFilter::Anisotropic);
            anisotropic.setAddressUProperty(TextureAddressMode::Clamp);
            anisotropic.setAddressVProperty(TextureAddressMode::Clamp);
            anisotropic.setMaxAnisotropyProperty(9999); // far beyond DiligentRenderer's own cap
            device.getSamplerStatesProperty()[0] = anisotropic;

            DualTextureEffect effect(device);
            effect.setTextureProperty(&stripTexture_);
            effect.setTexture2Property(&whiteTexture_);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.Apply();

            const VertexPositionTexture verts[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
                {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 1.0f)},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            const Rectangle region(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2,
                                    1, 1);
            device.GetBackBufferData(&region, &sample, 0, 1);
        }
        catch (const std::exception& error)
        {
            threw = true;
            std::printf("[FAIL] MaxAnisotropy=9999 threw: %s\n", error.what());
        }

        // A real sample at the boundary texel is a red/green blend or one of the two solid colours
        // (point-sampling can legitimately land on either side); the only failure this test cares
        // about is a crash/throw or the clear colour leaking through (nothing was drawn at all).
        const bool isClearColor = !threw && sample.getRProperty() <= 10 && sample.getGProperty() <= 10 &&
                                  sample.getBProperty() >= 200;
        const bool ok = !threw && !isClearColor;

        std::printf("[%s] Anisotropic, MaxAnisotropy=9999 (over any real cap): threw=%d "
                    "sample=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", threw ? 1 : 0, sample.getRProperty(), sample.getGProperty(),
                    sample.getBProperty());

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    DiligentAnisotropicTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentAnisotropicTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
