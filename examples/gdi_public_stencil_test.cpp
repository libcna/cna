// SPDX-License-Identifier: MS-PL
// GDI-050: public-API coverage for GDI's standalone CPU stencil plane.

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <exception>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;
    constexpr int kStencilReference = 0x5A;
    const Rectangle kMask(2, 2, 3, 3);
    const Rectangle kFull(0, 0, kWidth, kHeight);

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    [[nodiscard]] bool SameColor(const Color& left, const Color& right)
    {
        return left.getRProperty() == right.getRProperty() &&
               left.getGProperty() == right.getGProperty() &&
               left.getBProperty() == right.getBProperty() &&
               left.getAProperty() == right.getAProperty();
    }

    [[nodiscard]] DepthStencilState StencilWriteState()
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Always);
        state.setStencilPassProperty(StencilOperation::Replace);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setStencilMaskProperty(0xFF);
        state.setStencilWriteMaskProperty(0xFF);
        state.setReferenceStencilProperty(kStencilReference);
        return state;
    }

    [[nodiscard]] DepthStencilState StencilTestState()
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Equal);
        state.setStencilPassProperty(StencilOperation::Keep);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setStencilMaskProperty(0xFF);
        state.setStencilWriteMaskProperty(0xFF);
        state.setReferenceStencilProperty(kStencilReference);
        return state;
    }

    void Draw(SpriteBatch& sprites, Texture2D& texture, const Rectangle& destination,
              DepthStencilState& state)
    {
        sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque,
                      nullptr, &state, nullptr);
        sprites.Draw(texture, destination, Rectangle(0, 0, 1, 1), Color::White);
        sprites.End();
    }

    void StampMask(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture)
    {
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        DepthStencilState write = StencilWriteState();
        Draw(sprites, texture, kMask, write);
    }

    void RevealMask(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture)
    {
        device.Clear(ClearOptions::Target, Color::Black, 1.0f, 0);
        DepthStencilState test = StencilTestState();
        Draw(sprites, texture, kFull, test);
    }

    bool ExpectMask(const std::vector<Color>& pixels, bool visible,
                    const char* message)
    {
        bool correct = true;
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const bool inside = kMask.Contains(x, y);
                const Color& expected = (visible && inside) ? Color::Red : Color::Black;
                if (!SameColor(pixels[static_cast<std::size_t>(y * kWidth + x)], expected))
                    correct = false;
            }
        }
        return Expect(correct, message);
    }

    bool ExerciseBackbuffer(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture)
    {
        bool ok = true;
        std::vector<Color> pixels(kWidth * kHeight, Color::Transparent);

        StampMask(device, sprites, texture);
        RevealMask(device, sprites, texture);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        ok &= ExpectMask(pixels, true,
                         "public SpriteBatch stencil test gates the GDI backbuffer");

        device.Clear(ClearOptions::Stencil, Color::Black, 1.0f, 0);
        RevealMask(device, sprites, texture);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        ok &= ExpectMask(pixels, false,
                         "public stencil-only Clear resets the GDI backbuffer stencil");

        StampMask(device, sprites, texture);
        device.Clear(Color::Black);
        DepthStencilState test = StencilTestState();
        Draw(sprites, texture, kFull, test);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        ok &= ExpectMask(pixels, false,
                         "single-colour Clear resets the GDI backbuffer stencil");
        return ok;
    }

    bool ReadTargetAndExpect(RenderTarget2D& target, bool visible, const char* message)
    {
        std::vector<Color> pixels(kWidth * kHeight, Color::Transparent);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return ExpectMask(pixels, visible, message);
    }

    bool ExerciseRenderTarget(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture)
    {
        bool ok = true;
        RenderTarget2D target(device, kWidth, kHeight, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        device.SetRenderTarget(&target);
        StampMask(device, sprites, texture);
        RevealMask(device, sprites, texture);
        device.SetRenderTarget(nullptr);
        ok &= ReadTargetAndExpect(target, true,
                                  "public SpriteBatch stencil test gates a depthless GDI RenderTarget2D");

        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Stencil, Color::Black, 1.0f, 0);
        RevealMask(device, sprites, texture);
        device.SetRenderTarget(nullptr);
        ok &= ReadTargetAndExpect(target, false,
                                  "public stencil-only Clear resets a depthless GDI RenderTarget2D");

        device.SetRenderTarget(&target);
        StampMask(device, sprites, texture);
        device.Clear(Color::Black);
        DepthStencilState test = StencilTestState();
        Draw(sprites, texture, kFull, test);
        device.SetRenderTarget(nullptr);
        ok &= ReadTargetAndExpect(target, false,
                                  "single-colour Clear resets a depthless GDI RenderTarget2D stencil");
        return ok;
    }

    bool ExerciseRenderTargetUsage(GraphicsDevice& device, SpriteBatch& sprites,
                                   Texture2D& texture, RenderTargetUsage usage,
                                   bool expectPreserved, const char* message)
    {
        RenderTarget2D target(device, kWidth, kHeight, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, usage);
        device.SetRenderTarget(&target);
        StampMask(device, sprites, texture);
        device.SetRenderTarget(nullptr);

        // Rebinding is the usage-policy boundary. Replace colour only, then ask whether the
        // stencil mask survived that boundary. DiscardContents must have reset it during bind;
        // PreserveContents and PlatformContents must retain it.
        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target, Color::Black, 1.0f, 0);
        DepthStencilState test = StencilTestState();
        Draw(sprites, texture, kFull, test);
        device.SetRenderTarget(nullptr);
        return ReadTargetAndExpect(target, expectPreserved, message);
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI public stencil", kWidth, kHeight,
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
        parameters.setBackBufferWidthProperty(kWidth);
        parameters.setBackBufferHeightProperty(kHeight);
        parameters.setDeviceWindowHandleProperty(
            reinterpret_cast<PresentationParameters::IntPtr>(window));
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::Reach, parameters);

        bool ok = true;
        ok &= Expect(CNA::getCurrentGraphicsBackendType() == CNA::GraphicsBackendType::Gdi,
                     "test executable uses the GDI backend");
        ok &= Expect(device.SupportsCapability(CNA::GraphicsCapability::StencilBuffer),
                     "GDI advertises its standalone stencil plane");
        ok &= Expect(!device.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer),
                     "GDI does not advertise a combined depth/stencil attachment");

        Texture2D texture(device, 1, 1);
        const Color red = Color::Red;
        texture.SetData(&red, 1);
        SpriteBatch sprites(device);

        ok &= ExerciseBackbuffer(device, sprites, texture);
        ok &= ExerciseRenderTarget(device, sprites, texture);
        ok &= ExerciseRenderTargetUsage(
            device, sprites, texture, RenderTargetUsage::PreserveContents, true,
            "PreserveContents retains the standalone GDI stencil plane across rebind");
        ok &= ExerciseRenderTargetUsage(
            device, sprites, texture, RenderTargetUsage::PlatformContents, true,
            "PlatformContents retains the standalone GDI stencil plane across rebind");
        ok &= ExerciseRenderTargetUsage(
            device, sprites, texture, RenderTargetUsage::DiscardContents, false,
            "DiscardContents resets the standalone GDI stencil plane on rebind");
        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI public stencil test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
