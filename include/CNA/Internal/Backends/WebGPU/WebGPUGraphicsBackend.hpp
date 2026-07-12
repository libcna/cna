#pragma once

#include "../Common/IGraphicsBackend.hpp"

#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU backend requires webgpu.h from wgpu-native"
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::WebGPU
{
    class WebGPUGraphicsBackend;

    class WebGPUTextureBackend final : public ITextureBackend
    {
    public:
        WebGPUTextureBackend(WebGPUGraphicsBackend& owner, const ImageData& data);
        ~WebGPUTextureBackend() override;

        WebGPUTextureBackend(const WebGPUTextureBackend&) = delete;
        WebGPUTextureBackend& operator=(const WebGPUTextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }
        [[nodiscard]] WGPUTextureView View() const { return view_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
    };

    class WebGPUVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        WebGPUVertexBufferBackend(WebGPUGraphicsBackend& owner, int vertexCapacity);
        ~WebGPUVertexBufferBackend() override;

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes, SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
    };

    class WebGPUIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        WebGPUIndexBufferBackend(WebGPUGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit);
        ~WebGPUIndexBufferBackend() override;

        void SetData16(const void* data, int indexCount) override;
        void SetData32(const void* data, int indexCount) override;
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }

    private:
        void Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit);

        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    class WebGPUSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit WebGPUSpriteBatchBackend(WebGPUGraphicsBackend& owner);
        ~WebGPUSpriteBatchBackend() override = default;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };

    class WebGPUGraphicsBackend final : public IGraphicsBackend
    {
    public:
        struct SpriteVertex
        {
            float position[3];
            float uv[2];
            float color[4];
        };

        struct SpriteCommand
        {
            const WebGPUTextureBackend* texture = nullptr;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
        };

        WebGPUGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                              CnaPresentationMode presentationMode, int swapInterval);
        ~WebGPUGraphicsBackend() override;

        WebGPUGraphicsBackend(const WebGPUGraphicsBackend&) = delete;
        WebGPUGraphicsBackend& operator=(const WebGPUGraphicsBackend&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        bool TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const override;
        bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override { depthTestEnabled_ = enabled; }
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        void QueueSprite(const WebGPUTextureBackend& texture,
                         const Rectangle& destinationRectangle,
                         const Rectangle& sourceRectangle,
                         const Color& color,
                         float rotation,
                         const Vector2& origin,
                         SpriteEffects effects,
                         float layerDepth,
                         const Matrix& transform,
                         int textureFilter,
                         int addressU,
                         int addressV);

        [[nodiscard]] WGPUDevice Device() const { return device_; }
        [[nodiscard]] WGPUQueue Queue() const { return queue_; }

    private:
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        void CreateSurface();
        void RequestAdapterAndDevice();
        void ConfigureSurface(bool force = false);
        void CreateSpriteResources();
        void DestroySpriteResources();
        void RecreateDepthTexture();
        void RenderSprites(WGPURenderPassEncoder pass);
        void CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture);
        // Acquires a swapchain texture if none is currently held, and renders any pending
        // Clear()/sprite work into it. Called on demand by both Present() and ReadBackbuffer()
        // so that GetBackBufferData() can observe work queued earlier in the same logical frame,
        // matching the Vulkan/Bgfx backends' own on-demand-submit readback semantics. Returns
        // false if the surface isn't presentable right now (minimized, lost, etc).
        bool EnsureFrameRendered();
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        [[nodiscard]] WGPUSampler GetOrCreateSampler(int textureFilter, int addressU, int addressV);
        [[nodiscard]] WGPUPrimitiveTopology ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;
        [[noreturn]] static void ThrowUnsupported3DDraw(const char* method);

        SDL_Window* window_ = nullptr;
        void* metalView_ = nullptr;
        WGPUInstance instance_ = nullptr;
        WGPUSurface surface_ = nullptr;
        WGPUAdapter adapter_ = nullptr;
        WGPUDevice device_ = nullptr;
        WGPUQueue queue_ = nullptr;
        WGPUSurfaceConfiguration surfaceConfig_{};
        WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;

        WGPUShaderModule spriteShader_ = nullptr;
        WGPUBindGroupLayout spriteBindGroupLayout_ = nullptr;
        WGPUPipelineLayout spritePipelineLayout_ = nullptr;
        WGPURenderPipeline spritePipelineBlend_ = nullptr;
        WGPURenderPipeline spritePipelineOpaque_ = nullptr;
        WGPUBuffer spriteVertexBuffer_ = nullptr;
        std::uint64_t spriteVertexCapacityBytes_ = 0;
        std::array<WGPUSampler, 18> samplerCache_{};

        std::vector<SpriteCommand> spriteCommands_;
        int physicalWidth_ = 0;
        int physicalHeight_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool surfaceConfigured_ = false;
        bool clearColorPending_ = true;
        bool clearDepthPending_ = true;
        bool clearStencilPending_ = false;
        WGPUColor clearColor_{0.0, 0.0, 0.0, 1.0};
        float clearDepth_ = 1.0f;
        std::uint32_t clearStencil_ = 0;
        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = false;
        bool blendEnabled_ = true;

        WGPUBuffer readbackBuffer_ = nullptr;
        std::uint64_t readbackBufferCapacity_ = 0;
        std::uint32_t readbackBytesPerRow_ = 0;
        int readbackWidth_ = 0;
        int readbackHeight_ = 0;
        bool readbackValid_ = false;

        bool hasAcquiredTexture_ = false;
        WGPUTexture acquiredTexture_ = nullptr;
        bool framePending_ = true;
    };
}
