#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Vulkan/shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
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
        : width_(data.width), height_(data.height), owner_(owner)
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
        imgInfo.format        = VK_FORMAT_R8G8B8A8_SRGB;
        imgInfo.extent        = { static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height), 1 };
        imgInfo.mipLevels     = 1;
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

        // Transition UNDEFINED → TRANSFER_DST_OPTIMAL, copy, → SHADER_READ_ONLY
        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        owner_->CopyBufferToImage(stagingBuf, image_,
            static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height));
        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Clean up staging
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);

        // --- VkImageView ---
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
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

    VulkanRenderTargetBackend::VulkanRenderTargetBackend(int w, int h, bool /*hasDepth*/,
                                                          VulkanGraphicsBackend* owner)
        : width_(w), height_(h), hasDepth_(true), owner_(owner)
    {
        VkDevice dev = owner_->device_;
        const uint32_t uw = static_cast<uint32_t>(w);
        const uint32_t uh = static_cast<uint32_t>(h);

        // --- Color image (must use swapchainFormat_ for pipeline compatibility) ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { uw, uh, 1 };
        colorInfo.mipLevels     = 1;
        colorInfo.arrayLayers   = 1;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

        // --- Depth image (always created to match the 2-attachment rtRenderPass_) ---
        VkImageCreateInfo depthInfo{};
        depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType     = VK_IMAGE_TYPE_2D;
        depthInfo.format        = owner_->depthFormat_;
        depthInfo.extent        = { uw, uh, 1 };
        depthInfo.mipLevels     = 1;
        depthInfo.arrayLayers   = 1;
        depthInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
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
        depthView.format   = owner_->depthFormat_;
        depthView.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(dev, &depthView, nullptr, &depthView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkCreateImageView (depth) failed");

        // Lazily create shared RT render pass if not yet done
        if (owner_->rtRenderPass_ == VK_NULL_HANDLE)
            owner_->CreateRTRenderPass();

        // --- Framebuffer ---
        VkImageView fbAtts[] = { colorView_, depthView_ };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = owner_->rtRenderPass_;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments    = fbAtts;
        fbInfo.width           = uw;
        fbInfo.height          = uh;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetBackend: vkCreateFramebuffer failed");

        // Transition color image to SHADER_READ_ONLY_OPTIMAL (initial state before first RT use)
        owner_->TransitionImageLayout(colorImage_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
        imgDesc.imageView   = colorView_;
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
        if (framebuffer_ != VK_NULL_HANDLE)  { vkDestroyFramebuffer(dev, framebuffer_, nullptr);  framebuffer_ = VK_NULL_HANDLE; }
        if (colorView_   != VK_NULL_HANDLE)  { vkDestroyImageView(dev, colorView_, nullptr);       colorView_   = VK_NULL_HANDLE; }
        if (colorImage_  != VK_NULL_HANDLE)  { vkDestroyImage(dev, colorImage_, nullptr);          colorImage_  = VK_NULL_HANDLE; }
        if (colorMemory_ != VK_NULL_HANDLE)  { vkFreeMemory(dev, colorMemory_, nullptr);           colorMemory_ = VK_NULL_HANDLE; }
        if (depthView_   != VK_NULL_HANDLE)  { vkDestroyImageView(dev, depthView_, nullptr);       depthView_   = VK_NULL_HANDLE; }
        if (depthImage_  != VK_NULL_HANDLE)  { vkDestroyImage(dev, depthImage_, nullptr);          depthImage_  = VK_NULL_HANDLE; }
        if (depthMemory_ != VK_NULL_HANDLE)  { vkFreeMemory(dev, depthMemory_, nullptr);           depthMemory_ = VK_NULL_HANDLE; }
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

    VkRenderPass VulkanRenderTargetBackend::GetRenderPass() const
    {
        return owner_ ? owner_->rtRenderPass_ : VK_NULL_HANDLE;
    }

    void VulkanRenderTargetBackend::BindAsRenderTarget()
    {
        if (owner_) owner_->currentRT_ = this;
    }

    void VulkanRenderTargetBackend::UnbindAsRenderTarget()
    {
        if (owner_ && owner_->currentRT_ == this) owner_->currentRT_ = nullptr;
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
        active_ = true;
        backend_->activeBatches_.push_back({this, backend_->currentRT_});
    }

    void VulkanSpriteBatchBackend::FlushTexture()
    {
        if (!currentTexture_) return;
        uint32_t count = static_cast<uint32_t>(indices_.size()) - batchFirstIndex_;
        if (count == 0) return;
        draws_.push_back({ currentTexture_->GetVkDescriptorSet(), batchFirstIndex_, count });
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
    }

    void VulkanSpriteBatchBackend::ConsumeDraws()
    {
        vertices_.clear();
        indices_.clear();
        draws_.clear();
        customEffectBackend_ = nullptr;
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

        float u1 = std::clamp((float)src.X / tw, 0.f, 1.f);
        float v1 = std::clamp((float)src.Y / th, 0.f, 1.f);
        float u2 = std::clamp((float)(src.X + src.Width)  / tw, 0.f, 1.f);
        float v2 = std::clamp((float)(src.Y + src.Height) / th, 0.f, 1.f);

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
        // Pre-allocate for worst-case stride (e.g. VertexPositionColor = 16 bytes)
        VkDeviceSize size = static_cast<VkDeviceSize>(vertex_capacity) * 64;
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
        VkDeviceSize size = static_cast<VkDeviceSize>(index_capacity) * elemSize;
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

    VulkanGraphicsBackend::VulkanGraphicsBackend(SDL_Window* window)
        : window_(window)
    {
        if (!window_)
            throw std::runtime_error("VulkanGraphicsBackend: null window");

        CreateInstance();
        if (sEnableValidation) SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
        CreateRenderPass();
        CreateFramebuffers();
        CreateCommandPool();
        AllocateCommandBuffers();
        CreateSyncObjects();
        CreateSampler();
        CreateDescriptorSetLayout();
        CreateDescriptorPool();
        CreatePipeline2D();
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
        for (auto& [key, pipe] : pipelinesExt3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesExt3D_.clear();
        for (auto& [k, pipe] : pipelinesAlphaTest3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesAlphaTest3D_.clear();
        for (auto& [k, pipe] : pipelinesDualTex3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesDualTex3D_.clear();
        dualTexDescSets_.clear(); // freed with pool below
        for (auto& [k, pipe] : pipelinesEnvMap3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesEnvMap3D_.clear();
        for (auto& cache : envMapDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (envMapUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, envMapUBO_[i], nullptr);     envMapUBO_[i]    = VK_NULL_HANDLE; }
            if (envMapUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, envMapUBOMem_[i], nullptr);    envMapUBOMem_[i] = VK_NULL_HANDLE; }
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
        }
        if (defaultWhiteCubeView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteCubeView_, nullptr); defaultWhiteCubeView_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeImage_ != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteCubeImage_, nullptr);   defaultWhiteCubeImage_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeMem_  != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteCubeMem_, nullptr);       defaultWhiteCubeMem_  = VK_NULL_HANDLE; }
        // Default white texture (no free of descriptorSet — will be freed with the pool).
        if (defaultWhiteView_   != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteView_, nullptr);  defaultWhiteView_   = VK_NULL_HANDLE; }
        if (defaultWhiteImage_  != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteImage_, nullptr);     defaultWhiteImage_  = VK_NULL_HANDLE; }
        if (defaultWhiteMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteMemory_, nullptr);       defaultWhiteMemory_ = VK_NULL_HANDLE; }
        if (pipeline2D_            != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipeline2D_, nullptr);                   pipeline2D_            = VK_NULL_HANDLE; }
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
        if (pipelineLayout2D_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout2D_, nullptr);       pipelineLayout2D_      = VK_NULL_HANDLE; }
        for (auto fb : swapchainFramebuffers_)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        if (rtRenderPass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, rtRenderPass_, nullptr); rtRenderPass_ = VK_NULL_HANDLE; }
        if (renderPass_   != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPass_,   nullptr); renderPass_   = VK_NULL_HANDLE; }
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
        VkSurfaceFormatKHR fmt = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { fmt = f; break; }

        uint32_t mn = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, nullptr);
        std::vector<VkPresentModeKHR> modes(mn);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, modes.data());
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { mode = m; break; }

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
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
        // Entry: wait for any previous shader reads (RT-as-texture) before writing.
        renderPassDeps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[0].dstSubpass      = 0;
        renderPassDeps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        renderPassDeps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        renderPassDeps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        renderPassDeps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        renderPassDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes visible to subsequent fragment shader reads.
        renderPassDeps[1].srcSubpass      = 0;
        renderPassDeps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        renderPassDeps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        renderPassDeps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassDeps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
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

    void VulkanGraphicsBackend::CreateRTRenderPass()
    {
        // Same as renderPass_ but color finalLayout = SHADER_READ_ONLY_OPTIMAL.
        // This makes the two passes compatible so existing pipelines can be reused.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFormat_;
        depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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

        // Entry: wait for the previous frame's texture sample before writing.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes visible to the fragment shader in the next pass.
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments  = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rtRenderPass_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (RT) failed");
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
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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

    void VulkanGraphicsBackend::CreateFramebuffers()
    {
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            VkImageView atts[] = { swapchainImageViews_[i], depthImageView_ };
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = renderPass_;
            ci.attachmentCount = 2;
            ci.pAttachments    = atts;
            ci.width  = swapchainExtent_.width;
            ci.height = swapchainExtent_.height;
            ci.layers = 1;
            if (vkCreateFramebuffer(device_, &ci, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    void VulkanGraphicsBackend::CleanupSwapchain()
    {
        if (device_ == VK_NULL_HANDLE) return;
        for (auto fb : swapchainFramebuffers_) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        for (auto iv : swapchainImageViews_) if (iv) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
        // NOTE: renderPass_ is NOT destroyed here; it is permanent for the backend's lifetime
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

    void VulkanGraphicsBackend::CreatePipeline2D()
    {
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

        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;   pli.pSetLayouts         = &descriptorSetLayout_;
        pli.pushConstantRangeCount = 1;   pli.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout2D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (2D) failed");

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
        pci.renderPass          = renderPass_;
        pci.subpass             = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline2D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (2D) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
    }

    // =========================================================================
    // Depth buffer resources
    // =========================================================================

    VkFormat VulkanGraphicsBackend::FindDepthFormat() const
    {
        for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT,
                              VK_FORMAT_D32_SFLOAT_S8_UINT,
                              VK_FORMAT_D24_UNORM_S8_UINT }) {
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
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
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
    // 3D pipeline layout + per-variant pipeline (lazily created)
    // =========================================================================

    // Encode (topology × depthTest × depthWrite × blend × cullMode) into a single uint32_t key.
    static uint32_t Make3DKey(VkPrimitiveTopology topo, bool depthTest, bool depthWrite,
                              bool blend, int cullMode, uint32_t colorAttachmentCount = 1,
                              bool wireframe = false)
    {
        uint32_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        // bits 0-1: topology, 2: depthTest, 3: depthWrite, 4: blend, 5-6: cullMode, 7-9: colorAttachmentCount, 10: wireframe
        const uint32_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return t | (depthTest ? 4u : 0u) | (depthWrite ? 8u : 0u) | (blend ? 16u : 0u)
                 | (static_cast<uint32_t>(cullMode & 0x3) << 5)
                 | (nc << 7)
                 | (wireframe ? (1u << 10) : 0u);
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipeline3D(VkPrimitiveTopology topo,
                                                             bool depthTest, bool depthWrite,
                                                             bool blend, int cullMode,
                                                             uint32_t colorAttachmentCount,
                                                             bool wireframe)
    {
        // Create layout once
        if (pipelineLayout3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, 64 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (3D) failed");
        }

        uint32_t key = Make3DKey(topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
        auto it = pipelines3D_.find(key);
        if (it != pipelines3D_.end()) return it->second;

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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        if (blend) {
            cba.blendEnable         = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp        = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // Replicate the same blend state across all MRT outputs.
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
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;

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
        // Use the right render pass: single-color uses the standard swapchain-compatible pass;
        // N > 1 colors needs a dedicated MRT render pass.
        pci.renderPass = (colorAttachmentCount <= 1)
                         ? renderPass_
                         : GetOrCreateMRTRenderPass(colorAttachmentCount);
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
                                  bool wireframe = false)
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
        // 9-10: cullMode, 11-13: colorAttachmentCount, 14: wireframe
        const uint64_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return s | (t << 4) | (depthTest ? (1ull<<6) : 0) | (depthWrite ? (1ull<<7) : 0)
             | (blend ? (1ull<<8) : 0) | (static_cast<uint64_t>(cullMode & 3) << 9) | (nc << 11)
             | (wireframe ? (1ull<<14) : 0);
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
        // [24..31]: padding (unused)
        for (int i = 24; i < 32; ++i) pc[i] = 0.f;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineAlphaTest3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe)
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

        uint64_t key = MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
        auto it = pipelinesAlphaTest3D_.find(key);
        if (it != pipelinesAlphaTest3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kAlphaTest3dVertSpv, kAlphaTest3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kAlphaTest3dFragSpv, kAlphaTest3dFragSpv_size);

        // Vertex binding: position always at location=0, UV always remapped to location=1.
        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };   // position
        uint32_t uvOffset = 12;
        if (stride == 24) uvOffset = 16;   // past ubyte4 color
        if (stride == 32) uvOffset = 24;   // past float3 normal
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, uvOffset }; // UV remapped to location=1

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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor;
        cbs.pAttachments    = blendAttachments.data();

        constexpr VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = (colorAttachmentCount > 1) ? GetOrCreateMRTRenderPass(colorAttachmentCount)
                                                      : renderPass_;

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

        // Two combined image samplers at bindings 0 and 1.
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding            = i;
            bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount    = 1;
            bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayout2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (2-tex) failed");

        // Pool for up to 512 dual-texture descriptor sets.
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 * 2 };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = 512;
        pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPool2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (2-tex) failed");

        // Pipeline layout: same 128-byte PC range + 2-sampler descriptor set layout.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout2Tex_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutDualTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (DualTex3D) failed");
    }

    VkDescriptorSet VulkanGraphicsBackend::GetOrCreateDualTexDescSet(VkImageView view0, VkImageView view1)
    {
        EnsureDualTexResources();

        const uint64_t key = reinterpret_cast<uint64_t>(view0) * 2654435761ULL
                           ^ reinterpret_cast<uint64_t>(view1);
        auto it = dualTexDescSets_.find(key);
        if (it != dualTexDescSets_.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPool2Tex_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayout2Tex_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &ai, &ds) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo[2]{};
        imgInfo[0].sampler     = defaultSampler_;
        imgInfo[0].imageView   = view0;
        imgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo[1].sampler     = defaultSampler_;
        imgInfo[1].imageView   = view1;
        imgInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        dualTexDescSets_[key] = ds;
        return ds;
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineDualTex3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe)
    {
        EnsureDualTexResources();

        // DualTexture always uses stride=20 (VertexPositionTexture); key encodes topology+state.
        constexpr std::size_t kDualStride = 20;
        uint64_t key = MakeExt3DKey(kDualStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
        auto it = pipelinesDualTex3D_.find(key);
        if (it != pipelinesDualTex3D_.end()) return it->second;

        using namespace Shaders;
        // Vertex shader: reuse kTextured3dVertSpv (reads MVP + diffuseColor from same PC layout).
        VkShaderModule vert = CreateShaderModule(kTextured3dVertSpv,      kTextured3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kDualTexture3dFragSpv,   kDualTexture3dFragSpv_size);

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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = (colorAttachmentCount > 1) ? GetOrCreateMRTRenderPass(colorAttachmentCount)
                                                      : renderPass_;

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
        bufInfo.range  = 96;  // size of one EnvMapParams block in the shader

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
        uint32_t colorAttachmentCount, bool wireframe)
    {
        EnsureEnvMapResources();

        constexpr std::size_t kEnvStride = 32;
        uint64_t key = MakeExt3DKey(kEnvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = (colorAttachmentCount > 1) ? GetOrCreateMRTRenderPass(colorAttachmentCount)
                                                      : renderPass_;

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

    // ---- SkinnedEffect resources (Task 109) ----

    void VulkanGraphicsBackend::EnsureSkinnedResources()
    {
        if (descriptorSetLayoutSkinned_ != VK_NULL_HANDLE) return;

        // binding=0: sampler2D (fragment), binding=1: bone UBO dynamic (vertex)
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (Skinned) failed");

        const uint32_t maxSets = 128u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
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

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineSkinned3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe)
    {
        EnsureSkinnedResources();

        constexpr std::size_t kSkinnedStride = 52;
        uint64_t key = MakeExt3DKey(kSkinnedStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = (colorAttachmentCount > 1) ? GetOrCreateMRTRenderPass(colorAttachmentCount)
                                                      : renderPass_;

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
        uint32_t colorAttachmentCount, bool wireframe)
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

        uint64_t key = MakeExt3DKey(pvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
        auto it = pipelinesInstanced3D_.find(key);
        if (it != pipelinesInstanced3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kInstanced3dVertSpv, kInstanced3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kColored3dFragSpv,   kColored3dFragSpv_size);

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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (auto& ba : blendAttachments) {
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        constexpr VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = (colorAttachmentCount > 1) ? GetOrCreateMRTRenderPass(colorAttachmentCount)
                                                      : renderPass_;

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

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipelineExt3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe)
    {
        // Create layout once — 128-byte push constants + descriptor set for texture.
        if (pipelineLayoutExt3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout_;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutExt3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (Ext3D) failed");
        }

        uint64_t key = MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe);
        auto it = pipelinesExt3D_.find(key);
        if (it != pipelinesExt3D_.end()) return it->second;

        using namespace Shaders;
        const uint32_t* vertSpv = nullptr; size_t vertSpvSize = 0;
        const uint32_t* fragSpv = nullptr; size_t fragSpvSize = 0;
        switch (stride) {
        case 20:
            vertSpv = kTextured3dVertSpv;         vertSpvSize = kTextured3dVertSpv_size;
            fragSpv = kTextured3dFragSpv;         fragSpvSize = kTextured3dFragSpv_size;
            break;
        case 24:
            vertSpv = kColoredTextured3dVertSpv;  vertSpvSize = kColoredTextured3dVertSpv_size;
            fragSpv = kColoredTextured3dFragSpv;  fragSpvSize = kColoredTextured3dFragSpv_size;
            break;
        case 32:
        default:
            vertSpv = kLitTextured3dVertSpv;      vertSpvSize = kLitTextured3dVertSpv_size;
            fragSpv = kLitTextured3dFragSpv;      fragSpvSize = kLitTextured3dFragSpv_size;
            break;
        }
        VkShaderModule vert = CreateShaderModule(vertSpv, vertSpvSize);
        VkShaderModule frag = CreateShaderModule(fragSpv, fragSpvSize);

        // Vertex input per stride.
        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        uint32_t attrCount = 0;
        if (stride == 20) {
            // float3 pos + float2 uv
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    12};
            attrCount = 2;
        } else if (stride == 24) {
            // float3 pos + ubyte4 color + float2 uv
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12};
            attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};
            attrCount = 3;
        } else {
            // float3 pos + float3 normal + float2 uv (stride 32)
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
            attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    24};
            attrCount = 3;
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

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        if (blend) {
            cba.blendEnable         = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp        = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
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
        };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3; dyn.pDynamicStates = dynStates;

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
        pci.layout              = pipelineLayoutExt3D_;
        pci.renderPass = (colorAttachmentCount <= 1)
                         ? renderPass_
                         : GetOrCreateMRTRenderPass(colorAttachmentCount);
        pci.subpass = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (Ext3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelinesExt3D_[key] = p;
        return p;
    }

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
    }

    void VulkanGraphicsBackend::RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed");

        // Helper: draw all 2D batches for a specific RT (nullptr = backbuffer) into current render pass.
        // Sprite VB/IB ring buffers are shared across all passes in a frame — callers must
        // ensure total sprite counts fit within MaxSpriteVertices.
        auto drawSpritesFor = [&](VulkanRTSource* targetRT,
                                  float vpW, float vpH)
        {
            VkPipeline lastBoundPipeline = VK_NULL_HANDLE;
            for (auto& [batch, batchRT] : activeBatches_) {
                if (batchRT != targetRT) continue;
                const auto& verts = batch->GetVertices();
                const auto& inds  = batch->GetIndices();
                const auto& draws = batch->GetDrawCalls();
                if (verts.empty() || draws.empty()) continue;

                std::memcpy(spriteVBPtr_[currentFrame_], verts.data(),
                            verts.size() * sizeof(Sprite2DVertex));
                std::memcpy(spriteIBPtr_[currentFrame_], inds.data(),
                            inds.size() * sizeof(uint16_t));

                // Select pipeline: custom SPIR-V effect (Task 119) or built-in 2D.
                VkPipeline       activePipe   = pipeline2D_;
                VkPipelineLayout activeLayout = pipelineLayout2D_;
                const float*     customPC     = nullptr;
                const auto*      ceb          = batch->GetCustomEffectBackend();
                if (ceb && ceb->GetPipeline() != VK_NULL_HANDLE) {
                    activePipe   = ceb->GetPipeline();
                    activeLayout = ceb->GetPipelineLayout();
                    customPC     = ceb->GetPushConst();
                }

                if (activePipe != lastBoundPipeline) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipe);
                    lastBoundPipeline = activePipe;
                }
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &spriteVB_[currentFrame_], &off);
                vkCmdBindIndexBuffer(cb, spriteIB_[currentFrame_], 0, VK_INDEX_TYPE_UINT16);

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
                batch->ConsumeDraws();
            }
        };

        // UBO slot counters (reset once per frame, shared across all RT passes).
        uint32_t envMapUBOSlot  = 0;
        uint32_t skinnedUBOSlot = 0;

        // Helper: draw all pending 3D draws for a specific RT into the current render pass.
        auto draw3DFor = [&](VulkanRTSource* targetRT)
        {
            if (pending3D_.empty()) return;
            EnsureFrame3DBuffers();
            VkPipeline lastPipe   = VK_NULL_HANDLE;
            VkDeviceSize vbOff    = 0;
            VkDeviceSize ibOff    = 0;
            VkDeviceSize instVbOff = 0;
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

                std::memcpy(static_cast<uint8_t*>(frame3DVBPtr_[currentFrame_]) + vbOff,
                            draw.vbData.data(), draw.vbData.size());
                if (!draw.ibData.empty())
                    std::memcpy(static_cast<uint8_t*>(frame3DIBPtr_[currentFrame_]) + ibOff,
                                draw.ibData.data(), draw.ibData.size());
                if (draw.useInstanced && !draw.instVbData.empty())
                    std::memcpy(static_cast<uint8_t*>(frame3DInstVBPtr_[currentFrame_]) + instVbOff,
                                draw.instVbData.data(), draw.instVbData.size());

                const uint32_t nColor = targetRT ? targetRT->GetColorAttachmentCount() : 1u;
                VkPipeline pipe;
                if (draw.useAlphaTest) {
                    pipe = GetOrCreatePipelineAlphaTest3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else if (draw.useDualTexture) {
                    pipe = GetOrCreatePipelineDualTex3D(draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else if (draw.useEnvMap) {
                    pipe = GetOrCreatePipelineEnvMap3D(draw.topology,
                                                       draw.depthTest, draw.depthWrite,
                                                       draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else if (draw.useSkinned) {
                    pipe = GetOrCreatePipelineSkinned3D(draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else if (draw.useInstanced) {
                    pipe = GetOrCreatePipelineInstanced3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else if (draw.useExtParams) {
                    pipe = GetOrCreatePipelineExt3D(draw.stride, draw.topology,
                                                    draw.depthTest, draw.depthWrite,
                                                    draw.blend, draw.cullMode, nColor, draw.wireframe);
                } else {
                    pipe = GetOrCreatePipeline3D(draw.topology,
                                                 draw.depthTest, draw.depthWrite,
                                                 draw.blend, draw.cullMode, nColor, draw.wireframe);
                }
                if (pipe != lastPipe) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    lastPipe = pipe;
                }
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
                    VkDescriptorSet ds = (draw.dualTexDescSet != VK_NULL_HANDLE)
                                         ? draw.dualTexDescSet : defaultWhiteDescSet_;
                    if (ds != VK_NULL_HANDLE)
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutDualTex3D_, 0, 1, &ds, 0, nullptr);
                } else if (draw.useEnvMap) {
                    vkCmdPushConstants(cb, pipelineLayoutEnvMap3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.envMapPC);
                    if (draw.envMapDescSet != VK_NULL_HANDLE && envMapUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = envMapUBOSlot++;
                        const uint32_t uboOff = slot * kEnvMapUBOStride;
                        if (uboOff + 96 <= kEnvMapUBOStride * kEnvMapUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(envMapUBOPtr_[currentFrame_]) + uboOff,
                                        draw.envMapUboData, 96);
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
                        const uint32_t slot   = skinnedUBOSlot++;
                        const uint32_t uboOff = slot * kSkinnedUBOStride;
                        if (uboOff + kSkinnedUBOStride <= kSkinnedUBOStride * kSkinnedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(skinnedUBOPtr_[currentFrame_]) + uboOff,
                                        draw.boneMatrices.data(),
                                        draw.boneMatrices.size() * sizeof(float));
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutSkinned3D_, 0, 1,
                                                &draw.skinnedDescSet, 1, &uboOff);
                    }
                } else if (draw.useInstanced) {
                    vkCmdPushConstants(cb, pipelineLayoutExt3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                } else if (draw.useExtParams) {
                    vkCmdPushConstants(cb, pipelineLayoutExt3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    VkDescriptorSet ds = (draw.descSet != VK_NULL_HANDLE)
                                         ? draw.descSet : defaultWhiteDescSet_;
                    if (ds != VK_NULL_HANDLE)
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutExt3D_, 0, 1, &ds, 0, nullptr);
                } else {
                    vkCmdPushConstants(cb, pipelineLayout3D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, draw.pushConst);
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
        };

        // ---- Phase 1: off-screen RT passes ----
        // Collect unique render targets referenced this frame.
        std::vector<VulkanRTSource*> usedRTs;
        for (auto& [batch, rt] : activeBatches_)
            if (rt && std::find(usedRTs.begin(), usedRTs.end(), rt) == usedRTs.end())
                usedRTs.push_back(rt);
        for (auto& draw : pending3D_)
            if (draw.rt && std::find(usedRTs.begin(), usedRTs.end(), draw.rt) == usedRTs.end())
                usedRTs.push_back(draw.rt);

        for (auto* rt : usedRTs) {
            const uint32_t nColor = rt->GetColorAttachmentCount();
            std::vector<VkClearValue> rtCv(nColor + 1);
            for (uint32_t ci = 0; ci < nColor; ++ci)
                rtCv[ci].color = { { clearR_, clearG_, clearB_, clearA_ } };
            rtCv[nColor].depthStencil = { 1.0f, 0 };
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
        }

        // ---- Phase 2: backbuffer pass ----
        VkClearValue cv[2]{};
        cv[0].color        = { { clearR_, clearG_, clearB_, clearA_ } };
        cv[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass      = renderPass_;
        rp.framebuffer     = swapchainFramebuffers_[imageIndex];
        rp.renderArea      = { {0, 0}, swapchainExtent_ };
        rp.clearValueCount = 2;
        rp.pClearValues    = cv;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = static_cast<float>(swapchainExtent_.width);
        vp.height = static_cast<float>(swapchainExtent_.height);
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
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

        vkCmdEndRenderPass(cb);
        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed");
    }

    void VulkanGraphicsBackend::Present()
    {
        if (!initialized_) return;

        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }
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
    }

    void VulkanGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!initialized_ || swapchainImages_.empty()) return;

        // If there are pending draw commands (3D queue or sprite batches not yet submitted),
        // flush them via Present() so the swapchain image contains the rendered frame.
        if (!pending3D_.empty() || !activeBatches_.empty())
            Present();

        // Wait for all GPU work to finish so the swapchain image is safe to read.
        vkDeviceWaitIdle(device_);

        const VkDeviceSize bufSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer      stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        CreateBuffer(bufSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuf, stagingMem);

        VkImage srcImage = swapchainImages_[lastPresentedImageIndex_];

        // One-time command buffer: PRESENT_SRC_KHR → TRANSFER_SRC → copy → PRESENT_SRC_KHR
        VkCommandBuffer cb = BeginOneTimeCommands();

        auto barrier = [&](VkImage img,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout           = oldLayout;
            b.newLayout           = newLayout;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = img;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        // Transition swapchain image to TRANSFER_SRC_OPTIMAL.
        barrier(srcImage,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset       = { x, y, 0 };
        region.imageExtent       = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyImageToBuffer(cb, srcImage,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               stagingBuf, 1, &region);

        // Transition swapchain image back to PRESENT_SRC_KHR.
        barrier(srcImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);

        EndOneTimeCommands(cb);

        // Map and copy — also handle BGRA → RGBA channel swap.
        void* mapped = nullptr;
        vkMapMemory(device_, stagingMem, 0, bufSize, 0, &mapped);
        const auto* src = static_cast<const uint8_t*>(mapped);
        const bool isBGRA = (swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB);
        if (isBGRA) {
            for (int i = 0; i < w * h; ++i) {
                pixels[i * 4 + 0] = src[i * 4 + 2]; // R ← B
                pixels[i * 4 + 1] = src[i * 4 + 1]; // G ← G
                pixels[i * 4 + 2] = src[i * 4 + 0]; // B ← R
                pixels[i * 4 + 3] = src[i * 4 + 3]; // A ← A
            }
        } else {
            std::memcpy(pixels, src, static_cast<std::size_t>(bufSize));
        }
        vkUnmapMemory(device_, stagingMem);

        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
    }

    void VulkanGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    void VulkanGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_  = width;
        virtualHeight_ = height;
        if (initialized_ && width > 0 && height > 0)
            RecreateSwapchain();
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
        int w, int h, bool hasDepth)
    {
        return std::make_unique<VulkanRenderTargetBackend>(w, h, hasDepth, this);
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

    void VulkanGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        // TODO: depth buffer support
    }

    void VulkanGraphicsBackend::SetDepthTestEnabled(bool v)  { depthTestEnabled_  = v; }
    void VulkanGraphicsBackend::SetBlendEnabled(bool v)      { blendEnabled_      = v; }
    void VulkanGraphicsBackend::SetDepthWriteEnabled(bool v) { depthWriteEnabled_ = v; }

    void VulkanGraphicsBackend::SetStringMarkerEXT(const char* marker)
    {
        if (!marker || !marker[0]) return;
        Pending3DDraw m;
        m.isMarker   = true;
        m.markerLabel = marker;
        m.rt          = currentRT_;
        pending3D_.push_back(std::move(m));
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

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(), drawCount * stride);

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = drawCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.blend      = blendEnabled_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.indexType  = VK_INDEX_TYPE_UINT16;  // non-indexed, not used
        d.rt         = currentRT_;
        pending3D_.push_back(std::move(d));
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
        d.blend      = blendEnabled_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.indexType  = vulkanIB.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt         = currentRT_;
        pending3D_.push_back(std::move(d));
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

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext and skinned (same PC)
        }

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(),
                    static_cast<const uint8_t*>(vb.GetMappedPtr()) + params.vertexStart * stride,
                    drawCount * stride);

        d.topology       = ToVkTopology(primitive);
        d.drawCount      = drawCount;
        d.depthTest      = depthTestEnabled_;
        d.depthWrite     = depthWriteEnabled_;
        d.blend          = blendEnabled_;
        d.cullMode       = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.indexType      = VK_INDEX_TYPE_UINT16;
        d.rt             = currentRT_;
        d.stride         = stride;
        // stride==16 (VertexPositionColor) uses the colored3d pipeline (GetOrCreatePipeline3D)
        // which expects only 64-byte MVP push constants; Ext pipeline doesn't handle stride=16.
        d.useExtParams   = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned && stride != 16;
        d.useDualTexture = needsDualTex;
        d.useSkinned     = needsSkinned;
        if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
        } else if (needsEnvMap) {
            EnsureEnvMapResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vtc = dynamic_cast<const IVulkanCubeSamplable*>(params.envMap);
            VkImageView v2d  = vs0 ? vs0->GetVkImageView()       : defaultWhiteView_;
            VkImageView vcub = vtc ? vtc->GetVkCubeImageView()    : defaultWhiteCubeView_;
            d.envMapDescSet  = GetOrCreateEnvMapDescSet(currentFrame_, v2d, vcub);
            // Pack UBO data: eyePos, diffuse, emissive+envMapAmount, light0Dir, light0Diff, envMapSpecular
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
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = 0.f;
        } else if (needsDualTex) {
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(v0, v1);
        } else {
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
        }
        pending3D_.push_back(std::move(d));
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

        Pending3DDraw d{};
        const Matrix wvp = world * view * projection;
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext and skinned (same PC)
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
        d.blend         = blendEnabled_;
        d.cullMode      = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.indexType     = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt            = currentRT_;
        d.stride        = stride;
        d.useExtParams  = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned && stride != 16;
        d.useDualTexture = needsDualTex;
        d.useSkinned     = needsSkinned;
        if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
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
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = 0.f;
        } else if (needsDualTex) {
            EnsureDualTexResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(v0, v1);
        } else {
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
        }
        pending3D_.push_back(std::move(d));
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
        d.blend        = blendEnabled_;
        d.cullMode     = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.indexType    = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt           = currentRT_;
        d.stride       = pvStride;
        d.instVbStride = instStride;
        d.instanceCount = static_cast<uint32_t>(instCountClamped);
        d.baseVertex   = static_cast<int32_t>(params.baseVertex);
        d.useInstanced = true;
        d.descSet      = defaultWhiteDescSet_;  // no per-draw texture for now
        pending3D_.push_back(std::move(d));
    }

    // ---- Graphics state ----

    void VulkanGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int /*colorBlendFunc*/, int /*alphaBlendFunc*/)
    {
        // Blend::One=0, Blend::Zero=1 → Opaque preset: src=One, dst=Zero → no blending
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1);
    }

    void VulkanGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int /*depthFunc*/,
                                                        bool /*stencilEnable*/, int /*stencilFunc*/,
                                                        int /*stencilPass*/, int /*stencilFail*/,
                                                        int /*stencilDepthFail*/,
                                                        int /*stencilMask*/, int /*stencilWriteMask*/,
                                                        int /*referenceStencil*/,
                                                        bool /*twoSidedStencilMode*/,
                                                        int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                        int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        depthTestEnabled_  = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
    }

    void VulkanGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // XNA FillMode: Solid=0, WireFrame=1
        cullMode_          = cullMode;
        fillModeWireframe_ = (fillMode == 1) && fillModeNonSolidSupported_;
        scissorEnabled_    = scissorTestEnable;
    }

    void VulkanGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = static_cast<int32_t>(x);
        scissorY_ = static_cast<int32_t>(y);
        scissorW_ = static_cast<uint32_t>(std::max(0, w));
        scissorH_ = static_cast<uint32_t>(std::max(0, h));
    }

    void VulkanGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    std::unique_ptr<IOcclusionQueryBackend> VulkanGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<VulkanOcclusionQueryBackend>(this);
    }

    std::unique_ptr<ITexture3DBackend> VulkanGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool /*mipMap*/, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTexture3DBackend>(this, w, h, depth);
    }

    std::unique_ptr<ITextureCubeBackend> VulkanGraphicsBackend::CreateTextureCube(
        int size, bool /*mipMap*/, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTextureCubeBackend>(this, size);
    }

    std::unique_ptr<IRenderTargetCubeBackend> VulkanGraphicsBackend::CreateRenderTargetCube(int size)
    {
        return std::make_unique<VulkanRenderTargetCubeBackend>(this, size);
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

        // Build attachment view array: [colorView0, colorView1, ..., depthView_of_rt0].
        std::vector<VkImageView> atts;
        atts.reserve(count + 1);
        for (uint32_t i = 0; i < count; ++i)
            atts.push_back(rts[i]->GetColorView());
        atts.push_back(rts[0]->GetDepthView());

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

    VulkanMRTProxy::~VulkanMRTProxy()
    {
        if (owner_ && owner_->device_ != VK_NULL_HANDLE && framebuffer_ != VK_NULL_HANDLE)
            vkDestroyFramebuffer(owner_->device_, framebuffer_, nullptr);
    }

    // --- VulkanTexture3DBackend ---

    VulkanTexture3DBackend::VulkanTexture3DBackend(VulkanGraphicsBackend* owner, int w, int h, int depth)
        : owner_(owner), width_(w), height_(h), depth_(depth)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_3D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                   static_cast<uint32_t>(depth) };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

        owner_->TransitionImageLayout(image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
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

    // --- VulkanTextureCubeBackend ---

    VulkanTextureCubeBackend::VulkanTextureCubeBackend(VulkanGraphicsBackend* owner, int size)
        : owner_(owner), size_(size)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 6;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
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
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
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
        // Occlusion queries in Vulkan must be recorded inside a render pass.
        // CNA's Vulkan backend defers all draws to RecordCommandBuffer, so we
        // cannot inject query begin/end here. The result is always 0 (not visible)
        // until proper per-draw-call query injection is implemented.
    }

    void VulkanOcclusionQueryBackend::End()
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE) return;
        ended_ = true;
        // See Begin(): draw-level query injection is not yet implemented.
        // Report 0 visible pixels as a safe default.
        pixelCount_ = 0;
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

    VulkanRenderTargetCubeBackend::VulkanRenderTargetCubeBackend(VulkanGraphicsBackend* owner, int size)
        : owner_(owner), size_(size)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice    dev  = owner_->device_;
        const auto  us   = static_cast<uint32_t>(size);

        // Ensure RT render pass exists.
        if (owner_->rtRenderPass_ == VK_NULL_HANDLE)
            owner_->CreateRTRenderPass();

        // --- Color image: 6-layer cube-compatible, color attachment + sampled ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { us, us, 1 };
        colorInfo.mipLevels     = 1;
        colorInfo.arrayLayers   = 6;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

        // --- Full-cube image view for sampling (VK_IMAGE_VIEW_TYPE_CUBE, all 6 layers) ---
        {
            VkImageViewCreateInfo cv{};
            cv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            cv.image    = image_;
            cv.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            cv.format   = owner_->swapchainFormat_;
            cv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
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

        // --- Shared depth image (one 2D image reused across all faces) ---
        VkImageCreateInfo depthInfo{};
        depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType     = VK_IMAGE_TYPE_2D;
        depthInfo.format        = owner_->depthFormat_;
        depthInfo.extent        = { us, us, 1 };
        depthInfo.mipLevels     = 1;
        depthInfo.arrayLayers   = 1;
        depthInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
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
        dv.format   = owner_->depthFormat_;
        dv.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(dev, &dv, nullptr, &depthView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateImageView (depth) failed");

        // --- 6 framebuffers (one per face, sharing the depth view) ---
        for (int face = 0; face < 6; ++face) {
            VkImageView atts[] = { faceViews_[face], depthView_ };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = owner_->rtRenderPass_;
            fbInfo.attachmentCount = 2;
            fbInfo.pAttachments    = atts;
            fbInfo.width           = us;
            fbInfo.height          = us;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffers_[face]) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeBackend: vkCreateFramebuffer failed");

            faceProxies_[face].framebuffer = framebuffers_[face];
            faceProxies_[face].renderPass  = owner_->rtRenderPass_;
            faceProxies_[face].size        = size;
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
            if (faceViews_[i] != VK_NULL_HANDLE)
                vkDestroyImageView(dev, faceViews_[i], nullptr);
        }
        if (cubeView_    != VK_NULL_HANDLE) vkDestroyImageView(dev, cubeView_, nullptr);
        if (depthView_   != VK_NULL_HANDLE) vkDestroyImageView(dev, depthView_, nullptr);
        if (depthImage_  != VK_NULL_HANDLE) vkDestroyImage(dev, depthImage_, nullptr);
        if (depthMemory_ != VK_NULL_HANDLE) vkFreeMemory(dev, depthMemory_, nullptr);
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

} // namespace CNA::Internal::Backends::Vulkan

// =========================================================================
// Factory
// =========================================================================
namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_VULKAN
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Vulkan::VulkanGraphicsBackend>(args.window);
    }
#endif
}
