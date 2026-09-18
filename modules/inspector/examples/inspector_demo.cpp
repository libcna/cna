// SPDX-License-Identifier: MS-PL

// A small game that opts into the Inspector exactly as docs/inspector.md describes, so the agent,
// the cna-inspector bridge and the browser UI can be exercised end to end against real engine
// data: frames, draw metrics, CPU zones, markers and resources that are created and destroyed
// while the game runs. It runs under any platform, including HEADLESS.
//
//   cna_inspector_demo [--seconds N] [--port P]
//
// It prints the agent port and token; start the bridge with those values.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Diagnostics/Instrumentation.hpp"
#include "CNA/Inspector/Agent.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int Width = 640;
    constexpr int Height = 360;
    constexpr int SpriteCount = 48;

    std::unique_ptr<Texture2D> MakeTexture(GraphicsDevice& device, int size, Color color)
    {
        auto texture = std::make_unique<Texture2D>(device, size, size);
        std::vector<Color> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                                  color);
        texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
        return texture;
    }

    class InspectorDemo final : public Game
    {
    public:
        explicit InspectorDemo(double seconds) : seconds_(seconds)
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(Width);
            graphics_->setPreferredBackBufferHeightProperty(Height);
        }

    protected:
        void LoadContent() override
        {
            auto& device = getGraphicsDeviceProperty();
            spriteBatch_ = std::make_unique<SpriteBatch>(device);
            sprite_ = MakeTexture(device, 32, Color::White);
            background_ = MakeTexture(device, 256, Color::CornflowerBlue);
            scene_ = std::make_unique<RenderTarget2D>(device, Width / 2, Height / 2);
            vertices_ = std::make_unique<VertexBuffer>(
                device, VertexPositionColor::getVertexDeclarationStatic(), 1024, BufferUsage::None);
            indices_ = std::make_unique<IndexBuffer>(
                device, IndexElementSize::SixteenBits, 3072, BufferUsage::None);
        }

        void UnloadContent() override
        {
            churn_.reset();
            indices_.reset();
            vertices_.reset();
            scene_.reset();
            background_.reset();
            sprite_.reset();
            spriteBatch_.reset();
        }

        void Update(GameTime& gameTime) override
        {
            CNA_PROFILE_SCOPE("Demo/Update");
            const double now = gameTime.getTotalGameTimeProperty().getTotalSecondsProperty();
            if (seconds_ > 0.0 && now >= seconds_)
            {
                Exit();
                return;
            }
            {
                CNA_PROFILE_SCOPE("Demo/Simulate");
                // Enough arithmetic to show up as a measurable zone without starving the loop.
                double accumulator = 0.0;
                for (int step = 0; step < 20000; ++step)
                    accumulator += std::sin(now + step * 0.001);
                phase_ = now + accumulator * 1e-9;
            }
            // A texture that comes and goes shows resource lifetime in the Resources and Events
            // views while the game is running, not only at startup.
            const auto period = static_cast<std::int64_t>(now / 2.0);
            if (period != lastChurnPeriod_)
            {
                CNA_PROFILE_SCOPE("Demo/ChurnResources");
                lastChurnPeriod_ = period;
                if (period % 2 == 0)
                    churn_ = MakeTexture(getGraphicsDeviceProperty(), 64, Color::Orange);
                else
                    churn_.reset();
                CNA_DIAGNOSTICS_EVENT("Demo/ResourceChurn");
            }
            CNA_DIAGNOSTICS_GAUGE_SET("Demo/Sprites", SpriteCount);
            Game::Update(gameTime);
        }

        void Draw(const GameTime& gameTime) override
        {
            auto& device = getGraphicsDeviceProperty();
            {
                CNA_PROFILE_SCOPE_CATEGORY("Demo/DrawScene", ::CNA::Diagnostics::Category::Draw);
                device.SetRenderTarget(scene_.get());
                device.Clear(Color::Black);
                spriteBatch_->Begin();
                for (int index = 0; index < SpriteCount; ++index)
                {
                    const double angle = phase_ + index * 0.13;
                    const int x = Width / 4 + static_cast<int>(std::cos(angle) * 120.0);
                    const int y = Height / 4 + static_cast<int>(std::sin(angle * 1.3) * 60.0);
                    spriteBatch_->Draw(*sprite_, Rectangle(x, y, 12, 12),
                                       index % 2 == 0 ? Color::Yellow : Color::Magenta);
                }
                spriteBatch_->End();
            }
            {
                CNA_PROFILE_SCOPE_CATEGORY("Demo/Compose", ::CNA::Diagnostics::Category::Draw);
                device.SetRenderTarget(nullptr);
                device.Clear(Color::DarkSlateGray);
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
                spriteBatch_->Draw(*background_, Rectangle(0, 0, Width, Height), Color::White);
                spriteBatch_->Draw(*scene_, Rectangle(0, 0, Width, Height), Color::White);
                spriteBatch_->End();
            }
            Game::Draw(gameTime);
        }

    private:
        double seconds_ = 0.0;
        double phase_ = 0.0;
        std::int64_t lastChurnPeriod_ = -1;
        std::unique_ptr<GraphicsDeviceManager> graphics_;
        std::unique_ptr<SpriteBatch> spriteBatch_;
        std::unique_ptr<Texture2D> sprite_;
        std::unique_ptr<Texture2D> background_;
        std::unique_ptr<Texture2D> churn_;
        std::unique_ptr<RenderTarget2D> scene_;
        std::unique_ptr<VertexBuffer> vertices_;
        std::unique_ptr<IndexBuffer> indices_;
    };

    template<typename T>
    bool ParseNumber(std::string_view text, T& value)
    {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
}

int main(int argc, char** argv)
{
    double seconds = 0.0;
    unsigned port = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        const bool hasValue = index + 1 < argc;
        if (argument == "--seconds" && hasValue && ParseNumber(argv[index + 1], seconds))
            ++index;
        else if (argument == "--port" && hasValue && ParseNumber(argv[index + 1], port)
                 && port <= 65535)
            ++index;
        else
        {
            std::fprintf(stderr, "Usage: %s [--seconds N] [--port P]\n", argv[0]);
            return 2;
        }
    }

    CNA::Inspector::AgentConfiguration configuration;
    configuration.applicationName = "CNA Inspector demo";
    configuration.port = static_cast<std::uint16_t>(port);
    configuration.metadata = {
        {"Resolution", std::to_string(Width) + "x" + std::to_string(Height)},
        {"Sprites per frame", std::to_string(SpriteCount)},
    };
    std::string error;
    auto inspector = CNA::Inspector::Agent::Start(configuration, error);
    if (!inspector)
    {
        std::fprintf(stderr, "Inspector did not start: %s\n", error.c_str());
        return 1;
    }
    std::printf("Inspector port: %u\n", static_cast<unsigned>(inspector->GetPort()));
    std::printf("Inspector token: %s\n", inspector->GetAuthenticationToken().c_str());
    std::fflush(stdout);

    InspectorDemo game(seconds);
    game.Run();
    return 0;
}
