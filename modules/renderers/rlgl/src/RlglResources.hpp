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
}
