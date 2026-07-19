#pragma once

#include "IGraphicsBackend.hpp"

#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Backends
{
    /**
     * @brief Null-object 3D resources used by WarnAndStub on permanently 2D-only backends.
     *
     * These objects deliberately retain only the minimum observable bookkeeping needed to keep
     * the public XNA resource wrappers safe. They never allocate GPU resources or render.
     */
    class NoOpVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        explicit NoOpVertexBufferBackend(int vertexCapacity)
            : vertexCount_(vertexCapacity)
        {
            if (vertexCapacity <= 0)
                throw std::invalid_argument(
                    "NoOpVertexBufferBackend: vertex capacity must be positive");
        }

        void SetData(const void* /*data*/, int vertexCount, std::size_t /*strideInBytes*/) override
        {
            vertexCount_ = vertexCount;
        }

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    class NoOpIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        explicit NoOpIndexBufferBackend(int indexCapacity)
            : indexCount_(indexCapacity)
        {
            if (indexCapacity <= 0)
                throw std::invalid_argument(
                    "NoOpIndexBufferBackend: index capacity must be positive");
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

    class NoOpOcclusionQueryBackend final : public IOcclusionQueryBackend
    {
    public:
        void Begin() override {}
        void End() override {}
        [[nodiscard]] bool IsComplete() const override { return true; }
        [[nodiscard]] int PixelCount() const override { return 0; }
    };

    class NoOpTexture3DBackend final : public ITexture3DBackend
    {
    public:
        NoOpTexture3DBackend(int width, int height, int depth)
        {
            if (width <= 0 || height <= 0 || depth <= 0)
                throw std::invalid_argument(
                    "NoOpTexture3DBackend: dimensions must be positive");
        }

        void SetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/,
                     int /*w*/, int /*h*/, int /*depth*/,
                     const void* /*data*/, int /*dataLength*/) override
        {
        }

        void GetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/,
                     int /*w*/, int /*h*/, int /*depth*/,
                     void* data, int dataLength) const override
        {
            ZeroFill(data, dataLength);
        }

    private:
        static void ZeroFill(void* data, int dataLength)
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
        }
    };

    class NoOpTextureCubeBackend : public ITextureCubeBackend
    {
    public:
        explicit NoOpTextureCubeBackend(int size)
        {
            if (size <= 0)
                throw std::invalid_argument(
                    "NoOpTextureCubeBackend: size must be positive");
        }

        void SetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                     const void* /*data*/, int /*dataLength*/) override
        {
        }

        void GetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                     void* data, int dataLength) const override
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
        }
    };

    class NoOpRenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        explicit NoOpRenderTargetCubeBackend(int size)
            : size_(size)
        {
            if (size <= 0)
                throw std::invalid_argument(
                    "NoOpRenderTargetCubeBackend: size must be positive");
        }

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int /*face*/) override {}
        void UnbindAsRenderTarget() override {}

        void GetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                     void* data, int dataLength) const override
        {
            if (data != nullptr && dataLength > 0)
                std::memset(data, 0, static_cast<std::size_t>(dataLength));
        }

    private:
        int size_;
    };
} // namespace CNA::Internal::Backends
