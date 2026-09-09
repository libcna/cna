// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2222/MOD-2224: detailed Vulkan format and limit answers must come from
// the selected physical device, be intersected with resource paths CNA actually implements, and
// agree with real RenderTarget2D construction for base, mipmapped and multisampled requests.

#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#ifdef CNA_CNAEXT
#include "CNA/Graphics/StorageTexture2D.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::RendererFormatVerdict;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderTargetRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanStorageTexture2DRenderer;

namespace
{
    struct FormatCase
    {
        SurfaceFormat surface;
        VkFormat vulkan;
        std::uint32_t spirvStorageImageFormat;
        const char* name;
    };

    constexpr std::array<FormatCase, 27> kFormats{{
        {SurfaceFormat::Color, VK_FORMAT_R8G8B8A8_UNORM, 4, "Color"},
        {SurfaceFormat::Bgr565, VK_FORMAT_R5G6B5_UNORM_PACK16, 0, "Bgr565"},
        {SurfaceFormat::Bgra5551, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 0, "Bgra5551"},
        {SurfaceFormat::Bgra4444, VK_FORMAT_A4R4G4B4_UNORM_PACK16, 0, "Bgra4444"},
        {SurfaceFormat::Dxt1, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 0, "Dxt1"},
        {SurfaceFormat::Dxt3, VK_FORMAT_BC2_UNORM_BLOCK, 0, "Dxt3"},
        {SurfaceFormat::Dxt5, VK_FORMAT_BC3_UNORM_BLOCK, 0, "Dxt5"},
        {SurfaceFormat::NormalizedByte2, VK_FORMAT_R8G8_SNORM, 18, "NormalizedByte2"},
        {SurfaceFormat::NormalizedByte4, VK_FORMAT_R8G8B8A8_SNORM, 5, "NormalizedByte4"},
        {SurfaceFormat::Rgba1010102, VK_FORMAT_A2B10G10R10_UNORM_PACK32, 11, "Rgba1010102"},
        {SurfaceFormat::Rg32, VK_FORMAT_R16G16_UNORM, 12, "Rg32"},
        {SurfaceFormat::Rgba64, VK_FORMAT_R16G16B16A16_UNORM, 10, "Rgba64"},
        {SurfaceFormat::Alpha8, VK_FORMAT_R8_UNORM, 15, "Alpha8"},
        {SurfaceFormat::Single, VK_FORMAT_R32_SFLOAT, 3, "Single"},
        {SurfaceFormat::Vector2, VK_FORMAT_R32G32_SFLOAT, 6, "Vector2"},
        {SurfaceFormat::Vector4, VK_FORMAT_R32G32B32A32_SFLOAT, 1, "Vector4"},
        {SurfaceFormat::HalfSingle, VK_FORMAT_R16_SFLOAT, 9, "HalfSingle"},
        {SurfaceFormat::HalfVector2, VK_FORMAT_R16G16_SFLOAT, 7, "HalfVector2"},
        {SurfaceFormat::HalfVector4, VK_FORMAT_R16G16B16A16_SFLOAT, 2, "HalfVector4"},
        {SurfaceFormat::HdrBlendable, VK_FORMAT_R16G16B16A16_SFLOAT, 2, "HdrBlendable"},
        {SurfaceFormat::ColorBgraEXT, VK_FORMAT_B8G8R8A8_UNORM, 0, "ColorBgraEXT"},
        {SurfaceFormat::ColorSrgbEXT, VK_FORMAT_R8G8B8A8_SRGB, 0, "ColorSrgbEXT"},
        {SurfaceFormat::Dxt5SrgbEXT, VK_FORMAT_BC3_SRGB_BLOCK, 0, "Dxt5SrgbEXT"},
        {SurfaceFormat::Bc7EXT, VK_FORMAT_BC7_UNORM_BLOCK, 0, "Bc7EXT"},
        {SurfaceFormat::Bc7SrgbEXT, VK_FORMAT_BC7_SRGB_BLOCK, 0, "Bc7SrgbEXT"},
        {SurfaceFormat::ByteEXT, VK_FORMAT_R8_UNORM, 15, "ByteEXT"},
        {SurfaceFormat::UShortEXT, VK_FORMAT_R16_UNORM, 14, "UShortEXT"},
    }};

