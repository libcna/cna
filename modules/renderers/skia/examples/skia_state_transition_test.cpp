// SPDX-License-Identifier: MS-PL
// SKIA-59: exercise 2D state across target switches, Clear, Present, and a rejected Begin.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferSize = 8;
    constexpr int kTargetSize = 4;
    const Color kBlack(0, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaStateTransitionTest final : public Game
{
public:
    SkiaStateTransitionTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        graphics_->setPreferredBackBufferHeightProperty(kBackbufferSize);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        batch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        target_ = std::make_unique<RenderTarget2D>(device, kTargetSize, kTargetSize, false,
                                                   SurfaceFormat::Color, DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RasterizerState scissor = RasterizerState::CullNone;
        scissor.setScissorTestEnableProperty(true);
        RasterizerState noScissor = RasterizerState::CullNone;

        // Binding a target resets viewport/scissor to target size; unbinding must reset both
        // to the backbuffer. Keep the same enabled scissor state across that switch to expose a
        // stale target-local rectangle immediately.
        device.SetRenderTarget(target_.get());
        device.Clear(kBlack);
        device.setScissorRectangleProperty(Rectangle(0, 0, 2, kTargetSize));
        DrawSprite(scissor, Rectangle(0, 0, kTargetSize, kTargetSize), kRed);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        CheckTarget(1, 2, kRed, "target-local enabled scissor keeps its interior");
        CheckTarget(3, 2, kBlack, "target-local enabled scissor clips its exterior");

        device.Clear(kBlack);
        DrawSprite(scissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kBlue);
        CheckBackbuffer(7, 7, kBlue, "unbinding resets enabled scissor to the full backbuffer");

        // Clear ignores scissor but must not alter it. The second sprite must still be clipped
        // after the unconditional green clear.
        device.setScissorRectangleProperty(Rectangle(0, 0, 2, kBackbufferSize));
        device.Clear(kBlack);
        DrawSprite(scissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kRed);
        CheckBackbuffer(6, 3, kBlack, "scissor clips before Clear transition");
        device.Clear(kGreen);
        CheckBackbuffer(6, 3, kGreen, "Clear is unconditional despite enabled scissor");
        DrawSprite(scissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kRed);
        CheckBackbuffer(1, 3, kRed, "Clear retains the enabled scissor interior");
        CheckBackbuffer(6, 3, kGreen, "Clear retains the enabled scissor exterior");

        RasterizerState wireFrame;
        wireFrame.setFillModeProperty(FillMode::WireFrame);
        bool rejected = false;
        try
        {
            DrawSprite(wireFrame, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kBlue);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string(error.what()).find("FillMode::WireFrame") != std::string::npos;
        }
        Check(rejected, "rejected Begin reports the unsupported state");
        device.Clear(kBlack);
        DrawSprite(noScissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kBlue);
        CheckBackbuffer(6, 3, kBlue, "rejected Begin leaks neither its state nor its begun flag");

        // A same-size Present must preserve a caller-set sub-viewport. The second draw runs after
        // Present so a regression that resets viewport to full backbuffer paints the outer pixel.
        device.Clear(kBlack);
        device.setViewportProperty(Viewport(2, 1, 4, 6));
        DrawSprite(noScissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kRed);
        device.Present();
        DrawSprite(noScissor, Rectangle(0, 0, kBackbufferSize, kBackbufferSize), kBlue);
        CheckBackbuffer(3, 2, kBlue, "Present keeps the custom viewport interior");
        CheckBackbuffer(7, 7, kBlack, "Present keeps the custom viewport clip after presentation");
        Exit();
    }

private:
    void DrawSprite(RasterizerState& state, const Rectangle& destination, const Color& color)
    {
        SamplerState point = SamplerState::PointClamp;
        batch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &state);
        batch_->Draw(*white_, destination, color);
        batch_->End();
    }

    void CheckBackbuffer(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&pixel, &actual, 0, 1);
        CheckPixel(actual, expected, label);
    }

    void CheckTarget(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, y, 1, 1);
        target_->GetData(0, &pixel, &actual, 0, 1);
        CheckPixel(actual, expected, label);
    }

    void CheckPixel(Color actual, Color expected, const char* label)
    {
        const bool pass = Same(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        if (!pass)
            ++failures_;
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> batch_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaStateTransitionTest game;
    game.Run();
    return game.Result();
}
