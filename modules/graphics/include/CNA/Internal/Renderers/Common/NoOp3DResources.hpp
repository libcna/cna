#pragma once

#include "IGraphicsRenderer.hpp"

#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Null-object 3D resources used by WarnAndStub on permanently 2D-only renderers.
     *
     * These objects deliberately retain only the minimum observable bookkeeping needed to keep
     * the public XNA resource wrappers safe. They never allocate GPU resources or render.
     */
    class NoOpVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit NoOpVertexBufferRenderer(int vertexCapacity)
            : vertexCount_(vertexCapacity)
        {
            if (vertexCapacity <= 0)
                throw std::invalid_argument(
                    "NoOpVertexBufferRenderer: vertex capacity must be positive");
        }

        void SetData(const void* /*data*/, int vertexCount, std::size_t /*strideInBytes*/) override
        {
            vertexCount_ = vertexCount;
        }

        /// The declaration is accepted and dropped: nothing here renders, and the WarnAndStub
        /// contract only requires the object to stay safe to use.
        void SetVertexDeclaration(const VertexDeclaration& /*vertexDeclaration*/) override {}

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    class NoOpIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        explicit NoOpIndexBufferRenderer(int indexCapacity)
            : indexCount_(indexCapacity)
        {
            if (indexCapacity <= 0)
                throw std::invalid_argument(
                    "NoOpIndexBufferRenderer: index capacity must be positive");
        }

        void SetData16(const void* /*data*/, int indexCount) override
        {
            indexCount_ = indexCount;
            isThirtyTwoBit_ = false;
        }

        void SetData32(const void* /*data*/, int indexCount) override
        {
            indexCount_ = indexCount;
            isThirtyTwoBit_ = true;
        }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return isThirtyTwoBit_; }

    private:
        int indexCount_;
        bool isThirtyTwoBit_ = false;
    };

    class NoOpOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        void Begin() override {}
        void End() override {}
        [[nodiscard]] bool IsComplete() const override { return true; }
        [[nodiscard]] int PixelCount() const override { return 0; }
    };

    class NoOpTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        NoOpTexture3DRenderer(int width, int height, int depth)
        {
            if (width <= 0 || height <= 0 || depth <= 0)
                throw std::invalid_argument(
                    "NoOpTexture3DRenderer: dimensions must be positive");
        }

        /// Accepts and drops the upload. Reporting false would make the shared layer raise
        /// NotSupportedException, which is exactly what WarnAndStub exists to avoid -- within
        /// that explicitly opted-in policy the null object reports the transfer as handled.
        [[nodiscard]] bool SetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/,
                                   int /*w*/, int /*h*/, int /*depth*/,
                                   const void* /*data*/, int /*dataLength*/) override
        {
            return true;
        }

        /// Reads back deterministic zeros (same rationale as SetData above).
        [[nodiscard]] bool GetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/,
                                   int /*w*/, int /*h*/, int /*depth*/,
                                   void* data, int dataLength) const override
        {
            ZeroFill(data, dataLength);
            return true;
        }

    private:
        static void ZeroFill(void* data, int dataLength)
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
        }
    };

    class NoOpTextureCubeRenderer : public ITextureCubeRenderer
    {
    public:
        explicit NoOpTextureCubeRenderer(int size)
        {
            if (size <= 0)
                throw std::invalid_argument(
                    "NoOpTextureCubeRenderer: size must be positive");
        }

        /// Accepts and drops the upload (see NoOpTexture3DRenderer::SetData's rationale).
        [[nodiscard]] bool SetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/,
                                   int /*w*/, int /*h*/,
                                   const void* /*data*/, int /*dataLength*/) override
        {
            return true;
        }

        /// Reads back deterministic zeros.
        [[nodiscard]] bool GetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/,
                                   int /*w*/, int /*h*/,
                                   void* data, int dataLength) const override
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
            return true;
        }
    };

    class NoOpRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        explicit NoOpRenderTargetCubeRenderer(int size)
            : size_(size)
        {
            if (size <= 0)
                throw std::invalid_argument(
                    "NoOpRenderTargetCubeRenderer: size must be positive");
        }

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int /*face*/) override {}
        void UnbindAsRenderTarget() override {}

        /// Reads back deterministic zeros (see NoOpTexture3DRenderer::GetData's rationale).
        [[nodiscard]] bool GetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/,
                                   int /*w*/, int /*h*/,
                                   void* data, int dataLength) const override
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
            return true;
        }

    private:
        int size_;
    };
} // namespace CNA::Internal::Renderers
