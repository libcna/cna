// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        [[nodiscard]] int MaximumMipLevels(int width, int height)
        {
            int levels = 1;
            while (width > 1 || height > 1)
            {
                width = std::max(1, width / 2);
                height = std::max(1, height / 2);
                ++levels;
            }
            return levels;
        }

        [[nodiscard]] int MipSize(const int base, const int level)
        {
            return std::max(1, base >> level);
        }

        [[nodiscard]] std::size_t VolumeByteCount(
            const int width, const int height, const int depth)
        {
            const std::uint64_t bytes = static_cast<std::uint64_t>(width) *
                static_cast<std::uint64_t>(height) *
                static_cast<std::uint64_t>(depth) * 4u;
            if (bytes > SIZE_MAX)
                throw std::overflow_error("RLGL: Texture3D byte count overflow");
            return static_cast<std::size_t>(bytes);
        }

        class RlglTexture3DRenderer final
            : public ITexture3DRenderer,
              public IRlglTextureResource,
              public IRlglNativeResource
        {
        public:
            RlglTexture3DRenderer(
                const int width, const int height, const int depth,
                const bool mipMap, const int surfaceFormat,
                std::shared_ptr<RlglResourceLifetime> lifetime)
                : width_(width)
                , height_(height)
                , depth_(depth)
                , levelCount_(mipMap ? MaximumMipLevels(width, height) : 1)
                , surfaceFormat_(surfaceFormat)
                , lifetime_(std::move(lifetime))
            {
                if (width_ <= 0 || height_ <= 0 || depth_ <= 0)
                    throw std::invalid_argument("RLGL: Texture3D dimensions must be positive");
                if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
                {
                    throw std::invalid_argument(
                        "RLGL: Texture3D currently requires SurfaceFormat.Color");
                }

                id_ = Bridge::CreateTexture3DColor(
                    width_, height_, depth_, levelCount_);
                bool registered = false;
                try
                {
                    recoveryRegistered_ = lifetime_->Register(*this);
                    registered = true;
                    if (recoveryRegistered_)
                    {
                        definedLevels_.assign(
                            static_cast<std::size_t>(levelCount_), false);
                        recoveryLevels_.resize(
                            static_cast<std::size_t>(levelCount_));
                    }
                }
                catch (...)
                {
                    if (registered) lifetime_->Dispose(*this);
                    else ReleaseNativeResource();
                    throw;
                }
            }

            ~RlglTexture3DRenderer() override
            {
                lifetime_->Dispose(*this);
            }

            RlglTexture3DRenderer(const RlglTexture3DRenderer&) = delete;
            RlglTexture3DRenderer& operator=(const RlglTexture3DRenderer&) = delete;

            [[nodiscard]] bool SetData(
                const int level, const int x, const int y, const int z,
                const int width, const int height, const int depth,
                const void* const data, const int dataLength) override
            {
                const Dimensions dimensions = ValidateRegion(
                    level, x, y, z, width, height, depth, data);
                const std::size_t required = VolumeByteCount(width, height, depth);
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: Texture3D transfer buffer is too small");

                std::vector<std::uint8_t> recoveryCopy;
                if (recoveryRegistered_)
                {
                    recoveryCopy = PrepareRecoveryShadow(
                        level, dimensions, x, y, z, width, height, depth,
                        static_cast<const std::uint8_t*>(data));
                }
                Bridge::UpdateTexture3DColor(
                    id_, level, x, y, z, width, height, depth,
                    static_cast<const std::uint8_t*>(data));
                if (recoveryRegistered_)
                {
                    recoveryLevels_[static_cast<std::size_t>(level)] =
                        std::move(recoveryCopy);
                    definedLevels_[static_cast<std::size_t>(level)] = true;
                }
                return true;
            }

            [[nodiscard]] bool GetData(
                const int level, const int x, const int y, const int z,
                const int width, const int height, const int depth,
                void* const data, const int dataLength) const override
            {
                const Dimensions dimensions = ValidateRegion(
                    level, x, y, z, width, height, depth, data);
                const std::size_t required = VolumeByteCount(width, height, depth);
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: Texture3D readback buffer is too small");
                Bridge::ReadTexture3DColor(
                    id_, level,
                    dimensions.width, dimensions.height, dimensions.depth,
                    x, y, z, width, height, depth,
                    static_cast<std::uint8_t*>(data));
                return true;
            }

            void BindGL(const int unit) const override
            {
                Bridge::BindTexture3D(id_, unit);
            }

            void GetDimensionsEXT(
                int& width, int& height, int& depth) const noexcept override
            {
                width = width_;
                height = height_;
                depth = depth_;
            }

            [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override
            {
                return surfaceFormat_;
            }

            [[nodiscard]] unsigned int NativeTextureId() const noexcept override
            {
                return id_;
            }

            [[nodiscard]] bool SampledRowsAreBottomUp() const noexcept override
            {
                return false;
            }

            [[nodiscard]] Texture3DResourceSnapshot Snapshot() const
            {
                return {
                    id_, width_, height_, depth_, levelCount_, surfaceFormat_,
                    recoveryRegistered_, definedLevels_, recoveryLevels_};
            }

        private:
            struct Dimensions
            {
                int width;
                int height;
                int depth;
            };

            void ReleaseNativeResource() noexcept override
            {
                Bridge::DestroyTexture3D(id_);
                id_ = 0;
            }

            void InvalidateNativeResource() noexcept override
            {
                id_ = 0;
            }

            void RecreateNativeResource() override
            {
                const unsigned int replacement = Bridge::CreateTexture3DColor(
                    width_, height_, depth_, levelCount_);
                try
                {
                    for (int level = 0; level < levelCount_; ++level)
                    {
                        if (!definedLevels_[static_cast<std::size_t>(level)]) continue;
                        const auto& bytes = recoveryLevels_[static_cast<std::size_t>(level)];
                        if (bytes.empty())
                        {
                            throw std::runtime_error(
                                "RLGL: Texture3D recovery shadow is incomplete");
                        }
                        Bridge::UpdateTexture3DColor(
                            replacement, level, 0, 0, 0,
                            MipSize(width_, level), MipSize(height_, level),
                            MipSize(depth_, level), bytes.data());
                    }
                }
                catch (...)
                {
                    Bridge::DestroyTexture3D(replacement);
                    throw;
                }
                id_ = replacement;
            }

            [[nodiscard]] RlglResourceRecoveryInfo GetRecoveryInfo() const noexcept override
            {
                RlglResourceRecoveryInfo info;
                info.definedTextureSubresources = static_cast<std::size_t>(
                    std::count(definedLevels_.begin(), definedLevels_.end(), true));
                for (const auto& level : recoveryLevels_)
                    info.retainedCpuBytes += level.size();
                return info;
            }

            [[nodiscard]] Dimensions ValidateRegion(
                const int level, const int x, const int y, const int z,
                const int width, const int height, const int depth,
                const void* const data) const
            {
                if (level < 0 || level >= levelCount_)
                    throw std::out_of_range("RLGL: Texture3D mip level is invalid");
                const Dimensions dimensions{
                    MipSize(width_, level),
                    MipSize(height_, level),
                    MipSize(depth_, level)};
                if (x < 0 || y < 0 || z < 0 ||
                    width <= 0 || height <= 0 || depth <= 0 ||
                    x > dimensions.width - width ||
                    y > dimensions.height - height ||
                    z > dimensions.depth - depth || data == nullptr)
                {
                    throw std::out_of_range("RLGL: Texture3D transfer box is invalid");
                }
                return dimensions;
            }

            [[nodiscard]] std::vector<std::uint8_t> PrepareRecoveryShadow(
                const int level, const Dimensions dimensions,
                const int x, const int y, const int z,
                const int width, const int height, const int depth,
                const std::uint8_t* const data) const
            {
                std::vector<std::uint8_t> result =
                    recoveryLevels_[static_cast<std::size_t>(level)];
                if (result.empty())
                    result.assign(
                        VolumeByteCount(
                            dimensions.width, dimensions.height, dimensions.depth),
                        0u);

                const std::size_t destinationRowBytes =
                    static_cast<std::size_t>(dimensions.width) * 4u;
                const std::size_t destinationSliceBytes =
                    destinationRowBytes * dimensions.height;
                const std::size_t sourceRowBytes =
                    static_cast<std::size_t>(width) * 4u;
                const std::size_t sourceSliceBytes = sourceRowBytes * height;
                for (int slice = 0; slice < depth; ++slice)
                {
                    for (int row = 0; row < height; ++row)
                    {
                        std::memcpy(
                            result.data() +
                                static_cast<std::size_t>(z + slice) *
                                    destinationSliceBytes +
                                static_cast<std::size_t>(y + row) *
                                    destinationRowBytes +
                                static_cast<std::size_t>(x) * 4u,
                            data + static_cast<std::size_t>(slice) * sourceSliceBytes +
                                static_cast<std::size_t>(row) * sourceRowBytes,
                            sourceRowBytes);
                    }
                }
                return result;
            }

            unsigned int id_ = 0;
            int width_ = 0;
            int height_ = 0;
            int depth_ = 0;
            int levelCount_ = 1;
            int surfaceFormat_ = 0;
            bool recoveryRegistered_ = false;
            std::vector<bool> definedLevels_;
            std::vector<std::vector<std::uint8_t>> recoveryLevels_;
            std::shared_ptr<RlglResourceLifetime> lifetime_;
        };
    }

    std::unique_ptr<ITexture3DRenderer> CreateTexture3DRenderer(
        const int width, const int height, const int depth,
        const bool mipMap, const int surfaceFormat,
        const std::shared_ptr<RlglResourceLifetime>& lifetime)
    {
        return std::make_unique<RlglTexture3DRenderer>(
            width, height, depth, mipMap, surfaceFormat, lifetime);
    }

    Texture3DResourceSnapshot GetTexture3DResourceSnapshotForTesting(
        const ITexture3DRenderer& resource)
    {
        const auto* const texture =
            dynamic_cast<const RlglTexture3DRenderer*>(&resource);
        if (texture == nullptr)
            throw std::invalid_argument("RLGL: volume texture belongs to another renderer");
        return texture->Snapshot();
    }
}
