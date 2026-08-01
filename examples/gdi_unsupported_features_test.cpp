// SPDX-License-Identifier: MS-PL
// GDI-059: public unsupported-feature construction and invocation contract.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    template <typename Action>
    bool ExpectNotSupported(const char* message, const char* diagnosticFragment, Action&& action)
    {
        try
        {
            std::forward<Action>(action)();
        }
        catch (const System::NotSupportedException& error)
        {
            const bool diagnosticMatches =
                std::string(error.what()).find(diagnosticFragment) != std::string::npos;
            if (!diagnosticMatches)
            {
                std::fprintf(stderr, "Unexpected NotSupportedException diagnostic: %s\n",
                             error.what());
            }
            return Expect(diagnosticMatches, message);
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Wrong exception family for '%s': %s\n", message, error.what());
            return Expect(false, message);
        }

        std::fprintf(stderr, "Unsupported operation returned normally: %s\n", message);
        return Expect(false, message);
    }

    bool ExerciseUnsupportedResources(GraphicsDevice& device)
    {
        bool ok = true;
        ok &= ExpectNotSupported(
            "TextureCube construction fails immediately with NotSupportedException",
            "TextureCube resources", [&]
            {
                TextureCube texture(device, 2, false, SurfaceFormat::Color);
                (void)texture;
            });
        ok &= ExpectNotSupported(
            "Texture3D construction fails immediately with NotSupportedException",
            "Texture3D", [&]
            {
                Texture3D texture(device, 2, 2, 2, false, SurfaceFormat::Color);
                (void)texture;
            });
        ok &= ExpectNotSupported(
            "RenderTargetCube construction fails immediately with NotSupportedException",
            "RenderTargetCube resources", [&]
            {
                RenderTargetCube target(device, 2, false, SurfaceFormat::Color,
                                        DepthFormat::None, 0,
                                        RenderTargetUsage::DiscardContents);
                (void)target;
            });
        ok &= ExpectNotSupported(
            "ShaderEffect construction fails immediately with NotSupportedException",
            "ShaderEffect programs", [&]
            {
                ShaderEffect effect(device, "void main() {}", "void main() {}");
                (void)effect;
            });
        ok &= ExpectNotSupported(
            "OcclusionQuery construction fails immediately with NotSupportedException",
            "occlusion queries", [&]
            {
                OcclusionQuery query(device);
                (void)query;
            });
        return ok;
    }

    bool ExerciseUnsupportedBuffers(GraphicsDevice& device)
    {
        bool ok = true;
        ok &= ExpectNotSupported(
            "VertexBuffer construction fails immediately with NotSupportedException",
            "vertex buffers", [&]
            {
                VertexBuffer buffer(device, 3);
                (void)buffer;
            });
        ok &= ExpectNotSupported(
            "16-bit IndexBuffer construction fails immediately with NotSupportedException",
            "16-bit index buffers", [&]
            {
                IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3,
                                   BufferUsage::None);
                (void)buffer;
            });
        ok &= ExpectNotSupported(
            "32-bit IndexBuffer construction fails immediately with NotSupportedException",
            "32-bit index buffers", [&]
            {
                IndexBuffer buffer(device, IndexElementSize::ThirtyTwoBits, 3,
                                   BufferUsage::None);
                (void)buffer;
            });
        ok &= ExpectNotSupported(
            "DynamicVertexBuffer construction follows the same unsupported policy",
            "vertex buffers", [&]
            {
                DynamicVertexBuffer buffer(
                    device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                    BufferUsage::WriteOnly);
                (void)buffer;
            });
        ok &= ExpectNotSupported(
            "DynamicIndexBuffer construction follows the same unsupported policy",
            "16-bit index buffers", [&]
            {
                DynamicIndexBuffer buffer(device, IndexElementSize::SixteenBits, 3,
                                          BufferUsage::WriteOnly);
                (void)buffer;
            });
        return ok;
    }

    bool ExerciseUnsupported3DCalls(GraphicsDevice& device)
    {
        bool ok = true;
        ok &= ExpectNotSupported(
            "direct depth-state entry fails with NotSupportedException",
            "SetDepthTestEnabled", [&] { device.SetDepthTestEnabled(true); });

        BasicEffect effect(device);
        effect.Apply();
        const std::array<VertexPositionColor, 3> vertices = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Red),
            VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::Green),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color::Blue),
        };
        const std::array<std::uint16_t, 3> indices = { 0, 1, 2 };

        ok &= ExpectNotSupported(
            "DrawUserPrimitives fails before allocating an inherited Software 3D buffer",
            "vertex buffers", [&]
            {
                device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
            });
        ok &= ExpectNotSupported(
            "DrawUserIndexedPrimitives follows the same public 3D rejection policy",
            "vertex buffers", [&]
            {
                device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                                 vertices.data(), 0,
                                                 static_cast<int>(vertices.size()),
                                                 indices.data(), 0, 1);
            });
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

    SDL_Window* window = SDL_CreateWindow("CNA GDI unsupported features", 8, 8,
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
        parameters.setBackBufferWidthProperty(8);
        parameters.setBackBufferHeightProperty(8);
        parameters.setBackBufferFormatProperty(SurfaceFormat::Color);
        parameters.setDepthStencilFormatProperty(DepthFormat::None);
        parameters.setDeviceWindowHandleProperty(
            reinterpret_cast<PresentationParameters::IntPtr>(window));

        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::Reach, parameters);
        bool ok = true;
        ok &= Expect(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                     "GDI continues to report ThreeD=false");
        ok &= Expect(!device.SupportsCapability(CNA::GraphicsCapability::Texture3D) &&
                         !device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery) &&
                         !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
                     "resource-specific capability answers remain false");
        ok &= ExerciseUnsupportedResources(device);
        ok &= ExerciseUnsupportedBuffers(device);
        ok &= ExerciseUnsupported3DCalls(device);
        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI unsupported-feature test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
