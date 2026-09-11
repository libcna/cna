// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2243: independently prove Vulkan Texture2DArray limits, factory
// decisions, native identity, binding retention, deferred retirement and live-resource teardown.

#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>

using CNA::Graphics::Texture2DArrayUsage;
using CNA::Internal::Renderers::ITexture2DArrayRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanEffectRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanTexture2DArrayRenderer;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

namespace
{
    struct FormatCase
    {
        SurfaceFormat surface;
        VkFormat vulkan;
        const char* name;
    };

    constexpr std::array<FormatCase, 27> Formats{{
        {SurfaceFormat::Color, VK_FORMAT_R8G8B8A8_UNORM, "Color"},
        {SurfaceFormat::Bgr565, VK_FORMAT_R5G6B5_UNORM_PACK16, "Bgr565"},
        {SurfaceFormat::Bgra5551, VK_FORMAT_A1R5G5B5_UNORM_PACK16, "Bgra5551"},
        {SurfaceFormat::Bgra4444, VK_FORMAT_A4R4G4B4_UNORM_PACK16, "Bgra4444"},
        {SurfaceFormat::Dxt1, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "Dxt1"},
        {SurfaceFormat::Dxt3, VK_FORMAT_BC2_UNORM_BLOCK, "Dxt3"},
        {SurfaceFormat::Dxt5, VK_FORMAT_BC3_UNORM_BLOCK, "Dxt5"},
        {SurfaceFormat::NormalizedByte2, VK_FORMAT_R8G8_SNORM, "NormalizedByte2"},
        {SurfaceFormat::NormalizedByte4, VK_FORMAT_R8G8B8A8_SNORM, "NormalizedByte4"},
        {SurfaceFormat::Rgba1010102, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Rgba1010102"},
        {SurfaceFormat::Rg32, VK_FORMAT_R16G16_UNORM, "Rg32"},
        {SurfaceFormat::Rgba64, VK_FORMAT_R16G16B16A16_UNORM, "Rgba64"},
        {SurfaceFormat::Alpha8, VK_FORMAT_R8_UNORM, "Alpha8"},
        {SurfaceFormat::Single, VK_FORMAT_R32_SFLOAT, "Single"},
        {SurfaceFormat::Vector2, VK_FORMAT_R32G32_SFLOAT, "Vector2"},
        {SurfaceFormat::Vector4, VK_FORMAT_R32G32B32A32_SFLOAT, "Vector4"},
        {SurfaceFormat::HalfSingle, VK_FORMAT_R16_SFLOAT, "HalfSingle"},
        {SurfaceFormat::HalfVector2, VK_FORMAT_R16G16_SFLOAT, "HalfVector2"},
        {SurfaceFormat::HalfVector4, VK_FORMAT_R16G16B16A16_SFLOAT, "HalfVector4"},
        {SurfaceFormat::HdrBlendable, VK_FORMAT_R16G16B16A16_SFLOAT, "HdrBlendable"},
        {SurfaceFormat::ColorBgraEXT, VK_FORMAT_B8G8R8A8_UNORM, "ColorBgraEXT"},
        {SurfaceFormat::ColorSrgbEXT, VK_FORMAT_R8G8B8A8_SRGB, "ColorSrgbEXT"},
        {SurfaceFormat::Dxt5SrgbEXT, VK_FORMAT_BC3_SRGB_BLOCK, "Dxt5SrgbEXT"},
        {SurfaceFormat::Bc7EXT, VK_FORMAT_BC7_UNORM_BLOCK, "Bc7EXT"},
        {SurfaceFormat::Bc7SrgbEXT, VK_FORMAT_BC7_SRGB_BLOCK, "Bc7SrgbEXT"},
        {SurfaceFormat::ByteEXT, VK_FORMAT_R8_UNORM, "ByteEXT"},
        {SurfaceFormat::UShortEXT, VK_FORMAT_R16_UNORM, "UShortEXT"},
    }};

    [[nodiscard]] constexpr std::uint32_t UsageBits(const Texture2DArrayUsage usage)
    {
        return static_cast<std::uint32_t>(usage);
    }

    struct UsageCase
    {
        std::uint32_t declared;
        VkImageUsageFlags native;
        VkFormatFeatureFlags requiredFeatures;
        const char* name;
    };

    constexpr std::uint32_t Sampled = UsageBits(Texture2DArrayUsage::Sampled);
    constexpr std::uint32_t Filterable = UsageBits(Texture2DArrayUsage::Filterable);
    constexpr std::uint32_t TransferSource = UsageBits(Texture2DArrayUsage::TransferSource);
    constexpr std::uint32_t TransferDestination =
        UsageBits(Texture2DArrayUsage::TransferDestination);

    constexpr std::array<UsageCase, 5> Usages{{
        {Sampled, VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
         "sampled"},
        {Sampled | Filterable, VK_IMAGE_USAGE_SAMPLED_BIT,
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
         "sampled+filterable"},
        {Sampled | TransferSource,
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
         "sampled+transfer-source"},
        {Sampled | TransferDestination,
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
         "sampled+transfer-destination"},
        {Sampled | Filterable | TransferSource | TransferDestination,
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
             VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
         "all"},
    }};

    [[nodiscard]] int ClampToInt(const std::uint32_t value)
    {
        return value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(value);
    }
}

class VulkanTexture2DArrayContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<ITexture2DArrayRenderer> retainedAcrossDeviceTeardown_;
    VulkanTexture2DArrayRenderer* retainedNative_ = nullptr;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(const bool ok, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (ok) ++passed_; else ++failed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "A the live renderer is Vulkan");
        if (renderer == nullptr)
        {
            Exit();
            return;
        }

