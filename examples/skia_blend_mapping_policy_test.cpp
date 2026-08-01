// SPDX-License-Identifier: MS-PL
// SKIA-51: public BlendState rejection policy.  A failed Begin must name the actual requested
// XNA state and leave the SpriteBatch usable with one of the documented direct mappings.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SkiaBlendMappingPolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    bool finished_ = false;
    int failures_ = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    [[nodiscard]] bool BeginRejects(const BlendState& state, const char* requiredText)
    {
        try
        {
            spriteBatch_->Begin(SpriteSortMode::Deferred, state);
            spriteBatch_->End();
            return false;
        }
        catch (const std::runtime_error& error)
        {
            return std::string(error.what()).find(requiredText) != std::string::npos;
        }
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_) return;
        finished_ = true;

        BlendState destinationFactor;
        destinationFactor.setColorSourceBlendProperty(Blend::DestinationColor);
        destinationFactor.setAlphaSourceBlendProperty(Blend::DestinationColor);
        Check(BeginRejects(destinationFactor, "DestinationColor"),
              "unsupported direct-looking factor tuple names DestinationColor");

        BlendState subtract = BlendState::Opaque;
        subtract.setColorBlendFunctionProperty(BlendFunction::Subtract);
        Check(BeginRejects(subtract, "Subtract"),
              "unsupported blend equation names Subtract");

        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        Color pixel(0, 0, 0, 0);
        const Rectangle sample(2, 2, 1, 1);
        device.GetBackBufferData(&sample, &pixel, 0, 1);
        Check(pixel == Color::White, "SpriteBatch remains usable after rejected BlendState requests");
        Exit();
    }

public:
    SkiaBlendMappingPolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(4);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaBlendMappingPolicyTest game;
    game.Run();
    return game.Result();
}
