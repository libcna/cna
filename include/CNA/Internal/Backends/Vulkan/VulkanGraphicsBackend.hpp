#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>

namespace CNA::Internal::Backends::Vulkan
{
    class VulkanTextureBackend : public ITextureBackend
    {
    public:
        explicit VulkanTextureBackend(const ImageData& data);
        ~VulkanTextureBackend() override;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

    private:
        int width_ = 0;
        int height_ = 0;
        // TODO: VkImage, VkDeviceMemory, VkImageView, VkSampler
    };

    class VulkanSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        VulkanSpriteBatchBackend() = default;
        ~VulkanSpriteBatchBackend() override = default;

        void Begin() override {}
        void End() override {}
        void Draw(const ITextureBackend&, float, float) override {}
        void Draw(const ITextureBackend&, const Rectangle&, const Rectangle&, const Color&) override {}
        void Draw(const ITextureBackend&, const Rectangle&, const Rectangle&, const Color&,
                  float, const Vector2&, SpriteEffects, float) override {}
        // TODO: Implement SpriteBatch with Vulkan pipeline and descriptor sets
    };

    class VulkanGraphicsBackend : public IGraphicsBackend
    {
    public:
        explicit VulkanGraphicsBackend(SDL_Window* window);
        ~VulkanGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;

        void SetVirtualResolution(int, int) override {}
        void SetPresentationMode(int) override {}

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        // 3D pipeline: not yet implemented — every entry throws std::runtime_error.
        void ClearColorAndDepth(float, float, float, float, float) override;
        void SetDepthTestEnabled(bool) override;
        void SetBlendEnabled(bool) override;
        void SetDepthWriteEnabled(bool) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int) override;
        void DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&, const Matrix&,
                                   const Matrix&, PrimitiveType, int) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                          const Matrix&, const Matrix&, const Matrix&,
                                          PrimitiveType, int) override;

    private:
        static constexpr int MaxFramesInFlight = 2;

        SDL_Window* window_ = nullptr;

        VkInstance       instance_        = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR     surface_         = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_  = VK_NULL_HANDLE;
        VkDevice         device_          = VK_NULL_HANDLE;

        uint32_t graphicsQueueFamily_ = 0;
        uint32_t presentQueueFamily_  = 0;
        VkQueue  graphicsQueue_       = VK_NULL_HANDLE;
        VkQueue  presentQueue_        = VK_NULL_HANDLE;

        VkSwapchainKHR         swapchain_      = VK_NULL_HANDLE;
        VkFormat               swapchainFormat_ = VK_FORMAT_UNDEFINED;
        VkExtent2D             swapchainExtent_ = {};
        std::vector<VkImage>   swapchainImages_;
        std::vector<VkImageView> swapchainImageViews_;

        VkRenderPass                renderPass_          = VK_NULL_HANDLE;
        std::vector<VkFramebuffer>  swapchainFramebuffers_;

        VkCommandPool                commandPool_    = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;

        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence>     inFlightFences_;

        uint32_t currentFrame_ = 0;
        float clearR_ = 0.f, clearG_ = 0.f, clearB_ = 0.f, clearA_ = 1.f;
        bool initialized_ = false;

        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapchain();
        void CreateImageViews();
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateCommandPool();
        void AllocateCommandBuffers();
        void CreateSyncObjects();
        void RecreateSwapchain();
        void CleanupSwapchain();
        void RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* data,
            void* userData);
    };
}
