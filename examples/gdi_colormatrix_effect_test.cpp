// SPDX-License-Identifier: MS-PL
// High-level integration coverage for GDI-022's fixed CPU SpriteBatch ColorMatrixEffect.

#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
            std::fprintf(stderr, "%s\n", message);
        return condition;
    }

    bool ExpectColor(const char* label, const Color& actual,
                     std::array<int, 4> expected, int tolerance = 1)
    {
        const std::array<int, 4> actualChannels{
            actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), actual.getAProperty() };
        for (std::size_t channel = 0; channel < actualChannels.size(); ++channel)
        {
            if (std::abs(actualChannels[channel] - expected[channel]) > tolerance)
            {
                std::fprintf(stderr, "%s: expected RGBA(%d,%d,%d,%d), got RGBA(%d,%d,%d,%d).\n",
                             label, expected[0], expected[1], expected[2], expected[3],
                             actualChannels[0], actualChannels[1], actualChannels[2], actualChannels[3]);
                return false;
            }
        }
        return true;
    }

    Color DrawAndRead(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture,
                      ColorMatrixEffect& effect)
    {
        device.Clear(Color::Black);
        sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
        sprites.Draw(texture, Rectangle(1, 1, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites.End();

        Color result(0, 0, 0, 0);
        const Rectangle sample(1, 1, 1, 1);
        device.GetBackBufferData(&sample, &result, 0, 1);
        return result;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI ColorMatrixEffect", 4, 4, SDL_WINDOW_HIDDEN);
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
        parameters.setBackBufferWidthProperty(4);
        parameters.setBackBufferHeightProperty(4);
        parameters.setDeviceWindowHandleProperty(
            reinterpret_cast<PresentationParameters::IntPtr>(window));
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        if (CNA::getCurrentGraphicsRendererType() != CNA::GraphicsRendererType::Gdi)
        {
            std::fprintf(stderr, "ColorMatrixEffect integration test was not built for GDI.\n");
            result = 1;
        }

        Texture2D texture(device, 1, 1);
        const Color source(100, 200, 50, 128);
        texture.SetData(&source, 1);
        SpriteBatch sprites(device);
        ColorMatrixEffect effect(device);

        std::array<float, 16> invalidMatrix = effect.GetColorMatrix();
        invalidMatrix[0] = std::numeric_limits<float>::quiet_NaN();
        bool rejectedInvalidMatrix = false;
        try { effect.SetColorMatrix(invalidMatrix); }
        catch (const std::invalid_argument&) { rejectedInvalidMatrix = true; }
        result |= !Expect(rejectedInvalidMatrix,
                          "ColorMatrixEffect must reject non-finite matrix coefficients.");

        bool rejectedInvalidOffset = false;
        try { effect.SetColorOffset(Vector4(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f)); }
        catch (const std::invalid_argument&) { rejectedInvalidOffset = true; }
        result |= !Expect(rejectedInvalidOffset,
                          "ColorMatrixEffect must reject non-finite offset components.");

        // The fixed effect is a deliberately narrow exception, not a back door for ShaderEffect.
        bool rejectedCustomShader = false;
        try
        {
            ShaderEffect customShader(device, "void main() {}", "void main() {}");
            (void)customShader;
        }
        catch (const System::NotSupportedException&)
        {
            rejectedCustomShader = true;
        }
        result |= !Expect(rejectedCustomShader,
                          "GDI must reject ShaderEffect construction while accepting ColorMatrixEffect.");

        effect.SetGrayscale();
        result |= !ExpectColor("ColorMatrixEffect grayscale",
                               DrawAndRead(device, sprites, texture, effect),
                               { 167, 167, 167, 128 });

        effect.SetColorMatrix({
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        });
        effect.SetColorOffset(Vector4(0.1f, 0.0f, 0.0f, 0.0f));
        result |= !ExpectColor("ColorMatrixEffect matrix and offset",
                               DrawAndRead(device, sprites, texture, effect),
                               { 75, 50, 200, 128 });
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI ColorMatrixEffect test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
