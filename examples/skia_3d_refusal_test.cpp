// SPDX-License-Identifier: MS-PL
// SKIA-102: exhaustive public/backend refusal gate after the accepted 2D-only ADR.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaUnsupported3D.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends;
using CNA::Internal::Backends::Skia::kSkiaUnsupported3DPrefix;

namespace
{
    class DummyVertexBuffer final : public IVertexBufferBackend
    {
    public:
        void SetData(const void*, int, std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        [[nodiscard]] int GetVertexCount() const override { return 3; }
    };

    class DummyIndexBuffer final : public IIndexBufferBackend
    {
    public:
        void SetData16(const void*, int) override {}
        [[nodiscard]] int GetIndexCount() const override { return 3; }
    };
}

class Skia3DRefusalTest final : public Game
{
public:
    Skia3DRefusalTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& backend = device.GetBackend();
        const Color sentinel(17, 34, 51, 255);
        const Color blue(0, 0, 255, 255);
        const Color green(0, 255, 0, 255);

        CheckCapabilities(device);

        RenderTarget2D target(device, 8, 4, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 0,
                              RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(sentinel);

        CheckBuffers(device);
        CheckDraws(device, backend);
        CheckDepthStencil(device, backend);
        CheckEffectsAndModel(device);
        CheckStorageAndShaderBindings(device);
        CheckQuery(device);

        // Public XNA clear semantics mask attachments that the active target does not really own.
        device.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, blue, 0.25f, 7);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(ReadTargetCentre(target) == sentinel,
              "all rejected 3D work and depth/stencil-only clear leave target byte-exact");

        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     blue, 0.75f, 3);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(ReadTargetCentre(target) == blue,
              "public combined clear degrades only to the supported color aspect");

