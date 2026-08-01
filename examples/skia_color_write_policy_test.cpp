// SPDX-License-Identifier: MS-PL
// SKIA-57: public ColorWriteChannels matrix. It validates all RGBA masks after each accepted
// blend route on the backbuffer, the destination-reading runtime route on RenderTarget2D, and a
// source/destination alpha distinction so alpha writes cannot be accidentally ignored.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    enum class BlendRoute { Opaque, AlphaBlend, NonPremultiplied, Additive, DestinationColor };

    [[nodiscard]] const char* RouteName(BlendRoute route)
    {
        switch (route)
        {
            case BlendRoute::Opaque: return "Opaque";
            case BlendRoute::AlphaBlend: return "AlphaBlend";
            case BlendRoute::NonPremultiplied: return "NonPremultiplied";
            case BlendRoute::Additive: return "Additive";
            case BlendRoute::DestinationColor: return "DestinationColor";
        }
        return "invalid";
    }

    [[nodiscard]] BlendState RouteState(BlendRoute route, int mask)
    {
        BlendState state;
        switch (route)
        {
            case BlendRoute::Opaque: state = BlendState::Opaque; break;
            case BlendRoute::AlphaBlend: state = BlendState::AlphaBlend; break;
            case BlendRoute::NonPremultiplied: state = BlendState::NonPremultiplied; break;
            case BlendRoute::Additive: state = BlendState::Additive; break;
            case BlendRoute::DestinationColor:
                state.setColorSourceBlendProperty(Blend::DestinationColor);
                state.setColorDestinationBlendProperty(Blend::Zero);
                state.setAlphaSourceBlendProperty(Blend::One);
                state.setAlphaDestinationBlendProperty(Blend::Zero);
                break;
        }
        state.setColorWriteChannelsProperty(static_cast<ColorWriteChannels>(mask));
        return state;
    }

    [[nodiscard]] Color Destination(BlendRoute route)
    {
        switch (route)
        {
            case BlendRoute::Additive: return Color(20, 60, 100, 255);
            case BlendRoute::DestinationColor: return Color(200, 100, 50, 255);
            default: return Color(30, 60, 90, 255);
        }
    }

    [[nodiscard]] Color Tint(BlendRoute route)
    {
        switch (route)
        {
            case BlendRoute::Additive: return Color(80, 20, 40, 255);
            case BlendRoute::DestinationColor: return Color(128, 128, 128, 255);
            default: return Color(180, 40, 80, 255);
        }
    }

    [[nodiscard]] Color Blended(BlendRoute route)
    {
        switch (route)
        {
            case BlendRoute::Additive: return Color(100, 80, 140, 255);
            case BlendRoute::DestinationColor: return Color(100, 50, 25, 255);
            default: return Color(180, 40, 80, 255);
        }
    }

    [[nodiscard]] Color Masked(Color destination, Color blended, int mask)
    {
        return Color((mask & 1) ? blended.getRProperty() : destination.getRProperty(),
                     (mask & 2) ? blended.getGProperty() : destination.getGProperty(),
                     (mask & 4) ? blended.getBProperty() : destination.getBProperty(),
                     (mask & 8) ? blended.getAProperty() : destination.getAProperty());
    }

    [[nodiscard]] bool Equal(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }
}

class SkiaColorWritePolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    std::unique_ptr<Texture2D> transparentBlackTexture_;
    bool finished_ = false;
    int failures_ = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    void Draw(Texture2D& texture, BlendRoute route, int mask, Color tint)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, RouteState(route, mask),
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(texture, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), tint);
        spriteBatch_->End();
    }

    [[nodiscard]] Color ReadBackbuffer(GraphicsDevice& device)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(0, 0, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        // The Opaque route labels these bytes premultiplied. Zero RGB makes the alpha-only source
        // valid under either labelling and isolates the output alpha-write bit.
        transparentBlackTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{0, 0, 0, 128}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_) return;
        finished_ = true;
        auto& device = getGraphicsDeviceProperty();

        bool backbufferMatrix = true;
        for (BlendRoute route : {BlendRoute::Opaque, BlendRoute::AlphaBlend,
                                 BlendRoute::NonPremultiplied, BlendRoute::Additive,
                                 BlendRoute::DestinationColor})
        {
            const Color destination = Destination(route);
            const Color tint = Tint(route);
            const Color blended = Blended(route);
            for (int mask = 0; mask < 16; ++mask)
            {
                device.Clear(destination);
                Draw(*whiteTexture_, route, mask, tint);
                const Color actual = ReadBackbuffer(device);
                const Color expected = Masked(destination, blended, mask);
                if (!Equal(actual, expected))
                {
                    std::printf("[INFO] backbuffer route=%s mask=%d got=(%d,%d,%d,%d) want=(%d,%d,%d,%d)\n",
                                RouteName(route), mask, actual.getRProperty(), actual.getGProperty(),
                                actual.getBProperty(), actual.getAProperty(), expected.getRProperty(),
                                expected.getGProperty(), expected.getBProperty(), expected.getAProperty());
                    backbufferMatrix = false;
                }
            }
        }
        Check(backbufferMatrix, "all 16 RGBA masks work after each accepted blend route on the backbuffer");

        RenderTarget2D target(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const Color targetDestination = Destination(BlendRoute::DestinationColor);
        const Color targetBlended = Blended(BlendRoute::DestinationColor);
        bool targetMatrix = true;
        for (int mask = 0; mask < 16; ++mask)
        {
            device.SetRenderTarget(&target);
            device.Clear(targetDestination);
            Draw(*whiteTexture_, BlendRoute::DestinationColor, mask, Tint(BlendRoute::DestinationColor));
            device.SetRenderTarget(nullptr);
            Color actual(0, 0, 0, 0);
            target.GetData(&actual, 0, 1);
            if (!Equal(actual, Masked(targetDestination, targetBlended, mask)))
            {
                std::printf("[INFO] target mask=%d got=(%d,%d,%d,%d)\n", mask,
                            actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                            actual.getAProperty());
                targetMatrix = false;
            }
        }
        Check(targetMatrix, "all 16 masks preserve destination channels on the runtime-blended RenderTarget2D");

        bool alphaMatrix = true;
        for (int mask = 0; mask < 16; ++mask)
        {
            device.Clear(Color::Black);
            Draw(*transparentBlackTexture_, BlendRoute::Opaque, mask, Color::White);
            const Color actual = ReadBackbuffer(device);
            const int expectedAlpha = (mask & 8) ? 128 : 255;
            if (actual.getRProperty() != 0 || actual.getGProperty() != 0 || actual.getBProperty() != 0
                || actual.getAProperty() != expectedAlpha)
            {
                alphaMatrix = false;
            }
        }
        Check(alphaMatrix, "the alpha write bit selects source alpha or preserves destination alpha for all masks");
        Exit();
    }

public:
    SkiaColorWritePolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(1);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaColorWritePolicyTest game;
    game.Run();
    return game.Result();
}
