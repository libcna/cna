// SPDX-License-Identifier: MS-PL
// D2D-27/D2D-114/D2D-115: exercise Direct2D's frame-scoped resources across the boundaries
// that release them, and pin what disposing or updating a source mid-frame must produce.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Direct2DLifetimeTest final : public Game
{
public:
    Direct2DLifetimeTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(32);
        manager_->setPreferredBackBufferHeightProperty(24);
        manager_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        startedAt_ = std::chrono::steady_clock::now();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{255, 255, 255, 255}));
        target_ = std::make_unique<RenderTarget2D>(device, 16, 16, false,
                                                   SurfaceFormat::Color, DepthFormat::None);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        SamplerState pointWrap;
        pointWrap.setFilterProperty(TextureFilter::Point);
        pointWrap.setAddressUProperty(TextureAddressMode::Wrap);
        pointWrap.setAddressVProperty(TextureAddressMode::Mirror);

        // Fill an off-screen target, then sample it through ImageBrush with wrapping and flip.
        // This allocates frame-scoped Direct2D image brushes, which must be released by every
        // EndDraw path below rather than being retained across frames.
        device.SetRenderTarget(target_.get());
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const Color color = ((x + y + frame_) & 1) == 0
                    ? Color(255, 0, 0, 255) : Color(0, 255, 0, 255);
                sprites_->Draw(*white_, Rectangle(x, y, 1, 1), Rectangle(0, 0, 1, 1), color);
            }
        }
        sprites_->End();

        if ((frame_ & 1) == 0)
        {
            // Forces EndDraw and target switching while image resources are live.
            Color targetPixel(0, 0, 0, 0);
            const Rectangle texel(0, 0, 1, 1);
            target_->GetData(0, &texel, &targetPixel, 0, 1);
        }

        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
        for (int index = 0; index < 128; ++index)
        {
            const int x = (index * 3) % 32;
            const int y = (index * 5) % 24;
            sprites_->Draw(*target_, Rectangle(x, y, 4, 4), Rectangle(-1, -1, 4, 4), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        }
        sprites_->End();

        // Readback is another EndDraw boundary. Keep a minimal observable result so a silent
        // target/clip failure cannot turn this into a mere no-crash test.
        Color pixel(0, 0, 0, 0);
        const Rectangle readbackTexel(0, 0, 1, 1);
        device.GetBackBufferData(&readbackTexel, &pixel, 0, 1);
        if (pixel.getAProperty() != 255)
        {
            std::printf("[FAIL] Direct2D lifetime smoke readback alpha=%d\n", pixel.getAProperty());
            result_ = 1;
            Exit();
            return;
        }

        if (frame_ == 5 && !RunDisposeAfterEndScenario(device))
        {
            result_ = 1;
            Exit();
            return;
        }
        if (frame_ == 6 && !RunMidFrameUpdateScenario(device))
        {
            result_ = 1;
            Exit();
            return;
        }

        if (frame_ == 2)
        {
            device.GetRenderer().DebugSimulateContextLoss();
        }
        else if (frame_ == 4)
        {
            manager_->setPreferredBackBufferWidthProperty(40);
            manager_->setPreferredBackBufferHeightProperty(24);
            manager_->ApplyChanges();
        }

        ++frame_;
        if (frame_ == 8)
        {
            const auto elapsed = std::chrono::steady_clock::now() - startedAt_;
            const double elapsedMilliseconds =
                std::chrono::duration<double, std::milli>(elapsed).count();
            std::printf("[PASS] Direct2D lifetime smoke: frames=%d elapsed-ms=%.3f ms-per-frame=%.3f\n",
                        frame_, elapsedMilliseconds, elapsedMilliseconds / frame_);
            result_ = 0;
            Exit();
        }
    }

