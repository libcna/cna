// SPDX-License-Identifier: MS-PL
// SKIA-45/SKIA-78/SKIA-79: mip-dependent filters reject; the selected raster mode exposes no
// anisotropy, so Anisotropic is a documented, pixel-verified Linear fallback.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);

    struct MipFilter
    {
        TextureFilter filter;
        const char* name;
    };

    constexpr std::array<MipFilter, 6> kMipFilters = {{
        { TextureFilter::LinearMipPoint, "LinearMipPoint" },
        { TextureFilter::PointMipLinear, "PointMipLinear" },
        { TextureFilter::MinLinearMagPointMipLinear, "MinLinearMagPointMipLinear" },
        { TextureFilter::MinLinearMagPointMipPoint, "MinLinearMagPointMipPoint" },
        { TextureFilter::MinPointMagLinearMipLinear, "MinPointMagLinearMipLinear" },
        { TextureFilter::MinPointMagLinearMipPoint, "MinPointMagLinearMipPoint" },
    }};
}

class SkiaSamplerMipmapFilterTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> filterTexture_;
    bool finished_ = false;
    int failures_ = 0;

    void check(bool pass, const std::string& label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass)
            ++failures_;
    }

    [[nodiscard]] bool beginRejectsMipFilter(TextureFilter filter)
    {
        SamplerState sampler;
        sampler.setFilterProperty(filter);
        sampler.setAddressUProperty(TextureAddressMode::Clamp);
        sampler.setAddressVProperty(TextureAddressMode::Clamp);
        try
        {
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                                nullptr, Matrix::getIdentityProperty());
        }
        catch (const System::NotSupportedException&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
        // A successful Begin would make this test's contract fail. End keeps the test from
        // cascading an intentionally failed check into a later session error.
        spriteBatch_->End();
        return false;
    }

    [[nodiscard]] std::vector<Color> renderFilter(SamplerState& sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*filterTexture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();

        std::vector<Color> pixels(64, Color(0, 0, 0, 0));
        const Rectangle wholeBackbuffer(0, 0, 8, 8);
        device.GetBackBufferData(&wholeBackbuffer, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        texture_ = std::make_unique<Texture2D>(device, 1, 1);
        texture_->SetData(&kRed, 1);

        filterTexture_ = std::make_unique<Texture2D>(device, 2, 2);
        const std::array<Color, 4> filterPixels{{
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 255, 255),
        }};
        filterTexture_->SetData(filterPixels.data(), static_cast<int>(filterPixels.size()));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        for (const MipFilter& entry : kMipFilters)
            check(beginRejectsMipFilter(entry.filter), std::string(entry.name) + " rejects before Draw");

        auto& device = getGraphicsDeviceProperty();
        check(!device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering),
              "selected raster mode does not advertise anisotropic filtering");

        SamplerState linear;
        linear.setFilterProperty(TextureFilter::Linear);
        linear.setAddressUProperty(TextureAddressMode::Clamp);
        linear.setAddressVProperty(TextureAddressMode::Clamp);
        const std::vector<Color> linearPixels = renderFilter(linear);
        bool nonUniform = false;
        for (const Color& pixel : linearPixels)
            nonUniform = nonUniform || pixel != linearPixels.front();
        check(nonUniform, "linear reference draw contains observable filtered texture content");

        for (const int maxAnisotropy : {1, 4, 9999})
        {
            SamplerState anisotropic;
            anisotropic.setFilterProperty(TextureFilter::Anisotropic);
            anisotropic.setAddressUProperty(TextureAddressMode::Clamp);
            anisotropic.setAddressVProperty(TextureAddressMode::Clamp);
            anisotropic.setMaxAnisotropyProperty(maxAnisotropy);
            check(renderFilter(anisotropic) == linearPixels,
                  "Anisotropic MaxAnisotropy=" + std::to_string(maxAnisotropy)
                      + " uses the exact documented Linear fallback");
        }

        // The same SpriteBatch is deliberately reused after all six exceptions. This proves the
        // rejection occurs during setup and does not leave the public Begin/End state stuck.
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        Color pixel(0, 0, 0, 0);
        const Rectangle centre(2, 2, 1, 1);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        check(pixel.getRProperty() == 255 && pixel.getGProperty() == 0 && pixel.getBProperty() == 0,
              "PointClamp Begin/Draw/End succeeds after rejected mip filters");
        Exit();
    }

public:
    SkiaSamplerMipmapFilterTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    SkiaSamplerMipmapFilterTest game;
    game.Run();
    return game.Result();
}
