#include "CNA/Internal/Backends/Skia/SkiaRenderTargetCubeBackend.hpp"

#include "CNA/Internal/Backends/Skia/SkiaTextureStorageBackends.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] std::size_t FaceRgbaBytes(int dimension)
        {
            std::size_t bytes = 0;
            if (!CheckedTexelBytes2D(static_cast<std::size_t>(dimension),
                                     static_cast<std::size_t>(dimension), 4u, bytes))
            {
                throw System::NotSupportedException(
                    "Skia RenderTargetCube size overflows the host address space.");
            }
            return bytes;
        }

        [[nodiscard]] std::size_t SixFaceSurfaceAndShadowBytes(int dimension)
        {
            std::size_t bytes = 0;
            // Six SkSurface pixel stores plus six canonical straight-RGBA transfer shadows.
            if (!CheckedSizeMultiply(FaceRgbaBytes(dimension), 12u, bytes))
            {
                throw System::NotSupportedException(
                    "Skia RenderTargetCube size overflows the host address space.");
            }
            return bytes;
        }

        [[nodiscard]] bool ValidRegion(int dimension, int x, int y,
                                       int width, int height) noexcept
        {
            return width > 0 && height > 0 && x >= 0 && y >= 0
                && width <= dimension && height <= dimension
                && x <= dimension - width && y <= dimension - height;
        }
    }

    SkiaRenderTargetCubeBackend::SkiaRenderTargetCubeBackend(
        int size, bool preserveContents, bool mipMap,
        std::weak_ptr<SkiaRenderTargetBinding> binding,
        std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : size_(size)
        , preserveContents_(preserveContents)
        , binding_(std::move(binding))
        , resourceCounters_(std::move(resourceCounters))
    {
        if (size <= 0)
            throw std::invalid_argument("Skia RenderTargetCube face size must be positive.");
        if (size > kSkiaCpuTextureMaximumAxis)
        {
            throw System::NotSupportedException(
                "Skia RenderTargetCube face size exceeds the 16384 texel axis limit.");
        }

        std::vector<int> dimensions;
        int dimension = size;
        do
        {
            const std::size_t levelBytes = SixFaceSurfaceAndShadowBytes(dimension);
            std::size_t nextTotal = 0;
            if (!CheckedSkiaResourceAccumulate(storageBytes_, levelBytes, nextTotal))
            {
                throw System::NotSupportedException(
                    "Skia RenderTargetCube exceeds the 256 MiB per-resource CPU storage limit.");
            }
            storageBytes_ = nextTotal;
            dimensions.push_back(dimension);
            dimension = std::max(1, dimension / 2);
        }
        while (mipMap && dimensions.back() > 1);

        levels_.reserve(dimensions.size());
        for (const int levelDimension : dimensions)
        {
            Level level;
            level.dimension = levelDimension;
            const std::size_t faceBytes = FaceRgbaBytes(levelDimension);
            if (faceBytes > kSkiaCpuTextureStorageLimitBytes / 12u)
                throw std::logic_error("Skia RenderTargetCube allocation escaped its checked budget.");
            for (std::size_t faceIndex = 0; faceIndex < level.faces.size(); ++faceIndex)
            {
                auto& face = level.faces[faceIndex];
                face = std::make_unique<SkiaSurface>(levelDimension, levelDimension);
                face->Clear(0.0f, 0.0f, 0.0f, 0.0f);
                level.pixels[faceIndex].assign(faceBytes, 0u);
            }
            levels_.push_back(std::move(level));
        }

        if (resourceCounters_)
        {
            resourceCounters_->AddRenderTargetCube(storageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaRenderTargetCubeBackend::~SkiaRenderTargetCubeBackend()
    {
        if (const auto binding = binding_.lock())
            binding->Detach(this);
        if (resourceRegistered_)
            resourceCounters_->RemoveRenderTargetCube(storageBytes_);
    }

    SkiaSurface* SkiaRenderTargetCubeBackend::FaceSurface(int face, int level) noexcept
    {
        if (face < 0 || face >= 6 || level < 0 || level >= static_cast<int>(levels_.size()))
            return nullptr;
        return levels_[static_cast<std::size_t>(level)].faces[static_cast<std::size_t>(face)].get();
    }

    const SkiaSurface* SkiaRenderTargetCubeBackend::FaceSurface(int face, int level) const noexcept
    {
        if (face < 0 || face >= 6 || level < 0 || level >= static_cast<int>(levels_.size()))
            return nullptr;
        return levels_[static_cast<std::size_t>(level)].faces[static_cast<std::size_t>(face)].get();
    }

    void SkiaRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (face < 0 || face >= 6)
            throw std::out_of_range("Skia RenderTargetCube face is outside [0, 5].");
        const std::shared_ptr<SkiaRenderTargetBinding> binding = binding_.lock();
        if (!binding)
            throw std::runtime_error("Skia RenderTargetCube cannot bind after backend destruction.");
        if (SkiaRasterTarget* active = binding->ActiveTarget())
            active->FinalizeWriteEXT();
        boundFace_ = face;
        binding->Bind(this, FaceSurface(face, 0));
    }

    void SkiaRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        const std::shared_ptr<SkiaRenderTargetBinding> binding = binding_.lock();
        if (!binding)
            throw std::runtime_error("Skia RenderTargetCube cannot unbind after backend destruction.");
        if (binding->ActiveTarget() == this)
            FinalizeWriteEXT();
        binding->UnbindToBackbuffer();
    }

    SkiaSurface* SkiaRenderTargetCubeBackend::BoundSurfaceEXT() noexcept
    {
        return FaceSurface(boundFace_, 0);
    }

    const SkiaSurface* SkiaRenderTargetCubeBackend::BoundSurfaceEXT() const noexcept
    {
        return FaceSurface(boundFace_, 0);
    }

    void SkiaRenderTargetCubeBackend::BeforeWriteEXT() noexcept
    {
        if (boundFace_ >= 0 && boundFace_ < 6)
            mipDirty_[static_cast<std::size_t>(boundFace_)] = true;
    }

    void SkiaRenderTargetCubeBackend::FinalizeWriteEXT()
    {
        if (boundFace_ < 0 || boundFace_ >= 6
            || !mipDirty_[static_cast<std::size_t>(boundFace_)])
        {
            return;
        }
        SynchronizeRenderedFace(boundFace_);
    }

    bool SkiaRenderTargetCubeBackend::SetData(int face, int level, int x, int y,
                                               int width, int height,
                                               const void* data, int dataLength)
    {
        SkiaSurface* surface = FaceSurface(face, level);
        if (!surface || !data || !ValidRegion(surface->Width(), x, y, width, height))
            return false;
        const std::size_t required = static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;
        if (mipDirty_[static_cast<std::size_t>(face)])
            SynchronizeRenderedFace(face);

        const auto* source = static_cast<const std::uint8_t*>(data);
        if (!surface->WritePixels(x, y, width, height, source, width * 4))
            return false;

        Level& targetLevel = levels_[static_cast<std::size_t>(level)];
        std::vector<std::uint8_t>& targetPixels =
            targetLevel.pixels[static_cast<std::size_t>(face)];
        for (int row = 0; row < height; ++row)
        {
            const std::size_t targetOffset =
                (static_cast<std::size_t>(y + row) * targetLevel.dimension + x) * 4u;
            std::copy_n(source + static_cast<std::size_t>(row) * width * 4u,
                        static_cast<std::size_t>(width) * 4u,
                        targetPixels.data() + targetOffset);
        }
        return true;
    }

    bool SkiaRenderTargetCubeBackend::GetData(int face, int level, int x, int y,
                                               int width, int height,
                                               void* data, int dataLength) const
    {
        const SkiaSurface* surface = FaceSurface(face, level);
        if (!surface || !data || !ValidRegion(surface->Width(), x, y, width, height))
            return false;
        const std::size_t required = static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;
        if (mipDirty_[static_cast<std::size_t>(face)])
            const_cast<SkiaRenderTargetCubeBackend*>(this)->SynchronizeRenderedFace(face);

        const Level& sourceLevel = levels_[static_cast<std::size_t>(level)];
        const std::vector<std::uint8_t>& sourcePixels =
            sourceLevel.pixels[static_cast<std::size_t>(face)];
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * sourceLevel.dimension + x) * 4u;
            std::copy_n(sourcePixels.data() + sourceOffset,
                        static_cast<std::size_t>(width) * 4u,
                        destination + static_cast<std::size_t>(row) * width * 4u);
        }
        return true;
    }

    void SkiaRenderTargetCubeBackend::SynchronizeRenderedFace(int face)
    {
        if (face < 0 || face >= 6 || !mipDirty_[static_cast<std::size_t>(face)])
            return;

        std::vector<std::uint8_t>& levelZero = levels_[0].pixels[static_cast<std::size_t>(face)];
        if (!FaceSurface(face, 0)->ReadPixels(0, 0, size_, size_, levelZero.data(), size_ * 4))
            throw std::runtime_error("Skia RenderTargetCube failed to synchronize a rendered face.");
        GenerateMipChain(face);
        mipDirty_[static_cast<std::size_t>(face)] = false;
    }

    void SkiaRenderTargetCubeBackend::GenerateMipChain(int face)
    {
        for (std::size_t levelIndex = 1; levelIndex < levels_.size(); ++levelIndex)
        {
            SkiaSurface* targetSurface = FaceSurface(face, static_cast<int>(levelIndex));
            const int sourceDimension = levels_[levelIndex - 1].dimension;
            const int targetDimension = targetSurface->Width();
            const std::vector<std::uint8_t>& source =
                levels_[levelIndex - 1].pixels[static_cast<std::size_t>(face)];
            std::vector<std::uint8_t>& target =
                levels_[levelIndex].pixels[static_cast<std::size_t>(face)];
            std::fill(target.begin(), target.end(), 0u);

            for (int y = 0; y < targetDimension; ++y)
            {
                for (int x = 0; x < targetDimension; ++x)
                {
                    const int sourceX = x * 2;
                    const int sourceY = y * 2;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        unsigned int sum = 0;
                        unsigned int count = 0;
                        for (int dy = 0; dy < 2 && sourceY + dy < sourceDimension; ++dy)
                        {
                            for (int dx = 0; dx < 2 && sourceX + dx < sourceDimension; ++dx)
                            {
                                const std::size_t sourceOffset =
                                    (static_cast<std::size_t>(sourceY + dy) * sourceDimension
                                     + sourceX + dx) * 4u + static_cast<std::size_t>(channel);
                                sum += source[sourceOffset];
                                ++count;
                            }
                        }
                        const std::size_t targetOffset =
                            (static_cast<std::size_t>(y) * targetDimension + x) * 4u
                            + static_cast<std::size_t>(channel);
                        target[targetOffset] = static_cast<std::uint8_t>((sum + count / 2u) / count);
                    }
                }
            }
            if (!targetSurface->WritePixels(0, 0, targetDimension, targetDimension,
                                            target.data(), targetDimension * 4))
            {
                throw std::runtime_error("Skia RenderTargetCube failed to generate a mip level.");
            }
        }
    }
} // namespace CNA::Internal::Backends::Skia
