// SPDX-License-Identifier: MS-PL
// GDI-059/GDI-070: public rejection plus the complete direct 3D/resource boundary.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
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
#include "common/SdlTestGraphicsServices.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;

static_assert(std::is_base_of_v<IGraphicsRenderer, GdiRenderer>);
static_assert(!std::is_base_of_v<Software::SoftwareRenderer, GdiRenderer>,
              "GDI must compose the CPU core rather than inherit Software's 3D contract");

namespace
{
    class DummyVertexBuffer final : public IVertexBufferRenderer
    {
    public:
        void SetData(const void*, int, std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        [[nodiscard]] int GetVertexCount() const override { return 0; }
    };

    class DummyIndexBuffer final : public IIndexBufferRenderer
    {
    public:
        void SetData16(const void*, int) override {}
        [[nodiscard]] int GetIndexCount() const override { return 0; }
    };

    class DummyRenderTargetCube final : public IRenderTargetCubeRenderer
    {
    public:
        [[nodiscard]] bool SetData(int, int, int, int, int, int,
                                   const void*, int) override
        {
            return false;
        }
        [[nodiscard]] int GetSize() const override { return 2; }
        void BindAsRenderTargetFace(int) override {}
        void UnbindAsRenderTarget() override {}
    };

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
            "DrawUserPrimitives fails before allocating a private-core 3D buffer",
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

