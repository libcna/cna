// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md DX-217: renderer-neutral behavioral contract for all five presentation modes.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::CnaPresentationMode;

namespace
{
    constexpr int kPhysicalWidth = 160;
    constexpr int kPhysicalHeight = 96;
    constexpr int kLogicalSize = 40;

    struct Box
    {
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        int count = 0;

        [[nodiscard]] int Width() const { return count == 0 ? 0 : maxX - minX + 1; }
        [[nodiscard]] int Height() const { return count == 0 ? 0 : maxY - minY + 1; }
    };

    bool Close(int actual, int expected, int tolerance = 1)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void PrintGeometry(const char* mode, int logicalWidth, int logicalHeight,
                       int x, int y, int width, int height, const Box& box)
    {
        std::printf(
            "[INFO] %s: logical=%dx%d viewport=(%d,%d %dx%d) white=(%d,%d %dx%d, n=%d)\n",
            mode, logicalWidth, logicalHeight, x, y, width, height,
            box.count == 0 ? -1 : box.minX, box.count == 0 ? -1 : box.minY,
            box.Width(), box.Height(), box.count);
    }
}

class PresentationModeContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> white_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition)
            ++passed_;
    }

    static Box WhiteBox(const std::vector<std::uint8_t>& pixels, int width, int height)
    {
        Box box;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                if (pixels[offset] < 220 || pixels[offset + 1] < 220 ||
                    pixels[offset + 2] < 220)
                    continue;
                box.minX = std::min(box.minX, x);
                box.minY = std::min(box.minY, y);
                box.maxX = std::max(box.maxX, x);
                box.maxY = std::max(box.maxY, y);
                ++box.count;
            }
        }
        return box;
    }

    Box RenderMode(CnaPresentationMode mode, int physicalWidth, int physicalHeight,
                   int& logicalWidth, int& logicalHeight,
                   int& viewportX, int& viewportY, int& viewportWidth, int& viewportHeight)
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        renderer.SetVirtualResolution(kLogicalSize, kLogicalSize);
        renderer.SetPresentationMode(static_cast<int>(mode));
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        renderer.GetDefaultViewportRect(
            viewportX, viewportY, viewportWidth, viewportHeight);

        device.setViewportProperty(Viewport(0, 0, logicalWidth, logicalHeight));
        device.Clear(Color::Black);

        const int spriteWidth = mode == CnaPresentationMode::NativeBackBuffer
            ? kLogicalSize
            : logicalWidth;
        const int spriteHeight = mode == CnaPresentationMode::NativeBackBuffer
            ? kLogicalSize
            : logicalHeight;
        SpriteBatch batch(device);
        batch.Begin();
        batch.Draw(*white_, Rectangle(0, 0, spriteWidth, spriteHeight), Color::White);
        batch.End();

        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(physicalWidth) * physicalHeight * 4u, 0);
        renderer.ReadBackbuffer(0, 0, physicalWidth, physicalHeight, pixels.data());
        return WhiteBox(pixels, physicalWidth, physicalHeight);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
        int physicalWidth = 0;
        int physicalHeight = 0;
        renderer.GetViewportSize(physicalWidth, physicalHeight);
        Check(physicalWidth > kLogicalSize && physicalHeight > kLogicalSize &&
              physicalWidth != physicalHeight,
              "control surface is larger than and aspect-distinct from the 40x40 logical canvas");

        int logicalWidth = 0;
        int logicalHeight = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;

        Box box = RenderMode(CnaPresentationMode::Letterbox, physicalWidth, physicalHeight,
                             logicalWidth, logicalHeight, x, y, width, height);
        PrintGeometry("Letterbox", logicalWidth, logicalHeight, x, y, width, height, box);
        const int expectedLetterboxSize = std::min(physicalWidth, physicalHeight);
        const int expectedLetterboxX = (physicalWidth - expectedLetterboxSize) / 2;
        const int expectedLetterboxY = (physicalHeight - expectedLetterboxSize) / 2;
        Check(logicalWidth == kLogicalSize && logicalHeight == kLogicalSize &&
              Close(x, expectedLetterboxX) && Close(y, expectedLetterboxY) &&
              Close(width, expectedLetterboxSize) && Close(height, expectedLetterboxSize) &&
              Close(box.minX, expectedLetterboxX) && Close(box.minY, expectedLetterboxY) &&
              Close(box.Width(), expectedLetterboxSize) && Close(box.Height(), expectedLetterboxSize),
              "Letterbox centres a square image and preserves the surrounding bars");

        float windowX = 0.0f;
        float windowY = 0.0f;
        const bool hasWindowTransform = renderer.TransformLogicalToWindow(
            kLogicalSize * 0.5f, kLogicalSize * 0.5f, windowX, windowY);
        if (hasWindowTransform)
        {
            float roundTripX = 0.0f;
            float roundTripY = 0.0f;
            const bool inside = renderer.TransformWindowToLogical(
                windowX, windowY, roundTripX, roundTripY);
            Check(inside && std::fabs(roundTripX - kLogicalSize * 0.5f) < 0.05f &&
                  std::fabs(roundTripY - kLogicalSize * 0.5f) < 0.05f,
                  "logical/window transforms round-trip the Letterbox centre");

            float originX = 0.0f;
            float originY = 0.0f;
            renderer.TransformLogicalToWindow(0.0f, 0.0f, originX, originY);
            float ignoredX = 0.0f;
            float ignoredY = 0.0f;
            const bool barHasLogicalPoint = originX > 0.5f
                ? renderer.TransformWindowToLogical(
                    originX * 0.5f, windowY, ignoredX, ignoredY)
                : renderer.TransformWindowToLogical(
                    windowX, originY * 0.5f, ignoredX, ignoredY);
            Check(!barHasLogicalPoint,
                  "a point on a Letterbox bar has no logical counterpart");
        }
        else
        {
            std::printf("[INFO] native-window coordinate transform unavailable on this headless device; pixel presentation checks continue\n");
        }

        box = RenderMode(CnaPresentationMode::Stretch, physicalWidth, physicalHeight,
                         logicalWidth, logicalHeight, x, y, width, height);
        PrintGeometry("Stretch", logicalWidth, logicalHeight, x, y, width, height, box);
        Check(logicalWidth == kLogicalSize && logicalHeight == kLogicalSize &&
              x == 0 && y == 0 && width == physicalWidth && height == physicalHeight &&
              box.minX == 0 && box.minY == 0 && box.Width() == physicalWidth &&
              box.Height() == physicalHeight,
              "Stretch maps the fixed logical canvas over the full back buffer");

        box = RenderMode(CnaPresentationMode::Overscan, physicalWidth, physicalHeight,
                         logicalWidth, logicalHeight, x, y, width, height);
        PrintGeometry("Overscan", logicalWidth, logicalHeight, x, y, width, height, box);
        Check(logicalWidth == kLogicalSize && logicalHeight == kLogicalSize &&
              (x < 0 || y < 0) && box.minX == 0 && box.minY == 0 &&
              box.Width() == physicalWidth && box.Height() == physicalHeight,
              "Overscan uniformly covers the back buffer and clips the excess");

        box = RenderMode(CnaPresentationMode::NativeBackBuffer, physicalWidth, physicalHeight,
                         logicalWidth, logicalHeight, x, y, width, height);
        PrintGeometry("NativeBackBuffer", logicalWidth, logicalHeight, x, y, width, height, box);
        Check(logicalWidth == physicalWidth && logicalHeight == physicalHeight &&
              x == 0 && y == 0 && width == physicalWidth && height == physicalHeight &&
              Close(box.minX, 0) && Close(box.minY, 0) &&
              Close(box.Width(), kLogicalSize) && Close(box.Height(), kLogicalSize),
              "NativeBackBuffer exposes physical dimensions and leaves a 40x40 sprite unscaled");

        box = RenderMode(CnaPresentationMode::FixedHeightDynamicWidth,
                         physicalWidth, physicalHeight,
                         logicalWidth, logicalHeight, x, y, width, height);
        PrintGeometry("FixedHeightDynamicWidth", logicalWidth, logicalHeight,
                      x, y, width, height, box);
        const int expectedDynamicWidth = static_cast<int>(std::lround(
            static_cast<double>(physicalWidth) * kLogicalSize / physicalHeight));
        Check(logicalWidth == expectedDynamicWidth && logicalHeight == kLogicalSize &&
              x == 0 && y == 0 && width == physicalWidth && height == physicalHeight &&
              box.minX == 0 && box.minY == 0 && box.Width() == physicalWidth &&
              box.Height() == physicalHeight,
              "FixedHeightDynamicWidth preserves height, derives width, and fills the back buffer");

        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
        renderer.SetVirtualResolution(physicalWidth, physicalHeight);
        device.setViewportProperty(Viewport(0, 0, physicalWidth, physicalHeight));
        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    PresentationModeContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kPhysicalWidth);
        manager_->setPreferredBackBufferHeightProperty(kPhysicalHeight);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    PresentationModeContractTest game;
    game.Run();
    return game.Result();
}