        const std::size_t validationBefore = renderer->GetValidationMessagesEXT().size();
        const VkPhysicalDevice physical = renderer->GetPhysicalDeviceHandleEXT();
        VkImageFormatProperties colorArrayProperties{};
        const VkResult colorArrayResult = vkGetPhysicalDeviceImageFormatProperties(
            physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0,
            &colorArrayProperties);
        const int expectedLayers = colorArrayResult == VK_SUCCESS
            ? ClampToInt(colorArrayProperties.maxArrayLayers) : 0;
        const auto publishedLayers =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers);
        Check(publishedLayers.known &&
                  publishedLayers.value == static_cast<std::uint64_t>(expectedLayers) &&
                  renderer->GetMaxTextureArrayLayersEXT() == expectedLayers,
              "B the published array-layer limit equals the raw Color sampled-image limit",
              "published=" + std::to_string(publishedLayers.value) +
                  " raw=" + std::to_string(expectedLayers));

        const std::size_t liveBefore = renderer->GetLiveTexture2DArrayCountEXT();
        const std::uint64_t retiredBefore =
            renderer->GetRetiredTexture2DArrayCountEXT();
        int accepted = 0;
        int refused = 0;
        bool matrixExact = true;
        bool identitiesExact = true;
        std::string firstMatrixError;
        std::string firstIdentityError;

        for (const FormatCase& format : Formats)
        {
            VulkanRenderer::VulkanSurfaceFormatStorageEXT storage{};
            const bool implemented = renderer->MapSurfaceFormatToStorageEXT(
                static_cast<int>(format.surface), storage);
            if (implemented && storage.format != format.vulkan)
            {
                identitiesExact = false;
                if (firstIdentityError.empty())
                    firstIdentityError = std::string(format.name) +
                        " storage mapping substituted VkFormat " +
                        std::to_string(static_cast<int>(storage.format));
            }

            VkFormatProperties formatProperties{};
            if (implemented)
                vkGetPhysicalDeviceFormatProperties(physical, format.vulkan, &formatProperties);

            for (const UsageCase& usage : Usages)
            {
                VkImageFormatProperties imageProperties{};
                const bool rawAccepts = implemented &&
                    (formatProperties.optimalTilingFeatures & usage.requiredFeatures) ==
                        usage.requiredFeatures &&
                    vkGetPhysicalDeviceImageFormatProperties(
                        physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                        usage.native, 0, &imageProperties) == VK_SUCCESS &&
                    imageProperties.maxExtent.width >= 7 &&
                    imageProperties.maxExtent.height >= 5 &&
                    imageProperties.maxArrayLayers >= 2 && imageProperties.maxMipLevels >= 3;

                std::unique_ptr<ITexture2DArrayRenderer> candidate;
                std::string thrown;
                try
                {
                    candidate = renderer->CreateTexture2DArrayEXT(
                        7, 5, 2, 3, static_cast<int>(format.surface), usage.declared);
                }
                catch (const std::exception& exception)
                {
                    thrown = exception.what();
                }

                const bool created = candidate != nullptr;
                if (created) ++accepted; else ++refused;
                if (created != rawAccepts || !thrown.empty())
                {
                    matrixExact = false;
                    if (firstMatrixError.empty())
                        firstMatrixError = std::string(format.name) + "/" + usage.name +
                            " expected=" + (rawAccepts ? "create" : "refuse") +
                            " actual=" + (created ? "create" : "refuse") +
                            (thrown.empty() ? "" : " threw=" + thrown);
                }

                if (created)
                {
                    auto* native = dynamic_cast<VulkanTexture2DArrayRenderer*>(candidate.get());
                    const bool identity = native != nullptr && native->HasOwnerEXT() &&
                        native->GetWidthEXT() == 7 && native->GetHeightEXT() == 5 &&
                        native->GetLayerCountEXT() == 2 && native->GetMipLevelCountEXT() == 3 &&
                        native->GetSurfaceFormatEXT() == static_cast<int>(format.surface) &&
                        native->GetDeclaredUsageEXT() == usage.declared &&
                        native->GetVkFormatEXT() == format.vulkan &&
                        native->GetVkImageUsageEXT() == usage.native &&
                        native->GetVkImageEXT() != VK_NULL_HANDLE &&
                        native->GetVkArrayImageView() != VK_NULL_HANDLE &&
                        native->GetVkImageViewTypeEXT() == VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    if (!identity)
                    {
                        identitiesExact = false;
                        if (firstIdentityError.empty())
                            firstIdentityError = std::string(format.name) + "/" + usage.name +
                                " did not retain its exact image/view identity";
                    }
                }
                candidate.reset();
                if (renderer->GetLiveTexture2DArrayCountEXT() != liveBefore)
                {
                    identitiesExact = false;
                    if (firstIdentityError.empty())
                        firstIdentityError = "destroyed factory result remained in the live list";
                }
            }
        }

        Check(matrixExact, "C every format/usage factory decision equals raw device support",
              matrixExact ? std::to_string(accepted) + " created, " +
                                std::to_string(refused) + " refused"
                          : firstMatrixError);
        Check(identitiesExact, "D every accepted image retains exact dimensions, format and view",
              identitiesExact ? std::to_string(accepted) +
                                    " native identities checked"
                              : firstIdentityError);
        Check(renderer->GetLiveTexture2DArrayCountEXT() == liveBefore &&
                  renderer->GetRetiredTexture2DArrayCountEXT() ==
                      retiredBefore + static_cast<std::uint64_t>(accepted),
              "E every destroyed matrix image leaves the live list through fence retirement",
              "live=" + std::to_string(renderer->GetLiveTexture2DArrayCountEXT()) +
                  " retiredDelta=" + std::to_string(
                      renderer->GetRetiredTexture2DArrayCountEXT() - retiredBefore));

        bool invalidRequestsRefused =
            renderer->CreateTexture2DArrayEXT(0, 5, 2, 1, 0, Sampled) == nullptr &&
            renderer->CreateTexture2DArrayEXT(7, 5, 2, 0, 0, Sampled) == nullptr &&
            renderer->CreateTexture2DArrayEXT(7, 5, 2, 4, 0, Sampled) == nullptr &&
            renderer->CreateTexture2DArrayEXT(7, 5, 2, 1, 0, 0) == nullptr &&
            renderer->CreateTexture2DArrayEXT(
                7, 5, 2, 1, 0, Sampled | (UINT32_C(1) << 31)) == nullptr &&
            renderer->CreateTexture2DArrayEXT(7, 5, 2, 1, 27, Sampled) == nullptr;
        if (expectedLayers > 0 && expectedLayers < std::numeric_limits<int>::max())
            invalidRequestsRefused = invalidRequestsRefused &&
                renderer->CreateTexture2DArrayEXT(
                    1, 1, expectedLayers + 1, 1, 0, Sampled) == nullptr;
        if (colorArrayResult == VK_SUCCESS &&
            colorArrayProperties.maxExtent.width <
                static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            invalidRequestsRefused = invalidRequestsRefused &&
                renderer->CreateTexture2DArrayEXT(
                    static_cast<int>(colorArrayProperties.maxExtent.width) + 1,
                    1, 1, 1, 0, Sampled) == nullptr;
        }
        Check(invalidRequestsRefused,
              "F malformed, over-limit and overlong mip requests refuse before vkCreateImage");

        const std::size_t liveBeforeBinding = renderer->GetLiveTexture2DArrayCountEXT();
        const std::uint64_t retiredBeforeBinding =
            renderer->GetRetiredTexture2DArrayCountEXT();
        auto firstUnique = renderer->CreateTexture2DArrayEXT(3, 3, 2, 2, 0, Sampled);
        auto secondUnique = renderer->CreateTexture2DArrayEXT(3, 3, 2, 2, 0, Sampled);
        bool bindingLifetimeExact = firstUnique != nullptr && secondUnique != nullptr;
        if (bindingLifetimeExact)
        {
            std::shared_ptr<ITexture2DArrayRenderer> first(std::move(firstUnique));
            std::shared_ptr<ITexture2DArrayRenderer> second(std::move(secondUnique));
            VulkanEffectRenderer effect(renderer);
            bindingLifetimeExact = effect.BindTexture2DArrayEXT(0, first);
            first.reset();
            bindingLifetimeExact = bindingLifetimeExact &&
                renderer->GetLiveTexture2DArrayCountEXT() == liveBeforeBinding + 2 &&
                renderer->GetRetiredTexture2DArrayCountEXT() == retiredBeforeBinding;
            bindingLifetimeExact = bindingLifetimeExact &&
                effect.BindTexture2DArrayEXT(0, second);
            second.reset();
            bindingLifetimeExact = bindingLifetimeExact &&
                renderer->GetLiveTexture2DArrayCountEXT() == liveBeforeBinding + 1 &&
                renderer->GetRetiredTexture2DArrayCountEXT() == retiredBeforeBinding + 1;
            bindingLifetimeExact = bindingLifetimeExact &&
                effect.BindTexture2DArrayEXT(0, nullptr) &&
                renderer->GetLiveTexture2DArrayCountEXT() == liveBeforeBinding &&
                renderer->GetRetiredTexture2DArrayCountEXT() == retiredBeforeBinding + 2;
        }
        else
        {
            firstUnique.reset();
            secondUnique.reset();
        }
        Check(bindingLifetimeExact,
              "G rebind retains only current work and retires replaced/cleared array images");

        retainedAcrossDeviceTeardown_ = renderer->CreateTexture2DArrayEXT(
            7, 5, 2, 3, 0,
            Sampled | Filterable | TransferSource | TransferDestination);
        retainedNative_ = dynamic_cast<VulkanTexture2DArrayRenderer*>(
            retainedAcrossDeviceTeardown_.get());
        Check(retainedNative_ != nullptr && retainedNative_->HasOwnerEXT() &&
                  retainedNative_->GetVkImageEXT() != VK_NULL_HANDLE &&
                  retainedNative_->GetVkArrayImageView() != VK_NULL_HANDLE &&
                  renderer->GetLiveTexture2DArrayCountEXT() == liveBefore + 1,
              "H one live array record is deliberately retained across device teardown");

        Check(renderer->GetValidationMessagesEXT().size() == validationBefore,
              "I limit, allocation, view, rebind and refusal paths emit no validation message",
              std::to_string(validationBefore) + " -> " +
                  std::to_string(renderer->GetValidationMessagesEXT().size()));

        Exit();
    }