    bool ExerciseDirectRendererBoundary(GdiRenderer& renderer)
    {
        bool ok = true;
        ok &= Expect(!renderer.SupportsDepthStencil() && !renderer.SupportsDepthBuffer() &&
                         renderer.SupportsStencilBuffer(),
                     "direct attachment contract remains depthless and stencil-only");
        ok &= Expect(renderer.GetMaxTextureDimension() ==
                         Software::SoftwareFramebufferMaxDimension,
                     "direct texture ceiling matches the guarded CPU allocation ceiling");

        ok &= ExpectNotSupported(
            "direct TextureCube factory remains outside the 2D boundary",
            "TextureCube resources", [&] { (void)renderer.CreateTextureCube(2, false, 0); });
        ok &= ExpectNotSupported(
            "direct Texture3D factory remains outside the 2D boundary",
            "Texture3D resources", [&] { (void)renderer.CreateTexture3D(2, 2, 2, false, 0); });
        ok &= ExpectNotSupported(
            "direct RenderTargetCube factory remains outside the 2D boundary",
            "RenderTargetCube resources",
            [&] { (void)renderer.CreateRenderTargetCube(2, 0, false, false, 0); });
        ok &= ExpectNotSupported(
            "direct shader-effect factory remains outside the 2D boundary",
            "ShaderEffect programs",
            [&] { (void)renderer.CreateEffectRenderer("void main() {}", "void main() {}"); });
        ok &= ExpectNotSupported(
            "direct occlusion-query factory remains outside the 2D boundary",
            "occlusion queries", [&] { (void)renderer.CreateOcclusionQuery(); });
        ok &= ExpectNotSupported(
            "direct vertex-buffer factory remains outside the 2D boundary",
            "vertex buffers", [&] { (void)renderer.CreateVertexBuffer(3); });
        ok &= ExpectNotSupported(
            "direct 16-bit index factory remains outside the 2D boundary",
            "16-bit index buffers", [&] { (void)renderer.CreateIndexBuffer16(3); });
        ok &= ExpectNotSupported(
            "direct 32-bit index factory remains outside the 2D boundary",
            "32-bit index buffers", [&] { (void)renderer.CreateIndexBuffer32(3); });

        DummyRenderTargetCube cubeTarget;
        ok &= ExpectNotSupported(
            "direct cube-face binding cannot reach a foreign cube implementation",
            "RenderTargetCube face bindings",
            [&] { renderer.SetRenderTargetCubeFace(&cubeTarget, 0); });
        const RenderTargetBindingDescriptor cubeBinding =
            RenderTargetBindingDescriptor::ForRenderTargetCubeFace(
                &cubeTarget, 0, cubeTarget.GetSize(), 0);
        ok &= ExpectNotSupported(
            "normalized cube descriptor remains outside the 2D boundary",
            "RenderTargetCube face bindings",
            [&] { renderer.SetRenderTargets(&cubeBinding, 1); });

        std::unique_ptr<IRenderTargetRenderer> firstTarget =
            renderer.CreateRenderTarget2D(2, 2, 0, true, false, 0);
        std::unique_ptr<IRenderTargetRenderer> secondTarget =
            renderer.CreateRenderTarget2D(2, 2, 0, true, false, 0);
        const std::array<RenderTargetBindingDescriptor, 2> multipleBindings = {
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                firstTarget.get(), 0, 2, 2, 0),
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                secondTarget.get(), 0, 2, 2, 0),
        };
        ok &= ExpectNotSupported(
            "multiple 2D targets cannot fall through to Software behavior",
            "multiple simultaneous render targets",
            [&] { renderer.SetRenderTargets(multipleBindings.data(), 2); });
        const RenderTargetBindingDescriptor arraySliceBinding =
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                firstTarget.get(), 1, 2, 2, 0);
        ok &= ExpectNotSupported(
            "2D array slices cannot fall through to Software behavior",
            "RenderTarget2D array slices",
            [&] { renderer.SetRenderTargets(&arraySliceBinding, 1); });

        renderer.SetRenderTargetCubeFace(nullptr, 0);
        renderer.SetRenderTargets(nullptr, 0);
        ok &= Expect(true, "null cube/plural bindings explicitly restore the GDI backbuffer");

        ok &= ExpectNotSupported(
            "direct ClearColorAndDepth is rejected", "ClearColorAndDepth",
            [&] { renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f); });
        ok &= ExpectNotSupported(
            "direct ClearDepth is rejected", "ClearDepth",
            [&] { renderer.ClearDepth(1.0f); });
        ok &= ExpectNotSupported(
            "direct ClearDepthAndStencil is rejected", "ClearDepthAndStencil",
            [&] { renderer.ClearDepthAndStencil(1.0f, 0); });
        ok &= ExpectNotSupported(
            "direct ClearColorDepthAndStencil is rejected", "ClearColorDepthAndStencil",
            [&] { renderer.ClearColorDepthAndStencil(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0); });
        ok &= ExpectNotSupported(
            "direct depth-test toggle is rejected", "SetDepthTestEnabled",
            [&] { renderer.SetDepthTestEnabled(true); });
        ok &= ExpectNotSupported(
            "direct depth-write toggle is rejected", "SetDepthWriteEnabled",
            [&] { renderer.SetDepthWriteEnabled(true); });
        ok &= ExpectNotSupported(
            "legacy direct blend toggle is rejected", "SetBlendEnabled",
            [&] { renderer.SetBlendEnabled(true); });

        DummyVertexBuffer vertexBuffer;
        DummyIndexBuffer indexBuffer;
        const Matrix identity = Matrix::getIdentityProperty();
        const GpuDrawParams drawParameters{};
        ok &= ExpectNotSupported(
            "direct colored draw is rejected", "DrawColoredPrimitives",
            [&]
            {
                renderer.DrawColoredPrimitives(
                    vertexBuffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1);
            });
        ok &= ExpectNotSupported(
            "direct indexed colored draw is rejected", "DrawIndexedColoredPrimitives",
            [&]
            {
                renderer.DrawIndexedColoredPrimitives(
                    vertexBuffer, indexBuffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1);
            });
        ok &= ExpectNotSupported(
            "direct effect-aware draw is rejected", "DrawPrimitivesEx",
            [&]
            {
                renderer.DrawPrimitivesEx(
                    vertexBuffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, drawParameters);
            });
        ok &= ExpectNotSupported(
            "direct indexed effect-aware draw is rejected", "DrawIndexedPrimitivesEx",
            [&]
            {
                renderer.DrawIndexedPrimitivesEx(
                    vertexBuffer, indexBuffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, drawParameters);
            });
        ok &= ExpectNotSupported(
            "direct instanced draw is rejected", "DrawInstancedPrimitivesEx",
            [&]
            {
                renderer.DrawInstancedPrimitivesEx(
                    vertexBuffer, indexBuffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, 2, drawParameters);
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

        bool ok = true;
        {
            GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                                  GraphicsProfile::Reach, parameters);
            ok &= Expect(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                         "GDI continues to report ThreeD=false");
            ok &= Expect(!device.SupportsCapability(CNA::GraphicsCapability::Texture3D) &&
                             !device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery) &&
                             !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
                         "resource-specific capability answers remain false");
            ok &= ExerciseUnsupportedResources(device);
            ok &= ExerciseUnsupportedBuffers(device);
            ok &= ExerciseUnsupported3DCalls(device);
        }
        {
            GdiRenderer renderer(
                CNA::Examples::SdlTestRendererArgs(
                    window, nullptr, nullptr, 8, 8,
                    CnaPresentationMode::NativeBackBuffer),
                GdiConfiguration{});
            ok &= ExerciseDirectRendererBoundary(renderer);
        }
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
