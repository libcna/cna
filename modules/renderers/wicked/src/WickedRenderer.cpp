// SPDX-License-Identifier: MS-PL
// plan_wicked.md: CNA's WICKED graphics renderer on Wicked Engine's wi::graphics::GraphicsDevice.

#include "CNA/Internal/Renderers/Wicked/WickedRenderer.hpp"
#include "WickedShaderSources.hpp"

#include "wiGraphicsDevice_Vulkan.h"
#include "wiShaderCompiler.h"

// VK_USE_PLATFORM_XLIB_KHR makes Wicked's bundled Vulkan header include Xlib. Its object-like
// macros otherwise rewrite CNA enum members such as SetDataOptions::None in this translation unit.
#ifdef None
#   undef None
#endif
#ifdef Status
#   undef Status
#endif
#ifdef Success
#   undef Success
#endif
#ifdef Bool
#   undef Bool
#endif
#ifdef Always
#   undef Always
#endif
#ifdef Complex
#   undef Complex
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace CNA::Internal::Renderers::Wicked
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::PrimitiveType;

        constexpr const char* kShaderFileName = "cna_wicked.hlsl";

        // XNA Blend ordinals (Blend.hpp): One, Zero, SourceColor, InverseSourceColor, SourceAlpha,
        // InverseSourceAlpha, DestinationColor, InverseDestinationColor, DestinationAlpha,
        // InverseDestinationAlpha, BlendFactor, InverseBlendFactor, SourceAlphaSaturation.
        wig::Blend ToWickedBlend(int xnaBlend)
        {
            switch (xnaBlend)
            {
                case 0:  return wig::Blend::ONE;
                case 1:  return wig::Blend::ZERO;
                case 2:  return wig::Blend::SRC_COLOR;
                case 3:  return wig::Blend::INV_SRC_COLOR;
                case 4:  return wig::Blend::SRC_ALPHA;
                case 5:  return wig::Blend::INV_SRC_ALPHA;
                case 6:  return wig::Blend::DEST_COLOR;
                case 7:  return wig::Blend::INV_DEST_COLOR;
                case 8:  return wig::Blend::DEST_ALPHA;
                case 9:  return wig::Blend::INV_DEST_ALPHA;
                case 10: return wig::Blend::BLEND_FACTOR;
                case 11: return wig::Blend::INV_BLEND_FACTOR;
                case 12: return wig::Blend::SRC_ALPHA_SAT;
                default: return wig::Blend::ONE;
            }
        }

        // XNA BlendFunction ordinals: Add, Subtract, ReverseSubtract, Max, Min.
        wig::BlendOp ToWickedBlendOp(int xnaFunction)
        {
            switch (xnaFunction)
            {
                case 0:  return wig::BlendOp::ADD;
                case 1:  return wig::BlendOp::SUBTRACT;
                case 2:  return wig::BlendOp::REV_SUBTRACT;
                case 3:  return wig::BlendOp::MAX;
                case 4:  return wig::BlendOp::MIN;
                default: return wig::BlendOp::ADD;
            }
        }

        // XNA CompareFunction ordinals: Always, Never, Less, LessEqual, Equal, GreaterEqual,
        // Greater, NotEqual.
        wig::ComparisonFunc ToWickedCompare(int xnaCompare)
        {
            switch (xnaCompare)
            {
                case 0:  return wig::ComparisonFunc::ALWAYS;
                case 1:  return wig::ComparisonFunc::NEVER;
                case 2:  return wig::ComparisonFunc::LESS;
                case 3:  return wig::ComparisonFunc::LESS_EQUAL;
                case 4:  return wig::ComparisonFunc::EQUAL;
                case 5:  return wig::ComparisonFunc::GREATER_EQUAL;
                case 6:  return wig::ComparisonFunc::GREATER;
                case 7:  return wig::ComparisonFunc::NOT_EQUAL;
                default: return wig::ComparisonFunc::ALWAYS;
            }
        }

        // XNA StencilOperation ordinals: Keep, Zero, Replace, Increment, Decrement,
        // IncrementSaturation, DecrementSaturation, Invert.
        wig::StencilOp ToWickedStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
                case 0:  return wig::StencilOp::KEEP;
                case 1:  return wig::StencilOp::ZERO;
                case 2:  return wig::StencilOp::REPLACE;
                case 3:  return wig::StencilOp::INCR;
                case 4:  return wig::StencilOp::DECR;
                case 5:  return wig::StencilOp::INCR_SAT;
                case 6:  return wig::StencilOp::DECR_SAT;
                case 7:  return wig::StencilOp::INVERT;
                default: return wig::StencilOp::KEEP;
            }
        }

        // XNA CullMode ordinals: None, CullClockwiseFace, CullCounterClockwiseFace. XNA's default
        // winding is clockwise-front, which is what front_counter_clockwise = false selects below,
        // so CullClockwiseFace culls the front face and CullCounterClockwiseFace culls the back.
        wig::CullMode ToWickedCull(int xnaCull)
        {
            switch (xnaCull)
            {
                case 1:  return wig::CullMode::FRONT;
                case 2:  return wig::CullMode::BACK;
                default: return wig::CullMode::NONE;
            }
        }

        // XNA FillMode ordinals: Solid, WireFrame.
        wig::FillMode ToWickedFill(int xnaFill)
        {
            return xnaFill == 1 ? wig::FillMode::WIREFRAME : wig::FillMode::SOLID;
        }

        // XNA TextureAddressMode ordinals: Wrap, Clamp, Mirror.
        wig::TextureAddressMode ToWickedAddress(int xnaAddress)
        {
            switch (xnaAddress)
            {
                case 0:  return wig::TextureAddressMode::WRAP;
                case 2:  return wig::TextureAddressMode::MIRROR;
                default: return wig::TextureAddressMode::CLAMP;
            }
        }

        // XNA TextureFilter ordinals: Linear, Point, Anisotropic, LinearMipPoint, PointMipLinear,
        // MinLinearMagPointMipLinear, MinLinearMagPointMipPoint, MinPointMagLinearMipLinear,
        // MinPointMagLinearMipPoint.
        wig::Filter ToWickedFilter(int xnaFilter)
        {
            switch (xnaFilter)
            {
                case 0:  return wig::Filter::MIN_MAG_MIP_LINEAR;
                case 1:  return wig::Filter::MIN_MAG_MIP_POINT;
                case 2:  return wig::Filter::ANISOTROPIC;
                case 3:  return wig::Filter::MIN_MAG_LINEAR_MIP_POINT;
                case 4:  return wig::Filter::MIN_MAG_POINT_MIP_LINEAR;
                case 5:  return wig::Filter::MIN_LINEAR_MAG_POINT_MIP_LINEAR;
                case 6:  return wig::Filter::MIN_LINEAR_MAG_MIP_POINT;
                case 7:  return wig::Filter::MIN_POINT_MAG_MIP_LINEAR;
                case 8:  return wig::Filter::MIN_POINT_MAG_LINEAR_MIP_POINT;
                default: return wig::Filter::MIN_MAG_MIP_LINEAR;
            }
        }

        wig::ColorWrite ToWickedColorWrite(int xnaColorWriteChannels)
        {
            wig::ColorWrite mask = wig::ColorWrite::DISABLE;
            if (ColorWriteHasRed(xnaColorWriteChannels))   mask |= wig::ColorWrite::ENABLE_RED;
            if (ColorWriteHasGreen(xnaColorWriteChannels)) mask |= wig::ColorWrite::ENABLE_GREEN;
            if (ColorWriteHasBlue(xnaColorWriteChannels))  mask |= wig::ColorWrite::ENABLE_BLUE;
            if (ColorWriteHasAlpha(xnaColorWriteChannels)) mask |= wig::ColorWrite::ENABLE_ALPHA;
            return mask;
        }

        wig::PrimitiveTopology ToWickedTopology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return wig::PrimitiveTopology::TRIANGLELIST;
                case PrimitiveType::TriangleStrip: return wig::PrimitiveTopology::TRIANGLESTRIP;
                case PrimitiveType::LineList:      return wig::PrimitiveTopology::LINELIST;
                case PrimitiveType::LineStrip:     return wig::PrimitiveTopology::LINESTRIP;
                case PrimitiveType::PointListEXT:  return wig::PrimitiveTopology::POINTLIST;
                default:                           return wig::PrimitiveTopology::TRIANGLELIST;
            }
        }

        int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
                default:                           return primitiveCount * 3;
            }
        }

        /// Pbr48VS = 0, Pbr68VS = 1, PbrSkinned68VS = 2 -- the order CreateBuiltinShaders compiles.
        std::size_t PbrShaderIndex(const WickedPipelineKey& key)
        {
            if (key.variant == static_cast<std::uint32_t>(WickedShaderVariant::Basic48))
                return 0;
            return key.skinned != 0 ? 2 : 1;
        }

        WickedShaderVariant VariantForStride(std::size_t strideInBytes)
        {
            switch (strideInBytes)
            {
                case 16: return WickedShaderVariant::Basic16;
                case 20: return WickedShaderVariant::Basic20;
                case 24: return WickedShaderVariant::Basic24;
                case 32: return WickedShaderVariant::Basic32;
                case 48: return WickedShaderVariant::Basic48;
                case 52: return WickedShaderVariant::Basic52;
                case 56: return WickedShaderVariant::Basic56;
                case 68: return WickedShaderVariant::Basic68;
                default:
                    throw std::runtime_error(
                        "Wicked renderer: no built-in vertex layout for stride " +
                        std::to_string(strideInBytes) +
                        " bytes. Supported strides are 16, 20, 24, 32, 48, 52, 56 and 68.");
            }
        }

        /// Writes the four COLUMNS of an XNA (row-vector) matrix, which is what the built-in
        /// shaders dot the position against -- see WickedShaderSources.hpp.
        void WriteMatrixColumns(const Matrix& m, float* dest)
        {
            dest[0]  = m.M11; dest[1]  = m.M21; dest[2]  = m.M31; dest[3]  = m.M41;
            dest[4]  = m.M12; dest[5]  = m.M22; dest[6]  = m.M32; dest[7]  = m.M42;
            dest[8]  = m.M13; dest[9]  = m.M23; dest[10] = m.M33; dest[11] = m.M43;
            dest[12] = m.M14; dest[13] = m.M24; dest[14] = m.M34; dest[15] = m.M44;
        }

        Matrix OrthographicOffCenter(float left, float right, float bottom, float top,
                                     float zNear, float zFar)
        {
            Matrix m = Matrix::getIdentityProperty();
            m.M11 = 2.0f / (right - left);
            m.M22 = 2.0f / (top - bottom);
            m.M33 = 1.0f / (zNear - zFar);
            m.M41 = (left + right) / (left - right);
            m.M42 = (top + bottom) / (bottom - top);
            m.M43 = zNear / (zNear - zFar);
            m.M44 = 1.0f;
            return m;
        }

        wig::Format DepthFormatFor(int xnaDepthFormat)
        {
            switch (xnaDepthFormat)
            {
                case 1:  return wig::Format::D16_UNORM;
                case 2:  return wig::Format::D32_FLOAT;
                case 3:  return wig::Format::D24_UNORM_S8_UINT;
                default: return wig::Format::UNKNOWN;
            }
        }

        /**
         * Uploads a tightly packed RGBA8 box into one subresource region of @p destination.
         *
         * Shared by Texture2D, TextureCube and Texture3D so the staging/barrier/copy sequence
         * exists once: three near-identical copies of it would be three places for the mip, slice
         * or row-pitch arithmetic to drift apart.
         *
         * The staging texture is created with Usage::UPLOAD and the caller's tightly packed
         * SubresourceData; Wicked re-packs it row by row into the device's own
         * optimalBufferCopyRowPitchAlignment, so no alignment handling is needed here.
         */
        bool UploadTextureRegion(WickedRenderer& owner, const wig::Texture& destination,
                                 int level, int slice,
                                 int x, int y, int z, int w, int h, int depth,
                                 const void* data)
        {
            wig::GraphicsDevice* device = owner.GetDeviceEXT();
            const bool volume = destination.desc.type == wig::TextureDesc::Type::TEXTURE_3D;

            wig::TextureDesc stagingDesc;
            stagingDesc.type = destination.desc.type;
            stagingDesc.format = destination.desc.format;
            stagingDesc.width = static_cast<std::uint32_t>(w);
            stagingDesc.height = static_cast<std::uint32_t>(h);
            stagingDesc.depth = volume ? static_cast<std::uint32_t>(depth) : 1u;
            stagingDesc.mip_levels = 1;
            stagingDesc.array_size = 1;
            stagingDesc.sample_count = 1;
            stagingDesc.usage = wig::Usage::UPLOAD;
            stagingDesc.bind_flags = wig::BindFlag::NONE;
            // Deliberately NOT inherited from the destination: a one-slice staging texture is
            // never a cube map, whatever the destination is.
            stagingDesc.misc_flags = wig::ResourceMiscFlag::NONE;
            stagingDesc.layout = wig::ResourceState::COPY_SRC;

            // WICKED-79: the staging texture is created UNPOPULATED and written through its own
            // mapped subresource layout. Passing the tightly packed box as CreateTexture initial
            // data loses every row whose byte width is not already a multiple of the device's
            // optimalBufferCopyRowPitchAlignment: the initial-data repack stores rows tightly
            // while the later CopyTexture consumes the ALIGNED mapped pitches, so a narrow upload
            // smears across the destination (rows land every alignment/rowBytes-th row and the
            // rest reads back zero). Writing at staging.mapped_subresources[0]'s own pitches
            // stores the bytes exactly where CopyTexture reads them, at every width.
            wig::Texture staging;
            if (!device->CreateTexture(&stagingDesc, nullptr, &staging))
                return false;
            if (staging.mapped_data == nullptr || staging.mapped_subresource_count == 0)
                return false;
            {
                const wig::SubresourceData& layout = staging.mapped_subresources[0];
                const auto* src = static_cast<const std::uint8_t*>(data);
                auto* mappedBase = static_cast<std::uint8_t*>(staging.mapped_data);
                const int sliceCount = volume ? depth : 1;
                for (int slice3D = 0; slice3D < sliceCount; ++slice3D)
                {
                    for (int row = 0; row < h; ++row)
                    {
                        std::memcpy(mappedBase +
                                        static_cast<std::size_t>(slice3D) * layout.slice_pitch +
                                        static_cast<std::size_t>(row) * layout.row_pitch,
                                    src + (static_cast<std::size_t>(slice3D) * h + row) * w * 4,
                                    static_cast<std::size_t>(w) * 4);
                    }
                }
            }

            // A copy cannot be recorded inside a render pass, so any open pass is closed first;
            // the next draw re-opens it with a LOAD operation and keeps what was already rendered.
            owner.EndRenderPassEXT();
            wig::CommandList cmd = owner.GetCommandListEXT();

            const wig::GPUBarrier toCopyDst = wig::GPUBarrier::Image(
                &destination, destination.desc.layout, wig::ResourceState::COPY_DST);
            device->Barrier(&toCopyDst, 1, cmd);

            device->CopyTexture(&destination,
                                static_cast<std::uint32_t>(x),
                                static_cast<std::uint32_t>(y),
                                static_cast<std::uint32_t>(volume ? z : 0),
                                static_cast<std::uint32_t>(level),
                                static_cast<std::uint32_t>(slice),
                                &staging, 0, 0, cmd);

            const wig::GPUBarrier toOriginalLayout = wig::GPUBarrier::Image(
                &destination, wig::ResourceState::COPY_DST, destination.desc.layout);
            device->Barrier(&toOriginalLayout, 1, cmd);

            // WICKED-79: each staged upload is submitted before this helper returns, exactly as
            // the readback below always has been. Two staged texture copies recorded on ONE
            // command list interfere -- measured on a raw device with no CNA in the process: the
            // first face of a cube reads back with another upload's rows spliced in, while the
            // same sequence with a submit between the copies is byte-exact at every width. The
            // flush also keeps the staging texture's deferred destruction trivially ordered
            // behind its copy (plan_wicked.md WICKED-31).
            owner.FlushAndWaitEXT();
            return true;
        }

        /**
         * Reads one subresource region back into @p data as a tightly packed RGBA8 box.
         *
         * The counterpart of UploadTextureRegion above, and shared for the same reason. The
         * readback texture is sized to the requested region and the source box is selected with a
         * Box, so the region lands at its origin and only the requested bytes cross the bus.
         */
        bool ReadbackTextureRegion(WickedRenderer& owner, const wig::Texture& source,
                                   wig::ResourceState sourceLayout,
                                   int level, int slice,
                                   int x, int y, int z, int w, int h, int depth,
                                   void* data)
        {
            wig::GraphicsDevice* device = owner.GetDeviceEXT();
            const bool volume = source.desc.type == wig::TextureDesc::Type::TEXTURE_3D;

            wig::TextureDesc readbackDesc;
            readbackDesc.type = source.desc.type;
            readbackDesc.format = source.desc.format;
            readbackDesc.width = static_cast<std::uint32_t>(w);
            readbackDesc.height = static_cast<std::uint32_t>(h);
            readbackDesc.depth = volume ? static_cast<std::uint32_t>(depth) : 1u;
            readbackDesc.mip_levels = 1;
            readbackDesc.array_size = 1;
            readbackDesc.sample_count = 1;
            readbackDesc.usage = wig::Usage::READBACK;
            readbackDesc.bind_flags = wig::BindFlag::NONE;
            readbackDesc.misc_flags = wig::ResourceMiscFlag::NONE;
            readbackDesc.layout = wig::ResourceState::COPY_DST;

            wig::Texture readback;
            if (!device->CreateTexture(&readbackDesc, nullptr, &readback))
                return false;

            owner.EndRenderPassEXT();
            wig::CommandList cmd = owner.GetCommandListEXT();

            wig::Box box;
            box.left = static_cast<std::uint32_t>(x);
            box.top = static_cast<std::uint32_t>(y);
            box.front = static_cast<std::uint32_t>(volume ? z : 0);
            box.right = static_cast<std::uint32_t>(x + w);
            box.bottom = static_cast<std::uint32_t>(y + h);
            box.back = static_cast<std::uint32_t>(volume ? z + depth : 1);

            const wig::GPUBarrier toCopySrc = wig::GPUBarrier::Image(
                &source, sourceLayout, wig::ResourceState::COPY_SRC);
            device->Barrier(&toCopySrc, 1, cmd);
            device->CopyTexture(&readback, 0, 0, 0, 0, 0,
                                &source,
                                static_cast<std::uint32_t>(level),
                                static_cast<std::uint32_t>(slice),
                                cmd, &box);
            const wig::GPUBarrier toOriginalLayout = wig::GPUBarrier::Image(
                &source, wig::ResourceState::COPY_SRC, sourceLayout);
            device->Barrier(&toOriginalLayout, 1, cmd);

            owner.FlushAndWaitEXT();

            if (readback.mapped_data == nullptr || readback.mapped_subresource_count == 0)
                return false;

            const auto* mapped = static_cast<const std::uint8_t*>(readback.mapped_data);
            const wig::SubresourceData& layout = readback.mapped_subresources[0];
            auto* dest = static_cast<std::uint8_t*>(data);
            const int sliceCount = volume ? depth : 1;
            for (int slice3D = 0; slice3D < sliceCount; ++slice3D)
            {
                for (int row = 0; row < h; ++row)
                {
                    std::memcpy(dest + (static_cast<std::size_t>(slice3D) * h + row) * w * 4,
                                mapped + static_cast<std::size_t>(slice3D) * layout.slice_pitch +
                                    static_cast<std::size_t>(row) * layout.row_pitch,
                                static_cast<std::size_t>(w) * 4);
                }
            }
            return true;
        }
    }

    // ------------------------------------------------------------------------------------------
    // WickedPipelineKey
    // ------------------------------------------------------------------------------------------

    bool WickedPipelineKey::operator==(const WickedPipelineKey& other) const noexcept
    {
        return std::memcmp(this, &other, sizeof(WickedPipelineKey)) == 0;
    }

    std::size_t WickedPipelineKeyHash::operator()(const WickedPipelineKey& key) const noexcept
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&key);
        std::size_t hash = 1469598103934665603ull;
        for (std::size_t i = 0; i < sizeof(WickedPipelineKey); ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // ------------------------------------------------------------------------------------------
    // WickedTextureRenderer
    // ------------------------------------------------------------------------------------------

    WickedTextureRenderer::WickedTextureRenderer(WickedRenderer* owner, wig::Texture texture)
        : owner_(owner)
        , texture_(std::move(texture))
    {
    }

    WickedTextureRenderer::~WickedTextureRenderer() = default;

    int WickedTextureRenderer::GetWidth() const
    {
        return static_cast<int>(texture_.desc.width);
    }

    int WickedTextureRenderer::GetHeight() const
    {
        return static_cast<int>(texture_.desc.height);
    }

    void WickedTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (rgba == nullptr || owner_ == nullptr || !texture_.IsValid())
            return;

        const int width = GetWidth();
        const int height = GetHeight();
        const int rowBytes = width * 4;

        std::vector<std::uint8_t> packed;
        const std::uint8_t* source = rgba;
        if (stride != rowBytes)
        {
            packed.resize(static_cast<std::size_t>(rowBytes) * height);
            for (int y = 0; y < height; ++y)
            {
                std::memcpy(packed.data() + static_cast<std::size_t>(y) * rowBytes,
                            rgba + static_cast<std::size_t>(y) * stride,
                            static_cast<std::size_t>(rowBytes));
            }
            source = packed.data();
        }

        UpdatePixelsLevel(0, source, width, height);
    }

    void WickedTextureRenderer::UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                                                 int levelW, int levelH)
    {
        if (rgba == nullptr || owner_ == nullptr || !texture_.IsValid())
            return;
        if (level < 0 || static_cast<std::uint32_t>(level) >= texture_.desc.mip_levels)
            return;
        if (levelW <= 0 || levelH <= 0)
            return;

        (void)UploadTextureRegion(*owner_, texture_, level, 0, 0, 0, 0, levelW, levelH, 1, rgba);
    }

    bool WickedTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                       void* data, int dataLength) const
    {
        if (owner_ == nullptr || data == nullptr || !texture_.IsValid())
            return false;
        if (w <= 0 || h <= 0 || x < 0 || y < 0)
            return false;
        if (dataLength < w * h * 4)
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= texture_.desc.mip_levels)
            return false;

        const std::uint32_t levelWidth = std::max(1u, texture_.desc.width >> level);
        const std::uint32_t levelHeight = std::max(1u, texture_.desc.height >> level);
        if (static_cast<std::uint32_t>(x + w) > levelWidth ||
            static_cast<std::uint32_t>(y + h) > levelHeight)
        {
            return false;
        }

        return ReadbackTextureRegion(*owner_, texture_, texture_.desc.layout,
                                     level, 0, x, y, 0, w, h, 1, data);
    }

    // ------------------------------------------------------------------------------------------
    // WickedTextureCubeRenderer
    // ------------------------------------------------------------------------------------------

    WickedTextureCubeRenderer::WickedTextureCubeRenderer(WickedRenderer* owner, int size,
                                                       bool mipMap)
        : owner_(owner)
        , size_(std::max(1, size))
    {
        wig::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(size_);
        desc.height = static_cast<std::uint32_t>(size_);
        desc.format = wig::Format::R8G8B8A8_UNORM;
        desc.array_size = 6;
        desc.misc_flags = wig::ResourceMiscFlag::TEXTURECUBE;
        desc.bind_flags = wig::BindFlag::SHADER_RESOURCE;
        desc.layout = wig::ResourceState::SHADER_RESOURCE;
        desc.mip_levels = mipMap
            ? wig::GetMipCount(desc.width, desc.height)
            : 1u;

        if (!owner_->GetDeviceEXT()->CreateTexture(&desc, nullptr, &texture_))
            throw std::runtime_error("Wicked renderer: failed to create a cube map.");
        mipLevels_ = texture_.desc.mip_levels;
    }

    WickedTextureCubeRenderer::~WickedTextureCubeRenderer() = default;

    bool WickedTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength)
    {
        if (owner_ == nullptr || data == nullptr || !texture_.IsValid())
            return false;
        if (face < 0 || face > 5)
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || x < 0 || y < 0)
            return false;
        if (dataLength < w * h * 4)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x + w > levelSize || y + h > levelSize)
            return false;

        return UploadTextureRegion(*owner_, texture_, level, face, x, y, 0, w, h, 1, data);
    }

    bool WickedTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (owner_ == nullptr || data == nullptr || !texture_.IsValid())
            return false;
        if (face < 0 || face > 5)
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || x < 0 || y < 0)
            return false;
        if (dataLength < w * h * 4)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x + w > levelSize || y + h > levelSize)
            return false;

        return ReadbackTextureRegion(*owner_, texture_, texture_.desc.layout,
                                     level, face, x, y, 0, w, h, 1, data);
    }

    // ------------------------------------------------------------------------------------------
    // WickedTexture3DRenderer
    // ------------------------------------------------------------------------------------------

    WickedTexture3DRenderer::WickedTexture3DRenderer(WickedRenderer* owner, int width,
                                                   int height, int depth, bool mipMap)
        : owner_(owner)
        , width_(std::max(1, width))
        , height_(std::max(1, height))
        , depth_(std::max(1, depth))
    {
        wig::TextureDesc desc;
        desc.type = wig::TextureDesc::Type::TEXTURE_3D;
        desc.width = static_cast<std::uint32_t>(width_);
        desc.height = static_cast<std::uint32_t>(height_);
        desc.depth = static_cast<std::uint32_t>(depth_);
        desc.format = wig::Format::R8G8B8A8_UNORM;
        desc.bind_flags = wig::BindFlag::SHADER_RESOURCE;
        desc.layout = wig::ResourceState::SHADER_RESOURCE;
        desc.mip_levels = mipMap
            ? wig::GetMipCount(desc.width, desc.height, desc.depth)
            : 1u;

        if (!owner_->GetDeviceEXT()->CreateTexture(&desc, nullptr, &texture_))
            throw std::runtime_error("Wicked renderer: failed to create a volume texture.");
        mipLevels_ = texture_.desc.mip_levels;
    }

    WickedTexture3DRenderer::~WickedTexture3DRenderer() = default;

    bool WickedTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                         const void* data, int dataLength)
    {
        if (owner_ == nullptr || data == nullptr || !texture_.IsValid())
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0)
            return false;
        if (dataLength < w * h * depth * 4)
            return false;

        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const int levelDepth = std::max(1, depth_ >> level);
        if (x + w > levelWidth || y + h > levelHeight || z + depth > levelDepth)
            return false;

        return UploadTextureRegion(*owner_, texture_, level, 0, x, y, z, w, h, depth, data);
    }

    bool WickedTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                         void* data, int dataLength) const
    {
        if (owner_ == nullptr || data == nullptr || !texture_.IsValid())
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0)
            return false;
        if (dataLength < w * h * depth * 4)
            return false;

        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const int levelDepth = std::max(1, depth_ >> level);
        if (x + w > levelWidth || y + h > levelHeight || z + depth > levelDepth)
            return false;

        return ReadbackTextureRegion(*owner_, texture_, texture_.desc.layout,
                                     level, 0, x, y, z, w, h, depth, data);
    }

    // ------------------------------------------------------------------------------------------
    // WickedRenderTargetRenderer
    // ------------------------------------------------------------------------------------------

    WickedRenderTargetRenderer::WickedRenderTargetRenderer(WickedRenderer* owner,
                                                         int width, int height, int depthFormat,
                                                         bool preserveContents, bool mipMap,
                                                         int multiSampleCount)
        : owner_(owner)
        , width_(std::max(1, width))
        , height_(std::max(1, height))
        , preserveContents_(preserveContents)
    {
        wig::GraphicsDevice* device = owner_->GetDeviceEXT();

        const std::uint32_t sampleCount =
            multiSampleCount > 1 ? static_cast<std::uint32_t>(multiSampleCount) : 1u;
        multiSampleCount_ = sampleCount > 1 ? static_cast<int>(sampleCount) : 0;

        wig::TextureDesc colorDesc;
        colorDesc.width = static_cast<std::uint32_t>(width_);
        colorDesc.height = static_cast<std::uint32_t>(height_);
        colorDesc.format = wig::Format::R8G8B8A8_UNORM;
        colorDesc.bind_flags = wig::BindFlag::RENDER_TARGET | wig::BindFlag::SHADER_RESOURCE;
        colorDesc.layout = wig::ResourceState::SHADER_RESOURCE;
        colorDesc.sample_count = sampleCount;
        colorDesc.mip_levels = mipMap && sampleCount == 1
            ? wig::GetMipCount(colorDesc.width, colorDesc.height)
            : 1u;
        if (!device->CreateTexture(&colorDesc, nullptr, &color_))
            throw std::runtime_error("Wicked renderer: failed to create a render-target texture.");

        if (sampleCount > 1)
        {
            // A multisampled image can be neither sampled by the ordinary shaders nor copied by a
            // plain CopyTexture, so an MSAA target keeps a single-sample resolve destination that
            // the render pass writes on end. Without it, MultiSampleCount > 1 would make the
            // target unreadable and unsampleable -- which is what it used to do.
            wig::TextureDesc resolveDesc = colorDesc;
            resolveDesc.sample_count = 1;
            if (!device->CreateTexture(&resolveDesc, nullptr, &resolve_))
            {
                throw std::runtime_error(
                    "Wicked renderer: failed to create a render-target resolve texture.");
            }
        }

        const wig::Format depth = DepthFormatFor(depthFormat);
        if (depth != wig::Format::UNKNOWN)
        {
            wig::TextureDesc depthDesc;
            depthDesc.width = colorDesc.width;
            depthDesc.height = colorDesc.height;
            depthDesc.format = depth;
            depthDesc.bind_flags = wig::BindFlag::DEPTH_STENCIL;
            depthDesc.layout = wig::ResourceState::DEPTHSTENCIL;
            depthDesc.sample_count = sampleCount;
            if (!device->CreateTexture(&depthDesc, nullptr, &depth_))
            {
                throw std::runtime_error(
                    "Wicked renderer: failed to create a render-target depth buffer.");
            }
        }
    }

    WickedRenderTargetRenderer::~WickedRenderTargetRenderer()
    {
        if (owner_ != nullptr)
            owner_->NotifyRenderTargetDestroyedEXT(this);
    }

    void WickedRenderTargetRenderer::BindAsRenderTarget()
    {
        if (owner_ != nullptr)
            owner_->SetRenderTarget2D(this);
    }

    void WickedRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr)
            owner_->SetRenderTarget2D(nullptr);
    }

    bool WickedRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (owner_ == nullptr || data == nullptr || !color_.IsValid())
            return false;
        if (level != 0 || w <= 0 || h <= 0 || x < 0 || y < 0 || dataLength < w * h * 4)
            return false;
        if (x + w > width_ || y + h > height_)
            return false;

        // A multisampled target reads back through its resolve destination, which the render pass
        // wrote when the target was last unbound.
        return ReadbackTextureRegion(*owner_, GetSampleableTextureEXT(),
                                     wig::ResourceState::SHADER_RESOURCE,
                                     0, 0, x, y, 0, w, h, 1, data);
    }
    // ------------------------------------------------------------------------------------------
    // WickedRenderTargetCubeRenderer
    // ------------------------------------------------------------------------------------------

    WickedRenderTargetCubeRenderer::WickedRenderTargetCubeRenderer(WickedRenderer* owner,
                                                                 int size, int depthFormat,
                                                                 bool preserveContents, bool mipMap,
                                                                 int multiSampleCount)
        : owner_(owner)
        , size_(std::max(1, size))
        , preserveContents_(preserveContents)
    {
        wig::GraphicsDevice* device = owner_->GetDeviceEXT();

        const std::uint32_t sampleCount =
            multiSampleCount > 1 ? static_cast<std::uint32_t>(multiSampleCount) : 1u;
        multiSampleCount_ = sampleCount > 1 ? static_cast<int>(sampleCount) : 0;

        wig::TextureDesc colorDesc;
        colorDesc.width = static_cast<std::uint32_t>(size_);
        colorDesc.height = static_cast<std::uint32_t>(size_);
        colorDesc.format = wig::Format::R8G8B8A8_UNORM;
        colorDesc.array_size = 6;
        colorDesc.misc_flags = wig::ResourceMiscFlag::TEXTURECUBE;
        colorDesc.bind_flags = wig::BindFlag::RENDER_TARGET | wig::BindFlag::SHADER_RESOURCE;
        colorDesc.layout = wig::ResourceState::SHADER_RESOURCE;
        colorDesc.sample_count = sampleCount;
        colorDesc.mip_levels = mipMap && sampleCount == 1
            ? wig::GetMipCount(colorDesc.width, colorDesc.height)
            : 1u;
        if (!device->CreateTexture(&colorDesc, nullptr, &color_))
            throw std::runtime_error("Wicked renderer: failed to create a cube render target.");

        // One render-target view per face, created once. The resource's default shader-resource
        // view stays the whole cube, so the same texture is sampled as a cube map without a
        // second resource or a copy.
        for (int face = 0; face < 6; ++face)
        {
            faceSubresources_[face] = device->CreateSubresource(
                &color_, wig::SubresourceType::RTV, static_cast<std::uint32_t>(face), 1, 0, 1);
            if (faceSubresources_[face] < 0)
            {
                throw std::runtime_error(
                    "Wicked renderer: failed to create a cube render-target face view.");
            }
        }

        const wig::Format depth = DepthFormatFor(depthFormat);
        if (depth != wig::Format::UNKNOWN)
        {
            // A single face-sized depth buffer is enough: exactly one face is bound at a time, and
            // depth is never read back across faces.
            wig::TextureDesc depthDesc;
            depthDesc.width = colorDesc.width;
            depthDesc.height = colorDesc.height;
            depthDesc.format = depth;
            depthDesc.bind_flags = wig::BindFlag::DEPTH_STENCIL;
            depthDesc.layout = wig::ResourceState::DEPTHSTENCIL;
            depthDesc.sample_count = sampleCount;
            if (!device->CreateTexture(&depthDesc, nullptr, &depth_))
            {
                throw std::runtime_error(
                    "Wicked renderer: failed to create a cube render-target depth buffer.");
            }
        }
    }

    WickedRenderTargetCubeRenderer::~WickedRenderTargetCubeRenderer()
    {
        if (owner_ != nullptr)
            owner_->NotifyRenderTargetCubeDestroyedEXT(this);
    }

    int WickedRenderTargetCubeRenderer::RenderTargetSubresourceEXT(int face) const
    {
        if (face < 0 || face > 5)
            return -1;
        return faceSubresources_[static_cast<std::size_t>(face)];
    }

    void WickedRenderTargetCubeRenderer::MarkFaceRenderedEXT(int face)
    {
        if (face >= 0 && face <= 5)
            faceHasContent_[static_cast<std::size_t>(face)] = true;
    }

    bool WickedRenderTargetCubeRenderer::HasFaceContentEXT(int face) const
    {
        return face >= 0 && face <= 5 && faceHasContent_[static_cast<std::size_t>(face)];
    }

    void WickedRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        if (owner_ != nullptr)
            owner_->SetRenderTargetCubeFace(this, face);
    }

    void WickedRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr)
            owner_->SetRenderTargetCubeFace(nullptr, 0);
    }

    bool WickedRenderTargetCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                                const void* data, int dataLength)
    {
        if (owner_ == nullptr || data == nullptr || !color_.IsValid())
            return false;
        if (face < 0 || face > 5)
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= color_.desc.mip_levels)
            return false;
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || dataLength < w * h * 4)
            return false;
        // A multisampled face holds per-sample content that a plain copy cannot write.
        if (multiSampleCount_ > 1)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x + w > levelSize || y + h > levelSize)
            return false;

        const bool stored =
            UploadTextureRegion(*owner_, color_, level, face, x, y, 0, w, h, 1, data);
        if (stored)
            MarkFaceRenderedEXT(face);
        return stored;
    }

    bool WickedRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        if (owner_ == nullptr || data == nullptr || !color_.IsValid())
            return false;
        if (face < 0 || face > 5)
            return false;
        if (level < 0 || static_cast<std::uint32_t>(level) >= color_.desc.mip_levels)
            return false;
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || dataLength < w * h * 4)
            return false;
        // Same refusal as WickedRenderTargetRenderer::GetData -- see plan_wicked.md WICKED-52.
        if (multiSampleCount_ > 1)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x + w > levelSize || y + h > levelSize)
            return false;

        return ReadbackTextureRegion(*owner_, color_, wig::ResourceState::SHADER_RESOURCE,
                                     level, face, x, y, 0, w, h, 1, data);
    }

    // ------------------------------------------------------------------------------------------
    // WickedVertexBufferRenderer
    // ------------------------------------------------------------------------------------------

    namespace
    {
        // The largest stride any built-in variant uses, so a buffer created before its declaration
        // is known can still hold the requested vertex count.
        constexpr std::size_t kMaxSupportedStride = 68;

        /// CNA's established per-instance stream layout: one column-major Matrix per instance,
        /// the same 64-byte record the Vulkan renderer's instanced pipeline consumes.
        constexpr std::uint32_t kInstanceMatrixStride = 64;
    }

    WickedVertexBufferRenderer::WickedVertexBufferRenderer(WickedRenderer* owner,
                                                         int vertexCapacity)
        : owner_(owner)
        , capacity_(std::max(1, vertexCapacity))
    {
        // Storage is allocated lazily, on the first upload that reveals the real vertex stride.
        // Sizing it eagerly would mean assuming the widest supported stride for every buffer --
        // over four times the memory a stride-16 buffer needs, before the region multiplier.
    }

    WickedVertexBufferRenderer::~WickedVertexBufferRenderer() = default;

    void WickedVertexBufferRenderer::EnsureStorage(std::size_t strideInBytes)
    {
        const std::size_t regionSize = static_cast<std::size_t>(capacity_) * strideInBytes;
        if (buffer_.IsValid() && regionSize == regionSizeBytes_)
            return;

        wig::GPUBufferDesc desc;
        desc.size = static_cast<std::uint64_t>(regionSize) * kWickedBufferRegions;
        desc.usage = wig::Usage::UPLOAD;
        desc.bind_flags = wig::BindFlag::VERTEX_BUFFER;

        wig::GPUBuffer replacement;
        if (!owner_->GetDeviceEXT()->CreateBuffer(&desc, nullptr, &replacement))
            throw std::runtime_error("Wicked renderer: failed to create a vertex buffer.");

        buffer_ = std::move(replacement);
        regionSizeBytes_ = regionSize;
        regionIndex_ = 0;
    }

    std::uint64_t WickedVertexBufferRenderer::GetByteOffsetEXT() const
    {
        return static_cast<std::uint64_t>(regionIndex_) * regionSizeBytes_;
    }

    void WickedVertexBufferRenderer::SetData(const void* data, int vertexCount,
                                            std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions::None);
    }

    void WickedVertexBufferRenderer::SetDataWithOptions(const void* data, int vertexCount,
                                                       std::size_t strideInBytes,
                                                       SetDataOptions options)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }
        if (vertexCount > capacity_)
        {
            throw std::runtime_error(
                "Wicked renderer: " + std::to_string(vertexCount) +
                " vertices exceed this buffer's capacity of " + std::to_string(capacity_) + ".");
        }

        EnsureStorage(strideInBytes);
        if (buffer_.mapped_data == nullptr)
            throw std::runtime_error("Wicked renderer: vertex buffer is not CPU-writable.");

        // NoOverwrite is the caller's explicit promise that it is not touching data a queued draw
        // still reads, so it keeps the current region. Everything else moves to the next one --
        // buffer orphaning, which is what Discard asks for and a valid way to satisfy None.
        if (options != SetDataOptions::NoOverwrite)
            regionIndex_ = (regionIndex_ + 1) % kWickedBufferRegions;

        auto* destination = static_cast<std::uint8_t*>(buffer_.mapped_data) + GetByteOffsetEXT();
        std::memcpy(destination, data, static_cast<std::size_t>(vertexCount) * strideInBytes);
        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void WickedVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // The declaration's stride is what selects this renderer's input layout and shader variant,
        // exactly as it does on D3D11/D3D12/Vulkan (plan_wicked.md WICKED-45). VariantForStride()
        // refuses an unlisted WIDTH, but a custom layout that happens to be one of the eight known
        // widths would be read from the wrong bytes -- so the element list is remembered here and
        // checked at draw time by RequireFaithfulDeclarationEXT() (REMED-GFX-DECL-GUARD).
        declaration_.Remember(vertexDeclaration);
        declaredStride_ = static_cast<std::size_t>(vertexDeclaration.getVertexStrideProperty());
        if (stride_ == 0)
            stride_ = declaredStride_;
    }

    // ------------------------------------------------------------------------------------------
    // WickedIndexBufferRenderer
    // ------------------------------------------------------------------------------------------

    WickedIndexBufferRenderer::WickedIndexBufferRenderer(WickedRenderer* owner,
                                                       int indexCapacity, bool thirtyTwoBit)
        : owner_(owner)
        , capacity_(std::max(1, indexCapacity))
        , thirtyTwoBit_(thirtyTwoBit)
    {
        // Unlike a vertex buffer, an index buffer's element size is known at construction, so its
        // storage can be sized right away.
        regionSizeBytes_ = static_cast<std::size_t>(capacity_) * (thirtyTwoBit_ ? 4u : 2u);

        wig::GPUBufferDesc desc;
        desc.size = static_cast<std::uint64_t>(regionSizeBytes_) * kWickedBufferRegions;
        desc.usage = wig::Usage::UPLOAD;
        desc.bind_flags = wig::BindFlag::INDEX_BUFFER;
        if (!owner_->GetDeviceEXT()->CreateBuffer(&desc, nullptr, &buffer_))
            throw std::runtime_error("Wicked renderer: failed to create an index buffer.");
    }

    WickedIndexBufferRenderer::~WickedIndexBufferRenderer() = default;

    std::uint64_t WickedIndexBufferRenderer::GetByteOffsetEXT() const
    {
        return static_cast<std::uint64_t>(regionIndex_) * regionSizeBytes_;
    }

    void WickedIndexBufferRenderer::Upload(const void* data, int indexCount, std::size_t indexSize,
                                          SetDataOptions options)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }
        if (indexCount > capacity_)
        {
            throw std::runtime_error(
                "Wicked renderer: " + std::to_string(indexCount) +
                " indices exceed this buffer's capacity of " + std::to_string(capacity_) + ".");
        }
        if (buffer_.mapped_data == nullptr)
            throw std::runtime_error("Wicked renderer: index buffer is not CPU-writable.");

        if (options != SetDataOptions::NoOverwrite)
            regionIndex_ = (regionIndex_ + 1) % kWickedBufferRegions;

        auto* destination = static_cast<std::uint8_t*>(buffer_.mapped_data) + GetByteOffsetEXT();
        std::memcpy(destination, data, static_cast<std::size_t>(indexCount) * indexSize);
        indexCount_ = indexCount;
    }

    void WickedIndexBufferRenderer::SetData16(const void* data, int indexCount)
    {
        SetData16WithOptions(data, indexCount, SetDataOptions::None);
    }

    void WickedIndexBufferRenderer::SetData32(const void* data, int indexCount)
    {
        SetData32WithOptions(data, indexCount, SetDataOptions::None);
    }

    void WickedIndexBufferRenderer::SetData16WithOptions(const void* data, int indexCount,
                                                        SetDataOptions options)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error("Wicked renderer: SetData16 on a 32-bit index buffer.");
        Upload(data, indexCount, 2u, options);
    }

    void WickedIndexBufferRenderer::SetData32WithOptions(const void* data, int indexCount,
                                                        SetDataOptions options)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error("Wicked renderer: SetData32 on a 16-bit index buffer.");
        Upload(data, indexCount, 4u, options);
    }

    // ------------------------------------------------------------------------------------------
    // WickedOcclusionQueryRenderer
    // ------------------------------------------------------------------------------------------

    WickedOcclusionQueryRenderer::WickedOcclusionQueryRenderer(WickedRenderer* owner)
        : owner_(owner)
    {
        wig::GraphicsDevice* device = owner_->GetDeviceEXT();

        wig::GPUQueryHeapDesc heapDesc;
        heapDesc.type = wig::GpuQueryType::OCCLUSION;
        heapDesc.query_count = 1;
        if (!device->CreateQueryHeap(&heapDesc, &heap_))
            throw std::runtime_error("Wicked renderer: failed to create an occlusion query heap.");

        wig::GPUBufferDesc readbackDesc;
        readbackDesc.size = sizeof(std::uint64_t);
        readbackDesc.usage = wig::Usage::READBACK;
        if (!device->CreateBuffer(&readbackDesc, nullptr, &readback_))
            throw std::runtime_error("Wicked renderer: failed to create an occlusion readback buffer.");
    }

    WickedOcclusionQueryRenderer::~WickedOcclusionQueryRenderer() = default;

    void WickedOcclusionQueryRenderer::Begin()
    {
        if (owner_ == nullptr || active_)
            return;
        wig::CommandList cmd = owner_->GetCommandListEXT();
        owner_->GetDeviceEXT()->QueryBegin(&heap_, 0, cmd);
        active_ = true;
        resolved_ = false;
    }

    void WickedOcclusionQueryRenderer::End()
    {
        if (owner_ == nullptr || !active_)
            return;
        wig::GraphicsDevice* device = owner_->GetDeviceEXT();
        wig::CommandList cmd = owner_->GetCommandListEXT();
        device->QueryEnd(&heap_, 0, cmd);
        // QueryResolve writes into a buffer and therefore cannot be recorded inside a render pass.
        owner_->EndRenderPassEXT();
        cmd = owner_->GetCommandListEXT();
        device->QueryResolve(&heap_, 0, 1, &readback_, 0, cmd);
        resolvedFrame_ = device->GetFrameCount();
        active_ = false;
        resolved_ = true;
    }

    bool WickedOcclusionQueryRenderer::IsComplete() const
    {
        if (owner_ == nullptr || !resolved_)
            return false;
        // The resolve lands in a READBACK buffer that the CPU may only read once the frame it was
        // recorded in has been submitted and its buffered slot has come around again.
        return owner_->GetDeviceEXT()->GetFrameCount() >
               resolvedFrame_ + wig::GraphicsDevice::GetBufferCount();
    }

    int WickedOcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete() || readback_.mapped_data == nullptr)
            return 0;
        std::uint64_t samples = 0;
        std::memcpy(&samples, readback_.mapped_data, sizeof(samples));
        return static_cast<int>(std::min<std::uint64_t>(
            samples, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    }

    // ------------------------------------------------------------------------------------------
    // WickedSpriteBatchRenderer
    // ------------------------------------------------------------------------------------------

    WickedSpriteBatchRenderer::WickedSpriteBatchRenderer(WickedRenderer* owner)
        : owner_(owner)
    {
    }

    WickedSpriteBatchRenderer::~WickedSpriteBatchRenderer()
    {
        vertices_.clear();
        batchTexture_ = nullptr;
    }

    void WickedSpriteBatchRenderer::Begin()
    {
        vertices_.clear();
        batchTexture_ = nullptr;
        active_ = true;
    }

    void WickedSpriteBatchRenderer::End()
    {
        Flush();
        active_ = false;
    }

    void WickedSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
        hasTransform_ = true;
    }

    void WickedSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        if (textureFilter != filter_)
        {
            Flush();
            filter_ = textureFilter;
        }
    }

    void WickedSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU != addressU_ || addressV != addressV_)
        {
            Flush();
            addressU_ = addressU;
            addressV_ = addressV;
        }
    }

    void WickedSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y),
                                    texture.GetWidth(), texture.GetHeight()};
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        PushQuad(texture, destination, source, Color(255, 255, 255, 255), 0.0f,
                 Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void WickedSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        PushQuad(texture, destinationRectangle, sourceRectangle, color, 0.0f,
                 Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void WickedSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        PushQuad(texture, destinationRectangle, sourceRectangle, color, rotation, origin,
                 effects, layerDepth);
    }

    void WickedSpriteBatchRenderer::PushQuad(const ITextureRenderer& texture,
                                            const Rectangle& destinationRectangle,
                                            const Rectangle& sourceRectangle,
                                            const Color& color,
                                            float rotation,
                                            const Vector2& origin,
                                            SpriteEffects effects,
                                            float layerDepth)
    {
        if (batchTexture_ != nullptr && batchTexture_ != &texture)
            Flush();
        batchTexture_ = &texture;

        const float textureWidth = static_cast<float>(std::max(1, texture.GetWidth()));
        const float textureHeight = static_cast<float>(std::max(1, texture.GetHeight()));

        float u0 = static_cast<float>(sourceRectangle.X) / textureWidth;
        float v0 = static_cast<float>(sourceRectangle.Y) / textureHeight;
        float u1 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / textureWidth;
        float v1 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / textureHeight;

        using Underlying = std::underlying_type_t<SpriteEffects>;
        const auto effectBits = static_cast<Underlying>(effects);
        if ((effectBits & static_cast<Underlying>(SpriteEffects::FlipHorizontally)) != 0)
            std::swap(u0, u1);
        if ((effectBits & static_cast<Underlying>(SpriteEffects::FlipVertically)) != 0)
            std::swap(v0, v1);

        // The origin is expressed in source-texture pixels, so it scales with the destination
        // rectangle exactly as SpriteBatch's own destination-rectangle overload defines it.
        const float scaleX = sourceRectangle.Width != 0
            ? static_cast<float>(destinationRectangle.Width) / static_cast<float>(sourceRectangle.Width)
            : 1.0f;
        const float scaleY = sourceRectangle.Height != 0
            ? static_cast<float>(destinationRectangle.Height) / static_cast<float>(sourceRectangle.Height)
            : 1.0f;
        const float originX = origin.X * scaleX;
        const float originY = origin.Y * scaleY;

        const float left = -originX;
        const float top = -originY;
        const float right = left + static_cast<float>(destinationRectangle.Width);
        const float bottom = top + static_cast<float>(destinationRectangle.Height);

        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        const float anchorX = static_cast<float>(destinationRectangle.X);
        const float anchorY = static_cast<float>(destinationRectangle.Y);

        const auto transform = [&](float lx, float ly, float& outX, float& outY)
        {
            outX = anchorX + lx * cosine - ly * sine;
            outY = anchorY + lx * sine + ly * cosine;
        };

        SpriteVertex corners[4];
        transform(left, top, corners[0].x, corners[0].y);
        transform(right, top, corners[1].x, corners[1].y);
        transform(right, bottom, corners[2].x, corners[2].y);
        transform(left, bottom, corners[3].x, corners[3].y);

        corners[0].u = u0; corners[0].v = v0;
        corners[1].u = u1; corners[1].v = v0;
        corners[2].u = u1; corners[2].v = v1;
        corners[3].u = u0; corners[3].v = v1;

        for (SpriteVertex& vertex : corners)
        {
            vertex.z = layerDepth;
            vertex.r = color.getRProperty();
            vertex.g = color.getGProperty();
            vertex.b = color.getBProperty();
            vertex.a = color.getAProperty();
        }

        // Two triangles, wound so a sprite is front-facing under XNA's clockwise-front convention.
        vertices_.push_back(corners[0]);
        vertices_.push_back(corners[1]);
        vertices_.push_back(corners[2]);
        vertices_.push_back(corners[0]);
        vertices_.push_back(corners[2]);
        vertices_.push_back(corners[3]);
    }

    void WickedSpriteBatchRenderer::Flush()
    {
        if (vertices_.empty() || batchTexture_ == nullptr || owner_ == nullptr)
        {
            vertices_.clear();
            return;
        }

        owner_->DrawSpriteQuadsEXT(vertices_.data(), static_cast<int>(vertices_.size()),
                                   *batchTexture_, hasTransform_ ? &transform_ : nullptr,
                                   filter_, addressU_, addressV_);
        vertices_.clear();
    }

    // ------------------------------------------------------------------------------------------
    // WickedRenderer
    // ------------------------------------------------------------------------------------------

    WickedRenderer::WickedRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface, "Wicked renderer")
        , preferredWidth_(args.virtualWidth)
        , preferredHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , requestedMultiSampleCount_(std::max(1, args.multiSampleCount))
        , swapInterval_(args.swapInterval)
    {
        CreateDevice(args);
        CreateSwapChainResources();
        ResolveVirtualResolution();
        CreateSceneTarget(virtualWidth_, virtualHeight_, requestedMultiSampleCount_);
        CreateBuiltinShaders();

        // A 1x1 opaque white texture stands in for slot 0 whenever a draw samples no texture, so
        // one pixel shader can serve both the textured and untextured routes.
        wig::TextureDesc whiteDesc;
        whiteDesc.width = 1;
        whiteDesc.height = 1;
        whiteDesc.format = wig::Format::R8G8B8A8_UNORM;
        whiteDesc.bind_flags = wig::BindFlag::SHADER_RESOURCE;
        whiteDesc.layout = wig::ResourceState::SHADER_RESOURCE;
        const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
        wig::SubresourceData whiteData;
        whiteData.data_ptr = whitePixel;
        whiteData.row_pitch = 4;
        whiteData.slice_pitch = 4;
        if (!device_->CreateTexture(&whiteDesc, &whiteData, &whiteTexture_))
            throw std::runtime_error("Wicked renderer: failed to create the fallback white texture.");

        // The env-map sampler slot must always resolve to a real cube resource, even on a draw
        // that binds none -- an unbound descriptor is undefined behaviour, not "black".
        wig::TextureDesc whiteCubeDesc = whiteDesc;
        whiteCubeDesc.array_size = 6;
        whiteCubeDesc.misc_flags = wig::ResourceMiscFlag::TEXTURECUBE;
        const wig::SubresourceData whiteCubeData[6] = {
            whiteData, whiteData, whiteData, whiteData, whiteData, whiteData};
        if (!device_->CreateTexture(&whiteCubeDesc, whiteCubeData, &whiteCubeTexture_))
            throw std::runtime_error("Wicked renderer: failed to create the fallback white cube map.");

        // PbrEffect's normal-map slot needs a neutral default, and white is not it: the shader
        // decodes the sample as `rgb * 2 - 1`, so an unbound normal map must read (0.5, 0.5, 1)
        // to decode to the unperturbed surface normal (0, 0, 1).
        const std::uint8_t flatNormalPixel[4] = {128, 128, 255, 255};
        wig::SubresourceData flatNormalData;
        flatNormalData.data_ptr = flatNormalPixel;
        flatNormalData.row_pitch = 4;
        flatNormalData.slice_pitch = 4;
        if (!device_->CreateTexture(&whiteDesc, &flatNormalData, &flatNormalTexture_))
            throw std::runtime_error("Wicked renderer: failed to create the fallback normal map.");

        state_.depthEnable = 1;
        state_.depthWriteEnable = 1;
        state_.depthFunc = 3; // CompareFunction::LessEqual
        state_.cullMode = 0;  // CullMode::None
        state_.blendEnabled = 1;
        state_.colorSrcBlend = 4;  // Blend::SourceAlpha
        state_.alphaSrcBlend = 4;
        state_.colorDstBlend = 5;  // Blend::InverseSourceAlpha
        state_.alphaDstBlend = 5;

        IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
    }

    WickedRenderer::~WickedRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());

        if (device_)
        {
            if (renderPassActive_)
            {
                device_->RenderPassEnd(cmd_);
                renderPassActive_ = false;
            }
            if (frameActive_)
            {
                device_->SubmitCommandLists();
                frameActive_ = false;
            }
            device_->WaitForGPU();
        }

        pipelines_.clear();
        samplers_.clear();

        if (shaderDirectoryOwned_ && !shaderDirectory_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove_all(shaderDirectory_, ignored);
        }
    }

    void WickedRenderer::CreateDevice(const GraphicsRendererCreateArgs& args)
    {
#ifdef WICKED_CNA_PLATFORM
        CNA::Platform::X11NativeWindow x11;
        if (!CNA::Platform::TryGetX11(surface_.GetNativeHandle(), x11))
        {
            throw std::runtime_error(
                "Wicked renderer: Linux requires an X11 native window, got " +
                CNA::Platform::Describe(surface_.GetNativeHandle()));
        }
        nativeWindow_.type = wi::platform::NativeWindow::Type::X11;
        nativeWindow_.display = x11.display;
        nativeWindow_.window_id = x11.window;
        nativeWindow_.fullscreen = args.isFullScreen;
        wickedWindow_ = &nativeWindow_;
#elif defined(_WIN32)
        CNA::Platform::Win32NativeWindow win32;
        if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), win32))
        {
            throw std::runtime_error(
                "Wicked renderer: Windows requires a Win32 native window, got " +
                CNA::Platform::Describe(surface_.GetNativeHandle()));
        }
        wickedWindow_ = static_cast<wi::platform::window_type>(win32.hwnd);