public:
    VulkanTexture2DArrayContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void TearDownGraphicsDeviceAndVerify()
    {
        getGraphicsDeviceProperty().Dispose();
        const bool released = retainedNative_ != nullptr && !retainedNative_->HasOwnerEXT() &&
            retainedNative_->GetVkImageEXT() == VK_NULL_HANDLE &&
            retainedNative_->GetVkArrayImageView() == VK_NULL_HANDLE;
        Check(released,
              "J explicit GraphicsDevice teardown releases and disconnects a surviving array",
              retainedNative_ == nullptr
                  ? "the retained record was unexpectedly null"
                  : "owner=" + std::string(retainedNative_->HasOwnerEXT() ? "live" : "null") +
                        " image=" +
                        std::string(retainedNative_->GetVkImageEXT() != VK_NULL_HANDLE
                                        ? "live" : "null") +
                        " view=" +
                        std::string(retainedNative_->GetVkArrayImageView() != VK_NULL_HANDLE
                                        ? "live" : "null"));
        retainedAcrossDeviceTeardown_.reset();
        retainedNative_ = nullptr;
        Check(retainedAcrossDeviceTeardown_ == nullptr,
              "K the disconnected record can be destroyed after its Vulkan device");
    }

    void RecordTeardownFailure(const std::string& detail)
    {
        Check(false, "J device teardown releases a surviving array record", detail);
    }

    [[nodiscard]] int Result() const noexcept { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanTexture2DArrayContractTest game;
    game.Run();
    try
    {
        game.TearDownGraphicsDeviceAndVerify();
        game.Dispose();
    }
    catch (const std::exception& exception)
    {
        game.RecordTeardownFailure(exception.what());
    }
    std::printf("=== MOD-2243 result: %s ===\n", game.Result() == 0 ? "PASS" : "FAIL");
    return game.Result();
}
