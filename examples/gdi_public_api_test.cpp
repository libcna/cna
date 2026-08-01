// SPDX-License-Identifier: MS-PL
// GDI-055: high-level coverage for every advertised GDI graphics feature.
//
// This executable intentionally uses only the public CNA/XNA surface. Low-level raster details
// remain in gdi_2d_regression_test; this test proves that the same supported behavior is reachable
// through GraphicsDevice and its public resources.

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kInitialWidth = 8;
    constexpr int kInitialHeight = 6;
    constexpr int kResizedWidth = 12;
    constexpr int kResizedHeight = 8;

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    [[nodiscard]] bool SameColor(const Color& actual, const Color& expected,
                                 int tolerance = 0)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
               std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
               std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance &&
               std::abs(actual.getAProperty() - expected.getAProperty()) <= tolerance;
    }

    [[nodiscard]] bool SameRectangle(const Rectangle& actual, const Rectangle& expected)
    {
        return actual.X == expected.X && actual.Y == expected.Y &&
               actual.Width == expected.Width && actual.Height == expected.Height;
    }

    [[nodiscard]] bool SameViewport(const Viewport& actual, const Rectangle& expected)
    {
        return actual.getXProperty() == expected.X &&
               actual.getYProperty() == expected.Y &&
               actual.getWidthProperty() == expected.Width &&
               actual.getHeightProperty() == expected.Height &&
               actual.getMinDepthProperty() == 0.0f &&
               actual.getMaxDepthProperty() == 1.0f;
    }

    [[nodiscard]] std::vector<Color> ReadBackbuffer(GraphicsDevice& device, int width, int height)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(width * height), Color::Transparent);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    [[nodiscard]] const Color& PixelAt(const std::vector<Color>& pixels, int width, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y * width + x)];
    }

    void Draw(SpriteBatch& sprites, const Texture2D& texture, const Rectangle& destination,
              const Rectangle& source, RasterizerState* rasterizer = nullptr,
              float rotation = 0.0f, Vector2 origin = Vector2(0.0f, 0.0f))
    {
        SamplerState point = SamplerState::PointClamp;
        sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, nullptr, rasterizer);
        sprites.Draw(texture, destination, source, Color::White, rotation, origin,
                     SpriteEffects::None, 0.0f);
        sprites.End();
    }

    bool ExercisePresentationAndCapabilities(GraphicsDevice& device, SDL_Window* window)
    {
        bool ok = true;
        const PresentationParameters& parameters = device.getPresentationParametersProperty();
        const Rectangle initialBounds(0, 0, kInitialWidth, kInitialHeight);

        ok &= Expect(CNA::getCurrentGraphicsBackendType() == CNA::GraphicsBackendType::Gdi,
                     "test executable uses the GDI backend");
        ok &= Expect(parameters.getBackBufferWidthProperty() == kInitialWidth &&
                         parameters.getBackBufferHeightProperty() == kInitialHeight,
                     "PresentationParameters exposes the configured backbuffer dimensions");
        ok &= Expect(parameters.getBackBufferFormatProperty() == SurfaceFormat::Color &&
                         parameters.getDepthStencilFormatProperty() == DepthFormat::None &&
                         parameters.getMultiSampleCountProperty() == 0,
                     "PresentationParameters exposes the supported initial format and sample state");
        ok &= Expect(parameters.getDeviceWindowHandleProperty() ==
                         reinterpret_cast<PresentationParameters::IntPtr>(window),
                     "PresentationParameters retains the caller-provided SDL window");
        ok &= Expect(SameRectangle(parameters.getBoundsProperty(), initialBounds),
                     "PresentationParameters.Bounds matches the initial backbuffer");
        ok &= Expect(SameViewport(device.getViewportProperty(), initialBounds),
                     "GraphicsDevice starts with a full-backbuffer viewport");
        ok &= Expect(SameRectangle(device.getScissorRectangleProperty(), initialBounds),
                     "GraphicsDevice starts with a full-backbuffer scissor rectangle");

        struct CapabilityExpectation
        {
            CNA::GraphicsCapability capability;
            bool supported;
            const char* label;
        };
        constexpr CapabilityExpectation expectations[] = {
            { CNA::GraphicsCapability::ThreeD, false, "3D pipeline" },
            { CNA::GraphicsCapability::DepthStencilBuffer, false, "depth/stencil attachment" },
            { CNA::GraphicsCapability::MultiSampleAntiAliasing, true, "4x backbuffer MSAA" },
            { CNA::GraphicsCapability::MultipleRenderTargets, false, "multiple render targets" },
            { CNA::GraphicsCapability::AnisotropicFiltering, false, "anisotropic filtering" },
            { CNA::GraphicsCapability::WireFrame, true, "CPU SpriteBatch wireframe" },
            { CNA::GraphicsCapability::OcclusionQuery, false, "occlusion queries" },
            { CNA::GraphicsCapability::CustomEffects, false, "custom effects" },
            { CNA::GraphicsCapability::Texture3D, false, "3D textures" },
            { CNA::GraphicsCapability::StencilBuffer, true, "standalone stencil plane" },
        };
        for (const CapabilityExpectation& expectation : expectations)
        {
            char message[160]{};
            std::snprintf(message, sizeof(message), "GDI capability matrix reports %s as %s",
                          expectation.label,
                          expectation.supported ? "supported" : "unsupported");
            ok &= Expect(device.SupportsCapability(expectation.capability) == expectation.supported,
                         message);
        }
        return ok;
    }

    bool ExerciseTextureAndScissor(GraphicsDevice& device, SpriteBatch& sprites,
                                   Texture2D& atlas)
    {
        bool ok = true;
        const std::array<Color, 4> uploaded = {
            Color::Red, Color::Green, Color::Blue, Color(255, 255, 0, 255),
        };
        atlas.SetData(uploaded.data(), static_cast<int>(uploaded.size()));

        std::array<Color, 4> downloaded = {
            Color::Transparent, Color::Transparent, Color::Transparent, Color::Transparent,
        };
        atlas.GetData(downloaded.data(), static_cast<int>(downloaded.size()));
        bool exactRoundTrip = true;
        for (std::size_t index = 0; index < uploaded.size(); ++index)
            exactRoundTrip &= SameColor(downloaded[index], uploaded[index]);
        ok &= Expect(atlas.getWidthProperty() == 2 && atlas.getHeightProperty() == 2 &&
                         atlas.getFormatProperty() == SurfaceFormat::Color &&
                         atlas.getLevelCountProperty() == 1,
                     "Texture2D exposes its applied RGBA8 storage properties");
        ok &= Expect(exactRoundTrip, "Texture2D SetData/GetData round-trips public Color data");

        device.Clear(Color::Black);
        Draw(sprites, atlas, Rectangle(1, 1, 2, 2), Rectangle(1, 0, 1, 1));
        std::vector<Color> pixels = ReadBackbuffer(device, kInitialWidth, kInitialHeight);
        ok &= Expect(SameColor(PixelAt(pixels, kInitialWidth, 1, 1), Color::Green) &&
                         SameColor(PixelAt(pixels, kInitialWidth, 0, 0), Color::Black),
                     "SpriteBatch source rectangles reach the GDI backbuffer through public APIs");

        const Rectangle viewportBounds(1, 1, 6, 4);
        const Rectangle scissorBounds(3, 2, 2, 2);
        device.setViewportProperty(Viewport(viewportBounds));
        device.setScissorRectangleProperty(scissorBounds);
        RasterizerState scissorState;
        scissorState.setScissorTestEnableProperty(true);
        device.Clear(Color::Black);
        Draw(sprites, atlas, Rectangle(0, 0, 6, 4), Rectangle(0, 0, 1, 1), &scissorState);
        pixels = ReadBackbuffer(device, kInitialWidth, kInitialHeight);

        bool clippedExactly = true;
        for (int y = 0; y < kInitialHeight; ++y)
        {
            for (int x = 0; x < kInitialWidth; ++x)
            {
                const Color& expected = scissorBounds.Contains(x, y) ? Color::Red : Color::Black;
                clippedExactly &= SameColor(PixelAt(pixels, kInitialWidth, x, y), expected);
            }
        }
        ok &= Expect(SameViewport(device.getViewportProperty(), viewportBounds) &&
                         SameRectangle(device.getScissorRectangleProperty(), scissorBounds),
                     "GraphicsDevice exposes the applied viewport and framebuffer-space scissor");
        ok &= Expect(clippedExactly,
                     "public SpriteBatch rasterization clips to viewport intersected with scissor");
        return ok;
    }

    bool ExerciseRenderTarget(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& atlas)
    {
        bool ok = true;
        constexpr int targetWidth = 4;
        constexpr int targetHeight = 3;
        const Rectangle targetBounds(0, 0, targetWidth, targetHeight);
        RenderTarget2D target(device, targetWidth, targetHeight, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        ok &= Expect(target.getWidthProperty() == targetWidth &&
                         target.getHeightProperty() == targetHeight &&
                         target.getFormatProperty() == SurfaceFormat::Color &&
                         target.getLevelCountProperty() == 1 &&
                         target.getDepthStencilFormatProperty() == DepthFormat::None &&
                         target.getMultiSampleCountProperty() == 0 &&
                         target.getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents,
                     "RenderTarget2D reports the supported applied resource configuration");

        device.SetRenderTarget(&target);
        const std::vector<RenderTargetBinding> bindings = device.GetRenderTargets();
        ok &= Expect(bindings.size() == 1 && bindings[0].getRenderTargetProperty() == &target,
                     "GraphicsDevice.GetRenderTargets exposes the bound public target");
        ok &= Expect(SameViewport(device.getViewportProperty(), targetBounds) &&
                         SameRectangle(device.getScissorRectangleProperty(), targetBounds),
                     "binding RenderTarget2D resets viewport and scissor to its dimensions");
        device.Clear(Color::Green);
        Draw(sprites, atlas, Rectangle(1, 1, 1, 1), Rectangle(0, 0, 1, 1));
        device.SetRenderTarget(nullptr);

        std::vector<Color> targetPixels(targetWidth * targetHeight, Color::Transparent);
        target.GetData(targetPixels.data(), static_cast<int>(targetPixels.size()));
        bool targetContentsCorrect = true;
        for (int y = 0; y < targetHeight; ++y)
        {
            for (int x = 0; x < targetWidth; ++x)
            {
                const Color& expected = (x == 1 && y == 1) ? Color::Red : Color::Green;
                targetContentsCorrect &= SameColor(PixelAt(targetPixels, targetWidth, x, y), expected);
            }
        }
        ok &= Expect(targetContentsCorrect,
                     "RenderTarget2D receives SpriteBatch pixels and reads them back publicly");

        device.SetRenderTarget(&target);
        device.SetRenderTarget(nullptr);
        target.GetData(targetPixels.data(), static_cast<int>(targetPixels.size()));
        ok &= Expect(SameColor(PixelAt(targetPixels, targetWidth, 0, 0), Color::Green) &&
                         SameColor(PixelAt(targetPixels, targetWidth, 1, 1), Color::Red),
                     "PreserveContents survives a public RenderTarget2D rebind");

        device.Clear(Color::Black);
        Draw(sprites, target, targetBounds, targetBounds);
        const std::vector<Color> backbuffer =
            ReadBackbuffer(device, kInitialWidth, kInitialHeight);
        ok &= Expect(SameColor(PixelAt(backbuffer, kInitialWidth, 0, 0), Color::Green) &&
                         SameColor(PixelAt(backbuffer, kInitialWidth, 1, 1), Color::Red) &&
                         SameColor(PixelAt(backbuffer, kInitialWidth, 4, 0), Color::Black),
                     "RenderTarget2D can be sampled by public SpriteBatch after unbinding");
        return ok;
    }

    bool ExerciseResetResizeAndMsaa(GraphicsDevice& device, SpriteBatch& sprites,
                                    Texture2D& atlas)
    {
        bool ok = true;
        PresentationParameters resized = device.getPresentationParametersProperty().Clone();
        resized.setBackBufferWidthProperty(kResizedWidth);
        resized.setBackBufferHeightProperty(kResizedHeight);
        resized.setMultiSampleCountProperty(4);
        device.Reset(resized);

        const PresentationParameters& applied = device.getPresentationParametersProperty();
        const Rectangle resizedBounds(0, 0, kResizedWidth, kResizedHeight);
        ok &= Expect(applied.getBackBufferWidthProperty() == kResizedWidth &&
                         applied.getBackBufferHeightProperty() == kResizedHeight &&
                         applied.getMultiSampleCountProperty() == 4,
                     "GraphicsDevice.Reset applies resize and public 4x MSAA state");
        ok &= Expect(SameViewport(device.getViewportProperty(), resizedBounds) &&
                         SameRectangle(device.getScissorRectangleProperty(), resizedBounds),
                     "GraphicsDevice.Reset restores full resized viewport and scissor state");

        device.Clear(Color::Black);
        Draw(sprites, atlas, Rectangle(6, 2, 4, 4), Rectangle(0, 0, 1, 1), nullptr,
             0.78539816339f, Vector2(0.5f, 0.5f));
        std::vector<Color> pixels = ReadBackbuffer(device, kResizedWidth, kResizedHeight);
        ok &= Expect(SameColor(PixelAt(pixels, kResizedWidth, 3, 1),
                                     Color(63, 0, 0, 255), 1),
                     "public 4x MSAA reset resolves fractional SpriteBatch edge coverage");

        PresentationParameters unsupported = applied.Clone();
        unsupported.setMultiSampleCountProperty(2);
        device.Reset(unsupported);
        ok &= Expect(device.getPresentationParametersProperty().getMultiSampleCountProperty() == 0,
                     "GraphicsDevice.Reset writes back zero for unsupported GDI 2x MSAA");

        device.Clear(Color::Blue);
        pixels = ReadBackbuffer(device, kResizedWidth, kResizedHeight);
        ok &= Expect(SameColor(PixelAt(pixels, kResizedWidth,
                                      kResizedWidth - 1, kResizedHeight - 1), Color::Blue),
                     "backbuffer readback reaches the new edge after public resize");
        return ok;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI public API", kInitialWidth, kInitialHeight,
                                          SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(kInitialWidth);
        parameters.setBackBufferHeightProperty(kInitialHeight);
        parameters.setBackBufferFormatProperty(SurfaceFormat::Color);
        parameters.setDepthStencilFormatProperty(DepthFormat::None);
        parameters.setMultiSampleCountProperty(0);
        parameters.setDeviceWindowHandleProperty(
            reinterpret_cast<PresentationParameters::IntPtr>(window));

        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::Reach, parameters);
        SpriteBatch sprites(device);
        Texture2D atlas(device, 2, 2, false, SurfaceFormat::Color);

        bool ok = true;
        ok &= ExercisePresentationAndCapabilities(device, window);
        ok &= ExerciseTextureAndScissor(device, sprites, atlas);
        ok &= ExerciseRenderTarget(device, sprites, atlas);
        ok &= ExerciseResetResizeAndMsaa(device, sprites, atlas);
        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI public API test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
