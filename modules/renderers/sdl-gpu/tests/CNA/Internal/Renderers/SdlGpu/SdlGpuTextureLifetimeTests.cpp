// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SDL_GPU) || defined(CNA_RENDERER_PRESENT_SDL_GPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include <functional>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

class SdlGpuTextureLifetimeTest : public ::testing::TestWithParam<const char *>
{
};

TEST_P(SdlGpuTextureLifetimeTest, NativeAllocationIsReleasedBeforeAnEscapedRendererOrState)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::SdlGpu);
    const std::string kind = GetParam();
    std::shared_ptr<void> escaped;
    std::function<bool()> nativeAllocationIsLive;
    std::function<void()> checkAttachments;
    {
        PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              parameters);
        if (kind == "Texture2D")
        {
            Texture2D texture(device, 2, 2);
            auto renderer =
                std::dynamic_pointer_cast<SdlGpuTextureRenderer>(texture.GetRenderer().shared_from_this());
            ASSERT_NE(renderer, nullptr);
            escaped = renderer;
            nativeAllocationIsLive = [raw = renderer.get()] { return raw->Sampled().texture != nullptr; };
        }
        else if (kind == "Texture3D")
        {
            Texture3D texture(device, 2, 2, 2, false, SurfaceFormat::Color);
            auto renderer =
                std::dynamic_pointer_cast<SdlGpuTexture3DRenderer>(texture.GetRenderer().shared_from_this());
            ASSERT_NE(renderer, nullptr);
            escaped = renderer;
            nativeAllocationIsLive = [raw = renderer.get()] { return raw->Sampled().texture != nullptr; };
        }
        else if (kind == "TextureCube")
        {
            TextureCube texture(device, 2, false, SurfaceFormat::Color);
            auto renderer = std::dynamic_pointer_cast<SdlGpuTextureCubeRenderer>(
                texture.GetRenderer().shared_from_this());
            ASSERT_NE(renderer, nullptr);
            escaped = renderer;
            nativeAllocationIsLive = [raw = renderer.get()] { return raw->Sampled().texture != nullptr; };
        }
        else if (kind == "Target2D" || kind == "Target2DState")
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 4,
                                  RenderTargetUsage::PreserveContents);
            Texture2D texture(std::move(target));
            auto renderer = std::dynamic_pointer_cast<SdlGpuRenderTargetRenderer>(
                texture.GetRenderer().shared_from_this());
            ASSERT_NE(renderer, nullptr);
            const auto state = renderer->State();
            escaped = kind == "Target2D" ? std::shared_ptr<void>(renderer) : std::shared_ptr<void>(state);
            nativeAllocationIsLive = [raw = state.get()] { return raw->colorTexture != nullptr; };
            checkAttachments = [raw = state.get()] {
                EXPECT_EQ(raw->owner, nullptr);
                EXPECT_EQ(raw->depthTexture, nullptr);
                EXPECT_EQ(raw->msaaTexture, nullptr);
            };
        }
        else
        {
            ASSERT_TRUE(kind == "TargetCube" || kind == "TargetCubeState");
            RenderTargetCube target(device, 2, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 4,
                                    RenderTargetUsage::PreserveContents);
            TextureCube texture(std::move(target));
            auto renderer = std::dynamic_pointer_cast<SdlGpuRenderTargetCubeRenderer>(
                texture.GetRenderer().shared_from_this());
            ASSERT_NE(renderer, nullptr);
            const auto state = renderer->State();
            escaped = kind == "TargetCube" ? std::shared_ptr<void>(renderer) : std::shared_ptr<void>(state);
            nativeAllocationIsLive = [raw = state.get()] { return raw->cubeTexture != nullptr; };
            checkAttachments = [raw = state.get()] {
                EXPECT_EQ(raw->owner, nullptr);
                EXPECT_EQ(raw->depthTexture, nullptr);
                for (auto attachment : raw->msaaTextures)
                    EXPECT_EQ(attachment, nullptr);
            };
        }
        ASSERT_NE(escaped, nullptr);
        ASSERT_TRUE(nativeAllocationIsLive()) << "retention must not release a live allocation";
    }
    EXPECT_FALSE(nativeAllocationIsLive()) << "native textures cannot outlive their device";
    if (checkAttachments)
        checkAttachments();
    escaped.reset(); // ASan protects the later state/wrapper destructors from stale owner accesses.
}

INSTANTIATE_TEST_SUITE_P(SdlGpuResources, SdlGpuTextureLifetimeTest,
                         ::testing::Values("Texture2D", "Texture3D", "TextureCube", "Target2D", "TargetCube",
                                           "Target2DState", "TargetCubeState"),
                         [](const ::testing::TestParamInfo<const char *> &info) { return info.param; });

TEST(SdlGpuTextureLifetime, FailedConstructionLeavesNoRegisteredState)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::SdlGpu);
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    auto &renderer = dynamic_cast<SdlGpuRenderer &>(device.GetRenderer());
    CNA::Internal::Renderers::ImageData invalidImage;
    invalidImage.width = 0;
    invalidImage.height = 2;
    EXPECT_THROW(SdlGpuTextureRenderer(renderer, invalidImage), std::invalid_argument);
    EXPECT_THROW(SdlGpuTexture3DRenderer(renderer, 2, 2, 2, false, -1), std::invalid_argument);
    EXPECT_THROW(SdlGpuTextureCubeRenderer(renderer, 2, false, -1), std::invalid_argument);
    EXPECT_THROW(SdlGpuRenderTargetRenderer(renderer, 2, 2, 0, false, false, 0, -1), std::invalid_argument);
    EXPECT_THROW(SdlGpuRenderTargetCubeRenderer(renderer, 2, 0, false, false, 0, -1), std::invalid_argument);
}
#endif
