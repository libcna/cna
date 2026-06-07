#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Vulkan/shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
        backend_->activeBatches_.push_back(this);
    }

    void VulkanSpriteBatchBackend::FlushTexture()
    {
        if (!currentTexture_) return;
        uint32_t count = static_cast<uint32_t>(indices_.size()) - batchFirstIndex_;
        if (count == 0) return;
        draws_.push_back({ currentTexture_->GetDescriptorSet(), batchFirstIndex_, count });
        batchFirstIndex_ = static_cast<uint32_t>(indices_.size());
    }

    void VulkanSpriteBatchBackend::End()
    {
        if (!active_) return;
        FlushTexture();
        active_ = false;
    }

    void VulkanSpriteBatchBackend::ConsumeDraws()
    {
        vertices_.clear();
        indices_.clear();
        draws_.clear();
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

        auto& vkTex = static_cast<const VulkanTextureBackend&>(texture);
        if (currentTexture_ != nullptr && currentTexture_ != &vkTex)
            FlushTexture();
        currentTexture_ = &vkTex;

        float tw = static_cast<float>(vkTex.GetWidth());
        float th = static_cast<float>(vkTex.GetHeight());

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

    VulkanIndexBufferBackend::VulkanIndexBufferBackend(int index_capacity,
                                                       VulkanGraphicsBackend* owner)
        : capacity_(index_capacity), owner_(owner)
    {
        VkDeviceSize size = static_cast<VkDeviceSize>(index_capacity) * sizeof(uint16_t);
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
        CreateFrame3DBuffers();
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
        // Externally-owned vertex/index buffers (C++ objects may outlive this destructor).
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
            if (frame3DVB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DVB_[i], nullptr);   frame3DVB_[i]    = VK_NULL_HANDLE; }
            if (frame3DVBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DVBMem_[i], nullptr);   frame3DVBMem_[i] = VK_NULL_HANDLE; }
            if (frame3DIB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DIB_[i], nullptr);   frame3DIB_[i]    = VK_NULL_HANDLE; }
            if (frame3DIBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DIBMem_[i], nullptr);   frame3DIBMem_[i] = VK_NULL_HANDLE; }
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
        if (defaultSampler_      != VK_NULL_HANDLE) { vkDestroySampler(device_, defaultSampler_, nullptr);                  defaultSampler_      = VK_NULL_HANDLE; }

        // Step 5: destroy pipelines, render pass, and framebuffers.
        for (auto& [key, pipe] : pipelines3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelines3D_.clear();
        if (pipeline2D_       != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipeline2D_, nullptr);             pipeline2D_       = VK_NULL_HANDLE; }
        if (pipelineLayout3D_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout3D_, nullptr); pipelineLayout3D_ = VK_NULL_HANDLE; }
        if (pipelineLayout2D_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout2D_, nullptr); pipelineLayout2D_ = VK_NULL_HANDLE; }
        for (auto fb : swapchainFramebuffers_)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        if (renderPass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }

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
        VkPhysicalDeviceFeatures feat{};
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
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses   = &sub;
        ci.dependencyCount = 1; ci.pDependencies = &dep;
        if (vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
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

        // Dynamic viewport/scissor so resize doesn't require pipeline recreation
        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

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
                              bool blend, int cullMode)
    {
        uint32_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        return t | (depthTest ? 4u : 0u) | (depthWrite ? 8u : 0u) | (blend ? 16u : 0u)
                 | (static_cast<uint32_t>(cullMode & 0x3) << 5);
    }

    VkPipeline VulkanGraphicsBackend::GetOrCreatePipeline3D(VkPrimitiveTopology topo,
                                                             bool depthTest, bool depthWrite,
                                                             bool blend, int cullMode)
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

        uint32_t key = Make3DKey(topo, depthTest, depthWrite, blend, cullMode);
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
        rs.polygonMode = VK_POLYGON_MODE_FILL;
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

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cba;

        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

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
        pci.renderPass          = renderPass_;
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

        VkClearValue cv[2]{};
        cv[0].color          = { { clearR_, clearG_, clearB_, clearA_ } };
        cv[1].depthStencil   = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{};
        rp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass  = renderPass_;
        rp.framebuffer = swapchainFramebuffers_[imageIndex];
        rp.renderArea  = { {0,0}, swapchainExtent_ };
        rp.clearValueCount = 2;
        rp.pClearValues    = cv;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)swapchainExtent_.width;
        vp.height = (float)swapchainExtent_.height;
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc{ {0,0}, swapchainExtent_ };
        vkCmdSetScissor(cb, 0, 1, &sc);

        // ----- 2D sprite batches -----
        bool sprite2DPipelineBound = false;
        for (auto* batch : activeBatches_) {
            const auto& verts = batch->GetVertices();
            const auto& inds  = batch->GetIndices();
            const auto& draws = batch->GetDrawCalls();
            if (verts.empty() || draws.empty()) continue;

            uint32_t vbytes = static_cast<uint32_t>(verts.size() * sizeof(Sprite2DVertex));
            uint32_t ibytes = static_cast<uint32_t>(inds.size()  * sizeof(uint16_t));
            std::memcpy(spriteVBPtr_[currentFrame_], verts.data(), vbytes);
            std::memcpy(spriteIBPtr_[currentFrame_], inds.data(),  ibytes);

            if (!sprite2DPipelineBound) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline2D_);
                sprite2DPipelineBound = true;
            }
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &spriteVB_[currentFrame_], &off);
            vkCmdBindIndexBuffer(cb, spriteIB_[currentFrame_], 0, VK_INDEX_TYPE_UINT16);

            float vpSize[2] = { (float)swapchainExtent_.width, (float)swapchainExtent_.height };
            vkCmdPushConstants(cb, pipelineLayout2D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, vpSize);

            for (const auto& d : draws) {
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout2D_, 0, 1, &d.descSet, 0, nullptr);
                vkCmdDrawIndexed(cb, d.indexCount, 1, d.firstIndex, 0, 0);
            }
            batch->ConsumeDraws();
        }
        activeBatches_.clear();

        // ----- 3D draws -----
        // Upload all pending vertex/index data into this frame's ring buffers, then draw.
        VkPipeline lastPipeline3D = VK_NULL_HANDLE;
        VkDeviceSize vbOffset3D = 0;
        VkDeviceSize ibOffset3D = 0;
        for (const auto& draw : pending3D_) {
            if (draw.vbData.empty()) continue;
            if (vbOffset3D + draw.vbData.size() > kFrame3DVBSize) continue; // overflow guard
            if (!draw.ibData.empty() && ibOffset3D + draw.ibData.size() > kFrame3DIBSize) continue;

            std::memcpy(static_cast<uint8_t*>(frame3DVBPtr_[currentFrame_]) + vbOffset3D,
                        draw.vbData.data(), draw.vbData.size());
            if (!draw.ibData.empty())
                std::memcpy(static_cast<uint8_t*>(frame3DIBPtr_[currentFrame_]) + ibOffset3D,
                            draw.ibData.data(), draw.ibData.size());

            VkPipeline pipe = GetOrCreatePipeline3D(draw.topology,
                                                    draw.depthTest, draw.depthWrite,
                                                    draw.blend, draw.cullMode);
            if (pipe != lastPipeline3D) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                lastPipeline3D = pipe;
            }
            vkCmdPushConstants(cb, pipelineLayout3D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, draw.mvp);
            vkCmdBindVertexBuffers(cb, 0, 1, &frame3DVB_[currentFrame_], &vbOffset3D);
            if (!draw.ibData.empty()) {
                vkCmdBindIndexBuffer(cb, frame3DIB_[currentFrame_], ibOffset3D, VK_INDEX_TYPE_UINT16);
                vkCmdDrawIndexed(cb, draw.drawCount, 1, 0, 0, 0);
                ibOffset3D += static_cast<VkDeviceSize>(draw.ibData.size());
            } else {
                vkCmdDraw(cb, draw.drawCount, 1, 0, 0);
            }
            vbOffset3D += static_cast<VkDeviceSize>(draw.vbData.size());
        }
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

        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void VulkanGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SDL_GetWindowSize(window_, &width, &height);
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

    std::unique_ptr<IVertexBufferBackend> VulkanGraphicsBackend::CreateVertexBuffer(int cap)
    {
        auto vb = std::make_unique<VulkanVertexBufferBackend>(cap, this);
        liveVertexBuffers_.push_back(vb.get());
        return vb;
    }

    std::unique_ptr<IIndexBufferBackend> VulkanGraphicsBackend::CreateIndexBuffer16(int cap)
    {
        auto ib = std::make_unique<VulkanIndexBufferBackend>(cap, this);
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
        wvp.ToColumnMajor(d.mvp);

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(), drawCount * stride);

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = drawCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.blend      = blendEnabled_;
        d.cullMode   = cullMode_;
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
        wvp.ToColumnMajor(d.mvp);

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
                                                        int /*depthFunc*/)
    {
        depthTestEnabled_  = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
    }

    void VulkanGraphicsBackend::ApplyRasterizerState(int cullMode, int /*fillMode*/,
                                                      bool /*scissorTestEnable*/)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // Stored; folded into the pipeline key at draw time.
        // FillMode::WireFrame and scissor not supported in this backend — silently ignored.
        cullMode_ = cullMode;
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
