// SPDX-License-Identifier: MS-PL
// OPENVG's own equivalent of modules/graphics/examples/unsupported_3d_call_behavior_test.cpp
// (Canvas/FreeDirect/ASCII/SDL_Renderer's shared fixture), covering the identical ground --
// Unsupported3DGraphicsCallBehavior::Throw/WarnAndStub on every inherently-3D-only entry point --
// but reflecting OPENVG's actually-honest GraphicsCapability::Texture3D=false, which the shared
// fixture's Texture3D expectations assume is true (see docs/openvg-renderer.md's capability table
// and this file's own CMakeLists.txt registration comment for the full rationale). Texture3D's own
// permanent coverage lives in Texture3DTests.cpp's Texture3DUnsupportedRendererTest, which this
// file does not duplicate.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

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
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Unsupported3DGraphicsCallBehavior;

namespace
{
    template <typename Fn>
    bool ThrowsRuntimeError(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const std::runtime_error&) { return true; }
    }

    template <typename Fn>
    bool DoesNotThrow(Fn&& fn)
    {
        try { fn(); return true; }
        catch (...) { return false; }
    }

    int pass = 0, fail = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass; else ++fail;
    }
}

class OpenVgUnsupported3DBehaviorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        Check(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD), "3D capability unsupported");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::Texture3D),
              "Texture3D capability honestly false (no fabricated storage)");
        Check(device.GetUnsupported3DGraphicsCallBehavior() == Unsupported3DGraphicsCallBehavior::Throw,
              "default policy is Throw");
        Check(ThrowsRuntimeError([&] { device.SetDepthTestEnabled(true); }),
              "unsupported state call throws by default");

        // Texture3D's constructor throws System::NotSupportedException REGARDLESS of policy,
        // because it consults SupportsCapability(Texture3D) before ever reaching
        // CreateTexture3D() -- verified here rather than relying solely on the shared
        // Texture3DUnsupportedRendererTest, since that runs before this test sets WarnAndStub.
        device.SetUnsupported3DGraphicsCallBehavior(Unsupported3DGraphicsCallBehavior::WarnAndStub);
        Check(device.GetUnsupported3DGraphicsCallBehavior() == Unsupported3DGraphicsCallBehavior::WarnAndStub,
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
            vertexBuffer.GetData(verticesReadBack.data(), static_cast<int>(verticesReadBack.size()));
        });

        IndexBuffer indexBuffer(device, 3);
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        std::array<std::uint16_t, 3> indicesReadBack{};
        const bool indexBufferSafe = DoesNotThrow([&] {
            indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
            indexBuffer.GetData(indicesReadBack.data(), static_cast<int>(indicesReadBack.size()));
        });
        Check(vertexBufferSafe && verticesReadBack == vertices &&
                  indexBufferSafe && indicesReadBack == indices,
              "buffer factories return safe null objects with CPU readback");

        bool texture3DThrewNotSupported = false;
        try { Texture3D t(device, 2, 2, 2, false, SurfaceFormat::Color); (void)t; }
        catch (const System::NotSupportedException&) { texture3DThrewNotSupported = true; }
        catch (...) {}
        Check(texture3DThrewNotSupported,
              "Texture3D construction throws NotSupportedException regardless of WarnAndStub "
              "(GraphicsCapability::Texture3D is honestly false)");

        std::array<Color, 4> facePixels{Color::Blue, Color::Blue, Color::Blue, Color::Blue};
        std::array<Color, 4> faceReadBack{Color::White, Color::White, Color::White, Color::White};
        TextureCube textureCube(device, 2, false, SurfaceFormat::Color);
        const bool textureCubeSafe = DoesNotThrow([&] {
            textureCube.SetData(CubeMapFace::PositiveX, facePixels.data(), static_cast<int>(facePixels.size()));
            textureCube.GetData(CubeMapFace::PositiveX, faceReadBack.data(), static_cast<int>(faceReadBack.size()));
        });
        const auto isTransparent = [](const Color& c) {
            return c.getRProperty() == 0 && c.getGProperty() == 0 && c.getBProperty() == 0 && c.getAProperty() == 0;
        };
        Check(textureCubeSafe && isTransparent(faceReadBack[0]),
              "TextureCube is a safe zero-reading null object under WarnAndStub");

        RenderTargetCube renderTargetCube(device, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                          RenderTargetUsage::DiscardContents);
        Check(DoesNotThrow([&] {
                  device.SetRenderTarget(&renderTargetCube, CubeMapFace::PositiveX);
                  device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
              }),
              "cube render target null object can be bound and unbound");

        OcclusionQuery query(device);
        Check(DoesNotThrow([&] { query.Begin(); query.End(); }) &&
                  query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
              "occlusion query null object completes with zero pixels");

        device.SetUnsupported3DGraphicsCallBehavior(Unsupported3DGraphicsCallBehavior::Throw);
        Check(ThrowsRuntimeError([&] { device.SetDepthTestEnabled(true); }),
              "switching back to Throw restores exceptions");

        std::printf("=== %d/%d PASS ===\n", pass, pass + fail);
        Exit();
    }

public:
    OpenVgUnsupported3DBehaviorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }
};

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    OpenVgUnsupported3DBehaviorTest game;
    game.Run();
    return fail == 0 ? 0 : 1;
}