    constexpr std::uint32_t Bit(const CNA::RendererFormatUsage usage)
    {
        return static_cast<std::uint32_t>(usage);
    }

    constexpr std::uint32_t kAllUsageBits =
        Bit(CNA::RendererFormatUsage::TextureStorage) |
        Bit(CNA::RendererFormatUsage::Sampled) |
        Bit(CNA::RendererFormatUsage::Filterable) |
        Bit(CNA::RendererFormatUsage::RenderTarget) |
        Bit(CNA::RendererFormatUsage::Blendable) |
        Bit(CNA::RendererFormatUsage::StorageRead) |
        Bit(CNA::RendererFormatUsage::StorageWrite) |
        Bit(CNA::RendererFormatUsage::StorageAtomic) |
        Bit(CNA::RendererFormatUsage::TransferSource) |
        Bit(CNA::RendererFormatUsage::TransferDestination) |
        Bit(CNA::RendererFormatUsage::Mipmapped) |
        Bit(CNA::RendererFormatUsage::Multisample) |
        Bit(CNA::RendererFormatUsage::ColorTransfer);

    int ClampToInt(const std::uint32_t value)
    {
        return value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(value);
    }

    int HighestMultiSampleCount(const VkSampleCountFlags available)
    {
        constexpr std::array<VkSampleCountFlagBits, 6> candidates{{
            VK_SAMPLE_COUNT_64_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_2_BIT,
        }};
        for (const VkSampleCountFlagBits candidate : candidates)
        {
            if ((available & candidate) != 0)
                return static_cast<int>(candidate);
        }
        return 0;
    }

}

class VulkanFormatLimitQueriesTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void Check(const bool ok, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
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

        bool mappingsExact = true;
        std::string firstMappingError;
        for (const auto& format : kFormats)
        {
            VkFormat mapped = VK_FORMAT_UNDEFINED;
            std::uint32_t spirvMapped = 0;
            if (!VulkanRenderer::MapSurfaceFormatToVkFormatEXT(
                    static_cast<int>(format.surface), mapped) || mapped != format.vulkan ||
                VulkanRenderer::MapVkFormatToSpirvStorageImageFormatEXT(
                    mapped, spirvMapped) != (format.spirvStorageImageFormat != 0) ||
                spirvMapped != format.spirvStorageImageFormat)
            {
                mappingsExact = false;
                if (firstMappingError.empty())
                    firstMappingError = std::string(format.name) + " -> " +
                        std::to_string(static_cast<int>(mapped)) + ", expected " +
                        std::to_string(static_cast<int>(format.vulkan));
            }
        }
        VkFormat invalidMapping = VK_FORMAT_R8_UNORM;
        mappingsExact = mappingsExact &&
            !VulkanRenderer::MapSurfaceFormatToVkFormatEXT(-1, invalidMapping) &&
            invalidMapping == VK_FORMAT_UNDEFINED &&
            !VulkanRenderer::MapSurfaceFormatToVkFormatEXT(27, invalidMapping) &&
            invalidMapping == VK_FORMAT_UNDEFINED;
        Check(mappingsExact, "B all 27 SurfaceFormat values have exact VkFormat mappings",
              firstMappingError.empty() ? "invalid ordinals also refuse" : firstMappingError);

        const VkPhysicalDevice physical = renderer->GetPhysicalDeviceHandleEXT();
        const VkFormat renderTargetFormat = renderer->GetRenderTargetVkFormatEXT();
        bool masksExact = physical != VK_NULL_HANDLE && renderTargetFormat != VK_FORMAT_UNDEFINED;
        bool storageImagePathObserved = false;
        bool bcNeedsEnabledFeature = true;
        std::string firstMaskError;

        for (const auto& format : kFormats)
        {
            const int ordinal = static_cast<int>(format.surface);
            const CNA::RendererFormatSupport support =
                device.GetRendererSurfaceFormatSupportEXT(format.surface);
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical, format.vulkan, &properties);

            VulkanRenderer::VulkanSurfaceFormatStorageEXT storage{};
            const bool allocationMapped =
                renderer->MapSurfaceFormatToStorageEXT(ordinal, storage) &&
                storage.format == format.vulkan;
            VkImageFormatProperties textureProperties{};
            constexpr VkImageUsageFlags textureUsage =
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const bool nativeTexture =
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    textureUsage, 0, &textureProperties) == VK_SUCCESS;
            constexpr VkFormatFeatureFlags requiredTextureFeatures =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            const bool expectedStorage = allocationMapped && nativeTexture &&
                (properties.optimalTilingFeatures & requiredTextureFeatures) ==
                    requiredTextureFeatures;
            VulkanRenderer::VulkanSurfaceFormatStorageEXT storageImageStorage{};
            std::uint32_t storageImageSpirvFormat = 0;
            const bool storageImageMapped =
                VulkanRenderer::MapStorageImageFormatToStorageEXT(
                    ordinal, storageImageStorage, storageImageSpirvFormat) &&
                storageImageStorage.format == format.vulkan;
            VkImageFormatProperties storageImageProperties{};
            const bool expectedStorageImage = storageImageMapped &&
                (!VulkanRenderer::StorageImageFormatRequiresExtendedFeatureEXT(
                     storageImageSpirvFormat) ||
                 renderer->GetEnabledDeviceFeaturesEXT()
                         .shaderStorageImageExtendedFormats == VK_TRUE) &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0 &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT, 0, &storageImageProperties) == VK_SUCCESS;
            VkImageFormatProperties storageSampledProperties{};
            const bool expectedStorageSampled = expectedStorageImage &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0,
                    &storageSampledProperties) == VK_SUCCESS;
            VkImageFormatProperties storageTransferSourceProperties{};
            const bool expectedStorageTransferSource = expectedStorageImage &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0 &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0,
                    &storageTransferSourceProperties) == VK_SUCCESS;
            VkImageFormatProperties storageTransferDestinationProperties{};
            const bool expectedStorageTransferDestination = expectedStorageImage &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0 &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0,
                    &storageTransferDestinationProperties) == VK_SUCCESS;
            const bool expectedRenderTarget =
                renderer->ClassifyRenderTargetFormatEXT(ordinal) ==
                    RendererFormatVerdict::Supported;
            VulkanRenderer::VulkanSurfaceFormatStorageEXT renderTargetStorage{};
            const bool renderTargetMapped = renderer->MapRenderTargetFormatToStorageEXT(
                ordinal, renderTargetStorage);
            VkFormatProperties renderTargetProperties{};
            if (renderTargetMapped)
                vkGetPhysicalDeviceFormatProperties(
                    physical, renderTargetStorage.format, &renderTargetProperties);
            const bool expectedBlend = expectedRenderTarget &&
                (renderTargetProperties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
            VkImageFormatProperties transferSourceProperties{};
            constexpr VkImageUsageFlags transferSourceUsage =
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            const bool nativeTextureTransferSource = expectedStorage &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0 &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, format.vulkan, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                    transferSourceUsage, 0, &transferSourceProperties) == VK_SUCCESS;
            const bool expectedTransferSource = nativeTextureTransferSource ||
                expectedStorageTransferSource ||
                (expectedRenderTarget &&
                 (renderTargetProperties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0);
            VkImageFormatProperties renderTargetImageProperties{};
            constexpr VkImageUsageFlags renderTargetUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const bool nativeRenderTargetImage = expectedRenderTarget && renderTargetMapped &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, renderTargetStorage.format, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL, renderTargetUsage, 0,
                    &renderTargetImageProperties) == VK_SUCCESS;
            const bool expectedMips =
                (expectedStorage && textureProperties.maxMipLevels > 1) ||
                (expectedStorageImage && storageImageProperties.maxMipLevels > 1) ||
                (nativeRenderTargetImage && renderTargetImageProperties.maxMipLevels > 1 &&
                 (renderTargetProperties.optimalTilingFeatures &
                  (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) ==
                     (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
            const bool expectedSampled = expectedStorage || expectedStorageSampled ||
                (expectedRenderTarget &&
                 (renderTargetProperties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0);
            const bool expectedFilter =
                ((expectedStorage || expectedStorageSampled) &&
                 (properties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0) ||
                (expectedRenderTarget &&
                 (renderTargetProperties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0);
            VkImageFormatProperties multisampleProperties{};
            constexpr VkImageUsageFlags multisampleUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
            const bool nativeMultisample = expectedRenderTarget && renderTargetMapped &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, renderTargetStorage.format, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL, multisampleUsage, 0,
                    &multisampleProperties) == VK_SUCCESS &&
                (multisampleProperties.sampleCounts &
                 renderer->GetDeviceLimitsEXT().framebufferColorSampleCounts &
                 ~VK_SAMPLE_COUNT_1_BIT) != 0;
            const bool expectedColorTransfer =
                format.surface == SurfaceFormat::Color && expectedStorage;

            const bool oneExact = support.knownUsages == kAllUsageBits &&
                (support.supportedUsages & ~support.knownUsages) == 0 &&
                support.Supports(CNA::RendererFormatUsage::TextureStorage) ==
                    (expectedStorage || expectedStorageImage) &&
                support.Supports(CNA::RendererFormatUsage::Sampled) == expectedSampled &&
                support.Supports(CNA::RendererFormatUsage::Filterable) == expectedFilter &&
                support.Supports(CNA::RendererFormatUsage::RenderTarget) == expectedRenderTarget &&
                support.Supports(CNA::RendererFormatUsage::Blendable) == expectedBlend &&
                support.Supports(CNA::RendererFormatUsage::StorageRead) ==
                    expectedStorageImage &&
                support.Supports(CNA::RendererFormatUsage::StorageWrite) ==
                    expectedStorageImage &&
                !support.Supports(CNA::RendererFormatUsage::StorageAtomic) &&
                support.Supports(CNA::RendererFormatUsage::TransferSource) ==
                    expectedTransferSource &&
                support.Supports(CNA::RendererFormatUsage::TransferDestination) ==
                    (expectedStorage || expectedStorageTransferDestination) &&
                support.Supports(CNA::RendererFormatUsage::Mipmapped) == expectedMips &&
                support.Supports(CNA::RendererFormatUsage::Multisample) ==
                    nativeMultisample &&
                support.Supports(CNA::RendererFormatUsage::ColorTransfer) ==
                    expectedColorTransfer;
            if (!oneExact && firstMaskError.empty())
                firstMaskError = format.name;
            masksExact = masksExact && oneExact;

            storageImagePathObserved = storageImagePathObserved || expectedStorageImage;
            if ((format.surface == SurfaceFormat::Dxt1 ||
                 format.surface == SurfaceFormat::Dxt3 ||
                 format.surface == SurfaceFormat::Dxt5) && expectedStorage)
                bcNeedsEnabledFeature = bcNeedsEnabledFeature &&
                    renderer->GetEnabledDeviceFeaturesEXT().textureCompressionBC == VK_TRUE;
        }
        const auto invalidSupport = renderer->GetSurfaceFormatUsageSupportEXT(27);
        masksExact = masksExact && invalidSupport.knownUsages == 0 &&
            invalidSupport.supportedUsages == 0;
        Check(masksExact, "C detailed usage masks equal raw format/image facts plus CNA paths",
              firstMaskError.empty() ? "all formats; invalid ordinal remains unknown"
                                     : "first mismatch: " + firstMaskError);
        Check(storageImagePathObserved,
              "D storage-image support is advertised only for an exact CNA/SPIR-V path");
        Check(bcNeedsEnabledFeature,
              "E compressed storage is never advertised without the enabled BC feature");
        Check(renderer->GetEnabledDeviceFeaturesEXT().shaderStorageImageExtendedFormats ==
                  renderer->GetSupportedDeviceFeaturesEXT().shaderStorageImageExtendedFormats,
              "E1 storage-image extended-format guarantee is enabled exactly when supported");

#ifdef CNA_CNAEXT
        bool storageConstructorsAgree = true;
        int storageCreated = 0;
        int storageRefused = 0;
        std::string firstStorageConstructionError;
        using CNA::Graphics::StorageTexture2D;
        using CNA::Graphics::StorageTexture2DDescriptor;
        using CNA::Graphics::StorageTexture2DUsage;
        constexpr auto storageUsage =
            StorageTexture2DUsage::StorageRead | StorageTexture2DUsage::StorageWrite;
        for (const auto& format : kFormats)
        {
            const auto support = device.GetRendererSurfaceFormatSupportEXT(format.surface);
            const bool advertised =
                support.Supports(CNA::RendererFormatUsage::StorageRead) &&
                support.Supports(CNA::RendererFormatUsage::StorageWrite);
            bool publicCreated = false;
            try
            {
                auto texture = std::make_unique<StorageTexture2D>(
                    device, StorageTexture2DDescriptor(
                        3, 2, 1, format.surface, storageUsage));
                publicCreated = true;
            }
            catch (const std::exception& error)
            {
                if (advertised && firstStorageConstructionError.empty())
                    firstStorageConstructionError =
                        std::string(format.name) + " public: " + error.what();
            }
            std::unique_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer> native;
            try
            {
                native = renderer->CreateStorageTexture2DEXT(
                    3, 2, 1, static_cast<int>(format.surface), UINT32_C(3));
            }
            catch (const std::exception& error)
            {
                if (advertised && firstStorageConstructionError.empty())
                    firstStorageConstructionError =
                        std::string(format.name) + " native: " + error.what();
            }
            const auto* concrete =
                dynamic_cast<const VulkanStorageTexture2DRenderer*>(native.get());
            const bool nativeExact = concrete != nullptr &&
                concrete->GetVkFormatEXT() == format.vulkan &&
                concrete->GetVkImageEXT() != VK_NULL_HANDLE;
            const bool oneAgrees = advertised
                ? publicCreated && nativeExact
                : !publicCreated && native == nullptr;
            if (!oneAgrees && firstStorageConstructionError.empty())
                firstStorageConstructionError = format.name;
            storageConstructorsAgree = storageConstructorsAgree && oneAgrees;
            if (publicCreated) ++storageCreated; else ++storageRefused;
        }
        Check(storageConstructorsAgree && storageCreated > 1 && storageRefused > 0,
              "E2 every advertised storage-image format constructs with exact native identity",
              firstStorageConstructionError.empty()
                  ? std::to_string(storageCreated) + " created, " +
                        std::to_string(storageRefused) + " refused"
                  : firstStorageConstructionError);
#endif

        // MOD-2224: query truth is only useful if the matching public constructor behaves the
        // same way. Exercise every format at a deliberately odd size so the test also catches
        // accidental even/power-of-two assumptions in image allocation and mip calculation.
        constexpr int contractWidth = 7;
        constexpr int contractHeight = 5;
        constexpr int expectedMipLevels = 3; // 7x5 -> 3x2 -> 1x1
        bool baseConstructionAgrees = true;
        bool mipConstructionAgrees = true;
        bool multisampleConstructionAgrees = true;
        int baseSupported = 0;
        int baseRefused = 0;
        int mipSupported = 0;
        int mipRefused = 0;
        int multisampleSupported = 0;
        int multisampleRefused = 0;
        std::string firstConstructionError;

        const auto RecordConstructionError = [&](const FormatCase& format,
                                                  const char* path,
                                                  const std::string& reason)
        {
            if (firstConstructionError.empty())
                firstConstructionError = std::string(format.name) + " " + path + ": " + reason;
        };

        for (const auto& format : kFormats)
        {
            const CNA::RendererFormatSupport support =
                device.GetRendererSurfaceFormatSupportEXT(format.surface);
            VulkanRenderer::VulkanSurfaceFormatStorageEXT renderTargetStorage{};
            const bool hasRenderTargetMapping =
                renderer->MapRenderTargetFormatToStorageEXT(
                    static_cast<int>(format.surface), renderTargetStorage);
            const bool renderTargetAdvertised =
                support.Supports(CNA::RendererFormatUsage::RenderTarget);
            const bool publicPredicate =
                device.SupportsSurfaceFormatAsRenderTargetEXT(format.surface);
            bool baseReturned = false;
            bool baseIdentityExact = false;
            try
            {
                RenderTarget2D target(
                    device, contractWidth, contractHeight, false, format.surface,
                    DepthFormat::None);
                baseReturned = true;
                auto* native = dynamic_cast<VulkanRenderTargetRenderer*>(
                    target.GetRenderTargetRenderer());
                baseIdentityExact = native != nullptr &&
                    target.getWidthProperty() == contractWidth &&
                    target.getHeightProperty() == contractHeight &&
                    target.getFormatProperty() == format.surface &&
                    target.getLevelCountProperty() == 1 &&
                    native->GetSurfaceFormatEXT() == static_cast<int>(format.surface) &&
                    hasRenderTargetMapping &&
                    native->GetVkFormatEXT() == renderTargetStorage.format;
                if (!baseIdentityExact)
                    RecordConstructionError(format, "base", "constructed with the wrong identity");
            }
            catch (const std::exception& error)
            {
                if (renderTargetAdvertised)
                    RecordConstructionError(format, "base", error.what());
            }
            catch (...)
            {
                if (renderTargetAdvertised)
                    RecordConstructionError(format, "base", "unknown exception");
            }
            if (!renderTargetAdvertised && baseReturned)
                RecordConstructionError(format, "base", "unexpectedly constructed");
            if (baseReturned) ++baseSupported; else ++baseRefused;
            baseConstructionAgrees = baseConstructionAgrees &&
                renderTargetAdvertised == publicPredicate &&
                (renderTargetAdvertised
                    ? baseReturned && baseIdentityExact
                    : !baseReturned);

            const bool mipAdvertised = renderTargetAdvertised &&
                support.Supports(CNA::RendererFormatUsage::Mipmapped);
            bool mipReturned = false;
            bool mipIdentityExact = false;
            try
            {
                RenderTarget2D target(
                    device, contractWidth, contractHeight, true, format.surface,
                    DepthFormat::None);
                mipReturned = true;
                auto* native = dynamic_cast<VulkanRenderTargetRenderer*>(
                    target.GetRenderTargetRenderer());
                mipIdentityExact = native != nullptr &&
                    target.getWidthProperty() == contractWidth &&
                    target.getHeightProperty() == contractHeight &&
                    target.getFormatProperty() == format.surface &&
                    target.getLevelCountProperty() == expectedMipLevels &&
                    native->GetSurfaceFormatEXT() == static_cast<int>(format.surface) &&
                    hasRenderTargetMapping &&
                    native->GetVkFormatEXT() == renderTargetStorage.format;
                if (!mipIdentityExact)
                    RecordConstructionError(format, "mip", "constructed with the wrong identity");
            }
            catch (const std::exception& error)
            {
                if (mipAdvertised)
                    RecordConstructionError(format, "mip", error.what());
            }
            catch (...)
            {
                if (mipAdvertised)
                    RecordConstructionError(format, "mip", "unknown exception");
            }
            if (!mipAdvertised && mipReturned)
                RecordConstructionError(format, "mip", "unexpectedly constructed");
            if (mipReturned) ++mipSupported; else ++mipRefused;
            mipConstructionAgrees = mipConstructionAgrees &&
                (mipAdvertised
                    ? mipReturned && mipIdentityExact
                    : !mipReturned);

            VkImageFormatProperties multisampleProperties{};
            VkSampleCountFlags availableSamples = 0;
            if (hasRenderTargetMapping &&
                vkGetPhysicalDeviceImageFormatProperties(
                    physical, renderTargetStorage.format, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    0, &multisampleProperties) == VK_SUCCESS)
            {
                availableSamples = multisampleProperties.sampleCounts &
                    renderer->GetDeviceLimitsEXT().framebufferColorSampleCounts;
            }
            const int highestSamples = HighestMultiSampleCount(availableSamples);
            const bool multisampleAdvertised = renderTargetAdvertised &&
                support.Supports(CNA::RendererFormatUsage::Multisample);
            bool multisampleReturned = false;
            bool multisampleIdentityExact = false;
            try
            {
                RenderTarget2D target(
                    device, contractWidth, contractHeight, false, format.surface,
                    DepthFormat::None,
                    multisampleAdvertised ? highestSamples : 2);
                multisampleReturned = true;
                auto* native = dynamic_cast<VulkanRenderTargetRenderer*>(
                    target.GetRenderTargetRenderer());
                multisampleIdentityExact = native != nullptr && highestSamples > 1 &&
                    target.getMultiSampleCountProperty() == highestSamples &&
                    native->GetColorSampleCountEXT() ==
                        static_cast<VkSampleCountFlagBits>(highestSamples) &&
                    hasRenderTargetMapping &&
                    native->GetVkFormatEXT() == renderTargetStorage.format;
                if (!multisampleIdentityExact)
                    RecordConstructionError(
                        format, "MSAA", "constructed with the wrong format/sample count");
            }
            catch (const std::exception& error)
            {
                if (multisampleAdvertised)
                    RecordConstructionError(format, "MSAA", error.what());
            }
            catch (...)
            {
                if (multisampleAdvertised)
                    RecordConstructionError(format, "MSAA", "unknown exception");
            }
            if (!multisampleAdvertised && multisampleReturned)
                RecordConstructionError(format, "MSAA", "unexpectedly constructed");
            if (multisampleReturned) ++multisampleSupported; else ++multisampleRefused;
            multisampleConstructionAgrees = multisampleConstructionAgrees &&
                ((highestSamples > 1) == multisampleAdvertised) &&
                (multisampleAdvertised
                    ? multisampleReturned && multisampleIdentityExact
                    : !multisampleReturned);
        }

        Check(baseConstructionAgrees && baseSupported > 0 && baseRefused > 0,
              "F capability snapshot agrees with all odd-sized base target constructors",
              firstConstructionError.empty()
                  ? std::to_string(baseSupported) + " created, " +
                        std::to_string(baseRefused) + " refused"
                  : firstConstructionError);
        Check(mipConstructionAgrees && mipSupported > 0 && mipRefused > 0,
              "G mip capability agrees with complete 7x5 chains for every format",
              firstConstructionError.empty()
                  ? std::to_string(mipSupported) + " created, " +
                        std::to_string(mipRefused) + " refused"
                  : firstConstructionError);
        Check(multisampleConstructionAgrees && multisampleSupported > 0 &&
                  multisampleRefused > 0,
              "H MSAA capability agrees with exact per-format sample construction",
              firstConstructionError.empty()
                  ? std::to_string(multisampleSupported) + " created, " +
                        std::to_string(multisampleRefused) + " refused"
                  : firstConstructionError);

        const auto& limits = renderer->GetDeviceLimitsEXT();
        const bool compute = renderer->SupportsComputeShadersEXT();
        const std::uint64_t expectedStorageBytes = compute ? limits.maxStorageBufferRange : 0;
        const std::uint64_t expectedStorageAlignment =
            compute ? limits.minStorageBufferOffsetAlignment : 0;
        const std::uint64_t expectedComputeBindings = compute
            ? std::min(limits.maxPerStageDescriptorStorageBuffers,
                       limits.maxDescriptorSetStorageBuffers)
            : 0;
        const std::uint64_t expectedStorageImages = compute
            ? std::min(limits.maxPerStageDescriptorStorageImages,
                       limits.maxDescriptorSetStorageImages)
            : 0;
        constexpr std::uint32_t implementedSampledBindings = 15;
        constexpr std::uint64_t largestImplementedUniformBinding =
            UINT64_C(72) * UINT64_C(16) * sizeof(float);
        const std::uint64_t expectedSampled = std::min({
            implementedSampledBindings,
            limits.maxPerStageDescriptorSamplers,
            limits.maxPerStageDescriptorSampledImages,
            limits.maxDescriptorSetSamplers,
            limits.maxDescriptorSetSampledImages});

        const auto EqualLimit = [&](const CNA::RendererLimit name,
                                    const std::uint64_t expected)
        {
            const CNA::RendererLimitValue actual = device.GetRendererLimitEXT(name);
            return actual.known && actual.value == expected;
        };
        VkImageFormatProperties arrayProperties{};
        constexpr VkImageUsageFlags arrayUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
        const std::uint64_t expectedArrayLayers =
            vkGetPhysicalDeviceImageFormatProperties(
                physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, arrayUsage, 0, &arrayProperties) == VK_SUCCESS
                ? arrayProperties.maxArrayLayers : 0;
        const std::uint64_t expectedTimestampPicoseconds =
            renderer->GetGraphicsQueueTimestampValidBitsEXT() != 0 &&
                    limits.timestampPeriod > 0.0f
                ? std::max<std::uint64_t>(
                      1, static_cast<std::uint64_t>(
                             std::llround(static_cast<long double>(limits.timestampPeriod) *
                                          1000.0L)))
                : 0;
        const bool limitsExact =
            EqualLimit(CNA::RendererLimit::MaxTextureDimension,
                       ClampToInt(limits.maxImageDimension2D)) &&
            EqualLimit(CNA::RendererLimit::MaxVertexStreams,
                       std::min(UINT32_C(16), limits.maxVertexInputBindings)) &&
            EqualLimit(CNA::RendererLimit::MaxStorageBufferBytes, expectedStorageBytes) &&
            EqualLimit(CNA::RendererLimit::MaxUniformBufferBytes,
                       std::min<std::uint64_t>(largestImplementedUniformBinding,
                                               limits.maxUniformBufferRange)) &&
            EqualLimit(CNA::RendererLimit::MaxComputeStorageBufferBindings,
                       expectedComputeBindings) &&
            EqualLimit(CNA::RendererLimit::MaxTextureArrayLayers, expectedArrayLayers) &&
            EqualLimit(CNA::RendererLimit::MaxSampledTexturesPerShaderStage,
                       expectedSampled) &&
            EqualLimit(CNA::RendererLimit::MaxStorageImagesPerShaderStage,
                       expectedStorageImages) &&
            EqualLimit(CNA::RendererLimit::MaxVertexInputBindings,
                       std::min(UINT32_C(16), limits.maxVertexInputBindings)) &&
            EqualLimit(CNA::RendererLimit::MaxVertexInputAttributes,
                       ClampToInt(limits.maxVertexInputAttributes)) &&
            EqualLimit(CNA::RendererLimit::MaxColorAttachments,
                       std::min(UINT32_C(4), limits.maxColorAttachments)) &&
            EqualLimit(CNA::RendererLimit::MinStorageBufferOffsetAlignment,
                       expectedStorageAlignment) &&
            EqualLimit(CNA::RendererLimit::MinUniformBufferOffsetAlignment,
                       limits.minUniformBufferOffsetAlignment) &&
            EqualLimit(CNA::RendererLimit::TimestampPeriodPicoseconds,
                       expectedTimestampPicoseconds);
        Check(limitsExact, "I every new published limit equals this physical-device snapshot");

        Check(expectedArrayLayers > 0 &&
                  device.GetRendererLimitEXT(
                      CNA::RendererLimit::MaxTextureArrayLayers).value == expectedArrayLayers &&
                  device.GetRendererLimitEXT(
                      CNA::RendererLimit::MaxStorageImagesPerShaderStage).value ==
                          expectedStorageImages &&
                  device.GetRendererLimitEXT(
                      CNA::RendererLimit::TimestampPeriodPicoseconds).value ==
                          expectedTimestampPicoseconds,
              "J array, storage-image and timestamp limits publish implemented paths only");

        Check(renderer->GetValidationMessagesEXT().empty(),
              "K capability queries and constructor contracts emit no Vulkan validation message",
              renderer->GetValidationMessagesEXT().empty()
                  ? "none"
                  : renderer->GetValidationMessagesEXT().front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanFormatLimitQueriesTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanFormatLimitQueriesTest test;
    test.Run();
    return test.Result();
}
