// SPDX-License-Identifier: MS-PL
// plan_fna3d.md FNA3D-39/40: resource lifetime and device-identity regression test.
//
// Unlike Fna3d_Lifetime, this test deliberately destroys the native graphics renderer while
// every resource wrapper is still alive. It then proves that use is rejected without entering the
// freed FNA3D_Device and that later wrapper destruction is harmless. A second, sequential device
// proves that "is an Fna3d* type" is not mistaken for "belongs to this device". The devices are
// sequential because FNA3D's OpenGL driver owns one current context per thread and does not make a
// device's context current around arbitrary commands or teardown.

#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dWindowFlags.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::ImageData;
using CNA::Internal::Renderers::Fna3d::Fna3dRenderer;
using CNA::Internal::Renderers::Fna3d::Detail::PrepareWindowNeedsOpenGl;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (!condition)
        {
            ++failures;
        }
    }

    template <typename Action>
    bool ThrewContaining(Action&& action, const char* expected)
    {
        try
        {
            action();
        }
        catch (const std::exception& error)
        {
            return std::string(error.what()).find(expected) != std::string::npos;
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    template <typename Action>
    bool ThrewDeviceError(Action&& action)
    {
        return ThrewContaining(std::forward<Action>(action), "graphics device");
    }

    std::unique_ptr<Fna3dRenderer> CreateRenderer(SDL_Window* window)
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = SDL_GetWindowID(window);
        args.virtualWidth = 32;
        args.virtualHeight = 32;
        args.swapInterval = 0;
        return std::make_unique<Fna3dRenderer>(args);
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }

    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        (PrepareWindowNeedsOpenGl() ? static_cast<std::uint64_t>(SDL_WINDOW_OPENGL) : 0u) |
        static_cast<std::uint64_t>(SDL_WINDOW_HIDDEN));
    SDL_Window* window = SDL_CreateWindow("CNA FNA3D device lifetime", 32, 32, flags);
    if (window == nullptr)
    {
        std::printf("[SKIP] FNA3D test windows unavailable: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }

    try
    {
        auto rendererA = CreateRenderer(window);

        ImageData image{ 1, 1, { 20, 40, 60, 255 } };
        auto textureA = rendererA->CreateTexture(image);
        auto targetA = rendererA->CreateRenderTarget2D(4, 4, 0, false, false, 0);
        auto cubeA = rendererA->CreateTextureCube(2, false, 0);
        auto volumeA = rendererA->CreateTexture3D(2, 2, 2, false, 0);
        auto vertexA = rendererA->CreateVertexBuffer(3);
        auto indexA = rendererA->CreateIndexBuffer16(3);
        auto queryA = rendererA->CreateOcclusionQuery();
        auto spritesA = rendererA->CreateSpriteBatch();

        Check(ThrewContaining(
                  [&] { (void) rendererA->CreateVertexBuffer(std::numeric_limits<int>::max()); },
                  "signed 32-bit buffer limit"),
              "an overflowing vertex-buffer allocation is refused before FNA3D");
        Check(ThrewContaining(
                  [&] { indexA->SetData32(image.pixels.data(), std::numeric_limits<int>::max()); },
                  "signed 32-bit buffer limit"),
              "an overflowing index-buffer upload is refused before FNA3D");

        // The central case: resources and SpriteBatch all outlive their native device.
        rendererA.reset();

        std::uint8_t pixel[4] = {};
        Check(!textureA->GetData(0, 0, 0, 1, 1, pixel, sizeof(pixel)),
              "Texture2D readback after device destruction is refused without a native call");
        Check(ThrewDeviceError([&] { textureA->UpdatePixels(pixel, 4); }),
              "Texture2D upload after device destruction raises a deterministic error");
        Check(!cubeA->SetData(0, 0, 0, 0, 1, 1, pixel, sizeof(pixel)),
              "TextureCube upload after device destruction is refused");
        Check(!volumeA->SetData(0, 0, 0, 0, 1, 1, 1, pixel, sizeof(pixel)),
              "Texture3D upload after device destruction is refused");
        Check(ThrewDeviceError([&] { targetA->BindAsRenderTarget(); }),
              "render-target binding after device destruction raises a deterministic error");
        Check(ThrewDeviceError([&] { vertexA->SetData(pixel, 1, sizeof(pixel)); }),
              "vertex-buffer upload after device destruction raises a deterministic error");
        const std::uint16_t oneIndex = 0;
        Check(ThrewDeviceError([&] { indexA->SetData16(&oneIndex, 1); }),
              "index-buffer upload after device destruction raises a deterministic error");
        Check(ThrewDeviceError([&] { queryA->Begin(); }),
              "occlusion-query use after device destruction raises a deterministic error");
        Check(ThrewDeviceError([&] { spritesA->Begin(); }),
              "SpriteBatch use after device destruction raises a deterministic error");

        // A wrapper made by a different FNA3D device is still foreign even though its concrete
        // C++ type matches. FNA3D/OpenGL is single-current-context, so create the control device
        // after destroying A rather than pretending simultaneous devices are an upstream promise.
        auto rendererB = CreateRenderer(window);
        auto textureB = rendererB->CreateTexture(image);
        auto cubeB = rendererB->CreateTextureCube(2, false, 0);
        auto targetB = rendererB->CreateRenderTarget2D(4, 4, 0, false, false, 0);
        auto vertexB = rendererB->CreateVertexBuffer(3);
        auto indexB = rendererB->CreateIndexBuffer16(3);
        auto spritesB = rendererB->CreateSpriteBatch();
        Check(ThrewDeviceError([&] { rendererB->SetRenderTarget2D(targetA.get()); }),
              "a render target from another FNA3D device is refused");
        Check(ThrewDeviceError([&] {
                  spritesB->Begin();
                  spritesB->Draw(*textureA, 0.0f, 0.0f);
              }),
              "a SpriteBatch texture from another FNA3D device is refused");
        Check(ThrewDeviceError([&] {
                  rendererB->DrawIndexedColoredPrimitives(
                      *vertexA, *indexA, Matrix::getIdentityProperty(),
                      Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                      PrimitiveType::TriangleList, 1);
              }),
              "buffers from another FNA3D device are refused before drawing");
        GpuDrawParams environmentParams;
        environmentParams.envMapping = true;
        environmentParams.envMap = cubeA.get();
        Check(ThrewDeviceError([&] {
                  rendererB->ApplyStockEffectEXT(
                      Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                      Matrix::getIdentityProperty(), environmentParams);
              }),
              "an environment map from another FNA3D device is refused");
        environmentParams.envMap = cubeB.get();
        rendererB->ApplyStockEffectEXT(
            Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty(), environmentParams);
        environmentParams.envMap = nullptr;
        rendererB->ApplyStockEffectEXT(
            Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty(), environmentParams);
        Check(true, "a null environment map explicitly follows a live slot-1 binding");

        // Destruction in this order used to dereference rendererA's freed FNA3D_Device pointer.
        spritesA.reset();
        queryA.reset();
        indexA.reset();
        vertexA.reset();
        volumeA.reset();
        cubeA.reset();
        targetA.reset();
        textureA.reset();
        Check(true, "all wrappers destruct safely after their device");

        // Keep the control device genuinely usable after all cross-device refusals, then repeat
        // the post-device wrapper destruction order for its own resources.
        rendererB->Clear(0.1f, 0.2f, 0.3f, 1.0f);
        Check(true, "the second device still accepts commands");
        rendererB.reset();
        Check(true, "the second device tears down cleanly");
        spritesB.reset();
        indexB.reset();
        vertexB.reset();
        targetB.reset();
        cubeB.reset();
        textureB.reset();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        ++failures;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
