// SPDX-License-Identifier: MS-PL
// SDLGPU-132: the SDL GPU backbuffer must apply the requested depth/stencil contract and report
// its fixed Color transfer format truthfully across construction and Reset.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;

namespace
{
    constexpr int kSize = 32;
    int passed = 0;
    int failed = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        condition ? ++passed : ++failed;
    }

    Color DrawDepthPair(GraphicsDevice& device, BasicEffect& effect)
    {
        const VertexPositionColor vertices[12] = {
            {Vector3(-1.0f,  1.0f, 0.2f), Color::Red},
            {Vector3(-1.0f, -1.0f, 0.2f), Color::Red},
            {Vector3( 1.0f, -1.0f, 0.2f), Color::Red},
            {Vector3(-1.0f,  1.0f, 0.2f), Color::Red},
            {Vector3( 1.0f, -1.0f, 0.2f), Color::Red},
            {Vector3( 1.0f,  1.0f, 0.2f), Color::Red},
            {Vector3(-1.0f,  1.0f, 0.8f), Color::Green},
            {Vector3(-1.0f, -1.0f, 0.8f), Color::Green},
            {Vector3( 1.0f, -1.0f, 0.8f), Color::Green},
            {Vector3(-1.0f,  1.0f, 0.8f), Color::Green},
            {Vector3( 1.0f, -1.0f, 0.8f), Color::Green},
            {Vector3( 1.0f,  1.0f, 0.8f), Color::Green},
        };

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color(3, 5, 7, 255), 1.0f, 0);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 4);

        Color pixel(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        device.GetBackBufferData(&probe, &pixel, 0, 1);
        return pixel;
    }

    void ResetFormats(GraphicsDevice& device, SurfaceFormat color, DepthFormat depth)
    {
        PresentationParameters next = device.getPresentationParametersProperty().Clone();
        next.setBackBufferFormatProperty(color);
        next.setDepthStencilFormatProperty(depth);
        device.Reset(next);
    }
}

int main()
{
    try
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(kSize);
        parameters.setBackBufferHeightProperty(kSize);
        parameters.setBackBufferFormatProperty(SurfaceFormat::Color);
        parameters.setDepthStencilFormatProperty(DepthFormat::None);
        parameters.setPresentationIntervalProperty(PresentInterval::Immediate);
        parameters.setHeadlessEXTProperty(true);

        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::Reach, parameters);
        auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());

        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);
        effect.setTextureEnabledProperty(false);
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                  DepthFormat::None,
              "construction reports requested DepthFormat::None");
        Check(!renderer.SupportsDepthBuffer() && !renderer.SupportsStencilBuffer(),
              "DepthFormat::None allocates neither a depth nor stencil plane");
        Check(DrawDepthPair(device, effect) == Color::Green,
              "without a depth plane the later far green draw wins");

        ResetFormats(device, SurfaceFormat::Color, DepthFormat::Depth24);
        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                  DepthFormat::Depth24,
              "Reset reports applied Depth24");
        Check(renderer.SupportsDepthBuffer() && !renderer.SupportsStencilBuffer(),
              "Depth24 exposes depth without falsely advertising stencil");
        Check(DrawDepthPair(device, effect) == Color::Red,
              "Depth24 rejects the later farther draw");

        ResetFormats(device, SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                  DepthFormat::Depth24Stencil8,
              "Reset reports applied Depth24Stencil8");
        Check(renderer.SupportsDepthBuffer() && renderer.SupportsStencilBuffer(),
              "Depth24Stencil8 exposes both native planes");
        Check(DrawDepthPair(device, effect) == Color::Red,
              "Depth24Stencil8 rejects the later farther draw");

        ResetFormats(device, SurfaceFormat::Bgr565, DepthFormat::Depth16);
        Check(device.getPresentationParametersProperty().getBackBufferFormatProperty() ==
                  SurfaceFormat::Color,
              "fixed RGBA8/BGRA8 backbuffer reports logical SurfaceFormat::Color");
        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                  DepthFormat::Depth16,
              "Reset reports applied Depth16 even when native storage uses a valid wider fallback");
        Check(renderer.SupportsDepthBuffer() && !renderer.SupportsStencilBuffer(),
              "Depth16 exposes depth without falsely advertising stencil");
        Check(DrawDepthPair(device, effect) == Color::Red,
              "Depth16 rejects the later farther draw and remains Color-readable");

        ResetFormats(device, SurfaceFormat::Bgra4444, DepthFormat::None);
        Check(device.getPresentationParametersProperty().getBackBufferFormatProperty() ==
                  SurfaceFormat::Color,
              "A second unsupported color request is normalized after Reset");
        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                  DepthFormat::None,
              "A -> depth -> no-depth Reset restores the public None format");
        Check(!renderer.SupportsDepthBuffer() && !renderer.SupportsStencilBuffer(),
              "A -> depth -> no-depth Reset releases both native planes");
        Check(DrawDepthPair(device, effect) == Color::Green,
              "A -> depth -> no-depth Reset restores painter ordering");
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        ++failed;
    }

    std::printf("=== %d/%d PASS ===\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