        // The accepted disabled state and normal 2D draw must remain usable after every refusal.
        device.setDepthStencilStateProperty(DepthStencilState::None);
        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        SpriteBatch batch(device);
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        batch.Draw(white, Rectangle(0, 0, 16, 8), green);
        batch.End();
        Color recovered = Color::Transparent;
        const Rectangle centre(8, 4, 1, 1);
        device.GetBackBufferData(&centre, &recovered, 0, 1);
        Check(recovered == green, "SpriteBatch 2D rendering recovers after the complete refusal matrix");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    template<typename Callable>
    bool Refuses(Callable&& callable, const char* operation)
    {
        try
        {
            callable();
        }
        catch (const std::runtime_error& error)
        {
            return std::string(error.what()) == std::string(kSkiaUnsupported3DPrefix) + operation;
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    void CheckCapabilities(GraphicsDevice& device)
    {
        Check(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)
                  && !device.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer)
                  && !device.SupportsCapability(CNA::GraphicsCapability::WireFrame),
              "3D, depth/stencil and wireframe capabilities are false");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing)
                  && !device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets)
                  && !device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering),
              "MSAA, MRT and anisotropic capabilities remain false");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery)
                  && !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                  && device.SupportsCapability(CNA::GraphicsCapability::Texture3D),
              "query/effect capabilities are false while CPU Texture3D storage stays true");
    }

    void CheckBuffers(GraphicsDevice& device)
    {
        const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
        Check(Refuses([&] { VertexBuffer value(device, 3); }, "CreateVertexBuffer")
                  && Refuses([&] { VertexBuffer value(device, declaration, 3, BufferUsage::None); },
                             "CreateVertexBuffer")
                  && Refuses([&] { DynamicVertexBuffer value(device, declaration, 3, BufferUsage::None); },
                             "CreateVertexBuffer"),
              "all static/dynamic vertex-buffer constructors share the 3D refusal");
        Check(Refuses([&] { IndexBuffer value(device, IndexElementSize::SixteenBits, 3,
                                               BufferUsage::None); }, "CreateIndexBuffer16")
                  && Refuses([&] { DynamicIndexBuffer value(device, IndexElementSize::SixteenBits, 3,
                                                        BufferUsage::None); }, "CreateIndexBuffer16"),
              "all 16-bit index-buffer constructors share their exact refusal");
        Check(Refuses([&] { IndexBuffer value(device, IndexElementSize::ThirtyTwoBits, 3,
                                               BufferUsage::None); }, "CreateIndexBuffer32")
                  && Refuses([&] { DynamicIndexBuffer value(device, IndexElementSize::ThirtyTwoBits, 3,
                                                        BufferUsage::None); }, "CreateIndexBuffer32"),
              "32-bit index buffers no longer masquerade as the 16-bit route");
    }

    void CheckDraws(GraphicsDevice& device, IGraphicsBackend& backend)
    {
        const std::array<VertexPositionColor, 3> vertices = {
            VertexPositionColor(Vector3(-1, -1, 0), Color::Red),
            VertexPositionColor(Vector3(0, 1, 0), Color::Green),
            VertexPositionColor(Vector3(1, -1, 0), Color::Blue),
        };
        const std::array<std::uint16_t, 3> indices16 = {0, 1, 2};
        const std::array<std::uint32_t, 3> indices32 = {0, 1, 2};
        Check(Refuses([&] { device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1); },
                          "GraphicsDevice::DrawPrimitives")
                  && Refuses([&] { device.DrawIndexedPrimitives(
                                 PrimitiveType::TriangleList, 0, 0, 3, 0, 1); },
                             "GraphicsDevice::DrawIndexedPrimitives")
                  && Refuses([&] { device.DrawInstancedPrimitives(
                                 PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1); },
                             "GraphicsDevice::DrawInstancedPrimitives"),
              "buffered, indexed and instanced public draws refuse before binding validation");
        Check(Refuses([&] { device.DrawUserPrimitives(
                                 PrimitiveType::TriangleList, vertices.data(), 0, 1); },
                             "GraphicsDevice::DrawUserPrimitives")
                  && Refuses([&] { device.DrawUserPrimitives(
                                 PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()),
                                 0, 1); }, "GraphicsDevice::DrawUserPrimitives"),
              "typed and raw DrawUserPrimitives refuse before packing or allocation");
        Check(Refuses([&] { device.DrawUserIndexedPrimitives(
                                 PrimitiveType::TriangleList, vertices.data(), 0, 3,
                                 indices16.data(), 0, 1); },
                             "GraphicsDevice::DrawUserIndexedPrimitives")
                  && Refuses([&] { device.DrawUserIndexedPrimitives(
                                 PrimitiveType::TriangleList, vertices.data(), 0, 3,
                                 indices32.data(), 0, 1); },
                             "GraphicsDevice::DrawUserIndexedPrimitives"),
              "16/32-bit DrawUserIndexedPrimitives share one public refusal boundary");

        DummyVertexBuffer vb;
        DummyIndexBuffer ib;
        const Matrix identity = Matrix::getIdentityProperty();
        GpuDrawParams params;
        Check(Refuses([&] { backend.DrawColoredPrimitives(
                                 vb, identity, identity, identity, PrimitiveType::TriangleList, 1); },
                             "DrawColoredPrimitives")
                  && Refuses([&] { backend.DrawPrimitivesEx(
                                 vb, identity, identity, identity, PrimitiveType::TriangleList, 1,
                                 params); }, "DrawPrimitivesEx"),
              "both non-indexed backend draw entry points use the shared diagnostic");
        Check(Refuses([&] { backend.DrawIndexedColoredPrimitives(
                                 vb, ib, identity, identity, identity,
                                 PrimitiveType::TriangleList, 1); },
                             "DrawIndexedColoredPrimitives")
                  && Refuses([&] { backend.DrawIndexedPrimitivesEx(
                                 vb, ib, identity, identity, identity,
                                 PrimitiveType::TriangleList, 1, params); },
                             "DrawIndexedPrimitivesEx")
                  && Refuses([&] { backend.DrawInstancedPrimitivesEx(
                                 vb, ib, identity, identity, identity,
                                 PrimitiveType::TriangleList, 1, 1, params); },
                             "DrawInstancedPrimitivesEx"),
              "indexed/effect/instanced backend draws never fall through generic defaults");
    }

    void CheckDepthStencil(GraphicsDevice& device, IGraphicsBackend& backend)
    {
        Check(Refuses([&] { device.SetDepthTestEnabled(true); }, "SetDepthTestEnabled")
                  && Refuses([&] { device.SetDepthWriteEnabled(true); }, "SetDepthWriteEnabled"),
              "direct depth-test/write state toggles refuse uniformly");
        device.setDepthStencilStateProperty(DepthStencilState::None);
        Check(Refuses([&] { device.setDepthStencilStateProperty(DepthStencilState::Default); },
                          "ApplyDepthStencilState")
                  && !device.getDepthStencilStateProperty().getDepthBufferEnableProperty(),
              "active depth state rejects without replacing the accepted disabled cache");
        const int oldReference = device.getReferenceStencilProperty();
        Check(Refuses([&] { device.setReferenceStencilProperty(3); }, "SetReferenceStencil")
                  && device.getReferenceStencilProperty() == oldReference,
              "nonzero stencil reference rejects atomically");
        RasterizerState wireframe;
        wireframe.setFillModeProperty(FillMode::WireFrame);
        Check(Refuses([&] { device.setRasterizerStateProperty(wireframe); },
                          "RasterizerState::FillMode::WireFrame")
                  && device.getRasterizerStateProperty().getFillModeProperty() == FillMode::Solid,
              "wireframe state rejects atomically and capability remains false");
        Check(Refuses([&] { backend.ClearColorAndDepth(1, 0, 0, 1, 1); },
                          "ClearColorAndDepth")
                  && Refuses([&] { backend.ClearDepth(1); }, "ClearDepth")
                  && Refuses([&] { backend.ClearStencil(1); }, "ClearStencil")
                  && Refuses([&] { backend.ClearDepthAndStencil(1, 1); },
                             "ClearDepthAndStencil")
                  && Refuses([&] { backend.ClearColorAndStencil(1, 0, 0, 1, 1); },
                             "ClearColorAndStencil")
                  && Refuses([&] { backend.ClearColorDepthAndStencil(1, 0, 0, 1, 1, 1); },
                             "ClearColorDepthAndStencil"),
              "all six attachment-bearing backend clear methods refuse before color mutation");
    }

    void CheckEffectsAndModel(GraphicsDevice& device)
    {
        BasicEffect basic(device);
        AlphaTestEffect alpha(device);
        DualTextureEffect dual(device);
        EnvironmentMapEffect environment(device);
        SkinnedEffect skinned(device);
        PbrEffect pbr(device);
        SkinnedPbrEffect skinnedPbr(device);
        const std::array<Effect*, 7> effects = {
            &basic, &alpha, &dual, &environment, &skinned, &pbr, &skinnedPbr};
        bool allRefused = true;
        for (Effect* effect : effects)
        {
            effect->Apply();
            allRefused &= Refuses(
                [&] { device.DrawUserPrimitives(
                           PrimitiveType::TriangleList,
                           static_cast<const VertexPositionColor*>(nullptr), 0, 1); },
                "GraphicsDevice::DrawUserPrimitives");
        }
        Check(allRefused, "all seven stock 3D effect families reach the same public draw boundary");

        ModelMesh emptyMesh(&device, std::vector<ModelMeshPart*>{});
        Check(Refuses([&] { emptyMesh.Draw(); }, "ModelMesh::Draw"),
              "ModelMesh::Draw refuses before an empty/partial model can silently succeed");
    }

    void CheckStorageAndShaderBindings(GraphicsDevice& device)
    {
        TextureCube cube(device, 1, false, SurfaceFormat::Color);
        Texture3D volume(device, 1, 1, 1, false, SurfaceFormat::Color);
        const Color stored(91, 82, 73, 255);
        Color cubeRead = Color::Transparent;
        Color volumeRead = Color::Transparent;
        cube.SetData(CubeMapFace::PositiveX, &stored, 1);
        volume.SetData(&stored, 1);
        cube.GetData(CubeMapFace::PositiveX, &cubeRead, 1);
        volume.GetData(&volumeRead, 1);
        Check(cubeRead == stored && volumeRead == stored,
              "cube and volume remain byte-exact CPU transfer resources");

        const std::string marker = "CNA_SKIA_SKSL_V1";
        const std::string sksl = R"(
            uniform shader cnaTexture0;
            uniform float4 cnaTint;
            half4 main(float2 p) { return cnaTexture0.eval(p) * half4(cnaTint); }
        )";
        ShaderEffect effect(device, marker, sksl);
        Check(effect.IsEffectValid()
                  && Refuses([&] { effect.SetTexture(1, cube); },
                             "ShaderEffect::SetTexture(TextureCube)")
                  && Refuses([&] { effect.SetTexture(1, volume); },
                             "ShaderEffect::SetTexture(Texture3D)"),
              "tagged SkSL rejects cube/volume sampling without damaging transfer storage");
    }

    void CheckQuery(GraphicsDevice& device)
    {
        OcclusionQuery query(device);
        Check(!query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
              "unsupported query reports safe nonblocking property values");
        Check(Refuses([&] { query.Begin(); }, "OcclusionQuery::Begin")
                  && Refuses([&] { query.End(); }, "OcclusionQuery::End"),
              "OcclusionQuery Begin/End reject instead of silently no-oping");
    }

    [[nodiscard]] Color ReadTargetCentre(RenderTarget2D& target)
    {
        Color result = Color::Transparent;
        const Rectangle centre(4, 2, 1, 1);
        target.GetData(0, &centre, &result, 0, 1);
        return result;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    Skia3DRefusalTest game;
    game.Run();
    return game.Result();
}
