// SPDX-License-Identifier: MS-PL
// Cross-renderer integration test for Unsupported3DGraphicsCallBehavior on CNA's permanently
// 2D-only graphics renderers (SDL_RENDERER, DX3, CANVAS and ASCII).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Unsupported3DGraphicsCallBehavior;

namespace
{
    struct CapturedWarning
    {
        SDL_LogPriority priority;
        std::string message;
    };

    void SDLCALL CaptureWarnings(void* userdata, int, SDL_LogPriority priority,
                                 const char* message)
    {
        if (message == nullptr)
            return;

        std::string text(message);
        if (text.find("Unsupported3DGraphicsCallBehavior::WarnAndStub") ==
            std::string::npos)
        {
            return;
        }

        static_cast<std::vector<CapturedWarning>*>(userdata)->push_back(
            CapturedWarning{priority, std::move(text)});
    }

    template <typename F>
    bool ThrowsRuntimeError(F&& fn)
    {
        try
        {
            fn();
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    template <typename F>
    bool DoesNotThrow(F&& fn)
    {
        try
        {
            fn();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

class Unsupported3DCallBehaviorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++pass_;
        else
            ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();

        Check(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
              "3D capability remains unsupported");
        Check(device.GetUnsupported3DGraphicsCallBehavior() ==
                  Unsupported3DGraphicsCallBehavior::Throw,
              "default policy is Throw");
        Check(ThrowsRuntimeError([&] { device.SetDepthTestEnabled(true); }),
              "unsupported state call throws by default");

        SDL_LogOutputFunction previousLogFunction = nullptr;
        void* previousLogUserdata = nullptr;
        SDL_GetLogOutputFunction(&previousLogFunction, &previousLogUserdata);
        std::vector<CapturedWarning> warnings;
        SDL_SetLogOutputFunction(CaptureWarnings, &warnings);

        device.SetUnsupported3DGraphicsCallBehavior(
            Unsupported3DGraphicsCallBehavior::WarnAndStub);
        Check(device.GetUnsupported3DGraphicsCallBehavior() ==
                  Unsupported3DGraphicsCallBehavior::WarnAndStub,
              "policy can be changed at runtime");

        Check(DoesNotThrow([&] {
                  device.SetDepthTestEnabled(true);
                  device.SetDepthTestEnabled(false);
                  device.SetBlendEnabled(true);
              }),
              "unsupported state calls become no-ops");

        VertexBuffer vertexBuffer(device, 3);
        std::array<VertexPositionColor, 3> vertices{};
        std::array<VertexPositionColor, 3> verticesReadBack{};
        const bool vertexBufferSafe = DoesNotThrow([&] {
            vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
            vertexBuffer.GetData(verticesReadBack.data(),
                                 static_cast<int>(verticesReadBack.size()));
        });

        IndexBuffer indexBuffer(device, 3);
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        std::array<std::uint16_t, 3> indicesReadBack{};
        const bool indexBufferSafe = DoesNotThrow([&] {
            indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
            indexBuffer.GetData(indicesReadBack.data(),
                                static_cast<int>(indicesReadBack.size()));
        });
        Check(vertexBufferSafe && verticesReadBack == vertices &&
                  indexBufferSafe && indicesReadBack == indices,
              "buffer factories return safe null objects with CPU readback");

        std::array<Color, 8> volumePixels{
            Color::Red, Color::Red, Color::Red, Color::Red,
            Color::Red, Color::Red, Color::Red, Color::Red};
        std::array<Color, 8> volumeReadBack{
            Color::White, Color::White, Color::White, Color::White,
            Color::White, Color::White, Color::White, Color::White};
        Texture3D texture3D(device, 2, 2, 2, false, SurfaceFormat::Color);
        const bool texture3DSafe = DoesNotThrow([&] {
            texture3D.SetData(volumePixels.data(), static_cast<int>(volumePixels.size()));
            texture3D.GetData(volumeReadBack.data(),
                              static_cast<int>(volumeReadBack.size()));
        });

        std::array<Color, 4> facePixels{
            Color::Blue, Color::Blue, Color::Blue, Color::Blue};
        std::array<Color, 4> faceReadBack{
            Color::White, Color::White, Color::White, Color::White};
        TextureCube textureCube(device, 2, false, SurfaceFormat::Color);
        const bool textureCubeSafe = DoesNotThrow([&] {
            textureCube.SetData(CubeMapFace::PositiveX, facePixels.data(),
                                static_cast<int>(facePixels.size()));
            textureCube.GetData(CubeMapFace::PositiveX, faceReadBack.data(),
                                static_cast<int>(faceReadBack.size()));
        });

        const auto isTransparent = [](const Color& color) {
            return color.getRProperty() == 0 && color.getGProperty() == 0 &&
                   color.getBProperty() == 0 && color.getAProperty() == 0;
        };
        Check(texture3DSafe &&
                  std::all_of(volumeReadBack.begin(), volumeReadBack.end(), isTransparent) &&
                  textureCubeSafe &&
                  std::all_of(faceReadBack.begin(), faceReadBack.end(), isTransparent),
              "3D and cube textures are safe zero-reading null objects");

        RenderTargetCube renderTargetCube(
            device, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        Check(DoesNotThrow([&] {
                  device.SetRenderTarget(&renderTargetCube, CubeMapFace::PositiveX);
                  device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
              }),
              "cube render target null object can be bound and unbound");

        OcclusionQuery query(device);
        Check(DoesNotThrow([&] {
                  query.Begin();
                  query.End();
              }) &&
                  query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
              "occlusion query null object completes with zero pixels");

        const Matrix identity = Matrix::getIdentityProperty();
        Check(DoesNotThrow([&] {
                  renderer.DrawColoredPrimitives(
                      vertexBuffer.GetRenderer(), identity, identity, identity,
                      PrimitiveType::TriangleList, 1);
                  renderer.DrawIndexedColoredPrimitives(
                      vertexBuffer.GetRenderer(), indexBuffer.GetRenderer(),
                      identity, identity, identity, PrimitiveType::TriangleList, 1);
                  renderer.DrawInstancedPrimitivesEx(
                      vertexBuffer.GetRenderer(), indexBuffer.GetRenderer(),
                      identity, identity, identity, PrimitiveType::TriangleList,
                      1, 2, CNA::Internal::Renderers::GpuDrawParams{});
              }),
              "unsupported draw calls become no-ops");

        Check(!DoesNotThrow([&] {
                  texture3D.SetData(nullptr, static_cast<int>(volumePixels.size()));
              }) &&
                  !DoesNotThrow([&] {
                      VertexBuffer invalidBuffer(device, 0);
                      (void)invalidBuffer;
                  }),
              "WarnAndStub does not suppress invalid arguments");

        const auto depthWarningCount = std::count_if(
            warnings.begin(), warnings.end(), [](const CapturedWarning& warning) {
                return warning.message.find("SetDepthTestEnabled") != std::string::npos;
            });
        const bool allWarningsHaveWarningPriority =
            !warnings.empty() &&
            std::all_of(warnings.begin(), warnings.end(),
                        [](const CapturedWarning& warning) {
                            return warning.priority == SDL_LOG_PRIORITY_WARN;
                        });
        Check(depthWarningCount == 1,
              "the same unsupported operation warns only once");
        Check(allWarningsHaveWarningPriority,
              "stub diagnostics use warning severity");

        SDL_SetLogOutputFunction(previousLogFunction, previousLogUserdata);

        device.SetUnsupported3DGraphicsCallBehavior(
            Unsupported3DGraphicsCallBehavior::Throw);
        Check(ThrowsRuntimeError([&] { device.SetDepthTestEnabled(true); }),
              "switching back to Throw restores exceptions");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    Unsupported3DCallBehaviorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
        gdm_->setPreferredPresentationModeProperty(
            PresentationMode::NativeBackBuffer);
    }

    int GetResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    Unsupported3DCallBehaviorTest game;
    game.Run();
    return game.GetResult();
}