private:
    [[nodiscard]] bool ReadPixel(GraphicsDevice& device, int x, int y, Color& pixel) const
    {
        const Rectangle region(x, y, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return true;
    }

    [[nodiscard]] static bool Matches(const Color& actual, const Color& expected)
    {
        constexpr int tolerance = 4;
        return std::abs(static_cast<int>(actual.getRProperty()) - expected.getRProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - expected.getGProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - expected.getBProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getAProperty()) - expected.getAProperty()) <= tolerance;
    }

    // D2D-114: an application may dispose a texture after SpriteBatch::End and before the frame is
    // presented. Every draw branch must already hold whatever COM lifetime it needs, so the
    // committed frame still shows the disposed texture's pixels instead of released memory. All
    // four source branches are covered: a plain bitmap draw, the ImageBrush path (forced by a
    // flip), the CPU fallback / decorated path (forced by a tint), and a render-target source.
    [[nodiscard]] bool RunDisposeAfterEndScenario(GraphicsDevice& device)
    {
        SamplerState point = SamplerState::PointClamp;
        auto plain = Texture2D::CreateFromPixels(device, 1, 1,
                                                 std::vector<uint8_t>{255, 0, 0, 255});
        auto flipped = Texture2D::CreateFromPixels(device, 1, 1,
                                                   std::vector<uint8_t>{0, 255, 0, 255});
        auto tinted = Texture2D::CreateFromPixels(device, 1, 1,
                                                  std::vector<uint8_t>{255, 255, 255, 255});
        RenderTarget2D source(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None);
        const std::array<Color, 1> sourcePixel{Color(0, 0, 255, 255)};
        source.SetData(sourcePixel.data(), static_cast<int>(sourcePixel.size()));

        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, nullptr);
        sprites_->Draw(plain, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->Draw(flipped, Rectangle(4, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White,
                       0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sprites_->Draw(tinted, Rectangle(8, 0, 4, 4), Rectangle(0, 0, 1, 1), Color(0, 128, 128, 255));
        sprites_->Draw(source, Rectangle(12, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();

        // Every source is destroyed here, while the frame it was drawn into has not been read back
        // or presented yet.
        plain.Dispose();
        flipped.Dispose();
        tinted.Dispose();
        source.Dispose();

        struct Expectation
        {
            const char* label;
            int x;
            Color expected;
        };
        const std::array<Expectation, 4> expectations{{
            {"plain bitmap", 1, Color(255, 0, 0, 255)},
            {"image brush", 5, Color(0, 255, 0, 255)},
            {"decorated", 9, Color(0, 128, 128, 255)},
            {"render-target source", 13, Color(0, 0, 255, 255)},
        }};
        bool passed = true;
        for (const Expectation& expectation : expectations)
        {
            Color actual(0, 0, 0, 0);
            (void)ReadPixel(device, expectation.x, 1, actual);
            const bool matches = Matches(actual, expectation.expected);
            std::printf("[%s] dispose-after-End %s: got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        matches ? "PASS" : "FAIL", expectation.label,
                        actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                        actual.getAProperty(), expectation.expected.getRProperty(),
                        expectation.expected.getGProperty(), expectation.expected.getBProperty(),
                        expectation.expected.getAProperty());
            passed = passed && matches;
        }
        return passed;
    }

    // D2D-115: updating a source in the middle of a frame has one semantics on every branch --
    // a draw already issued keeps the pixels it was issued with, and the next draw observes the
    // new ones. SpriteSortMode::Immediate is required for the first draw to be a real native draw
    // rather than something End() would replay after the update.
    [[nodiscard]] bool RunMidFrameUpdateScenario(GraphicsDevice& device)
    {
        SamplerState point = SamplerState::PointClamp;
        auto plain = Texture2D::CreateFromPixels(device, 1, 1,
                                                 std::vector<uint8_t>{255, 0, 0, 255});
        auto flipped = Texture2D::CreateFromPixels(device, 1, 1,
                                                   std::vector<uint8_t>{0, 255, 0, 255});

        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend, &point, nullptr, nullptr);
        sprites_->Draw(plain, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->Draw(flipped, Rectangle(4, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White,
                       0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);

        const std::array<Color, 1> replacedPlain{Color(0, 0, 255, 255)};
        const std::array<Color, 1> replacedFlipped{Color(255, 0, 255, 255)};
        plain.SetData(replacedPlain.data(), static_cast<int>(replacedPlain.size()));
        flipped.SetData(replacedFlipped.data(), static_cast<int>(replacedFlipped.size()));

        sprites_->Draw(plain, Rectangle(8, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->Draw(flipped, Rectangle(12, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White,
                       0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sprites_->End();

        struct Expectation
        {
            const char* label;
            int x;
            Color expected;
        };
        const std::array<Expectation, 4> expectations{{
            {"plain bitmap before update", 1, Color(255, 0, 0, 255)},
            {"image brush before update", 5, Color(0, 255, 0, 255)},
            {"plain bitmap after update", 9, Color(0, 0, 255, 255)},
            {"image brush after update", 13, Color(255, 0, 255, 255)},
        }};
        bool passed = true;
        for (const Expectation& expectation : expectations)
        {
            Color actual(0, 0, 0, 0);
            (void)ReadPixel(device, expectation.x, 5, actual);
            const bool matches = Matches(actual, expectation.expected);
            std::printf("[%s] mid-frame update %s: got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        matches ? "PASS" : "FAIL", expectation.label,
                        actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                        actual.getAProperty(), expectation.expected.getRProperty(),
                        expectation.expected.getGProperty(), expectation.expected.getBProperty(),
                        expectation.expected.getAProperty());
            passed = passed && matches;
        }
        return passed;
    }

    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    std::chrono::steady_clock::time_point startedAt_{};
    int frame_ = 0;
    int result_ = 1;
};

int main()
{
    Direct2DLifetimeTest test;
    test.Run();
    return test.Result();
}
