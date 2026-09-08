// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2222: detailed Vulkan format and limit answers must come from the
// selected physical device and must be intersected with resource paths CNA actually implements.

#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::RendererFormatVerdict;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    struct FormatCase
    {
        SurfaceFormat surface;
        VkFormat vulkan;
        const char* name;
    };

    constexpr std::array<FormatCase, 27> kFormats{{
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
            if (!VulkanRenderer::MapSurfaceFormatToVkFormatEXT(
                    static_cast<int>(format.surface), mapped) || mapped != format.vulkan)
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
        bool storageImageNativeButRefused = false;
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
            const bool expectedTransferSource = expectedRenderTarget &&
                (renderTargetProperties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
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
                (nativeRenderTargetImage && renderTargetImageProperties.maxMipLevels > 1 &&
                 (renderTargetProperties.optimalTilingFeatures &
                  (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) ==
                     (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
            const bool expectedSampled = expectedStorage ||
                (expectedRenderTarget &&
                 (renderTargetProperties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0);
            const bool expectedFilter =
                (expectedStorage &&
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
                support.Supports(CNA::RendererFormatUsage::TextureStorage) == expectedStorage &&
                support.Supports(CNA::RendererFormatUsage::Sampled) == expectedSampled &&
                support.Supports(CNA::RendererFormatUsage::Filterable) == expectedFilter &&
                support.Supports(CNA::RendererFormatUsage::RenderTarget) == expectedRenderTarget &&
                support.Supports(CNA::RendererFormatUsage::Blendable) == expectedBlend &&
                !support.Supports(CNA::RendererFormatUsage::StorageRead) &&
                !support.Supports(CNA::RendererFormatUsage::StorageWrite) &&
                !support.Supports(CNA::RendererFormatUsage::StorageAtomic) &&
                support.Supports(CNA::RendererFormatUsage::TransferSource) ==
                    expectedTransferSource &&
                support.Supports(CNA::RendererFormatUsage::TransferDestination) ==
                    expectedStorage &&
                support.Supports(CNA::RendererFormatUsage::Mipmapped) == expectedMips &&
                support.Supports(CNA::RendererFormatUsage::Multisample) ==
                    nativeMultisample &&
                support.Supports(CNA::RendererFormatUsage::ColorTransfer) ==
                    expectedColorTransfer;
            if (!oneExact && firstMaskError.empty())
                firstMaskError = format.name;
            masksExact = masksExact && oneExact;

            storageImageNativeButRefused = storageImageNativeButRefused ||
                ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0 &&
                 !support.Supports(CNA::RendererFormatUsage::StorageRead) &&
                 !support.Supports(CNA::RendererFormatUsage::StorageWrite));
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
        Check(storageImageNativeButRefused,
              "D native-only storage-image support is not advertised as a CNA path");
        Check(bcNeedsEnabledFeature,
              "E compressed storage is never advertised without the enabled BC feature");

        const auto& limits = renderer->GetDeviceLimitsEXT();
        const bool compute = renderer->SupportsComputeShadersEXT();
        const std::uint64_t expectedStorageBytes = compute ? limits.maxStorageBufferRange : 0;
        const std::uint64_t expectedStorageAlignment =
            compute ? limits.minStorageBufferOffsetAlignment : 0;
        const std::uint64_t expectedComputeBindings = compute
            ? std::min(limits.maxPerStageDescriptorStorageBuffers,
                       limits.maxDescriptorSetStorageBuffers)
            : 0;
        constexpr std::uint32_t implementedSampledBindings = 12;
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
            EqualLimit(CNA::RendererLimit::MaxTextureArrayLayers, 0) &&
            EqualLimit(CNA::RendererLimit::MaxSampledTexturesPerShaderStage,
                       expectedSampled) &&
            EqualLimit(CNA::RendererLimit::MaxStorageImagesPerShaderStage, 0) &&
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
            EqualLimit(CNA::RendererLimit::TimestampPeriodPicoseconds, 0);
        Check(limitsExact, "F every new published limit equals this physical-device snapshot");

        const bool nativeOnlyLimitsExist = limits.maxImageArrayLayers > 0 &&
            limits.maxPerStageDescriptorStorageImages > 0 &&
            renderer->GetPhysicalDevicePropertiesEXT().limits.timestampPeriod > 0.0F;
        Check(nativeOnlyLimitsExist &&
                  device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers).value == 0 &&
                  device.GetRendererLimitEXT(
                      CNA::RendererLimit::MaxStorageImagesPerShaderStage).value == 0 &&
                  device.GetRendererLimitEXT(
                      CNA::RendererLimit::TimestampPeriodPicoseconds).value == 0,
              "G native array/storage-image/timestamp facts stay zero until CNA implements them");

        Check(renderer->GetValidationMessagesEXT().empty(),
              "H capability queries emit no Vulkan validation message",
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
