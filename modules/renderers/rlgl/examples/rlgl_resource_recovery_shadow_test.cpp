// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-055: exact resource descriptions and CPU shadows are retained only
// for recovery-registered children; default-pool render targets are classified as content-lost.

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglResourceLifetime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "RlglResources.hpp"
#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;

#if defined(CNA_RLGL_COMPILED_EFFECTS)
    constexpr std::size_t kEnabledResourceCount = 11;
#else
    constexpr std::size_t kEnabledResourceCount = 10;
#endif

    [[nodiscard]] CNA::Internal::Graphics::ImageData MakeImage(
        const int width, const int height, const int mipLevels,
        const SurfaceFormat format, const std::vector<std::uint8_t>& bytes)
    {
        CNA::Internal::Graphics::ImageData image;
        image.width = width;
        image.height = height;
        image.mipLevels = mipLevels;
        image.surfaceFormat = static_cast<int>(format);
        image.pixels = bytes;
        return image;
    }

    [[nodiscard]] std::vector<std::uint8_t> Sequence(
        const std::size_t count, const std::uint8_t first)
    {
        std::vector<std::uint8_t> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::uint8_t>(first + index);
        return bytes;
    }

    [[nodiscard]] bool EqualBytes(
        const std::vector<std::uint8_t>& actual,
        const std::vector<std::uint8_t>& expected)
    {
        return actual == expected;
    }

    class ResourceRecoveryShadowTest final : public CNA::Examples::PixelTestGame
    {
    public:
        ResourceRecoveryShadowTest()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(48);
            graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());
            const auto lifetime = renderer.GetResourceLifetimeForTesting();

            const auto initial = lifetime->GetSnapshotForTesting();
            Check(initial.active && initial.registeredResources == 0 &&
                    initial.recoveryResources == 0 &&
                    initial.recoveryEnabledForNewResources,
                "recovery registration is enabled by default on an empty device");

            {
                Texture2D publicTexture(device, 2, 2);
                const std::array<Color, 4> colors{
                    Color(1, 2, 3, 4), Color(5, 6, 7, 8),
                    Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
                publicTexture.SetData(colors.data(), static_cast<int>(colors.size()));
                const auto publicState =
                    Rlgl::GetTexture2DResourceSnapshotForTesting(publicTexture.GetRenderer());
                const std::vector<std::uint8_t> expected{
                    1, 2, 3, 4, 5, 6, 7, 8,
                    9, 10, 11, 12, 13, 14, 15, 16};
                const auto shared = lifetime->GetSnapshotForTesting();
                Check(publicState.recoveryLevels.size() == 1 &&
                        publicState.recoveryLevels[0] == expected &&
                        shared.recoveryResources == 1 && shared.retainedCpuBytes == 16,
                    "public Texture2D shares its level-zero CPU storage with recovery");
            }

            const auto color0 = Sequence(64, 3);
            auto colorTexture = renderer.CreateTexture(MakeImage(
                4, 4, 3, SurfaceFormat::Color, color0));
            const auto color1 = Sequence(16, 81);
            const auto color2 = Sequence(4, 141);
            colorTexture->UpdatePixelsLevel(1, color1.data(), 2, 2);
            colorTexture->UpdatePixelsLevel(2, color2.data(), 1, 1);
            const auto colorTextureState =
                Rlgl::GetTexture2DResourceSnapshotForTesting(*colorTexture);
            Check(colorTextureState.recoveryRegistered &&
                    colorTextureState.width == 4 && colorTextureState.height == 4 &&
                    colorTextureState.levelCount == 3 &&
                    colorTextureState.definedLevels ==
                        std::vector<bool>({true, true, true}) &&
                    colorTextureState.recoveryLevels.size() == 3 &&
                    EqualBytes(colorTextureState.recoveryLevels[0], color0) &&
                    EqualBytes(colorTextureState.recoveryLevels[1], color1) &&
                    EqualBytes(colorTextureState.recoveryLevels[2], color2),
                "Texture2D retains exact bytes and defined state for every uploaded mip");

            auto colorCube = renderer.CreateTextureCube(
                4, true, static_cast<int>(SurfaceFormat::Color));
            const auto cubePatch = Sequence(16, 33);
            colorCube->SetData(
                2, 0, 1, 1, 2, 2, cubePatch.data(),
                static_cast<int>(cubePatch.size()));
            const auto cubeTail = Sequence(4, 211);
            colorCube->SetData(
                5, 2, 0, 0, 1, 1, cubeTail.data(),
                static_cast<int>(cubeTail.size()));
            const auto colorCubeState =
                Rlgl::GetTextureCubeResourceSnapshotForTesting(*colorCube);
            std::vector<std::uint8_t> expectedColorFace(64, 0u);
            for (int row = 0; row < 2; ++row)
            {
                std::copy_n(
                    cubePatch.data() + static_cast<std::size_t>(row) * 8u, 8,
                    expectedColorFace.data() +
                        static_cast<std::size_t>(1 + row) * 16u + 4u);
            }
            Check(colorCubeState.recoveryRegistered && colorCubeState.levelCount == 3 &&
                    colorCubeState.definedSubresources.size() == 18 &&
                    std::count(
                        colorCubeState.definedSubresources.begin(),
                        colorCubeState.definedSubresources.end(), true) == 2 &&
                    colorCubeState.definedSubresources[6] &&
                    colorCubeState.definedSubresources[17] &&
                    EqualBytes(colorCubeState.recoverySubresources[6], expectedColorFace) &&
                    EqualBytes(colorCubeState.recoverySubresources[17], cubeTail),
                "TextureCube merges partial Color writes into exact face-major shadows");

            const auto dxtTexture0 = Sequence(8, 17);
            auto dxtTexture = renderer.CreateTexture(MakeImage(
                4, 4, 2, SurfaceFormat::Dxt1, dxtTexture0));
            const auto dxtTexture1 = Sequence(8, 101);
            dxtTexture->UpdatePixelsLevel(1, dxtTexture1.data(), 2, 2);
            const auto dxtTextureState =
                Rlgl::GetTexture2DResourceSnapshotForTesting(*dxtTexture);
            Check(dxtTextureState.recoveryRegistered &&
                    dxtTextureState.definedLevels == std::vector<bool>({true, true}) &&
                    EqualBytes(dxtTextureState.recoveryLevels[0], dxtTexture0) &&
                    EqualBytes(dxtTextureState.recoveryLevels[1], dxtTexture1),
                "compressed Texture2D retains exact blocks independently of native support");

            auto dxtCube = renderer.CreateTextureCube(
                8, true, static_cast<int>(SurfaceFormat::Dxt5));
            const auto dxtCubeLevel0Patch = Sequence(16, 49);
            dxtCube->SetCompressedDataEXT(
                0, 0, 4, 4, 4, 4, dxtCubeLevel0Patch.data(),
                static_cast<int>(dxtCubeLevel0Patch.size()));
            const auto dxtCubeLevel1 = Sequence(16, 149);
            dxtCube->SetCompressedDataEXT(
                3, 1, 0, 0, 4, 4, dxtCubeLevel1.data(),
                static_cast<int>(dxtCubeLevel1.size()));
            const auto dxtCubeState =
                Rlgl::GetTextureCubeResourceSnapshotForTesting(*dxtCube);
            std::vector<std::uint8_t> expectedDxtFace(64, 0u);
            std::copy(
                dxtCubeLevel0Patch.begin(), dxtCubeLevel0Patch.end(),
                expectedDxtFace.begin() + 48);
            Check(dxtCubeState.recoveryRegistered && dxtCubeState.levelCount == 4 &&
                    dxtCubeState.definedSubresources.size() == 24 &&
                    std::count(
                        dxtCubeState.definedSubresources.begin(),
                        dxtCubeState.definedSubresources.end(), true) == 2 &&
                    dxtCubeState.definedSubresources[0] &&
                    dxtCubeState.definedSubresources[13] &&
                    EqualBytes(dxtCubeState.recoverySubresources[0], expectedDxtFace) &&
                    EqualBytes(dxtCubeState.recoverySubresources[13], dxtCubeLevel1),
                "compressed TextureCube merges exact block regions for native and fallback paths");

            auto vertexBuffer = renderer.CreateVertexBuffer(3);
            const VertexDeclaration declaration(8, {
                VertexElement(
                    0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0)});
            vertexBuffer->SetVertexDeclaration(declaration);
            const std::array<float, 6> vertices{
                -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f};
            vertexBuffer->SetData(vertices.data(), 3, 8);
            auto indexBuffer = renderer.CreateIndexBuffer16(3);
            const std::array<std::uint16_t, 3> indices{2, 0, 1};
            indexBuffer->SetData16(indices.data(), 3);
            const auto vertexState =
                Rlgl::GetBufferResourceSnapshotForTesting(*vertexBuffer);
            const auto indexState =
                Rlgl::GetBufferResourceSnapshotForTesting(*indexBuffer);
            const std::vector<std::uint8_t> expectedVertices(
                reinterpret_cast<const std::uint8_t*>(vertices.data()),
                reinterpret_cast<const std::uint8_t*>(vertices.data()) + sizeof(vertices));
            const std::vector<std::uint8_t> expectedIndices(
                reinterpret_cast<const std::uint8_t*>(indices.data()),
                reinterpret_cast<const std::uint8_t*>(indices.data()) + sizeof(indices));
            Check(vertexState.recoveryRegistered && indexState.recoveryRegistered &&
                    vertexState.stride == 8 && vertexState.declaration.size() == 1 &&
                    EqualBytes(vertexState.cpuBytes, expectedVertices) &&
                    EqualBytes(indexState.cpuBytes, expectedIndices),
                "vertex/index buffers retain exact capacity bytes and vertex declaration");

            auto target = renderer.CreateRenderTarget2DEXT(
                8, 4, 3, true, true, 0, static_cast<int>(SurfaceFormat::Rg32));
            auto targetCube = renderer.CreateRenderTargetCubeEXT(
                8, 2, true, true, 0, static_cast<int>(SurfaceFormat::Rgba1010102));
            const auto targetState =
                Rlgl::GetRenderTargetResourceSnapshotForTesting(*target);
            const auto targetCubeState =
                Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(*targetCube);
            Check(targetState.width == 8 && targetState.height == 4 &&
                    targetState.depthFormat == 3 && targetState.levelCount == 4 &&
                    targetState.surfaceFormat == static_cast<int>(SurfaceFormat::Rg32) &&
                    targetState.preserveContents && targetCubeState.size == 8 &&
                    targetCubeState.depthFormat == 2 && targetCubeState.levelCount == 4 &&
                    targetCubeState.surfaceFormat ==
                        static_cast<int>(SurfaceFormat::Rgba1010102) &&
                    targetCubeState.preserveContents,
                "both render-target families retain exact attachment creation descriptions");

            auto spriteBatch = renderer.CreateSpriteBatch();
            auto query = renderer.CreateOcclusionQuery();
            const auto enabled = lifetime->GetSnapshotForTesting();
            Check(enabled.registeredResources == kEnabledResourceCount &&
                    enabled.recoveryResources == kEnabledResourceCount &&
                    enabled.contentLostResources == 2 &&
                    enabled.restorableResources == kEnabledResourceCount - 2 &&
                    enabled.definedTextureSubresources == 9 &&
                    enabled.retainedCpuBytes >= 278,
                "recovery registry measures all families and classifies targets as content-lost");

            device.SetContextRecoveryEnabled(false);
            const auto disabledStart = lifetime->GetSnapshotForTesting();
            Check(!disabledStart.recoveryEnabledForNewResources &&
                    disabledStart.recoveryResources == enabled.recoveryResources,
                "GraphicsDevice forwards recovery disablement without dropping existing entries");

            const auto disabledPixels = Sequence(16, 7);
            auto disabledTexture = renderer.CreateTexture(MakeImage(
                2, 2, 1, SurfaceFormat::Color, disabledPixels));
            auto disabledCube = renderer.CreateTextureCube(
                2, false, static_cast<int>(SurfaceFormat::Color));
            disabledCube->SetData(
                1, 0, 0, 0, 2, 2, disabledPixels.data(),
                static_cast<int>(disabledPixels.size()));
            auto disabledVertex = renderer.CreateVertexBuffer(2);
            disabledVertex->SetData(vertices.data(), 2, 8);
            auto disabledIndex = renderer.CreateIndexBuffer16(3);
            disabledIndex->SetData16(indices.data(), 3);
            const auto disabledDxtBytes = Sequence(8, 221);
            auto disabledDxt = renderer.CreateTexture(MakeImage(
                4, 4, 1, SurfaceFormat::Dxt1, disabledDxtBytes));

            const auto disabledTextureState =
                Rlgl::GetTexture2DResourceSnapshotForTesting(*disabledTexture);
            const auto disabledCubeState =
                Rlgl::GetTextureCubeResourceSnapshotForTesting(*disabledCube);
            const auto disabledVertexState =
                Rlgl::GetBufferResourceSnapshotForTesting(*disabledVertex);
            const auto disabledIndexState =
                Rlgl::GetBufferResourceSnapshotForTesting(*disabledIndex);
            const std::vector<std::uint8_t> expectedDisabledVertices(
                reinterpret_cast<const std::uint8_t*>(vertices.data()),
                reinterpret_cast<const std::uint8_t*>(vertices.data()) + 16);
            std::vector<std::uint8_t> disabledDxtReadback(8, 0u);
            const bool dxtRead = disabledDxt->GetData(
                0, 0, 0, 4, 4, disabledDxtReadback.data(),
                static_cast<int>(disabledDxtReadback.size()));
            Check(!disabledTextureState.recoveryRegistered &&
                    disabledTextureState.definedLevels.empty() &&
                    disabledTextureState.recoveryLevels.empty() &&
                    !disabledCubeState.recoveryRegistered &&
                    disabledCubeState.definedSubresources.empty() &&
                    disabledCubeState.recoverySubresources.empty() &&
                    !disabledVertexState.recoveryRegistered &&
                    disabledVertexState.cpuBytes.empty() &&
                    !disabledIndexState.recoveryRegistered &&
                    disabledIndexState.cpuBytes.empty(),
                "resources created while disabled retain no recovery shadows");
            Check(disabledVertexState.nativeBytes == expectedDisabledVertices &&
                    disabledIndexState.nativeBytes == expectedIndices && dxtRead &&
                    disabledDxtReadback == disabledDxtBytes,
                "disabled recovery does not change native uploads or fallback DXT readback");

            const auto disabled = lifetime->GetSnapshotForTesting();
            Check(disabled.registeredResources == kEnabledResourceCount + 5 &&
                    disabled.recoveryResources == enabled.recoveryResources &&
                    disabled.retainedCpuBytes == enabled.retainedCpuBytes &&
                    disabled.definedTextureSubresources ==
                        enabled.definedTextureSubresources,
                "disabled children remain teardown-tracked but add no recovery memory");

            device.SetContextRecoveryEnabled(true);
            auto reenabledQuery = renderer.CreateOcclusionQuery();
            const auto reenabled = lifetime->GetSnapshotForTesting();
            Check(reenabled.recoveryEnabledForNewResources &&
                    reenabled.registeredResources == kEnabledResourceCount + 6 &&
                    reenabled.recoveryResources == enabled.recoveryResources + 1 &&
                    reenabled.contentLostResources == enabled.contentLostResources,
                "re-enabling recovery affects only resources created afterward");
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };
}

int main()
{
    return CNA::Examples::RunPixelTest<ResourceRecoveryShadowTest>();
}