#else
        (void)args;
        throw std::runtime_error("Wicked renderer: native window handling is unavailable");
#endif
        UpdateNativeWindowSnapshot();

        // Wicked Engine's Vulkan device is the one device renderer available on every platform CNA
        // targets with this renderer. Its D3D12 device is a Windows-only alternative that additionally
        // needs the engine's own root-signature macro in every shader, which this renderer's HLSL does
        // not yet declare -- tracked as plan_wicked.md WICKED-60, not silently selected here.
        device_ = std::make_unique<wig::GraphicsDevice_Vulkan>(
            wickedWindow_, wig::ValidationMode::Disabled, wig::GPUPreference::Discrete);
        if (!device_)
            throw std::runtime_error("Wicked renderer: failed to create the Wicked Engine device.");
    }

    void WickedRenderer::CreateSwapChainResources()
    {
        const CNA::Platform::WindowSize drawableSize = surface_.GetDrawableSize();
        const int pixelWidth = drawableSize.width;
        const int pixelHeight = drawableSize.height;

        swapChainDesc_.width = static_cast<std::uint32_t>(std::max(1, pixelWidth));
        swapChainDesc_.height = static_cast<std::uint32_t>(std::max(1, pixelHeight));
        swapChainDesc_.buffer_count = 2;
        swapChainDesc_.format = wig::Format::R8G8B8A8_UNORM;
        swapChainDesc_.vsync = swapInterval_ != 0;
        swapChainDesc_.allow_hdr = false;
        swapChainDesc_.clear_color[0] = 0.0f;
        swapChainDesc_.clear_color[1] = 0.0f;
        swapChainDesc_.clear_color[2] = 0.0f;
        swapChainDesc_.clear_color[3] = 1.0f;

        if (!device_->CreateSwapChain(&swapChainDesc_, wickedWindow_, &swapChain_))
            throw std::runtime_error("Wicked renderer: failed to create the swap chain.");
    }

    void WickedRenderer::ResolveVirtualResolution()
    {
        const CNA::Platform::WindowSize drawableSize = surface_.GetDrawableSize();
        int pixelWidth = drawableSize.width;
        int pixelHeight = drawableSize.height;
        pixelWidth = std::max(1, pixelWidth);
        pixelHeight = std::max(1, pixelHeight);

        if (preferredWidth_ <= 0 || preferredHeight_ <= 0)
        {
            virtualWidth_ = pixelWidth;
            virtualHeight_ = pixelHeight;
            return;
        }

        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer)
        {
            virtualWidth_ = preferredWidth_;
            virtualHeight_ = preferredHeight_;
            return;
        }

        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            virtualHeight_ = preferredHeight_;
            virtualWidth_ = static_cast<int>(std::lround(
                static_cast<double>(pixelWidth) * preferredHeight_ / pixelHeight));
            virtualWidth_ = std::max(1, virtualWidth_);
            return;
        }

        virtualWidth_ = preferredWidth_;
        virtualHeight_ = preferredHeight_;
    }

    void WickedRenderer::CreateSceneTarget(int width, int height, int sampleCount)
    {
        width = std::max(1, width);
        height = std::max(1, height);
        sampleCount = std::max(1, sampleCount);

        SceneTarget target;
        target.width = width;
        target.height = height;
        target.sampleCount = sampleCount;

        wig::TextureDesc colorDesc;
        colorDesc.width = static_cast<std::uint32_t>(width);
        colorDesc.height = static_cast<std::uint32_t>(height);
        colorDesc.format = wig::Format::R8G8B8A8_UNORM;
        colorDesc.bind_flags = wig::BindFlag::RENDER_TARGET | wig::BindFlag::SHADER_RESOURCE;
        colorDesc.layout = wig::ResourceState::SHADER_RESOURCE;
        colorDesc.sample_count = static_cast<std::uint32_t>(sampleCount);
        if (!device_->CreateTexture(&colorDesc, nullptr, &target.color))
        {
            if (sampleCount > 1)
            {
                // The device refused the requested sample count; fall back to no MSAA rather than
                // reporting a count that was never applied.
                CreateSceneTarget(width, height, 1);
                return;
            }
            throw std::runtime_error("Wicked renderer: failed to create the scene colour target.");
        }

        if (sampleCount > 1)
        {
            wig::TextureDesc resolveDesc = colorDesc;
            resolveDesc.sample_count = 1;
            if (!device_->CreateTexture(&resolveDesc, nullptr, &target.resolve))
            {
                CreateSceneTarget(width, height, 1);
                return;
            }
        }

        wig::TextureDesc depthDesc;
        depthDesc.width = colorDesc.width;
        depthDesc.height = colorDesc.height;
        depthDesc.format = wig::Format::D24_UNORM_S8_UINT;
        depthDesc.bind_flags = wig::BindFlag::DEPTH_STENCIL;
        depthDesc.layout = wig::ResourceState::DEPTHSTENCIL;
        depthDesc.sample_count = static_cast<std::uint32_t>(sampleCount);
        if (!device_->CreateTexture(&depthDesc, nullptr, &target.depth))
            throw std::runtime_error("Wicked renderer: failed to create the scene depth buffer.");

        scene_ = std::move(target);
        multiSampleCount_ = sampleCount > 1 ? sampleCount : 0;
        sceneCleared_ = false;
    }

    void WickedRenderer::CompileShader(wig::ShaderStage stage, const char* entryPoint,
                                              wig::Shader& out)
    {
        wi::shadercompiler::CompilerInput input;
        input.format = device_->GetShaderFormat();
        input.stage = stage;
        input.minshadermodel = wig::ShaderModel::SM_6_0;
        input.shadersourcefilename = shaderDirectory_ + "/" + kShaderFileName;
        input.entrypoint = entryPoint;

        wi::shadercompiler::CompilerOutput output;
        wi::shadercompiler::Compile(input, output);
        if (!output.IsValid() || output.shaderdata == nullptr || output.shadersize == 0)
        {
            throw std::runtime_error(
                std::string("Wicked renderer: failed to compile shader entry point '") + entryPoint +
                "'. " +
                (output.error_message.empty()
                     ? std::string("The compiler produced no output. Wicked Engine's shader "
                                   "compiler loads ./libdxcompiler.so from the CURRENT WORKING "
                                   "DIRECTORY, so it must be present there.")
                     : output.error_message));
        }

        if (!device_->CreateShader(stage, output.shaderdata, output.shadersize, &out, entryPoint))
        {
            throw std::runtime_error(
                std::string("Wicked renderer: the device rejected the compiled shader '") +
                entryPoint + "'.");
        }
    }

    void WickedRenderer::CreateBuiltinShaders()
    {
        // wi::shadercompiler reads its input from a file, so the embedded source is materialised
        // into a private temporary directory for the lifetime of this renderer. This keeps CNA free
        // of a shader-asset deployment step (plan_wicked.md design decision 4).
        std::error_code ec;
        const std::filesystem::path base =
            std::filesystem::temp_directory_path(ec) /
            ("cna-wicked-shaders-" + std::to_string(static_cast<unsigned long long>(
                 reinterpret_cast<std::uintptr_t>(this))));
        if (ec)
            throw std::runtime_error("Wicked renderer: no writable temporary directory is available.");

        std::filesystem::create_directories(base, ec);
        if (ec)
        {
            throw std::runtime_error("Wicked renderer: could not create the shader staging directory '" +
                                     base.string() + "'.");
        }
        shaderDirectory_ = base.string();
        shaderDirectoryOwned_ = true;

        {
            std::ofstream file(base / kShaderFileName, std::ios::binary | std::ios::trunc);
            if (!file)
                throw std::runtime_error("Wicked renderer: could not write the shader staging file.");
            file << kWickedShaderSource;
        }

        static constexpr const char* kVertexEntryPoints[] = {
            "Basic16VS", "Basic20VS", "Basic24VS", "Basic32VS",
            "Basic48VS", "Basic52VS", "Basic56VS", "Basic68VS"
        };
        static_assert(std::size(kVertexEntryPoints) ==
                          static_cast<std::size_t>(WickedShaderVariant::Count),
                      "Every shader variant needs its own vertex entry point.");
        static constexpr const char* kInstancedVertexEntryPoints[] = {
            "Basic16InstVS", "Basic20InstVS", "Basic24InstVS", "Basic32InstVS"
        };
        static_assert(std::size(kInstancedVertexEntryPoints) == kWickedInstancedVariantCount,
                      "Every instanced variant needs its own vertex entry point.");
        for (std::size_t i = 0; i < vertexShaders_.size(); ++i)
            CompileShader(wig::ShaderStage::VS, kVertexEntryPoints[i], vertexShaders_[i]);
        for (std::size_t i = 0; i < instancedVertexShaders_.size(); ++i)
        {
            CompileShader(wig::ShaderStage::VS, kInstancedVertexEntryPoints[i],
                          instancedVertexShaders_[i]);
        }
        static constexpr const char* kSkinnedVertexEntryPoints[] = {
            "Skinned52VS", "Skinned56VS", "Skinned68VS"
        };
        static_assert(std::size(kSkinnedVertexEntryPoints) == kWickedSkinnableVariantCount,
                      "Every skinnable variant needs its own vertex entry point.");
        for (std::size_t i = 0; i < skinnedVertexShaders_.size(); ++i)
        {
            CompileShader(wig::ShaderStage::VS, kSkinnedVertexEntryPoints[i],
                          skinnedVertexShaders_[i]);
        }
        CompileShader(wig::ShaderStage::PS, "BasicPS", pixelShader_);
        CompileShader(wig::ShaderStage::VS, "EnvMapVS", envMapVertexShader_);
        CompileShader(wig::ShaderStage::PS, "EnvMapPS", envMapPixelShader_);
        static constexpr const char* kPbrVertexEntryPoints[] = {
            "Pbr48VS", "Pbr68VS", "PbrSkinned68VS"
        };
        static_assert(std::size(kPbrVertexEntryPoints) == 3,
                      "The PBR vertex shader table must stay in step with PbrShaderIndex().");
        for (std::size_t i = 0; i < pbrVertexShaders_.size(); ++i)
            CompileShader(wig::ShaderStage::VS, kPbrVertexEntryPoints[i], pbrVertexShaders_[i]);
        CompileShader(wig::ShaderStage::PS, "PbrPS", pbrPixelShader_);

        using Element = wig::InputLayout::Element;
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic16)].elements = {
            Element{"POSITION", 0, wig::Format::R32G32B32_FLOAT, 0, 0},
            Element{"COLOR",    0, wig::Format::R8G8B8A8_UNORM,  0, 12},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic20)].elements = {
            Element{"POSITION", 0, wig::Format::R32G32B32_FLOAT, 0, 0},
            Element{"TEXCOORD", 0, wig::Format::R32G32_FLOAT,    0, 12},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic24)].elements = {
            Element{"POSITION", 0, wig::Format::R32G32B32_FLOAT, 0, 0},
            Element{"COLOR",    0, wig::Format::R8G8B8A8_UNORM,  0, 12},
            Element{"TEXCOORD", 0, wig::Format::R32G32_FLOAT,    0, 16},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic32)].elements = {
            Element{"POSITION", 0, wig::Format::R32G32B32_FLOAT, 0, 0},
            Element{"NORMAL",   0, wig::Format::R32G32B32_FLOAT, 0, 12},
            Element{"TEXCOORD", 0, wig::Format::R32G32_FLOAT,    0, 24},
        };
        // The four wider stock layouts, byte-for-byte the same element tables the D3D11/D3D12
        // renderers use for these strides.
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic48)].elements = {
            Element{"POSITION", 0, wig::Format::R32G32B32_FLOAT,    0, 0},
            Element{"NORMAL",   0, wig::Format::R32G32B32_FLOAT,    0, 12},
            Element{"TANGENT",  0, wig::Format::R32G32B32A32_FLOAT, 0, 24},
            Element{"TEXCOORD", 0, wig::Format::R32G32_FLOAT,       0, 40},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic52)].elements = {
            Element{"POSITION",     0, wig::Format::R32G32B32_FLOAT,    0, 0},
            Element{"NORMAL",       0, wig::Format::R32G32B32_FLOAT,    0, 12},
            Element{"TEXCOORD",     0, wig::Format::R32G32_FLOAT,       0, 24},
            Element{"BLENDWEIGHT",  0, wig::Format::R32G32B32A32_FLOAT, 0, 32},
            Element{"BLENDINDICES", 0, wig::Format::R8G8B8A8_UINT,      0, 48},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic56)].elements = {
            Element{"POSITION",     0, wig::Format::R32G32B32_FLOAT,    0, 0},
            Element{"NORMAL",       0, wig::Format::R32G32B32_FLOAT,    0, 12},
            Element{"TEXCOORD",     0, wig::Format::R32G32_FLOAT,       0, 24},
            Element{"BLENDWEIGHT",  0, wig::Format::R32G32B32A32_FLOAT, 0, 32},
            Element{"BLENDINDICES", 0, wig::Format::R8G8B8A8_UINT,      0, 48},
            Element{"COLOR",        0, wig::Format::R8G8B8A8_UNORM,     0, 52},
        };
        inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic68)].elements = {
            Element{"POSITION",     0, wig::Format::R32G32B32_FLOAT,    0, 0},
            Element{"NORMAL",       0, wig::Format::R32G32B32_FLOAT,    0, 12},
            Element{"TANGENT",      0, wig::Format::R32G32B32A32_FLOAT, 0, 24},
            Element{"TEXCOORD",     0, wig::Format::R32G32_FLOAT,       0, 40},
            Element{"BLENDWEIGHT",  0, wig::Format::R32G32B32A32_FLOAT, 0, 48},
            Element{"BLENDINDICES", 0, wig::Format::R8G8B8A8_UINT,      0, 64},
        };

        // EnvironmentMapEffect draws VertexPositionNormalTexture geometry, so it reuses the
        // stride-32 element list rather than declaring a fifth copy of it.
        envMapInputLayout_.elements =
            inputLayouts_[static_cast<std::size_t>(WickedShaderVariant::Basic32)].elements;

        // Each instanced layout is its non-instanced sibling plus CNA's established 64-byte
        // per-instance world matrix at input slot 1, delivered as four float4 columns. Deriving it
        // rather than writing it out four more times keeps the geometry half from drifting.
        for (std::size_t i = 0; i < instancedInputLayouts_.size(); ++i)
        {
            instancedInputLayouts_[i].elements = inputLayouts_[i].elements;
            for (std::uint32_t column = 0; column < 4; ++column)
            {
                instancedInputLayouts_[i].elements.push_back(Element{
                    "TEXCOORD", 4 + column, wig::Format::R32G32B32A32_FLOAT, 1, column * 16,
                    wig::InputClassification::PER_INSTANCE_DATA});
            }
        }
    }

    // ---- frame plumbing ----

    void WickedRenderer::BeginFrame()
    {
        if (frameActive_)
            return;
        cmd_ = device_->BeginCommandList();
        frameActive_ = true;
    }

    wig::CommandList WickedRenderer::GetCommandListEXT()
    {
        BeginFrame();
        return cmd_;
    }

    void WickedRenderer::EndRenderPassEXT()
    {
        if (!renderPassActive_)
            return;
        device_->RenderPassEnd(cmd_);
        renderPassActive_ = false;
    }

    void WickedRenderer::FlushAndWaitEXT()
    {
        EndRenderPassEXT();
        if (frameActive_)
        {
            device_->SubmitCommandLists();
            frameActive_ = false;
        }
        device_->WaitForGPU();
    }

    void WickedRenderer::BeginRenderPass()
    {
        BeginFrame();

        wig::RenderPassImage images[kWickedMaxRenderTargets * 2 + 2];
        std::uint32_t imageCount = 0;

        if (currentRenderTargetCube_ != nullptr)
        {
            const bool load = currentRenderTargetCube_->PreservesContentsEXT() &&
                              currentRenderTargetCube_->HasFaceContentEXT(currentCubeFace_);
            images[imageCount++] = wig::RenderPassImage::RenderTarget(
                &currentRenderTargetCube_->GetColorTextureEXT(),
                load ? wig::RenderPassImage::LoadOp::LOAD : wig::RenderPassImage::LoadOp::DONTCARE,
                wig::RenderPassImage::StoreOp::STORE,
                wig::ResourceState::SHADER_RESOURCE,
                wig::ResourceState::SHADER_RESOURCE,
                currentRenderTargetCube_->RenderTargetSubresourceEXT(currentCubeFace_));
            if (currentRenderTargetCube_->GetDepthTextureEXT().IsValid())
            {
                images[imageCount++] = wig::RenderPassImage::DepthStencil(
                    &currentRenderTargetCube_->GetDepthTextureEXT(),
                    load ? wig::RenderPassImage::LoadOp::LOAD
                         : wig::RenderPassImage::LoadOp::DONTCARE);
            }
            currentRenderTargetCube_->MarkFaceRenderedEXT(currentCubeFace_);
        }
        else if (currentRenderTargetCount_ > 0)
        {
            for (int slot = 0; slot < currentRenderTargetCount_; ++slot)
            {
                WickedRenderTargetRenderer* target = currentRenderTargets_[slot];
                const bool load = target->PreservesContentsEXT() && target->HasContentEXT();
                images[imageCount++] = wig::RenderPassImage::RenderTarget(
                    &target->GetColorTextureEXT(),
                    load ? wig::RenderPassImage::LoadOp::LOAD
                         : wig::RenderPassImage::LoadOp::DONTCARE);
                target->MarkRenderedEXT();
            }
            for (int slot = 0; slot < currentRenderTargetCount_; ++slot)
            {
                const wig::Texture& resolve = currentRenderTargets_[slot]->GetResolveTextureEXT();
                if (resolve.IsValid())
                    images[imageCount++] = wig::RenderPassImage::Resolve(&resolve);
            }
            // Depth comes from slot 0, which is what XNA's own MRT rules require: every target in
            // the set shares one depth/stencil buffer.
            WickedRenderTargetRenderer* primary = currentRenderTargets_[0];
            if (primary->GetDepthTextureEXT().IsValid())
            {
                const bool load = primary->PreservesContentsEXT() && primary->HasContentEXT();
                images[imageCount++] = wig::RenderPassImage::DepthStencil(
                    &primary->GetDepthTextureEXT(),
                    load ? wig::RenderPassImage::LoadOp::LOAD
                         : wig::RenderPassImage::LoadOp::DONTCARE);
            }
        }
        else
        {
            images[imageCount++] = wig::RenderPassImage::RenderTarget(
                &scene_.color,
                sceneCleared_ ? wig::RenderPassImage::LoadOp::LOAD
                              : wig::RenderPassImage::LoadOp::DONTCARE);
            images[imageCount++] = wig::RenderPassImage::DepthStencil(
                &scene_.depth,
                sceneCleared_ ? wig::RenderPassImage::LoadOp::LOAD
                              : wig::RenderPassImage::LoadOp::DONTCARE);
            if (scene_.resolve.IsValid())
                images[imageCount++] = wig::RenderPassImage::Resolve(&scene_.resolve);
            sceneCleared_ = true;
        }

        device_->RenderPassBegin(images, imageCount, cmd_);
        renderPassActive_ = true;
        ApplyViewportAndScissor();
    }

    void WickedRenderer::EnsureRenderPass()
    {
        if (!renderPassActive_)
            BeginRenderPass();
    }

    int WickedRenderer::ActiveTargetWidth() const
    {
        if (currentRenderTargetCube_ != nullptr) return currentRenderTargetCube_->GetSize();
        return currentRenderTargetCount_ > 0 ? currentRenderTargets_[0]->GetWidth() : scene_.width;
    }

    int WickedRenderer::ActiveTargetHeight() const
    {
        if (currentRenderTargetCube_ != nullptr) return currentRenderTargetCube_->GetSize();
        return currentRenderTargetCount_ > 0 ? currentRenderTargets_[0]->GetHeight() : scene_.height;
    }

    void WickedRenderer::ApplyViewportAndScissor()
    {
        if (!renderPassActive_)
            return;

        wig::Viewport viewport;
        if (viewportSet_)
        {
            viewport.top_left_x = viewport_[0];
            viewport.top_left_y = viewport_[1];
            viewport.width = viewport_[2];
            viewport.height = viewport_[3];
            viewport.min_depth = viewport_[4];
            viewport.max_depth = viewport_[5];
        }
        else
        {
            viewport.width = static_cast<float>(ActiveTargetWidth());
            viewport.height = static_cast<float>(ActiveTargetHeight());
        }
        device_->BindViewports(1, &viewport, cmd_);

        wig::Rect scissor;
        if (scissorEnabled_)
        {
            scissor.left = scissorRect_[0];
            scissor.top = scissorRect_[1];
            scissor.right = scissorRect_[0] + scissorRect_[2];
            scissor.bottom = scissorRect_[1] + scissorRect_[3];
        }
        else
        {
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = ActiveTargetWidth();
            scissor.bottom = ActiveTargetHeight();
        }
        device_->BindScissorRects(1, &scissor, cmd_);
    }

    // ---- pipeline / sampler caches ----

    wig::PipelineState* WickedRenderer::GetPipeline(const WickedPipelineKey& key)
    {
        auto existing = pipelines_.find(key);
        if (existing != pipelines_.end())
            return &existing->second.pipeline;

        WickedPipelineEntry entry;

        entry.blend.independent_blend_enable = true;
        for (int slot = 0; slot < 4; ++slot)
        {
            wig::BlendState::RenderTargetBlendState& target = entry.blend.render_target[slot];
            target.blend_enable = key.blendEnabled != 0;
            target.src_blend = ToWickedBlend(key.colorSrcBlend);
            target.dest_blend = ToWickedBlend(key.colorDstBlend);
            target.blend_op = ToWickedBlendOp(key.colorBlendFunc);
            target.src_blend_alpha = ToWickedBlend(key.alphaSrcBlend);
            target.dest_blend_alpha = ToWickedBlend(key.alphaDstBlend);
            target.blend_op_alpha = ToWickedBlendOp(key.alphaBlendFunc);
            target.render_target_write_mask = ToWickedColorWrite(key.colorWriteChannels[slot]);
        }

        entry.rasterizer.fill_mode = ToWickedFill(key.fillMode);
        entry.rasterizer.cull_mode = ToWickedCull(key.cullMode);
        entry.rasterizer.front_counter_clockwise = false;
        entry.rasterizer.depth_bias = static_cast<std::int32_t>(key.depthBias);
        entry.rasterizer.slope_scaled_depth_bias = key.slopeScaleDepthBias;
        entry.rasterizer.depth_clip_enable = true;
        entry.rasterizer.multisample_enable = scene_.sampleCount > 1;

        entry.depthStencil.depth_enable = key.depthEnable != 0;
        entry.depthStencil.depth_write_mask = key.depthWriteEnable != 0
            ? wig::DepthWriteMask::ALL
            : wig::DepthWriteMask::ZERO;
        entry.depthStencil.depth_func = ToWickedCompare(key.depthFunc);
        entry.depthStencil.stencil_enable = key.stencilEnable != 0;
        entry.depthStencil.stencil_read_mask = static_cast<std::uint8_t>(key.stencilMask & 0xFF);
        entry.depthStencil.stencil_write_mask =
            static_cast<std::uint8_t>(key.stencilWriteMask & 0xFF);
        entry.depthStencil.front_face.stencil_func = ToWickedCompare(key.stencilFunc);
        entry.depthStencil.front_face.stencil_pass_op = ToWickedStencilOp(key.stencilPass);
        entry.depthStencil.front_face.stencil_fail_op = ToWickedStencilOp(key.stencilFail);
        entry.depthStencil.front_face.stencil_depth_fail_op =
            ToWickedStencilOp(key.stencilDepthFail);
        if (key.twoSidedStencil != 0)
        {
            entry.depthStencil.back_face.stencil_func = ToWickedCompare(key.ccwStencilFunc);
            entry.depthStencil.back_face.stencil_pass_op = ToWickedStencilOp(key.ccwStencilPass);
            entry.depthStencil.back_face.stencil_fail_op = ToWickedStencilOp(key.ccwStencilFail);
            entry.depthStencil.back_face.stencil_depth_fail_op =
                ToWickedStencilOp(key.ccwStencilDepthFail);
        }
        else
        {
            entry.depthStencil.back_face = entry.depthStencil.front_face;
        }

        // A multi-stream draw needs its own input layout: the variant's own element table places
        // every element in slot 0 at its offset inside the COMBINED vertex, which is where those
        // bytes live only when one buffer holds all of them.
        if (key.streamCount > 0)
        {
            const wig::InputLayout& base = key.instanced != 0
                ? instancedInputLayouts_[key.variant]
                : inputLayouts_[key.variant];
            for (const wig::InputLayout::Element& element : base.elements)
            {
                wig::InputLayout::Element reslotted = element;
                if (element.input_slot_class == wig::InputClassification::PER_INSTANCE_DATA)
                {
                    // The per-instance elements already name their own slot and their offsets are
                    // relative to the instance record, not the combined vertex.
                    entry.inputLayout.elements.push_back(reslotted);
                    continue;
                }
                for (std::uint32_t stream = 0; stream < key.streamCount; ++stream)
                {
                    const std::int32_t base_offset = key.streamBase[stream];
                    const std::int32_t end = base_offset + key.streamStride[stream];
                    if (static_cast<std::int32_t>(element.aligned_byte_offset) >= base_offset &&
                        static_cast<std::int32_t>(element.aligned_byte_offset) < end)
                    {
                        reslotted.input_slot = stream;
                        reslotted.aligned_byte_offset =
                            element.aligned_byte_offset - static_cast<std::uint32_t>(base_offset);
                        break;
                    }
                }
                entry.inputLayout.elements.push_back(reslotted);
            }
        }

        auto emplaced = pipelines_.emplace(key, std::move(entry));
        WickedPipelineEntry& stored = emplaced.first->second;

        wig::PipelineStateDesc desc;
        if (key.pbr != 0)
        {
            desc.vs = &pbrVertexShaders_[PbrShaderIndex(key)];
            desc.ps = &pbrPixelShader_;
            desc.il = &inputLayouts_[key.variant];
        }
        else if (key.envMap != 0)
        {
            desc.vs = &envMapVertexShader_;
            desc.ps = &envMapPixelShader_;
            desc.il = &envMapInputLayout_;
        }
        else
        {
            if (key.skinned != 0)
            {
                desc.vs = &skinnedVertexShaders_[key.variant - kWickedFirstSkinnableVariant];
            }
            else if (key.instanced != 0)
            {
                desc.vs = &instancedVertexShaders_[key.variant];
            }
            else
            {
                desc.vs = &vertexShaders_[key.variant];
            }
            desc.ps = &pixelShader_;
            // A skinned draw reads the same geometry layout as its non-skinned sibling; only the
            // vertex program differs.
            desc.il = key.instanced != 0 ? &instancedInputLayouts_[key.variant]
                                         : &inputLayouts_[key.variant];
        }
        if (key.streamCount > 0)
            desc.il = &stored.inputLayout;
        desc.bs = &stored.blend;
        desc.rs = &stored.rasterizer;
        desc.dss = &stored.depthStencil;
        desc.pt = static_cast<wig::PrimitiveTopology>(key.topology);
        desc.sample_mask = key.multiSampleMask;

        if (!device_->CreatePipelineState(&desc, &stored.pipeline))
        {
            pipelines_.erase(emplaced.first);
            throw std::runtime_error("Wicked renderer: failed to create a graphics pipeline state.");
        }
        return &stored.pipeline;
    }

    const wig::Sampler* WickedRenderer::GetSampler(int filter, int addressU, int addressV,
                                                          int maxAnisotropy)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(filter)) << 48) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(addressU)) << 32) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(addressV)) << 16) |
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(maxAnisotropy) & 0xFFFFu);

        auto existing = samplers_.find(key);
        if (existing != samplers_.end())
            return &existing->second;

        wig::SamplerDesc desc;
        desc.filter = ToWickedFilter(filter);
        desc.address_u = ToWickedAddress(addressU);
        desc.address_v = ToWickedAddress(addressV);
        desc.address_w = wig::TextureAddressMode::CLAMP;
        desc.max_anisotropy = static_cast<std::uint32_t>(std::clamp(maxAnisotropy, 1, 16));

        wig::Sampler sampler;
        if (!device_->CreateSampler(&desc, &sampler))
            throw std::runtime_error("Wicked renderer: failed to create a sampler.");
        return &samplers_.emplace(key, std::move(sampler)).first->second;
    }

    void WickedRenderer::BindCommonState(wig::CommandList cmd, const WickedPipelineKey& key,
                                                const WickedShaderConstants& constants)
    {
        device_->BindPipelineState(GetPipeline(key), cmd);
        device_->BindDynamicConstantBuffer(constants, 0, cmd);
        device_->BindBlendFactor(blendFactor_[0], blendFactor_[1], blendFactor_[2],
                                 blendFactor_[3], cmd);
        device_->BindStencilRef(static_cast<std::uint32_t>(referenceStencil_), cmd);
    }

    // ---- clears ----

    void WickedRenderer::ClearActiveTarget(bool color, const float rgba[4],
                                                  bool depth, float depthValue,
                                                  bool stencil, int stencilValue)
    {
        // The cheapest and most exact clear is a render-pass load operation, which is available
        // only while no pass is open. Once one is open, the equivalent is a full-target quad drawn
        // with the requested colour/depth/stencil writes -- the same fallback the other
        // render-pass-based CNA renderers use for a mid-frame clear.
        if (!renderPassActive_ && currentRenderTargetCount_ == 0 &&
            currentRenderTargetCube_ == nullptr && color && !sceneCleared_)
        {
            BeginFrame();
            wig::RenderPassImage images[3];
            std::uint32_t imageCount = 0;
            wig::Texture& target = scene_.color;
            target.desc.clear.color[0] = rgba[0];
            target.desc.clear.color[1] = rgba[1];
            target.desc.clear.color[2] = rgba[2];
            target.desc.clear.color[3] = rgba[3];
            scene_.depth.desc.clear.depth_stencil.depth = depth ? depthValue : 1.0f;
            scene_.depth.desc.clear.depth_stencil.stencil =
                stencil ? static_cast<std::uint32_t>(stencilValue) : 0u;
            images[imageCount++] = wig::RenderPassImage::RenderTarget(
                &target, wig::RenderPassImage::LoadOp::CLEAR);
            images[imageCount++] = wig::RenderPassImage::DepthStencil(
                &scene_.depth, wig::RenderPassImage::LoadOp::CLEAR);
            if (scene_.resolve.IsValid())
                images[imageCount++] = wig::RenderPassImage::Resolve(&scene_.resolve);
            device_->RenderPassBegin(images, imageCount, cmd_);
            renderPassActive_ = true;
            sceneCleared_ = true;
            ApplyViewportAndScissor();
            return;
        }

        EnsureRenderPass();
        DrawFullTargetQuad(rgba, depthValue, color, depth, stencil, stencilValue);
    }

    void WickedRenderer::DrawFullTargetQuad(const float rgba[4], float depthValue,
                                                   bool writeColor, bool writeDepth,
                                                   bool writeStencil, int stencilValue)
    {
        struct QuadVertex
        {
            float x, y, z;
            std::uint8_t r, g, b, a;
            float u, v;
        };
        static_assert(sizeof(QuadVertex) == 24, "The quad vertex must match stride 24.");

        const auto channel = [](float value) -> std::uint8_t
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        const std::uint8_t r = channel(rgba[0]);
        const std::uint8_t g = channel(rgba[1]);
        const std::uint8_t b = channel(rgba[2]);
        const std::uint8_t a = channel(rgba[3]);

        // Clip-space corners, so the quad covers the whole target regardless of the view transform.
        const float z = std::clamp(depthValue, 0.0f, 1.0f);
        const QuadVertex vertices[6] = {
            {-1.0f,  1.0f, z, r, g, b, a, 0.0f, 0.0f},
            { 1.0f,  1.0f, z, r, g, b, a, 1.0f, 0.0f},
            { 1.0f, -1.0f, z, r, g, b, a, 1.0f, 1.0f},
            {-1.0f,  1.0f, z, r, g, b, a, 0.0f, 0.0f},
            { 1.0f, -1.0f, z, r, g, b, a, 1.0f, 1.0f},
            {-1.0f, -1.0f, z, r, g, b, a, 0.0f, 1.0f},
        };

        WickedPipelineKey key{};
        key.variant = static_cast<std::uint32_t>(WickedShaderVariant::Basic24);
        key.topology = static_cast<std::uint32_t>(wig::PrimitiveTopology::TRIANGLELIST);
        key.blendEnabled = 0;
        key.colorSrcBlend = 0;  // Blend::One
        key.alphaSrcBlend = 0;
        key.colorDstBlend = 1;  // Blend::Zero
        key.alphaDstBlend = 1;
        for (int slot = 0; slot < 4; ++slot)
            key.colorWriteChannels[slot] = writeColor ? 15 : 0;
        key.multiSampleMask = 0xFFFFFFFFu;
        key.depthEnable = writeDepth ? 1u : 0u;
        key.depthWriteEnable = writeDepth ? 1u : 0u;
        key.depthFunc = 0; // CompareFunction::Always
        key.stencilEnable = writeStencil ? 1u : 0u;
        key.stencilFunc = 0;  // Always
        key.stencilPass = 2;  // StencilOperation::Replace
        key.stencilFail = 0;  // Keep
        key.stencilDepthFail = 0;
        key.stencilMask = 0xFF;
        key.stencilWriteMask = 0xFF;
        key.cullMode = 0;
        key.fillMode = 0;

        WickedShaderConstants constants;
        // Identity, so the clip-space corners above pass straight through.
        constants.mvp[0] = 1.0f;  constants.mvp[5] = 1.0f;
        constants.mvp[10] = 1.0f; constants.mvp[15] = 1.0f;
        constants.world[0] = 1.0f;  constants.world[5] = 1.0f;
        constants.world[10] = 1.0f; constants.world[15] = 1.0f;
        constants.flags[1] = 1.0f; // vertex colour on, texture off, lighting off

        const int savedStencilRef = referenceStencil_;
        if (writeStencil)
            referenceStencil_ = stencilValue;

        wig::CommandList cmd = GetCommandListEXT();
        BindCommonState(cmd, key, constants);

        const wig::GraphicsDevice::GPUAllocation allocation = device_->AllocateGPU(sizeof(vertices), cmd);
        std::memcpy(allocation.data, vertices, sizeof(vertices));
        const wig::GPUBuffer* buffers[] = {&allocation.buffer};
        const std::uint32_t strides[] = {sizeof(QuadVertex)};
        const std::uint64_t offsets[] = {allocation.offset};
        device_->BindVertexBuffers(buffers, 0, 1, strides, offsets, cmd);
        device_->BindResource(&whiteTexture_, 0, cmd);
        device_->BindResource(&whiteTexture_, 1, cmd);
        device_->BindSampler(GetSampler(1, 1, 1, 1), 0, cmd);
        device_->Draw(6, 0, cmd);

        referenceStencil_ = savedStencilRef;
    }

    void WickedRenderer::Clear(float r, float g, float b, float a)
    {
        const float rgba[4] = {r, g, b, a};
        ClearActiveTarget(true, rgba, false, 1.0f, false, 0);
    }

    void WickedRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const float rgba[4] = {r, g, b, a};
        ClearActiveTarget(true, rgba, true, depth, false, 0);
    }

    void WickedRenderer::ClearDepth(float depth)
    {
        const float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ClearActiveTarget(false, rgba, true, depth, false, 0);
    }

    void WickedRenderer::ClearStencil(int stencil)
    {
        const float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ClearActiveTarget(false, rgba, false, 1.0f, true, stencil);
    }

    void WickedRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        const float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ClearActiveTarget(false, rgba, true, depth, true, stencil);
    }

    void WickedRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const float rgba[4] = {r, g, b, a};
        ClearActiveTarget(true, rgba, false, 1.0f, true, stencil);
    }

    void WickedRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                          float depth, int stencil)
    {
        const float rgba[4] = {r, g, b, a};
        ClearActiveTarget(true, rgba, true, depth, true, stencil);
    }

    // ---- present ----

    WickedRenderer::PresentRect WickedRenderer::ComputePresentRect() const
    {
        const float surfaceWidth = static_cast<float>(swapChainDesc_.width);
        const float surfaceHeight = static_cast<float>(swapChainDesc_.height);
        const float logicalWidth = static_cast<float>(std::max(1, scene_.width));
        const float logicalHeight = static_cast<float>(std::max(1, scene_.height));

        PresentRect rect;
        switch (presentationMode_)
        {
            case CnaPresentationMode::Stretch:
                rect.x = 0.0f;
                rect.y = 0.0f;
                rect.w = surfaceWidth;
                rect.h = surfaceHeight;
                return rect;
            case CnaPresentationMode::Overscan:
            {
                const float scale = std::max(surfaceWidth / logicalWidth,
                                             surfaceHeight / logicalHeight);
                rect.w = logicalWidth * scale;
                rect.h = logicalHeight * scale;
                break;
            }
            case CnaPresentationMode::NativeBackBuffer:
                rect.w = logicalWidth;
                rect.h = logicalHeight;
                break;
            case CnaPresentationMode::Letterbox:
            case CnaPresentationMode::FixedHeightDynamicWidth:
            default:
            {
                const float scale = std::min(surfaceWidth / logicalWidth,
                                             surfaceHeight / logicalHeight);
                rect.w = logicalWidth * scale;
                rect.h = logicalHeight * scale;
                break;
            }
        }
        rect.x = (surfaceWidth - rect.w) * 0.5f;
        rect.y = (surfaceHeight - rect.h) * 0.5f;
        return rect;
    }

    void WickedRenderer::Present()
    {
        BeginFrame();
        EndRenderPassEXT();

        // A window resize invalidates the swap chain's extent; recreating it before the present
        // pass keeps the blit rectangle and the surface in agreement.
        const CNA::Platform::WindowSize drawableSize = surface_.GetDrawableSize();
        int pixelWidth = drawableSize.width;
        int pixelHeight = drawableSize.height;
        pixelWidth = std::max(1, pixelWidth);
        pixelHeight = std::max(1, pixelHeight);
        if (static_cast<std::uint32_t>(pixelWidth) != swapChainDesc_.width ||
            static_cast<std::uint32_t>(pixelHeight) != swapChainDesc_.height)
        {
            swapChainDesc_.width = static_cast<std::uint32_t>(pixelWidth);
            swapChainDesc_.height = static_cast<std::uint32_t>(pixelHeight);
            device_->CreateSwapChain(&swapChainDesc_, nullptr, &swapChain_);

            const int previousVirtualWidth = virtualWidth_;
            const int previousVirtualHeight = virtualHeight_;
            ResolveVirtualResolution();
            if (virtualWidth_ != previousVirtualWidth || virtualHeight_ != previousVirtualHeight)
                CreateSceneTarget(virtualWidth_, virtualHeight_, std::max(1, multiSampleCount_));
        }

        const wig::Texture& presented = scene_.resolve.IsValid() ? scene_.resolve : scene_.color;

        device_->RenderPassBegin(&swapChain_, cmd_);

        const PresentRect rect = ComputePresentRect();
        wig::Viewport viewport;
        viewport.top_left_x = rect.x;
        viewport.top_left_y = rect.y;
        viewport.width = rect.w;
        viewport.height = rect.h;
        device_->BindViewports(1, &viewport, cmd_);

        wig::Rect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<std::int32_t>(swapChainDesc_.width);
        scissor.bottom = static_cast<std::int32_t>(swapChainDesc_.height);
        device_->BindScissorRects(1, &scissor, cmd_);

        struct BlitVertex
        {
            float x, y, z;
            std::uint8_t r, g, b, a;
            float u, v;
        };
        const BlitVertex vertices[6] = {
            {-1.0f,  1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 1.0f},
        };

        WickedPipelineKey key{};
        key.variant = static_cast<std::uint32_t>(WickedShaderVariant::Basic24);
        key.topology = static_cast<std::uint32_t>(wig::PrimitiveTopology::TRIANGLELIST);
        key.colorSrcBlend = 0;
        key.alphaSrcBlend = 0;
        key.colorDstBlend = 1;
        key.alphaDstBlend = 1;
        key.multiSampleMask = 0xFFFFFFFFu;
        key.depthFunc = 0;
        key.stencilMask = 0xFF;
        key.stencilWriteMask = 0xFF;

        WickedShaderConstants constants;
        constants.mvp[0] = 1.0f;  constants.mvp[5] = 1.0f;
        constants.mvp[10] = 1.0f; constants.mvp[15] = 1.0f;
        constants.world[0] = 1.0f;  constants.world[5] = 1.0f;
        constants.world[10] = 1.0f; constants.world[15] = 1.0f;
        constants.flags[0] = 1.0f; // sample texture 0
        constants.flags[1] = 1.0f; // modulate by the (white) vertex colour

        BindCommonState(cmd_, key, constants);
        const wig::GraphicsDevice::GPUAllocation allocation = device_->AllocateGPU(sizeof(vertices), cmd_);
        std::memcpy(allocation.data, vertices, sizeof(vertices));
        const wig::GPUBuffer* buffers[] = {&allocation.buffer};
        const std::uint32_t strides[] = {sizeof(BlitVertex)};
        const std::uint64_t offsets[] = {allocation.offset};
        device_->BindVertexBuffers(buffers, 0, 1, strides, offsets, cmd_);
        device_->BindResource(&presented, 0, cmd_);
        device_->BindResource(&whiteTexture_, 1, cmd_);
        device_->BindSampler(GetSampler(0, 1, 1, 1), 0, cmd_);
        device_->Draw(6, 0, cmd_);

        device_->RenderPassEnd(cmd_);
        renderPassActive_ = false;

        device_->SubmitCommandLists();
        frameActive_ = false;
        sceneCleared_ = false;
    }

    // ---- presentation / window ----

    void WickedRenderer::GetViewportSize(int& width, int& height)
    {
        width = scene_.width;
        height = scene_.height;
    }

    void WickedRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        UpdateNativeWindowSnapshot();
    }

    void WickedRenderer::UpdateNativeWindowSnapshot()
    {
#ifdef WICKED_CNA_PLATFORM
        const CNA::Platform::WindowSize size = surface_.GetDrawableSize();
        nativeWindow_.width = size.width;
        nativeWindow_.height = size.height;
        nativeWindow_.dpi = surface_.GetDisplayScale() * 96.0f;
#endif
    }

    void WickedRenderer::SetVirtualResolution(int width, int height)
    {
        preferredWidth_ = width;
        preferredHeight_ = height;
        const int previousWidth = virtualWidth_;
        const int previousHeight = virtualHeight_;
        ResolveVirtualResolution();
        if (virtualWidth_ != previousWidth || virtualHeight_ != previousHeight)
        {
            FlushAndWaitEXT();
            CreateSceneTarget(virtualWidth_, virtualHeight_, std::max(1, multiSampleCount_));
        }
    }

    void WickedRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SetVirtualResolution(preferredWidth_, preferredHeight_);
    }

    void WickedRenderer::SetSwapInterval(int interval)
    {
        if (interval == swapInterval_)
            return;
        swapInterval_ = interval;
        swapChainDesc_.vsync = interval != 0;
        FlushAndWaitEXT();
        device_->CreateSwapChain(&swapChainDesc_, nullptr, &swapChain_);
    }

    int WickedRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const int requested = std::max(1, requestedMultiSampleCount);
        if (requested == std::max(1, multiSampleCount_))
            return multiSampleCount_;
        FlushAndWaitEXT();
        requestedMultiSampleCount_ = requested;
        CreateSceneTarget(virtualWidth_, virtualHeight_, requested);
        return multiSampleCount_;
    }

    bool WickedRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                         float& logX, float& logY) const
    {
        const PresentRect rect = ComputePresentRect();
        if (rect.w <= 0.0f || rect.h <= 0.0f)
            return false;
        const float drawableX = surface_.WindowToDrawable(windowX);
        const float drawableY = surface_.WindowToDrawable(windowY);
        logX = (drawableX - rect.x) * static_cast<float>(scene_.width) / rect.w;
        logY = (drawableY - rect.y) * static_cast<float>(scene_.height) / rect.h;
        return drawableX >= rect.x && drawableX < rect.x + rect.w
            && drawableY >= rect.y && drawableY < rect.y + rect.h;
    }

    bool WickedRenderer::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        const PresentRect rect = ComputePresentRect();
        if (scene_.width <= 0 || scene_.height <= 0)
            return false;
        windowX = surface_.DrawableToWindow(
            rect.x + logX * rect.w / static_cast<float>(scene_.width));
        windowY = surface_.DrawableToWindow(
            rect.y + logY * rect.h / static_cast<float>(scene_.height));
        return true;
    }

    void WickedRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("Wicked renderer: invalid ReadBackbuffer region.");
        if (x + w > scene_.width || y + h > scene_.height)
            throw std::runtime_error("Wicked renderer: ReadBackbuffer region exceeds the back buffer.");

        const wig::Texture& source = scene_.resolve.IsValid() ? scene_.resolve : scene_.color;

        wig::TextureDesc readbackDesc;
        readbackDesc.width = static_cast<std::uint32_t>(scene_.width);
        readbackDesc.height = static_cast<std::uint32_t>(scene_.height);
        readbackDesc.format = source.desc.format;
        readbackDesc.usage = wig::Usage::READBACK;
        readbackDesc.bind_flags = wig::BindFlag::NONE;
        readbackDesc.layout = wig::ResourceState::COPY_DST;
        wig::Texture readback;
        if (!device_->CreateTexture(&readbackDesc, nullptr, &readback))
            throw std::runtime_error("Wicked renderer: failed to create a back-buffer readback texture.");

        EndRenderPassEXT();
        wig::CommandList cmd = GetCommandListEXT();

        const wig::GPUBarrier toCopySrc = wig::GPUBarrier::Image(
            &source, wig::ResourceState::SHADER_RESOURCE, wig::ResourceState::COPY_SRC);
        device_->Barrier(&toCopySrc, 1, cmd);
        device_->CopyTexture(&readback, 0, 0, 0, 0, 0, &source, 0, 0, cmd);
        const wig::GPUBarrier toShaderResource = wig::GPUBarrier::Image(
            &source, wig::ResourceState::COPY_SRC, wig::ResourceState::SHADER_RESOURCE);
        device_->Barrier(&toShaderResource, 1, cmd);

        FlushAndWaitEXT();

        if (readback.mapped_data == nullptr || readback.mapped_subresource_count == 0)
            throw std::runtime_error("Wicked renderer: the back-buffer readback texture is unmapped.");

        const auto* mapped = static_cast<const std::uint8_t*>(readback.mapped_data);
        const std::uint32_t rowPitch = readback.mapped_subresources[0].row_pitch;
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(pixels + static_cast<std::size_t>(row) * w * 4,
                        mapped + static_cast<std::size_t>(y + row) * rowPitch +
                            static_cast<std::size_t>(x) * 4,
                        static_cast<std::size_t>(w) * 4);
        }
    }

    // ---- resources ----

    std::unique_ptr<ITextureRenderer> WickedRenderer::CreateTexture(const ImageData& data)
    {
        const int width = std::max(1, data.width);
        const int height = std::max(1, data.height);

        wig::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(width);
        desc.height = static_cast<std::uint32_t>(height);
        desc.format = wig::Format::R8G8B8A8_UNORM;
        desc.bind_flags = wig::BindFlag::SHADER_RESOURCE;
        desc.layout = wig::ResourceState::SHADER_RESOURCE;
        desc.mip_levels = static_cast<std::uint32_t>(std::max(1, data.mipLevels));

        const std::size_t level0Bytes = static_cast<std::size_t>(width) * height * 4;
        const bool hasPixels = data.pixels.size() >= level0Bytes;

        // Every declared level is filled with a real 2x2 box-filtered reduction of the one below
        // it, computed here on the CPU (plan_wicked.md WICKED-28).
        //
        // A GPU blit chain would be faster, but it needs each mip level transitioned independently
        // between render-target and shader-resource while the SAME image is both sampled and
        // rendered into -- per-subresource layout reasoning that cannot be verified anywhere in
        // this environment and fails silently when wrong. Texture creation is a load-time
        // operation in CNA, so the CPU cost buys determinism at a price nothing here pays per
        // frame.
        const std::uint32_t mipLevels = std::max(1u, desc.mip_levels);
        std::vector<std::uint8_t> mipChain;
        std::vector<wig::SubresourceData> initial(mipLevels);
        if (hasPixels)
        {
            std::size_t upperLevelBytes = 0;
            for (std::uint32_t level = 1; level < mipLevels; ++level)
            {
                const std::size_t levelWidth = std::max(1, width >> level);
                const std::size_t levelHeight = std::max(1, height >> level);
                upperLevelBytes += levelWidth * levelHeight * 4;
            }
            mipChain.resize(upperLevelBytes);

            const std::uint8_t* previous = data.pixels.data();
            int previousWidth = width;
            int previousHeight = height;
            std::size_t chainOffset = 0;

            initial[0].data_ptr = data.pixels.data();
            initial[0].row_pitch = static_cast<std::uint32_t>(width) * 4u;
            initial[0].slice_pitch = static_cast<std::uint32_t>(level0Bytes);

            for (std::uint32_t level = 1; level < mipLevels; ++level)
            {
                const int levelWidth = std::max(1, width >> level);
                const int levelHeight = std::max(1, height >> level);
                std::uint8_t* destination = mipChain.data() + chainOffset;

                for (int y = 0; y < levelHeight; ++y)
                {
                    // A level whose parent is already 1 texel on an axis samples that texel twice,
                    // which is what keeps a non-square chain correct all the way to 1x1.
                    const int y0 = std::min(y * 2, previousHeight - 1);
                    const int y1 = std::min(y * 2 + 1, previousHeight - 1);
                    for (int x = 0; x < levelWidth; ++x)
                    {
                        const int x0 = std::min(x * 2, previousWidth - 1);
                        const int x1 = std::min(x * 2 + 1, previousWidth - 1);
                        const std::size_t offsets[4] = {
                            (static_cast<std::size_t>(y0) * previousWidth + x0) * 4,
                            (static_cast<std::size_t>(y0) * previousWidth + x1) * 4,
                            (static_cast<std::size_t>(y1) * previousWidth + x0) * 4,
                            (static_cast<std::size_t>(y1) * previousWidth + x1) * 4,
                        };
                        std::uint8_t* texel =
                            destination + (static_cast<std::size_t>(y) * levelWidth + x) * 4;
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            const unsigned sum =
                                static_cast<unsigned>(previous[offsets[0] + channel]) +
                                previous[offsets[1] + channel] +
                                previous[offsets[2] + channel] +
                                previous[offsets[3] + channel];
                            texel[channel] = static_cast<std::uint8_t>((sum + 2) / 4);
                        }
                    }
                }

                initial[level].data_ptr = destination;
                initial[level].row_pitch = static_cast<std::uint32_t>(levelWidth) * 4u;
                initial[level].slice_pitch =
                    static_cast<std::uint32_t>(levelWidth) * levelHeight * 4u;

                previous = destination;
                previousWidth = levelWidth;
                previousHeight = levelHeight;
                chainOffset += static_cast<std::size_t>(levelWidth) * levelHeight * 4;
            }
        }

        wig::Texture texture;
        if (!device_->CreateTexture(&desc, hasPixels ? initial.data() : nullptr, &texture))
            throw std::runtime_error("Wicked renderer: failed to create a texture.");

        return std::make_unique<WickedTextureRenderer>(this, std::move(texture));
    }

    std::unique_ptr<ISpriteBatchRenderer> WickedRenderer::CreateSpriteBatch()
    {
        return std::make_unique<WickedSpriteBatchRenderer>(this);
    }

    std::unique_ptr<IOcclusionQueryRenderer> WickedRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<WickedOcclusionQueryRenderer>(this);
    }

    std::unique_ptr<IVertexBufferRenderer> WickedRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<WickedVertexBufferRenderer>(this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> WickedRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<WickedIndexBufferRenderer>(this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> WickedRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<WickedIndexBufferRenderer>(this, index_capacity, true);
    }

    std::unique_ptr<ITextureCubeRenderer> WickedRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        // Only SurfaceFormat::Color is stored: CNA's shared Texture2D/TextureCube layer already
        // converts every format it reads to RGBA8 before it reaches a renderer, so accepting the
        // ordinal and storing RGBA8 is what every other CNA renderer does here too.
        (void)surfaceFormat;
        return std::make_unique<WickedTextureCubeRenderer>(this, size, mipMap);
    }

    std::unique_ptr<ITexture3DRenderer> WickedRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        (void)surfaceFormat;
        return std::make_unique<WickedTexture3DRenderer>(this, w, h, depth, mipMap);
    }

    std::unique_ptr<IRenderTargetRenderer> WickedRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<WickedRenderTargetRenderer>(
            this, w, h, depthFormat, preserveContents, mipMap, multiSampleCount);
    }

    void WickedRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        auto* target = dynamic_cast<WickedRenderTargetRenderer*>(rt);
        const int desiredCount = target != nullptr ? 1 : 0;
        if (currentRenderTargetCube_ == nullptr && currentRenderTargetCount_ == desiredCount &&
            (desiredCount == 0 || currentRenderTargets_[0] == target))
        {
            return;
        }
        EndRenderPassEXT();
        currentRenderTargets_ = {};
        currentRenderTargetCount_ = desiredCount;
        if (target != nullptr)
            currentRenderTargets_[0] = target;
        currentRenderTargetCube_ = nullptr;
        viewportSet_ = false;
    }

    std::unique_ptr<IRenderTargetCubeRenderer> WickedRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<WickedRenderTargetCubeRenderer>(
            this, size, depthFormat, preserveContents, mipMap, multiSampleCount);
    }

    void WickedRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        auto* target = dynamic_cast<WickedRenderTargetCubeRenderer*>(rt);
        if (target == nullptr)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (face < 0 || face > 5)
        {
            throw std::runtime_error(
                "Wicked renderer: cube face index " + std::to_string(face) + " is out of range.");
        }
        if (target == currentRenderTargetCube_ && face == currentCubeFace_)
            return;
        EndRenderPassEXT();
        currentRenderTargets_ = {};
        currentRenderTargetCount_ = 0;
        currentRenderTargetCube_ = target;
        currentCubeFace_ = face;
        viewportSet_ = false;
    }

    void WickedRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                  int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }

        if (renderTargets[0].IsRenderTargetCubeFace())
        {
            // A cube face is only ever bound alone: XNA has no MRT set mixing a cube face with
            // other targets, and flattening one into slot 0 would silently lose the face.
            if (count > 1)
            {
                throw std::runtime_error(
                    "Wicked renderer: a RenderTargetCube face cannot be combined with other "
                    "render targets in one binding set.");
            }
            SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                    renderTargets[0].GetCubeFace());
            return;
        }

        if (count > kWickedMaxRenderTargets)
        {
            throw std::runtime_error(
                "Wicked renderer: at most " + std::to_string(kWickedMaxRenderTargets) +
                " simultaneous render targets are supported (" + std::to_string(count) +
                " were bound).");
        }

        std::array<WickedRenderTargetRenderer*, kWickedMaxRenderTargets> targets{};
        for (int slot = 0; slot < count; ++slot)
        {
            if (renderTargets[slot].IsRenderTargetCubeFace())
            {
                throw std::runtime_error(
                    "Wicked renderer: a RenderTargetCube face cannot appear in a multi-target "
                    "binding set.");
            }
            auto* target =
                dynamic_cast<WickedRenderTargetRenderer*>(renderTargets[slot].GetRenderTarget2D());
            if (target == nullptr)
            {
                throw std::runtime_error(
                    "Wicked renderer: render-target slot " + std::to_string(slot) +
                    " is empty; XNA's binding set has no holes.");
            }
            targets[slot] = target;
        }
        // Every attachment of one render pass must share an extent, so a mismatched set is refused
        // rather than rendered into a region only the first target actually covers.
        for (int slot = 1; slot < count; ++slot)
        {
            if (targets[slot]->GetWidth() != targets[0]->GetWidth() ||
                targets[slot]->GetHeight() != targets[0]->GetHeight())
            {
                throw std::runtime_error(
                    "Wicked renderer: every render target in one binding set must have the same "
                    "size; slot " + std::to_string(slot) + " differs from slot 0.");
            }
        }

        if (count == 1)
        {
            SetRenderTarget2D(targets[0]);
            return;
        }

        EndRenderPassEXT();
        currentRenderTargets_ = targets;
        currentRenderTargetCount_ = count;
        currentRenderTargetCube_ = nullptr;
        viewportSet_ = false;
    }

    void WickedRenderer::NotifyRenderTargetDestroyedEXT(const WickedRenderTargetRenderer* rt)
    {
        for (int slot = 0; slot < currentRenderTargetCount_; ++slot)
        {
            if (currentRenderTargets_[slot] == rt)
            {
                EndRenderPassEXT();
                currentRenderTargets_ = {};
                currentRenderTargetCount_ = 0;
                break;
            }
        }
    }

    void WickedRenderer::NotifyRenderTargetCubeDestroyedEXT(
        const WickedRenderTargetCubeRenderer* rt)
    {
        if (currentRenderTargetCube_ == rt)
        {
            EndRenderPassEXT();
            currentRenderTargetCube_ = nullptr;
        }
    }

    // ---- state ----

    void WickedRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& writeState)
    {
        state_.colorSrcBlend = colorSrcBlend;
        state_.alphaSrcBlend = alphaSrcBlend;
        state_.colorDstBlend = colorDstBlend;
        state_.alphaDstBlend = alphaDstBlend;
        state_.colorBlendFunc = colorBlendFunc;
        state_.alphaBlendFunc = alphaBlendFunc;
        for (int slot = 0; slot < 4; ++slot)
            state_.colorWriteChannels[slot] = writeState.colorWriteChannels[slot];
        state_.multiSampleMask = writeState.multiSampleMask;
        state_.blendEnabled = 1;
    }

    void WickedRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail,
                                                        int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask,
                                                        int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        state_.depthEnable = depthEnable ? 1u : 0u;
        state_.depthWriteEnable = depthWriteEnable ? 1u : 0u;
        state_.depthFunc = depthFunc;
        state_.stencilEnable = stencilEnable ? 1u : 0u;
        state_.stencilFunc = stencilFunc;
        state_.stencilPass = stencilPass;
        state_.stencilFail = stencilFail;
        state_.stencilDepthFail = stencilDepthFail;
        state_.stencilMask = stencilMask;
        state_.stencilWriteMask = stencilWriteMask;
        state_.twoSidedStencil = twoSidedStencilMode ? 1u : 0u;
        state_.ccwStencilFunc = ccwStencilFunc;
        state_.ccwStencilPass = ccwStencilPass;
        state_.ccwStencilFail = ccwStencilFail;
        state_.ccwStencilDepthFail = ccwStencilDepthFail;
        referenceStencil_ = referenceStencil;
    }

    void WickedRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        state_.cullMode = cullMode;
        state_.fillMode = fillMode;
        state_.depthBias = depthBias;
        state_.slopeScaleDepthBias = slopeScaleDepthBias;
        if (scissorEnabled_ != scissorTestEnable)
        {
            scissorEnabled_ = scissorTestEnable;
            ApplyViewportAndScissor();
        }
    }

    void WickedRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= samplerStates_.size())
            return;
        SamplerSlotState& state = samplerStates_[static_cast<std::size_t>(slot)];
        state.filter = filter;
        state.addressU = addressU;
        state.addressV = addressV;
        state.maxAnisotropy = maxAnisotropy;
    }

    void WickedRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void WickedRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void WickedRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        ApplyViewportAndScissor();
    }

    void WickedRenderer::SetViewport(int x, int y, int w, int h,
                                             float minDepth, float maxDepth)
    {
        viewportSet_ = true;
        viewport_[0] = static_cast<float>(x);
        viewport_[1] = static_cast<float>(y);
        viewport_[2] = static_cast<float>(w);
        viewport_[3] = static_cast<float>(h);
        viewport_[4] = minDepth;
        viewport_[5] = maxDepth;
        ApplyViewportAndScissor();
    }

    void WickedRenderer::SetDepthTestEnabled(bool enabled)
    {
        state_.depthEnable = enabled ? 1u : 0u;
    }

    void WickedRenderer::SetBlendEnabled(bool enabled)
    {
        state_.blendEnabled = enabled ? 1u : 0u;
    }

    void WickedRenderer::SetDepthWriteEnabled(bool enabled)
    {
        state_.depthWriteEnable = enabled ? 1u : 0u;
    }

    // ---- draws ----

    void WickedRenderer::SubmitDraw(const IVertexBufferRenderer& vb,
                                           const IIndexBufferRenderer* ib,
                                           const Matrix& world, const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive, int primitiveCount,
                                           const GpuDrawParams* params, int instanceCount)
    {
        if (primitiveCount <= 0 || instanceCount <= 0)
            return;

        // REMED-GFX-DECL-GUARD, at draw time and before anything native is touched: this route
        // dispatches on byte stride, so a declaration the stride table would reinterpret must be
        // refused rather than silently rendered from the wrong offsets.
        RequireFaithfulDeclarationEXT(vb, instanceCount > 1 ? "DrawInstancedPrimitivesEx"
                                                            : "DrawPrimitives");

        const auto& vertexBuffer = static_cast<const WickedVertexBufferRenderer&>(vb);
        std::size_t stride = vertexBuffer.GetStrideEXT();
        if (params != nullptr)
            stride = CombinedVertexStrideOr(*params, stride);
        if (stride == 0)
            throw std::runtime_error("Wicked renderer: the vertex buffer has no known stride.");

        const bool instanced = instanceCount > 1;
        const GpuVertexStreamBinding* instanceStream = nullptr;
        if (params != nullptr)
        {
            // REMED-GFX-202: several per-instance streams remain unexpressible -- the instanced
            // vertex programs declare exactly one instance record -- so that shape is still
            // refused rather than rendered from the first of them. Several PER-VERTEX streams are
            // supported (WICKED-58) and handled below.
            if (HasMultipleInstanceStreams(*params))
            {
                throw std::runtime_error(
                    "Wicked renderer: only one per-instance VertexBufferBinding is supported (" +
                    std::to_string(InstanceStreamCount(*params)) + " were bound).");
            }
            instanceStream = FirstInstanceStream(*params);
        }

        if (instanced)
        {
            if (instanceStream == nullptr || instanceStream->buffer == nullptr)
            {
                throw std::runtime_error(
                    "Wicked renderer: an instanced draw needs a VertexBufferBinding whose "
                    "InstanceFrequency is greater than zero; none was bound.");
            }
            // Wicked Engine's InputLayout has no instance-step-rate field, so a frequency other
            // than 1 cannot be expressed natively. Refusing is the honest answer: silently
            // treating it as 1 would draw every instance from the wrong record.
            if (instanceStream->instanceFrequency != 1)
            {
                throw std::runtime_error(
                    "Wicked renderer: only InstanceFrequency == 1 is supported (requested " +
                    std::to_string(instanceStream->instanceFrequency) + ").");
            }
            if (VariantForStride(stride) >=
                static_cast<WickedShaderVariant>(kWickedInstancedVariantCount))
            {
                throw std::runtime_error(
                    "Wicked renderer: instancing is available only for the stride-16/20/24/32 "
                    "layouts (stride " + std::to_string(stride) + " was bound).");
            }
            if (instanceStream->strideInBytes != kInstanceMatrixStride)
            {
                throw std::runtime_error(
                    "Wicked renderer: the per-instance stream must be a 64-byte Matrix (stride " +
                    std::to_string(instanceStream->strideInBytes) + " was bound).");
            }
        }

        WickedPipelineKey key = state_;
        key.variant = static_cast<std::uint32_t>(VariantForStride(stride));
        key.instanced = instanced ? 1u : 0u;
        key.topology = static_cast<std::uint32_t>(ToWickedTopology(primitive));

        // WICKED-58: only a genuinely split declaration populates the stream table, so a
        // single-buffer draw keeps producing exactly the key it produced before this existed.
        if (params != nullptr && HasMultipleVertexStreams(*params))
        {
            int perVertexSlots = 0;
            for (int i = 0; i < params->vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params->vertexStreams[i];
                if (stream.instanceFrequency != 0)
                    continue;
                if (stream.slot < 0 || stream.slot >= kMaxVertexStreams)
                {
                    throw std::runtime_error(
                        "Wicked renderer: vertex stream slot " + std::to_string(stream.slot) +
                        " is outside the supported range.");
                }
                key.streamBase[stream.slot] = stream.combinedByteBase;
                key.streamStride[stream.slot] = stream.strideInBytes;
                ++perVertexSlots;
            }
            key.streamCount = static_cast<std::uint32_t>(params->vertexStreamCount);
            (void)perVertexSlots;
        }

        WickedShaderConstants constants;
        // The instanced entry points take the world transform per instance, so the constant buffer
        // carries VIEW * PROJECTION alone there; `world` still folds in for the lighting/fog space.
        WriteMatrixColumns(instanced ? view * projection : world * view * projection,
                           constants.mvp);
        WriteMatrixColumns(world, constants.world);

        const ITextureRenderer* texture0 = nullptr;
        const ITextureRenderer* texture1 = nullptr;
        if (params != nullptr)
        {
            texture0 = params->texture0;
            texture1 = params->texture1;

            std::copy_n(params->diffuseColor, 4, constants.diffuse);
            std::copy_n(params->ambientColor, 3, constants.ambient);
            std::copy_n(params->emissiveColor, 3, constants.emissive);
            constants.emissive[3] = params->specularPower;
            std::copy_n(params->specularColor, 3, constants.specular);
            std::copy_n(params->alphaTest, 4, constants.alphaTest);
            std::copy_n(params->fogColor, 3, constants.fogColor);
            constants.fogColor[3] = params->fogEnabled ? 1.0f : 0.0f;
            std::copy_n(params->fogVector, 4, constants.fogVector);
            std::copy_n(params->light0Dir, 3, constants.lightDir[0]);
            std::copy_n(params->light1Dir, 3, constants.lightDir[1]);
            std::copy_n(params->light2Dir, 3, constants.lightDir[2]);
            std::copy_n(params->light0Diffuse, 3, constants.lightDiffuse[0]);
            std::copy_n(params->light1Diffuse, 3, constants.lightDiffuse[1]);
            std::copy_n(params->light2Diffuse, 3, constants.lightDiffuse[2]);
            std::copy_n(params->light0Specular, 3, constants.lightSpecular[0]);
            std::copy_n(params->light1Specular, 3, constants.lightSpecular[1]);
            std::copy_n(params->light2Specular, 3, constants.lightSpecular[2]);
            std::copy_n(params->eyePositionWorld, 3, constants.eyePosition);
            constants.flags[0] = params->textureEnabled && texture0 != nullptr ? 1.0f : 0.0f;
            constants.flags[1] = params->vertexColorEnabled ? 1.0f : 0.0f;
            constants.flags[2] = params->lightingEnabled ? 1.0f : 0.0f;
            constants.flags[3] = params->dualTexture && texture1 != nullptr ? 1.0f : 0.0f;

            if (params->envMapping)
            {
                if (instanced)
                {
                    throw std::runtime_error(
                        "Wicked renderer: EnvironmentMapEffect has no instanced shader variant.");
                }
                if (stride != 32)
                {
                    throw std::runtime_error(
                        "Wicked renderer: EnvironmentMapEffect needs the stride-32 "
                        "VertexPositionNormalTexture layout (stride " +
                        std::to_string(stride) + " was bound).");
                }
                key.envMap = 1;
                // The normal matrix is computed here rather than in the shader so a non-uniform
                // scale is handled exactly; HLSL has no matrix inverse.
                WriteMatrixColumns(Matrix::Transpose(Matrix::Invert(world)),
                                   constants.worldInverseTranspose);
                constants.envMapParams[0] = params->envMapAmount;
                constants.envMapParams[1] = params->fresnelEnabled ? 1.0f : 0.0f;
                constants.envMapParams[2] = params->fresnelFactor;
                std::copy_n(params->envMapSpecular, 3, constants.envMapSpecular);
            }

            if (params->skinned)
            {
                const auto variant = static_cast<std::size_t>(VariantForStride(stride));
                if (variant < kWickedFirstSkinnableVariant)
                {
                    throw std::runtime_error(
                        "Wicked renderer: SkinnedEffect needs a vertex layout carrying blend "
                        "weights and indices (stride 52, 56 or 68); stride " +
                        std::to_string(stride) + " was bound.");
                }
                if (instanced)
                {
                    throw std::runtime_error(
                        "Wicked renderer: SkinnedEffect has no instanced shader variant.");
                }
                if (params->boneCount <= 0)
                {
                    throw std::runtime_error(
                        "Wicked renderer: SkinnedEffect was selected but no bone transforms were "
                        "supplied.");
                }
                key.skinned = 1;
                // The world inverse-transpose composes with the per-bone rotation in the vertex
                // shader, so it is needed here exactly as it is for the env-map program.
                WriteMatrixColumns(Matrix::Transpose(Matrix::Invert(world)),
                                   constants.worldInverseTranspose);
            }

            if (params->pbr)
            {
                if (stride != 48 && stride != 68)
                {
                    throw std::runtime_error(
                        "Wicked renderer: PbrEffect needs a vertex layout carrying a tangent "
                        "(stride 48 or 68); stride " + std::to_string(stride) + " was bound.");
                }
                if (params->skinned && stride != 68)
                {
                    throw std::runtime_error(
                        "Wicked renderer: SkinnedPbrEffect needs the stride-68 layout; stride " +
                        std::to_string(stride) + " was bound.");
                }
                if (instanced)
                {
                    throw std::runtime_error(
                        "Wicked renderer: PbrEffect has no instanced shader variant.");
                }
                key.pbr = 1;
                // The PBR vertex stage needs the normal matrix whether or not the draw is skinned,
                // so it is written here too rather than only on the skinned branch above.
                WriteMatrixColumns(Matrix::Transpose(Matrix::Invert(world)),
                                   constants.worldInverseTranspose);
                constants.pbrFactors[0] = params->pbrMetallicFactor;
                constants.pbrFactors[1] = params->pbrRoughnessFactor;
                constants.pbrFactors[2] = params->pbrNormalScale;
                constants.pbrFactors[3] = params->pbrOcclusionStrength;
                constants.pbrSrgb[0] = params->pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;
                constants.pbrSrgb[1] = params->pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;
                constants.pbrSrgb[2] = params->pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
                constants.pbrDielectricFresnel[0] = params->pbrDielectricF0[0];
                constants.pbrDielectricFresnel[1] = params->pbrDielectricF0[1];
                constants.pbrDielectricFresnel[2] = params->pbrDielectricF0[2];
                constants.pbrDielectricFresnel[3] = params->pbrDielectricF90;
                std::memcpy(constants.pbrTextureTransformRows,
                            params->pbrTextureTransformRows,
                            sizeof(params->pbrTextureTransformRows));
            }
        }
        else
        {
            constants.flags[1] = 1.0f; // DrawColoredPrimitives is the vertex-colour route.
        }

        wig::CommandList cmd = GetCommandListEXT();
        EnsureRenderPass();
        BindCommonState(cmd, key, constants);

        if (key.skinned != 0 && params != nullptr)
        {
            WickedBoneConstants boneConstants;
            const int boneCount = std::min(params->boneCount, 72);
            std::copy_n(params->boneTransforms, static_cast<std::size_t>(boneCount) * 16,
                        boneConstants.bones);
            boneConstants.skinParams[0] = static_cast<float>(params->weightsPerVertex);
            device_->BindDynamicConstantBuffer(boneConstants, 1, cmd);
        }

        const auto resolveTexture = [&](const ITextureRenderer* texture) -> const wig::Texture*
        {
            if (const auto* wicked = dynamic_cast<const WickedTextureRenderer*>(texture))
                return &wicked->GetTextureEXT();
            if (const auto* target = dynamic_cast<const WickedRenderTargetRenderer*>(texture))
                return &target->GetSampleableTextureEXT();
            return &whiteTexture_;
        };
        device_->BindResource(resolveTexture(texture0), 0, cmd);
        device_->BindResource(resolveTexture(texture1), 1, cmd);

        const wig::Texture* environmentMap = &whiteCubeTexture_;
        if (params != nullptr)
        {
            if (const auto* cube =
                    dynamic_cast<const WickedTextureCubeRenderer*>(params->envMap))
            {
                environmentMap = &cube->GetTextureEXT();
            }
            else if (const auto* renderedCube =
                         dynamic_cast<const WickedRenderTargetCubeRenderer*>(params->envMap))
            {
                environmentMap = &renderedCube->GetColorTextureEXT();
            }
        }
        device_->BindResource(environmentMap, 2, cmd);

        // The PBR sampler slots always resolve to a real texture. The neutral default differs per
        // slot: a flat normal for t3, and white for the rest so the metallic/roughness/emissive
        // factors act alone and occlusion leaves the ambient term untouched.
        const wig::Texture* normalMap = &flatNormalTexture_;
        const wig::Texture* metallicRoughnessMap = &whiteTexture_;
        const wig::Texture* emissiveMap = &whiteTexture_;
        const wig::Texture* occlusionMap = &whiteTexture_;
        if (params != nullptr && key.pbr != 0)
        {
            if (params->pbrNormalMap != nullptr)
                normalMap = resolveTexture(params->pbrNormalMap);
            if (params->pbrMetallicRoughnessMap != nullptr)
                metallicRoughnessMap = resolveTexture(params->pbrMetallicRoughnessMap);
            if (params->pbrEmissiveMap != nullptr)
                emissiveMap = resolveTexture(params->pbrEmissiveMap);
            if (params->pbrOcclusionMap != nullptr)
                occlusionMap = resolveTexture(params->pbrOcclusionMap);
        }
        device_->BindResource(normalMap, 3, cmd);
        device_->BindResource(metallicRoughnessMap, 4, cmd);
        device_->BindResource(emissiveMap, 5, cmd);
        device_->BindResource(occlusionMap, 6, cmd);
        for (std::uint32_t slot = 0; slot < 2; ++slot)
        {
            const SamplerSlotState& sampler = samplerStates_[slot];
            device_->BindSampler(GetSampler(sampler.filter, sampler.addressU, sampler.addressV,
                                            sampler.maxAnisotropy), slot, cmd);
        }

        if (!vertexBuffer.GetBufferEXT().IsValid())
        {
            throw std::runtime_error(
                "Wicked renderer: the vertex buffer has no storage yet; SetData must run before a "
                "draw that reads it.");
        }

        const wig::GPUBuffer* buffers[kMaxVertexStreams] = {};
        std::uint32_t strides[kMaxVertexStreams] = {};
        // The buffer's own region offset is added to every binding: an UPLOAD buffer is split into
        // regions so a write cannot land on memory a queued draw still reads (WICKED-32).
        std::uint64_t offsets[kMaxVertexStreams] = {};
        std::uint32_t boundStreams = 0;

        const auto bindStream = [&](int slot, const IVertexBufferRenderer* rendererBuffer,
                                    std::uint32_t slotStride, std::uint64_t elementOffset)
        {
            const auto* wicked = static_cast<const WickedVertexBufferRenderer*>(rendererBuffer);
            if (wicked == nullptr || !wicked->GetBufferEXT().IsValid())
            {
                throw std::runtime_error(
                    "Wicked renderer: vertex stream " + std::to_string(slot) +
                    " has no storage yet; SetData must run before a draw that reads it.");
            }
            buffers[slot] = &wicked->GetBufferEXT();
            strides[slot] = slotStride;
            offsets[slot] = wicked->GetByteOffsetEXT() + elementOffset;
            boundStreams = std::max(boundStreams, static_cast<std::uint32_t>(slot) + 1u);
        };

        if (key.streamCount > 0)
        {
            for (int i = 0; i < params->vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& s = params->vertexStreams[i];
                bindStream(s.slot, s.buffer, static_cast<std::uint32_t>(s.strideInBytes),
                           VertexStreamByteOffset(s));
            }
        }
        else
        {
            buffers[0] = &vertexBuffer.GetBufferEXT();
            strides[0] = static_cast<std::uint32_t>(stride);
            offsets[0] = vertexBuffer.GetByteOffsetEXT();
            // WICKED-77: the instanced route does not fold the geometry stream's public
            // VertexOffset into baseVertex -- GraphicsDevice::DrawInstancedPrimitives fills the
            // stream table with foldedOffset = 0, so each stream carries its whole offset and the
            // renderer owes it at bind time. The ordinary routes fold it into baseVertex/vertexStart
            // and leave this stream's own offset at 0, so adding it is exact on every route.
            if (params != nullptr)
            {
                if (const GpuVertexStreamBinding* geometry = FirstPerVertexStream(*params))
                    offsets[0] += VertexStreamByteOffset(*geometry);
            }
            boundStreams = 1;
            if (instanced)
            {
                // VertexOffset is in vertex ELEMENTS, so the byte offset is its own stride's
                // multiple.
                bindStream(instanceStream->slot, instanceStream->buffer, kInstanceMatrixStride,
                           VertexStreamByteOffset(*instanceStream));
            }
        }
        device_->BindVertexBuffers(buffers, 0, boundStreams, strides, offsets, cmd);

        const int elementCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (ib != nullptr)
        {
            const auto& indexBuffer = static_cast<const WickedIndexBufferRenderer&>(*ib);
            const wig::IndexBufferFormat format = indexBuffer.IsThirtyTwoBit()
                ? wig::IndexBufferFormat::UINT32
                : wig::IndexBufferFormat::UINT16;
            device_->BindIndexBuffer(&indexBuffer.GetBufferEXT(), format,
                                     indexBuffer.GetByteOffsetEXT(), cmd);
            const std::uint32_t startIndex =
                params != nullptr ? static_cast<std::uint32_t>(params->startIndex) : 0u;
            const std::int32_t baseVertex =
                params != nullptr ? static_cast<std::int32_t>(params->baseVertex) : 0;
            if (instanced)
            {
                device_->DrawIndexedInstanced(static_cast<std::uint32_t>(elementCount),
                                              static_cast<std::uint32_t>(instanceCount),
                                              startIndex, baseVertex, 0, cmd);
            }
            else
            {
                device_->DrawIndexed(static_cast<std::uint32_t>(elementCount), startIndex,
                                     baseVertex, cmd);
            }
        }
        else
        {
            const std::uint32_t startVertex =
                params != nullptr ? static_cast<std::uint32_t>(params->vertexStart) : 0u;
            if (instanced)
            {
                device_->DrawInstanced(static_cast<std::uint32_t>(elementCount),
                                       static_cast<std::uint32_t>(instanceCount),
                                       startVertex, 0, cmd);
            }
            else
            {
                device_->Draw(static_cast<std::uint32_t>(elementCount), startVertex, cmd);
            }
        }
    }

    void WickedRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                           const IIndexBufferRenderer& ib,
                                                           const Matrix& world, const Matrix& view,
                                                           const Matrix& projection,
                                                           PrimitiveType primitive,
                                                           int primitiveCount,
                                                           int instanceCount,
                                                           const GpuDrawParams& params)
    {
        SubmitDraw(vb, &ib, world, view, projection, primitive, primitiveCount, &params,
                   instanceCount);
    }

    void WickedRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        SubmitDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void WickedRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                              const IIndexBufferRenderer& ib,
                                                              const Matrix& world,
                                                              const Matrix& view,
                                                              const Matrix& projection,
                                                              PrimitiveType primitive,
                                                              int primitiveCount)
    {
        SubmitDraw(vb, &ib, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void WickedRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                  const Matrix& world, const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        SubmitDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
    }

    void WickedRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                         const IIndexBufferRenderer& ib,
                                                         const Matrix& world, const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive,
                                                         int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        SubmitDraw(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
    }

    void WickedRenderer::DrawSpriteQuadsEXT(const void* vertices, int vertexCount,
                                                    const ITextureRenderer& texture,
                                                    const Matrix* transform,
                                                    int filter, int addressU, int addressV)
    {
        if (vertices == nullptr || vertexCount <= 0)
            return;

        wig::CommandList cmd = GetCommandListEXT();
        EnsureRenderPass();

        WickedPipelineKey key = state_;
        key.variant = static_cast<std::uint32_t>(WickedShaderVariant::Basic24);
        key.topology = static_cast<std::uint32_t>(wig::PrimitiveTopology::TRIANGLELIST);
        // Sprites are 2D: depth testing must not reject them, and they must not write depth.
        key.depthEnable = 0;
        key.depthWriteEnable = 0;
        key.cullMode = 0;

        const float width = static_cast<float>(std::max(1, ActiveTargetWidth()));
        const float height = static_cast<float>(std::max(1, ActiveTargetHeight()));
        // FNA's own SpriteBatch projection: near 0, far -1, so a layer depth in [0,1] reaches
        // clip space unchanged.
        Matrix projection = OrthographicOffCenter(0.0f, width, height, 0.0f, 0.0f, -1.0f);
        if (transform != nullptr)
            projection = (*transform) * projection;

        WickedShaderConstants constants;
        WriteMatrixColumns(projection, constants.mvp);
        constants.world[0] = 1.0f;  constants.world[5] = 1.0f;
        constants.world[10] = 1.0f; constants.world[15] = 1.0f;
        constants.flags[0] = 1.0f;
        constants.flags[1] = 1.0f;

        BindCommonState(cmd, key, constants);

        const std::size_t byteCount = static_cast<std::size_t>(vertexCount) * 24u;
        const wig::GraphicsDevice::GPUAllocation allocation = device_->AllocateGPU(byteCount, cmd);
        std::memcpy(allocation.data, vertices, byteCount);
        const wig::GPUBuffer* buffers[] = {&allocation.buffer};
        const std::uint32_t strides[] = {24u};
        const std::uint64_t offsets[] = {allocation.offset};
        device_->BindVertexBuffers(buffers, 0, 1, strides, offsets, cmd);

        const wig::Texture* bound = &whiteTexture_;
        if (const auto* wicked = dynamic_cast<const WickedTextureRenderer*>(&texture))
            bound = &wicked->GetTextureEXT();
        else if (const auto* target = dynamic_cast<const WickedRenderTargetRenderer*>(&texture))
            bound = &target->GetSampleableTextureEXT();
        device_->BindResource(bound, 0, cmd);
        device_->BindResource(&whiteTexture_, 1, cmd);
        device_->BindSampler(GetSampler(filter, addressU, addressV, 1), 0, cmd);

        device_->Draw(static_cast<std::uint32_t>(vertexCount), 0, cmd);
    }

    // ---- diagnostics / capabilities ----

    void WickedRenderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker == nullptr || !frameActive_)
            return;
        device_->SetMarker(marker, cmd_);
    }

    std::string WickedRenderer::GetAdapterNameEXT() const
    {
        return device_ ? device_->GetAdapterName() : std::string();
    }

    wig::ShaderFormat WickedRenderer::GetShaderFormatEXT() const
    {
        return device_ ? device_->GetShaderFormat() : wig::ShaderFormat::NONE;
    }

    bool WickedRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // Exhaustive over CNA::GraphicsCapability with NO default arm, deliberately: a capability
        // added after this renderer was written must surface as a -Wswitch diagnostic, never as a
        // catch-all answer. A permissive `default: return true` is what previously made another
        // renderer claim Texture3D it did not have; a `default: return false` is the mirror hazard
        // and is what would have answered "no" to the real instancing route below.
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::WireFrame:
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::StencilBuffer:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return true;
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return true;
            // Real volume-texture storage: CreateTexture3D() returns a genuine GPU resource whose
            // SetData/GetData persist and retrieve voxels, which is exactly what this capability
            // asks about.
            case CNA::GraphicsCapability::Texture3D:
                return true;
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return true;
            // WICKED-58: a VertexDeclaration split across several per-vertex buffers is re-slotted
            // into its own input layout. Several PER-INSTANCE streams remain unsupported and are
            // refused at the draw, as the interface requires.
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                return true;
            // WICKED-53: four instanced vertex entry points read a per-instance 64-byte
            // column-major Matrix from input slot 1. The narrower contract this renderer does not
            // meet -- an InstanceFrequency other than 1, and instancing on the wide
            // tangent/skinned strides -- is refused at the draw, which is a boundary inside a
            // supported capability rather than an absent one.
            case CNA::GraphicsCapability::Instancing:
                return true;
            // WICKED-57/68: IEffectRenderer addresses shader constants BY NAME, which needs SPIR-V
            // reflection this renderer does not do. Refused explicitly at the call site.
            case CNA::GraphicsCapability::CustomEffects:
                return false;
        }
        return false;
    }

    int WickedRenderer::GetMaxTextureDimension() const
    {
        return 16384;
    }

    int WickedRenderer::GetMaxVertexStreams() const
    {
        return kMaxVertexStreams;
    }

#ifdef CNA_RENDERER_WICKED
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRendererImpl(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<WickedRenderer>(args);
    }
#endif
}

#ifdef CNA_RENDERER_WICKED
namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return CNA::Internal::Renderers::Wicked::CreateGraphicsRendererImpl(args);
    }
}
#endif
