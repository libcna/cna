#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace CNA::Internal::Backends::Vulkan
{
    class VulkanGraphicsBackend;  // forward

    // -------------------------------------------------------------------------
    // Vertex types (internal to the Vulkan backend)
    // -------------------------------------------------------------------------

    struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };

    // -------------------------------------------------------------------------
    // VulkanTextureBackend
    // -------------------------------------------------------------------------

    class VulkanTextureBackend : public ITextureBackend
    {
    public:
        explicit VulkanTextureBackend(const ImageData& data, VulkanGraphicsBackend* owner);
        ~VulkanTextureBackend() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        VkDescriptorSet GetDescriptorSet() const { return descriptorSet_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        int                 width_         = 0;
        int                 height_        = 0;
        VkImage             image_         = VK_NULL_HANDLE;
        VkDeviceMemory      memory_        = VK_NULL_HANDLE;
        VkImageView         imageView_     = VK_NULL_HANDLE;
        VkDescriptorSet     descriptorSet_ = VK_NULL_HANDLE;
        VulkanGraphicsBackend* owner_      = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanSpriteBatchBackend
    // -------------------------------------------------------------------------

    class VulkanSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        explicit VulkanSpriteBatchBackend(VulkanGraphicsBackend* backend);
        ~VulkanSpriteBatchBackend() override = default;

        void Begin() override;
        void End()   override;

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects,
                  float layerDepth) override;

        // Accessors for VulkanGraphicsBackend to harvest draw data at Present()
        const std::vector<Sprite2DVertex>& GetVertices() const { return vertices_; }
        const std::vector<uint16_t>&       GetIndices()  const { return indices_; }

        struct DrawCall { VkDescriptorSet descSet; uint32_t firstIndex; uint32_t indexCount; };
        const std::vector<DrawCall>& GetDrawCalls() const { return draws_; }

        void ConsumeDraws();  // called by backend after upload — clears vectors

    private:
        VulkanGraphicsBackend*           backend_        = nullptr;
        bool                             active_         = false;
        std::vector<Sprite2DVertex>      vertices_;
        std::vector<uint16_t>            indices_;
        std::vector<DrawCall>            draws_;
        const VulkanTextureBackend*      currentTexture_ = nullptr;
        uint32_t                         batchFirstIndex_= 0;

        void FlushTexture();
    };

    // -------------------------------------------------------------------------
    // VulkanVertexBufferBackend
    // -------------------------------------------------------------------------

    class VulkanVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        explicit VulkanVertexBufferBackend(int vertex_capacity, VulkanGraphicsBackend* owner);
        ~VulkanVertexBufferBackend() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        int  GetVertexCount() const override { return vertexCount_; }

        VkBuffer GetBuffer()     const { return buffer_; }
        int      GetCapacity()   const { return capacity_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        VkBuffer                buffer_      = VK_NULL_HANDLE;
        VkDeviceMemory          memory_      = VK_NULL_HANDLE;
        void*                   mappedPtr_   = nullptr;
        int                     capacity_    = 0;
        int                     vertexCount_ = 0;
        VulkanGraphicsBackend*  owner_       = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanIndexBufferBackend
    // -------------------------------------------------------------------------

    class VulkanIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        explicit VulkanIndexBufferBackend(int index_capacity, VulkanGraphicsBackend* owner);
        ~VulkanIndexBufferBackend() override;

        void SetData16(const void* data, int index_count) override;
        int  GetIndexCount() const override { return indexCount_; }

        VkBuffer GetBuffer() const { return buffer_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        VkBuffer                buffer_     = VK_NULL_HANDLE;
        VkDeviceMemory          memory_     = VK_NULL_HANDLE;
        void*                   mappedPtr_  = nullptr;
        int                     capacity_   = 0;
        int                     indexCount_ = 0;
        VulkanGraphicsBackend*  owner_      = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanGraphicsBackend
    // -------------------------------------------------------------------------

    class VulkanGraphicsBackend : public IGraphicsBackend
    {
        friend class VulkanTextureBackend;
        friend class VulkanVertexBufferBackend;
        friend class VulkanIndexBufferBackend;
        friend class VulkanSpriteBatchBackend;

    public:
        explicit VulkanGraphicsBackend(SDL_Window* window);
        ~VulkanGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;

        void SetVirtualResolution(int, int) override {}
        void SetPresentationMode(int)       override {}

        SDL_Window*  GetWindowInternal()   const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend>      CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend>  CreateSpriteBatch() override;

        void ClearColorAndDepth(float, float, float, float, float) override;
        void SetDepthTestEnabled(bool)  override;
        void SetBlendEnabled(bool)      override;
        void SetDepthWriteEnabled(bool) override;
        std::unique_ptr<IVertexBufferBackend>  CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend>   CreateIndexBuffer16(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend&,
                                   const Matrix&, const Matrix&, const Matrix&,
                                   PrimitiveType, int) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                          const IIndexBufferBackend&,
                                          const Matrix&, const Matrix&, const Matrix&,
                                          PrimitiveType, int) override;

    private:
        // --- Constants ---
        static constexpr int      MaxFramesInFlight = 2;
        static constexpr uint32_t MaxSpriteVertices = 32768;
        static constexpr uint32_t MaxSpriteIndices  = MaxSpriteVertices / 4 * 6;
        static constexpr uint32_t MaxDescriptorSets = 512;

        // --- Core Vulkan objects (lifetime = backend) ---
        SDL_Window*      window_         = nullptr;
        VkInstance       instance_       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice         device_         = VK_NULL_HANDLE;

        uint32_t graphicsQueueFamily_ = 0;
        uint32_t presentQueueFamily_  = 0;
        VkQueue  graphicsQueue_       = VK_NULL_HANDLE;
        VkQueue  presentQueue_        = VK_NULL_HANDLE;

        VkCommandPool                commandPool_    = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;

        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence>     inFlightFences_;

        // --- Swapchain (recreated on resize) ---
        VkSwapchainKHR           swapchain_       = VK_NULL_HANDLE;
        VkFormat                 swapchainFormat_ = VK_FORMAT_UNDEFINED;
        VkExtent2D               swapchainExtent_ = {};
        std::vector<VkImage>     swapchainImages_;
        std::vector<VkImageView> swapchainImageViews_;

        // --- Render pass (permanent) + framebuffers (per swapchain image) ---
        VkRenderPass               renderPass_ = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchainFramebuffers_;

        // --- Depth buffer (recreated with swapchain) ---
        VkFormat        depthFormat_    = VK_FORMAT_UNDEFINED;
        VkImage         depthImage_     = VK_NULL_HANDLE;
        VkDeviceMemory  depthMemory_    = VK_NULL_HANDLE;
        VkImageView     depthImageView_ = VK_NULL_HANDLE;

        // --- Pipeline resources (permanent) ---
        VkSampler             defaultSampler_        = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_   = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool_        = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout2D_      = VK_NULL_HANDLE;
        VkPipeline            pipeline2D_            = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout3D_      = VK_NULL_HANDLE;
        std::unordered_map<uint32_t, VkPipeline>             pipelines3D_;

        // --- Sprite batch GPU buffers (host-visible, one per frame-in-flight) ---
        std::array<VkBuffer,       MaxFramesInFlight> spriteVB_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> spriteVBMem_ = {};
        std::array<void*,          MaxFramesInFlight> spriteVBPtr_ = {};
        std::array<VkBuffer,       MaxFramesInFlight> spriteIB_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> spriteIBMem_ = {};
        std::array<void*,          MaxFramesInFlight> spriteIBPtr_ = {};

        // --- Lifetime tracking for externally-owned Vulkan resources ---
        std::vector<VulkanTextureBackend*>      liveTextures_;
        std::vector<VulkanVertexBufferBackend*> liveVertexBuffers_;
        std::vector<VulkanIndexBufferBackend*>  liveIndexBuffers_;

        // --- Per-frame accumulated draw data (cleared in Present) ---
        struct Pending3DDraw {
            VkBuffer            vb;
            VkBuffer            ib;          // VK_NULL_HANDLE = non-indexed
            VkPrimitiveTopology topology;
            uint32_t            drawCount;   // vertex or index count
            float               mvp[16];
            bool                depthTest;
            bool                depthWrite;
            bool                blend;
        };
        std::vector<Pending3DDraw>                    pending3D_;
        std::vector<VulkanSpriteBatchBackend*>        activeBatches_;

        // --- Frame state ---
        uint32_t currentFrame_ = 0;
        float    clearR_ = 0.f, clearG_ = 0.f, clearB_ = 0.f, clearA_ = 1.f;
        bool     initialized_       = false;
        bool     depthTestEnabled_  = true;
        bool     depthWriteEnabled_ = true;
        bool     blendEnabled_      = false;

        // ---- Init helpers ----
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
        void CreateSampler();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void CreatePipeline2D();
        VkFormat   FindDepthFormat() const;
        void       CreateDepthResources();
        void       CleanupDepthResources();
        VkPipeline GetOrCreatePipeline3D(VkPrimitiveTopology, bool depthTest, bool depthWrite, bool blend);
        void CreateSpriteBuffers();

        // ---- Swapchain lifecycle ----
        void RecreateSwapchain();
        void CleanupSwapchain();

        // ---- Frame recording ----
        void RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);

        // ---- Memory / resource helpers (also used by texture/buffer backends) ----
        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
        void     CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& buf, VkDeviceMemory& mem, void** mapped = nullptr);
        VkCommandBuffer BeginOneTimeCommands();
        void            EndOneTimeCommands(VkCommandBuffer cb);
        void TransitionImageLayout(VkImage img, VkImageLayout from, VkImageLayout to);
        void CopyBufferToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h);
        VkShaderModule  CreateShaderModule(const uint32_t* spv, size_t byteSize);

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT*,
            void*);
    };
}
