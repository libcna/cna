// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-057: exact child-resource recreation, rollback, active-target
// rebinding, and repeated stock/compiled draws.

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglResourceLifetime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#if defined(CNA_RLGL_COMPILED_EFFECTS)
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#endif

#include "RlglResources.hpp"
#include "common/PixelTestGame.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;
    using CNA::Internal::Graphics::PositionColorStream;
    using CNA::Internal::Renderers::RenderTargetBindingDescriptor;

    void SetResourceFailureIndex(const char* const index)
    {
#if defined(_WIN32)
        (void)_putenv_s("CNA_RLGL_DEBUG_FAIL_RESOURCE_RESTORE_AT",
                       index != nullptr ? index : "");
#else
        if (index != nullptr)
            (void)setenv("CNA_RLGL_DEBUG_FAIL_RESOURCE_RESTORE_AT", index, 1);
        else
            (void)unsetenv("CNA_RLGL_DEBUG_FAIL_RESOURCE_RESTORE_AT");
#endif
    }

    [[nodiscard]] CNA::Internal::Graphics::ImageData MakeImage(
        const int width, const int height, const int mipLevels,
        const SurfaceFormat format, const std::vector<std::uint8_t>& bytes)
    {
        CNA::Internal::Graphics::ImageData result;
        result.width = width;
        result.height = height;
        result.mipLevels = mipLevels;
        result.surfaceFormat = static_cast<int>(format);
        result.pixels = bytes;
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> SolidRgba(
        const int width, const int height, const Color& color)
    {
        std::vector<std::uint8_t> result(
            static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t index = 0; index < result.size(); index += 4)
        {
            result[index] = color.getRProperty();
            result[index + 1] = color.getGProperty();
            result[index + 2] = color.getBProperty();
            result[index + 3] = color.getAProperty();
        }
        return result;
    }

    [[nodiscard]] bool IsColor(const Color& actual, const Color& expected)
    {
        constexpr int tolerance = 3;
        return std::abs(static_cast<int>(actual.getRProperty()) -
                        static_cast<int>(expected.getRProperty())) <= tolerance &&
            std::abs(static_cast<int>(actual.getGProperty()) -
                     static_cast<int>(expected.getGProperty())) <= tolerance &&
            std::abs(static_cast<int>(actual.getBProperty()) -
                     static_cast<int>(expected.getBProperty())) <= tolerance &&
            std::abs(static_cast<int>(actual.getAProperty()) -
                     static_cast<int>(expected.getAProperty())) <= tolerance;
    }

    class ResourceRecreateTest final : public CNA::Examples::PixelTestGame
    {
    public:
        ResourceRecreateTest()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(64);
            graphics_->setPreferredPresentationModeProperty(
                PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());
            const auto lifetime = renderer.GetResourceLifetimeForTesting();

            const Color textureColor(41, 137, 229, 255);
            const auto textureLevel0 = SolidRgba(4, 4, textureColor);
            const auto textureLevel1 = SolidRgba(2, 2, Color(7, 83, 191, 255));
            const auto textureLevel2 = SolidRgba(1, 1, Color(231, 73, 19, 255));
            auto texture = renderer.CreateTexture(MakeImage(
                4, 4, 3, SurfaceFormat::Color, textureLevel0));
            texture->UpdatePixelsLevel(1, textureLevel1.data(), 2, 2);
            texture->UpdatePixelsLevel(2, textureLevel2.data(), 1, 1);

            const std::vector<std::uint8_t> dxtLevel0{
                0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
            const std::vector<std::uint8_t> dxtLevel1{
                0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0};
            auto dxtTexture = renderer.CreateTexture(MakeImage(
                4, 4, 2, SurfaceFormat::Dxt1, dxtLevel0));
            dxtTexture->UpdatePixelsLevel(1, dxtLevel1.data(), 2, 2);

            auto cube = renderer.CreateTextureCube(
                4, true, static_cast<int>(SurfaceFormat::Color));
            const auto cubeFace = SolidRgba(4, 4, Color(13, 211, 97, 255));
            const auto cubeTail = SolidRgba(1, 1, Color(197, 31, 173, 255));
            cube->SetData(
                2, 0, 0, 0, 4, 4, cubeFace.data(),
                static_cast<int>(cubeFace.size()));
            cube->SetData(
                5, 2, 0, 0, 1, 1, cubeTail.data(),
                static_cast<int>(cubeTail.size()));

            auto dxtCube = renderer.CreateTextureCube(
                4, false, static_cast<int>(SurfaceFormat::Dxt1));
            dxtCube->SetCompressedDataEXT(
                4, 0, 0, 0, 4, 4,
                dxtLevel0.data(), static_cast<int>(dxtLevel0.size()));

            const std::array<PositionColorStream, 3> triangle{{
                {-0.75f, -0.75f, 0.0f, 255, 0, 0, 255},
                {0.75f, -0.75f, 0.0f, 255, 0, 0, 255},
                {0.0f, 0.75f, 0.0f, 255, 0, 0, 255}}};
            auto vertexBuffer = renderer.CreateVertexBuffer(3);
            vertexBuffer->SetVertexDeclaration(
                VertexPositionColor::getVertexDeclarationStatic());
            vertexBuffer->SetData(
                triangle.data(), static_cast<int>(triangle.size()),
                sizeof(PositionColorStream));
            auto indexBuffer = renderer.CreateIndexBuffer16(3);
            const std::array<std::uint16_t, 3> indices{{0, 1, 2}};
            indexBuffer->SetData16(indices.data(), static_cast<int>(indices.size()));

            auto target = renderer.CreateRenderTarget2DEXT(
                8, 8, static_cast<int>(DepthFormat::Depth24Stencil8),
                true, false, 0, static_cast<int>(SurfaceFormat::Color));
            auto targetCube = renderer.CreateRenderTargetCubeEXT(
                8, static_cast<int>(DepthFormat::Depth24),
                true, false, 0, static_cast<int>(SurfaceFormat::Color));
            auto spriteBatch = renderer.CreateSpriteBatch();
            auto query = renderer.CreateOcclusionQuery();

            RenderTarget2D contentLostTarget(
                device, 8, 8, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            int contentLostCount = 0;
            contentLostTarget.ContentLost +=
                [&contentLostCount](System::Object*, const System::EventArgs&)
                {
                    ++contentLostCount;
                };

#if defined(CNA_RLGL_COMPILED_EFFECTS)
            Effect compiledEffect(
                device, CNA::TestSupport::BuildSyntheticDrawableEffect());
            compiledEffect.getParametersProperty()["Transform"]->SetValue(
                Matrix::getIdentityProperty());
            const Vector4 compiledTint(0.25f, 0.75f, 0.5f, 1.0f);
            compiledEffect.getParametersProperty()["Tint"]->SetValue(compiledTint);
#endif

            const auto originalLifetime = lifetime->GetSnapshotForTesting();
            const auto originalTexture =
                Rlgl::GetTexture2DResourceSnapshotForTesting(*texture);
            const auto originalDxt =
                Rlgl::GetTexture2DResourceSnapshotForTesting(*dxtTexture);
            Check(originalLifetime.recoveryResources >= 11 &&
                    originalTexture.texture != 0 && originalDxt.texture != 0,
                "every resource family is registered before recovery");

            const std::array<RenderTargetBindingDescriptor, 2> mrt{{
                RenderTargetBindingDescriptor::ForRenderTarget2D(
                    target.get(), 0, 8, 8, target->GetMultiSampleCount()),
                RenderTargetBindingDescriptor::ForRenderTargetCubeFace(
                    targetCube.get(), 3, 8, targetCube->GetMultiSampleCount())}};
            renderer.SetRenderTargets(mrt.data(), static_cast<int>(mrt.size()));
            renderer.SetViewport(0, 0, 8, 8, 0.0f, 1.0f);

            renderer.DebugSimulateContextLoss();
            Check(AllNativeIdentitiesAreZero(
                    *texture, *dxtTexture, *cube, *dxtCube,
                    *vertexBuffer, *indexBuffer, *target, *targetCube,
                    contentLostTarget) && !renderer.CanBeginDrawEXT(),
                "loss invalidates every registered native identity before recreation");

            SetResourceFailureIndex("2");
            bool injectedFailure = false;
            try
            {
                renderer.DebugRestoreContext();
            }
            catch (const std::exception&)
            {
                injectedFailure = true;
            }
            SetResourceFailureIndex(nullptr);
            const auto failedLifetime = lifetime->GetSnapshotForTesting();
            Check(injectedFailure && !renderer.CanBeginDrawEXT() &&
                    AllNativeIdentitiesAreZero(
                        *texture, *dxtTexture, *cube, *dxtCube,
                        *vertexBuffer, *indexBuffer, *target, *targetCube,
                        contentLostTarget) &&
                    failedLifetime.failedResourceRestorations == 1 &&
                    failedLifetime.resourceRestorations == 0 && contentLostCount == 0,
                "an injected child failure rolls back every replacement identity");

            renderer.DebugRestoreContext();
            Check(renderer.CanBeginDrawEXT() &&
                    device.getGraphicsDeviceStatusProperty() ==
                        GraphicsDeviceStatus::Normal &&
                    lifetime->GetSnapshotForTesting().resourceRestorations == 1 &&
                    contentLostCount == 1 && contentLostTarget.getIsContentLostProperty(),
                "retry restores every child before Reset and reports target content loss");
            Check(ExactResourceStateSurvived(
                    *texture, *dxtTexture, *cube, *dxtCube,
                    *vertexBuffer, *indexBuffer, *target, *targetCube,
                    contentLostTarget, textureLevel1, dxtLevel1, cubeFace, cubeTail),
                "textures, cubes, buffers, declarations, and target descriptions are exact");
            Check(DrawActiveMrtAndStock(
                    renderer, *vertexBuffer, *indexBuffer, *target, *targetCube),
                "active MRT names rebind and a stock indexed draw succeeds after retry");
            Check(DrawSprite(renderer, device, *spriteBatch, *texture, textureColor),
                "SpriteBatch pipeline and restored Texture2D draw after recovery");
            Check(QueryRuns(*query),
                "occlusion query owns a valid replacement identity after recovery");
#if defined(CNA_RLGL_COMPILED_EFFECTS)
            Check(DrawCompiled(device, compiledEffect, compiledTint),
                "compiled effect bytecode and parameters draw after recovery");
#endif

            const RenderTargetBindingDescriptor activeTarget =
                RenderTargetBindingDescriptor::ForRenderTarget2D(
                    target.get(), 0, 8, 8, target->GetMultiSampleCount());
            renderer.SetRenderTargets(&activeTarget, 1);
            renderer.SetViewport(0, 0, 8, 8, 0.0f, 1.0f);
            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();
            Check(lifetime->GetSnapshotForTesting().resourceRestorations == 2 &&
                    contentLostCount == 2 &&
                    ExactResourceStateSurvived(
                        *texture, *dxtTexture, *cube, *dxtCube,
                        *vertexBuffer, *indexBuffer, *target, *targetCube,
                        contentLostTarget, textureLevel1, dxtLevel1, cubeFace, cubeTail) &&
                    DrawActiveTargetAndStock(
                        renderer, *vertexBuffer, *indexBuffer, *target),
                "a second complete cycle preserves resources and the active target");
            Check(DrawSprite(renderer, device, *spriteBatch, *texture, textureColor),
                "SpriteBatch remains usable after the second complete cycle");
#if defined(CNA_RLGL_COMPILED_EFFECTS)
            Check(DrawCompiled(device, compiledEffect, compiledTint),
                "compiled draw remains usable after the second complete cycle");
#endif
        }

    private:
        static bool AllNativeIdentitiesAreZero(
            const CNA::Internal::Renderers::ITextureRenderer& texture,
            const CNA::Internal::Renderers::ITextureRenderer& dxtTexture,
            const CNA::Internal::Renderers::ITextureCubeRenderer& cube,
            const CNA::Internal::Renderers::ITextureCubeRenderer& dxtCube,
            const CNA::Internal::Renderers::IVertexBufferRenderer& vertexBuffer,
            const CNA::Internal::Renderers::IIndexBufferRenderer& indexBuffer,
            const CNA::Internal::Renderers::IRenderTargetRenderer& target,
            const CNA::Internal::Renderers::IRenderTargetCubeRenderer& targetCube,
            const RenderTarget2D& contentLostTarget)
        {
            return Rlgl::GetTexture2DResourceSnapshotForTesting(texture).texture == 0 &&
                Rlgl::GetTexture2DResourceSnapshotForTesting(dxtTexture).texture == 0 &&
                Rlgl::GetTextureCubeResourceSnapshotForTesting(cube).texture == 0 &&
                Rlgl::GetTextureCubeResourceSnapshotForTesting(dxtCube).texture == 0 &&
                Rlgl::GetBufferResourceSnapshotForTesting(vertexBuffer).id == 0 &&
                Rlgl::GetBufferResourceSnapshotForTesting(indexBuffer).id == 0 &&
                Rlgl::GetRenderTargetResourceSnapshotForTesting(target).colorTexture == 0 &&
                Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(targetCube).colorTexture == 0 &&
                Rlgl::GetRenderTargetResourceSnapshotForTesting(
                    *contentLostTarget.GetRenderTargetRenderer()).colorTexture == 0;
        }

        static bool ExactResourceStateSurvived(
            CNA::Internal::Renderers::ITextureRenderer& texture,
            CNA::Internal::Renderers::ITextureRenderer& dxtTexture,
            CNA::Internal::Renderers::ITextureCubeRenderer& cube,
            CNA::Internal::Renderers::ITextureCubeRenderer& dxtCube,
            CNA::Internal::Renderers::IVertexBufferRenderer& vertexBuffer,
            CNA::Internal::Renderers::IIndexBufferRenderer& indexBuffer,
            CNA::Internal::Renderers::IRenderTargetRenderer& target,
            CNA::Internal::Renderers::IRenderTargetCubeRenderer& targetCube,
            RenderTarget2D& contentLostTarget,
            const std::vector<std::uint8_t>& textureLevel1,
            const std::vector<std::uint8_t>& dxtLevel1,
            const std::vector<std::uint8_t>& cubeFace,
            const std::vector<std::uint8_t>& cubeTail)
        {
            std::vector<std::uint8_t> textureRead(textureLevel1.size());
            std::vector<std::uint8_t> dxtRead(dxtLevel1.size());
            std::vector<std::uint8_t> cubeRead(cubeFace.size());
            std::vector<std::uint8_t> cubeTailRead(cubeTail.size());
            std::array<std::uint8_t, 64> dxtCubeRead{};
            const bool reads =
                texture.GetData(1, 0, 0, 2, 2, textureRead.data(), textureRead.size()) &&
                dxtTexture.GetData(1, 0, 0, 2, 2, dxtRead.data(), dxtRead.size()) &&
                cube.GetData(2, 0, 0, 0, 4, 4, cubeRead.data(), cubeRead.size()) &&
                cube.GetData(5, 2, 0, 0, 1, 1,
                             cubeTailRead.data(), cubeTailRead.size()) &&
                dxtCube.GetData(4, 0, 0, 0, 4, 4,
                                dxtCubeRead.data(), dxtCubeRead.size());
            const auto vertex = Rlgl::GetBufferResourceSnapshotForTesting(vertexBuffer);
            const auto index = Rlgl::GetBufferResourceSnapshotForTesting(indexBuffer);
            const auto targetState =
                Rlgl::GetRenderTargetResourceSnapshotForTesting(target);
            const auto targetCubeState =
                Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(targetCube);
            const auto publicTargetState =
                Rlgl::GetRenderTargetResourceSnapshotForTesting(
                    *contentLostTarget.GetRenderTargetRenderer());
            return reads && textureRead == textureLevel1 && dxtRead == dxtLevel1 &&
                cubeRead == cubeFace && cubeTailRead == cubeTail &&
                dxtCubeRead[0] >= 248 && dxtCubeRead[1] <= 4 && dxtCubeRead[2] <= 4 &&
                vertex.id != 0 && vertex.nativeBytes == vertex.cpuBytes &&
                vertex.declaration ==
                    VertexPositionColor::getVertexDeclarationStatic().GetVertexElements() &&
                index.id != 0 && index.nativeBytes == index.cpuBytes &&
                targetState.framebuffer != 0 && targetState.colorTexture != 0 &&
                targetState.depthStencilRenderbuffer != 0 &&
                targetCubeState.framebuffer != 0 && targetCubeState.colorTexture != 0 &&
                targetCubeState.depthStencilRenderbuffer != 0 &&
                publicTargetState.framebuffer != 0 && publicTargetState.colorTexture != 0;
        }

        static bool DrawActiveMrtAndStock(
            Rlgl::RlglRenderer& renderer,
            CNA::Internal::Renderers::IVertexBufferRenderer& vertexBuffer,
            CNA::Internal::Renderers::IIndexBufferRenderer& indexBuffer,
            CNA::Internal::Renderers::IRenderTargetRenderer& target,
            CNA::Internal::Renderers::IRenderTargetCubeRenderer& targetCube)
        {
            renderer.SetBlendEnabled(false);
            renderer.SetDepthTestEnabled(false);
            renderer.ApplyRasterizerState(0, 0, false);
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            const Matrix identity = Matrix::getIdentityProperty();
            renderer.DrawIndexedColoredPrimitives(
                vertexBuffer, indexBuffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1);
            renderer.SetRenderTargets(nullptr, 0);

            std::array<std::uint8_t, 4> targetPixel{};
            std::array<std::uint8_t, 4> cubePixel{};
            return target.GetData(0, 4, 4, 1, 1,
                                  targetPixel.data(), targetPixel.size()) &&
                targetCube.GetData(3, 0, 0, 0, 1, 1,
                                   cubePixel.data(), cubePixel.size()) &&
                targetPixel[0] >= 252 && targetPixel[1] <= 3 && targetPixel[2] <= 3 &&
                cubePixel[0] <= 3 && cubePixel[1] <= 3 && cubePixel[2] <= 3;
        }

        static bool DrawActiveTargetAndStock(
            Rlgl::RlglRenderer& renderer,
            CNA::Internal::Renderers::IVertexBufferRenderer& vertexBuffer,
            CNA::Internal::Renderers::IIndexBufferRenderer& indexBuffer,
            CNA::Internal::Renderers::IRenderTargetRenderer& target)
        {
            renderer.SetBlendEnabled(false);
            renderer.SetDepthTestEnabled(false);
            renderer.ApplyRasterizerState(0, 0, false);
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            const Matrix identity = Matrix::getIdentityProperty();
            renderer.DrawIndexedColoredPrimitives(
                vertexBuffer, indexBuffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1);
            renderer.SetRenderTargets(nullptr, 0);
            std::array<std::uint8_t, 4> pixel{};
            return target.GetData(
                       0, 4, 4, 1, 1, pixel.data(), pixel.size()) &&
                pixel[0] >= 252 && pixel[1] <= 3 && pixel[2] <= 3;
        }

        static bool DrawSprite(
            Rlgl::RlglRenderer& renderer, GraphicsDevice& device,
            CNA::Internal::Renderers::ISpriteBatchRenderer& spriteBatch,
            const CNA::Internal::Renderers::ITextureRenderer& texture,
            const Color& expected)
        {
            renderer.SetViewport(0, 0, 64, 64, 0.0f, 1.0f);
            device.Clear(Color::Black);
            spriteBatch.Begin();
            spriteBatch.Draw(texture, 8.0f, 8.0f);
            spriteBatch.End();
            Color pixel;
            const Rectangle rectangle(9, 9, 1, 1);
            device.GetBackBufferData(&rectangle, &pixel, 0, 1);
            return IsColor(pixel, expected);
        }

        static bool QueryRuns(CNA::Internal::Renderers::IOcclusionQueryRenderer& query)
        {
            try
            {
                query.Begin();
                query.End();
                (void)query.IsComplete();
                (void)query.PixelCount();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

#if defined(CNA_RLGL_COMPILED_EFFECTS)
        static bool DrawCompiled(
            GraphicsDevice& device, Effect& effect, const Vector4& tint)
        {
            struct ClipVertex
            {
                float x;
                float y;
                float z;
            };
            const VertexDeclaration declaration(sizeof(ClipVertex), {
                VertexElement(
                    0, VertexElementFormat::Vector3,
                    VertexElementUsage::Position, 0)});
            const std::array<ClipVertex, 6> quad{{
                {-1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}};
            device.Clear(Color::Black);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, static_cast<const void*>(quad.data()),
                0, 2, declaration);
            Color pixel;
            const Rectangle centre(32, 32, 1, 1);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            const auto channel = [](const float value)
            {
                return static_cast<int>(value * 255.0f + 0.5f);
            };
            return std::abs(static_cast<int>(pixel.getRProperty()) - channel(tint.X)) <= 2 &&
                std::abs(static_cast<int>(pixel.getGProperty()) - channel(tint.Y)) <= 2 &&
                std::abs(static_cast<int>(pixel.getBProperty()) - channel(tint.Z)) <= 2 &&
                std::abs(static_cast<int>(pixel.getAProperty()) - channel(tint.W)) <= 2;
        }
#endif

        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };
}

int main()
{
    SetResourceFailureIndex(nullptr);
    return CNA::Examples::RunPixelTest<ResourceRecreateTest>();
}
