#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Vulkan/shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

namespace CNA::Internal::Backends::Vulkan
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;

    static const char* const kValidationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    static const char* const kDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

// Validation is desired in debug builds but requires the Khronos layer.
// Checked at runtime in CreateInstance(); flipped to false if unavailable.
#ifdef NDEBUG
    static bool sEnableValidation = false;
#else
    static bool sEnableValidation = true;
#endif

    // =========================================================================
    // Helpers
    // =========================================================================

    // Task 878/879: numeric sample count corresponding to a VkSampleCountFlagBits, for reporting
    // IRenderTargetBackend::GetMultiSampleCount()'s real applied value.
    static int SampleCountToInt(VkSampleCountFlagBits s)
    {
        switch (s) {
            case VK_SAMPLE_COUNT_2_BIT:  return 2;
            case VK_SAMPLE_COUNT_4_BIT:  return 4;
            case VK_SAMPLE_COUNT_8_BIT:  return 8;
            case VK_SAMPLE_COUNT_16_BIT: return 16;
            case VK_SAMPLE_COUNT_32_BIT: return 32;
            case VK_SAMPLE_COUNT_64_BIT: return 64;
            default:                     return 0;
        }
    }

    // Task 911: maps a Microsoft::Xna::Framework::Graphics::DepthFormat to a real, distinct
    // VkFormat per render target instance (mirrors EasyGL's/Bgfx's MapDepthFormat, but with a
    // device-capability fallback chain since Vulkan -- unlike GL/bgfx -- doesn't guarantee every
    // one of these formats is actually supported for DEPTH_STENCIL_ATTACHMENT usage on every
    // device). DepthFormat::None is handled by the caller (no VkFormat / no attachment at all --
    // this function is only called when a real depth buffer was actually requested).
    static VkFormat PickDepthFormat(VkPhysicalDevice pd, DepthFormat requested)
    {
        auto supports = [pd](VkFormat fmt) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(pd, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        };
        switch (requested) {
        case DepthFormat::Depth16:
            if (supports(VK_FORMAT_D16_UNORM)) return VK_FORMAT_D16_UNORM;
            break;
        case DepthFormat::Depth24:
            // X8_D24_UNORM_PACK32: real 24-bit depth, no stencil -- the closest Vulkan
            // equivalent to XNA's Depth24 (no-stencil) request.
            if (supports(VK_FORMAT_X8_D24_UNORM_PACK32)) return VK_FORMAT_X8_D24_UNORM_PACK32;
            break;
        case DepthFormat::Depth24Stencil8:
        case DepthFormat::None:
        default:
            break;
        }
        // Depth24Stencil8's primary candidates, and the fallback for Depth16/Depth24 when their
        // preferred no-stencil format isn't supported on this device (a combined format with
        // unused stencil bits is still a real, correct depth buffer for those two requests).
        for (VkFormat fmt : { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT }) {
            if (supports(fmt)) return fmt;
        }
        // Last resort: any real depth-capable format at all.
        for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM }) {
            if (supports(fmt)) return fmt;
        }
        throw std::runtime_error("Vulkan: no suitable depth format for requested DepthFormat");
    }

    static int VertexCountForPrimitives(PrimitiveType pt, int n)
    {
        switch (pt) {
        case PrimitiveType::TriangleList:  return n * 3;
        case PrimitiveType::TriangleStrip: return n + 2;
        case PrimitiveType::LineList:      return n * 2;
        case PrimitiveType::LineStrip:     return n + 1;
        }
        return 0;
    }

    static VkPrimitiveTopology ToVkTopology(PrimitiveType pt)
    {
        switch (pt) {
        case PrimitiveType::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveType::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    // =========================================================================
    // VulkanTextureBackend
    // =========================================================================

    VulkanTextureBackend::VulkanTextureBackend(const ImageData& data, VulkanGraphicsBackend* owner)
        : width_(data.width), height_(data.height),
          levelCount_(data.mipLevels > 0 ? data.mipLevels : 1), owner_(owner)
    {
        VkDevice dev = owner_->device_;

        // --- Staging buffer ---
        VkDeviceSize size = static_cast<VkDeviceSize>(data.width) * data.height * 4;
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem);

        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, data.pixels.data(), static_cast<size_t>(size));
        vkUnmapMemory(dev, stagingMem);

        // --- VkImage ---
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height), 1 };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (image) failed");
        vkBindImageMemory(dev, image_, memory_, 0);

        // Transition UNDEFINED → TRANSFER_DST_OPTIMAL, copy, → SHADER_READ_ONLY (level 0 only --
        // the shared TransitionImageLayout helper hardcodes a single-level range).
        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        owner_->CopyBufferToImage(stagingBuf, image_,
            static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height));
        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Clean up staging
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);

        // Task 925: levels 1..levelCount_-1 have no data yet (uploaded later via
        // UpdatePixelsLevel) but still need a defined layout -- mirrors
        // VulkanTexture3DBackend's own construction-time full-range barrier (Task 864).
        if (levelCount_ > 1)
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                                 static_cast<uint32_t>(levelCount_ - 1), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        // --- VkImageView ---
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = static_cast<uint32_t>(levelCount_);
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(dev, &viewInfo, nullptr, &imageView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (texture) failed");

        // --- Descriptor set ---
        VkDescriptorSetAllocateInfo dsInfo{};
        dsInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsInfo.descriptorPool     = owner_->descriptorPool_;
        dsInfo.descriptorSetCount = 1;
        dsInfo.pSetLayouts        = &owner_->descriptorSetLayout_;
        if (vkAllocateDescriptorSets(dev, &dsInfo, &descriptorSet_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateDescriptorSets (texture) failed");

        VkDescriptorImageInfo imgDescInfo{};
        imgDescInfo.sampler     = owner_->defaultSampler_;
        imgDescInfo.imageView   = imageView_;
        imgDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet_;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgDescInfo;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    }

    void VulkanTextureBackend::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        vkDeviceWaitIdle(owner_->device_);
        VkDevice dev = owner_->device_;
        if (descriptorSet_ != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(dev, owner_->descriptorPool_, 1, &descriptorSet_);
            descriptorSet_ = VK_NULL_HANDLE;
        }
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(dev, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(dev, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }

    void VulkanTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!owner_ || !owner_->device_ || !rgba) return;
        VkDevice dev = owner_->device_;
        VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * 4;

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem);

        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
        if (stride == width_ * 4)
        {
            std::memcpy(mapped, rgba, static_cast<std::size_t>(size));
        }
        else
        {
            for (int y = 0; y < height_; ++y)
                std::memcpy(static_cast<uint8_t*>(mapped) + y * width_ * 4,
                            rgba + y * stride, static_cast<std::size_t>(width_) * 4);
        }
        vkUnmapMemory(dev, stagingMem);

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        owner_->CopyBufferToImage(stagingBuf, image_,
            static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // Task 925: transitions exactly ONE mip level's layout -- the shared TransitionImageLayout
    // helper always barriers level 0 regardless of the level actually being copied (the same
    // imprecision VulkanTexture3DBackend::SetData already has, Task 864 -- confirmed via live
    // Vulkan validation-layer errors while building this test: reusing that helper for level>0
    // barriers level 0 while vkCmdCopyBufferToImage targets the real level, a genuine mismatch).
    void VulkanTextureBackend::TransitionLevelLayout(int level, VkImageLayout from, VkImageLayout to)
    {
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image_;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 1, 0, 1 };
        VkPipelineStageFlags srcStage, dstStage;
        if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else { // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        owner_->EndOneTimeCommands(cb);
    }

    // Task 925 (split from Task 867): real GPU upload for level>0, mirroring
    // VulkanTexture3DBackend::SetData's established staging-buffer pattern (Task 864) --
    // previously the shared IGraphicsBackend no-op default, silently discarding the caller's
    // mip-level data.
    void VulkanTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (!owner_ || !owner_->device_ || !rgba || level < 0 || level >= levelCount_) return;
        VkDevice dev = owner_->device_;
        VkDeviceSize size = static_cast<VkDeviceSize>(levelW) * levelH * 4;

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void* mapped = nullptr;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        std::memcpy(mapped, rgba, static_cast<std::size_t>(size));

        TransitionLevelLayout(level,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { 0, 0, 0 };
        region.imageExtent      = { static_cast<uint32_t>(levelW), static_cast<uint32_t>(levelH), 1 };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        owner_->EndOneTimeCommands(cb);

        TransitionLevelLayout(level,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    VulkanTextureBackend::~VulkanTextureBackend()
    {
        if (owner_) {
            auto& list = owner_->liveTextures_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    // =========================================================================
    // VulkanRenderTargetBackend
    // =========================================================================

    // Mirrors EasyGLGraphicsBackend.cpp's CalculateRenderTargetMipLevels / Texture2D.cpp's
    // CalculateMipLevels (Task 878).
    static int CalculateVulkanRTMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    VulkanRenderTargetBackend::VulkanRenderTargetBackend(int w, int h, int depthFormat,
                                                          bool preserveContents,
                                                          VulkanGraphicsBackend* owner,
                                                          int requestedMultiSampleCount,
                                                          bool mipMap)
        : width_(w), height_(h), preserveContents_(preserveContents), owner_(owner)
    {
        // Task 911: real per-instance DepthStencilFormat fidelity -- None means no depth
        // attachment at all; otherwise a real, distinct VkFormat picked for THIS instance,
        // independent of the backbuffer's own depthFormat_.
        const bool hasDepth = (static_cast<DepthFormat>(depthFormat) != DepthFormat::None);
        if (hasDepth)
            depthVkFormat_ = PickDepthFormat(owner_->physicalDevice_, static_cast<DepthFormat>(depthFormat));
        VkDevice dev = owner_->device_;
        const uint32_t uw = static_cast<uint32_t>(w);
        const uint32_t uh = static_cast<uint32_t>(h);
        levelCount_ = mipMap ? CalculateVulkanRTMipLevels(w, h) : 1;

        // Task 878/879: this RT engages real MSAA only if it was asked for AND the backend
        // itself was constructed with backbuffer MSAA enabled (sampleCount_ > 1) -- see the
        // "piggyback on the backend's own sampleCount_" scope decision in plan_graphics.md.
        // Reusing the backend's single already-lazily-created MSAA pipeline/render-pass
        // infrastructure avoids threading an independent numeric sample count through every
        // pipeline cache key. If the backend has no MSAA infrastructure at all, a RT-only MSAA
        // request honestly reports MultiSampleCount == 0 (via appliedMultiSampleCount_ staying
        // 0 below) rather than silently no-oping.
        const bool wantsMsaa = requestedMultiSampleCount > 0 &&
                               owner_->sampleCount_ > VK_SAMPLE_COUNT_1_BIT;

        // --- Color image (must use swapchainFormat_ for pipeline compatibility) ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { uw, uh, 1 };
        colorInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        colorInfo.arrayLayers   = 1;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC/DST (Task 878): needed by MaybeGenerateMips' vkCmdBlitImage cascade when
        // levelCount_ > 1; harmless to always request even for non-mipmapped RTs.
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        colorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &colorInfo, nullptr, &colorImage_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImage (color) failed");

        VkMemoryRequirements colorReq;
        vkGetImageMemoryRequirements(dev, colorImage_, &colorReq);
        VkMemoryAllocateInfo colorAlloc{};
        colorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        colorAlloc.allocationSize  = colorReq.size;
        colorAlloc.memoryTypeIndex = owner_->FindMemoryType(colorReq.memoryTypeBits,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &colorAlloc, nullptr, &colorMemory_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkAllocateMemory (color) failed");
        vkBindImageMemory(dev, colorImage_, colorMemory_, 0);

        VkImageViewCreateInfo colorView{};
        colorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorView.image    = colorImage_;
        colorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorView.format   = owner_->swapchainFormat_;
        colorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(dev, &colorView, nullptr, &colorView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImageView (color) failed");

        // Task 878: a second view over the *full* mip range, used only for sampling (the
        // framebuffer attachment above must stay mip-0-only). Identical range to colorView_
        // when levelCount_ == 1, so this is a no-op change for every pre-existing non-mipmapped
        // RT — sampling still only ever sees level 0.
        VkImageViewCreateInfo sampleView = colorView;
        sampleView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(levelCount_), 0, 1 };
        if (vkCreateImageView(dev, &sampleView, nullptr, &colorSampleView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImageView (color sample) failed");

        // --- Depth image (Task 911: only created when a real depth format was requested --
        // DepthFormat::None correctly gets no depth attachment at all now, matching EasyGL/Bgfx's
        // existing behavior. Promoted in-place to MSAA samples when this RT engages MSAA --
        // depthView_ is never sampled externally by anything in this codebase, so there is no
        // separate single-sample depth-resolve path to keep in sync, unlike colorImage_). ---
        if (hasDepth)
        {
            VkImageCreateInfo depthInfo{};
            depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthInfo.imageType     = VK_IMAGE_TYPE_2D;
            depthInfo.format        = depthVkFormat_;
            depthInfo.extent        = { uw, uh, 1 };
            depthInfo.mipLevels     = 1;
            depthInfo.arrayLayers   = 1;
            depthInfo.samples       = wantsMsaa ? owner_->sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
            depthInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            depthInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &depthInfo, nullptr, &depthImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImage (depth) failed");

            VkMemoryRequirements depthReq;
            vkGetImageMemoryRequirements(dev, depthImage_, &depthReq);
            VkMemoryAllocateInfo depthAlloc{};
            depthAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            depthAlloc.allocationSize  = depthReq.size;
            depthAlloc.memoryTypeIndex = owner_->FindMemoryType(depthReq.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &depthAlloc, nullptr, &depthMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkAllocateMemory (depth) failed");
            vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

            VkImageViewCreateInfo depthView{};
            depthView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            depthView.image    = depthImage_;
            depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthView.format   = depthVkFormat_;
            depthView.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &depthView, nullptr, &depthView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImageView (depth) failed");
        }

        if (wantsMsaa)
        {
            // --- MSAA color image: the actual attached render target. TRANSIENT_ATTACHMENT
            // only (never sampled directly) -- resolved automatically into colorImage_ at
            // vkCmdEndRenderPass via the render pass's pResolveAttachments mechanism, mirroring
            // VulkanGraphicsBackend::CreateMsaaColorResources' backbuffer counterpart exactly. ---
            VkImageCreateInfo msaaColorInfo{};
            msaaColorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            msaaColorInfo.imageType     = VK_IMAGE_TYPE_2D;
            msaaColorInfo.format        = owner_->swapchainFormat_;
            msaaColorInfo.extent        = { uw, uh, 1 };
            msaaColorInfo.mipLevels     = 1;
            msaaColorInfo.arrayLayers   = 1;
            msaaColorInfo.samples       = owner_->sampleCount_;
            msaaColorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            msaaColorInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            msaaColorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            msaaColorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &msaaColorInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImage (MSAA color) failed");

            VkMemoryRequirements msaaColorReq;
            vkGetImageMemoryRequirements(dev, msaaColorImage_, &msaaColorReq);
            VkMemoryAllocateInfo msaaColorAlloc{};
            msaaColorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            msaaColorAlloc.allocationSize  = msaaColorReq.size;
            msaaColorAlloc.memoryTypeIndex = owner_->FindMemoryType(msaaColorReq.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &msaaColorAlloc, nullptr, &msaaColorMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkAllocateMemory (MSAA color) failed");
            vkBindImageMemory(dev, msaaColorImage_, msaaColorMemory_, 0);

            VkImageViewCreateInfo msaaColorView{};
            msaaColorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            msaaColorView.image    = msaaColorImage_;
            msaaColorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            msaaColorView.format   = owner_->swapchainFormat_;
            msaaColorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &msaaColorView, nullptr, &msaaColorView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImageView (MSAA color) failed");
        }

        // --- Framebuffer (Task 911: render pass now selected/lazily-created per this
        // instance's own real depthVkFormat_, not a single backend-wide shared one). ---
        if (wantsMsaa)
        {
            // att0=MSAA color, att1=resolve (colorImage_/colorView_), att2=MSAA depth (if hasDepth)
            // -- mirrors GetOrCreateRTRenderPassMsaa()'s attachment order exactly.
            VkImageView fbAtts[] = { msaaColorView_, colorView_, depthView_ };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_);
            fbInfo.attachmentCount = hasDepth ? 3u : 2u;
            fbInfo.pAttachments    = fbAtts;
            fbInfo.width           = uw;
            fbInfo.height          = uh;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &msaaFramebuffer_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateFramebuffer (MSAA) failed");
            appliedMultiSampleCount_ = SampleCountToInt(owner_->sampleCount_);
        }
        else
        {
            VkImageView fbAtts[] = { colorView_, depthView_ };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            // Pipelines only ever need a reference render pass built against the discard
            // variant (see GetOrCreateRTRenderPass()'s own comment: discard/load differ only in
            // loadOp/initialLayout, which don't affect compatibility) -- but THIS framebuffer
            // must be built against whichever variant this RT instance actually uses.
            fbInfo.renderPass      = owner_->GetOrCreateRTRenderPass(depthVkFormat_, !preserveContents_);
            fbInfo.attachmentCount = hasDepth ? 2u : 1u;
            fbInfo.pAttachments    = fbAtts;
            fbInfo.width           = uw;
            fbInfo.height          = uh;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetBackend: vkCreateFramebuffer failed");
        }

        // Transition color image to SHADER_READ_ONLY_OPTIMAL (initial state before first RT use).
        // Task 878: covers *all* levelCount_ levels (not just level 0, unlike the shared
        // single-level TransitionImageLayout helper) since colorSampleView_ above already
        // exposes the full range to the descriptor below, which declares that whole range to be
        // in SHADER_READ_ONLY_OPTIMAL — levels 1..levelCount_-1 get real content the first time
        // this RT is rendered into and unbound (MaybeGenerateMips), same as EasyGL's own
        // eager-allocate-then-regenerate-on-unbind pattern (Task 336).
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = colorImage_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        // --- Descriptor set so the RT can be sampled as a texture ---
        VkDescriptorSetAllocateInfo dsInfo{};
        dsInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsInfo.descriptorPool     = owner_->descriptorPool_;
        dsInfo.descriptorSetCount = 1;
        dsInfo.pSetLayouts        = &owner_->descriptorSetLayout_;
        if (vkAllocateDescriptorSets(dev, &dsInfo, &descriptorSet_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkAllocateDescriptorSets failed");

        VkDescriptorImageInfo imgDesc{};
        imgDesc.sampler     = owner_->defaultSampler_;
        imgDesc.imageView   = colorSampleView_;
        imgDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet_;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgDesc;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        owner_->liveRenderTargets_.push_back(this);
    }

    void VulkanRenderTargetBackend::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        vkDeviceWaitIdle(owner_->device_);
        VkDevice dev = owner_->device_;
        if (descriptorSet_ != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(dev, owner_->descriptorPool_, 1, &descriptorSet_);
            descriptorSet_ = VK_NULL_HANDLE;
        }
        if (framebuffer_     != VK_NULL_HANDLE) { vkDestroyFramebuffer(dev, framebuffer_, nullptr);     framebuffer_     = VK_NULL_HANDLE; }
        if (msaaFramebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(dev, msaaFramebuffer_, nullptr); msaaFramebuffer_ = VK_NULL_HANDLE; }
        if (colorView_   != VK_NULL_HANDLE)  { vkDestroyImageView(dev, colorView_, nullptr);       colorView_   = VK_NULL_HANDLE; }
        if (colorSampleView_ != VK_NULL_HANDLE) { vkDestroyImageView(dev, colorSampleView_, nullptr); colorSampleView_ = VK_NULL_HANDLE; }
        if (colorImage_  != VK_NULL_HANDLE)  { vkDestroyImage(dev, colorImage_, nullptr);          colorImage_  = VK_NULL_HANDLE; }
        if (colorMemory_ != VK_NULL_HANDLE)  { vkFreeMemory(dev, colorMemory_, nullptr);           colorMemory_ = VK_NULL_HANDLE; }
        if (msaaColorView_   != VK_NULL_HANDLE) { vkDestroyImageView(dev, msaaColorView_, nullptr); msaaColorView_   = VK_NULL_HANDLE; }
        if (msaaColorImage_  != VK_NULL_HANDLE) { vkDestroyImage(dev, msaaColorImage_, nullptr);    msaaColorImage_  = VK_NULL_HANDLE; }
        if (msaaColorMemory_ != VK_NULL_HANDLE) { vkFreeMemory(dev, msaaColorMemory_, nullptr);     msaaColorMemory_ = VK_NULL_HANDLE; }
        if (depthView_   != VK_NULL_HANDLE)  { vkDestroyImageView(dev, depthView_, nullptr);       depthView_   = VK_NULL_HANDLE; }
        if (depthImage_  != VK_NULL_HANDLE)  { vkDestroyImage(dev, depthImage_, nullptr);          depthImage_  = VK_NULL_HANDLE; }
        if (depthMemory_ != VK_NULL_HANDLE)  { vkFreeMemory(dev, depthMemory_, nullptr);           depthMemory_ = VK_NULL_HANDLE; }
        appliedMultiSampleCount_ = 0;
    }

    VulkanRenderTargetBackend::~VulkanRenderTargetBackend()
    {
        if (owner_) {
            auto& list = owner_->liveRenderTargets_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
            if (owner_->currentRT_ == this) owner_->currentRT_ = nullptr;
        }
        ReleaseVulkanResources();
    }

    VkFramebuffer VulkanRenderTargetBackend::GetFramebuffer() const
    {
        return (msaaFramebuffer_ != VK_NULL_HANDLE) ? msaaFramebuffer_ : framebuffer_;
    }

    VkRenderPass VulkanRenderTargetBackend::GetRenderPass() const
    {
        if (!owner_) return VK_NULL_HANDLE;
        if (msaaFramebuffer_ != VK_NULL_HANDLE) return owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_);
        return owner_->GetOrCreateRTRenderPass(depthVkFormat_, !preserveContents_);
    }

    void VulkanRenderTargetBackend::BindAsRenderTarget()
    {
        if (owner_) owner_->currentRT_ = this;
    }

    void VulkanRenderTargetBackend::UnbindAsRenderTarget()
    {
        if (owner_ && owner_->currentRT_ == this) owner_->currentRT_ = nullptr;
    }

    // Task 878: regenerate the mip chain from level 0's just-rendered (and, if this RT engaged
    // MSAA, just-resolved) content via a vkCmdBlitImage cascade -- the Vulkan equivalent of
    // EasyGL's glGenerateMipmap-on-unbind (Task 336) / FNA3D's OPENGL_ResolveTarget. Called from
    // RecordCommandBuffer right after this RT's render pass ends, so level 0 is already in
    // SHADER_READ_ONLY_OPTIMAL (the RT render pass's finalLayout) when this runs.
    void VulkanRenderTargetBackend::MaybeGenerateMips(VkCommandBuffer cb)
    {
        if (levelCount_ <= 1) return;

        auto barrier = [&](uint32_t level, VkImageLayout oldL, VkImageLayout newL,
                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout           = oldL;
            b.newLayout           = newL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = colorImage_;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        barrier(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        int srcW = width_, srcH = height_;
        for (int level = 1; level < levelCount_; ++level) {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            barrier(static_cast<uint32_t>(level),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level - 1), 0, 1 };
            blit.srcOffsets[1]  = { srcW, srcH, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
            blit.dstOffsets[1]  = { dstW, dstH, 1 };
            vkCmdBlitImage(cb, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              colorImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              1, &blit, VK_FILTER_LINEAR);

            // level-1 is done being read from -- restore it to its steady-state layout.
            barrier(static_cast<uint32_t>(level - 1),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            if (level < levelCount_ - 1) {
                // Becomes the source for the next iteration.
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else {
                // Last level -- done, restore to steady-state layout directly.
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }

            srcW = dstW; srcH = dstH;
        }
    }

    // =========================================================================
    // VulkanSpriteBatchBackend
    // =========================================================================

    VulkanSpriteBatchBackend::VulkanSpriteBatchBackend(VulkanGraphicsBackend* backend)
        : backend_(backend) {}

    void VulkanSpriteBatchBackend::Begin()
    {
        if (active_) return;
        vertices_.clear();
        indices_.clear();
        draws_.clear();
        currentTexture_  = nullptr;
        batchFirstIndex_ = 0;
        activeRT_        = backend_->currentRT_;
        active_ = true;
    }

    void VulkanSpriteBatchBackend::FlushTexture()
    {
        if (!currentTexture_) return;
        uint32_t count = static_cast<uint32_t>(indices_.size()) - batchFirstIndex_;
        if (count == 0) return;
        // Task 665 fix: previously always used the texture's own pre-baked descriptor set
        // (currentTexture_->GetVkDescriptorSet(), built once at texture-load time with a fixed
        // default sampler), completely bypassing SetSamplerFilter/SetSamplerAddressMode. Apply
        // the pending SamplerState to slot 0 (Task 118's existing per-slot VkSampler cache) and
        // build a fresh descriptor set combining the texture's own image view with THAT sampler.
        backend_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        VkDescriptorSet ds = backend_->GetOrCreateTexSamplerDescSet(
            currentTexture_->GetVkImageView(), backend_->slotSamplers_[0]);
        draws_.push_back({ ds, batchFirstIndex_, count });
        batchFirstIndex_ = static_cast<uint32_t>(indices_.size());
    }

    void VulkanSpriteBatchBackend::End()
    {
        if (!active_) return;
        backend_->activeCustomEffect_ = nullptr;
        if (customEffect_) customEffect_->Apply(); // may set backend_->activeCustomEffect_
        customEffectBackend_ = backend_->activeCustomEffect_;
        FlushTexture();
        active_ = false;

        // Task 664 fix: move this cycle's geometry into its own independent, frame-lifetime
        // snapshot pushed onto backend_->activeBatches_ NOW (at End(), not Begin()), so a 2nd
        // Begin()/Draw()/End() cycle on this same object later in the same frame starts from
        // freshly-cleared vertices_/indices_/draws_ without ever touching (and destroying) this
        // cycle's already-completed data. Previously activeBatches_ stored a raw `this` pointer
        // pushed at Begin() time, so a 2nd Begin() call's vertices_.clear() wiped out the 1st
        // cycle's data in place before RecordCommandBuffer() ever harvested it at Present() —
        // only the last batch's draws ever survived to be recorded.
        if (!vertices_.empty() && !draws_.empty())
        {
            auto snapshot = std::make_unique<BatchSnapshot>();
            snapshot->vertices            = std::move(vertices_);
            snapshot->indices             = std::move(indices_);
            snapshot->draws               = std::move(draws_);
            snapshot->customEffectBackend = customEffectBackend_;
            backend_->activeBatches_.push_back({ std::move(snapshot), activeRT_ });
        }
    }

    void VulkanSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        float w = static_cast<float>(texture.GetWidth());
        float h = static_cast<float>(texture.GetHeight());
        Rectangle dest(static_cast<int>(x), static_cast<int>(y),
                        static_cast<int>(w), static_cast<int>(h));
        Rectangle src(0, 0, static_cast<int>(w), static_cast<int>(h));
        Draw(texture, dest, src, Color(255, 255, 255, 255));
    }

    void VulkanSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& dest, const Rectangle& src,
                                        const Color& color)
    {
        Draw(texture, dest, src, color, 0.f, Vector2(0.f, 0.f), SpriteEffects::None, 0.f);
    }

    void VulkanSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& dest, const Rectangle& src,
                                        const Color& color, float rotation,
                                        const Vector2& origin, SpriteEffects effects,
                                        float /*layerDepth*/)
    {
        if (!active_) throw std::runtime_error("Vulkan SpriteBatch: Draw called outside Begin/End");

        const auto* samplable = dynamic_cast<const IVulkanSamplable*>(&texture);
        if (!samplable)
            throw std::runtime_error("Vulkan SpriteBatch: texture is not IVulkanSamplable");
        if (currentTexture_ != nullptr && currentTexture_ != samplable)
            FlushTexture();
        currentTexture_ = samplable;

        float tw = static_cast<float>(texture.GetWidth());
        float th = static_cast<float>(texture.GetHeight());

        // Task 665 fix: no [0,1] clamp here — matches FNA, which divides straight through with
        // no clamping (SpriteBatch.cs). A sourceRectangle that extends past the texture bounds
        // intentionally produces UVs outside [0,1], letting the bound SamplerState's
        // TextureAddressMode (Wrap/Mirror/Clamp) govern edge sampling — the classic XNA
        // scrolling/tiling-background technique. Clamping here (the pre-fix behaviour) silently
        // defeated Wrap/Mirror addressing entirely, regardless of which sampler was bound.
        float u1 = (float)src.X / tw;
        float v1 = (float)src.Y / th;
        float u2 = (float)(src.X + src.Width)  / tw;
        float v2 = (float)(src.Y + src.Height) / th;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally))
            std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically))
            std::swap(v1, v2);

        float r = color.getRProperty() / 255.f;
        float g = color.getGProperty() / 255.f;
        float b = color.getBProperty() / 255.f;
        float a = color.getAProperty() / 255.f;

        float dx = (float)dest.X,  dy = (float)dest.Y;
        float dw = (float)dest.Width, dh = (float)dest.Height;
        float sw = (float)src.Width,  sh = (float)src.Height;
        float ox = origin.X,       oy = origin.Y;
        float sx = dw / sw,        sy = dh / sh;

        float p0x = (0.f - ox) * sx, p0y = (0.f - oy) * sy;
        float p1x = (sw  - ox) * sx, p1y = (0.f - oy) * sy;
        float p2x = (sw  - ox) * sx, p2y = (sh  - oy) * sy;
        float p3x = (0.f - ox) * sx, p3y = (sh  - oy) * sy;

        float cosR = std::cos(rotation), sinR = std::sin(rotation);
        auto rot = [&](float px, float py, float& rx, float& ry) {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rot(p0x, p0y, v0x, v0y);
        rot(p1x, p1y, v1x, v1y);
        rot(p2x, p2y, v2x, v2y);
        rot(p3x, p3y, v3x, v3y);

        auto base = static_cast<uint16_t>(vertices_.size());
        vertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        vertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        vertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        vertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        indices_.insert(indices_.end(),
            {static_cast<uint16_t>(base),
             static_cast<uint16_t>(base + 1),
             static_cast<uint16_t>(base + 2),
             static_cast<uint16_t>(base + 2),
             static_cast<uint16_t>(base + 3),
             static_cast<uint16_t>(base)});
    }

    // =========================================================================
    // VulkanVertexBufferBackend
    // =========================================================================

    VulkanVertexBufferBackend::VulkanVertexBufferBackend(int vertex_capacity,
                                                         VulkanGraphicsBackend* owner)
        : capacity_(vertex_capacity), owner_(owner)
    {
        // Pre-allocate for worst-case stride (e.g. VertexPositionColor = 16 bytes).
        // A 0-vertex capacity (e.g. an empty model part) must still produce a valid,
        // non-empty VkBuffer -- vkCreateBuffer/vkAllocateMemory with size 0 is invalid
        // per the Vulkan spec.
        VkDeviceSize size = std::max<VkDeviceSize>(1, static_cast<VkDeviceSize>(vertex_capacity) * 64);
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_, memory_, &mappedPtr_);
    }

    void VulkanVertexBufferBackend::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        vkDeviceWaitIdle(owner_->device_);
        VkDevice dev = owner_->device_;
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(dev, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }

    VulkanVertexBufferBackend::~VulkanVertexBufferBackend()
    {
        if (owner_) {
            auto& list = owner_->liveVertexBuffers_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    void VulkanVertexBufferBackend::SetData(const void* data, int vertex_count,
                                            std::size_t stride_in_bytes)
    {
        vertexCount_ = vertex_count;
        stride_      = stride_in_bytes;
        std::memcpy(mappedPtr_, data, vertex_count * stride_in_bytes);
    }

    // =========================================================================
    // VulkanIndexBufferBackend
    // =========================================================================

    VulkanIndexBufferBackend::VulkanIndexBufferBackend(int index_capacity, bool thirtyTwoBit,
                                                       VulkanGraphicsBackend* owner)
        : capacity_(index_capacity), thirtyTwoBit_(thirtyTwoBit), owner_(owner)
    {
        const std::size_t elemSize = thirtyTwoBit ? sizeof(uint32_t) : sizeof(uint16_t);
        // A 0-index capacity (e.g. an empty model part) must still produce a valid,
        // non-empty VkBuffer -- vkCreateBuffer/vkAllocateMemory with size 0 is invalid
        // per the Vulkan spec.
        VkDeviceSize size = std::max<VkDeviceSize>(1, static_cast<VkDeviceSize>(index_capacity) * elemSize);
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_, memory_, &mappedPtr_);
    }

    void VulkanIndexBufferBackend::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        vkDeviceWaitIdle(owner_->device_);
        VkDevice dev = owner_->device_;
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(dev, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }

    VulkanIndexBufferBackend::~VulkanIndexBufferBackend()
    {
        if (owner_) {
            auto& list = owner_->liveIndexBuffers_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    void VulkanIndexBufferBackend::SetData16(const void* data, int index_count)
    {
        indexCount_ = index_count;
        std::memcpy(mappedPtr_, data, static_cast<size_t>(index_count) * sizeof(uint16_t));
    }

    void VulkanIndexBufferBackend::SetData32(const void* data, int index_count)
    {
        indexCount_ = index_count;
        std::memcpy(mappedPtr_, data, static_cast<size_t>(index_count) * sizeof(uint32_t));
    }

    // =========================================================================
    // VulkanGraphicsBackend — construction
    // =========================================================================

    static VkSampleCountFlagBits PickSampleCount(VkPhysicalDevice pd, int requested)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        VkSampleCountFlags avail = props.limits.framebufferColorSampleCounts
                                 & props.limits.framebufferDepthSampleCounts;
        const VkSampleCountFlagBits candidates[] = {
            VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT,  VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_1_BIT
        };
        const int counts[] = { 64, 32, 16, 8, 4, 2, 1 };
        for (int i = 0; i < 7; ++i) {
            if (counts[i] <= requested && (avail & candidates[i]))
                return candidates[i];
        }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    VulkanGraphicsBackend::VulkanGraphicsBackend(SDL_Window* window, int multiSampleCount, int swapInterval)
        : window_(window)
        , swapInterval_(swapInterval)
    {
        if (!window_)
            throw std::runtime_error("VulkanGraphicsBackend: null window");

        CreateInstance();
        if (sEnableValidation) SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        sampleCount_ = PickSampleCount(physicalDevice_, multiSampleCount);
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT)
            SDL_Log("[Vulkan] MSAA: %d×", static_cast<int>(sampleCount_));
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
        CreateRenderPass();
        // Task 911: RT render passes are no longer eagerly created here -- they're now
        // depth-format-keyed and lazily created on first use via GetOrCreateRTRenderPass()/
        // GetOrCreateRTRenderPassMsaa() (see VulkanRenderTargetBackend/VulkanRenderTargetCubeBackend).
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) {
            CreateMsaaColorResources();
            CreateRenderPassMsaa();
        }
        CreateFramebuffers();
        CreateCommandPool();
        AllocateCommandBuffers();
        CreateSyncObjects();
        CreateSampler();
        CreateDescriptorSetLayout();
        CreateDescriptorPool();
        // Task 911: the 2D sprite pipeline(s) are now lazily created per real target depth format
        // on first use (see GetOrCreatePipeline2D()/GetOrCreatePipeline2DMsaa(), called from
        // drawSpritesFor) rather than eagerly here.
        CreateSpriteBuffers();
        initialized_ = true;
        SDL_Log("[Vulkan] Backend initialised");
    }

    // =========================================================================
    // VulkanGraphicsBackend — destruction
    // =========================================================================

    VulkanGraphicsBackend::~VulkanGraphicsBackend()
    {
        if (device_ == VK_NULL_HANDLE) {
            if (surface_  != VK_NULL_HANDLE) { SDL_Vulkan_DestroySurface(instance_, surface_, nullptr); surface_  = VK_NULL_HANDLE; }
            if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr);                   instance_ = VK_NULL_HANDLE; }
            return;
        }

        // Step 1: wait for all in-flight GPU work to complete.
        vkDeviceWaitIdle(device_);

        // Step 1b: destroy the MRT proxy (if any) while device_ is still valid -- mrtProxy_'s own
        // destructor guards on owner_->device_ != VK_NULL_HANDLE, so it must run before Step 8
        // destroys the device, not via automatic member destruction after this body returns
        // (which runs too late and would leak its framebuffer/depth image/view/memory, Task 911).
        mrtProxy_.reset();

        // Step 2: destroy buffers and memory.
        // Externally-owned render targets, vertex/index buffers (C++ objects may outlive this destructor).
        for (auto* rt : liveRenderTargets_) { rt->ReleaseVulkanResources(); rt->DisconnectOwner(); }
        liveRenderTargets_.clear();
        for (auto* vb : liveVertexBuffers_) { vb->ReleaseVulkanResources(); vb->DisconnectOwner(); }
        liveVertexBuffers_.clear();
        for (auto* ib : liveIndexBuffers_)  { ib->ReleaseVulkanResources(); ib->DisconnectOwner(); }
        liveIndexBuffers_.clear();
        // Backend-owned sprite buffers + per-frame 3D buffers.
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (spriteVB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, spriteVB_[i], nullptr);    spriteVB_[i]    = VK_NULL_HANDLE; }
            if (spriteVBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, spriteVBMem_[i], nullptr);    spriteVBMem_[i] = VK_NULL_HANDLE; }
            if (spriteIB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, spriteIB_[i], nullptr);    spriteIB_[i]    = VK_NULL_HANDLE; }
            if (spriteIBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, spriteIBMem_[i], nullptr);    spriteIBMem_[i] = VK_NULL_HANDLE; }
            if (frame3DVB_[i]       != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DVB_[i], nullptr);        frame3DVB_[i]       = VK_NULL_HANDLE; }
            if (frame3DVBMem_[i]    != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DVBMem_[i], nullptr);        frame3DVBMem_[i]    = VK_NULL_HANDLE; }
            if (frame3DIB_[i]       != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DIB_[i], nullptr);        frame3DIB_[i]       = VK_NULL_HANDLE; }
            if (frame3DIBMem_[i]    != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DIBMem_[i], nullptr);        frame3DIBMem_[i]    = VK_NULL_HANDLE; }
            if (frame3DInstVB_[i]   != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DInstVB_[i], nullptr);    frame3DInstVB_[i]   = VK_NULL_HANDLE; }
            if (frame3DInstVBMem_[i]!= VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DInstVBMem_[i], nullptr);   frame3DInstVBMem_[i]= VK_NULL_HANDLE; }
        }

        // Step 3: destroy image views, images, and memory.
        // Externally-owned textures.
        for (auto* tex : liveTextures_) { tex->ReleaseVulkanResources(); tex->DisconnectOwner(); }
        liveTextures_.clear();
        // MSAA color buffer.
        CleanupMsaaColorResources();
        // Depth buffer.
        if (depthImageView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, depthImageView_, nullptr); depthImageView_ = VK_NULL_HANDLE; }
        if (depthImage_     != VK_NULL_HANDLE) { vkDestroyImage(device_, depthImage_, nullptr);         depthImage_     = VK_NULL_HANDLE; }
        if (depthMemory_    != VK_NULL_HANDLE) { vkFreeMemory(device_, depthMemory_, nullptr);           depthMemory_    = VK_NULL_HANDLE; }

        // Step 4: destroy descriptor resources.
        if (descriptorPool_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);           descriptorPool_      = VK_NULL_HANDLE; }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr); descriptorSetLayout_ = VK_NULL_HANDLE; }
        texSamplerDescSets_.clear(); // descriptor sets freed with pool above
        for (auto& [k, s] : samplerCache_)
            if (s != VK_NULL_HANDLE) vkDestroySampler(device_, s, nullptr);
        samplerCache_.clear();
        if (defaultSampler_      != VK_NULL_HANDLE) { vkDestroySampler(device_, defaultSampler_, nullptr);                  defaultSampler_      = VK_NULL_HANDLE; }

        // Step 5: destroy pipelines, render pass, and framebuffers.
        for (auto& [key, pipe] : pipelines3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelines3D_.clear();
        for (auto& [k, pipe] : pipelinesAlphaTest3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesAlphaTest3D_.clear();
        for (auto& [k, pipe] : pipelinesDualTex3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesDualTex3D_.clear();
        for (auto& cache : dualTexDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (dualTexFogUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, dualTexFogUBO_[i], nullptr);    dualTexFogUBO_[i]    = VK_NULL_HANDLE; }
            if (dualTexFogUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, dualTexFogUBOMem_[i], nullptr);   dualTexFogUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesEnvMap3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesEnvMap3D_.clear();
        for (auto& cache : envMapDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (envMapUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, envMapUBO_[i], nullptr);     envMapUBO_[i]    = VK_NULL_HANDLE; }
            if (envMapUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, envMapUBOMem_[i], nullptr);    envMapUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesLitTextured3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesLitTextured3D_.clear();
        for (auto& cache : litTexturedDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (litTexturedUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, litTexturedUBO_[i], nullptr);    litTexturedUBO_[i]    = VK_NULL_HANDLE; }
            if (litTexturedUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, litTexturedUBOMem_[i], nullptr);   litTexturedUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesFogColored3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesFogColored3D_.clear();
        for (auto& [k, pipe] : pipelinesFogTex3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesFogTex3D_.clear();
        for (auto& cache : fogTex3DDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (fogTex3DUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, fogTex3DUBO_[i], nullptr);    fogTex3DUBO_[i]    = VK_NULL_HANDLE; }
            if (fogTex3DUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, fogTex3DUBOMem_[i], nullptr);   fogTex3DUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesSkinned3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesSkinned3D_.clear();
        for (auto& [k, pipe] : pipelinesInstanced3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesInstanced3D_.clear();
        for (auto& cache : skinnedDescSets_) cache.clear();
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, skinnedUBO_[i], nullptr);    skinnedUBO_[i]    = VK_NULL_HANDLE; }
            if (skinnedUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, skinnedUBOMem_[i], nullptr);   skinnedUBOMem_[i] = VK_NULL_HANDLE; }
            if (skinnedFogUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, skinnedFogUBO_[i], nullptr);    skinnedFogUBO_[i]    = VK_NULL_HANDLE; }
            if (skinnedFogUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, skinnedFogUBOMem_[i], nullptr);   skinnedFogUBOMem_[i] = VK_NULL_HANDLE; }
        }
        if (defaultWhiteCubeView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteCubeView_, nullptr); defaultWhiteCubeView_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeImage_ != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteCubeImage_, nullptr);   defaultWhiteCubeImage_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeMem_  != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteCubeMem_, nullptr);       defaultWhiteCubeMem_  = VK_NULL_HANDLE; }
        // Default white texture (no free of descriptorSet — will be freed with the pool).
        if (defaultWhiteView_   != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteView_, nullptr);  defaultWhiteView_   = VK_NULL_HANDLE; }
        if (defaultWhiteImage_  != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteImage_, nullptr);     defaultWhiteImage_  = VK_NULL_HANDLE; }
        if (defaultWhiteMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteMemory_, nullptr);       defaultWhiteMemory_ = VK_NULL_HANDLE; }
        for (auto& [fmt, p] : pipelines2DMsaaByDepthFmt_) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
        pipelines2DMsaaByDepthFmt_.clear();
        for (auto& [fmt, p] : pipelines2DByDepthFmt_) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
        pipelines2DByDepthFmt_.clear();
        if (pipelineLayout3D_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout3D_, nullptr);       pipelineLayout3D_      = VK_NULL_HANDLE; }
        if (pipelineLayoutExt3D_        != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutExt3D_, nullptr);        pipelineLayoutExt3D_        = VK_NULL_HANDLE; }
        if (pipelineLayoutAlphaTest3D_  != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutAlphaTest3D_, nullptr);  pipelineLayoutAlphaTest3D_  = VK_NULL_HANDLE; }
        if (pipelineLayoutDualTex3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutDualTex3D_, nullptr);    pipelineLayoutDualTex3D_    = VK_NULL_HANDLE; }
        if (pipelineLayoutEnvMap3D_     != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutEnvMap3D_, nullptr);     pipelineLayoutEnvMap3D_     = VK_NULL_HANDLE; }
        if (descriptorPool2Tex_         != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPool2Tex_, nullptr);         descriptorPool2Tex_         = VK_NULL_HANDLE; }
        if (descriptorSetLayout2Tex_    != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayout2Tex_, nullptr); descriptorSetLayout2Tex_   = VK_NULL_HANDLE; }
        if (descriptorPoolEnvMap_       != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolEnvMap_, nullptr);       descriptorPoolEnvMap_       = VK_NULL_HANDLE; }
        if (descriptorSetLayoutEnvMap_  != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutEnvMap_, nullptr); descriptorSetLayoutEnvMap_ = VK_NULL_HANDLE; }
        if (pipelineLayoutSkinned3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutSkinned3D_, nullptr);    pipelineLayoutSkinned3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolSkinned_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolSkinned_, nullptr);      descriptorPoolSkinned_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutSkinned_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutSkinned_, nullptr); descriptorSetLayoutSkinned_ = VK_NULL_HANDLE; }
        if (pipelineLayoutLitTextured3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutLitTextured3D_, nullptr);    pipelineLayoutLitTextured3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolLitTextured_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolLitTextured_, nullptr);      descriptorPoolLitTextured_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutLitTextured_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutLitTextured_, nullptr); descriptorSetLayoutLitTextured_ = VK_NULL_HANDLE; }
        if (pipelineLayoutFogTex3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutFogTex3D_, nullptr);    pipelineLayoutFogTex3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolFogTex3D_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolFogTex3D_, nullptr);      descriptorPoolFogTex3D_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutFogTex3D_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutFogTex3D_, nullptr); descriptorSetLayoutFogTex3D_ = VK_NULL_HANDLE; }
        if (pipelineLayout2D_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout2D_, nullptr);       pipelineLayout2D_      = VK_NULL_HANDLE; }
        for (auto fb : swapchainFramebuffers_)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        if (renderPassMsaa_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPassMsaa_, nullptr); renderPassMsaa_ = VK_NULL_HANDLE; }
        // Task 911: RT render passes are now depth-format-keyed caches, not single members.
        for (auto& [fmt, rp] : rtRenderPassByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassByDepthFmt_.clear();
        for (auto& [fmt, rp] : rtRenderPassLoadByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassLoadByDepthFmt_.clear();
        for (auto& [fmt, rp] : rtRenderPassMsaaByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaByDepthFmt_.clear();
        if (renderPass_       != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPass_,       nullptr); renderPass_       = VK_NULL_HANDLE; }
        for (auto& [n, rp] : mrtRenderPasses_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        mrtRenderPasses_.clear();

        // Sync objects (semaphores and fences).
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (i < (int)imageAvailableSemaphores_.size() && imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
                imageAvailableSemaphores_[i] = VK_NULL_HANDLE;
            }
            if (i < (int)renderFinishedSemaphores_.size() && renderFinishedSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
                renderFinishedSemaphores_[i] = VK_NULL_HANDLE;
            }
            if (i < (int)inFlightFences_.size() && inFlightFences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, inFlightFences_[i], nullptr);
                inFlightFences_[i] = VK_NULL_HANDLE;
            }
        }

        // Deferred readback staging buffer.
        if (readbackStagingBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, readbackStagingBuf_, nullptr); readbackStagingBuf_ = VK_NULL_HANDLE; }
        if (readbackStagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, readbackStagingMem_, nullptr);    readbackStagingMem_ = VK_NULL_HANDLE; }

        // Step 6: destroy command pool (before swapchain).
        if (commandPool_ != VK_NULL_HANDLE) { vkDestroyCommandPool(device_, commandPool_, nullptr); commandPool_ = VK_NULL_HANDLE; }

        // Step 7: destroy swapchain resources.
        for (auto iv : swapchainImageViews_)
            if (iv != VK_NULL_HANDLE) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_ != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }

        // Step 8: destroy device last among Vulkan device-level objects.
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;

        if (sEnableValidation && debugMessenger_ != VK_NULL_HANDLE) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn) { fn(instance_, debugMessenger_, nullptr); debugMessenger_ = VK_NULL_HANDLE; }
        }
        if (surface_  != VK_NULL_HANDLE) { SDL_Vulkan_DestroySurface(instance_, surface_, nullptr); surface_  = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr);                   instance_ = VK_NULL_HANDLE; }
    }

    // =========================================================================
    // Instance / device / surface init
    // =========================================================================

    void VulkanGraphicsBackend::CreateInstance()
    {
        if (sEnableValidation) {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, nullptr);
            std::vector<VkLayerProperties> layers(n);
            vkEnumerateInstanceLayerProperties(&n, layers.data());
            for (const char* name : kValidationLayers) {
                bool found = false;
                for (const auto& l : layers) if (std::strcmp(l.layerName, name) == 0) { found = true; break; }
                if (!found) {
                    SDL_Log("[Vulkan] Validation layer '%s' not available — running without validation", name);
                    sEnableValidation = false;
                    break;
                }
            }
        }

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "CNA";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "CNA";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_1;

        uint32_t sdlN = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlN);
        std::vector<const char*> exts(sdlExts, sdlExts + sdlN);
        if (sEnableValidation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        if (sEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            ci.ppEnabledLayerNames = kValidationLayers;
        }
        if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
    }

    void VulkanGraphicsBackend::SetupDebugMessenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = DebugCallback;
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (!fn || fn(instance_, &info, nullptr, &debugMessenger_) != VK_SUCCESS)
            SDL_Log("[Vulkan] Warning: could not set up validation debug messenger");
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanGraphicsBackend::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* d, void*)
    {
        if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            SDL_Log("[Vulkan Validation] %s", d->pMessage);
        return VK_FALSE;
    }

    void VulkanGraphicsBackend::CreateSurface()
    {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }

    void VulkanGraphicsBackend::PickPhysicalDevice()
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance_, &n, nullptr);
        if (n == 0) throw std::runtime_error("Vulkan: no GPU found");
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance_, &n, devs.data());

        for (auto dev : devs) {
            uint32_t ec = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &ec, nullptr);
            std::vector<VkExtensionProperties> exts(ec);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &ec, exts.data());
            bool hasSwap = false;
            for (const auto& e : exts) if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { hasSwap = true; break; }
            if (!hasSwap) continue;

            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qps(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qn, qps.data());
            std::optional<uint32_t> gfx, pres;
            for (uint32_t i = 0; i < qn; ++i) {
                if (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfx = i;
                VkBool32 ps = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &ps);
                if (ps) pres = i;
                if (gfx && pres) break;
            }
            if (!gfx || !pres) continue;

            uint32_t fc = 0, mc = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fc, nullptr);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &mc, nullptr);
            if (fc == 0 || mc == 0) continue;

            physicalDevice_      = dev;
            graphicsQueueFamily_ = *gfx;
            presentQueueFamily_  = *pres;
            break;
        }
        if (physicalDevice_ == VK_NULL_HANDLE)
            throw std::runtime_error("Vulkan: no suitable GPU");

        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(physicalDevice_, &p);
        SDL_Log("[Vulkan] GPU: %s", p.deviceName);
    }

    void VulkanGraphicsBackend::CreateLogicalDevice()
    {
        std::set<uint32_t> families = { graphicsQueueFamily_, presentQueueFamily_ };
        float prio = 1.f;
        std::vector<VkDeviceQueueCreateInfo> qis;
        for (uint32_t f : families) {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = f;
            qi.queueCount = 1;
            qi.pQueuePriorities = &prio;
            qis.push_back(qi);
        }
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
        VkPhysicalDeviceFeatures feat{};
        if (supported.fillModeNonSolid) {
            feat.fillModeNonSolid     = VK_TRUE;
            fillModeNonSolidSupported_ = true;
        }
        if (supported.samplerAnisotropy) {
            feat.samplerAnisotropy = VK_TRUE;
            anisotropySupported_   = true;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &props);
            maxSamplerAnisotropy_ = props.limits.maxSamplerAnisotropy;
        }
        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = static_cast<uint32_t>(qis.size());
        ci.pQueueCreateInfos = qis.data();
        ci.pEnabledFeatures = &feat;
        ci.enabledExtensionCount = static_cast<uint32_t>(std::size(kDeviceExtensions));
        ci.ppEnabledExtensionNames = kDeviceExtensions;
        if (sEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            ci.ppEnabledLayerNames = kValidationLayers;
        }
        if (vkCreateDevice(physicalDevice_, &ci, nullptr, &device_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed");
        vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, presentQueueFamily_,  0, &presentQueue_);
        pfnCmdInsertDebugLabel_ = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(device_, "vkCmdInsertDebugUtilsLabelEXT"));

        // Task 456: one-time startup capability dump. This backend previously had NO startup log
        // at all (unlike EasyGL/Bgfx/SDL_Renderer, which all print something at initialization) --
        // a real, previously-undocumented gap on its own.
        {
            VkPhysicalDeviceProperties devProps{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &devProps);
            const int maxMsaa = SampleCountToInt(PickSampleCount(physicalDevice_, 64));
            std::cout << "CNA: Vulkan capabilities -- device=" << devProps.deviceName
                      << "; MSAA up to " << maxMsaa
                      << "x; MRT up to 4 targets (FNA MAX_RENDERTARGET_BINDINGS); "
                         "anisotropic filtering: "
                      << (anisotropySupported_
                              ? ("supported, max " + std::to_string(static_cast<int>(maxSamplerAnisotropy_)) + "x")
                              : std::string("NOT supported on this device"))
                      << "; wireframe fill mode: " << (fillModeNonSolidSupported_ ? "supported" : "NOT supported")
                      << "; SurfaceFormat: Color only (Task 176)" << std::endl;
        }
    }

    // =========================================================================
    // Swapchain
    // =========================================================================

    void VulkanGraphicsBackend::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

        uint32_t fn = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fn, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fn);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fn, fmts.data());
        // FNA's SurfaceFormat.Color (the default backbuffer format) is linear, not sRGB — a
        // separate SurfaceFormat.ColorSrgbEXT exists for the gamma-encoded variant. An SRGB
        // swapchain format would apply an automatic linear-to-sRGB encode to every presented
        // pixel regardless of content, which XNA/FNA does not do by default. Prefer UNORM.
        VkSurfaceFormatKHR fmt = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { fmt = f; break; }

        uint32_t mn = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, nullptr);
        std::vector<VkPresentModeKHR> modes(mn);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, modes.data());
        // Choose present mode based on swapInterval_:
        //   0 (Immediate) → prefer IMMEDIATE, fall back to MAILBOX, then FIFO
        //   2 (Two)       → prefer FIFO_RELAXED, fall back to FIFO
        //   1 (Default/One) → FIFO (guaranteed VSync, always available)
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (swapInterval_ == 0)
        {
            for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) { mode = m; break; }
            if (mode == VK_PRESENT_MODE_FIFO_KHR)
                for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { mode = m; break; }
        }
        else if (swapInterval_ == 2)
        {
            for (auto m : modes) if (m == VK_PRESENT_MODE_FIFO_RELAXED_KHR) { mode = m; break; }
        }

        VkExtent2D ext;
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            ext = caps.currentExtent;
        } else {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
            ext.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
            ext.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface_;
        ci.minImageCount = imgCount;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        uint32_t qfi[] = { graphicsQueueFamily_, presentQueueFamily_ };
        if (graphicsQueueFamily_ != presentQueueFamily_) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = qfi;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = mode;
        ci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");

        swapchainFormat_ = fmt.format;
        swapchainExtent_ = ext;
    }

    void VulkanGraphicsBackend::CreateImageViews()
    {
        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
        swapchainImages_.resize(n);
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapchainImages_.data());
        swapchainImageViews_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo ci{};
            ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image    = swapchainImages_[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format   = swapchainFormat_;
            ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(device_, &ci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateImageView failed");
        }
    }

    void VulkanGraphicsBackend::CreateRenderPass()
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format  = swapchainFormat_;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAtt{};
        depthAtt.format  = depthFormat_;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pDepthStencilAttachment = &depthRef;

        // Use the same two subpass dependencies as rtRenderPass_ so that pipelines
        // created against renderPass_ are also compatible with rtRenderPass_.
        VkSubpassDependency renderPassDeps[2]{};
        // Entry: wait for previous shader reads (RT-as-texture) AND the previous frame's
        // depth-buffer writes before this frame clears/tests depth. The depth image is
        // shared across frames, so without the depth scope the loadOp CLEAR can race a
        // prior frame's depth writes, intermittently corrupting depth-tested draws.
        renderPassDeps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[0].dstSubpass      = 0;
        renderPassDeps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        renderPassDeps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        renderPassDeps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        renderPassDeps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        renderPassDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes (and the finalLayout=PRESENT_SRC transition) visible to
        // subsequent fragment-shader reads (RT-as-texture) AND transfer reads (the deferred
        // GetBackBufferData copy reads the swapchain image at the TRANSFER stage).
        renderPassDeps[1].srcSubpass      = 0;
        renderPassDeps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        renderPassDeps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            VK_PIPELINE_STAGE_TRANSFER_BIT;
        renderPassDeps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassDeps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        renderPassDeps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses   = &sub;
        ci.dependencyCount = 2; ci.pDependencies = renderPassDeps;
        if (vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
    }

    VkRenderPass VulkanGraphicsBackend::GetOrCreateRTRenderPass(VkFormat depthFmt, bool discardContents)
    {
        auto& cache = discardContents ? rtRenderPassByDepthFmt_ : rtRenderPassLoadByDepthFmt_;
        auto it = cache.find(depthFmt);
        if (it != cache.end()) return it->second;

        const bool hasDepth = (depthFmt != VK_FORMAT_UNDEFINED);

        // Same as renderPass_ but color finalLayout = SHADER_READ_ONLY_OPTIMAL.
        // This makes the two passes compatible so pipelines can be reused across them.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // PreserveContents (LOAD_OP_LOAD): initialLayout = SHADER_READ_ONLY_OPTIMAL matches the
        // image state after construction (explicit transition) and after any previous RT render
        // pass (finalLayout = SHADER_READ_ONLY_OPTIMAL).
        colorAtt.initialLayout  = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFmt;
        depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        // Depth uses DONT_CARE/UNDEFINED even in the PreserveContents variant: the depth image
        // starts in UNDEFINED and its previous content is never needed across RT passes.
        depthAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // Entry: wait for the previous frame's texture sample AND the previous frame's
        // depth-buffer writes (this RT's own depth image) before this frame clears/tests depth.
        // Task 905: this must match renderPass_'s (CreateRenderPass()) dependencies exactly --
        // not just a "wait for shader reads" subset -- because GetOrCreatePipeline3D() (and
        // every other GetOrCreatePipelineXxx3D) may reuse a pipeline created against renderPass_
        // to draw into this render pass whenever their formats happen to coincide. Render-pass
        // "compatibility" (VUID-vkCmdDraw-renderPass-02684) requires matching subpass dependency
        // stage/access masks too, not just attachment descriptions/subpass shape -- confirmed via
        // live Vulkan validation errors from a depth-tested 3D draw into a non-MSAA RT before the
        // original Task 905 fix (see plan_graphics.md). The depth-related stage/access bits are
        // harmlessly over-broad (safe over-synchronization) for the hasDepth=false variant, kept
        // identical across both for exact byte-for-byte parity with every other render pass in
        // this backend rather than risk a subtly-incompatible narrower mask.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes visible to the fragment shader AND transfer reads (the
        // deferred GetBackBufferData copy path) in the next pass.
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = hasDepth ? 2u : 1u; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (RT) failed");
        cache[depthFmt] = rp;
        return rp;
    }

    VkRenderPass VulkanGraphicsBackend::GetOrCreateRTRenderPassMsaa(VkFormat depthFmt)
    {
        auto it = rtRenderPassMsaaByDepthFmt_.find(depthFmt);
        if (it != rtRenderPassMsaaByDepthFmt_.end()) return it->second;

        const bool hasDepth = (depthFmt != VK_FORMAT_UNDEFINED);

        // Task 878/879/911: 3-attachment MSAA RT render pass, shared by every MSAA-enabled
        // RenderTarget2D/RenderTargetCube requesting this same real depth format (or none).
        // Same attachment formats/sample-counts/subpass shape AND (see the deps[] comment below)
        // the same subpass dependency stage/access masks as CreateRenderPassMsaa()'s backbuffer
        // pass, so pipelines already created against renderPassMsaa_ remain render-pass-compatible
        // here whenever depthFmt happens to equal depthFormat_ -- but with the resolve
        // attachment's finalLayout = SHADER_READ_ONLY_OPTIMAL instead of PRESENT_SRC_KHR, since
        // this resolves into an RT's sampleable colorImage_, never presented (attachment
        // initial/final layouts don't affect pipeline compatibility). DiscardContents-shaped only
        // (LOAD_OP_CLEAR): PreserveContents + MSAA is not given its own LOAD_OP_LOAD variant here
        // (see VulkanRenderTargetBackend's constructor comment) -- an intentionally narrower scope
        // than the non-MSAA discard/load split.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = sampleCount_;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription resolveAtt{};
        resolveAtt.format         = swapchainFormat_;
        resolveAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolveAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFmt;
        depthAtt.samples        = sampleCount_;
        depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef   { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference resolveRef { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef   { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pResolveAttachments     = &resolveRef;
        sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // Task 878/879 empirical finding: Vulkan's validation layer treats a pipeline's
        // originally-bound render pass and the render pass it's actually recorded against as
        // "compatible" only if their subpass dependency stage/access masks match too, not just
        // attachment descriptions/subpass shape as the basic spec text on render-pass
        // compatibility implies (confirmed via VUID-vkCmdDraw-renderPass-02684 validation errors
        // when an earlier, narrower deps[] didn't exactly match renderPassMsaa_'s). Since
        // pipelines with msaa=true may be created against renderPassMsaa_ (see
        // GetOrCreatePipeline3D et al.) and reused here whenever depthFmt coincides, this render
        // pass's dependencies must exactly mirror CreateRenderPassMsaa()'s, including the
        // transfer-read scope that (semantically) only matters for the backbuffer's
        // GetBackBufferData path. Kept identical for hasDepth=false too, for the same
        // safe-over-synchronization reasoning as GetOrCreateRTRenderPass()'s comment.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, resolveAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = hasDepth ? 3u : 2u; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (RT MSAA) failed");
        rtRenderPassMsaaByDepthFmt_[depthFmt] = rp;
        return rp;
    }

    VkRenderPass VulkanGraphicsBackend::GetOrCreateMRTRenderPass(uint32_t colorAttachmentCount)
    {
        auto it = mrtRenderPasses_.find(colorAttachmentCount);
        if (it != mrtRenderPasses_.end()) return it->second;

        // N color attachments (all swapchainFormat_) + 1 depth.
        std::vector<VkAttachmentDescription> atts(colorAttachmentCount + 1);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            atts[i].format         = swapchainFormat_;
            atts[i].samples        = VK_SAMPLE_COUNT_1_BIT;
            atts[i].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            atts[i].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            atts[i].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            atts[i].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            atts[i].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        auto& depth = atts[colorAttachmentCount];
        depth.format         = depthFormat_;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::vector<VkAttachmentReference> colorRefs(colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
            colorRefs[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ colorAttachmentCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = colorAttachmentCount;
        sub.pColorAttachments       = colorRefs.data();
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency deps2[2]{};
        // Entry: wait for texture reads before writing.
        deps2[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps2[0].dstSubpass    = 0;
        deps2[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps2[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps2[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps2[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps2[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes visible to the fragment shader in the next pass.
        deps2[1].srcSubpass    = 0;
        deps2[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps2[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps2[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps2[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps2[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps2[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = static_cast<uint32_t>(atts.size());
        ci.pAttachments    = atts.data();
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps2;

        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (MRT) failed");

        mrtRenderPasses_[colorAttachmentCount] = rp;
        return rp;
    }

    // Task 911: shared render-pass-selection decision for every 3D pipeline creation function.
    // MRT stays out of Task 911's scope (always the device-wide depthFormat_, via
    // GetOrCreateMRTRenderPass -- XNA/FNA's own semantics for "which bound target's depth format
    // wins in a multi-target draw" are inherently ambiguous). For a single-target draw, reuse the
    // backbuffer's own renderPass_/renderPassMsaa_ whenever the target's real depth format matches
    // depthFormat_ -- the overwhelmingly common case (a backbuffer draw, or a RenderTarget2D/
    // RenderTargetCube constructed with the default DepthFormat) -- since they are already
    // guaranteed render-pass-compatible; this keeps pipeline reuse/cache-hit behavior identical to
    // before this task for that common case. Only a target with a genuinely different depth
    // format (Task 911's whole point) falls back to a render pass keyed by its own format.
    VkRenderPass VulkanGraphicsBackend::PickRTPipelineRenderPass(uint32_t colorAttachmentCount, bool msaa,
                                                                  VkFormat targetDepthFmt)
    {
        if (colorAttachmentCount > 1)
            return GetOrCreateMRTRenderPass(colorAttachmentCount);
        if (targetDepthFmt == depthFormat_)
            return (msaa && renderPassMsaa_) ? renderPassMsaa_ : renderPass_;
        return msaa ? GetOrCreateRTRenderPassMsaa(targetDepthFmt)
                    : GetOrCreateRTRenderPass(targetDepthFmt, true);
    }

    void VulkanGraphicsBackend::CreateFramebuffers()
    {
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        const bool msaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT);
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            VkFramebufferCreateInfo ci{};
            ci.sType  = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.width  = swapchainExtent_.width;
            ci.height = swapchainExtent_.height;
            ci.layers = 1;
            if (msaa) {
                // att 0 = MSAA color, att 1 = resolve (swapchain), att 2 = MSAA depth
                VkImageView atts[] = { msaaColorView_, swapchainImageViews_[i], depthImageView_ };
                ci.renderPass      = renderPassMsaa_;
                ci.attachmentCount = 3;
                ci.pAttachments    = atts;
            } else {
                VkImageView atts[] = { swapchainImageViews_[i], depthImageView_ };
                ci.renderPass      = renderPass_;
                ci.attachmentCount = 2;
                ci.pAttachments    = atts;
            }
            if (vkCreateFramebuffer(device_, &ci, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    void VulkanGraphicsBackend::CleanupSwapchain()
    {
        if (device_ == VK_NULL_HANDLE) return;
        for (auto fb : swapchainFramebuffers_) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        CleanupMsaaColorResources();
        for (auto iv : swapchainImageViews_) if (iv) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
        // NOTE: renderPass_/renderPassMsaa_ are NOT destroyed here; permanent for backend lifetime

        // Defensive: the swapchain (and therefore its real pixel extent) is about to be torn
        // down and rebuilt at a possibly-different size by RecreateSwapchain() -- a
        // previously game-set custom Viewport should not be trusted to still be meaningful
        // relative to the new extent until GraphicsDevice::UpdateViewportFromWindow() (which
        // runs on every Present()/Reset()/SetVirtualResolution() call, i.e. immediately after
        // every path that reaches this point) re-confirms or replaces it. Currently a no-op in
        // practice, since that re-confirmation already always happens on the very next frame
        // regardless -- kept as a belt-and-braces guard against a future code path that might
        // call CleanupSwapchain()/RecreateSwapchain() without an immediate follow-up refresh.
        viewportSet_ = false;
    }

    void VulkanGraphicsBackend::RecreateSwapchain()
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w == 0 || h == 0) return;
        vkDeviceWaitIdle(device_);
        CleanupSwapchain();
        CleanupDepthResources();
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) CreateMsaaColorResources();
        CreateFramebuffers();
    }

    // =========================================================================
    // Command pool / buffers / sync
    // =========================================================================

    void VulkanGraphicsBackend::CreateCommandPool()
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = graphicsQueueFamily_;
        if (vkCreateCommandPool(device_, &ci, nullptr, &commandPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed");
    }

    void VulkanGraphicsBackend::AllocateCommandBuffers()
    {
        commandBuffers_.resize(MaxFramesInFlight);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MaxFramesInFlight;
        if (vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    void VulkanGraphicsBackend::CreateSyncObjects()
    {
        imageAvailableSemaphores_.resize(MaxFramesInFlight);
        renderFinishedSemaphores_.resize(MaxFramesInFlight);
        inFlightFences_.resize(MaxFramesInFlight);
        VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MaxFramesInFlight; ++i)
            if (vkCreateSemaphore(device_, &si, nullptr, &imageAvailableSemaphores_[i]) ||
                vkCreateSemaphore(device_, &si, nullptr, &renderFinishedSemaphores_[i]) ||
                vkCreateFence(device_, &fi, nullptr, &inFlightFences_[i]))
                throw std::runtime_error("Vulkan: failed to create sync objects");
    }

    // =========================================================================
    // Sampler / Descriptor layout / Descriptor pool
    // =========================================================================

    void VulkanGraphicsBackend::CreateSampler()
    {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = VK_FILTER_LINEAR;
        ci.minFilter    = VK_FILTER_LINEAR;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.borderColor  = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        // Task 878: VkSamplerCreateInfo{} zero-inits maxLod to 0, which clamps every sample to
        // mip level 0 regardless of mipmapMode -- harmless for the single-level images this
        // sampler was exclusively used with until now, but would silently defeat real mip
        // chains (RenderTarget2D mipMap=true) once they exist. The actual visible range is
        // still bounded by each resource's own VkImageView levelCount, so this is a no-op for
        // every pre-existing single-level texture/RT.
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(device_, &ci, nullptr, &defaultSampler_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSampler failed");
        for (auto& s : slotSamplers_) s = defaultSampler_;
    }

    void VulkanGraphicsBackend::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &b;
        if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &descriptorSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    void VulkanGraphicsBackend::CreateDescriptorPool()
    {
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDescriptorSets };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = MaxDescriptorSets;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    // =========================================================================
    // Per-slot SamplerState (Task 118)
    // =========================================================================

    void VulkanGraphicsBackend::ApplySamplerState(int slot, int filter,
                                                   int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (slot < 0 || slot >= 16) return;

        SamplerStateKey key{ filter, addressU, addressV, maxAnisotropy };
        auto it = samplerCache_.find(key);
        if (it != samplerCache_.end()) {
            slotSamplers_[slot] = it->second;
            return;
        }

        // XNA TextureFilter int values:
        //  0=Linear, 1=Point, 2=Anisotropic, 3=LinearMipPoint, 4=PointMipLinear,
        //  5=MinLinearMagPointMipLinear, 6=MinLinearMagPointMipPoint,
        //  7=MinPointMagLinearMipLinear, 8=MinPointMagLinearMipPoint
        VkFilter magF, minF;
        VkSamplerMipmapMode mipMode;
        bool enableAniso = false;
        switch (filter) {
        case 1:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // Point
        case 2:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  enableAniso = true; break; // Anisotropic
        case 3:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // LinearMipPoint
        case 4:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // PointMipLinear
        case 5:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // MinLinearMagPointMipLinear
        case 6:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // MinLinearMagPointMipPoint
        case 7:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // MinPointMagLinearMipLinear
        case 8:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // MinPointMagLinearMipPoint
        default: magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // Linear (0)
        }

        // XNA TextureAddressMode int values: 0=Wrap, 1=Clamp, 2=Mirror
        auto toAddr = [](int a) -> VkSamplerAddressMode {
            switch (a) {
            case 0:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case 2:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; // Clamp (1)
            }
        };

        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = magF;
        ci.minFilter    = minF;
        ci.addressModeU = toAddr(addressU);
        ci.addressModeV = toAddr(addressV);
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.mipmapMode   = mipMode;
        ci.borderColor  = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        // Task 878: see CreateSampler()'s identical comment -- without this, every per-slot
        // sampler variant would silently clamp to mip level 0 too.
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        if (enableAniso && anisotropySupported_) {
            ci.anisotropyEnable = VK_TRUE;
            ci.maxAnisotropy    = std::min(static_cast<float>(std::max(1, maxAnisotropy)),
                                           maxSamplerAnisotropy_);
        }

        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device_, &ci, nullptr, &sampler) != VK_SUCCESS)
            return; // fallback: keep existing slot sampler
        samplerCache_[key]   = sampler;
        slotSamplers_[slot] = sampler;
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateTexSamplerDescSet(VkImageView view,
                                                                         VkSampler sampler)
    {
        if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
            return defaultWhiteDescSet_;

        auto key = std::make_pair(view, sampler);
        auto it  = texSamplerDescSets_.find(key);
        if (it != texSamplerDescSets_.end())
            return it->second;

        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorPool     = descriptorPool_;
        dsAI.descriptorSetCount = 1;
        dsAI.pSetLayouts        = &descriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &dsAI, &ds) != VK_SUCCESS)
            return defaultWhiteDescSet_;

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = sampler;
        imgInfo.imageView   = view;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = ds;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        texSamplerDescSets_[key] = ds;
        return ds;
    }

    // =========================================================================
    // VulkanEffectBackend (Task 119 — SPIR-V custom Effect)
    // =========================================================================

    VulkanEffectBackend::VulkanEffectBackend(VulkanGraphicsBackend* owner) : owner_(owner) {}

    VulkanEffectBackend::~VulkanEffectBackend()
    {
        if (!owner_ || !owner_->device_) return;
        vkDeviceWaitIdle(owner_->device_);
        if (pipeline_       != VK_NULL_HANDLE) { vkDestroyPipeline(owner_->device_, pipeline_, nullptr);       pipeline_       = VK_NULL_HANDLE; }
        if (pipelineLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(owner_->device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
        if (fragModule_     != VK_NULL_HANDLE) { vkDestroyShaderModule(owner_->device_, fragModule_, nullptr);  fragModule_     = VK_NULL_HANDLE; }
        if (vertModule_     != VK_NULL_HANDLE) { vkDestroyShaderModule(owner_->device_, vertModule_, nullptr);  vertModule_     = VK_NULL_HANDLE; }
        if (owner_->activeCustomEffect_ == this) owner_->activeCustomEffect_ = nullptr;
    }

    // vertSpv and fragSpv contain raw SPIR-V bytecode (must be 4-byte aligned size).
    // Push-constant contract (128 bytes, vert+frag stages):
    //   [0..7]    = vec2 vpSize  — set automatically by the sprite-batch runtime
    //   [8..71]   = mat4 uMatrix — SetUniformMat4(any name, ...)
    //   [72..87]  = vec4 uColor  — SetUniformVec4/Vec3/Vec2(any name, ...)
    //   [88..119] = 8 floats     — SetUniformFloat / SetUniformInt (slots 0–7)
    bool VulkanEffectBackend::CompileProgram(const std::string& vertSpv, const std::string& fragSpv)
    {
        compileError_.clear();
        if (vertSpv.size() % 4 != 0 || fragSpv.size() % 4 != 0) {
            compileError_ = "SPIR-V size must be a multiple of 4 bytes";
            return false;
        }

        VkShaderModuleCreateInfo mci{};
        mci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mci.codeSize = vertSpv.size();
        mci.pCode    = reinterpret_cast<const uint32_t*>(vertSpv.data());
        if (vkCreateShaderModule(owner_->device_, &mci, nullptr, &vertModule_) != VK_SUCCESS) {
            compileError_ = "Failed to create vertex shader module"; return false;
        }
        mci.codeSize = fragSpv.size();
        mci.pCode    = reinterpret_cast<const uint32_t*>(fragSpv.data());
        if (vkCreateShaderModule(owner_->device_, &mci, nullptr, &fragModule_) != VK_SUCCESS) {
            compileError_ = "Failed to create fragment shader module"; return false;
        }

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size   = 128;

        VkPipelineLayoutCreateInfo pli{};
        pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;
        pli.pSetLayouts            = &owner_->descriptorSetLayout_;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges    = &pcRange;
        if (vkCreatePipelineLayout(owner_->device_, &pli, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            compileError_ = "Failed to create pipeline layout"; return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vertModule_; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragModule_; stages[1].pName = "main";

        // Same vertex input as Sprite2DVertex: x,y | u,v | r,g,b,a (32 bytes)
        VkVertexInputBindingDescription bind{ 0, sizeof(Sprite2DVertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r) };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vport{ 0, 0, (float)owner_->swapchainExtent_.width,
                          (float)owner_->swapchainExtent_.height, 0, 1 };
        VkRect2D sci{ {0,0}, owner_->swapchainExtent_ };
        VkPipelineViewportStateCreateInfo vpState{};
        vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpState.viewportCount = 1; vpState.pViewports = &vport;
        vpState.scissorCount  = 1; vpState.pScissors  = &sci;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cba;

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;

        VkPipelineDepthStencilStateCreateInfo dsInfo{};
        dsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dsInfo.depthTestEnable  = VK_FALSE;
        dsInfo.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpState;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &dsInfo;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout_;
        pci.renderPass          = owner_->renderPass_;
        pci.subpass             = 0;

        if (vkCreateGraphicsPipelines(owner_->device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) {
            compileError_ = "Failed to create graphics pipeline"; return false;
        }
        return true;
    }

    void VulkanEffectBackend::Bind()
    {
        if (pipeline_ != VK_NULL_HANDLE)
            owner_->activeCustomEffect_ = this;
    }

    void VulkanEffectBackend::Unbind()
    {
        if (owner_->activeCustomEffect_ == this)
            owner_->activeCustomEffect_ = nullptr;
    }

    bool VulkanEffectBackend::IsValid() const { return pipeline_ != VK_NULL_HANDLE; }

    std::string VulkanEffectBackend::GetCompileError() const { return compileError_; }

    void VulkanEffectBackend::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        // uMatrix at byte offset 16 (GLSL pads vec2 to 16 before mat4): float[4..19]
        std::memcpy(pushConst_ + 4, matrix, 64);
    }

    void VulkanEffectBackend::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        // uColor at byte offset 80: float[20..23]
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void VulkanEffectBackend::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void VulkanEffectBackend::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void VulkanEffectBackend::SetUniformFloat(const char* /*name*/, float value)
    {
        // uFloat0 at byte offset 96: float[24]
        pushConst_[24] = value;
    }

    void VulkanEffectBackend::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    std::unique_ptr<IEffectBackend> VulkanGraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto backend = std::make_unique<VulkanEffectBackend>(this);
        if (!vertSrc.empty() && !fragSrc.empty())
            backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    // =========================================================================
    // Shader module helper
    // =========================================================================

    VkShaderModule VulkanGraphicsBackend::CreateShaderModule(const uint32_t* spv, size_t byteSize)
    {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = byteSize;
        ci.pCode    = spv;
        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &ci, nullptr, &mod) != VK_SUCCESS)
            throw std::runtime_error("vkCreateShaderModule failed");
        return mod;
    }

    // =========================================================================
    // 2D Sprite pipeline
    // =========================================================================

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipeline2D(VkFormat depthFmt)
    {
        auto cached = pipelines2DByDepthFmt_.find(depthFmt);
        if (cached != pipelines2DByDepthFmt_.end()) return cached->second;

        using namespace Shaders;

        VkShaderModule vert = CreateShaderModule(kSprite2dVertSpv, kSprite2dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kSprite2dFragSpv, kSprite2dFragSpv_size);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        // Sprite2DVertex: x,y | u,v | r,g,b,a (32 bytes)
        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Sprite2DVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r) };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
        VkRect2D sc{ {0,0}, swapchainExtent_ };
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.pViewports = &vp;
        vs.scissorCount  = 1; vs.pScissors  = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Alpha blending for sprites
        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cba;

        // Dynamic viewport/scissor/blend-constants so resize and state changes
        // don't require pipeline recreation.
        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;

        // Push constant: vec2 viewportSize (8 bytes) for NDC conversion
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0; pcRange.size = 8;

        if (pipelineLayout2D_ == VK_NULL_HANDLE) {
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount         = 1;   pli.pSetLayouts         = &descriptorSetLayout_;
            pli.pushConstantRangeCount = 1;   pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout2D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (2D) failed");
        }

        // Depth stencil: disabled for sprites, but required because render pass has depth attachment
        VkPipelineDepthStencilStateCreateInfo ds2d{};
        ds2d.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds2d.depthTestEnable  = VK_FALSE;
        ds2d.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds2d;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout2D_;
        // Task 911: render pass selected per the target's own real depth format.
        pci.renderPass          = PickRTPipelineRenderPass(1, false, depthFmt);
        pci.subpass             = 0;

        VkPipeline pipe = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (2D) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        pipelines2DByDepthFmt_[depthFmt] = pipe;
        return pipe;
    }

    // =========================================================================
    // Depth buffer resources
    // =========================================================================

    VkFormat VulkanGraphicsBackend::FindDepthFormat() const
    {
        // Task 870: stencil-capable formats must be preferred FIRST. Every backbuffer/RT depth
        // image on this backend shares this one device-wide format (real per-instance
        // DepthStencilFormat fidelity is RenderTarget2D/RenderTargetCube-only, see
        // VulkanRenderTargetBackend/PickDepthFormat -- Task 911), so if this pick has no stencil
        // aspect, DepthStencilState.StencilEnable can never actually work no matter how correctly
        // ApplyDepthStencilState/the pipeline-creation functions map it. VK_FORMAT_D32_SFLOAT has
        // mandatory support on essentially all real Vulkan hardware and used to be checked first,
        // meaning a stencil-capable format was almost never actually chosen.
        for (VkFormat fmt : { VK_FORMAT_D24_UNORM_S8_UINT,
                              VK_FORMAT_D32_SFLOAT_S8_UINT,
                              VK_FORMAT_D32_SFLOAT }) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return fmt;
        }
        throw std::runtime_error("Vulkan: no suitable depth format");
    }

    void VulkanGraphicsBackend::CreateDepthResources()
    {
        depthFormat_ = FindDepthFormat();

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = depthFormat_;
        imgInfo.extent        = { swapchainExtent_.width, swapchainExtent_.height, 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = sampleCount_;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &imgInfo, nullptr, &depthImage_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage (depth) failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, depthImage_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (depth) failed");
        vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = depthFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (depth) failed");
    }

    void VulkanGraphicsBackend::CleanupDepthResources()
    {
        if (depthImageView_) { vkDestroyImageView(device_, depthImageView_, nullptr); depthImageView_ = VK_NULL_HANDLE; }
        if (depthImage_)     { vkDestroyImage(device_, depthImage_, nullptr);         depthImage_     = VK_NULL_HANDLE; }
        if (depthMemory_)    { vkFreeMemory(device_, depthMemory_, nullptr);           depthMemory_    = VK_NULL_HANDLE; }
    }

    // =========================================================================
    // MSAA color buffer resources (recreated with swapchain)
    // =========================================================================

    void VulkanGraphicsBackend::CreateMsaaColorResources()
    {
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = swapchainFormat_;
        imgInfo.extent        = { swapchainExtent_.width, swapchainExtent_.height, 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = sampleCount_;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &imgInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage (MSAA color) failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, msaaColorImage_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &msaaColorMemory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (MSAA color) failed");
        vkBindImageMemory(device_, msaaColorImage_, msaaColorMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = msaaColorImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &msaaColorView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (MSAA color) failed");
    }

    void VulkanGraphicsBackend::CleanupMsaaColorResources()
    {
        if (msaaColorView_)   { vkDestroyImageView(device_, msaaColorView_, nullptr);   msaaColorView_   = VK_NULL_HANDLE; }
        if (msaaColorImage_)  { vkDestroyImage(device_, msaaColorImage_, nullptr);      msaaColorImage_  = VK_NULL_HANDLE; }
        if (msaaColorMemory_) { vkFreeMemory(device_, msaaColorMemory_, nullptr);       msaaColorMemory_ = VK_NULL_HANDLE; }
    }

    void VulkanGraphicsBackend::CreateRenderPassMsaa()
    {
        // att 0: MSAA color (rendered to, not stored — resolved to swapchain)
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = sampleCount_;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // att 1: resolve target = swapchain image (1 sample, stored, presented)
        VkAttachmentDescription resolveAtt{};
        resolveAtt.format         = swapchainFormat_;
        resolveAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolveAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // att 2: MSAA depth
        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFormat_;
        depthAtt.samples        = sampleCount_;
        depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef   { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference resolveRef { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef   { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pResolveAttachments     = &resolveRef;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency deps[2]{};
        // Entry: wait for previous shader reads (RT-as-texture) AND the previous frame's
        // depth-buffer writes (shared depth image) before clearing/testing depth this frame.
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: synchronize the resolve write + finalLayout=PRESENT_SRC transition against
        // both fragment-shader reads (RT-as-texture) and transfer reads (deferred readback copy).
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, resolveAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 3; ci.pAttachments  = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        if (vkCreateRenderPass(device_, &ci, nullptr, &renderPassMsaa_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (MSAA) failed");
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipeline2DMsaa(VkFormat depthFmt)
    {
        auto cached = pipelines2DMsaaByDepthFmt_.find(depthFmt);
        if (cached != pipelines2DMsaaByDepthFmt_.end()) return cached->second;

        using namespace Shaders;

        VkShaderModule vert = CreateShaderModule(kSprite2dVertSpv, kSprite2dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kSprite2dFragSpv, kSprite2dFragSpv_size);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag; stages[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Sprite2DVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r) };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
        VkRect2D   sc{ {0,0}, swapchainExtent_ };
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount  = 1; vs.pViewports = &vp;
        vs.scissorCount   = 1; vs.pScissors  = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = sampleCount_;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cba;

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;

        if (pipelineLayout2D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{};
            pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pcRange.offset = 0; pcRange.size = 8;
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount         = 1;   pli.pSetLayouts         = &descriptorSetLayout_;
            pli.pushConstantRangeCount = 1;   pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout2D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (2D) failed");
        }

        VkPipelineDepthStencilStateCreateInfo ds2d{};
        ds2d.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds2d.depthTestEnable  = VK_FALSE;
        ds2d.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds2d;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout2D_;
        // Task 911: render pass selected per the target's own real depth format.
        pci.renderPass          = PickRTPipelineRenderPass(1, true, depthFmt);
        pci.subpass             = 0;

        VkPipeline pipe = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (2D MSAA) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        pipelines2DMsaaByDepthFmt_[depthFmt] = pipe;
        return pipe;
    }

    // =========================================================================
    // 3D pipeline layout + per-variant pipeline (lazily created)
    // =========================================================================

    // XNA CompareFunction enum -> VkCompareOp (mirrors EasyGL's ToEasyGLCompareFunc exactly):
    // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7
    static VkCompareOp ToVkCompareOp(int xnaCompare)
    {
        switch (xnaCompare) {
        case 1: return VK_COMPARE_OP_NEVER;
        case 2: return VK_COMPARE_OP_LESS;
        case 3: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 4: return VK_COMPARE_OP_EQUAL;
        case 5: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 6: return VK_COMPARE_OP_GREATER;
        case 7: return VK_COMPARE_OP_NOT_EQUAL;
        default: return VK_COMPARE_OP_ALWAYS; // CompareFunction::Always = 0
        }
    }

    // XNA StencilOperation ordinals -> VkStencilOp (mirrors EasyGL's ToEasyGLStencilOp exactly):
    // Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4, IncrementSaturation=5,
    // DecrementSaturation=6, Invert=7
    static VkStencilOp ToVkStencilOp(int xnaOp)
    {
        switch (xnaOp) {
        case 1: return VK_STENCIL_OP_ZERO;
        case 2: return VK_STENCIL_OP_REPLACE;
        case 3: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 4: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case 5: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 6: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 7: return VK_STENCIL_OP_INVERT;
        default: return VK_STENCIL_OP_KEEP; // StencilOperation::Keep = 0
        }
    }

    // Task 868: XNA Blend enum -> VkBlendFactor (mirrors EasyGL's ToEasyGLBlendFactor exactly):
    // One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5,
    // DestinationColor=6, InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9,
    // BlendFactor=10, InverseBlendFactor=11, SourceAlphaSaturation=12
    static VkBlendFactor ToVkBlendFactor(int xnaBlend)
    {
        switch (xnaBlend) {
        case  1: return VK_BLEND_FACTOR_ZERO;
        case  2: return VK_BLEND_FACTOR_SRC_COLOR;
        case  3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case  4: return VK_BLEND_FACTOR_SRC_ALPHA;
        case  5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case  6: return VK_BLEND_FACTOR_DST_COLOR;
        case  7: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case  8: return VK_BLEND_FACTOR_DST_ALPHA;
        case  9: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 10: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 11: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 12: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE; // Blend::One = 0
        }
    }

    // Task 868: XNA BlendFunction enum -> VkBlendOp (mirrors EasyGL's ToEasyGLBlendEquation
    // exactly): Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4
    static VkBlendOp ToVkBlendOp(int xnaBlendFunc)
    {
        switch (xnaBlendFunc) {
        case 1: return VK_BLEND_OP_SUBTRACT;
        case 2: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 3: return VK_BLEND_OP_MAX;
        case 4: return VK_BLEND_OP_MIN;
        default: return VK_BLEND_OP_ADD; // BlendFunction::Add = 0
        }
    }

    // Task 868: packs BlendKeyParams into a uint32_t (4 bits per Blend value -- 13 real values fit;
    // 3 bits per BlendFunction value -- 5 real values fit), 22 bits total. This is the pipeline
    // cache key's own second half (see PipelineKey/PipelineKeyHash) -- a plain uint64_t ran out of
    // free bit-width for this once a depth VkFormat is already folded in via
    // FoldDepthFormatIntoKey. When blend is disabled, always collapses to 0 regardless of the
    // requested (irrelevant) factors, so different disabled BlendStates don't create duplicate
    // pipelines.
    static uint32_t PackBlendBits(bool blend, const BlendKeyParams& bp)
    {
        if (!blend) return 0;
        return (static_cast<uint32_t>(bp.colorSrc  & 0xF))
             | (static_cast<uint32_t>(bp.colorDst  & 0xF) << 4)
             | (static_cast<uint32_t>(bp.alphaSrc  & 0xF) << 8)
             | (static_cast<uint32_t>(bp.alphaDst  & 0xF) << 12)
             | (static_cast<uint32_t>(bp.colorFunc & 0x7) << 16)
             | (static_cast<uint32_t>(bp.alphaFunc & 0x7) << 19);
    }

    // Task 868: fills a VkPipelineColorBlendAttachmentState's real blend factors/op from
    // BlendKeyParams -- shared by every 3D pipeline-creation function so the exact same
    // XNA->Vulkan mapping is used everywhere, mirroring FillDepthStencilState's own established
    // pattern. Previously every call site hardcoded BlendState.NonPremultiplied's own equation
    // here whenever blend was true, regardless of what was actually requested.
    static void FillBlendAttachmentState(VkPipelineColorBlendAttachmentState& cba, bool blend,
                                          const BlendKeyParams& bp)
    {
        cba.blendEnable = blend ? VK_TRUE : VK_FALSE;
        if (blend) {
            cba.srcColorBlendFactor = ToVkBlendFactor(bp.colorSrc);
            cba.dstColorBlendFactor = ToVkBlendFactor(bp.colorDst);
            cba.colorBlendOp        = ToVkBlendOp(bp.colorFunc);
            cba.srcAlphaBlendFactor = ToVkBlendFactor(bp.alphaSrc);
            cba.dstAlphaBlendFactor = ToVkBlendFactor(bp.alphaDst);
            cba.alphaBlendOp        = ToVkBlendOp(bp.alphaFunc);
        }
    }

    // Packs every DepthStencilKeyParams field into 29 bits, meant to be OR'd (after shifting past
    // whatever bit range a caller's own existing key dimensions already occupy) into that
    // caller's uint64_t pipeline cache key.
    static uint64_t PackDepthStencilBits(const DepthStencilKeyParams& ds)
    {
        return  (static_cast<uint64_t>(ds.depthFunc & 7))
              | (ds.stencilEnable ? (1ull << 3) : 0ull)
              | (static_cast<uint64_t>(ds.stencilFunc & 7)         << 4)
              | (static_cast<uint64_t>(ds.stencilFail & 7)         << 7)
              | (static_cast<uint64_t>(ds.stencilDepthFail & 7)    << 10)
              | (static_cast<uint64_t>(ds.stencilPass & 7)         << 13)
              | (ds.twoSidedStencilMode ? (1ull << 16) : 0ull)
              | (static_cast<uint64_t>(ds.ccwStencilFunc & 7)      << 17)
              | (static_cast<uint64_t>(ds.ccwStencilFail & 7)      << 20)
              | (static_cast<uint64_t>(ds.ccwStencilDepthFail & 7) << 23)
              | (static_cast<uint64_t>(ds.ccwStencilPass & 7)      << 26);
    }

    // Fills in a VkPipelineDepthStencilStateCreateInfo's depthCompareOp and front/back
    // VkStencilOpState blocks from a DepthStencilKeyParams -- shared by every 3D
    // pipeline-creation function so the exact same XNA->Vulkan mapping is used everywhere.
    // stencilTestEnable's compare/write masks and the reference value are intentionally left
    // untouched here: they are true Vulkan dynamic state (vkCmdSetStencilCompareMask/WriteMask/
    // Reference), applied per-draw, not baked into the pipeline.
    static void FillDepthStencilState(VkPipelineDepthStencilStateCreateInfo& ds,
                                       const DepthStencilKeyParams& p)
    {
        ds.depthCompareOp     = ToVkCompareOp(p.depthFunc);
        ds.stencilTestEnable  = p.stencilEnable ? VK_TRUE : VK_FALSE;
        ds.front.failOp       = ToVkStencilOp(p.stencilFail);
        ds.front.passOp       = ToVkStencilOp(p.stencilPass);
        ds.front.depthFailOp  = ToVkStencilOp(p.stencilDepthFail);
        ds.front.compareOp    = ToVkCompareOp(p.stencilFunc);
        // XNA's TwoSidedStencilMode=false uses the SAME (front, clockwise) ops/func for both
        // faces (FNA's own real behavior: CCW fields are simply ignored when this is false, not
        // reset to any default) -- mirrors EasyGL's identical fallback-to-front pattern exactly.
        //
        // Task 870 empirical finding: with rs.frontFace = VK_FRONT_FACE_CLOCKWISE and this
        // backend's vertex shaders' Y-flip (pos.y = -pos.y, see colored3d.vert.glsl et al.),
        // VkPipelineDepthStencilStateCreateInfo::front/back end up applied to the OPPOSITE
        // winding from what the *culling* path's frontFace determination would suggest --
        // confirmed via a genuinely differential stencil_twosided test (a back-facing triangle's
        // CounterClockwiseStencilFunction/Fail must apply and did not until front/back were
        // swapped here specifically; culling itself, which uses the exact same frontFace value,
        // is unaffected and already correct across this whole project's existing test suite).
        // Root cause not fully isolated (plausibly an llvmpipe/Mesa software-rasterizer quirk in
        // its own front/back VkStencilOpState assignment specifically, since culling's front/back
        // classification is provably correct on this same driver) -- swapped here pragmatically
        // since XNA's "front"/"CounterClockwise" stencil settings must land on whichever Vulkan
        // slot the hardware/driver actually evaluates for each winding, not on the slot named to
        // match culling's own already-correct convention.
        if (p.twoSidedStencilMode) {
            ds.front.failOp      = ToVkStencilOp(p.ccwStencilFail);
            ds.front.passOp      = ToVkStencilOp(p.ccwStencilPass);
            ds.front.depthFailOp = ToVkStencilOp(p.ccwStencilDepthFail);
            ds.front.compareOp   = ToVkCompareOp(p.ccwStencilFunc);
            ds.back.failOp      = ToVkStencilOp(p.stencilFail);
            ds.back.passOp      = ToVkStencilOp(p.stencilPass);
            ds.back.depthFailOp = ToVkStencilOp(p.stencilDepthFail);
            ds.back.compareOp   = ToVkCompareOp(p.stencilFunc);
        } else {
            ds.back = ds.front;
        }
    }

    // Encode (topology × depthTest × depthWrite × blend × cullMode × msaa × depth/stencil state)
    // into a uint64_t key.
    static uint64_t Make3DKey(VkPrimitiveTopology topo, bool depthTest, bool depthWrite,
                              bool blend, int cullMode, uint32_t colorAttachmentCount,
                              bool wireframe, bool msaa,
                              const DepthStencilKeyParams& ds = {})
    {
        uint64_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        // bits 0-1: topology, 2: depthTest, 3: depthWrite, 4: blend, 5-6: cullMode,
        // 7-9: colorAttachmentCount, 10: wireframe, 11: msaa, 12-40: depth/stencil state
        // (Task 870, see PackDepthStencilBits).
        const uint64_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return t | (depthTest ? 4ull : 0ull) | (depthWrite ? 8ull : 0ull) | (blend ? 16ull : 0ull)
                 | (static_cast<uint64_t>(cullMode & 0x3) << 5)
                 | (nc << 7)
                 | (wireframe ? (1ull << 10) : 0ull)
                 | (msaa     ? (1ull << 11) : 0ull)
                 | (PackDepthStencilBits(ds) << 12);
    }

    // Task 911: folds a target's real depth VkFormat into an already-computed pipeline cache key.
    // Needed because two draws with an otherwise-identical key could still require genuinely
    // different, mutually-incompatible pipelines if they render into targets with different real
    // depth formats -- Vulkan pipeline/render-pass compatibility requires an exact
    // attachment-format match (see PickRTPipelineRenderPass()). Uses bits 45+, well clear of
    // Make3DKey's own bits (top out at 40) and MakeExt3DKey's (top out at 44), and comfortably
    // wide for any real VkFormat ordinal.
    static uint64_t FoldDepthFormatIntoKey(uint64_t key, VkFormat depthFmt)
    {
        return key ^ (static_cast<uint64_t>(depthFmt) << 45);
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipeline3D(VkPrimitiveTopology topo,
                                                             bool depthTest, bool depthWrite,
                                                             bool blend, int cullMode,
                                                             uint32_t colorAttachmentCount,
                                                             bool wireframe, bool msaa,
                                                             const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        // Create layout once
        if (pipelineLayout3D_ == VK_NULL_HANDLE) {
            // 128 bytes: colored3d.vert.glsl's PC struct mirrors FillExtPushConst()'s full
            // 32-float layout (Task 364) so diffuseColor/vertexColorEnabled reach the shader.
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (3D) failed");
        }

        PipelineKey key = { FoldDepthFormatIntoKey(Make3DKey(topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelines3D_.find(key);
        if (it != pipelines3D_.end()) return it->second;

        using namespace Shaders;
        // Task 899: dedicated shaders (was: reuse kColored3dVertSpv/kColored3dFragSpv) --
        // colored3d.vert.glsl/.frag.glsl now declare a fog UBO binding as part of the shared
        // colored3d/textured3d/colored_textured3d bundle, incompatible with this pipeline's
        // unmodified zero-descriptor-set pipelineLayout3D_ (used only by the legacy,
        // no-GpuDrawParams DrawColoredPrimitives()/DrawIndexedColoredPrimitives() path, which
        // has no fog data to forward anyway).
        VkShaderModule vert = CreateShaderModule(kColored3dLegacyVertSpv, kColored3dLegacyVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kInstanced3dFragSpv, kInstanced3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };
        attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 2; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // Pipeline uses VK_FRONT_FACE_CLOCKWISE, so CW faces are front faces.
        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;  // cull CW (front) faces
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;   // cull CCW (back) faces

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        VkPipelineColorBlendAttachmentState cba{};
        // Task 868: real per-BlendState mapping, replacing the previous hardcoded
        // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
        FillBlendAttachmentState(cba, blend, blendParams);
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // Replicate the same blend state across all MRT outputs.
        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u), cba);

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        // Task 870: stencil reference/compare mask/write mask are true Vulkan dynamic state,
        // set per-draw via vkCmdSetStencilReference/CompareMask/WriteMask (see draw3DFor) --
        // not baked into the pipeline, so DepthStencilState.ReferenceStencil/StencilMask/
        // StencilWriteMask changes never need a new pipeline variant.
        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 7; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        pci.renderPass          = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass             = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelines3D_[key] = p;
        return p;
    }

    // =========================================================================
    // Sprite GPU buffers
    // =========================================================================

    void VulkanGraphicsBackend::CreateSpriteBuffers()
    {
        VkDeviceSize vbSize = MaxSpriteVertices * sizeof(Sprite2DVertex);
        VkDeviceSize ibSize = MaxSpriteIndices  * sizeof(uint16_t);
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                spriteVB_[i], spriteVBMem_[i], &spriteVBPtr_[i]);
            CreateBuffer(ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                spriteIB_[i], spriteIBMem_[i], &spriteIBPtr_[i]);
        }
    }

    void VulkanGraphicsBackend::CreateFrame3DBuffers()
    {
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(kFrame3DVBSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DVB_[i], frame3DVBMem_[i], &frame3DVBPtr_[i]);
            CreateBuffer(kFrame3DIBSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DIB_[i], frame3DIBMem_[i], &frame3DIBPtr_[i]);
        }
    }

    void VulkanGraphicsBackend::EnsureFrame3DBuffers()
    {
        if (!frame3DBuffersAllocated_) {
            CreateFrame3DBuffers();
            frame3DBuffersAllocated_ = true;
        }
    }

    void VulkanGraphicsBackend::CreateFrame3DInstBuffers()
    {
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(kFrame3DInstVBSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DInstVB_[i], frame3DInstVBMem_[i], &frame3DInstVBPtr_[i]);
        }
    }

    void VulkanGraphicsBackend::EnsureFrame3DInstBuffers()
    {
        if (!frame3DInstBuffersAllocated_) {
            CreateFrame3DInstBuffers();
            frame3DInstBuffersAllocated_ = true;
        }
    }

    // =========================================================================
    // Extended 3D pipeline helpers (Tasks 52-54: textured + lit)
    // =========================================================================

    static uint64_t MakeExt3DKey(std::size_t stride, VkPrimitiveTopology topo,
                                  bool depthTest, bool depthWrite, bool blend,
                                  int cullMode, uint32_t colorAttachmentCount,
                                  bool wireframe, bool msaa,
                                  const DepthStencilKeyParams& ds = {})
    {
        uint64_t s = 0;
        switch (stride) { case 20: s = 1; break; case 24: s = 2; break; case 32: s = 3; break;
                          case 52: s = 4; break; default: s = 0; }
        uint64_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        // bits 0-3: stride, 4-5: topology, 6: depthTest, 7: depthWrite, 8: blend,
        // 9-10: cullMode, 11-13: colorAttachmentCount, 14: wireframe, 15: msaa,
        // 16-44: depth/stencil state (Task 870, see PackDepthStencilBits).
        const uint64_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return s | (t << 4) | (depthTest ? (1ull<<6) : 0) | (depthWrite ? (1ull<<7) : 0)
             | (blend ? (1ull<<8) : 0) | (static_cast<uint64_t>(cullMode & 3) << 9) | (nc << 11)
             | (wireframe ? (1ull<<14) : 0)
             | (msaa      ? (1ull<<15) : 0)
             | (PackDepthStencilBits(ds) << 16);
    }

    void VulkanGraphicsBackend::EnsureDefaultWhiteTexture()
    {
        if (defaultWhiteImage_ != VK_NULL_HANDLE) return;
        VkDevice dev = device_;

        // 1×1 RGBA white image.
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent        = {1, 1, 1};
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &info, nullptr, &defaultWhiteImage_) != VK_SUCCESS) return;

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, defaultWhiteImage_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &alloc, nullptr, &defaultWhiteMemory_);
        vkBindImageMemory(dev, defaultWhiteImage_, defaultWhiteMemory_, 0);

        // Upload white pixel.
        VkBuffer       sb = VK_NULL_HANDLE;
        VkDeviceMemory sm = VK_NULL_HANDLE;
        void*          sp = nullptr;
        const uint8_t  whitePixel[4] = {255, 255, 255, 255};
        CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     sb, sm, &sp);
        std::memcpy(sp, whitePixel, 4);

        TransitionImageLayout(defaultWhiteImage_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkCommandBuffer cb = BeginOneTimeCommands();
        VkBufferImageCopy reg{};
        reg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        reg.imageExtent      = {1, 1, 1};
        vkCmdCopyBufferToImage(cb, sb, defaultWhiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
        EndOneTimeCommands(cb);
        TransitionImageLayout(defaultWhiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(dev, sb, nullptr);
        vkFreeMemory(dev, sm, nullptr);

        // Image view.
        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = defaultWhiteImage_;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev, &vci, nullptr, &defaultWhiteView_);

        // Descriptor set.
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorPool     = descriptorPool_;
        dsAI.descriptorSetCount = 1;
        dsAI.pSetLayouts        = &descriptorSetLayout_;
        vkAllocateDescriptorSets(dev, &dsAI, &defaultWhiteDescSet_);

        VkDescriptorImageInfo di{};
        di.sampler     = defaultSampler_;
        di.imageView   = defaultWhiteView_;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wr{};
        wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet          = defaultWhiteDescSet_;
        wr.dstBinding      = 0;
        wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount = 1;
        wr.pImageInfo      = &di;
        vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
    }

    void VulkanGraphicsBackend::FillExtPushConst(float (&pc)[32], const Matrix& wvp,
                                                  const GpuDrawParams& p)
    {
        // [0..15]: MVP matrix (column-major)
        wvp.ToColumnMajor(pc);
        // [16..19]: diffuseColor
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        // [20..22]: ambientColor, [23]: lightingEnabled
        pc[20] = p.ambientColor[0]; pc[21] = p.ambientColor[1]; pc[22] = p.ambientColor[2];
        pc[23] = p.lightingEnabled ? 1.f : 0.f;
        // [24..26]: light0Dir, [27]: textureEnabled
        pc[24] = p.light0Dir[0]; pc[25] = p.light0Dir[1]; pc[26] = p.light0Dir[2];
        pc[27] = p.textureEnabled ? 1.f : 0.f;
        // [28..30]: light0Diffuse, [31]: vertexColorEnabled
        pc[28] = p.light0Diffuse[0]; pc[29] = p.light0Diffuse[1]; pc[30] = p.light0Diffuse[2];
        pc[31] = p.vertexColorEnabled ? 1.f : 0.f;
    }

    void VulkanGraphicsBackend::FillInstancedPushConst(float (&pc)[32], const Matrix& view,
                                                        const Matrix& proj, const GpuDrawParams& p)
    {
        // [0..15]: VP matrix (view × projection, column-major); world comes from per-instance buffer
        const Matrix vp = view * proj;
        vp.ToColumnMajor(pc);
        // [16..31]: same layout as FillExtPushConst
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        pc[20] = p.ambientColor[0]; pc[21] = p.ambientColor[1]; pc[22] = p.ambientColor[2];
        pc[23] = p.lightingEnabled ? 1.f : 0.f;
        pc[24] = p.light0Dir[0]; pc[25] = p.light0Dir[1]; pc[26] = p.light0Dir[2];
        pc[27] = p.textureEnabled ? 1.f : 0.f;
        pc[28] = p.light0Diffuse[0]; pc[29] = p.light0Diffuse[1]; pc[30] = p.light0Diffuse[2];
        pc[31] = p.vertexColorEnabled ? 1.f : 0.f;
    }

    void VulkanGraphicsBackend::FillAlphaTestPushConst(float (&pc)[32], const Matrix& wvp,
                                                        const GpuDrawParams& p)
    {
        // [0..15]: MVP matrix (column-major)
        wvp.ToColumnMajor(pc);
        // [16..19]: diffuseColor
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        // [20..23]: alpha test params {refVal, tolerance, passWeight, failWeight}
        pc[20] = p.alphaTest[0]; pc[21] = p.alphaTest[1];
        pc[22] = p.alphaTest[2]; pc[23] = p.alphaTest[3];
        // [24]: vertexColorEnabled (Task 887, read only by the stride-24 colored variant's VS)
        pc[24] = p.vertexColorEnabled ? 1.f : 0.f;
        // [25..30]: fog (Task 888) {fogEnabled, fogStart, fogEnd, fogColor.xyz}; [31]: padding
        pc[25] = p.fogEnabled ? 1.f : 0.f;
        pc[26] = p.fogStart; pc[27] = p.fogEnd;
        pc[28] = p.fogColor[0]; pc[29] = p.fogColor[1]; pc[30] = p.fogColor[2];
        pc[31] = 0.f;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineAlphaTest3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        if (pipelineLayoutAlphaTest3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout_;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutAlphaTest3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (AlphaTest3D) failed");
        }

        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesAlphaTest3D_.find(key);
        if (it != pipelinesAlphaTest3D_.end()) return it->second;

        using namespace Shaders;
        // Task 887: stride 24 (VertexPositionColorTexture) gets its own vertex shader that reads
        // the color attribute and gates it by VertexColorEnabled; strides 20/32 have no color data
        // and keep the original shared position+UV-only shader (UV offset remapped per stride).
        const bool colored = (stride == 24);
        VkShaderModule vert = colored
            ? CreateShaderModule(kAlphaTestColored3dVertSpv, kAlphaTestColored3dVertSpv_size)
            : CreateShaderModule(kAlphaTest3dVertSpv, kAlphaTest3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kAlphaTest3dFragSpv, kAlphaTest3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        uint32_t attrCount;
        if (colored) {
            // float3 pos + ubyte4 color + float2 uv (mirrors colored_textured3d's layout).
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
            attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    16 };
            attrCount = 3;
        } else {
            // Position always at location=0, UV always remapped to location=1.
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };   // position
            uint32_t uvOffset = (stride == 32) ? 24 : 12;         // past float3 normal, else stride 20
            attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, uvOffset }; // UV remapped to location=1
            attrCount = 2;
        }

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor;
        cbs.pAttachments    = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutAlphaTest3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesAlphaTest3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    void VulkanGraphicsBackend::EnsureDualTexResources()
    {
        if (descriptorSetLayout2Tex_ != VK_NULL_HANDLE) return;

        // Two combined image samplers at bindings 0/1, plus (Task 899) a dynamic fog UBO at
        // binding=2 -- DualTextureEffect's 128-byte push constant has no spare bytes for fog.
        VkDescriptorSetLayoutBinding bindings[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding            = i;
            bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount    = 1;
            bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayout2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (2-tex) failed");

        // Pool for up to 512 dual-texture descriptor sets × MaxFramesInFlight (per-frame cache,
        // Task 899 -- the fog UBO buffer differs per frame in flight).
        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 2 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPool2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (2-tex) failed");

        // Pipeline layout: same 128-byte PC range + 2-sampler+fog descriptor set layout.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout2Tex_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutDualTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (DualTex3D) failed");

        // Per-frame UBO ring buffer for the fog block (Task 899).
        const VkDeviceSize uboSize = kDualTexFogUBOStride * kDualTexFogUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (dualTexFogUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    dualTexFogUBO_[i], dualTexFogUBOMem_[i], &dualTexFogUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateDualTexDescSet(
        uint32_t frameIdx, VkImageView view0, VkImageView view1,
        VkSampler sampler0, VkSampler sampler1)
    {
        EnsureDualTexResources();

        const uint64_t key = reinterpret_cast<uint64_t>(view0)    * 2654435761ULL
                           ^ reinterpret_cast<uint64_t>(view1)    * 40503ULL
                           ^ reinterpret_cast<uint64_t>(sampler0) * 2246822519ULL
                           ^ reinterpret_cast<uint64_t>(sampler1) * 3266489917ULL;
        auto& cache = dualTexDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPool2Tex_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayout2Tex_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo[2]{};
        imgInfo[0].sampler     = sampler0;
        imgInfo[0].imageView   = view0;
        imgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo[1].sampler     = sampler1;
        imgInfo[1].imageView   = view1;
        imgInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = dualTexFogUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 32; // vec4 fogColorEnabled + vec4 fogStartEnd

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
        cache[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineDualTex3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureDualTexResources();

        // DualTexture always uses stride=20 (VertexPositionTexture); key encodes topology+state.
        constexpr std::size_t kDualStride = 20;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kDualStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesDualTex3D_.find(key);
        if (it != pipelinesDualTex3D_.end()) return it->second;

        using namespace Shaders;
        // Task 899: dedicated vertex shader (was: reuse kTextured3dVertSpv) -- textured3d.vert.glsl
        // now declares its own fog UBO at binding=1 (Bundle A's shared layout), which conflicts
        // with dual_texture3d's 2-sampler descriptor set layout (fog UBO here is at binding=2).
        VkShaderModule vert = CreateShaderModule(kDualTexture3dVertSpv,  kDualTexture3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kDualTexture3dFragSpv,  kDualTexture3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, kDualStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,    12 };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 2; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutDualTex3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesDualTex3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // ---- EnvironmentMapEffect resources (Task 108) ----

    void VulkanGraphicsBackend::EnsureEnvMapResources()
    {
        if (descriptorSetLayoutEnvMap_ != VK_NULL_HANDLE) return;

        // Descriptor set layout: binding=0 sampler2D, binding=1 samplerCube, binding=2 UBO dynamic.
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutEnvMap_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (EnvMap) failed");

        // Descriptor pool: max 512 sets × MaxFramesInFlight.
        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 2 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolEnvMap_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (EnvMap) failed");

        // Pipeline layout: 128-byte PC (mvp+world) + env map descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutEnvMap_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutEnvMap3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (EnvMap3D) failed");

        // Per-frame UBO ring buffers for env map FS params.
        const VkDeviceSize uboSize = kEnvMapUBOStride * kEnvMapUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (envMapUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    envMapUBO_[i], envMapUBOMem_[i], &envMapUBOPtr_[i]);
            }
        }

        // Default white cube image for fallback when envMap is null.
        if (defaultWhiteCubeImage_ == VK_NULL_HANDLE) {
            VkImageCreateInfo imgInfo{};
            imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            imgInfo.imageType     = VK_IMAGE_TYPE_2D;
            imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
            imgInfo.extent        = { 1, 1, 1 };
            imgInfo.mipLevels     = 1;
            imgInfo.arrayLayers   = 6;
            imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(device_, &imgInfo, nullptr, &defaultWhiteCubeImage_);

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(device_, defaultWhiteCubeImage_, &memReq);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memReq.size;
            allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device_, &allocInfo, nullptr, &defaultWhiteCubeMem_);
            vkBindImageMemory(device_, defaultWhiteCubeImage_, defaultWhiteCubeMem_, 0);

            // Upload white pixels to all 6 faces via staging buffer.
            const uint32_t white = 0xFFFFFFFF;
            VkBuffer stageBuf = VK_NULL_HANDLE; VkDeviceMemory stageMem = VK_NULL_HANDLE;
            CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stageBuf, stageMem, nullptr);
            void* mapped = nullptr;
            vkMapMemory(device_, stageMem, 0, 4, 0, &mapped);
            std::memcpy(mapped, &white, 4);
            vkUnmapMemory(device_, stageMem);

            VkCommandBuffer cb = BeginOneTimeCommands();
            VkImageMemoryBarrier barr{};
            barr.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barr.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barr.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barr.image               = defaultWhiteCubeImage_;
            barr.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
            barr.srcAccessMask       = 0;
            barr.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barr);

            for (uint32_t face = 0; face < 6; ++face) {
                VkBufferImageCopy region{};
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 };
                region.imageExtent      = { 1, 1, 1 };
                vkCmdCopyBufferToImage(cb, stageBuf, defaultWhiteCubeImage_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
            barr.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barr.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barr.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barr.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barr);
            EndOneTimeCommands(cb);

            vkDestroyBuffer(device_, stageBuf, nullptr);
            vkFreeMemory(device_, stageMem, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image    = defaultWhiteCubeImage_;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
            vkCreateImageView(device_, &viewInfo, nullptr, &defaultWhiteCubeView_);
        }
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateEnvMapDescSet(
        uint32_t frameIdx, VkImageView view2D, VkImageView viewCube)
    {
        EnsureEnvMapResources();
        if (view2D   == VK_NULL_HANDLE) view2D   = defaultWhiteView_;
        if (viewCube == VK_NULL_HANDLE) viewCube = defaultWhiteCubeView_;

        const uint64_t key = reinterpret_cast<uint64_t>(view2D) * 2654435761ULL
                           ^ reinterpret_cast<uint64_t>(viewCube);
        auto& cache = envMapDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolEnvMap_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutEnvMap_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo[2]{};
        imgInfo[0] = { defaultSampler_, view2D,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imgInfo[1] = { defaultSampler_, viewCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        // Binding=2: dynamic UBO pointing to the whole per-frame ring buffer.
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = envMapUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 128; // size of one EnvMapParams block in the shader (96 + fog, Task 899)

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        cache[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineEnvMap3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureEnvMapResources();

        constexpr std::size_t kEnvStride = 32;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kEnvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesEnvMap3D_.find(key);
        if (it != pipelinesEnvMap3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kEnvMap3dVertSpv, kEnvMap3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kEnvMap3dFragSpv, kEnvMap3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, kEnvStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 };   // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    24 };   // aUV

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutEnvMap3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesEnvMap3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    void VulkanGraphicsBackend::FillEnvMapPushConst(float (&pc)[32],
                                                     const Matrix& wvp, const Matrix& world)
    {
        wvp.ToColumnMajor(pc);        // [0..15]: MVP
        world.ToColumnMajor(pc + 16); // [16..31]: World
    }

    // ---- BasicEffect lit-textured resources (Task 897) ----
    // DirectionalLight1/DirectionalLight2/EmissiveColor forwarding, added alongside the
    // unchanged 128-byte PC (still filled by FillExtPushConst, same as strides 20/24) since
    // that content is shared/verified — only a new UBO binding is added for the extra data.

    void VulkanGraphicsBackend::EnsureLitTexturedResources()
    {
        if (descriptorSetLayoutLitTextured_ != VK_NULL_HANDLE) return;

        // binding=0: sampler2D (fragment), binding=1: light1/2+emissive+world+specular UBO
        // dynamic. Vertex stage needs it too (Task 898: world matrix, for a correct world-space
        // position/normal instead of the wrong MVP-based transform).
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutLitTextured_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (LitTextured) failed");

        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolLitTextured_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (LitTextured) failed");

        // Pipeline layout: same 128-byte PC as pipelineLayoutExt3D_ (unchanged content/fill
        // function) + the new lit-textured descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutLitTextured_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutLitTextured3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (LitTextured3D) failed");

        const VkDeviceSize uboSize = kLitTexturedUBOStride * kLitTexturedUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (litTexturedUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    litTexturedUBO_[i], litTexturedUBOMem_[i], &litTexturedUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateLitTexturedDescSet(
        uint32_t frameIdx, VkImageView view2D)
    {
        EnsureLitTexturedResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        const uint64_t key = reinterpret_cast<uint64_t>(view2D);
        auto& cache = litTexturedDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolLitTextured_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutLitTextured_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo{ defaultSampler_, view2D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = litTexturedUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 256;  // size of one LitLightParams block in the shader (Task 886/898/888)

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        cache[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineLitTextured3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureLitTexturedResources();

        constexpr std::size_t kLitStride = 32;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kLitStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesLitTextured3D_.find(key);
        if (it != pipelinesLitTextured3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kLitTextured3dVertSpv, kLitTextured3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kLitTextured3dFragSpv, kLitTextured3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(kLitStride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 };   // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    24 };   // aUV

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutLitTextured3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesLitTextured3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // ---- BasicEffect fog bundle (Task 899): colored3d / textured3d / colored_textured3d ----
    //
    // These 3 pipelines share the exact same fully-packed 128-byte FillExtPushConst() push
    // constant (zero spare bytes for fog), so fog is forwarded via one small shared dynamic UBO
    // instead -- mirroring descriptorSetLayoutLitTextured_'s exact shape (sampler@0 + dynamic
    // UBO@1). colored3d's own shaders never sample a texture, but the layout still declares
    // binding=0 so all three pipelines can share one descriptor-set-layout/pool/UBO/cache bundle;
    // a fallback white texture is bound there for colored3d draws (same fallback pattern every
    // other pipeline already uses when a slot has no real texture).

    void VulkanGraphicsBackend::EnsureFogTex3DResources()
    {
        if (descriptorSetLayoutFogTex3D_ != VK_NULL_HANDLE) return;

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (FogTex3D) failed");

        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (FogTex3D) failed");

        // Pipeline layout: same 128-byte PC as FillExtPushConst() (unchanged content/fill
        // function) + the new fog descriptor set. Declared with both stages even though
        // colored3d's own fragment shader never reads it -- Vulkan permits a pipeline layout to
        // declare a stage-accessible range wider than what any specific attached shader reads.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutFogTex3D_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (FogTex3D) failed");

        const VkDeviceSize uboSize = kFogTex3DUBOStride * kFogTex3DUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (fogTex3DUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    fogTex3DUBO_[i], fogTex3DUBOMem_[i], &fogTex3DUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateFogTex3DDescSet(
        uint32_t frameIdx, VkImageView view2D)
    {
        EnsureFogTex3DResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        const uint64_t key = reinterpret_cast<uint64_t>(view2D);
        auto& cache = fogTex3DDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolFogTex3D_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutFogTex3D_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo{ defaultSampler_, view2D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = fogTex3DUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 32; // vec4 fogColorEnabled + vec4 fogStartEnd

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        cache[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineFogColored3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureFogTex3DResources();

        PipelineKey key = { FoldDepthFormatIntoKey(Make3DKey(topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesFogColored3D_.find(key);
        if (it != pipelinesFogColored3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kColored3dVertSpv, kColored3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kColored3dFragSpv, kColored3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };
        attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 2; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        VkPipelineColorBlendAttachmentState cba{};
        // Task 868: real per-BlendState mapping, replacing the previous hardcoded
        // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
        FillBlendAttachmentState(cba, blend, blendParams);
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u), cba);

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 7; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutFogTex3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        pci.renderPass          = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass             = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (FogColored3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelinesFogColored3D_[key] = p;
        return p;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineFogTex3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureFogTex3DResources();

        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesFogTex3D_.find(key);
        if (it != pipelinesFogTex3D_.end()) return it->second;

        using namespace Shaders;
        const uint32_t* vertSpv = nullptr; size_t vertSpvSize = 0;
        const uint32_t* fragSpv = nullptr; size_t fragSpvSize = 0;
        if (stride == 24) {
            vertSpv = kColoredTextured3dVertSpv;  vertSpvSize = kColoredTextured3dVertSpv_size;
            fragSpv = kColoredTextured3dFragSpv;  fragSpvSize = kColoredTextured3dFragSpv_size;
        } else {
            vertSpv = kTextured3dVertSpv;         vertSpvSize = kTextured3dVertSpv_size;
            fragSpv = kTextured3dFragSpv;         fragSpvSize = kTextured3dFragSpv_size;
        }
        VkShaderModule vert = CreateShaderModule(vertSpv, vertSpvSize);
        VkShaderModule frag = CreateShaderModule(fragSpv, fragSpvSize);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        uint32_t attrCount = 0;
        if (stride == 24) {
            // float3 pos + ubyte4 color + float2 uv
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12};
            attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};
            attrCount = 3;
        } else {
            // float3 pos + float2 uv (stride 20)
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    12};
            attrCount = 2;
        }

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        VkPipelineColorBlendAttachmentState cba{};
        // Task 868: real per-BlendState mapping, replacing the previous hardcoded
        // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
        FillBlendAttachmentState(cba, blend, blendParams);
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u), cba);

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 7; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutFogTex3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass() (which also keeps Task 904's msaa-aware fix above intact).
        pci.renderPass = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (FogTex3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelinesFogTex3D_[key] = p;
        return p;
    }

    // ---- SkinnedEffect resources (Task 109) ----

    void VulkanGraphicsBackend::EnsureSkinnedResources()
    {
        if (descriptorSetLayoutSkinned_ != VK_NULL_HANDLE) return;

        // binding=0: sampler2D (fragment), binding=1: bone UBO dynamic (vertex),
        // binding=2: fog UBO dynamic (Task 899 -- BoneBlock has zero spare capacity, so fog gets
        // its own small dedicated dynamic UBO instead of being packed alongside the bones).
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (Skinned) failed");

        const uint32_t maxSets = 128u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets * 2 }; // BoneBlock + fog
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (Skinned) failed");

        // 128-byte PC (same Ext3D layout) + skinned descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutSkinned_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutSkinned3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (Skinned3D) failed");

        // Per-frame bone matrix UBO ring buffers.
        const VkDeviceSize uboSize = kSkinnedUBOStride * kSkinnedUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    skinnedUBO_[i], skinnedUBOMem_[i], &skinnedUBOPtr_[i]);
            }
        }
        // Per-frame fog UBO ring buffers (Task 899).
        const VkDeviceSize fogUboSize = kSkinnedFogUBOStride * kSkinnedFogUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedFogUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(fogUboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    skinnedFogUBO_[i], skinnedFogUBOMem_[i], &skinnedFogUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateSkinnedDescSet(
        uint32_t frameIdx, VkImageView view2D)
    {
        EnsureSkinnedResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        const uint64_t key = reinterpret_cast<uint64_t>(view2D);
        auto& cache = skinnedDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolSkinned_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutSkinned_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = defaultSampler_;
        imgInfo.imageView   = view2D;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // binding=1: dynamic UBO pointing to the whole per-frame bone ring buffer.
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = skinnedUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = kSkinnedUBOStride;  // one bone palette block

        // binding=2: dynamic fog UBO (Task 899).
        VkDescriptorBufferInfo fogBufInfo{};
        fogBufInfo.buffer = skinnedFogUBO_[frameIdx];
        fogBufInfo.offset = 0;
        fogBufInfo.range  = 32; // vec4 fogColorEnabled + vec4 fogStartEnd

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &fogBufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        cache[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineSkinned3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        EnsureSkinnedResources();

        // Task 11.10: this layout is independently duplicated (magic stride 52) in
        // EasyGLGraphicsBackend.cpp's ApplyLayout and BgfxGraphicsBackend.cpp's MakeBgfxLayout -
        // see EasyGLGraphicsBackend.cpp's own comment at its "case 52" for the full cross-
        // reference to the canonical VertexPositionNormalTextureSkinned::getVertexDeclarationStatic()
        // layout and why a shared-derivation refactor was investigated but deferred.
        constexpr std::size_t kSkinnedStride = 52;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kSkinnedStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesSkinned3D_.find(key);
        if (it != pipelinesSkinned3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kSkinned3dVertSpv, kSkinned3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kSkinned3dFragSpv, kSkinned3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, kSkinnedStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[5]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }; // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 }; // aUV
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }; // aBoneWeights
        attrs[4] = { 4, 0, VK_FORMAT_R8G8B8A8_UINT,       48 }; // aBoneIndices

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 5; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutSkinned3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesSkinned3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineInstanced3D(
        std::size_t pvStride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        // Ensure pipelineLayoutExt3D_ exists (128-byte PC + 1 descriptor set for future texture use).
        if (pipelineLayoutExt3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout_;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutExt3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (Ext3D/Instanced) failed");
        }

        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(pvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams) };
        auto it = pipelinesInstanced3D_.find(key);
        if (it != pipelinesInstanced3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kInstanced3dVertSpv, kInstanced3dVertSpv_size);
        // Task 899: dedicated FS (was: reuse kColored3dFragSpv) -- colored3d.frag.glsl now
        // declares a 2nd descriptor binding (fog UBO) as part of the shared colored3d/textured3d/
        // colored_textured3d bundle, incompatible with Instanced3D's unmodified 1-binding layout.
        VkShaderModule frag = CreateShaderModule(kInstanced3dFragSpv, kInstanced3dFragSpv_size);

        // Two vertex bindings: binding=0 per-vertex (VERTEX rate), binding=1 per-instance (INSTANCE rate).
        constexpr uint32_t kInstStride = 64; // sizeof(mat4)
        VkVertexInputBindingDescription binds[2]{};
        binds[0] = { 0, static_cast<uint32_t>(pvStride), VK_VERTEX_INPUT_RATE_VERTEX   };
        binds[1] = { 1, kInstStride,                      VK_VERTEX_INPUT_RATE_INSTANCE };

        VkVertexInputAttributeDescription attrs[5]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos (per-vertex)
        attrs[1] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0  }; // aInstCol0 (per-instance)
        attrs[2] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 }; // aInstCol1
        attrs[3] = { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }; // aInstCol2
        attrs[4] = { 7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 }; // aInstCol3

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 2; vis.pVertexBindingDescriptions   = binds;
        vis.vertexAttributeDescriptionCount = 5; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(dss, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams);
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[6] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                                  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 6; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &dss;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutExt3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesInstanced3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // Task 899: GetOrCreatePipelineExt3D (textured3d/colored_textured3d via the OLD, plain
    // pipelineLayoutExt3D_/descriptorSetLayout_) was removed here -- BasicEffect draws for
    // stride 20/24 now exclusively use GetOrCreatePipelineFogTex3D (the new fog-capable bundle)
    // instead, so this function had become unreachable dead code, AND would have failed
    // vkCreateGraphicsPipelines validation if ever called (its shaders -- kTextured3dVertSpv/
    // kColoredTextured3dVertSpv -- now declare a 2nd descriptor binding for fog that
    // pipelineLayoutExt3D_'s original 1-binding descriptorSetLayout_ does not provide).
    // pipelineLayoutExt3D_/descriptorSetLayout_ themselves are unchanged and still used by
    // Instanced3D and 2D SpriteBatch, per this task's explicit requirement.

    // =========================================================================
    // Memory / resource helpers
    // =========================================================================

    uint32_t VulkanGraphicsBackend::FindMemoryType(uint32_t typeBits,
                                                    VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("Vulkan: FindMemoryType failed");
    }

    void VulkanGraphicsBackend::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkMemoryPropertyFlags props,
                                              VkBuffer& buf, VkDeviceMemory& mem, void** mapped)
    {
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size  = size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &ci, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("vkCreateBuffer failed");

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device_, buf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits, props);
        if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory failed");
        vkBindBufferMemory(device_, buf, mem, 0);

        if (mapped)
            vkMapMemory(device_, mem, 0, size, 0, mapped);
    }

    VkCommandBuffer VulkanGraphicsBackend::BeginOneTimeCommands()
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &ai, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        return cb;
    }

    void VulkanGraphicsBackend::EndOneTimeCommands(VkCommandBuffer cb)
    {
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cb);
    }

    void VulkanGraphicsBackend::TransitionImageLayout(VkImage img,
                                                       VkImageLayout from, VkImageLayout to)
    {
        VkCommandBuffer cb = BeginOneTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = img;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkPipelineStageFlags srcStage, dstStage;
        if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            // Needed by VulkanTextureBackend::UpdatePixels to re-upload a texture that has
            // already been sampled at least once (i.e. every SetData call after the first).
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            // Task 865: needed by VulkanTexture3DBackend/VulkanTextureCubeBackend::GetData to
            // read back a texture that has already been sampled at least once.
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            // Task 865: restores sampling readiness after GetData's readback copy.
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::runtime_error("Vulkan: unsupported image layout transition");
        }
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndOneTimeCommands(cb);
    }

    void VulkanGraphicsBackend::CopyBufferToImage(VkBuffer buf, VkImage img,
                                                   uint32_t w, uint32_t h)
    {
        VkCommandBuffer cb = BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { w, h, 1 };
        vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        EndOneTimeCommands(cb);
    }

    // =========================================================================
    // IGraphicsBackend implementation
    // =========================================================================

    void VulkanGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        readbackStagingValid_ = false;  // new frame content invalidates the readback cache
        // Task 875: mark the currently-bound RT as needing its render pass recorded this frame,
        // even if no draw call follows.
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed");

        // Task 447/854: reset every OcclusionQuery pool actually tagged on a pending draw this
        // frame, before any render pass begins (vkCmdResetQueryPool must not be called inside a
        // render pass instance). Replaces the old "reset once at construction only" bug -- a
        // query reused across frames (the real, idiomatic XNA usage pattern: Begin()/End() called
        // once per Draw()) now gets a fresh reset every time it's actually used, not just the
        // first. recordedThisFrame_ is cleared here too, so draw3DFor's own contiguous-run
        // tracking starts fresh each frame.
        {
            std::vector<VulkanOcclusionQueryBackend*> queriesThisFrame;
            for (const auto& draw : pending3D_) {
                if (draw.occlusionQuery &&
                    std::find(queriesThisFrame.begin(), queriesThisFrame.end(), draw.occlusionQuery)
                        == queriesThisFrame.end())
                    queriesThisFrame.push_back(draw.occlusionQuery);
            }
            for (auto* q : queriesThisFrame) {
                q->recordedThisFrame_ = false;
                if (q->pool_ != VK_NULL_HANDLE)
                    vkCmdResetQueryPool(cb, q->pool_, 0, 1);
            }
        }

        // Helper: draw all 2D batches for a specific RT (nullptr = backbuffer) into current render pass.
        // Sprite VB/IB ring buffers are shared across all passes in a frame — each snapshot is
        // memcpy'd/bound at its own running byte offset (vbOff/ibOff below), mirroring draw3DFor's
        // already-correct accumulating-cursor pattern, so multiple Begin()/End() cycles (or
        // multiple SpriteBatch instances) targeting the same RT in one frame compose additively
        // instead of overwriting each other at a hardcoded offset 0 (Task 664 fix).
        auto drawSpritesFor = [&](VulkanRTSource* targetRT,
                                  float vpW, float vpH)
        {
            static constexpr VkDeviceSize kSpriteVBSize = MaxSpriteVertices * sizeof(Sprite2DVertex);
            static constexpr VkDeviceSize kSpriteIBSize = MaxSpriteIndices  * sizeof(uint16_t);
            VkPipeline   lastBoundPipeline = VK_NULL_HANDLE;
            VkDeviceSize vbOff = 0;
            VkDeviceSize ibOff = 0;
            for (auto& [snapshot, batchRT] : activeBatches_) {
                if (batchRT != targetRT) continue;
                const auto& verts = snapshot->vertices;
                const auto& inds  = snapshot->indices;
                const auto& draws = snapshot->draws;
                if (verts.empty() || draws.empty()) continue;

                const VkDeviceSize vbBytes = verts.size() * sizeof(Sprite2DVertex);
                const VkDeviceSize ibBytes = inds.size()  * sizeof(uint16_t);
                if (vbOff + vbBytes > kSpriteVBSize || ibOff + ibBytes > kSpriteIBSize) continue;

                std::memcpy(static_cast<uint8_t*>(spriteVBPtr_[currentFrame_]) + vbOff,
                            verts.data(), vbBytes);
                std::memcpy(static_cast<uint8_t*>(spriteIBPtr_[currentFrame_]) + ibOff,
                            inds.data(), ibBytes);

                // Select pipeline: custom SPIR-V effect (Task 119) or built-in 2D.
                // Task 903 finding: this previously only checked the BACKBUFFER's own MSAA state
                // (targetRT == nullptr && sampleCount_ > 1), never an RT's -- meaning a SpriteBatch
                // fill into an MSAA-enabled RenderTarget2D/RenderTargetCube face always bound the
                // non-MSAA pipeline against the RT's real 3-attachment MSAA render pass, a genuine
                // render-pass-compatibility validation error (VUID-vkCmdDraw-multisampledRenderTo
                // SingleSampled-07284/VUID-vkCmdDraw-renderPass-02684), invisible until this task's
                // own RenderTargetCube MSAA test was the first to fill an MSAA target via
                // SpriteBatch (every prior MSAA RT test used a 3D BasicEffect fill instead, whose
                // own pipeline selection already checks rt->WantsMsaa() correctly). Fixed to check
                // the actual bound target's WantsMsaa() when targeting an RT, same as the 3D path.
                const bool targetWantsMsaa = (targetRT == nullptr) ? (sampleCount_ > VK_SAMPLE_COUNT_1_BIT)
                                                                    : targetRT->WantsMsaa();
                const bool useMsaaPipe = targetWantsMsaa && (sampleCount_ > VK_SAMPLE_COUNT_1_BIT);
                // Task 911: this target's own real depth VkFormat -- see draw3DFor's identical
                // targetDepthFmt computation for the full rationale.
                const VkFormat targetDepthFmt = targetRT ? targetRT->GetDepthFormat() : depthFormat_;
                VkPipeline       activePipe   = useMsaaPipe ? GetOrCreatePipeline2DMsaa(targetDepthFmt)
                                                             : GetOrCreatePipeline2D(targetDepthFmt);
                VkPipelineLayout activeLayout = pipelineLayout2D_;
                const float*     customPC     = nullptr;
                const auto*      ceb          = snapshot->customEffectBackend;
                if (ceb && ceb->GetPipeline() != VK_NULL_HANDLE) {
                    activePipe   = ceb->GetPipeline();
                    activeLayout = ceb->GetPipelineLayout();
                    customPC     = ceb->GetPushConst();
                }

                if (activePipe != lastBoundPipeline) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipe);
                    lastBoundPipeline = activePipe;
                }
                VkDeviceSize vbBindOff = vbOff;
                vkCmdBindVertexBuffers(cb, 0, 1, &spriteVB_[currentFrame_], &vbBindOff);
                vkCmdBindIndexBuffer(cb, spriteIB_[currentFrame_], ibOff, VK_INDEX_TYPE_UINT16);

                float vpSize[2] = { vpW, vpH };
                if (customPC) {
                    // Push 128-byte block: vpSize at [0..7], user uniforms at [8..127].
                    float fullPC[32];
                    std::memcpy(fullPC,     vpSize,      8);
                    std::memcpy(fullPC + 2, customPC + 2, 120);
                    vkCmdPushConstants(cb, activeLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, fullPC);
                } else {
                    vkCmdPushConstants(cb, pipelineLayout2D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, vpSize);
                }

                for (const auto& d : draws) {
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        activeLayout, 0, 1, &d.descSet, 0, nullptr);
                    vkCmdDrawIndexed(cb, d.indexCount, 1, d.firstIndex, 0, 0);
                }

                vbOff += vbBytes;
                ibOff += ibBytes;
            }
        };

        // UBO slot counters (reset once per frame, shared across all RT passes).
        uint32_t envMapUBOSlot  = 0;
        uint32_t skinnedUBOSlot = 0;
        uint32_t litTexturedUBOSlot = 0;
        uint32_t fogTex3DUBOSlot    = 0; // Task 899: colored3d/textured3d/colored_textured3d
        uint32_t dualTexFogUBOSlot  = 0; // Task 899: DualTextureEffect
        uint32_t skinnedFogUBOSlot  = 0; // Task 899: SkinnedEffect

        // Helper: draw all pending 3D draws for a specific RT into the current render pass.
        auto draw3DFor = [&](VulkanRTSource* targetRT)
        {
            if (pending3D_.empty()) return;
            EnsureFrame3DBuffers();
            VkPipeline lastPipe   = VK_NULL_HANDLE;
            VkDeviceSize vbOff    = 0;
            VkDeviceSize ibOff    = 0;
            VkDeviceSize instVbOff = 0;
            // Task 447/854: the OcclusionQuery currently wrapped in a real vkCmdBeginQuery for
            // this render pass, if any -- tracks contiguous runs of draws sharing the same
            // occlusionQuery tag so they land in one vkCmdBeginQuery/vkCmdEndQuery pair.
            VulkanOcclusionQueryBackend* openQuery = nullptr;
            for (const auto& draw : pending3D_) {
                if (draw.rt != targetRT) continue;
                if (draw.isMarker) {
                    if (pfnCmdInsertDebugLabel_) {
                        VkDebugUtilsLabelEXT lbl{};
                        lbl.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                        lbl.pLabelName = draw.markerLabel.c_str();
                        lbl.color[0]   = 1.0f; lbl.color[1] = 1.0f;
                        lbl.color[2]   = 1.0f; lbl.color[3] = 1.0f;
                        pfnCmdInsertDebugLabel_(cb, &lbl);
                    }
                    continue;
                }
                if (draw.vbData.empty()) continue;
                if (vbOff + draw.vbData.size() > kFrame3DVBSize) continue;
                if (!draw.ibData.empty() && ibOff + draw.ibData.size() > kFrame3DIBSize) continue;
                if (draw.useInstanced && instVbOff + draw.instVbData.size() > kFrame3DInstVBSize) continue;

                // Task 447/854: this draw is definitely about to be recorded -- open/close real
                // vkCmdBeginQuery/vkCmdEndQuery pairs around contiguous runs of draws sharing the
                // same occlusionQuery tag. A query already recorded once this frame (an earlier
                // contiguous run, in this render pass or an earlier one) is deliberately NOT
                // reopened -- Vulkan requires a reset between two vkCmdBeginQuery calls on the
                // same query, and re-resetting mid-frame would corrupt the first run's own
                // in-flight result; this implements the approved "reject additional spans beyond
                // the first contiguous run" policy.
                if (draw.occlusionQuery != openQuery) {
                    if (openQuery && openQuery->pool_ != VK_NULL_HANDLE) {
                        vkCmdEndQuery(cb, openQuery->pool_, 0);
                        openQuery->recordedThisFrame_ = true;
                    }
                    openQuery = nullptr;
                    if (draw.occlusionQuery && draw.occlusionQuery->pool_ != VK_NULL_HANDLE
                        && !draw.occlusionQuery->recordedThisFrame_) {
                        vkCmdBeginQuery(cb, draw.occlusionQuery->pool_, 0, 0);
                        openQuery = draw.occlusionQuery;
                    }
                }

                std::memcpy(static_cast<uint8_t*>(frame3DVBPtr_[currentFrame_]) + vbOff,
                            draw.vbData.data(), draw.vbData.size());
                if (!draw.ibData.empty())
                    std::memcpy(static_cast<uint8_t*>(frame3DIBPtr_[currentFrame_]) + ibOff,
                                draw.ibData.data(), draw.ibData.size());
                if (draw.useInstanced && !draw.instVbData.empty())
                    std::memcpy(static_cast<uint8_t*>(frame3DInstVBPtr_[currentFrame_]) + instVbOff,
                                draw.instVbData.data(), draw.instVbData.size());

                const uint32_t nColor = targetRT ? targetRT->GetColorAttachmentCount() : 1u;
                // Task 878/879: MSAA-aware for RT passes too, not just the backbuffer -- an RT
                // draw uses the MSAA pipeline variant when this specific RT actually engaged
                // MSAA (VulkanRTSource::WantsMsaa(), true only when the backend itself has MSAA
                // infrastructure AND the RT requested it; see the "piggyback on sampleCount_"
                // scope decision in plan_graphics.md).
                const bool drawMsaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) &&
                                      (targetRT == nullptr || targetRT->WantsMsaa());
                // Task 911: this target's own real depth VkFormat -- the backbuffer's
                // device-wide depthFormat_ when drawing into the swapchain (no VulkanRTSource),
                // else the specific RenderTarget2D/RenderTargetCube instance's own picked format
                // (VK_FORMAT_UNDEFINED for DepthFormat::None). Threaded into every pipeline-cache
                // lookup below so a distinct depth format gets its own pipeline/render pass (see
                // PickRTPipelineRenderPass()).
                const VkFormat targetDepthFmt = targetRT ? targetRT->GetDepthFormat() : depthFormat_;
                VkPipeline pipe;
                if (draw.useAlphaTest) {
                    pipe = GetOrCreatePipelineAlphaTest3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useDualTexture) {
                    pipe = GetOrCreatePipelineDualTex3D(draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useEnvMap) {
                    pipe = GetOrCreatePipelineEnvMap3D(draw.topology,
                                                       draw.depthTest, draw.depthWrite,
                                                       draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useSkinned) {
                    pipe = GetOrCreatePipelineSkinned3D(draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useInstanced) {
                    pipe = GetOrCreatePipelineInstanced3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useLitTextured) {
                    pipe = GetOrCreatePipelineLitTextured3D(draw.topology,
                                                            draw.depthTest, draw.depthWrite,
                                                            draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else if (draw.useFogTex3D) {
                    // Task 899: colored3d (stride 16) / textured3d (20) / colored_textured3d (24)
                    // fog-capable bundle. The legacy no-GpuDrawParams DrawColoredPrimitives()
                    // path never sets useFogTex3D, so it still falls to the plain colored3d
                    // pipeline below.
                    pipe = (draw.stride == 16)
                           ? GetOrCreatePipelineFogColored3D(draw.topology,
                                                             draw.depthTest, draw.depthWrite,
                                                             draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt)
                           : GetOrCreatePipelineFogTex3D(draw.stride, draw.topology,
                                                         draw.depthTest, draw.depthWrite,
                                                         draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                } else {
                    pipe = GetOrCreatePipeline3D(draw.topology,
                                                 draw.depthTest, draw.depthWrite,
                                                 draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                }
                if (pipe != lastPipe) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    lastPipe = pipe;
                }
                // All 3D pipelines declare VK_DYNAMIC_STATE_DEPTH_BIAS, so the dynamic
                // depth bias must be set before each draw. Zero values = no bias.
                vkCmdSetDepthBias(cb, draw.depthBias, 0.0f, draw.slopeScaleDepthBias);
                // Task 870: stencil reference/compare mask/write mask are true Vulkan dynamic
                // state -- every 3D pipeline declares these 3 dynamic states now, so they must be
                // set before each draw regardless of whether StencilEnable is currently true
                // (matches vkCmdSetDepthBias's own always-set convention just above).
                vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                           static_cast<uint32_t>(draw.stencilReadMask));
                vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                         static_cast<uint32_t>(draw.stencilWriteMask));
                vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                         static_cast<uint32_t>(draw.referenceStencil));
                if (draw.useAlphaTest) {
                    vkCmdPushConstants(cb, pipelineLayoutAlphaTest3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    VkDescriptorSet ds = (draw.descSet != VK_NULL_HANDLE)
                                         ? draw.descSet : defaultWhiteDescSet_;
                    if (ds != VK_NULL_HANDLE)
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutAlphaTest3D_, 0, 1, &ds, 0, nullptr);
                } else if (draw.useDualTexture) {
                    vkCmdPushConstants(cb, pipelineLayoutDualTex3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // Task 899: dualTexDescSet's layout now has a 3rd binding (dynamic fog UBO),
                    // so no defaultWhiteDescSet_ fallback here anymore (that set belongs to the
                    // structurally-incompatible 1-binding descriptorSetLayout_).
                    if (draw.dualTexDescSet != VK_NULL_HANDLE && dualTexFogUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = dualTexFogUBOSlot++;
                        const uint32_t uboOff = slot * kDualTexFogUBOStride;
                        if (uboOff + 32 <= kDualTexFogUBOStride * kDualTexFogUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(dualTexFogUBOPtr_[currentFrame_]) + uboOff,
                                        draw.dualTexFogUboData, 32);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutDualTex3D_, 0, 1,
                                                &draw.dualTexDescSet, 1, &uboOff);
                    }
                } else if (draw.useEnvMap) {
                    vkCmdPushConstants(cb, pipelineLayoutEnvMap3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.envMapPC);
                    if (draw.envMapDescSet != VK_NULL_HANDLE && envMapUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = envMapUBOSlot++;
                        const uint32_t uboOff = slot * kEnvMapUBOStride;
                        if (uboOff + 128 <= kEnvMapUBOStride * kEnvMapUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(envMapUBOPtr_[currentFrame_]) + uboOff,
                                        draw.envMapUboData, 128);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutEnvMap3D_, 0, 1,
                                                &draw.envMapDescSet, 1, &uboOff);
                    }
                } else if (draw.useSkinned) {
                    vkCmdPushConstants(cb, pipelineLayoutSkinned3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    if (draw.skinnedDescSet != VK_NULL_HANDLE && skinnedUBOPtr_[currentFrame_]
                        && !draw.boneMatrices.empty()) {
                        const uint32_t boneSlot = skinnedUBOSlot++;
                        const uint32_t boneOff  = boneSlot * kSkinnedUBOStride;
                        if (boneOff + kSkinnedUBOStride <= kSkinnedUBOStride * kSkinnedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(skinnedUBOPtr_[currentFrame_]) + boneOff,
                                        draw.boneMatrices.data(),
                                        draw.boneMatrices.size() * sizeof(float));
                        }
                        // Task 899: 2nd dynamic UBO (fog, binding=2). vkCmdBindDescriptorSets'
                        // pDynamicOffsets are consumed in ascending-binding-number order of the
                        // set's dynamic bindings, i.e. [BoneBlock@1, FogParams@2].
                        uint32_t fogOff = 0;
                        if (skinnedFogUBOPtr_[currentFrame_]) {
                            const uint32_t fogSlot = skinnedFogUBOSlot++;
                            fogOff = fogSlot * kSkinnedFogUBOStride;
                            if (fogOff + 32 <= kSkinnedFogUBOStride * kSkinnedFogUBOMaxDraws) {
                                std::memcpy(static_cast<uint8_t*>(skinnedFogUBOPtr_[currentFrame_]) + fogOff,
                                            draw.skinnedFogUboData, 32);
                            }
                        }
                        const uint32_t dynOffsets[2] = { boneOff, fogOff };
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutSkinned3D_, 0, 1,
                                                &draw.skinnedDescSet, 2, dynOffsets);
                    }
                } else if (draw.useInstanced) {
                    vkCmdPushConstants(cb, pipelineLayoutExt3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                } else if (draw.useLitTextured) {
                    vkCmdPushConstants(cb, pipelineLayoutLitTextured3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // No defaultWhiteDescSet_ fallback here (unlike useAlphaTest) -- that set
                    // belongs to the simple 1-binding descriptorSetLayout_, structurally
                    // incompatible with this pipeline's 2-binding (sampler+UBO) layout. Mirrors
                    // useEnvMap/useSkinned/useDualTexture/useFogTex3D's own pattern.
                    if (draw.litTexturedDescSet != VK_NULL_HANDLE && litTexturedUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = litTexturedUBOSlot++;
                        const uint32_t uboOff = slot * kLitTexturedUBOStride;
                        if (uboOff + 256 <= kLitTexturedUBOStride * kLitTexturedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(litTexturedUBOPtr_[currentFrame_]) + uboOff,
                                        draw.litUboData, 256);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutLitTextured3D_, 0, 1,
                                                &draw.litTexturedDescSet, 1, &uboOff);
                    }
                } else if (draw.useFogTex3D) {
                    vkCmdPushConstants(cb, pipelineLayoutFogTex3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // No defaultWhiteDescSet_ fallback here -- that set belongs to the
                    // structurally-incompatible 1-binding descriptorSetLayout_ (mirrors
                    // useLitTextured/useSkinned/useDualTexture's own pattern); the fallback
                    // white texture is already substituted inside GetOrCreateFogTex3DDescSet.
                    if (draw.fogTex3DDescSet != VK_NULL_HANDLE && fogTex3DUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = fogTex3DUBOSlot++;
                        const uint32_t uboOff = slot * kFogTex3DUBOStride;
                        if (uboOff + 32 <= kFogTex3DUBOStride * kFogTex3DUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(fogTex3DUBOPtr_[currentFrame_]) + uboOff,
                                        draw.fogTex3DUboData, 32);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutFogTex3D_, 0, 1,
                                                &draw.fogTex3DDescSet, 1, &uboOff);
                    }
                } else {
                    // 128 bytes: see pipelineLayout3D_ creation comment (Task 364) — draw.pushConst
                    // already holds diffuseColor/vertexColorEnabled at the same float offsets
                    // FillExtPushConst() uses, filled by DrawPrimitivesEx for stride==16 draws.
                    vkCmdPushConstants(cb, pipelineLayout3D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 128, draw.pushConst);
                }
                vkCmdBindVertexBuffers(cb, 0, 1, &frame3DVB_[currentFrame_], &vbOff);
                if (draw.useInstanced && !draw.instVbData.empty()) {
                    vkCmdBindVertexBuffers(cb, 1, 1, &frame3DInstVB_[currentFrame_], &instVbOff);
                }
                if (!draw.ibData.empty()) {
                    vkCmdBindIndexBuffer(cb, frame3DIB_[currentFrame_], ibOff, draw.indexType);
                    vkCmdDrawIndexed(cb, draw.drawCount, draw.instanceCount, 0, draw.baseVertex, 0);
                    ibOff += static_cast<VkDeviceSize>(draw.ibData.size());
                } else {
                    vkCmdDraw(cb, draw.drawCount, draw.instanceCount, 0, 0);
                }
                if (draw.useInstanced)
                    instVbOff += static_cast<VkDeviceSize>(draw.instVbData.size());
                vbOff += static_cast<VkDeviceSize>(draw.vbData.size());
            }
            // Task 447/854: close a query left open at the end of this render pass's own draw
            // list (its span ran through the last tagged draw with no following untagged one).
            if (openQuery && openQuery->pool_ != VK_NULL_HANDLE) {
                vkCmdEndQuery(cb, openQuery->pool_, 0);
                openQuery->recordedThisFrame_ = true;
            }
        };

        // ---- Phase 1: off-screen RT passes ----
        // Collect unique render targets referenced this frame.
        std::vector<VulkanRTSource*> usedRTs;
        // Task 875: an RT explicitly Clear()-ed with no draw call still needs its render pass
        // recorded (matches FNA/XNA, where Clear() takes effect regardless of what's drawn
        // afterward) — otherwise its colour image never leaves VK_IMAGE_LAYOUT_UNDEFINED.
        for (auto* rt : clearedRTs_)
            if (rt && std::find(usedRTs.begin(), usedRTs.end(), rt) == usedRTs.end())
                usedRTs.push_back(rt);
        for (auto& [batch, rt] : activeBatches_)
            if (rt && std::find(usedRTs.begin(), usedRTs.end(), rt) == usedRTs.end())
                usedRTs.push_back(rt);
        for (auto& draw : pending3D_)
            if (draw.rt && std::find(usedRTs.begin(), usedRTs.end(), draw.rt) == usedRTs.end())
                usedRTs.push_back(draw.rt);

        for (auto* rt : usedRTs) {
            const uint32_t nColor = rt->GetColorAttachmentCount();
            const bool rtMsaa = rt->WantsMsaa();
            // MSAA RT render pass: att0=MSAA color, att1=resolve (unused clear value, DONT_CARE
            // loadOp), att2=depth — 3 clear values, mirroring Phase 2's hasMsaa handling below.
            // Non-MSAA: nColor color attachments + 1 depth, as before (Task 878/879).
            std::vector<VkClearValue> rtCv(rtMsaa ? 3u : (nColor + 1));
            if (rtMsaa) {
                rtCv[0].color        = { { clearR_, clearG_, clearB_, clearA_ } };
                rtCv[1].color        = {};
                rtCv[2].depthStencil = { clearDepth_, static_cast<uint32_t>(clearStencil_) };
            } else {
                for (uint32_t ci = 0; ci < nColor; ++ci)
                    rtCv[ci].color = { { clearR_, clearG_, clearB_, clearA_ } };
                rtCv[nColor].depthStencil = { clearDepth_, static_cast<uint32_t>(clearStencil_) };
            }
            VkRenderPassBeginInfo rtRp{};
            rtRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rtRp.renderPass      = rt->GetRenderPass();
            rtRp.framebuffer     = rt->GetFramebuffer();
            const uint32_t rtW   = static_cast<uint32_t>(rt->GetWidth());
            const uint32_t rtH   = static_cast<uint32_t>(rt->GetHeight());
            rtRp.renderArea      = { {0, 0}, { rtW, rtH } };
            rtRp.clearValueCount = static_cast<uint32_t>(rtCv.size());
            rtRp.pClearValues    = rtCv.data();
            vkCmdBeginRenderPass(cb, &rtRp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport rtVp{};
            rtVp.x = 0; rtVp.y = 0;
            rtVp.width  = static_cast<float>(rtW);
            rtVp.height = static_cast<float>(rtH);
            rtVp.minDepth = 0.f; rtVp.maxDepth = 1.f;
            vkCmdSetViewport(cb, 0, 1, &rtVp);
            VkRect2D rtSc{ {0, 0}, { rtW, rtH } };
            vkCmdSetScissor(cb, 0, 1, &rtSc);

            drawSpritesFor(rt, static_cast<float>(rtW), static_cast<float>(rtH));
            draw3DFor(rt);

            vkCmdEndRenderPass(cb);

            // Task 878: regenerate this RT's mip chain (no-op unless it actually owns mips).
            rt->MaybeGenerateMips(cb);
        }

        // ---- Phase 2: backbuffer pass ----
        const bool hasMsaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) && (renderPassMsaa_ != VK_NULL_HANDLE);
        // MSAA render pass: att0=MSAA color, att1=resolve(swapchain), att2=depth — 3 clear values.
        // Non-MSAA render pass: att0=swapchain color, att1=depth — 2 clear values.
        VkClearValue cv[3]{};
        cv[0].color        = { { clearR_, clearG_, clearB_, clearA_ } };
        if (hasMsaa) {
            cv[1].color        = {};
            cv[2].depthStencil = { clearDepth_, static_cast<uint32_t>(clearStencil_) };
        } else {
            cv[1].depthStencil = { clearDepth_, static_cast<uint32_t>(clearStencil_) };
        }
        VkRenderPassBeginInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass      = hasMsaa ? renderPassMsaa_ : renderPass_;
        rp.framebuffer     = swapchainFramebuffers_[imageIndex];
        rp.renderArea      = { {0, 0}, swapchainExtent_ };
        rp.clearValueCount = hasMsaa ? 3u : 2u;
        rp.pClearValues    = cv;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        if (viewportSet_ && viewportW_ > 0 && viewportH_ > 0) {
            // Task 880: honor a custom sub-region Viewport for the backbuffer pass.
            vp.x = static_cast<float>(viewportX_);
            vp.y = static_cast<float>(viewportY_);
            vp.width    = static_cast<float>(viewportW_);
            vp.height   = static_cast<float>(viewportH_);
            vp.minDepth = viewportMinDepth_;
            vp.maxDepth = viewportMaxDepth_;
        } else {
            vp.x = 0; vp.y = 0;
            vp.width  = static_cast<float>(swapchainExtent_.width);
            vp.height = static_cast<float>(swapchainExtent_.height);
            vp.minDepth = 0.f; vp.maxDepth = 1.f;
        }
        vkCmdSetViewport(cb, 0, 1, &vp);
        {
            VkRect2D sc{ {0, 0}, swapchainExtent_ };
            if (scissorEnabled_ && scissorW_ > 0 && scissorH_ > 0)
                sc = { {scissorX_, scissorY_}, {scissorW_, scissorH_} };
            vkCmdSetScissor(cb, 0, 1, &sc);
        }
        {
            float bc[4] = { blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_ };
            vkCmdSetBlendConstants(cb, bc);
        }

        const float vpW2D = (virtualWidth_  > 0) ? static_cast<float>(virtualWidth_)
                                                  : static_cast<float>(swapchainExtent_.width);
        const float vpH2D = (virtualHeight_ > 0) ? static_cast<float>(virtualHeight_)
                                                  : static_cast<float>(swapchainExtent_.height);
        drawSpritesFor(nullptr, vpW2D, vpH2D);
        draw3DFor(nullptr);

        activeBatches_.clear();
        pending3D_.clear();
        clearedRTs_.clear();

        vkCmdEndRenderPass(cb);

        // If ReadBackbuffer queued a deferred readback, copy the swapchain image
        // to the staging buffer NOW — before vkQueuePresentKHR hands the image to
        // the presentation engine. This eliminates the CPU/display-engine race.
        if (readbackPending_)
        {
            const VkDeviceSize needed = static_cast<VkDeviceSize>(readbackW_) * readbackH_ * 4;
            if (readbackStagingBuf_ == VK_NULL_HANDLE
                || readbackAllocW_ < readbackW_ || readbackAllocH_ < readbackH_)
            {
                if (readbackStagingBuf_ != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(device_, readbackStagingBuf_, nullptr);
                    vkFreeMemory(device_, readbackStagingMem_, nullptr);
                }
                CreateBuffer(needed,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             readbackStagingBuf_, readbackStagingMem_, nullptr);
                readbackAllocW_ = readbackW_;
                readbackAllocH_ = readbackH_;
            }

            auto layoutBarrier = [&](VkImage img,
                                     VkImageLayout oldL, VkImageLayout newL,
                                     VkAccessFlags srcA, VkAccessFlags dstA,
                                     VkPipelineStageFlags src, VkPipelineStageFlags dst)
            {
                VkImageMemoryBarrier b{};
                b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout           = oldL;
                b.newLayout           = newL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image               = img;
                b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                b.srcAccessMask       = srcA;
                b.dstAccessMask       = dstA;
                vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
            };

            // The render pass ended with finalLayout = PRESENT_SRC_KHR.
            // Transition to TRANSFER_SRC_OPTIMAL for the copy.
            const VkImage swImg = swapchainImages_[imageIndex];
            layoutBarrier(swImg,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset      = 0;
            region.bufferRowLength   = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageOffset       = { readbackX_, readbackY_, 0 };
            region.imageExtent       = { static_cast<uint32_t>(readbackW_),
                                         static_cast<uint32_t>(readbackH_), 1 };
            vkCmdCopyImageToBuffer(cb, swImg,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readbackStagingBuf_, 1, &region);

            // Transition back to PRESENT_SRC_KHR for vkQueuePresentKHR.
            layoutBarrier(swImg,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_ACCESS_MEMORY_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

            readbackPending_ = false;
        }

        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed");
    }

    void VulkanGraphicsBackend::Present()
    {
        if (SubmitFrame(false)) {
            // Non-deferred path already presented inside SubmitFrame.
        }
    }

    bool VulkanGraphicsBackend::SubmitFrame(bool deferSwap)
    {
        if (!initialized_) return false;

        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return false; }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed");

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
        RecordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

        VkSemaphore waitSems[]   = { imageAvailableSemaphores_[currentFrame_] };
        VkSemaphore signalSems[] = { renderFinishedSemaphores_[currentFrame_] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = waitSems;
        si.pWaitDstStageMask    = waitStages;
        si.commandBufferCount   = 1; si.pCommandBuffers   = &commandBuffers_[currentFrame_];
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;
        if (vkQueueSubmit(graphicsQueue_, 1, &si, inFlightFences_[currentFrame_]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed");

        if (deferSwap) {
            // Wait for render + readback copy to complete, but hold the image. The caller
            // (ReadBackbuffer) reads the staging buffer before the image is presented, so
            // presentation-engine timing can never corrupt the captured pixels.
            vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
            deferredPresentImageIndex_ = imageIndex;
            hasDeferredPresent_        = true;
            return true;
        }

        VkSwapchainKHR sc[] = { swapchain_ };
        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
        pi.swapchainCount     = 1; pi.pSwapchains     = sc;
        pi.pImageIndices      = &imageIndex;
        result = vkQueuePresentKHR(presentQueue_, &pi);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            RecreateSwapchain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        lastPresentedImageIndex_ = imageIndex;
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
        return true;
    }

    void VulkanGraphicsBackend::FinishDeferredPresent()
    {
        if (!hasDeferredPresent_) return;
        hasDeferredPresent_ = false;

        uint32_t imageIndex = deferredPresentImageIndex_;
        VkSemaphore signalSems[] = { renderFinishedSemaphores_[currentFrame_] };
        VkSwapchainKHR sc[] = { swapchain_ };
        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        // The submit already completed (fence-waited), so the renderFinished semaphore is
        // signalled and the present will not block.
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
        pi.swapchainCount     = 1; pi.pSwapchains     = sc;
        pi.pImageIndices      = &imageIndex;
        VkResult result = vkQueuePresentKHR(presentQueue_, &pi);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            RecreateSwapchain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        lastPresentedImageIndex_ = imageIndex;
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void VulkanGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!initialized_ || swapchainImages_.empty()) return;

        const int fullW = static_cast<int>(swapchainExtent_.width);
        const int fullH = static_cast<int>(swapchainExtent_.height);
        if (fullW <= 0 || fullH <= 0) return;

        // Capture the WHOLE backbuffer into the staging buffer only when there is new
        // work to render, or when the cache is stale (a new frame was cleared). This lets
        // several GetBackBufferData() reads of the same frame be served from the cache
        // without re-presenting — re-presenting an empty queue would re-render a cleared
        // frame and destroy the content of all but the first read.
        const bool hasNewWork = !pending3D_.empty() || !activeBatches_.empty();
        if (hasNewWork || !readbackStagingValid_) {
            // Deferred copy: RecordCommandBuffer copies the whole swapchain image into
            // readbackStagingBuf_ before vkQueuePresentKHR. SubmitFrame(true) renders and
            // waits for the GPU but HOLDS the present, so we read the staging buffer below
            // BEFORE the image is handed to the presentation engine — no race possible.
            readbackX_ = 0; readbackY_ = 0; readbackW_ = fullW; readbackH_ = fullH;
            readbackPending_ = true;
            if (!SubmitFrame(true)) {
                // Swapchain out-of-date (common on first frame under Wayland/RADV); the
                // staging buffer was not written. Zero the output so the caller can detect
                // a blank frame and retry, rather than seeing stale data.
                readbackPending_ = false;
                std::memset(pixels, 0, static_cast<std::size_t>(w) * h * 4);
                return;
            }
            readbackStagingValid_ = true;
        }

        // Serve the requested sub-region from the cached full-frame staging buffer,
        // handling the BGRA → RGBA channel swap.
        const VkDeviceSize fullSize = static_cast<VkDeviceSize>(fullW) * fullH * 4;
        void* mapped = nullptr;
        vkMapMemory(device_, readbackStagingMem_, 0, fullSize, 0, &mapped);
        const auto* src = static_cast<const uint8_t*>(mapped);
        const bool isBGRA = (swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB);
        for (int row = 0; row < h; ++row) {
            const int sy = y + row;
            for (int col = 0; col < w; ++col) {
                const int sx = x + col;
                uint8_t* d = pixels + (static_cast<std::size_t>(row) * w + col) * 4;
                if (sx < 0 || sx >= fullW || sy < 0 || sy >= fullH) {
                    d[0] = d[1] = d[2] = d[3] = 0;
                    continue;
                }
                const uint8_t* s = src + (static_cast<std::size_t>(sy) * fullW + sx) * 4;
                if (isBGRA) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            }
        }
        vkUnmapMemory(device_, readbackStagingMem_);

        // Now that the pixels are safely captured, present the held image.
        FinishDeferredPresent();
    }

    void VulkanGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        // Real fix for the reported resize/viewport bug: this used to call
        // SDL_GetWindowSize() (logical/DPI-scaled "points"), while the swapchain itself is
        // always created in PHYSICAL pixels (CreateSwapchain() uses caps.currentExtent or
        // SDL_GetWindowSizeInPixels() as a fallback -- both real device pixels). On any
        // display where the OS DPI scale != 1.0 (mobile devices, Retina, Wayland fractional
        // scaling), that mismatch made GraphicsDevice::UpdateViewportFromWindow() compute a
        // Viewport smaller than the real framebuffer, rendering into only a corner of the
        // screen.
        //
        // Deliberately still a LIVE query (matching CreateSwapchain()'s own fallback), not a
        // read of the cached swapchainExtent_ member -- an earlier version of this fix read
        // swapchainExtent_ directly, which regressed 51 gtest cases (TextureCubeTest,
        // AlphaTestReferenceScalingTest, etc., confirmed via git-stash to be clean on the
        // pre-fix baseline): those tests construct many short-lived GraphicsDevice/window
        // instances in quick succession, and swapchainExtent_ only reflects whatever the
        // window's real size happened to be at the moment CreateSwapchain() last ran --
        // wrong/stale if the window wasn't fully realized by the windowing system yet at
        // that exact moment, with nothing to refresh it until a future resize event. A live
        // SDL_GetWindowSizeInPixels() query self-corrects on every call, exactly like the
        // original SDL_GetWindowSize() call this replaces -- just in the correct (physical,
        // not logical) units.
        SDL_GetWindowSizeInPixels(window_, &width, &height);
    }

    void VulkanGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_  = width;
        virtualHeight_ = height;
        if (initialized_ && width > 0 && height > 0)
            RecreateSwapchain();
    }

    int VulkanGraphicsBackend::GetMultiSampleCount() const
    {
        return SampleCountToInt(sampleCount_);
    }

    int VulkanGraphicsBackend::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const VkSampleCountFlagBits newCount = PickSampleCount(physicalDevice_, requestedMultiSampleCount);
        if (newCount == sampleCount_)
            return SampleCountToInt(sampleCount_);

        vkDeviceWaitIdle(device_);

        // Tear down every piece of state whose creation baked in the OLD sampleCount_ --
        // the backbuffer's MSAA render pass, every depth-format-keyed render-target/2D-sprite
        // MSAA render pass/pipeline (Task 911 — lazily recreated on next MSAA-enabled use,
        // regardless of depth format), and every lazily-created 3D pipeline (each VkPipeline
        // hardcodes rasterizationSamples at creation time). The sample-count-independent
        // renderPass_/pipelines2DByDepthFmt_/rtRenderPassByDepthFmt_/rtRenderPassLoadByDepthFmt_
        // are left untouched.
        auto clearPipelineCache = [this](auto& cache) {
            for (auto& [key, pipe] : cache)
                if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipe, nullptr);
            cache.clear();
        };
        clearPipelineCache(pipelines2DMsaaByDepthFmt_);
        if (renderPassMsaa_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPassMsaa_, nullptr); renderPassMsaa_ = VK_NULL_HANDLE; }
        for (auto& [fmt, rp] : rtRenderPassMsaaByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaByDepthFmt_.clear();
        clearPipelineCache(pipelines3D_);
        clearPipelineCache(pipelinesAlphaTest3D_);
        clearPipelineCache(pipelinesDualTex3D_);
        clearPipelineCache(pipelinesEnvMap3D_);
        clearPipelineCache(pipelinesLitTextured3D_);
        clearPipelineCache(pipelinesFogColored3D_);
        clearPipelineCache(pipelinesFogTex3D_);
        clearPipelineCache(pipelinesSkinned3D_);
        clearPipelineCache(pipelinesInstanced3D_);

        sampleCount_ = newCount;

        // CreateFramebuffers() (called by RecreateSwapchain() below) reads renderPassMsaa_
        // directly whenever sampleCount_ > 1, so it must already exist before that call. The 2D
        // MSAA sprite pipeline(s) are lazily recreated per depth format on next use instead
        // (Task 911), same as every 3D pipeline.
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) {
            CreateRenderPassMsaa();
        }
        RecreateSwapchain();

        SDL_Log("[Vulkan] MultiSampleCount reset to %d×", SampleCountToInt(sampleCount_));
        return SampleCountToInt(sampleCount_);
    }

    std::unique_ptr<ITextureBackend> VulkanGraphicsBackend::CreateTexture(const ImageData& data)
    {
        auto tex = std::make_unique<VulkanTextureBackend>(data, this);
        liveTextures_.push_back(tex.get());
        return tex;
    }

    std::unique_ptr<ISpriteBatchBackend> VulkanGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<VulkanSpriteBatchBackend>(this);
    }

    std::unique_ptr<IRenderTargetBackend> VulkanGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // multiSampleCount is honored on a "piggyback on the backend's own sampleCount_" basis
        // (Task 878/879) — see VulkanRenderTargetBackend's constructor comment and
        // plan_graphics.md for the exact scope decision. mipMap (Task 878) is a real
        // vkCmdBlitImage cascade regenerated every frame this RT is rendered into — see
        // VulkanRenderTargetBackend::MaybeGenerateMips. depthFormat (Task 877) now gets true
        // per-instance fidelity (Task 911) — see VulkanRenderTargetBackend's constructor comment.
        return std::make_unique<VulkanRenderTargetBackend>(w, h, depthFormat, preserveContents, this,
                                                            multiSampleCount, mipMap);
    }

    void VulkanGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt == nullptr) {
            currentRT_ = nullptr;
        } else {
            auto* vrt = static_cast<VulkanRenderTargetBackend*>(rt);
            currentRT_ = vrt;
            vrt->BindAsRenderTarget();
        }
    }

    std::unique_ptr<IVertexBufferBackend> VulkanGraphicsBackend::CreateVertexBuffer(int cap)
    {
        auto vb = std::make_unique<VulkanVertexBufferBackend>(cap, this);
        liveVertexBuffers_.push_back(vb.get());
        return vb;
    }

    std::unique_ptr<IIndexBufferBackend> VulkanGraphicsBackend::CreateIndexBuffer16(int cap)
    {
        auto ib = std::make_unique<VulkanIndexBufferBackend>(cap, false, this);
        liveIndexBuffers_.push_back(ib.get());
        return ib;
    }

    std::unique_ptr<IIndexBufferBackend> VulkanGraphicsBackend::CreateIndexBuffer32(int cap)
    {
        auto ib = std::make_unique<VulkanIndexBufferBackend>(cap, true, this);
        liveIndexBuffers_.push_back(ib.get());
        return ib;
    }

    // ---- 3D pipeline ----

    void VulkanGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearDepth_ = depth;
        readbackStagingValid_ = false;  // new frame content invalidates the readback cache
        // Task 875: see Clear()'s identical fix.
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::ClearDepth(float depth) { clearDepth_ = depth; readbackStagingValid_ = false; /* Vulkan depth-only clear not yet implemented */ }

    // Task 871: every render pass this backend records already unconditionally clears the
    // color/depth attachments every frame (see RecordCommandBuffer()'s hardcoded loadOp=CLEAR),
    // so -- mirroring that existing (if imperfect) design rather than introducing new
    // per-request selectivity -- these new stencil-aware entry points just update clearStencil_
    // (read by RecordCommandBuffer()'s VkClearValue construction) and mark the currently-bound RT
    // dirty exactly like Clear()/ClearColorAndDepth() already do (Task 875).
    void VulkanGraphicsBackend::ClearStencil(int stencil)
    {
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        clearDepth_ = depth;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearDepth_ = depth;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        if (currentRT_ && std::find(clearedRTs_.begin(), clearedRTs_.end(), currentRT_) == clearedRTs_.end())
            clearedRTs_.push_back(currentRT_);
    }

    void VulkanGraphicsBackend::SetDepthTestEnabled(bool v)  { depthTestEnabled_  = v; }
    void VulkanGraphicsBackend::SetBlendEnabled(bool v)      { blendEnabled_      = v; }
    void VulkanGraphicsBackend::SetDepthWriteEnabled(bool v) { depthWriteEnabled_ = v; }

    void VulkanGraphicsBackend::PushPending3DDraw(Pending3DDraw&& d)
    {
        d.occlusionQuery = activeOcclusionQuery_;
        pending3D_.push_back(std::move(d));
    }

    void VulkanGraphicsBackend::SetStringMarkerEXT(const char* marker)
    {
        if (!marker || !marker[0]) return;
        Pending3DDraw m;
        m.isMarker   = true;
        m.markerLabel = marker;
        m.rt          = currentRT_;
        PushPending3DDraw(std::move(m));
    }

    void VulkanGraphicsBackend::DrawColoredPrimitives(
        const IVertexBufferBackend& vb,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& vulkanVB = static_cast<const VulkanVertexBufferBackend&>(vb);
        uint32_t drawCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        std::size_t stride = vulkanVB.GetStride() > 0 ? vulkanVB.GetStride() : 16;

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(d.pushConst);
        // This path carries no BasicEffect diffuse/VertexColorEnabled (no GpuDrawParams at
        // all); preserve the historical behavior of outputting the raw vertex colors
        // unmodified (diffuseColor=white, vertexColorEnabled=true — Task 364).
        d.pushConst[16] = 1.0f; d.pushConst[17] = 1.0f; d.pushConst[18] = 1.0f; d.pushConst[19] = 1.0f;
        d.pushConst[31] = 1.0f;

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(), drawCount * stride);

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = drawCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend      = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType  = VK_INDEX_TYPE_UINT16;  // non-indexed, not used
        d.rt         = currentRT_;
        PushPending3DDraw(std::move(d));
    }

    void VulkanGraphicsBackend::DrawIndexedColoredPrimitives(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& vulkanVB = static_cast<const VulkanVertexBufferBackend&>(vb);
        const auto& vulkanIB = static_cast<const VulkanIndexBufferBackend&>(ib);
        uint32_t indexCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        std::size_t stride  = vulkanVB.GetStride() > 0 ? vulkanVB.GetStride() : 16;
        int vertexCount     = vulkanVB.GetVertexCount();

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(d.pushConst);
        // See DrawColoredPrimitives above: preserve the historical raw-vertex-color output
        // for this no-GpuDrawParams legacy path (Task 364).
        d.pushConst[16] = 1.0f; d.pushConst[17] = 1.0f; d.pushConst[18] = 1.0f; d.pushConst[19] = 1.0f;
        d.pushConst[31] = 1.0f;

        d.vbData.resize(static_cast<std::size_t>(vertexCount) * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * stride);

        d.ibData.resize(static_cast<std::size_t>(indexCount) * sizeof(uint16_t));
        std::memcpy(d.ibData.data(), vulkanIB.GetMappedPtr(),
                    static_cast<std::size_t>(indexCount) * sizeof(uint16_t));

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = indexCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend      = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType  = vulkanIB.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt         = currentRT_;
        PushPending3DDraw(std::move(d));
    }

    // ---- Extended 3D draws (Tasks 53-55) ----

    void VulkanGraphicsBackend::DrawPrimitivesEx(
        const IVertexBufferBackend& vb_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        EnsureDefaultWhiteTexture();
        const auto& vb = static_cast<const VulkanVertexBufferBackend&>(vb_in);
        const std::size_t stride = vb.GetStride() > 0 ? vb.GetStride() : 20;
        const uint32_t drawCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));

        const bool needsAlphaTest  = (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex    = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap     = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned    = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // stride==32 always uses the lit-textured shader (BasicEffect's VertexPositionNormalTexture
        // path, lit or not — the shader itself branches on lightingEnabled), unless another
        // effect (alpha test/dual tex/env map/skinned) takes priority for this stride.
        const bool needsLitTextured = (stride == 32) && !needsAlphaTest && !needsDualTex
                                     && !needsEnvMap && !needsSkinned;

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext, lit-textured, and skinned (same PC)
        }

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(),
                    static_cast<const uint8_t*>(vb.GetMappedPtr()) + params.vertexStart * stride,
                    drawCount * stride);

        d.topology       = ToVkTopology(primitive);
        d.drawCount      = drawCount;
        d.depthTest      = depthTestEnabled_;
        d.depthWrite     = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend          = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode       = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType      = VK_INDEX_TYPE_UINT16;
        d.rt             = currentRT_;
        d.stride         = stride;
        // Task 899: all BasicEffect draws that reach neither alpha-test/dual-tex/env-map/skinned
        // nor lit-textured (stride 16/20/24) now route through the fog-capable colored3d/
        // textured3d/colored_textured3d bundle instead of the old zero-fog pipelines.
        d.useFogTex3D    = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                         && !needsLitTextured;
        d.useDualTexture = needsDualTex;
        d.useSkinned     = needsSkinned;
        d.useLitTextured = needsLitTextured;
        if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            d.skinnedFogUboData[0] = params.fogColor[0]; d.skinnedFogUboData[1] = params.fogColor[1];
            d.skinnedFogUboData[2] = params.fogColor[2]; d.skinnedFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.skinnedFogUboData[4] = params.fogStart; d.skinnedFogUboData[5] = params.fogEnd;
            d.skinnedFogUboData[6] = 0.f; d.skinnedFogUboData[7] = 0.f;
        } else if (needsEnvMap) {
            EnsureEnvMapResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vtc = dynamic_cast<const IVulkanCubeSamplable*>(params.envMap);
            VkImageView v2d  = vs0 ? vs0->GetVkImageView()       : defaultWhiteView_;
            VkImageView vcub = vtc ? vtc->GetVkCubeImageView()    : defaultWhiteCubeView_;
            d.envMapDescSet  = GetOrCreateEnvMapDescSet(currentFrame_, v2d, vcub);
            // Pack UBO data: eyePos, diffuse, emissive+envMapAmount, light0Dir,
            // light0Diff+fresnelEnabled, envMapSpecular+fresnelFactor
            d.envMapUboData[0]  = params.eyePositionWorld[0];
            d.envMapUboData[1]  = params.eyePositionWorld[1];
            d.envMapUboData[2]  = params.eyePositionWorld[2];
            d.envMapUboData[3]  = 0.f;
            d.envMapUboData[4]  = params.diffuseColor[0]; d.envMapUboData[5]  = params.diffuseColor[1];
            d.envMapUboData[6]  = params.diffuseColor[2]; d.envMapUboData[7]  = params.diffuseColor[3];
            d.envMapUboData[8]  = params.emissiveColor[0]; d.envMapUboData[9]  = params.emissiveColor[1];
            d.envMapUboData[10] = params.emissiveColor[2]; d.envMapUboData[11] = params.envMapAmount;
            d.envMapUboData[12] = params.light0Dir[0]; d.envMapUboData[13] = params.light0Dir[1];
            d.envMapUboData[14] = params.light0Dir[2]; d.envMapUboData[15] = 0.f;
            d.envMapUboData[16] = params.light0Diffuse[0]; d.envMapUboData[17] = params.light0Diffuse[1];
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = params.fresnelEnabled ? 1.f : 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = params.fresnelFactor;
            // Task 899's noted cheap leftover: fog packed into EnvMapParams' spare tail bytes.
            d.envMapUboData[24] = params.fogColor[0]; d.envMapUboData[25] = params.fogColor[1];
            d.envMapUboData[26] = params.fogColor[2]; d.envMapUboData[27] = params.fogEnabled ? 1.f : 0.f;
            d.envMapUboData[28] = params.fogStart; d.envMapUboData[29] = params.fogEnd;
            d.envMapUboData[30] = 0.f; d.envMapUboData[31] = 0.f;
        } else if (needsDualTex) {
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(currentFrame_, v0, v1, slotSamplers_[0], slotSamplers_[1]);
            d.dualTexFogUboData[0] = params.fogColor[0]; d.dualTexFogUboData[1] = params.fogColor[1];
            d.dualTexFogUboData[2] = params.fogColor[2]; d.dualTexFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.dualTexFogUboData[4] = params.fogStart; d.dualTexFogUboData[5] = params.fogEnd;
            d.dualTexFogUboData[6] = 0.f; d.dualTexFogUboData[7] = 0.f;
        } else if (needsLitTextured) {
            EnsureLitTexturedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.litTexturedDescSet = GetOrCreateLitTexturedDescSet(currentFrame_, view);
            // Pack UBO data: light1Dir+pad, light1Diffuse+pad, light2Dir+pad, light2Diffuse+pad,
            // emissiveColor+pad.
            d.litUboData[0]  = params.light1Dir[0];     d.litUboData[1]  = params.light1Dir[1];
            d.litUboData[2]  = params.light1Dir[2];     d.litUboData[3]  = 0.f;
            d.litUboData[4]  = params.light1Diffuse[0]; d.litUboData[5]  = params.light1Diffuse[1];
            d.litUboData[6]  = params.light1Diffuse[2]; d.litUboData[7]  = 0.f;
            d.litUboData[8]  = params.light2Dir[0];     d.litUboData[9]  = params.light2Dir[1];
            d.litUboData[10] = params.light2Dir[2];     d.litUboData[11] = 0.f;
            d.litUboData[12] = params.light2Diffuse[0]; d.litUboData[13] = params.light2Diffuse[1];
            d.litUboData[14] = params.light2Diffuse[2]; d.litUboData[15] = 0.f;
            d.litUboData[16] = params.emissiveColor[0]; d.litUboData[17] = params.emissiveColor[1];
            d.litUboData[18] = params.emissiveColor[2]; d.litUboData[19] = 0.f;
            // World matrix (Task 898: needed by the vertex shader for a correct world-space
            // position/normal, since the 128-byte PC has no spare room for it).
            for (int wi = 0; wi < 16; ++wi) d.litUboData[20 + wi] = params.worldColMajor[wi];
            d.litUboData[36] = params.eyePositionWorld[0]; d.litUboData[37] = params.eyePositionWorld[1];
            d.litUboData[38] = params.eyePositionWorld[2]; d.litUboData[39] = 0.f;
            d.litUboData[40] = params.light0Specular[0]; d.litUboData[41] = params.light0Specular[1];
            d.litUboData[42] = params.light0Specular[2]; d.litUboData[43] = 0.f;
            d.litUboData[44] = params.light1Specular[0]; d.litUboData[45] = params.light1Specular[1];
            d.litUboData[46] = params.light1Specular[2]; d.litUboData[47] = 0.f;
            d.litUboData[48] = params.light2Specular[0]; d.litUboData[49] = params.light2Specular[1];
            d.litUboData[50] = params.light2Specular[2]; d.litUboData[51] = 0.f;
            d.litUboData[52] = params.specularColor[0]; d.litUboData[53] = params.specularColor[1];
            d.litUboData[54] = params.specularColor[2]; d.litUboData[55] = params.specularPower;
            d.litUboData[56] = params.fogColor[0]; d.litUboData[57] = params.fogColor[1];
            d.litUboData[58] = params.fogColor[2]; d.litUboData[59] = params.fogEnabled ? 1.f : 0.f;
            d.litUboData[60] = params.fogStart; d.litUboData[61] = params.fogEnd;
            d.litUboData[62] = 0.f; d.litUboData[63] = 0.f;
        } else {
            // Shared fallback fill: reached both by alpha-test draws (whose pipeline also uses
            // the plain single-sampler descriptorSetLayout_/d.descSet) and, when !needsAlphaTest,
            // by the colored3d/textured3d/colored_textured3d fog-capable bundle (Task 899).
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
            if (d.useFogTex3D) {
                EnsureFogTex3DResources();
                d.fogTex3DDescSet = GetOrCreateFogTex3DDescSet(currentFrame_, view);
                d.fogTex3DUboData[0] = params.fogColor[0]; d.fogTex3DUboData[1] = params.fogColor[1];
                d.fogTex3DUboData[2] = params.fogColor[2]; d.fogTex3DUboData[3] = params.fogEnabled ? 1.f : 0.f;
                d.fogTex3DUboData[4] = params.fogStart; d.fogTex3DUboData[5] = params.fogEnd;
                d.fogTex3DUboData[6] = 0.f; d.fogTex3DUboData[7] = 0.f;
            }
        }
        PushPending3DDraw(std::move(d));
    }

    void VulkanGraphicsBackend::DrawIndexedPrimitivesEx(
        const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        EnsureDefaultWhiteTexture();
        const auto& vb = static_cast<const VulkanVertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const VulkanIndexBufferBackend&>(ib_in);
        const std::size_t stride  = vb.GetStride() > 0 ? vb.GetStride() : 20;
        const uint32_t indexCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        const int vertexCount     = vb.GetVertexCount();

        const bool needsAlphaTest = (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex   = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap    = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned   = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        const bool needsLitTextured = (stride == 32) && !needsAlphaTest && !needsDualTex
                                     && !needsEnvMap && !needsSkinned;

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext, lit-textured, and skinned (same PC)
        }

        d.vbData.resize(static_cast<std::size_t>(vertexCount) * stride);
        std::memcpy(d.vbData.data(), vb.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * stride);
        const int indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        d.ibData.resize(static_cast<std::size_t>(indexCount) * indexSize);
        std::memcpy(d.ibData.data(),
                    static_cast<const uint8_t*>(ib.GetMappedPtr()) + params.startIndex * indexSize,
                    static_cast<std::size_t>(indexCount) * indexSize);
        d.baseVertex = static_cast<int32_t>(params.baseVertex);

        d.topology      = ToVkTopology(primitive);
        d.drawCount     = indexCount;
        d.depthTest     = depthTestEnabled_;
        d.depthWrite    = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend         = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode      = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType     = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt            = currentRT_;
        d.stride        = stride;
        // Task 899: see DrawPrimitivesEx's identical comment above.
        d.useFogTex3D   = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                        && !needsLitTextured;
        d.useDualTexture = needsDualTex;
        d.useSkinned     = needsSkinned;
        d.useLitTextured = needsLitTextured;
        if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            d.skinnedFogUboData[0] = params.fogColor[0]; d.skinnedFogUboData[1] = params.fogColor[1];
            d.skinnedFogUboData[2] = params.fogColor[2]; d.skinnedFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.skinnedFogUboData[4] = params.fogStart; d.skinnedFogUboData[5] = params.fogEnd;
            d.skinnedFogUboData[6] = 0.f; d.skinnedFogUboData[7] = 0.f;
        } else if (needsEnvMap) {
            EnsureEnvMapResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vtc = dynamic_cast<const IVulkanCubeSamplable*>(params.envMap);
            VkImageView v2d  = vs0 ? vs0->GetVkImageView()       : defaultWhiteView_;
            VkImageView vcub = vtc ? vtc->GetVkCubeImageView()    : defaultWhiteCubeView_;
            d.envMapDescSet  = GetOrCreateEnvMapDescSet(currentFrame_, v2d, vcub);
            d.envMapUboData[0]  = params.eyePositionWorld[0];
            d.envMapUboData[1]  = params.eyePositionWorld[1];
            d.envMapUboData[2]  = params.eyePositionWorld[2];
            d.envMapUboData[3]  = 0.f;
            d.envMapUboData[4]  = params.diffuseColor[0]; d.envMapUboData[5]  = params.diffuseColor[1];
            d.envMapUboData[6]  = params.diffuseColor[2]; d.envMapUboData[7]  = params.diffuseColor[3];
            d.envMapUboData[8]  = params.emissiveColor[0]; d.envMapUboData[9]  = params.emissiveColor[1];
            d.envMapUboData[10] = params.emissiveColor[2]; d.envMapUboData[11] = params.envMapAmount;
            d.envMapUboData[12] = params.light0Dir[0]; d.envMapUboData[13] = params.light0Dir[1];
            d.envMapUboData[14] = params.light0Dir[2]; d.envMapUboData[15] = 0.f;
            d.envMapUboData[16] = params.light0Diffuse[0]; d.envMapUboData[17] = params.light0Diffuse[1];
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = params.fresnelEnabled ? 1.f : 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = params.fresnelFactor;
            // Task 899's noted cheap leftover: fog packed into EnvMapParams' spare tail bytes.
            d.envMapUboData[24] = params.fogColor[0]; d.envMapUboData[25] = params.fogColor[1];
            d.envMapUboData[26] = params.fogColor[2]; d.envMapUboData[27] = params.fogEnabled ? 1.f : 0.f;
            d.envMapUboData[28] = params.fogStart; d.envMapUboData[29] = params.fogEnd;
            d.envMapUboData[30] = 0.f; d.envMapUboData[31] = 0.f;
        } else if (needsDualTex) {
            EnsureDualTexResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(currentFrame_, v0, v1, slotSamplers_[0], slotSamplers_[1]);
            d.dualTexFogUboData[0] = params.fogColor[0]; d.dualTexFogUboData[1] = params.fogColor[1];
            d.dualTexFogUboData[2] = params.fogColor[2]; d.dualTexFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.dualTexFogUboData[4] = params.fogStart; d.dualTexFogUboData[5] = params.fogEnd;
            d.dualTexFogUboData[6] = 0.f; d.dualTexFogUboData[7] = 0.f;
        } else if (needsLitTextured) {
            EnsureLitTexturedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.litTexturedDescSet = GetOrCreateLitTexturedDescSet(currentFrame_, view);
            d.litUboData[0]  = params.light1Dir[0];     d.litUboData[1]  = params.light1Dir[1];
            d.litUboData[2]  = params.light1Dir[2];     d.litUboData[3]  = 0.f;
            d.litUboData[4]  = params.light1Diffuse[0]; d.litUboData[5]  = params.light1Diffuse[1];
            d.litUboData[6]  = params.light1Diffuse[2]; d.litUboData[7]  = 0.f;
            d.litUboData[8]  = params.light2Dir[0];     d.litUboData[9]  = params.light2Dir[1];
            d.litUboData[10] = params.light2Dir[2];     d.litUboData[11] = 0.f;
            d.litUboData[12] = params.light2Diffuse[0]; d.litUboData[13] = params.light2Diffuse[1];
            d.litUboData[14] = params.light2Diffuse[2]; d.litUboData[15] = 0.f;
            d.litUboData[16] = params.emissiveColor[0]; d.litUboData[17] = params.emissiveColor[1];
            d.litUboData[18] = params.emissiveColor[2]; d.litUboData[19] = 0.f;
            // World matrix (Task 898: needed by the vertex shader for a correct world-space
            // position/normal, since the 128-byte PC has no spare room for it).
            for (int wi = 0; wi < 16; ++wi) d.litUboData[20 + wi] = params.worldColMajor[wi];
            d.litUboData[36] = params.eyePositionWorld[0]; d.litUboData[37] = params.eyePositionWorld[1];
            d.litUboData[38] = params.eyePositionWorld[2]; d.litUboData[39] = 0.f;
            d.litUboData[40] = params.light0Specular[0]; d.litUboData[41] = params.light0Specular[1];
            d.litUboData[42] = params.light0Specular[2]; d.litUboData[43] = 0.f;
            d.litUboData[44] = params.light1Specular[0]; d.litUboData[45] = params.light1Specular[1];
            d.litUboData[46] = params.light1Specular[2]; d.litUboData[47] = 0.f;
            d.litUboData[48] = params.light2Specular[0]; d.litUboData[49] = params.light2Specular[1];
            d.litUboData[50] = params.light2Specular[2]; d.litUboData[51] = 0.f;
            d.litUboData[52] = params.specularColor[0]; d.litUboData[53] = params.specularColor[1];
            d.litUboData[54] = params.specularColor[2]; d.litUboData[55] = params.specularPower;
            d.litUboData[56] = params.fogColor[0]; d.litUboData[57] = params.fogColor[1];
            d.litUboData[58] = params.fogColor[2]; d.litUboData[59] = params.fogEnabled ? 1.f : 0.f;
            d.litUboData[60] = params.fogStart; d.litUboData[61] = params.fogEnd;
            d.litUboData[62] = 0.f; d.litUboData[63] = 0.f;
        } else {
            // Shared fallback fill: reached both by alpha-test draws (whose pipeline also uses
            // the plain single-sampler descriptorSetLayout_/d.descSet) and, when !needsAlphaTest,
            // by the colored3d/textured3d/colored_textured3d fog-capable bundle (Task 899).
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
            if (d.useFogTex3D) {
                EnsureFogTex3DResources();
                d.fogTex3DDescSet = GetOrCreateFogTex3DDescSet(currentFrame_, view);
                d.fogTex3DUboData[0] = params.fogColor[0]; d.fogTex3DUboData[1] = params.fogColor[1];
                d.fogTex3DUboData[2] = params.fogColor[2]; d.fogTex3DUboData[3] = params.fogEnabled ? 1.f : 0.f;
                d.fogTex3DUboData[4] = params.fogStart; d.fogTex3DUboData[5] = params.fogEnd;
                d.fogTex3DUboData[6] = 0.f; d.fogTex3DUboData[7] = 0.f;
            }
        }
        PushPending3DDraw(std::move(d));
    }

    void VulkanGraphicsBackend::DrawInstancedPrimitivesEx(
        const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
        if (params.instanceVb == nullptr) {
            // No per-instance VB — fall back to single-instance indexed draw.
            DrawIndexedPrimitivesEx(vb_in, ib_in, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        EnsureDefaultWhiteTexture();
        EnsureFrame3DInstBuffers();

        const auto& vb       = static_cast<const VulkanVertexBufferBackend&>(vb_in);
        const auto& ib       = static_cast<const VulkanIndexBufferBackend&>(ib_in);
        const auto& instVb   = static_cast<const VulkanVertexBufferBackend&>(*params.instanceVb);
        const std::size_t pvStride   = vb.GetStride() > 0 ? vb.GetStride() : 20;
        const std::size_t instStride = instVb.GetStride() > 0 ? instVb.GetStride() : 64;
        const uint32_t indexCount    = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        const int vertexCount        = vb.GetVertexCount();
        const int instCountClamped   = std::max(1, instanceCount);

        Pending3DDraw d{};
        FillInstancedPushConst(d.pushConst, view, projection, params);

        // Copy per-vertex data (all vertices)
        d.vbData.resize(static_cast<std::size_t>(vertexCount) * pvStride);
        std::memcpy(d.vbData.data(), vb.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * pvStride);

        // Copy index data (with startIndex offset)
        const int indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        d.ibData.resize(static_cast<std::size_t>(indexCount) * indexSize);
        std::memcpy(d.ibData.data(),
                    static_cast<const uint8_t*>(ib.GetMappedPtr()) + params.startIndex * indexSize,
                    static_cast<std::size_t>(indexCount) * indexSize);

        // Copy per-instance data (instanceCount entries)
        d.instVbData.resize(static_cast<std::size_t>(instCountClamped) * instStride);
        std::memcpy(d.instVbData.data(), instVb.GetMappedPtr(),
                    d.instVbData.size());

        d.topology     = ToVkTopology(primitive);
        d.drawCount    = indexCount;
        d.depthTest    = depthTestEnabled_;
        d.depthWrite   = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend        = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode     = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType    = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt           = currentRT_;
        d.stride       = pvStride;
        d.instVbStride = instStride;
        d.instanceCount = static_cast<uint32_t>(instCountClamped);
        d.baseVertex   = static_cast<int32_t>(params.baseVertex);
        d.useInstanced = true;
        d.descSet      = defaultWhiteDescSet_;  // no per-draw texture for now
        PushPending3DDraw(std::move(d));
    }

    // ---- Graphics state ----

    void VulkanGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc)
    {
        // Blend::One=0, Blend::Zero=1 → Opaque preset: src=One, dst=Zero → no blending
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1);
        // Task 868: previously every one of these 6 real values was discarded except for the
        // enabled/disabled boolean above -- every pipeline hardcoded BlendState.NonPremultiplied's
        // own equation whenever blending was on at all. Now stored for real use by
        // FillBlendAttachmentState() at pipeline-creation time.
        blendParams_.colorSrc  = colorSrcBlend;
        blendParams_.colorDst  = colorDstBlend;
        blendParams_.alphaSrc  = alphaSrcBlend;
        blendParams_.alphaDst  = alphaDstBlend;
        blendParams_.colorFunc = colorBlendFunc;
        blendParams_.alphaFunc = alphaBlendFunc;
    }

    void VulkanGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail,
                                                        int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask,
                                                        int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        depthTestEnabled_  = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        dsParams_.depthFunc           = depthFunc;
        dsParams_.stencilEnable       = stencilEnable;
        dsParams_.stencilFunc         = stencilFunc;
        dsParams_.stencilFail         = stencilFail;
        dsParams_.stencilDepthFail    = stencilDepthFail;
        dsParams_.stencilPass         = stencilPass;
        dsParams_.twoSidedStencilMode = twoSidedStencilMode;
        dsParams_.ccwStencilFunc      = ccwStencilFunc;
        dsParams_.ccwStencilFail      = ccwStencilFail;
        dsParams_.ccwStencilDepthFail = ccwStencilDepthFail;
        dsParams_.ccwStencilPass      = ccwStencilPass;
        stencilReadMask_  = stencilMask;
        stencilWriteMask_ = stencilWriteMask;
        referenceStencil_ = referenceStencil;
    }

    void VulkanGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // XNA FillMode: Solid=0, WireFrame=1
        cullMode_            = cullMode;
        fillModeWireframe_   = (fillMode == 1) && fillModeNonSolidSupported_;
        scissorEnabled_      = scissorTestEnable;
        // DepthBias maps to vkCmdSetDepthBias constant factor, SlopeScaleDepthBias to the
        // slope factor — matching FNA's glPolygonOffset(slopeScaleDepthBias, depthBias).
        depthBias_           = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void VulkanGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = static_cast<int32_t>(x);
        scissorY_ = static_cast<int32_t>(y);
        scissorW_ = static_cast<uint32_t>(std::max(0, w));
        scissorH_ = static_cast<uint32_t>(std::max(0, h));
    }

    void VulkanGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        // Storage-only (Task 880); consumed at command-buffer-record time via
        // vkCmdSetViewport, mirroring SetScissorRect's identical pattern. Only the
        // backbuffer pass reads this state -- RT passes stay hardcoded to each RT's own
        // full size, matching RecordCommandBuffer's existing per-RT-pass scissor-hardcoding
        // precedent, since the deferred, potentially-multi-RT-per-frame recording model
        // cannot recover "what Viewport was active when each RT's draws were issued" from a
        // single frame-global stored value.
        viewportX_        = static_cast<int32_t>(x);
        viewportY_        = static_cast<int32_t>(y);
        viewportW_        = static_cast<uint32_t>(std::max(0, w));
        viewportH_        = static_cast<uint32_t>(std::max(0, h));
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
        viewportSet_      = true;
    }

    void VulkanGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void VulkanGraphicsBackend::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    std::unique_ptr<IOcclusionQueryBackend> VulkanGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<VulkanOcclusionQueryBackend>(this);
    }

    std::unique_ptr<ITexture3DBackend> VulkanGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTexture3DBackend>(this, w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeBackend> VulkanGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTextureCubeBackend>(this, size, mipMap);
    }

    std::unique_ptr<IRenderTargetCubeBackend> VulkanGraphicsBackend::CreateRenderTargetCube(int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        // mipMap (Task 907): real per-face vkCmdBlitImage cascade, mirroring Task 878's
        // RenderTarget2D fix -- see VulkanRenderTargetCubeBackend::FaceProxy::MaybeGenerateMips.
        // multiSampleCount (Task 903): now wired up -- mirrors VulkanRenderTargetBackend's
        // Task 878/879 "piggyback on the backend's own sampleCount_" MSAA treatment, applied per
        // cube face via a shared MSAA color image (see VulkanRenderTargetCubeBackend's
        // constructor). depthFormat (Task 877) now gets true per-instance fidelity (Task 911),
        // mirroring VulkanRenderTargetBackend's identical treatment.
        return std::make_unique<VulkanRenderTargetCubeBackend>(this, size, depthFormat, mipMap, multiSampleCount);
    }

    void VulkanGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (!rts || count <= 0) {
            currentRT_ = nullptr;
            mrtProxy_.reset();
            return;
        }
        if (count == 1) {
            mrtProxy_.reset();
            auto* vrt = static_cast<VulkanRenderTargetBackend*>(rts[0]);
            currentRT_ = vrt;
            vrt->BindAsRenderTarget();
            return;
        }
        // Build MRT proxy for N > 1.
        std::vector<VulkanRenderTargetBackend*> vRts(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            vRts[i] = static_cast<VulkanRenderTargetBackend*>(rts[i]);
        mrtProxy_ = std::make_unique<VulkanMRTProxy>(this, vRts.data(), static_cast<uint32_t>(count));
        currentRT_ = mrtProxy_.get();
    }

    // --- VulkanMRTProxy ---

    VulkanMRTProxy::VulkanMRTProxy(VulkanGraphicsBackend* owner,
                                    VulkanRenderTargetBackend* const* rts, uint32_t count)
        : owner_(owner), colorCount_(count)
    {
        if (!owner || !rts || count == 0) return;
        VkDevice dev = owner->device_;

        width_  = rts[0]->GetWidth();
        height_ = rts[0]->GetHeight();

        renderPass_ = owner->GetOrCreateMRTRenderPass(count);

        // Task 911: this proxy owns its own dedicated depth image in the device-wide
        // depthFormat_, sized to rts[0]'s width/height -- NOT borrowed from any bound RT's own
        // depthView_ (previously always valid because every RT unconditionally got a real
        // depthFormat_-shaped depth buffer; now a RenderTarget2D can genuinely have no depth
        // buffer at all for DepthFormat::None, or a distinct real format, either of which would
        // leave GetOrCreateMRTRenderPass's depth attachment reference dangling/mismatched).
        VkImageCreateInfo depthInfo{};
        depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType     = VK_IMAGE_TYPE_2D;
        depthInfo.format        = owner_->depthFormat_;
        depthInfo.extent        = { static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1 };
        depthInfo.mipLevels     = 1;
        depthInfo.arrayLayers   = 1;
        depthInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &depthInfo, nullptr, &depthImage_) != VK_SUCCESS)
            throw std::runtime_error("VulkanMRTProxy: vkCreateImage (depth) failed");

        VkMemoryRequirements depthReq;
        vkGetImageMemoryRequirements(dev, depthImage_, &depthReq);
        VkMemoryAllocateInfo depthAlloc{};
        depthAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depthAlloc.allocationSize  = depthReq.size;
        depthAlloc.memoryTypeIndex = owner_->FindMemoryType(depthReq.memoryTypeBits,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &depthAlloc, nullptr, &depthMemory_) != VK_SUCCESS)
            throw std::runtime_error("VulkanMRTProxy: vkAllocateMemory (depth) failed");
        vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image    = depthImage_;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format   = owner_->depthFormat_;
        depthViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(dev, &depthViewInfo, nullptr, &depthView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanMRTProxy: vkCreateImageView (depth) failed");

        // Build attachment view array: [colorView0, colorView1, ..., this proxy's own depthView_].
        std::vector<VkImageView> atts;
        atts.reserve(count + 1);
        for (uint32_t i = 0; i < count; ++i)
            atts.push_back(rts[i]->GetColorView());
        atts.push_back(depthView_);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(atts.size());
        fbInfo.pAttachments    = atts.data();
        fbInfo.width           = static_cast<uint32_t>(width_);
        fbInfo.height          = static_cast<uint32_t>(height_);
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
            throw std::runtime_error("VulkanMRTProxy: vkCreateFramebuffer failed");
    }

    VkFormat VulkanMRTProxy::GetDepthFormat() const
    {
        return owner_ ? owner_->depthFormat_ : VK_FORMAT_UNDEFINED;
    }

    VulkanMRTProxy::~VulkanMRTProxy()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(dev, framebuffer_, nullptr);
        if (depthView_   != VK_NULL_HANDLE) vkDestroyImageView(dev, depthView_, nullptr);
        if (depthImage_  != VK_NULL_HANDLE) vkDestroyImage(dev, depthImage_, nullptr);
        if (depthMemory_ != VK_NULL_HANDLE) vkFreeMemory(dev, depthMemory_, nullptr);
    }

    // --- VulkanTexture3DBackend ---

    // Task 864: mirrors Texture3D.cpp's CalculateMipLevels(w,h) -- depth does not participate in
    // the level count, matching FNA's Texture3D constructor exactly.
    static int CalculateVulkanTexture3DMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    VulkanTexture3DBackend::VulkanTexture3DBackend(VulkanGraphicsBackend* owner, int w, int h, int depth, bool mipMap)
        : owner_(owner), width_(w), height_(h), depth_(depth)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        levelCount_ = mipMap ? CalculateVulkanTexture3DMipLevels(w, h) : 1;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_3D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                   static_cast<uint32_t>(depth) };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC (Task 865): needed by GetData's vkCmdCopyImageToBuffer readback.
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS) return;

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) return;
        vkBindImageMemory(dev, image_, memory_, 0);

        // Task 864: covers *all* levelCount_ levels (not just level 0, unlike the shared
        // single-level TransitionImageLayout helper) so SetData/GetData can address any mip
        // level from construction time, mirroring VulkanRenderTargetBackend's identical fix.
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       static_cast<uint32_t>(levelCount_), 0, 1 };
        vkCreateImageView(dev, &viewInfo, nullptr, &imageView_);
    }

    VulkanTexture3DBackend::~VulkanTexture3DBackend()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(dev, imageView_, nullptr);
        if (image_     != VK_NULL_HANDLE) vkDestroyImage(dev, image_, nullptr);
        if (memory_    != VK_NULL_HANDLE) vkFreeMemory(dev, memory_, nullptr);
    }

    void VulkanTexture3DBackend::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int dataLength)
    {
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        std::memcpy(mapped, data, static_cast<size_t>(dataLength));

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { x, y, z };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                     static_cast<uint32_t>(depth) };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        owner_->EndOneTimeCommands(cb);

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // Task 865: real GPU readback via vkCmdCopyImageToBuffer + a host-visible staging buffer,
    // mirroring SetData's upload path in reverse.
    void VulkanTexture3DBackend::GetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { x, y, z };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                     static_cast<uint32_t>(depth) };
        vkCmdCopyImageToBuffer(cb, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);
        owner_->EndOneTimeCommands(cb);

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::memcpy(data, mapped, static_cast<size_t>(dataLength));

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // --- VulkanTextureCubeBackend ---

    // Task 864: mirrors TextureCube.cpp's CalculateMipLevels(size,size) -- cube faces are square.
    static int CalculateVulkanTextureCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    VulkanTextureCubeBackend::VulkanTextureCubeBackend(VulkanGraphicsBackend* owner, int size, bool mipMap)
        : owner_(owner), size_(size)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        levelCount_ = mipMap ? CalculateVulkanTextureCubeMipLevels(size) : 1;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1 };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 6;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC (Task 865): needed by GetData's vkCmdCopyImageToBuffer readback.
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS) return;

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) return;
        vkBindImageMemory(dev, image_, memory_, 0);

        // Transition all 6 faces to shader-read-only (empty initially).
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image_;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                         static_cast<uint32_t>(levelCount_), 0, 6 };
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        owner_->EndOneTimeCommands(cb);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       static_cast<uint32_t>(levelCount_), 0, 6 };
        vkCreateImageView(dev, &viewInfo, nullptr, &imageView_);
    }

    VulkanTextureCubeBackend::~VulkanTextureCubeBackend()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(dev, imageView_, nullptr);
        if (image_     != VK_NULL_HANDLE) vkDestroyImage(dev, image_, nullptr);
        if (memory_    != VK_NULL_HANDLE) vkFreeMemory(dev, memory_, nullptr);
    }

    void VulkanTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int dataLength)
    {
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return;
        if (face < 0 || face >= 6) return;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        std::memcpy(mapped, data, static_cast<size_t>(dataLength));

        // Transition only the target face layer to TRANSFER_DST_OPTIMAL.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier toXfer{};
        toXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toXfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.image               = image_;
        toXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1,
                                        static_cast<uint32_t>(face), 1 };
        toXfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toXfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toXfer);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT,
                                     static_cast<uint32_t>(level),
                                     static_cast<uint32_t>(face), 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toXfer;
        std::swap(toRead.oldLayout, toRead.newLayout);
        std::swap(toRead.srcAccessMask, toRead.dstAccessMask);
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);

        owner_->EndOneTimeCommands(cb);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // Task 865: real GPU readback via vkCmdCopyImageToBuffer + a host-visible staging buffer,
    // mirroring SetData's per-face upload path in reverse (inline barriers scoped to just the
    // target face layer, mirroring SetData's own approach -- the shared TransitionImageLayout
    // helper always transitions layer 0 only, so it can't be reused for an arbitrary cube face).
    void VulkanTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return;
        if (face < 0 || face >= 6) return;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);

        // Transition only the target face layer to TRANSFER_SRC_OPTIMAL.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier toXfer{};
        toXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toXfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.image               = image_;
        toXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1,
                                        static_cast<uint32_t>(face), 1 };
        toXfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toXfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toXfer);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT,
                                     static_cast<uint32_t>(level),
                                     static_cast<uint32_t>(face), 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyImageToBuffer(cb, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);

        VkImageMemoryBarrier toRead = toXfer;
        std::swap(toRead.oldLayout, toRead.newLayout);
        std::swap(toRead.srcAccessMask, toRead.dstAccessMask);
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);

        owner_->EndOneTimeCommands(cb);

        std::memcpy(data, mapped, static_cast<size_t>(dataLength));

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // --- VulkanOcclusionQueryBackend ---

    VulkanOcclusionQueryBackend::VulkanOcclusionQueryBackend(VulkanGraphicsBackend* owner)
        : owner_(owner)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;

        VkQueryPoolCreateInfo qi{};
        qi.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType  = VK_QUERY_TYPE_OCCLUSION;
        qi.queryCount = 1;
        vkCreateQueryPool(owner_->device_, &qi, nullptr, &pool_);
        // Reset the query slot before first use.
        if (pool_ != VK_NULL_HANDLE)
        {
            VkCommandBuffer cb = owner_->BeginOneTimeCommands();
            vkCmdResetQueryPool(cb, pool_, 0, 1);
            owner_->EndOneTimeCommands(cb);
        }
    }

    VulkanOcclusionQueryBackend::~VulkanOcclusionQueryBackend()
    {
        if (owner_ && owner_->device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE)
            vkDestroyQueryPool(owner_->device_, pool_, nullptr);
    }

    void VulkanOcclusionQueryBackend::Begin()
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE) return;
        ended_ = false;
        // Task 447/854: occlusion queries in Vulkan must be recorded inside a render pass, and
        // CNA's Vulkan backend defers all draws to RecordCommandBuffer -- so Begin()/End() don't
        // inject any Vulkan commands directly. Instead, this marks the query "active": every
        // Pending3DDraw pushed via PushPending3DDraw() between now and End() gets tagged with
        // `this`, and RecordCommandBuffer() later wraps whichever contiguous run of tagged draws
        // land in the same render pass in a real vkCmdBeginQuery/vkCmdEndQuery pair.
        owner_->activeOcclusionQuery_ = this;
    }

    void VulkanOcclusionQueryBackend::End()
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE) return;
        ended_ = true;
        // Guard rather than unconditionally clearing: XNA's own OcclusionQuery API doesn't
        // support nested Begin() calls, so this should always already be `this` in practice.
        if (owner_->activeOcclusionQuery_ == this)
            owner_->activeOcclusionQuery_ = nullptr;
        // pixelCount_ is no longer hardcoded to 0 here -- IsComplete() populates it once the
        // real GPU query result (recorded by RecordCommandBuffer(), see Begin()'s comment) is
        // ready, matching every other backend's own async completion timing.
    }

    bool VulkanOcclusionQueryBackend::IsComplete() const
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE || !ended_) return false;
        uint64_t result = 0;
        VkResult r = vkGetQueryPoolResults(owner_->device_, pool_, 0, 1,
                                           sizeof(result), &result,
                                           sizeof(result),
                                           VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS)
        {
            pixelCount_ = static_cast<int>(result);
            return true;
        }
        return false; // VK_NOT_READY
    }

    int VulkanOcclusionQueryBackend::PixelCount() const { return pixelCount_; }

    // --- VulkanRenderTargetCubeBackend ---

    VulkanRenderTargetCubeBackend::VulkanRenderTargetCubeBackend(VulkanGraphicsBackend* owner, int size,
                                                                  int depthFormat, bool mipMap,
                                                                  int requestedMultiSampleCount)
        : owner_(owner), size_(size)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice    dev  = owner_->device_;
        const auto  us   = static_cast<uint32_t>(size);
        levelCount_ = mipMap ? CalculateVulkanRTMipLevels(size, size) : 1;

        // Task 911: real per-instance DepthStencilFormat fidelity, mirroring
        // VulkanRenderTargetBackend's identical constructor fix -- None means no depth
        // attachment at all; otherwise a real, distinct VkFormat picked for THIS instance,
        // independent of the backbuffer's own depthFormat_.
        const bool hasDepth = (static_cast<DepthFormat>(depthFormat) != DepthFormat::None);
        if (hasDepth)
            depthVkFormat_ = PickDepthFormat(owner_->physicalDevice_, static_cast<DepthFormat>(depthFormat));

        // Task 903: mirrors VulkanRenderTargetBackend's identical "piggyback on the backend's own
        // sampleCount_" scope decision (Task 878/879) -- see plan_graphics.md.
        const bool wantsMsaa = requestedMultiSampleCount > 0 &&
                               owner_->sampleCount_ > VK_SAMPLE_COUNT_1_BIT;

        // --- Color image: 6-layer cube-compatible, color attachment + sampled ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { us, us, 1 };
        colorInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        colorInfo.arrayLayers   = 6;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC/DST (Task 907): needed by FaceProxy::MaybeGenerateMips' vkCmdBlitImage
        // cascade when levelCount_ > 1; harmless when levelCount_==1.
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        colorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &colorInfo, nullptr, &image_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImage failed");

        VkMemoryRequirements colorReq;
        vkGetImageMemoryRequirements(dev, image_, &colorReq);
        VkMemoryAllocateInfo colorAlloc{};
        colorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        colorAlloc.allocationSize  = colorReq.size;
        colorAlloc.memoryTypeIndex = owner_->FindMemoryType(colorReq.memoryTypeBits,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &colorAlloc, nullptr, &memory_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetCubeBackend: vkAllocateMemory failed");
        vkBindImageMemory(dev, image_, memory_, 0);

        // --- Full-cube image view for sampling (VK_IMAGE_VIEW_TYPE_CUBE, all 6 layers, full mip
        // range -- Task 907: levelCount_ levels instead of hardcoded 1, mirroring
        // VulkanRenderTargetBackend::colorSampleView_'s identical Task 878 fix) ---
        {
            VkImageViewCreateInfo cv{};
            cv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            cv.image    = image_;
            cv.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            cv.format   = owner_->swapchainFormat_;
            cv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(levelCount_), 0, 6 };
            if (vkCreateImageView(dev, &cv, nullptr, &cubeView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImageView (cube) failed");
        }

        // --- 6 per-face color image views (for framebuffer attachments) ---
        for (int face = 0; face < 6; ++face) {
            VkImageViewCreateInfo fv{};
            fv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            fv.image    = image_;
            fv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            fv.format   = owner_->swapchainFormat_;
            fv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                    static_cast<uint32_t>(face), 1 };
            if (vkCreateImageView(dev, &fv, nullptr, &faceViews_[face]) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImageView failed");
        }

        // Task 907: transition every level of every face to SHADER_READ_ONLY_OPTIMAL up front.
        // Level 0 of an actually-rendered face gets this from its own render pass's finalLayout
        // regardless (matching this class's pre-existing, unchanged behavior for the non-mip
        // case), but levels 1..levelCount_-1 are NEVER touched by any render pass -- only by
        // FaceProxy::MaybeGenerateMips' blit cascade, whose own first barrier for each
        // destination level assumes it starts in SHADER_READ_ONLY_OPTIMAL. Without this upfront
        // transition that assumption is false the first time any face is ever rendered,
        // producing live VUID-vkCmdDraw-None-09600 validation errors (mirrors
        // VulkanRenderTargetBackend's identical Task 878 fix).
        if (levelCount_ > 1)
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 6 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        // --- Shared depth image (one 2D image reused across all faces; promoted to MSAA samples
        // when this cube engages MSAA, mirroring VulkanRenderTargetBackend's depthImage_. Task
        // 911: only created when a real depth format was requested -- DepthFormat::None
        // correctly gets no depth attachment at all now.) ---
        if (hasDepth)
        {
            VkImageCreateInfo depthInfo{};
            depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthInfo.imageType     = VK_IMAGE_TYPE_2D;
            depthInfo.format        = depthVkFormat_;
            depthInfo.extent        = { us, us, 1 };
            depthInfo.mipLevels     = 1;
            depthInfo.arrayLayers   = 1;
            depthInfo.samples       = wantsMsaa ? owner_->sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
            depthInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            depthInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &depthInfo, nullptr, &depthImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImage (depth) failed");

            VkMemoryRequirements depthReq;
            vkGetImageMemoryRequirements(dev, depthImage_, &depthReq);
            VkMemoryAllocateInfo depthAlloc{};
            depthAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            depthAlloc.allocationSize  = depthReq.size;
            depthAlloc.memoryTypeIndex = owner_->FindMemoryType(depthReq.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &depthAlloc, nullptr, &depthMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkAllocateMemory (depth) failed");
            vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

            VkImageViewCreateInfo dv{};
            dv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dv.image    = depthImage_;
            dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dv.format   = depthVkFormat_;
            dv.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &dv, nullptr, &depthView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImageView (depth) failed");
        }

        // --- Shared MSAA color image (Task 903): one 2D image reused across all 6 faces, same
        // "shared across faces" pattern as depthImage_ above -- only one face is ever rendered
        // into at a time. TRANSIENT_ATTACHMENT only (never sampled directly), resolved into that
        // face's own faceViews_[face]/image_ layer via the depth-format-keyed MSAA render pass's
        // (GetOrCreateRTRenderPassMsaa()) pResolveAttachments, mirroring
        // VulkanRenderTargetBackend::msaaColorImage_ exactly. ---
        if (wantsMsaa)
        {
            VkImageCreateInfo msaaColorInfo{};
            msaaColorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            msaaColorInfo.imageType     = VK_IMAGE_TYPE_2D;
            msaaColorInfo.format        = owner_->swapchainFormat_;
            msaaColorInfo.extent        = { us, us, 1 };
            msaaColorInfo.mipLevels     = 1;
            msaaColorInfo.arrayLayers   = 1;
            msaaColorInfo.samples       = owner_->sampleCount_;
            msaaColorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            msaaColorInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            msaaColorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            msaaColorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &msaaColorInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImage (MSAA color) failed");

            VkMemoryRequirements msaaColorReq;
            vkGetImageMemoryRequirements(dev, msaaColorImage_, &msaaColorReq);
            VkMemoryAllocateInfo msaaColorAlloc{};
            msaaColorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            msaaColorAlloc.allocationSize  = msaaColorReq.size;
            msaaColorAlloc.memoryTypeIndex = owner_->FindMemoryType(msaaColorReq.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &msaaColorAlloc, nullptr, &msaaColorMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkAllocateMemory (MSAA color) failed");
            vkBindImageMemory(dev, msaaColorImage_, msaaColorMemory_, 0);

            VkImageViewCreateInfo msaaColorView{};
            msaaColorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            msaaColorView.image    = msaaColorImage_;
            msaaColorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            msaaColorView.format   = owner_->swapchainFormat_;
            msaaColorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &msaaColorView, nullptr, &msaaColorView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImageView (MSAA color) failed");

            appliedMultiSampleCount_ = SampleCountToInt(owner_->sampleCount_);
        }

        // --- 6 framebuffers (one per face, sharing the depth view) -- MSAA variant (att0=MSAA
        // color, att1=resolve into this face's own view, att2=MSAA depth if hasDepth) when this
        // cube engages MSAA, else the plain variant, mirroring VulkanRenderTargetBackend's
        // mutually-exclusive framebuffer_/msaaFramebuffer_ pattern exactly. Task 911: render pass
        // now selected/lazily-created per this instance's own real depthVkFormat_. ---
        for (int face = 0; face < 6; ++face) {
            if (wantsMsaa)
            {
                VkImageView atts[] = { msaaColorView_, faceViews_[face], depthView_ };
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass      = owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_);
                fbInfo.attachmentCount = hasDepth ? 3u : 2u;
                fbInfo.pAttachments    = atts;
                fbInfo.width           = us;
                fbInfo.height          = us;
                fbInfo.layers          = 1;
                if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &msaaFramebuffers_[face]) != VK_SUCCESS)
                    throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateFramebuffer (MSAA) failed");

                faceProxies_[face].msaaFramebuffer = msaaFramebuffers_[face];
                faceProxies_[face].msaaRenderPass  = owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_);
            }
            else
            {
                VkImageView atts[] = { faceViews_[face], depthView_ };
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                // Task 911: this class has no preserveContents concept (unlike
                // VulkanRenderTargetBackend) -- always use the discard/clear variant, matching
                // this class's own pre-existing (pre-911) behavior of always using rtRenderPass_.
                fbInfo.renderPass      = owner_->GetOrCreateRTRenderPass(depthVkFormat_, true);
                fbInfo.attachmentCount = hasDepth ? 2u : 1u;
                fbInfo.pAttachments    = atts;
                fbInfo.width           = us;
                fbInfo.height          = us;
                fbInfo.layers          = 1;
                if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffers_[face]) != VK_SUCCESS)
                    throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateFramebuffer failed");

                faceProxies_[face].framebuffer = framebuffers_[face];
                faceProxies_[face].renderPass  = owner_->GetOrCreateRTRenderPass(depthVkFormat_, true);
            }

            faceProxies_[face].size        = size;
            faceProxies_[face].image       = image_;
            faceProxies_[face].levelCount  = levelCount_;
            faceProxies_[face].faceIndex   = face;
            faceProxies_[face].depthFormat = depthVkFormat_;
        }
    }

    VulkanRenderTargetCubeBackend::~VulkanRenderTargetCubeBackend()
    {
        if (owner_) {
            // Clear currentRT_ if it points to any of our face proxies.
            for (auto& fp : faceProxies_) {
                if (owner_->currentRT_ == &fp) { owner_->currentRT_ = nullptr; break; }
            }
        }
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        vkDeviceWaitIdle(dev);
        for (int i = 0; i < 6; ++i) {
            if (framebuffers_[i] != VK_NULL_HANDLE)
                vkDestroyFramebuffer(dev, framebuffers_[i], nullptr);
            if (msaaFramebuffers_[i] != VK_NULL_HANDLE)
                vkDestroyFramebuffer(dev, msaaFramebuffers_[i], nullptr);
            if (faceViews_[i] != VK_NULL_HANDLE)
                vkDestroyImageView(dev, faceViews_[i], nullptr);
        }
        if (cubeView_    != VK_NULL_HANDLE) vkDestroyImageView(dev, cubeView_, nullptr);
        if (depthView_   != VK_NULL_HANDLE) vkDestroyImageView(dev, depthView_, nullptr);
        if (depthImage_  != VK_NULL_HANDLE) vkDestroyImage(dev, depthImage_, nullptr);
        if (depthMemory_ != VK_NULL_HANDLE) vkFreeMemory(dev, depthMemory_, nullptr);
        if (msaaColorView_   != VK_NULL_HANDLE) vkDestroyImageView(dev, msaaColorView_, nullptr);
        if (msaaColorImage_  != VK_NULL_HANDLE) vkDestroyImage(dev, msaaColorImage_, nullptr);
        if (msaaColorMemory_ != VK_NULL_HANDLE) vkFreeMemory(dev, msaaColorMemory_, nullptr);
        if (image_       != VK_NULL_HANDLE) vkDestroyImage(dev, image_, nullptr);
        if (memory_      != VK_NULL_HANDLE) vkFreeMemory(dev, memory_, nullptr);
    }

    void VulkanRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (owner_ && face >= 0 && face < 6)
            owner_->currentRT_ = &faceProxies_[face];
    }

    void VulkanRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (owner_) {
            for (auto& fp : faceProxies_)
                if (owner_->currentRT_ == &fp) { owner_->currentRT_ = nullptr; return; }
        }
    }

    // Task 907: regenerate this face's mip chain (levels 0..levelCount-1 of the shared 6-layer
    // `image`, this face's own `faceIndex` layer) via a vkCmdBlitImage cascade -- identical
    // mechanism to VulkanRenderTargetBackend::MaybeGenerateMips (Task 878), just scoped to one
    // array layer instead of the whole (non-array) 2D image.
    void VulkanRenderTargetCubeBackend::FaceProxy::MaybeGenerateMips(VkCommandBuffer cb)
    {
        if (levelCount <= 1) return;
        const uint32_t layer = static_cast<uint32_t>(faceIndex);

        auto barrier = [&](uint32_t level, VkImageLayout oldL, VkImageLayout newL,
                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout           = oldL;
            b.newLayout           = newL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = image;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, layer, 1 };
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        barrier(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        int srcW = size, srcH = size;
        for (int level = 1; level < levelCount; ++level) {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            barrier(static_cast<uint32_t>(level),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level - 1), layer, 1 };
            blit.srcOffsets[1]  = { srcW, srcH, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), layer, 1 };
            blit.dstOffsets[1]  = { dstW, dstH, 1 };
            vkCmdBlitImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              1, &blit, VK_FILTER_LINEAR);

            barrier(static_cast<uint32_t>(level - 1),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            if (level < levelCount - 1) {
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else {
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }

            srcW = dstW; srcH = dstH;
        }
    }

} // namespace CNA::Internal::Backends::Vulkan

// =========================================================================
// Factory
// =========================================================================
namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_VULKAN
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Vulkan::VulkanGraphicsBackend>(args.window, args.multiSampleCount, args.swapInterval);
    }
#endif
}
