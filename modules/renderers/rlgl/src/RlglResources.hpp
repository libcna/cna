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

    /**
     * @brief Creates the current RLGL two-dimensional texture implementation.
     * @param data Dimensions, format, mip count, and level-zero bytes.
     * @return Renderer-owned texture record.
     */
    [[nodiscard]] std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data);

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
