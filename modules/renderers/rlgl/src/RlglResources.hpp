// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglRenderer;

    /** @brief Common native-texture identity shared by sampled RLGL resources. */
    class IRlglTextureResource
    {
    public:
        /** @brief Releases the renderer-local texture identity interface. */
        virtual ~IRlglTextureResource() = default;

        /** @brief Returns the rlgl-owned OpenGL texture name. */
        [[nodiscard]] virtual unsigned int NativeTextureId() const noexcept = 0;

        /** @brief Returns true when rasterization stored the texture rows bottom-up. */
        [[nodiscard]] virtual bool SampledRowsAreBottomUp() const noexcept = 0;
    };

    /**
     * @brief Creates the current RLGL two-dimensional texture implementation.
     * @param data Dimensions, format, mip count, and level-zero bytes.
     * @return Renderer-owned texture record.
     */
    [[nodiscard]] std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data);

    /** @brief Complete renderer/native RenderTarget2D facts exposed to focused validation. */
    struct RenderTargetResourceSnapshot
    {
        unsigned int framebuffer = 0;
        unsigned int resolveFramebuffer = 0;
        unsigned int colorTexture = 0;
        unsigned int multisampleColorRenderbuffer = 0;
        unsigned int depthStencilRenderbuffer = 0;
        int width = 0;
        int height = 0;
        int depthFormat = 0;
        int levelCount = 1;
        int multiSampleCount = 0;
        bool preserveContents = false;
    };

    /**
     * @brief Creates a Color RenderTarget2D resource with optional multisampling.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param depthFormat Raw XNA DepthFormat ordinal.
     * @param preserveContents Whether target contents must survive target switches.
     * @param mipMap Whether to allocate and regenerate a full mip chain.
     * @param multiSampleCount Requested sample count, clamped to the live device limit.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal; Color is the current baseline.
     * @return Renderer-owned framebuffer resource.
     */
    [[nodiscard]] std::unique_ptr<IRenderTargetRenderer> CreateRenderTargetRenderer(
        int width, int height, int depthFormat, bool preserveContents,
        bool mipMap, int multiSampleCount, int surfaceFormat);

    /**
     * @brief Captures RenderTarget2D resource state for focused validation.
     * @param resource RLGL render-target resource.
     * @return Native identities and applied creation parameters.
     */
    [[nodiscard]] RenderTargetResourceSnapshot GetRenderTargetResourceSnapshotForTesting(
        const IRenderTargetRenderer& resource);

    /**
     * @brief Creates the production low-level SpriteBatch implementation.
     * @param renderer Owning device used for viewport and sampler application.
     * @return Renderer-owned sprite scheduler.
     */
    [[nodiscard]] std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatchRenderer(
        RlglRenderer& renderer);

    /** @brief Complete renderer/native buffer facts exposed to focused validation. */
    struct BufferResourceSnapshot
    {
        unsigned int id = 0;
        int capacity = 0;
        int count = 0;
        std::size_t stride = 0;
        bool indexBuffer = false;
        bool thirtyTwoBit = false;
        int nativeByteSize = 0;
        int nativeUsage = 0;
        int ordinaryUploadCount = 0;
        int discardUploadCount = 0;
        int noOverwriteUploadCount = 0;
        std::vector<std::uint8_t> cpuBytes;
        std::vector<std::uint8_t> nativeBytes;
        std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> declaration;
    };

    /** @brief One GL-compatible attribute binding derived from an XNA vertex element. */
    struct VertexAttributeBinding
    {
        unsigned int location = 0;
        int componentCount = 0;
        int scalarType = 0;
        bool normalized = false;
        int stride = 0;
        int offset = 0;
    };

    /**
     * @brief Creates a declaration-aware fixed-capacity vertex resource.
     * @param vertexCapacity Maximum vertex count.
     * @return Renderer-owned resource; native allocation occurs when its stride is known.
     */
    [[nodiscard]] std::unique_ptr<IVertexBufferRenderer> CreateVertexBufferRenderer(
        int vertexCapacity);

    /**
     * @brief Creates a fixed-capacity index resource.
     * @param indexCapacity Maximum index count.
     * @param thirtyTwoBit True for 32-bit indices and false for 16-bit indices.
     * @return Renderer-owned resource with native storage allocated immediately.
     */
    [[nodiscard]] std::unique_ptr<IIndexBufferRenderer> CreateIndexBufferRenderer(
        int indexCapacity, bool thirtyTwoBit);

    /**
     * @brief Captures vertex-resource and native-storage state for focused validation.
     * @param resource RLGL vertex resource.
     * @return Complete resource snapshot.
     */
    [[nodiscard]] BufferResourceSnapshot GetBufferResourceSnapshotForTesting(
        const IVertexBufferRenderer& resource);

    /**
     * @brief Captures index-resource and native-storage state for focused validation.
     * @param resource RLGL index resource.
     * @return Complete resource snapshot.
     */
    [[nodiscard]] BufferResourceSnapshot GetBufferResourceSnapshotForTesting(
        const IIndexBufferRenderer& resource);

    /**
     * @brief Returns the native texture name of an RLGL Texture2D resource.
     * @param resource RLGL texture resource.
     * @return Non-zero native texture name.
     */
    [[nodiscard]] unsigned int GetNativeTextureId(const ITextureRenderer& resource);

    /**
     * @brief Reports whether a sampled resource was populated by GL rasterization.
     * @param resource RLGL texture or RenderTarget2D resource.
     * @return True for render targets and false for CPU-uploaded Texture2D resources.
     */
    [[nodiscard]] bool SampledRowsAreBottomUp(const ITextureRenderer& resource);

    /**
     * @brief Returns the native buffer name of an RLGL vertex resource.
     * @param resource RLGL vertex resource.
     * @return Non-zero native name once storage has been allocated.
     */
    [[nodiscard]] unsigned int GetNativeBufferId(const IVertexBufferRenderer& resource);

    /**
     * @brief Returns the native buffer name of an RLGL index resource.
     * @param resource RLGL index resource.
     * @return Non-zero native name.
     */
    [[nodiscard]] unsigned int GetNativeBufferId(const IIndexBufferRenderer& resource);

    /**
     * @brief Returns the uploaded stride of an RLGL vertex resource.
     * @param resource RLGL vertex resource.
     * @return Vertex stride in bytes.
     */
    [[nodiscard]] std::size_t GetVertexStride(const IVertexBufferRenderer& resource);

    /**
     * @brief Returns the fixed logical capacity of an RLGL vertex resource.
     * @param resource RLGL vertex resource.
     * @return Capacity in vertices.
     */
    [[nodiscard]] int GetBufferCapacity(const IVertexBufferRenderer& resource);

    /**
     * @brief Returns the fixed logical capacity of an RLGL index resource.
     * @param resource RLGL index resource.
     * @return Capacity in indices.
     */
    [[nodiscard]] int GetBufferCapacity(const IIndexBufferRenderer& resource);

    /**
     * @brief Returns the declaration retained by an RLGL vertex resource.
     * @param resource RLGL vertex resource.
     * @return Stable declaration element reference.
     */
    [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>&
    GetVertexDeclaration(const IVertexBufferRenderer& resource);

    /**
     * @brief Maps an XNA element format to the generic rlgl attribute description.
     * @param element Source declaration element.
     * @param location Shader attribute location selected by its semantic.
     * @param stride Complete vertex record size.
     * @param baseOffset Additional byte offset for the bound stream.
     * @return Attribute shape accepted by rlgl's generic pointer API.
     */
    [[nodiscard]] VertexAttributeBinding DescribeVertexAttribute(
        const Microsoft::Xna::Framework::Graphics::VertexElement& element,
        unsigned int location, int stride, int baseOffset = 0);
}
