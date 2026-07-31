// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Sokol/SokolGraphicsBackend.hpp"

#include "CNA/Internal/Backends/Common/NotYetImplemented.hpp"

#include "sokol_log.h"
#include "sokol_gfx.h"

#include "CNA/Internal/Backends/Sokol/shaders/sokol_shaders.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

// The GL back-buffer readback below calls glReadPixels directly: sokol_gfx has no readback API of
// its own, and on this project's target platform (SOKOL_GLCORE on Linux) sokol renders into the
// SDL window's default framebuffer, which glReadPixels can read straight out of. Restricted to the
// GL APIs on purpose -- ReadBackbuffer refuses any other CNA_SOKOL_API rather than pretending.
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    #define CNA_SOKOL_HAS_GL_READBACK 1
    #if defined(SOKOL_GLCORE)
        #define GL_GLEXT_PROTOTYPES
        #include <GL/gl.h>
    #else
        #include <GLES3/gl3.h>
    #endif
#else
    #define CNA_SOKOL_HAS_GL_READBACK 0
#endif

namespace CNA::Internal::Backends::Sokol
{
    namespace
    {
        constexpr const char* kBackendName = "Sokol";

        /// Per-frame sprite capacity. 16384 quads is exactly 65536 vertices, the largest run a
        /// uint16 index buffer can address, so this is the natural ceiling for the shared quad
        /// index buffer rather than an arbitrary number.
        constexpr int kMaxSpriteQuads = 16384;
        constexpr int kSpriteVerticesPerQuad = 4;
        constexpr int kSpriteIndicesPerQuad = 6;

        // --- XNA ordinal -> sokol_gfx enum translations -----------------------------------
        //
        // Every raw int crossing IGraphicsBackend is a Microsoft::Xna::Framework::Graphics enum
        // ordinal. These map the complete enum, not the subset the 2D path happens to reach, so
        // an unmapped value cannot silently collapse onto its neighbour (REMED-GFX-170).

        sg_blend_factor ToBlendFactor(int blend)
        {
            switch (blend)
            {
                case 0:  return SG_BLENDFACTOR_ONE;                        // Blend::One
                case 1:  return SG_BLENDFACTOR_ZERO;                       // Blend::Zero
                case 2:  return SG_BLENDFACTOR_SRC_COLOR;                  // SourceColor
                case 3:  return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;        // InverseSourceColor
                case 4:  return SG_BLENDFACTOR_SRC_ALPHA;                  // SourceAlpha
                case 5:  return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;        // InverseSourceAlpha
                case 6:  return SG_BLENDFACTOR_DST_COLOR;                  // DestinationColor
                case 7:  return SG_BLENDFACTOR_ONE_MINUS_DST_COLOR;        // InverseDestinationColor
                case 8:  return SG_BLENDFACTOR_DST_ALPHA;                  // DestinationAlpha
                case 9:  return SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA;        // InverseDestinationAlpha
                case 10: return SG_BLENDFACTOR_BLEND_COLOR;                // BlendFactor
                case 11: return SG_BLENDFACTOR_ONE_MINUS_BLEND_COLOR;      // InverseBlendFactor
                case 12: return SG_BLENDFACTOR_SRC_ALPHA_SATURATED;        // SourceAlphaSaturation
                default: return SG_BLENDFACTOR_ONE;
            }
        }

        sg_blend_op ToBlendOp(int blendFunction)
        {
            switch (blendFunction)
            {
                case 0:  return SG_BLENDOP_ADD;               // BlendFunction::Add
                case 1:  return SG_BLENDOP_SUBTRACT;          // Subtract
                case 2:  return SG_BLENDOP_REVERSE_SUBTRACT;  // ReverseSubtract
                case 3:  return SG_BLENDOP_MAX;               // Max
                case 4:  return SG_BLENDOP_MIN;               // Min
                default: return SG_BLENDOP_ADD;
            }
        }

        sg_compare_func ToCompareFunc(int compareFunction)
        {
            switch (compareFunction)
            {
                case 0:  return SG_COMPAREFUNC_ALWAYS;        // CompareFunction::Always
                case 1:  return SG_COMPAREFUNC_NEVER;         // Never
                case 2:  return SG_COMPAREFUNC_LESS;          // Less
                case 3:  return SG_COMPAREFUNC_LESS_EQUAL;    // LessEqual
                case 4:  return SG_COMPAREFUNC_EQUAL;         // Equal
                case 5:  return SG_COMPAREFUNC_GREATER_EQUAL; // GreaterEqual
                case 6:  return SG_COMPAREFUNC_GREATER;       // Greater
                case 7:  return SG_COMPAREFUNC_NOT_EQUAL;     // NotEqual
                default: return SG_COMPAREFUNC_LESS_EQUAL;
            }
        }

        sg_stencil_op ToStencilOp(int stencilOperation)
        {
            switch (stencilOperation)
            {
                case 0:  return SG_STENCILOP_KEEP;        // StencilOperation::Keep
                case 1:  return SG_STENCILOP_ZERO;        // Zero
                case 2:  return SG_STENCILOP_REPLACE;     // Replace
                case 3:  return SG_STENCILOP_INCR_WRAP;   // Increment
                case 4:  return SG_STENCILOP_DECR_WRAP;   // Decrement
                case 5:  return SG_STENCILOP_INCR_CLAMP;  // IncrementSaturation
                case 6:  return SG_STENCILOP_DECR_CLAMP;  // DecrementSaturation
                case 7:  return SG_STENCILOP_INVERT;      // Invert
                default: return SG_STENCILOP_KEEP;
            }
        }

        sg_color_mask ToColorMask(int colorWriteChannels)
        {
            // XNA's ColorWriteChannels bit layout (bit0=R, bit1=G, bit2=B, bit3=A) is identical to
            // sokol's SG_COLORMASK_* bit layout, so the ordinal maps straight through. The explicit
            // masking keeps a caller-supplied value outside 0..15 from producing an invalid enum.
            return static_cast<sg_color_mask>(colorWriteChannels & 0x0F);
        }

        sg_wrap ToWrap(int addressMode)
        {
            switch (addressMode)
            {
                case 0:  return SG_WRAP_REPEAT;          // TextureAddressMode::Wrap
                case 1:  return SG_WRAP_CLAMP_TO_EDGE;   // Clamp
                case 2:  return SG_WRAP_MIRRORED_REPEAT; // Mirror
                default: return SG_WRAP_CLAMP_TO_EDGE;
            }
        }

        /// Decomposes an XNA TextureFilter into sokol's three independent min/mag/mip filters.
        /// All nine XNA values are distinct combinations, so none of them may share a mapping.
        void ToFilters(int textureFilter, sg_filter& minFilter, sg_filter& magFilter,
                       sg_filter& mipFilter, bool& anisotropic)
        {
            anisotropic = false;
            switch (textureFilter)
            {
                case 1: // Point
                    minFilter = SG_FILTER_NEAREST; magFilter = SG_FILTER_NEAREST; mipFilter = SG_FILTER_NEAREST; return;
                case 2: // Anisotropic
                    minFilter = SG_FILTER_LINEAR; magFilter = SG_FILTER_LINEAR; mipFilter = SG_FILTER_LINEAR;
                    anisotropic = true; return;
                case 3: // LinearMipPoint
                    minFilter = SG_FILTER_LINEAR; magFilter = SG_FILTER_LINEAR; mipFilter = SG_FILTER_NEAREST; return;
                case 4: // PointMipLinear
                    minFilter = SG_FILTER_NEAREST; magFilter = SG_FILTER_NEAREST; mipFilter = SG_FILTER_LINEAR; return;
                case 5: // MinLinearMagPointMipLinear
                    minFilter = SG_FILTER_LINEAR; magFilter = SG_FILTER_NEAREST; mipFilter = SG_FILTER_LINEAR; return;
                case 6: // MinLinearMagPointMipPoint
                    minFilter = SG_FILTER_LINEAR; magFilter = SG_FILTER_NEAREST; mipFilter = SG_FILTER_NEAREST; return;
                case 7: // MinPointMagLinearMipLinear
                    minFilter = SG_FILTER_NEAREST; magFilter = SG_FILTER_LINEAR; mipFilter = SG_FILTER_LINEAR; return;
                case 8: // MinPointMagLinearMipPoint
                    minFilter = SG_FILTER_NEAREST; magFilter = SG_FILTER_LINEAR; mipFilter = SG_FILTER_NEAREST; return;
                case 0: // Linear
                default:
                    minFilter = SG_FILTER_LINEAR; magFilter = SG_FILTER_LINEAR; mipFilter = SG_FILTER_LINEAR; return;
            }
        }

        sg_primitive_type ToPrimitiveType(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return SG_PRIMITIVETYPE_TRIANGLES;
                case PrimitiveType::TriangleStrip: return SG_PRIMITIVETYPE_TRIANGLE_STRIP;
                case PrimitiveType::LineList:      return SG_PRIMITIVETYPE_LINES;
                case PrimitiveType::LineStrip:     return SG_PRIMITIVETYPE_LINE_STRIP;
                case PrimitiveType::PointListEXT:  return SG_PRIMITIVETYPE_POINTS;
            }
            return SG_PRIMITIVETYPE_TRIANGLES;
        }

        /// XNA counts primitives; every graphics API below counts the vertices/indices they use.
        int ElementCountForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
            }
            return primitiveCount * 3;
        }

        /// Maps an XNA CullMode onto sokol's, given that the pipeline below pins
        /// face_winding to SG_FACEWINDING_CW.
        ///
        /// XNA rasterizes with a top-left origin and Y growing downwards, and CNA's projection
        /// matrices preserve that, so a triangle wound clockwise on screen in XNA is also wound
        /// clockwise in clip space here -- which is exactly what SG_FACEWINDING_CW calls the front
        /// face. XNA's CullCounterClockwiseFace therefore removes sokol's BACK faces.
        ///
        /// Getting this backwards is invisible in a smoke test and catastrophic in a real scene:
        /// the first version of this function had the two swapped, and every triangle in the 3D
        /// test rendered as background until Sokol_3D's own culling check caught it.
        sg_cull_mode ToCullMode(int cullMode)
        {
            switch (cullMode)
            {
                case 1:  return SG_CULLMODE_FRONT;  // CullMode::CullClockwiseFace
                case 2:  return SG_CULLMODE_BACK;   // CullMode::CullCounterClockwiseFace
                case 0:                             // CullMode::None
                default: return SG_CULLMODE_NONE;
            }
        }

        /// Translates a VertexElementFormat ordinal. Returns SG_VERTEXFORMAT_INVALID for the
        /// formats sokol_gfx has no equivalent for, so the caller refuses rather than substituting
        /// a differently-sized attribute and silently misreading the whole vertex.
        sg_vertex_format ToVertexFormat(Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
        {
            switch (format)
            {
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Single:           return SG_VERTEXFORMAT_FLOAT;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector2:          return SG_VERTEXFORMAT_FLOAT2;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector3:          return SG_VERTEXFORMAT_FLOAT3;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4:          return SG_VERTEXFORMAT_FLOAT4;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Color:            return SG_VERTEXFORMAT_UBYTE4N;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Byte4:            return SG_VERTEXFORMAT_UBYTE4;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Short2:           return SG_VERTEXFORMAT_SHORT2;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::Short4:           return SG_VERTEXFORMAT_SHORT4;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::NormalizedShort2: return SG_VERTEXFORMAT_SHORT2N;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::NormalizedShort4: return SG_VERTEXFORMAT_SHORT4N;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::HalfVector2:      return SG_VERTEXFORMAT_HALF2;
                case Microsoft::Xna::Framework::Graphics::VertexElementFormat::HalfVector4:      return SG_VERTEXFORMAT_HALF4;
            }
            return SG_VERTEXFORMAT_INVALID;
        }

        sg_buffer MakeBufferHandle(std::uint32_t id) { sg_buffer handle; handle.id = id; return handle; }
        sg_image MakeImageHandle(std::uint32_t id) { sg_image handle; handle.id = id; return handle; }
        sg_view MakeViewHandle(std::uint32_t id) { sg_view handle; handle.id = id; return handle; }
        sg_sampler MakeSamplerHandle(std::uint32_t id) { sg_sampler handle; handle.id = id; return handle; }
        sg_shader MakeShaderHandle(std::uint32_t id) { sg_shader handle; handle.id = id; return handle; }
        sg_pipeline MakePipelineHandle(std::uint32_t id) { sg_pipeline handle; handle.id = id; return handle; }

        int MipDimension(int base, int level)
        {
            int value = base >> level;
            return value > 0 ? value : 1;
        }

        /// Resolves the sokol_gfx texture-view id for a texture-sampling draw, accepting either a
        /// plain SokolTextureBackend or a SokolRenderTargetBackend sampled as a texture (a target
        /// rendered to in an earlier pass) -- the two are unrelated class hierarchies (a render
        /// target's ITextureBackend half has no CPU-side pixel data at all), so this tries both
        /// rather than assuming one.
        std::uint32_t ResolveSampledTextureViewId(const ITextureBackend& texture, const char* context)
        {
            if (const auto* plain = dynamic_cast<const SokolTextureBackend*>(&texture))
                return plain->GetViewIdEXT();
            if (const auto* renderTarget = dynamic_cast<const SokolRenderTargetBackend*>(&texture))
                return renderTarget->GetColorTextureViewIdEXT();
            throw std::runtime_error(
                std::string("Sokol backend: ") + context + " was handed a foreign texture backend");
        }
    }

    // ---------------------------------------------------------------------------------------
    // SokolTextureBackend
    // ---------------------------------------------------------------------------------------

    SokolTextureBackend::SokolTextureBackend(const ImageData& data)
        : width_(data.width)
        , height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Sokol backend: texture dimensions must be positive");
        if (mipLevels_ > SG_MAX_MIPMAPS)
            mipLevels_ = SG_MAX_MIPMAPS;

        levels_.resize(static_cast<std::size_t>(mipLevels_));
        for (int level = 0; level < mipLevels_; ++level)
        {
            const std::size_t bytes = static_cast<std::size_t>(MipDimension(width_, level))
                                    * static_cast<std::size_t>(MipDimension(height_, level)) * 4u;
            levels_[static_cast<std::size_t>(level)].assign(bytes, 0u);
        }

        const std::size_t level0Bytes = levels_[0].size();
        if (!data.pixels.empty())
            std::memcpy(levels_[0].data(), data.pixels.data(), std::min(level0Bytes, data.pixels.size()));

        RecreateImage();
    }

    SokolTextureBackend::~SokolTextureBackend()
    {
        DestroyImage();
    }

    void SokolTextureBackend::DestroyImage()
    {
        if (viewId_ != 0)
        {
            sg_destroy_view(MakeViewHandle(viewId_));
            viewId_ = 0;
        }
        if (imageId_ != 0)
        {
            sg_destroy_image(MakeImageHandle(imageId_));
            imageId_ = 0;
        }
    }

    void SokolTextureBackend::RecreateImage()
    {
        // Recreated as immutable rather than updated in place: sg_update_image() is limited to one
        // call per image per frame, which a caller writing several mip levels in one frame (the
        // XNB texture loader does exactly that) violates immediately, while creating an immutable
        // image with initial data has no such limit. Every level comes from the CPU shadow, since
        // sokol_gfx replaces an image in full and has no sub-image upload.
        DestroyImage();

        sg_image_desc desc = {};
        desc.type = SG_IMAGETYPE_2D;
        desc.usage.immutable = true;
        desc.width = width_;
        desc.height = height_;
        desc.num_mipmaps = mipLevels_;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.label = "cna_texture2d";
        for (int level = 0; level < mipLevels_; ++level)
        {
            desc.data.mip_levels[level].ptr = levels_[static_cast<std::size_t>(level)].data();
            desc.data.mip_levels[level].size = levels_[static_cast<std::size_t>(level)].size();
        }
        imageId_ = sg_make_image(&desc).id;
        if (sg_query_image_state(MakeImageHandle(imageId_)) != SG_RESOURCESTATE_VALID)
        {
            imageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_image failed for a Texture2D");
        }

        sg_view_desc viewDesc = {};
        viewDesc.texture.image = MakeImageHandle(imageId_);
        viewDesc.label = "cna_texture2d_view";
        viewId_ = sg_make_view(&viewDesc).id;
        if (sg_query_view_state(MakeViewHandle(viewId_)) != SG_RESOURCESTATE_VALID)
        {
            viewId_ = 0;
            DestroyImage();
            throw std::runtime_error("Sokol backend: sg_make_view failed for a Texture2D");
        }
    }

    int SokolTextureBackend::GetWidth() const { return width_; }

    int SokolTextureBackend::GetHeight() const { return height_; }

    SDL_Texture* SokolTextureBackend::GetNativeTexture() const { return nullptr; }

    void SokolTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr) return;

        const int rowBytes = width_ * 4;
        auto& level0 = levels_[0];
        if (stride <= 0 || stride == rowBytes)
        {
            std::memcpy(level0.data(), rgba, level0.size());
        }
        else
        {
            for (int row = 0; row < height_; ++row)
            {
                std::memcpy(level0.data() + static_cast<std::size_t>(row) * rowBytes,
                            rgba + static_cast<std::size_t>(row) * stride,
                            static_cast<std::size_t>(rowBytes));
            }
        }
        RecreateImage();
    }

    void SokolTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= mipLevels_) return;

        auto& target = levels_[static_cast<std::size_t>(level)];
        const std::size_t supplied = static_cast<std::size_t>(std::max(levelW, 0))
                                   * static_cast<std::size_t>(std::max(levelH, 0)) * 4u;
        std::memcpy(target.data(), rgba, std::min(target.size(), supplied));
        RecreateImage();
    }

    bool SokolTextureBackend::GetData(int level, int x, int y, int w, int h,
                                      void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= mipLevels_) return false;
        if (w <= 0 || h <= 0) return false;

        const int levelW = MipDimension(width_, level);
        const int levelH = MipDimension(height_, level);
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH) return false;
        if (dataLength < w * h * 4) return false;

        const auto& source = levels_[static_cast<std::size_t>(level)];
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(levelW)
                 + static_cast<std::size_t>(x)) * 4u;
            std::memcpy(destination + static_cast<std::size_t>(row) * w * 4u,
                        source.data() + sourceOffset,
                        static_cast<std::size_t>(w) * 4u);
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // SokolTextureCubeBackend
    // ---------------------------------------------------------------------------------------

    namespace
    {
        int CalculateCubeMipLevels(int size)
        {
            int levels = 1;
            int s = size;
            while (s > 1) { s = std::max(1, s / 2); ++levels; }
            return levels;
        }
    }

    int SokolTextureCubeBackend::LevelDim(int level) const
    {
        return std::max(1, size_ >> level);
    }

    SokolTextureCubeBackend::SokolTextureCubeBackend(int size, bool mipMap)
        : size_(size)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        levels_.resize(static_cast<std::size_t>(levelCount_));
        for (int level = 0; level < levelCount_; ++level)
        {
            const int dim = LevelDim(level);
            const std::size_t faceBytes = static_cast<std::size_t>(dim) * static_cast<std::size_t>(dim) * 4u;
            for (auto& face : levels_[static_cast<std::size_t>(level)])
                face.assign(faceBytes, 0u);
        }
    }

    bool SokolTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                          const void* data, int dataLength)
    {
        if (data == nullptr || face < 0 || face > 5) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > dim || y + h > dim) return false;
        if (dataLength < w * h * 4) return false;

        const auto* src = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& pixels =
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t dstOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dim) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::memcpy(pixels.data() + dstOffset, src + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        }
        return true;
    }

    bool SokolTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (data == nullptr || face < 0 || face > 5) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > dim || y + h > dim) return false;
        if (dataLength < w * h * 4) return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const std::vector<std::uint8_t>& pixels =
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t srcOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dim) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes, pixels.data() + srcOffset, rowBytes);
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // SokolTexture3DBackend
    // ---------------------------------------------------------------------------------------

    namespace
    {
        // Mirrors Texture3D.cpp's own CalculateMipLevels(width, height) exactly -- depth does not
        // participate in the level COUNT (matching FNA's Texture3D.cs constructor), even though
        // LevelDepth() below still halves depth per level like width/height do.
        int CalculateVolumeMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }
    }

    int SokolTexture3DBackend::LevelWidth(int level) const { return std::max(1, width_ >> level); }
    int SokolTexture3DBackend::LevelHeight(int level) const { return std::max(1, height_ >> level); }
    int SokolTexture3DBackend::LevelDepth(int level) const { return std::max(1, depth_ >> level); }

    SokolTexture3DBackend::SokolTexture3DBackend(int width, int height, int depth, bool mipMap)
        : width_(width)
        , height_(height)
        , depth_(depth)
        , levelCount_(mipMap ? CalculateVolumeMipLevels(width, height) : 1)
    {
        levels_.resize(static_cast<std::size_t>(levelCount_));
        for (int level = 0; level < levelCount_; ++level)
        {
            const std::size_t voxelCount = static_cast<std::size_t>(LevelWidth(level))
                * static_cast<std::size_t>(LevelHeight(level))
                * static_cast<std::size_t>(LevelDepth(level));
            levels_[static_cast<std::size_t>(level)].assign(voxelCount * 4u, 0u);
        }
    }

    bool SokolTexture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                        const void* data, int dataLength)
    {
        if (data == nullptr || level < 0 || level >= levelCount_) return false;
        const int levelW = LevelWidth(level);
        const int levelH = LevelHeight(level);
        const int levelD = LevelDepth(level);
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0 ||
            x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (dataLength < w * h * depth * 4) return false;

        const auto* src = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& voxels = levels_[static_cast<std::size_t>(level)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        const std::size_t srcSliceBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t dstOffset =
                    ((static_cast<std::size_t>(z + slice) * static_cast<std::size_t>(levelH)
                      + static_cast<std::size_t>(y + row)) * static_cast<std::size_t>(levelW)
                     + static_cast<std::size_t>(x)) * 4u;
                const std::size_t srcOffset = static_cast<std::size_t>(slice) * srcSliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                std::memcpy(voxels.data() + dstOffset, src + srcOffset, rowBytes);
            }
        }
        return true;
    }

    bool SokolTexture3DBackend::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                        void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= levelCount_) return false;
        const int levelW = LevelWidth(level);
        const int levelH = LevelHeight(level);
        const int levelD = LevelDepth(level);
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0 ||
            x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (dataLength < w * h * depth * 4) return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const std::vector<std::uint8_t>& voxels = levels_[static_cast<std::size_t>(level)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        const std::size_t dstSliceBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t srcOffset =
                    ((static_cast<std::size_t>(z + slice) * static_cast<std::size_t>(levelH)
                      + static_cast<std::size_t>(y + row)) * static_cast<std::size_t>(levelW)
                     + static_cast<std::size_t>(x)) * 4u;
                const std::size_t dstOffset = static_cast<std::size_t>(slice) * dstSliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                std::memcpy(dst + dstOffset, voxels.data() + srcOffset, rowBytes);
            }
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderTargetBackend
    // ---------------------------------------------------------------------------------------

    SokolRenderTargetBackend::SokolRenderTargetBackend(int width, int height, bool hasDepthStencil,
                                                        int multiSampleCount)
        : width_(width)
        , height_(height)
    {
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Sokol backend: render target dimensions must be positive");

        // Clamp to the driver's real maximum, mirroring EasyGLGraphicsBackend's identical
        // GL_MAX_SAMPLES clamp for its own MSAA render targets (plan_sokol.md SOKOL-26):
        // glRenderbufferStorageMultisample/sg_make_image would otherwise fail outright for a
        // sample count the driver cannot provide. GetMultiSampleCount() reports this real value,
        // not the raw request, matching RenderTarget2D.MultiSampleCount's own FNA3D semantics.
        if (multiSampleCount > 1)
        {
#if CNA_SOKOL_HAS_GL_READBACK
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            multiSampleCount_ = maxSamples > 1 ? std::min(multiSampleCount, static_cast<int>(maxSamples))
                                                : 1;
#else
            multiSampleCount_ = 1;
#endif
            if (multiSampleCount_ <= 1) multiSampleCount_ = 0;
        }
        const bool msaa = multiSampleCount_ > 1;

        sg_image_desc colorDesc = {};
        colorDesc.type = SG_IMAGETYPE_2D;
        colorDesc.width = width_;
        colorDesc.height = height_;
        colorDesc.num_mipmaps = 1;
        colorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
        if (msaa)
        {
            // sokol_gfx.h's own "offscreen rendering"/MSAA doc comment: colorImageId_ becomes the
            // single-sample RESOLVE image here (never rendered into directly), while a separate
            // multisample-only image is what the pass actually attaches for drawing.
            colorDesc.usage.resolve_attachment = true;
            colorDesc.sample_count = 1;
            colorDesc.label = "cna_render_target_color_resolve";
        }
        else
        {
            colorDesc.usage.color_attachment = true;
            colorDesc.sample_count = 1;
            colorDesc.label = "cna_render_target_color";
        }
        colorImageId_ = sg_make_image(&colorDesc).id;
        if (sg_query_image_state(MakeImageHandle(colorImageId_)) != SG_RESOURCESTATE_VALID)
        {
            colorImageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_image failed for a RenderTarget2D's colour image");
        }

        if (msaa)
        {
            sg_image_desc msaaColorDesc = {};
            msaaColorDesc.type = SG_IMAGETYPE_2D;
            msaaColorDesc.usage.color_attachment = true;
            msaaColorDesc.width = width_;
            msaaColorDesc.height = height_;
            msaaColorDesc.num_mipmaps = 1;
            msaaColorDesc.sample_count = multiSampleCount_;
            msaaColorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
            msaaColorDesc.label = "cna_render_target_color_msaa";
            msaaColorImageId_ = sg_make_image(&msaaColorDesc).id;
            if (sg_query_image_state(MakeImageHandle(msaaColorImageId_)) != SG_RESOURCESTATE_VALID)
            {
                msaaColorImageId_ = 0;
                sg_destroy_image(MakeImageHandle(colorImageId_));
                colorImageId_ = 0;
                throw std::runtime_error(
                    "Sokol backend: sg_make_image failed for a RenderTarget2D's MSAA colour image");
            }
        }

        sg_view_desc colorAttachmentDesc = {};
        colorAttachmentDesc.color_attachment.image = MakeImageHandle(msaa ? msaaColorImageId_ : colorImageId_);
        colorAttachmentDesc.label = "cna_render_target_color_attachment_view";
        colorAttachmentViewId_ = sg_make_view(&colorAttachmentDesc).id;
        if (sg_query_view_state(MakeViewHandle(colorAttachmentViewId_)) != SG_RESOURCESTATE_VALID)
        {
            colorAttachmentViewId_ = 0;
            if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
            msaaColorImageId_ = 0;
            sg_destroy_image(MakeImageHandle(colorImageId_));
            colorImageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_view (colour attachment) failed for a RenderTarget2D");
        }

        if (msaa)
        {
            sg_view_desc resolveDesc = {};
            resolveDesc.resolve_attachment.image = MakeImageHandle(colorImageId_);
            resolveDesc.label = "cna_render_target_resolve_attachment_view";
            resolveAttachmentViewId_ = sg_make_view(&resolveDesc).id;
            if (sg_query_view_state(MakeViewHandle(resolveAttachmentViewId_)) != SG_RESOURCESTATE_VALID)
            {
                resolveAttachmentViewId_ = 0;
                sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
                colorAttachmentViewId_ = 0;
                sg_destroy_image(MakeImageHandle(msaaColorImageId_));
                msaaColorImageId_ = 0;
                sg_destroy_image(MakeImageHandle(colorImageId_));
                colorImageId_ = 0;
                throw std::runtime_error(
                    "Sokol backend: sg_make_view (resolve attachment) failed for a RenderTarget2D");
            }
        }

        sg_view_desc colorTextureDesc = {};
        colorTextureDesc.texture.image = MakeImageHandle(colorImageId_);
        colorTextureDesc.label = "cna_render_target_color_texture_view";
        colorTextureViewId_ = sg_make_view(&colorTextureDesc).id;
        if (sg_query_view_state(MakeViewHandle(colorTextureViewId_)) != SG_RESOURCESTATE_VALID)
        {
            colorTextureViewId_ = 0;
            if (resolveAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(resolveAttachmentViewId_));
            resolveAttachmentViewId_ = 0;
            sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
            colorAttachmentViewId_ = 0;
            if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
            msaaColorImageId_ = 0;
            sg_destroy_image(MakeImageHandle(colorImageId_));
            colorImageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_view (texture) failed for a RenderTarget2D");
        }

        if (!hasDepthStencil) return;

        sg_image_desc depthDesc = {};
        depthDesc.type = SG_IMAGETYPE_2D;
        depthDesc.usage.depth_stencil_attachment = true;
        depthDesc.width = width_;
        depthDesc.height = height_;
        depthDesc.num_mipmaps = 1;
        // A multisampled colour attachment requires a depth-stencil attachment with the identical
        // sample count in the same pass (sokol_gfx's VALIDATE_BEGINPASS_..._SAMPLECOUNT). It is
        // never resolved -- nothing here reads a render target's depth-stencil content back.
        depthDesc.sample_count = msaa ? multiSampleCount_ : 1;
        depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depthDesc.label = "cna_render_target_depth_stencil";
        depthStencilImageId_ = sg_make_image(&depthDesc).id;
        if (sg_query_image_state(MakeImageHandle(depthStencilImageId_)) != SG_RESOURCESTATE_VALID)
        {
            depthStencilImageId_ = 0;
            sg_destroy_view(MakeViewHandle(colorTextureViewId_));
            colorTextureViewId_ = 0;
            if (resolveAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(resolveAttachmentViewId_));
            resolveAttachmentViewId_ = 0;
            sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
            colorAttachmentViewId_ = 0;
            if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
            msaaColorImageId_ = 0;
            sg_destroy_image(MakeImageHandle(colorImageId_));
            colorImageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_image failed for a RenderTarget2D's depth-stencil image");
        }

        sg_view_desc depthAttachmentDesc = {};
        depthAttachmentDesc.depth_stencil_attachment.image = MakeImageHandle(depthStencilImageId_);
        depthAttachmentDesc.label = "cna_render_target_depth_stencil_attachment_view";
        depthStencilAttachmentViewId_ = sg_make_view(&depthAttachmentDesc).id;
        if (sg_query_view_state(MakeViewHandle(depthStencilAttachmentViewId_)) != SG_RESOURCESTATE_VALID)
        {
            depthStencilAttachmentViewId_ = 0;
            sg_destroy_image(MakeImageHandle(depthStencilImageId_));
            depthStencilImageId_ = 0;
            sg_destroy_view(MakeViewHandle(colorTextureViewId_));
            colorTextureViewId_ = 0;
            if (resolveAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(resolveAttachmentViewId_));
            resolveAttachmentViewId_ = 0;
            sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
            colorAttachmentViewId_ = 0;
            if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
            msaaColorImageId_ = 0;
            sg_destroy_image(MakeImageHandle(colorImageId_));
            colorImageId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_view (depth-stencil attachment) failed for a RenderTarget2D");
        }
    }

    SokolRenderTargetBackend::~SokolRenderTargetBackend()
    {
        if (depthStencilAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(depthStencilAttachmentViewId_));
        if (depthStencilImageId_ != 0) sg_destroy_image(MakeImageHandle(depthStencilImageId_));
        if (colorTextureViewId_ != 0) sg_destroy_view(MakeViewHandle(colorTextureViewId_));
        if (resolveAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(resolveAttachmentViewId_));
        if (colorAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
        if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
        if (colorImageId_ != 0) sg_destroy_image(MakeImageHandle(colorImageId_));
    }

    int SokolRenderTargetBackend::GetWidth() const { return width_; }

    int SokolRenderTargetBackend::GetHeight() const { return height_; }

    SDL_Texture* SokolRenderTargetBackend::GetNativeTexture() const { return nullptr; }

    void SokolRenderTargetBackend::BindAsRenderTarget() {}

    void SokolRenderTargetBackend::UnbindAsRenderTarget() {}

    bool SokolRenderTargetBackend::HasRealDepthBuffer(bool depthFormatWasRequested) const
    {
        (void)depthFormatWasRequested;
        return depthStencilAttachmentViewId_ != 0;
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderTargetCubeBackend
    // ---------------------------------------------------------------------------------------

    SokolRenderTargetCubeBackend::SokolRenderTargetCubeBackend(int size, bool hasDepthStencil)
        : size_(size)
    {
        if (size <= 0)
            throw std::runtime_error("Sokol backend: cube render target size must be positive");

        sg_image_desc colorDesc = {};
        colorDesc.type = SG_IMAGETYPE_CUBE;
        colorDesc.usage.color_attachment = true;
        colorDesc.width = size_;
        colorDesc.height = size_;
        colorDesc.num_mipmaps = 1;
        colorDesc.sample_count = 1;
        colorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
        colorDesc.label = "cna_render_target_cube_color";
        imageId_ = sg_make_image(&colorDesc).id;
        if (sg_query_image_state(MakeImageHandle(imageId_)) != SG_RESOURCESTATE_VALID)
        {
            imageId_ = 0;
            throw std::runtime_error(
                "Sokol backend: sg_make_image failed for a RenderTargetCube's colour image");
        }

        for (int face = 0; face < 6; ++face)
        {
            sg_view_desc attachmentDesc = {};
            attachmentDesc.color_attachment.image = MakeImageHandle(imageId_);
            attachmentDesc.color_attachment.slice = face;
            attachmentDesc.label = "cna_render_target_cube_color_attachment_view";
            const std::uint32_t viewId = sg_make_view(&attachmentDesc).id;
            if (sg_query_view_state(MakeViewHandle(viewId)) != SG_RESOURCESTATE_VALID)
            {
                if (viewId != 0) sg_destroy_view(MakeViewHandle(viewId));
                for (int destroyed = 0; destroyed < face; ++destroyed)
                    sg_destroy_view(MakeViewHandle(colorAttachmentViewIds_[static_cast<std::size_t>(destroyed)]));
                sg_destroy_image(MakeImageHandle(imageId_));
                imageId_ = 0;
                throw std::runtime_error(
                    "Sokol backend: sg_make_view (colour attachment) failed for a RenderTargetCube face");
            }
            colorAttachmentViewIds_[static_cast<std::size_t>(face)] = viewId;
        }

        sg_view_desc textureDesc = {};
        textureDesc.texture.image = MakeImageHandle(imageId_);
        textureDesc.label = "cna_render_target_cube_texture_view";
        textureViewId_ = sg_make_view(&textureDesc).id;
        if (sg_query_view_state(MakeViewHandle(textureViewId_)) != SG_RESOURCESTATE_VALID)
        {
            textureViewId_ = 0;
            for (std::uint32_t viewId : colorAttachmentViewIds_)
                sg_destroy_view(MakeViewHandle(viewId));
            sg_destroy_image(MakeImageHandle(imageId_));
            imageId_ = 0;
            throw std::runtime_error(
                "Sokol backend: sg_make_view (texture) failed for a RenderTargetCube");
        }

        if (!hasDepthStencil) return;

        // ONE plain 2D depth-stencil image, shared by all six faces -- see this class's own doc
        // comment for why that matches FNA3D's cube render-target convention.
        sg_image_desc depthDesc = {};
        depthDesc.type = SG_IMAGETYPE_2D;
        depthDesc.usage.depth_stencil_attachment = true;
        depthDesc.width = size_;
        depthDesc.height = size_;
        depthDesc.num_mipmaps = 1;
        depthDesc.sample_count = 1;
        depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depthDesc.label = "cna_render_target_cube_depth_stencil";
        depthStencilImageId_ = sg_make_image(&depthDesc).id;
        if (sg_query_image_state(MakeImageHandle(depthStencilImageId_)) != SG_RESOURCESTATE_VALID)
        {
            depthStencilImageId_ = 0;
            sg_destroy_view(MakeViewHandle(textureViewId_));
            textureViewId_ = 0;
            for (std::uint32_t viewId : colorAttachmentViewIds_)
                sg_destroy_view(MakeViewHandle(viewId));
            sg_destroy_image(MakeImageHandle(imageId_));
            imageId_ = 0;
            throw std::runtime_error(
                "Sokol backend: sg_make_image failed for a RenderTargetCube's depth-stencil image");
        }

        sg_view_desc depthAttachmentDesc = {};
        depthAttachmentDesc.depth_stencil_attachment.image = MakeImageHandle(depthStencilImageId_);
        depthAttachmentDesc.label = "cna_render_target_cube_depth_stencil_attachment_view";
        depthStencilAttachmentViewId_ = sg_make_view(&depthAttachmentDesc).id;
        if (sg_query_view_state(MakeViewHandle(depthStencilAttachmentViewId_)) != SG_RESOURCESTATE_VALID)
        {
            depthStencilAttachmentViewId_ = 0;
            sg_destroy_image(MakeImageHandle(depthStencilImageId_));
            depthStencilImageId_ = 0;
            sg_destroy_view(MakeViewHandle(textureViewId_));
            textureViewId_ = 0;
            for (std::uint32_t viewId : colorAttachmentViewIds_)
                sg_destroy_view(MakeViewHandle(viewId));
            sg_destroy_image(MakeImageHandle(imageId_));
            imageId_ = 0;
            throw std::runtime_error(
                "Sokol backend: sg_make_view (depth-stencil attachment) failed for a RenderTargetCube");
        }
    }

    SokolRenderTargetCubeBackend::~SokolRenderTargetCubeBackend()
    {
        if (depthStencilAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(depthStencilAttachmentViewId_));
        if (depthStencilImageId_ != 0) sg_destroy_image(MakeImageHandle(depthStencilImageId_));
        if (textureViewId_ != 0) sg_destroy_view(MakeViewHandle(textureViewId_));
        for (std::uint32_t viewId : colorAttachmentViewIds_)
            if (viewId != 0) sg_destroy_view(MakeViewHandle(viewId));
        if (imageId_ != 0) sg_destroy_image(MakeImageHandle(imageId_));
    }

    int SokolRenderTargetCubeBackend::GetSize() const { return size_; }

    void SokolRenderTargetCubeBackend::BindAsRenderTargetFace(int /*face*/) {}

    void SokolRenderTargetCubeBackend::UnbindAsRenderTarget() {}

    bool SokolRenderTargetCubeBackend::HasRealDepthBuffer(bool depthFormatWasRequested) const
    {
        (void)depthFormatWasRequested;
        return depthStencilAttachmentViewId_ != 0;
    }

    // ---------------------------------------------------------------------------------------
    // SokolVertexBufferBackend
    // ---------------------------------------------------------------------------------------

    SokolVertexBufferBackend::SokolVertexBufferBackend(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
    }

    SokolVertexBufferBackend::~SokolVertexBufferBackend()
    {
        if (bufferId_ != 0) sg_destroy_buffer(MakeBufferHandle(bufferId_));
    }

    void SokolVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }

        // Recreated rather than updated: sokol_gfx permits at most one sg_update_buffer() per
        // buffer per frame, while creating an immutable buffer with initial data has no such
        // limit, so a game that re-uploads several times in one frame stays legal here.
        if (bufferId_ != 0)
        {
            sg_destroy_buffer(MakeBufferHandle(bufferId_));
            bufferId_ = 0;
        }

        sg_buffer_desc desc = {};
        desc.usage.vertex_buffer = true;
        desc.usage.immutable = true;
        desc.size = static_cast<std::size_t>(vertexCount) * strideInBytes;
        desc.data.ptr = data;
        desc.data.size = desc.size;
        desc.label = "cna_vertex_buffer";
        bufferId_ = sg_make_buffer(&desc).id;
        if (sg_query_buffer_state(MakeBufferHandle(bufferId_)) != SG_RESOURCESTATE_VALID)
        {
            bufferId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_buffer failed for a VertexBuffer");
        }

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void SokolVertexBufferBackend::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // Consumed by DrawColored3D, which builds its pipeline's vertex layout from the real
        // element offsets rather than inferring one from the byte stride.
        declaration_ = vertexDeclaration;
        hasDeclaration_ = true;
    }

    int SokolVertexBufferBackend::GetVertexCount() const { return vertexCount_; }

    // ---------------------------------------------------------------------------------------
    // SokolIndexBufferBackend
    // ---------------------------------------------------------------------------------------

    SokolIndexBufferBackend::SokolIndexBufferBackend(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity)
        , thirtyTwoBit_(thirtyTwoBit)
    {
    }

    SokolIndexBufferBackend::~SokolIndexBufferBackend()
    {
        if (bufferId_ != 0) sg_destroy_buffer(MakeBufferHandle(bufferId_));
    }

    void SokolIndexBufferBackend::Upload(const void* data, int indexCount, std::size_t indexSize)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }

        // Same recreate-instead-of-update reasoning as SokolVertexBufferBackend::SetData.
        if (bufferId_ != 0)
        {
            sg_destroy_buffer(MakeBufferHandle(bufferId_));
            bufferId_ = 0;
        }

        sg_buffer_desc desc = {};
        desc.usage.index_buffer = true;
        desc.usage.immutable = true;
        desc.size = static_cast<std::size_t>(indexCount) * indexSize;
        desc.data.ptr = data;
        desc.data.size = desc.size;
        desc.label = "cna_index_buffer";
        bufferId_ = sg_make_buffer(&desc).id;
        if (sg_query_buffer_state(MakeBufferHandle(bufferId_)) != SG_RESOURCESTATE_VALID)
        {
            bufferId_ = 0;
            throw std::runtime_error("Sokol backend: sg_make_buffer failed for an IndexBuffer");
        }

        indexCount_ = indexCount;
    }

    void SokolIndexBufferBackend::SetData16(const void* data, int indexCount)
    {
        thirtyTwoBit_ = false;
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void SokolIndexBufferBackend::SetData32(const void* data, int indexCount)
    {
        thirtyTwoBit_ = true;
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    int SokolIndexBufferBackend::GetIndexCount() const { return indexCount_; }

    bool SokolIndexBufferBackend::IsThirtyTwoBit() const { return thirtyTwoBit_; }

    // ---------------------------------------------------------------------------------------
    // SokolOcclusionQueryBackend
    // ---------------------------------------------------------------------------------------

    SokolOcclusionQueryBackend::SokolOcclusionQueryBackend()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        GLuint id = 0;
        glGenQueries(1, &id);
        queryId_ = id;
#endif
    }

    SokolOcclusionQueryBackend::~SokolOcclusionQueryBackend()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (queryId_ != 0)
        {
            const GLuint id = queryId_;
            glDeleteQueries(1, &id);
        }
#endif
    }

    void SokolOcclusionQueryBackend::Begin()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        // See this method's own header doc: a repeated Begin() with no intervening End() must not
        // reach glBeginQuery a second time, or GL raises GL_INVALID_OPERATION that stays pending
        // until sokol_gfx's own next GL call trips over it.
        if (queryId_ == 0 || active_) return;
        active_ = true;
        // SOKOL_GLCORE is desktop GL, which reports the real sample count (GL_SAMPLES_PASSED);
        // GLES3 has no such query at all, only the any-samples boolean EasyGL's own GLES3 target
        // uses -- matching that here keeps this branch meaningful if SOKOL_GLES3 is ever the
        // active configure-time API (docs/sokol-backend.md: only GLCORE is implemented/verified).
    #if defined(SOKOL_GLCORE)
        glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    #else
        glBeginQuery(GL_ANY_SAMPLES_PASSED, queryId_);
    #endif
#endif
    }

    void SokolOcclusionQueryBackend::End()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        // See Begin()'s own comment: an End() with no active Begin() must not reach glEndQuery.
        if (queryId_ == 0 || !active_) return;
        active_ = false;
        hasResult_ = true;
    #if defined(SOKOL_GLCORE)
        glEndQuery(GL_SAMPLES_PASSED);
    #else
        glEndQuery(GL_ANY_SAMPLES_PASSED);
    #endif
#endif
    }

    bool SokolOcclusionQueryBackend::IsComplete() const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        // See hasResult_'s own comment: glGetQueryObject* errors on a query that was never
        // started, or is still active, so neither case ever reaches GL at all here.
        if (queryId_ == 0 || active_ || !hasResult_) return false;
        GLuint available = 0;
        glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
        return available != 0;
#else
        return false;
#endif
    }

    int SokolOcclusionQueryBackend::PixelCount() const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (!IsComplete()) return 0;
        GLuint result = 0;
        glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &result);
        return static_cast<int>(result);
#else
        return 0;
#endif
    }

    // ---------------------------------------------------------------------------------------
    // SokolSpriteBatchBackend
    // ---------------------------------------------------------------------------------------

    SokolSpriteBatchBackend::SokolSpriteBatchBackend(SokolGraphicsBackend& backend)
        : backend_(backend)
    {
    }

    SokolSpriteBatchBackend::~SokolSpriteBatchBackend() = default;

    void SokolSpriteBatchBackend::Begin()
    {
        begun_ = true;
        pendingVertices_.clear();
        currentTexture_ = nullptr;
    }

    void SokolSpriteBatchBackend::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void SokolSpriteBatchBackend::SetTransformMatrix(const Matrix& m) { transform_ = m; }

    void SokolSpriteBatchBackend::SetSamplerFilter(int textureFilter) { pendingFilter_ = textureFilter; }

    void SokolSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void SokolSpriteBatchBackend::FlushBatch()
    {
        if (pendingVertices_.empty() || currentTexture_ == nullptr)
        {
            pendingVertices_.clear();
            currentTexture_ = nullptr;
            return;
        }

        backend_.DrawSpriteRunEXT(*currentTexture_, pendingVertices_, transform_,
                                  pendingFilter_, pendingAddressU_, pendingAddressV_);
        pendingVertices_.clear();
        currentTexture_ = nullptr;
    }

    void SokolSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h),
             Rectangle(0, 0, w, h), Microsoft::Xna::Framework::Color::White);
    }

    void SokolSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color,
             0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void SokolSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth)
    {
        (void)layerDepth; // Sort order is resolved by the shared SpriteBatch before it reaches here.

        if (!begun_) throw std::runtime_error("Sokol backend: SpriteBatch Draw called before Begin()");

        if (currentTexture_ != &texture)
        {
            if (currentTexture_ != nullptr) FlushBatch();
            currentTexture_ = &texture;
        }

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());
        if (texW <= 0.0f || texH <= 0.0f) return;

        // Deliberately unclamped, matching FNA: a sourceRectangle reaching past the texture yields
        // UVs outside [0,1] so the bound TextureAddressMode governs the edge, which is what makes
        // the classic XNA tiling/scrolling technique work.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) std::swap(v1, v2);

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);
        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);
        if (sw == 0.0f || sh == 0.0f) return;

        const float scaleX = dw / sw;
        const float scaleY = dh / sh;
        const float ox = origin.X;
        const float oy = origin.Y;

        const float cornerX[4] = {(0.0f - ox) * scaleX, (sw - ox) * scaleX,
                                  (sw - ox) * scaleX, (0.0f - ox) * scaleX};
        const float cornerY[4] = {(0.0f - oy) * scaleY, (0.0f - oy) * scaleY,
                                  (sh - oy) * scaleY, (sh - oy) * scaleY};
        const float cornerU[4] = {u1, u2, u2, u1};
        const float cornerV[4] = {v1, v1, v2, v2};

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        for (int corner = 0; corner < 4; ++corner)
        {
            Vertex vertex{};
            vertex.x = dx + cornerX[corner] * cosR - cornerY[corner] * sinR;
            vertex.y = dy + cornerX[corner] * sinR + cornerY[corner] * cosR;
            vertex.u = cornerU[corner];
            vertex.v = cornerV[corner];
            vertex.r = r;
            vertex.g = g;
            vertex.b = b;
            vertex.a = a;
            pendingVertices_.push_back(vertex);
        }
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- cache keys
    // ---------------------------------------------------------------------------------------

    bool SokolGraphicsBackend::PipelineKey::operator==(const PipelineKey& other) const
    {
        return colorSrcBlend == other.colorSrcBlend
            && alphaSrcBlend == other.alphaSrcBlend
            && colorDstBlend == other.colorDstBlend
            && alphaDstBlend == other.alphaDstBlend
            && colorBlendFunc == other.colorBlendFunc
            && alphaBlendFunc == other.alphaBlendFunc
            && colorWriteChannels == other.colorWriteChannels
            && blendEnabled == other.blendEnabled
            && depthTestEnabled == other.depthTestEnabled
            && depthWriteEnabled == other.depthWriteEnabled
            && depthFunc == other.depthFunc
            && hasDepthAttachment == other.hasDepthAttachment
            && sampleCount == other.sampleCount;
    }

    std::size_t SokolGraphicsBackend::PipelineKeyHash::operator()(const PipelineKey& key) const
    {
        std::size_t hash = 1469598103934665603ull;
        auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(static_cast<std::size_t>(key.colorSrcBlend));
        mix(static_cast<std::size_t>(key.alphaSrcBlend));
        mix(static_cast<std::size_t>(key.colorDstBlend));
        mix(static_cast<std::size_t>(key.alphaDstBlend));
        mix(static_cast<std::size_t>(key.colorBlendFunc));
        mix(static_cast<std::size_t>(key.alphaBlendFunc));
        mix(static_cast<std::size_t>(key.colorWriteChannels));
        mix(static_cast<std::size_t>(key.blendEnabled));
        mix(static_cast<std::size_t>(key.depthTestEnabled));
        mix(static_cast<std::size_t>(key.depthWriteEnabled));
        mix(static_cast<std::size_t>(key.depthFunc));
        mix(static_cast<std::size_t>(key.hasDepthAttachment));
        mix(static_cast<std::size_t>(key.sampleCount));
        return hash;
    }

    bool SokolGraphicsBackend::Pipeline3DKey::operator==(const Pipeline3DKey& other) const
    {
        return kind == other.kind
            && colorSrcBlend == other.colorSrcBlend
            && alphaSrcBlend == other.alphaSrcBlend
            && colorDstBlend == other.colorDstBlend
            && alphaDstBlend == other.alphaDstBlend
            && colorBlendFunc == other.colorBlendFunc
            && alphaBlendFunc == other.alphaBlendFunc
            && colorWriteChannels == other.colorWriteChannels
            && blendEnabled == other.blendEnabled
            && depthTestEnabled == other.depthTestEnabled
            && depthWriteEnabled == other.depthWriteEnabled
            && depthFunc == other.depthFunc
            && hasDepthAttachment == other.hasDepthAttachment
            && stencilEnabled == other.stencilEnabled
            && stencilFunc == other.stencilFunc
            && stencilPass == other.stencilPass
            && stencilFail == other.stencilFail
            && stencilDepthFail == other.stencilDepthFail
            && ccwStencilFunc == other.ccwStencilFunc
            && ccwStencilPass == other.ccwStencilPass
            && ccwStencilFail == other.ccwStencilFail
            && ccwStencilDepthFail == other.ccwStencilDepthFail
            && twoSidedStencilMode == other.twoSidedStencilMode
            && stencilMask == other.stencilMask
            && stencilWriteMask == other.stencilWriteMask
            && referenceStencil == other.referenceStencil
            && depthBias == other.depthBias
            && slopeScaleDepthBias == other.slopeScaleDepthBias
            && sampleCount == other.sampleCount
            && cullMode == other.cullMode
            && primitiveType == other.primitiveType
            && indexType == other.indexType
            && stride == other.stride
            && positionOffset == other.positionOffset
            && positionFormat == other.positionFormat
            && colorOffset == other.colorOffset
            && colorFormat == other.colorFormat
            && texCoordOffset == other.texCoordOffset
            && texCoordFormat == other.texCoordFormat
            && normalOffset == other.normalOffset
            && normalFormat == other.normalFormat;
    }

    std::size_t SokolGraphicsBackend::Pipeline3DKeyHash::operator()(const Pipeline3DKey& key) const
    {
        std::size_t hash = 1469598103934665603ull;
        auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(static_cast<std::size_t>(key.kind));
        mix(static_cast<std::size_t>(key.colorSrcBlend));
        mix(static_cast<std::size_t>(key.alphaSrcBlend));
        mix(static_cast<std::size_t>(key.colorDstBlend));
        mix(static_cast<std::size_t>(key.alphaDstBlend));
        mix(static_cast<std::size_t>(key.colorBlendFunc));
        mix(static_cast<std::size_t>(key.alphaBlendFunc));
        mix(static_cast<std::size_t>(key.colorWriteChannels));
        mix(static_cast<std::size_t>(key.blendEnabled));
        mix(static_cast<std::size_t>(key.depthTestEnabled));
        mix(static_cast<std::size_t>(key.depthWriteEnabled));
        mix(static_cast<std::size_t>(key.depthFunc));
        mix(static_cast<std::size_t>(key.hasDepthAttachment));
        mix(static_cast<std::size_t>(key.stencilEnabled));
        mix(static_cast<std::size_t>(key.stencilFunc));
        mix(static_cast<std::size_t>(key.stencilPass));
        mix(static_cast<std::size_t>(key.stencilFail));
        mix(static_cast<std::size_t>(key.stencilDepthFail));
        mix(static_cast<std::size_t>(key.ccwStencilFunc));
        mix(static_cast<std::size_t>(key.ccwStencilPass));
        mix(static_cast<std::size_t>(key.ccwStencilFail));
        mix(static_cast<std::size_t>(key.ccwStencilDepthFail));
        mix(static_cast<std::size_t>(key.twoSidedStencilMode));
        mix(static_cast<std::size_t>(key.stencilMask));
        mix(static_cast<std::size_t>(key.stencilWriteMask));
        mix(static_cast<std::size_t>(key.referenceStencil));
        mix(std::hash<float>{}(key.depthBias));
        mix(std::hash<float>{}(key.slopeScaleDepthBias));
        mix(static_cast<std::size_t>(key.sampleCount));
        mix(static_cast<std::size_t>(key.cullMode));
        mix(static_cast<std::size_t>(key.primitiveType));
        mix(static_cast<std::size_t>(key.indexType));
        mix(static_cast<std::size_t>(key.stride));
        mix(static_cast<std::size_t>(key.positionOffset));
        mix(static_cast<std::size_t>(key.positionFormat));
        mix(static_cast<std::size_t>(key.colorOffset + 1));
        mix(static_cast<std::size_t>(key.colorFormat));
        mix(static_cast<std::size_t>(key.texCoordOffset + 1));
        mix(static_cast<std::size_t>(key.texCoordFormat));
        mix(static_cast<std::size_t>(key.normalOffset + 1));
        mix(static_cast<std::size_t>(key.normalFormat));
        return hash;
    }

    bool SokolGraphicsBackend::SamplerKey::operator==(const SamplerKey& other) const
    {
        return filter == other.filter && addressU == other.addressU
            && addressV == other.addressV && maxAnisotropy == other.maxAnisotropy;
    }

    std::size_t SokolGraphicsBackend::SamplerKeyHash::operator()(const SamplerKey& key) const
    {
        return (static_cast<std::size_t>(key.filter) << 24)
             ^ (static_cast<std::size_t>(key.addressU) << 16)
             ^ (static_cast<std::size_t>(key.addressV) << 8)
             ^ static_cast<std::size_t>(key.maxAnisotropy);
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- lifetime
    // ---------------------------------------------------------------------------------------

    SokolGraphicsBackend::SokolGraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , sampleCount_(args.multiSampleCount > 1 ? args.multiSampleCount : 1)
        , swapInterval_(args.swapInterval)
    {
        if (window_ == nullptr)
            throw std::runtime_error("Sokol backend: initialized with a null SDL_Window");

        CreateGpuContext(window_, sampleCount_);
        try
        {
            SetupSokol();
            CreateSpriteResources();
        }
        catch (...)
        {
            // Transactional construction: a partially initialised backend must never reach the
            // window registry or a caller's unique_ptr.
            if (sg_isvalid()) sg_shutdown();
            if (glContext_ != nullptr)
            {
                SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
                glContext_ = nullptr;
            }
            throw;
        }

        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    SokolGraphicsBackend::~SokolGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);

        if (passActive_)
        {
            sg_end_pass();
            passActive_ = false;
        }
        // Explicitly released before sg_shutdown(): its destructor calls sg_destroy_view()/
        // sg_destroy_image(), which assert if the sokol_gfx context is no longer valid by the time
        // this member's own destructor runs (member destruction happens after this body, in
        // reverse declaration order -- after sg_shutdown() below, not before it).
        defaultWhiteTexture_.reset();
        if (sg_isvalid()) sg_shutdown();
        if (glContext_ != nullptr)
        {
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
            glContext_ = nullptr;
        }
    }

    void SokolGraphicsBackend::CreateGpuContext(SDL_Window* window, int multiSampleCount)
    {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    #if defined(SOKOL_GLCORE)
        // sokol_gfx's GLCORE backend targets the OpenGL 4.1 core profile; asking for less would
        // make sg_setup() fail on features it assumes unconditionally.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    #else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    #endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        // Without an explicit request SDL hands out a window with zero stencil bits, which would
        // make every stencil clear and DepthStencilState.StencilEnable a silent no-op.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        if (multiSampleCount > 1)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multiSampleCount);
        }

        glContext_ = SDL_GL_CreateContext(window);
        if (glContext_ == nullptr)
            throw std::runtime_error(std::string("Sokol backend: SDL_GL_CreateContext failed: ") + SDL_GetError());

        if (!SDL_GL_MakeCurrent(window, static_cast<SDL_GLContext>(glContext_)))
            throw std::runtime_error(std::string("Sokol backend: SDL_GL_MakeCurrent failed: ") + SDL_GetError());

        SDL_GL_SetSwapInterval(swapInterval_);

        // Report back what the driver actually granted rather than what was asked for, matching
        // FNA3D's "MultiSampleCount reflects the real clamped value" semantics.
        int grantedSamples = 0;
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &grantedSamples))
            sampleCount_ = grantedSamples > 1 ? grantedSamples : 1;
        else
            sampleCount_ = 1;
#else
        (void)window;
        (void)multiSampleCount;
        throw std::runtime_error(
            "Sokol backend: CNA_SOKOL_API is set to a non-GL API, but only the GL context path is "
            "implemented (plan_sokol.md Phase SOKOL-8)");
#endif
    }

    void SokolGraphicsBackend::SetupSokol()
    {
        sg_desc desc = {};
        desc.logger.func = slog_func;
        desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        desc.environment.defaults.sample_count = sampleCount_;
        sg_setup(&desc);

        if (!sg_isvalid())
            throw std::runtime_error("Sokol backend: sg_setup() did not produce a valid sokol_gfx context");
    }

    void SokolGraphicsBackend::CreateSpriteResources()
    {
        spriteShaderId_ = sg_make_shader(cna_sprite_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(spriteShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: sprite shader creation failed");

        // Streaming vertex buffer: each SpriteBatch flush appends its own run and draws from the
        // returned byte offset, which is what makes an arbitrary number of flushes per frame legal
        // under sokol_gfx's one-update-per-buffer-per-frame rule.
        sg_buffer_desc vertexDesc = {};
        vertexDesc.usage.vertex_buffer = true;
        vertexDesc.usage.stream_update = true;
        vertexDesc.size = static_cast<std::size_t>(kMaxSpriteQuads) * kSpriteVerticesPerQuad
                        * sizeof(SokolSpriteBatchBackend::Vertex);
        vertexDesc.label = "cna_sprite_vertices";
        spriteVertexBufferId_ = sg_make_buffer(&vertexDesc).id;
        if (sg_query_buffer_state(MakeBufferHandle(spriteVertexBufferId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: sprite vertex buffer creation failed");

        // The quad index pattern never changes, so it is built once as an immutable buffer and
        // every flush indexes into its own appended vertex run via vertex_buffer_offsets.
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(kMaxSpriteQuads) * kSpriteIndicesPerQuad);
        for (int quad = 0; quad < kMaxSpriteQuads; ++quad)
        {
            const auto base = static_cast<std::uint16_t>(quad * kSpriteVerticesPerQuad);
            indices.push_back(static_cast<std::uint16_t>(base + 0));
            indices.push_back(static_cast<std::uint16_t>(base + 1));
            indices.push_back(static_cast<std::uint16_t>(base + 2));
            indices.push_back(static_cast<std::uint16_t>(base + 2));
            indices.push_back(static_cast<std::uint16_t>(base + 3));
            indices.push_back(static_cast<std::uint16_t>(base + 0));
        }

        sg_buffer_desc indexDesc = {};
        indexDesc.usage.index_buffer = true;
        indexDesc.usage.immutable = true;
        indexDesc.size = indices.size() * sizeof(std::uint16_t);
        indexDesc.data.ptr = indices.data();
        indexDesc.data.size = indexDesc.size;
        indexDesc.label = "cna_sprite_indices";
        spriteIndexBufferId_ = sg_make_buffer(&indexDesc).id;
        if (sg_query_buffer_state(MakeBufferHandle(spriteIndexBufferId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: sprite index buffer creation failed");

        colored3dShaderId_ = sg_make_shader(cna_colored3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(colored3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: colored-3D shader creation failed");

        textured3dShaderId_ = sg_make_shader(cna_textured3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(textured3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: textured-3D shader creation failed");

        lit3dShaderId_ = sg_make_shader(cna_lit3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(lit3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: lit-3D shader creation failed");
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- pass management
    // ---------------------------------------------------------------------------------------

    void SokolGraphicsBackend::QueueClear(bool color, float r, float g, float b, float a,
                                          bool depth, float depthValue,
                                          bool stencil, int stencilValue)
    {
        // A sokol_gfx pass fixes its load actions when it begins, so a clear arriving mid-pass has
        // to close that pass and let the next one start with the requested action.
        EndPassIfActive();

        if (color)
        {
            pendingClearColor_ = true;
            pendingClear_[0] = r;
            pendingClear_[1] = g;
            pendingClear_[2] = b;
            pendingClear_[3] = a;
        }
        if (depth)
        {
            pendingClearDepth_ = true;
            pendingDepth_ = depthValue;
        }
        if (stencil)
        {
            pendingClearStencil_ = true;
            pendingStencil_ = stencilValue;
        }
    }

    void SokolGraphicsBackend::BeginPassIfNeeded()
    {
        if (passActive_) return;

        sg_pass pass = {};

        // LOAD unless an explicit Clear* asked otherwise: a pass restarted mid-frame (because a
        // clear or a state change closed the previous one) must not discard what was already drawn.
        // This is also what makes a bound render target's content survive across separate
        // SetRenderTarget(s) bind/unbind cycles regardless of RenderTargetUsage -- matching the
        // EasyGL backend's own documented simplification: a real FBO naturally preserves content,
        // and an explicit Clear() (which GraphicsDevice itself issues for a DiscardContents
        // target immediately after binding) is what actually discards it, on every backend.
        pass.action.colors[0].load_action = pendingClearColor_ ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
        pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        pass.action.colors[0].clear_value.r = pendingClear_[0];
        pass.action.colors[0].clear_value.g = pendingClear_[1];
        pass.action.colors[0].clear_value.b = pendingClear_[2];
        pass.action.colors[0].clear_value.a = pendingClear_[3];

        // Resolved once here: which colour/depth-stencil attachment views the active target (a
        // RenderTarget2D, a RenderTargetCube face, or -- when both are null -- the swapchain)
        // actually has, so every branch below reads from one place instead of re-deriving it.
        std::uint32_t colorAttachmentViewId = 0;
        std::uint32_t depthStencilAttachmentViewId = 0;
        std::uint32_t resolveAttachmentViewId = 0;
        if (currentRenderTargetCube_ != nullptr)
        {
            colorAttachmentViewId =
                currentRenderTargetCube_->GetColorAttachmentViewIdEXT(currentRenderTargetCubeFace_);
            depthStencilAttachmentViewId = currentRenderTargetCube_->GetDepthStencilAttachmentViewIdEXT();
        }
        else if (currentRenderTarget_ != nullptr)
        {
            colorAttachmentViewId = currentRenderTarget_->GetColorAttachmentViewIdEXT();
            depthStencilAttachmentViewId = currentRenderTarget_->GetDepthStencilAttachmentViewIdEXT();
            resolveAttachmentViewId = currentRenderTarget_->GetResolveAttachmentViewIdEXT();
        }
        const bool boundToCustomTarget = currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr;
        const bool hasDepthStencil = !boundToCustomTarget || depthStencilAttachmentViewId != 0;
        if (hasDepthStencil)
        {
            pass.action.depth.load_action = pendingClearDepth_ ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
            pass.action.depth.store_action = SG_STOREACTION_STORE;
            pass.action.depth.clear_value = pendingDepth_;

            pass.action.stencil.load_action = pendingClearStencil_ ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
            pass.action.stencil.store_action = SG_STOREACTION_STORE;
            pass.action.stencil.clear_value = static_cast<std::uint8_t>(pendingStencil_ & 0xFF);
        }
        // No depth attachment at all: pass.action.depth/.stencil are left at their zero-value
        // defaults (LOADACTION_DEFAULT), which sokol_gfx ignores when attachments.depth_stencil is
        // an unset view -- a Clear(DepthBuffer) queued while such a target is bound has nothing to
        // act on, matching real XNA's own no-op-if-no-depth-buffer behaviour.

        if (!boundToCustomTarget)
        {
            int physicalWidth = 0;
            int physicalHeight = 0;
            GetPhysicalSizeEXT(physicalWidth, physicalHeight);

            pass.swapchain.width = physicalWidth;
            pass.swapchain.height = physicalHeight;
            pass.swapchain.sample_count = sampleCount_;
            pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
            pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            pass.swapchain.gl.framebuffer = 0;
            pass.label = "cna_swapchain_pass";
        }
        else
        {
            pass.attachments.colors[0] = MakeViewHandle(colorAttachmentViewId);
            if (hasDepthStencil)
                pass.attachments.depth_stencil = MakeViewHandle(depthStencilAttachmentViewId);
            if (resolveAttachmentViewId != 0)
            {
                // sokol_gfx.h's own MSAA doc comment: naming a resolve-attachment view triggers an
                // automatic resolve at sg_end_pass(), and the MSAA colour image's own content need
                // not survive past that point -- only the resolved, single-sample image is ever
                // sampled later (SokolRenderTargetBackend::GetColorTextureViewIdEXT()).
                pass.attachments.resolves[0] = MakeViewHandle(resolveAttachmentViewId);
                pass.action.colors[0].store_action = SG_STOREACTION_DONTCARE;
            }
            pass.label = currentRenderTargetCube_ != nullptr
                ? "cna_render_target_cube_pass" : "cna_render_target_pass";
        }

        sg_begin_pass(&pass);
        passActive_ = true;

        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;

        ApplyPendingViewportAndScissor();
    }

    void SokolGraphicsBackend::EndPassIfActive()
    {
        if (!passActive_) return;
        sg_end_pass();
        passActive_ = false;
    }

    void SokolGraphicsBackend::ApplyPendingViewportAndScissor()
    {
        if (!passActive_) return;

        if (currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr)
        {
            // A render target's pixel space IS logical space -- no window letterbox scaling
            // applies to it at all (GraphicsDevice::ResetViewportAndScissorForRenderTarget already
            // resets the public Viewport/ScissorRectangle to the target's own size in target-pixel
            // units on every bind, so any SetViewport/SetScissorRect this backend receives while a
            // target is bound already arrives in that same space).
            int targetWidth = 0;
            int targetHeight = 0;
            GetCurrentTargetSizeEXT(targetWidth, targetHeight);

            if (viewportSet_)
            {
                sg_apply_viewport(viewportRect_[0], viewportRect_[1],
                                  viewportRect_[2], viewportRect_[3], true);
            }
            else
            {
                sg_apply_viewport(0, 0, targetWidth, targetHeight, true);
            }

            if (scissorEnabled_)
            {
                sg_apply_scissor_rect(scissorRect_[0], scissorRect_[1],
                                      scissorRect_[2], scissorRect_[3], true);
            }
            else
            {
                sg_apply_scissor_rect(0, 0, targetWidth, targetHeight, true);
            }
            return;
        }

        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);

        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSizeEXT(logicalWidth, logicalHeight);

        // Logical-to-physical is a single uniform scale: the logical height is what a game fixes
        // and the logical width follows the window aspect, so there is no letterbox offset here.
        const double scale = (logicalHeight > 0)
            ? static_cast<double>(physicalHeight) / static_cast<double>(logicalHeight)
            : 1.0;
        auto toPhysical = [scale](int value) { return static_cast<int>(value * scale + 0.5); };

        if (viewportSet_)
        {
            sg_apply_viewport(toPhysical(viewportRect_[0]), toPhysical(viewportRect_[1]),
                              toPhysical(viewportRect_[2]), toPhysical(viewportRect_[3]), true);
        }
        else
        {
            sg_apply_viewport(0, 0, physicalWidth, physicalHeight, true);
        }

        if (scissorEnabled_)
        {
            sg_apply_scissor_rect(toPhysical(scissorRect_[0]), toPhysical(scissorRect_[1]),
                                  toPhysical(scissorRect_[2]), toPhysical(scissorRect_[3]), true);
        }
        else
        {
            sg_apply_scissor_rect(0, 0, physicalWidth, physicalHeight, true);
        }
    }

    void SokolGraphicsBackend::GetCurrentTargetSizeEXT(int& width, int& height) const
    {
        if (currentRenderTargetCube_ != nullptr)
        {
            width = currentRenderTargetCube_->GetSize();
            height = currentRenderTargetCube_->GetSize();
            return;
        }
        if (currentRenderTarget_ != nullptr)
        {
            width = currentRenderTarget_->GetWidth();
            height = currentRenderTarget_->GetHeight();
            return;
        }
        GetPhysicalSizeEXT(width, height);
    }

    bool SokolGraphicsBackend::CurrentPassHasDepthStencilAttachmentEXT() const
    {
        if (currentRenderTargetCube_ != nullptr)
            return currentRenderTargetCube_->GetDepthStencilAttachmentViewIdEXT() != 0;
        if (currentRenderTarget_ != nullptr)
            return currentRenderTarget_->GetDepthStencilAttachmentViewIdEXT() != 0;
        return true;
    }

    int SokolGraphicsBackend::CurrentPassSampleCountEXT() const
    {
        // RenderTargetCube is never multisampled yet (plan_sokol.md SOKOL-26's remaining item).
        if (currentRenderTargetCube_ != nullptr) return 1;
        if (currentRenderTarget_ != nullptr)
        {
            const int rtSampleCount = currentRenderTarget_->GetMultiSampleCount();
            return rtSampleCount > 1 ? rtSampleCount : 1;
        }
        return sampleCount_;
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- clears and present
    // ---------------------------------------------------------------------------------------

    void SokolGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        QueueClear(true, r, g, b, a, false, 1.0f, false, 0);
    }

    void SokolGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        QueueClear(true, r, g, b, a, true, depth, false, 0);
    }

    void SokolGraphicsBackend::ClearDepth(float depth)
    {
        QueueClear(false, 0, 0, 0, 0, true, depth, false, 0);
    }

    void SokolGraphicsBackend::ClearStencil(int stencil)
    {
        QueueClear(false, 0, 0, 0, 0, false, 1.0f, true, stencil);
    }

    void SokolGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        QueueClear(false, 0, 0, 0, 0, true, depth, true, stencil);
    }

    void SokolGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        QueueClear(true, r, g, b, a, false, 1.0f, true, stencil);
    }

    void SokolGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        QueueClear(true, r, g, b, a, true, depth, true, stencil);
    }

    void SokolGraphicsBackend::Present()
    {
        // Begin before ending so a frame whose only content was a Clear() still produces the pass
        // that actually performs it.
        BeginPassIfNeeded();
        EndPassIfActive();
        sg_commit();
        SDL_GL_SwapWindow(window_);
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- presentation geometry
    // ---------------------------------------------------------------------------------------

    void SokolGraphicsBackend::GetPhysicalSizeEXT(int& width, int& height) const
    {
        width = 0;
        height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
    }

    void SokolGraphicsBackend::GetLogicalSizeEXT(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSizeEXT(width, height);
            return;
        }

        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);

        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physicalHeight > 0)
        {
            width = static_cast<int>(static_cast<double>(physicalWidth) * virtualHeight_
                                     / physicalHeight + 0.5);
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : physicalWidth;
        }
    }

    void SokolGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        GetLogicalSizeEXT(width, height);
    }

    void SokolGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SokolGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void SokolGraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    bool SokolGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);
        if (physicalHeight <= 0) return false;

        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physicalHeight);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool SokolGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);
        if (physicalHeight <= 0) return false;

        const float inverseScale = static_cast<float>(physicalHeight) / static_cast<float>(virtualHeight_);
        windowX = logX * inverseScale;
        windowY = logY * inverseScale;
        return true;
    }

    SDL_Window* SokolGraphicsBackend::GetWindowInternal() const { return window_; }

    SDL_Renderer* SokolGraphicsBackend::GetRendererInternal() const { return nullptr; }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- resource creation
    // ---------------------------------------------------------------------------------------

    std::unique_ptr<ITextureBackend> SokolGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SokolTextureBackend>(data);
    }

    std::unique_ptr<ITextureCubeBackend> SokolGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        (void)surfaceFormat;  // always stored as RGBA8, matching CreateTexture
        return std::make_unique<SokolTextureCubeBackend>(size, mipMap);
    }

    std::unique_ptr<ITexture3DBackend> SokolGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        (void)surfaceFormat;  // always stored as RGBA8, matching CreateTexture
        return std::make_unique<SokolTexture3DBackend>(w, h, depth, mipMap);
    }

    std::unique_ptr<IOcclusionQueryBackend> SokolGraphicsBackend::CreateOcclusionQuery()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        return std::make_unique<SokolOcclusionQueryBackend>();
#else
        return nullptr;
#endif
    }

    std::unique_ptr<ISpriteBatchBackend> SokolGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SokolSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IVertexBufferBackend> SokolGraphicsBackend::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<SokolVertexBufferBackend>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferBackend> SokolGraphicsBackend::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<SokolIndexBufferBackend>(indexCapacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> SokolGraphicsBackend::CreateIndexBuffer32(int indexCapacity)
    {
        return std::make_unique<SokolIndexBufferBackend>(indexCapacity, true);
    }

    std::unique_ptr<IRenderTargetBackend> SokolGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // preserveContents is intentionally unused -- see this method's own header doc and
        // BeginPassIfNeeded's identical comment for why every bind already behaves that way.
        (void)preserveContents;
        if (mipMap)
            NotYetImplemented(kBackendName, "a mipmapped RenderTarget2D");
        // multiSampleCount is real as of plan_sokol.md SOKOL-26: SokolRenderTargetBackend clamps it
        // to the driver's GL_MAX_SAMPLES and allocates the matching MSAA colour (and, when a depth
        // format was requested, depth-stencil) image plus a resolve target, following sokol_gfx.h's
        // own documented offscreen-MSAA workflow. GetMultiSampleCount() reports the real, clamped
        // value, matching every other backend's own convention.

        return std::make_unique<SokolRenderTargetBackend>(w, h, depthFormat != 0, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeBackend> SokolGraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // See CreateRenderTarget2D's identical reasoning for preserveContents/multiSampleCount.
        (void)preserveContents;
        if (mipMap)
            NotYetImplemented(kBackendName, "a mipmapped RenderTargetCube");
        (void)multiSampleCount;

        return std::make_unique<SokolRenderTargetCubeBackend>(size, depthFormat != 0);
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- state
    // ---------------------------------------------------------------------------------------

    void SokolGraphicsBackend::BindSingleRenderTarget2D(SokolRenderTargetBackend* rt)
    {
        if (currentRenderTarget_ == rt && currentRenderTargetCube_ == nullptr) return;
        EndPassIfActive();
        currentRenderTarget_ = rt;
        currentRenderTargetCube_ = nullptr;
        // Any pending Clear* from before this bind belongs to whatever was previously active
        // (the back buffer, or a different target); GraphicsDevice always issues its own
        // DiscardContents Clear() AFTER this call returns (see GraphicsDevice::SetRenderTarget(s)'s
        // own sequencing), so nothing here should carry a stale pending clear into the new target.
        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;
    }

    void SokolGraphicsBackend::BindRenderTargetCubeFace(SokolRenderTargetCubeBackend* rt, int face)
    {
        if (currentRenderTargetCube_ == rt && currentRenderTargetCubeFace_ == face
            && currentRenderTarget_ == nullptr)
            return;
        EndPassIfActive();
        currentRenderTarget_ = nullptr;
        currentRenderTargetCube_ = rt;
        currentRenderTargetCubeFace_ = face;
        // See BindSingleRenderTarget2D's identical comment.
        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;
    }

    void SokolGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        BindSingleRenderTarget2D(static_cast<SokolRenderTargetBackend*>(rt));
    }

    void SokolGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                int count)
    {
        if (renderTargets == nullptr || count == 0)
        {
            BindSingleRenderTarget2D(nullptr);
            return;
        }
        if (count > 1)
            NotYetImplemented(kBackendName, "multiple render targets (MRT)");
        if (renderTargets[0].IsRenderTargetCubeFace())
        {
            BindRenderTargetCubeFace(
                static_cast<SokolRenderTargetCubeBackend*>(renderTargets[0].GetRenderTargetCube()),
                renderTargets[0].GetCubeFace());
            return;
        }

        BindSingleRenderTarget2D(
            static_cast<SokolRenderTargetBackend*>(renderTargets[0].GetRenderTarget2D()));
    }

    void SokolGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        blendColorSrc_ = colorSrcBlend;
        blendAlphaSrc_ = alphaSrcBlend;
        blendColorDst_ = colorDstBlend;
        blendAlphaDst_ = alphaDstBlend;
        blendColorFunc_ = colorBlendFunc;
        blendAlphaFunc_ = alphaBlendFunc;
        colorWriteChannels_ = writeState.colorWriteChannels[0];

        // XNA has no separate "blending on" switch: Opaque is expressed as One/Zero/Add, which is
        // exactly a disabled blend unit, so it is detected rather than requiring a second call.
        const bool isOpaque = colorSrcBlend == 0 && colorDstBlend == 1
                           && alphaSrcBlend == 0 && alphaDstBlend == 1
                           && colorBlendFunc == 0 && alphaBlendFunc == 0;
        blendEnabled_ = !isOpaque;

        // BlendState.MultiSampleMask has no sokol_gfx equivalent -- sokol exposes alpha-to-coverage
        // but no per-sample coverage mask -- so a non-default mask is intentionally not honoured
        // here. Recorded as a known gap in docs/sokol-backend.md rather than silently pretended.
    }

    void SokolGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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
        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthFunc_ = depthFunc;
        referenceStencil_ = referenceStencil;

        // Wired into Pipeline3DKey (plan_sokol.md SOKOL-23): the 3D pipeline is the only draw path
        // that ever requests stencil (XNA's SpriteBatch always runs with it disabled), so only
        // DrawColored3D's key construction reads these -- GetSpritePipeline never does.
        stencilEnabled_ = stencilEnable;
        stencilFunc_ = stencilFunc;
        stencilPass_ = stencilPass;
        stencilFail_ = stencilFail;
        stencilDepthFail_ = stencilDepthFail;
        stencilMask_ = stencilMask;
        stencilWriteMask_ = stencilWriteMask;
        twoSidedStencilMode_ = twoSidedStencilMode;
        ccwStencilFunc_ = ccwStencilFunc;
        ccwStencilPass_ = ccwStencilPass;
        ccwStencilFail_ = ccwStencilFail;
        ccwStencilDepthFail_ = ccwStencilDepthFail;
    }

    void SokolGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        scissorEnabled_ = scissorTestEnable;
        if (!scissorTestEnable && passActive_) ApplyPendingViewportAndScissor();

        // cullMode reaches the colored-3D pipeline key (SOKOL-20); the sprite pipeline is always
        // unculled, matching what XNA's own SpriteBatch does.
        cullMode_ = cullMode;
        fillMode_ = fillMode;
        // Real as of SOKOL-23: reaches Get3DPipeline's sg_depth_state.bias/bias_slope_scale, the
        // same mapping EasyGL/Vulkan use for glPolygonOffset(slopeScaleDepthBias, depthBias).
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;

        // fillMode is still accepted and ignored: sokol_gfx exposes no polygon fill mode at all
        // (WireFrame is a documented gap -- EasyGL emulates it by re-expanding triangles into
        // GL_LINES at draw time, which this backend does not do). Recorded in
        // docs/sokol-backend.md rather than throwing, since GraphicsDevice applies a
        // RasterizerState every frame regardless of what is drawn.
    }

    void SokolGraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                 int maxAnisotropy)
    {
        // SpriteBatch does not read this: DrawSpriteRunEXT receives its filter/address values
        // directly from SpriteBatch::Begin's own SamplerState. The textured/lit 3D draw paths read
        // slot 0 from here, mirroring GraphicsDevice.SamplerStates.
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        samplerSlots_[slot].filter = filter;
        samplerSlots_[slot].addressU = addressU;
        samplerSlots_[slot].addressV = addressV;
        samplerSlots_[slot].maxAnisotropy = maxAnisotropy;
    }

    void SokolGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void SokolGraphicsBackend::SetReferenceStencil(int value) { referenceStencil_ = value; }

    void SokolGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        if (passActive_) ApplyPendingViewportAndScissor();
    }

    void SokolGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportSet_ = true;
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;

        // sokol_gfx's viewport call carries no depth range; a non-default Viewport.MinDepth/
        // MaxDepth would need the 3D projection path to fold it into the pipeline instead.
        (void)minDepth; (void)maxDepth;

        if (passActive_) ApplyPendingViewportAndScissor();
    }

    void SokolGraphicsBackend::SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }

    void SokolGraphicsBackend::SetBlendEnabled(bool enabled) { blendEnabled_ = enabled; }

    void SokolGraphicsBackend::SetDepthWriteEnabled(bool enabled) { depthWriteEnabled_ = enabled; }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- caches and sprite drawing
    // ---------------------------------------------------------------------------------------

    std::uint32_t SokolGraphicsBackend::GetSpritePipeline()
    {
        // Needed here too, not just for the 3D pipelines -- a SpriteBatch draw into a depth-less
        // render target hits the exact same sokol_gfx pipeline/pass pixel-format mismatch otherwise.
        const bool hasDepthAttachment = CurrentPassHasDepthStencilAttachmentEXT();
        // Likewise: a SpriteBatch draw into a multisampled RenderTarget2D hits the identical
        // sample_count mismatch the 3D pipelines guard against (plan_sokol.md SOKOL-26).
        const int sampleCount = CurrentPassSampleCountEXT();

        const PipelineKey key{
            blendColorSrc_, blendAlphaSrc_, blendColorDst_, blendAlphaDst_,
            blendColorFunc_, blendAlphaFunc_, colorWriteChannels_,
            blendEnabled_, depthTestEnabled_, depthWriteEnabled_, depthFunc_, hasDepthAttachment,
            sampleCount};

        if (const auto found = pipelineCache_.find(key); found != pipelineCache_.end())
            return found->second;

        sg_pipeline_desc desc = {};
        desc.shader = MakeShaderHandle(spriteShaderId_);
        desc.layout.buffers[0].stride = static_cast<int>(sizeof(SokolSpriteBatchBackend::Vertex));
        desc.layout.attrs[ATTR_cna_sprite_position].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_cna_sprite_position].offset = 0;
        desc.layout.attrs[ATTR_cna_sprite_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_cna_sprite_texcoord0].offset = 2 * static_cast<int>(sizeof(float));
        desc.layout.attrs[ATTR_cna_sprite_color0].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_cna_sprite_color0].offset = 4 * static_cast<int>(sizeof(float));
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.sample_count = key.sampleCount;
        if (key.hasDepthAttachment)
        {
            desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            desc.depth.compare = key.depthTestEnabled ? ToCompareFunc(key.depthFunc) : SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = key.depthTestEnabled && key.depthWriteEnabled;
        }
        else
        {
            // sokol_gfx requires compare=ALWAYS/NEVER and write_enabled=false when the pipeline's
            // own depth.pixel_format is NONE -- a depth test/write request is simply impossible
            // to honour with no depth attachment, matching real hardware.
            desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
            desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = false;
        }
        desc.color_count = 1;
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.colors[0].write_mask = ToColorMask(key.colorWriteChannels);
        desc.colors[0].blend.enabled = key.blendEnabled;
        desc.colors[0].blend.src_factor_rgb = ToBlendFactor(key.colorSrcBlend);
        desc.colors[0].blend.dst_factor_rgb = ToBlendFactor(key.colorDstBlend);
        desc.colors[0].blend.op_rgb = ToBlendOp(key.colorBlendFunc);
        desc.colors[0].blend.src_factor_alpha = ToBlendFactor(key.alphaSrcBlend);
        desc.colors[0].blend.dst_factor_alpha = ToBlendFactor(key.alphaDstBlend);
        desc.colors[0].blend.op_alpha = ToBlendOp(key.alphaBlendFunc);
        desc.blend_color.r = blendFactor_[0];
        desc.blend_color.g = blendFactor_[1];
        desc.blend_color.b = blendFactor_[2];
        desc.blend_color.a = blendFactor_[3];
        desc.label = "cna_sprite_pipeline";

        const std::uint32_t pipelineId = sg_make_pipeline(&desc).id;
        if (sg_query_pipeline_state(MakePipelineHandle(pipelineId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: sprite pipeline creation failed");

        pipelineCache_.emplace(key, pipelineId);
        return pipelineId;
    }

    std::uint32_t SokolGraphicsBackend::GetSampler(int filter, int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        const SamplerKey key{filter, addressU, addressV, maxAnisotropy};
        if (const auto found = samplerCache_.find(key); found != samplerCache_.end())
            return found->second;

        sg_filter minFilter = SG_FILTER_LINEAR;
        sg_filter magFilter = SG_FILTER_LINEAR;
        sg_filter mipFilter = SG_FILTER_LINEAR;
        bool anisotropic = false;
        ToFilters(filter, minFilter, magFilter, mipFilter, anisotropic);

        sg_sampler_desc desc = {};
        desc.min_filter = minFilter;
        desc.mag_filter = magFilter;
        desc.mipmap_filter = mipFilter;
        desc.wrap_u = ToWrap(addressU);
        desc.wrap_v = ToWrap(addressV);
        desc.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        desc.max_anisotropy = anisotropic
            ? static_cast<std::uint32_t>(std::clamp(maxAnisotropy, 1, 16))
            : 1u;
        desc.label = "cna_sampler";

        const std::uint32_t samplerId = sg_make_sampler(&desc).id;
        if (sg_query_sampler_state(MakeSamplerHandle(samplerId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: sampler creation failed");

        samplerCache_.emplace(key, samplerId);
        return samplerId;
    }

    void SokolGraphicsBackend::DrawSpriteRunEXT(
        const ITextureBackend& texture,
        const std::vector<SokolSpriteBatchBackend::Vertex>& vertices,
        const Matrix& transform,
        int filter, int addressU, int addressV)
    {
        if (vertices.empty()) return;

        const std::uint32_t textureViewId = ResolveSampledTextureViewId(texture, "SpriteBatch");

        BeginPassIfNeeded();

        const std::size_t byteCount = vertices.size() * sizeof(SokolSpriteBatchBackend::Vertex);
        const sg_buffer vertexBuffer = MakeBufferHandle(spriteVertexBufferId_);
        if (sg_query_buffer_will_overflow(vertexBuffer, byteCount))
        {
            throw std::runtime_error(
                "Sokol backend: exceeded the per-frame sprite capacity of "
                + std::to_string(kMaxSpriteQuads) + " quads");
        }

        sg_range vertexRange{};
        vertexRange.ptr = vertices.data();
        vertexRange.size = byteCount;
        const int vertexOffset = sg_append_buffer(vertexBuffer, &vertexRange);

        int logicalWidth = 0;
        int logicalHeight = 0;
        // XNA builds the SpriteBatch projection from Viewport.Width/Height, which
        // GraphicsDevice::ResetViewportAndScissorForRenderTarget already reset to the bound
        // target's own pixel size on bind -- so this needs the TARGET's size, not the window's
        // letterboxed logical size, whenever a render target is active.
        if (currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr)
            GetCurrentTargetSizeEXT(logicalWidth, logicalHeight);
        else
            GetLogicalSizeEXT(logicalWidth, logicalHeight);
        if (viewportSet_ && viewportRect_[2] > 0 && viewportRect_[3] > 0)
        {
            // A custom sub-viewport makes sprite coordinates viewport-local rather than
            // window/target-local, on top of either size above.
            logicalWidth = viewportRect_[2];
            logicalHeight = viewportRect_[3];
        }
        if (logicalWidth <= 0 || logicalHeight <= 0) return;

        const Matrix ortho = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth),
            static_cast<float>(logicalHeight), 0.0f,
            -1.0f, 1.0f);
        const Matrix combined = transform * ortho;

        cna_sprite_vs_params_t params{};
        combined.ToColumnMajor(params.mvp);

        sg_apply_pipeline(MakePipelineHandle(GetSpritePipeline()));

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = vertexBuffer;
        bindings.vertex_buffer_offsets[0] = vertexOffset;
        bindings.index_buffer = MakeBufferHandle(spriteIndexBufferId_);
        bindings.views[VIEW_cna_tex] = MakeViewHandle(textureViewId);
        bindings.samplers[SMP_cna_smp] = MakeSamplerHandle(GetSampler(filter, addressU, addressV, 1));
        sg_apply_bindings(&bindings);

        sg_range uniformRange{};
        uniformRange.ptr = &params;
        uniformRange.size = sizeof(params);
        sg_apply_uniforms(UB_cna_sprite_vs_params, &uniformRange);

        const int quadCount = static_cast<int>(vertices.size()) / kSpriteVerticesPerQuad;
        sg_draw(0, quadCount * kSpriteIndicesPerQuad, 1);
    }

    // ---------------------------------------------------------------------------------------
    // SokolGraphicsBackend -- readback, 3D stubs, capabilities
    // ---------------------------------------------------------------------------------------

    void SokolGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("Sokol backend: ReadBackbuffer called with an empty region");

        // Real XNA's GraphicsDevice.GetBackBufferData always reads the actual presented window
        // surface, never whatever RenderTarget2D happens to be currently bound -- and this
        // implementation reads whatever pass is active, which would be the wrong surface while a
        // target is bound. Refusing loudly beats returning the wrong pixels silently; matching the
        // real semantic (temporarily unbinding, reading, then rebinding) is plan_sokol.md SOKOL-25b.
        if (currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr)
        {
            NotYetImplemented(kBackendName,
                "GetBackBufferData while a RenderTarget2D or RenderTargetCube face is bound");
        }

        // BeginPassIfNeeded() first, for the same reason Present() does it: a Clear() only records
        // a pending pass action, so a Clear()-then-read-back sequence with nothing drawn in
        // between would otherwise read whatever the previous frame's buffer swap left behind.
        // Ending the pass then flushes everything, and glFinish waits for the driver to actually
        // produce the pixels. sg_commit() is deliberately not called -- it would reset the sprite
        // streaming buffer's append offset mid-frame, and a readback is not the end of a frame.
        BeginPassIfNeeded();
        EndPassIfActive();
        glFinish();

        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);

        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSizeEXT(logicalWidth, logicalHeight);

        const double scale = (logicalHeight > 0)
            ? static_cast<double>(physicalHeight) / static_cast<double>(logicalHeight)
            : 1.0;
        const int physicalX = static_cast<int>(x * scale + 0.5);
        const int physicalY = static_cast<int>(y * scale + 0.5);
        const int physicalW = std::max(1, static_cast<int>(w * scale + 0.5));
        const int physicalH = std::max(1, static_cast<int>(h * scale + 0.5));

        // glReadPixels' origin is the bottom-left of the framebuffer, while every CNA caller works
        // in top-left game coordinates, so the region is mirrored on the way in and the rows are
        // reversed on the way out.
        const int glY = physicalHeight - (physicalY + physicalH);
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(physicalW) * physicalH * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(physicalX, glY, physicalW, physicalH, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

        // Nearest-sample the physical pixels back down to the requested logical region, which is a
        // no-op whenever the window is not scaled (the usual case, and every pixel test's case).
        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = std::min(physicalH - 1,
                                           static_cast<int>((h - 1 - row) * scale + 0.5));
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = std::min(physicalW - 1,
                                                  static_cast<int>(column * scale + 0.5));
                const std::uint8_t* source =
                    raw.data() + (static_cast<std::size_t>(sourceRow) * physicalW + sourceColumn) * 4u;
                std::uint8_t* destination =
                    pixels + (static_cast<std::size_t>(row) * w + column) * 4u;
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                destination[3] = source[3];
            }
        }
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
        NotYetImplemented(kBackendName, "ReadBackbuffer on a non-GL CNA_SOKOL_API");
#endif
    }

    std::uint32_t SokolGraphicsBackend::Get3DPipeline(const Pipeline3DKey& key)
    {
        if (const auto found = pipeline3dCache_.find(key); found != pipeline3dCache_.end())
            return found->second;

        sg_pipeline_desc desc = {};
        switch (key.kind)
        {
            case Shader3DKind::Colored:
                desc.shader = MakeShaderHandle(colored3dShaderId_);
                desc.layout.attrs[ATTR_cna_colored3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_colored3d_position].offset = key.positionOffset;
                if (key.colorOffset >= 0)
                {
                    desc.layout.attrs[ATTR_cna_colored3d_color0].format =
                        static_cast<sg_vertex_format>(key.colorFormat);
                    desc.layout.attrs[ATTR_cna_colored3d_color0].offset = key.colorOffset;
                }
                // When the declaration carries no Color element the slot is left INVALID, which
                // sokol_gfx permits because the remaining attribute slots stay continuous. The
                // shader still reads color0, but flags.x is 0 for that case, so the unbound
                // attribute is multiplied out.
                break;

            case Shader3DKind::Textured:
                desc.shader = MakeShaderHandle(textured3dShaderId_);
                desc.layout.attrs[ATTR_cna_textured3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_textured3d_position].offset = key.positionOffset;
                desc.layout.attrs[ATTR_cna_textured3d_texcoord0].format =
                    static_cast<sg_vertex_format>(key.texCoordFormat);
                desc.layout.attrs[ATTR_cna_textured3d_texcoord0].offset = key.texCoordOffset;
                if (key.colorOffset >= 0)
                {
                    desc.layout.attrs[ATTR_cna_textured3d_color0].format =
                        static_cast<sg_vertex_format>(key.colorFormat);
                    desc.layout.attrs[ATTR_cna_textured3d_color0].offset = key.colorOffset;
                }
                break;

            case Shader3DKind::Lit:
                // Normal and TexCoord are both required in the declaration for this kind (checked
                // in DrawColored3D before a key is ever built) -- every built-in vertex type that
                // carries a Normal also carries a TexCoord, so this is not a real restriction on
                // any of them, and it keeps attribute slots 0..2 always continuous. Only color0
                // (slot 3, the trailing one) may legitimately be absent: sokol_gfx only rejects a
                // VALID slot following an INVALID one, and a trailing gap is not that.
                desc.shader = MakeShaderHandle(lit3dShaderId_);
                desc.layout.attrs[ATTR_cna_lit3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_lit3d_position].offset = key.positionOffset;
                desc.layout.attrs[ATTR_cna_lit3d_normal].format =
                    static_cast<sg_vertex_format>(key.normalFormat);
                desc.layout.attrs[ATTR_cna_lit3d_normal].offset = key.normalOffset;
                desc.layout.attrs[ATTR_cna_lit3d_texcoord0].format =
                    static_cast<sg_vertex_format>(key.texCoordFormat);
                desc.layout.attrs[ATTR_cna_lit3d_texcoord0].offset = key.texCoordOffset;
                if (key.colorOffset >= 0)
                {
                    desc.layout.attrs[ATTR_cna_lit3d_color0].format =
                        static_cast<sg_vertex_format>(key.colorFormat);
                    desc.layout.attrs[ATTR_cna_lit3d_color0].offset = key.colorOffset;
                }
                break;
        }
        desc.layout.buffers[0].stride = key.stride;

        desc.index_type = static_cast<sg_index_type>(key.indexType);
        desc.primitive_type = static_cast<sg_primitive_type>(key.primitiveType);
        desc.cull_mode = ToCullMode(key.cullMode);
        // Pinned rather than left to sokol's default, so ToCullMode's mapping is anchored to a
        // stated convention instead of to whatever upstream happens to default to.
        desc.face_winding = SG_FACEWINDING_CW;
        desc.sample_count = key.sampleCount;
        if (key.hasDepthAttachment)
        {
            desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            desc.depth.compare = key.depthTestEnabled ? ToCompareFunc(key.depthFunc) : SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = key.depthTestEnabled && key.depthWriteEnabled;

            // CreateRenderTarget2D always allocates the combined SG_PIXELFORMAT_DEPTH_STENCIL
            // format regardless of the requested DepthFormat (docs/sokol-backend.md), so a real
            // stencil plane exists whenever hasDepthAttachment does -- there is no depth-only case
            // to distinguish here, unlike D3D/Vulkan backends that honour DepthFormat precisely.
            desc.stencil.enabled = key.stencilEnabled;
            if (key.stencilEnabled)
            {
                const sg_stencil_face_state front{
                    ToCompareFunc(key.stencilFunc), ToStencilOp(key.stencilFail),
                    ToStencilOp(key.stencilDepthFail), ToStencilOp(key.stencilPass)};
                desc.stencil.front = front;
                desc.stencil.back = key.twoSidedStencilMode
                    ? sg_stencil_face_state{ToCompareFunc(key.ccwStencilFunc), ToStencilOp(key.ccwStencilFail),
                                            ToStencilOp(key.ccwStencilDepthFail), ToStencilOp(key.ccwStencilPass)}
                    : front;
                desc.stencil.read_mask = static_cast<std::uint8_t>(key.stencilMask & 0xFF);
                desc.stencil.write_mask = static_cast<std::uint8_t>(key.stencilWriteMask & 0xFF);
                desc.stencil.ref = static_cast<std::uint8_t>(key.referenceStencil & 0xFF);
            }
        }
        else
        {
            // See GetSpritePipeline's identical branch: sokol_gfx requires compare=ALWAYS and
            // write_enabled=false whenever the pipeline's own depth.pixel_format is NONE. No real
            // stencil plane exists there either (see the comment above), so stencil stays disabled.
            desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
            desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = false;
            desc.stencil.enabled = false;
        }
        // RasterizerState.DepthBias/SlopeScaleDepthBias -- meaningless with no depth attachment,
        // but harmless to set regardless (sokol_gfx places no constraint on these two fields when
        // depth.pixel_format is NONE, unlike compare/write_enabled).
        desc.depth.bias = key.depthBias;
        desc.depth.bias_slope_scale = key.slopeScaleDepthBias;
        desc.color_count = 1;
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.colors[0].write_mask = ToColorMask(key.colorWriteChannels);
        desc.colors[0].blend.enabled = key.blendEnabled;
        desc.colors[0].blend.src_factor_rgb = ToBlendFactor(key.colorSrcBlend);
        desc.colors[0].blend.dst_factor_rgb = ToBlendFactor(key.colorDstBlend);
        desc.colors[0].blend.op_rgb = ToBlendOp(key.colorBlendFunc);
        desc.colors[0].blend.src_factor_alpha = ToBlendFactor(key.alphaSrcBlend);
        desc.colors[0].blend.dst_factor_alpha = ToBlendFactor(key.alphaDstBlend);
        desc.colors[0].blend.op_alpha = ToBlendOp(key.alphaBlendFunc);
        desc.blend_color.r = blendFactor_[0];
        desc.blend_color.g = blendFactor_[1];
        desc.blend_color.b = blendFactor_[2];
        desc.blend_color.a = blendFactor_[3];
        desc.label = "cna_3d_pipeline";

        const std::uint32_t pipelineId = sg_make_pipeline(&desc).id;
        if (sg_query_pipeline_state(MakePipelineHandle(pipelineId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol backend: 3D pipeline creation failed");

        pipeline3dCache_.emplace(key, pipelineId);
        return pipelineId;
    }

    SokolTextureBackend& SokolGraphicsBackend::GetDefaultWhiteTexture()
    {
        if (!defaultWhiteTexture_)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            defaultWhiteTexture_ = std::make_unique<SokolTextureBackend>(white);
        }
        return *defaultWhiteTexture_;
    }

    void SokolGraphicsBackend::DrawColored3D(const IVertexBufferBackend& vbIn,
                                             const IIndexBufferBackend* ibIn,
                                             const Matrix& world,
                                             const Matrix& view,
                                             const Matrix& projection,
                                             PrimitiveType primitive,
                                             int primitiveCount,
                                             const GpuDrawParams& params)
    {
        if (primitiveCount <= 0) return;

        // Everything this backend cannot shade yet is refused here rather than rendered as an
        // untextured, unlit approximation that would look plausible and be wrong. Plain texturing
        // (SOKOL-21) and lighting (SOKOL-21) are real below; dual-texture, environment mapping,
        // skinning and PBR are each a distinct stock effect with their own vertex/uniform contract
        // this backend does not implement yet.
        if (params.dualTexture || params.envMapping || params.skinned || params.pbr)
        {
            NotYetImplemented(kBackendName,
                "dual-texture/environment-mapped/skinned/PBR 3D draws");
        }
        if (params.customEffectBackend != nullptr)
            NotYetImplemented(kBackendName, "custom ShaderEffect draws");
        if (params.instanceCount > 1)
            NotYetImplemented(kBackendName, "instanced draws");

        const auto& vb = static_cast<const SokolVertexBufferBackend&>(vbIn);
        if (vb.GetBufferIdEXT() == 0 || vb.GetVertexCount() <= 0) return;

        const VertexDeclaration* declaration = vb.GetDeclarationEXT();

        // Resolved before the pipeline key is built: the index width is part of the pipeline.
        const SokolIndexBufferBackend* ib = nullptr;
        if (ibIn != nullptr)
        {
            ib = static_cast<const SokolIndexBufferBackend*>(ibIn);
            if (ib->GetBufferIdEXT() == 0 || ib->GetIndexCount() <= 0) return;
        }

        const Shader3DKind kind = params.lightingEnabled ? Shader3DKind::Lit
                                 : params.textureEnabled  ? Shader3DKind::Textured
                                                           : Shader3DKind::Colored;

        Pipeline3DKey key{};
        key.kind = kind;
        key.colorSrcBlend = blendColorSrc_;
        key.alphaSrcBlend = blendAlphaSrc_;
        key.colorDstBlend = blendColorDst_;
        key.alphaDstBlend = blendAlphaDst_;
        key.colorBlendFunc = blendColorFunc_;
        key.alphaBlendFunc = blendAlphaFunc_;
        key.colorWriteChannels = colorWriteChannels_;
        key.blendEnabled = blendEnabled_;
        key.depthTestEnabled = depthTestEnabled_;
        key.depthWriteEnabled = depthWriteEnabled_;
        key.depthFunc = depthFunc_;
        key.cullMode = cullMode_;
        key.hasDepthAttachment = CurrentPassHasDepthStencilAttachmentEXT();
        key.sampleCount = CurrentPassSampleCountEXT();
        key.stencilEnabled = stencilEnabled_;
        key.stencilFunc = stencilFunc_;
        key.stencilPass = stencilPass_;
        key.stencilFail = stencilFail_;
        key.stencilDepthFail = stencilDepthFail_;
        key.ccwStencilFunc = ccwStencilFunc_;
        key.ccwStencilPass = ccwStencilPass_;
        key.ccwStencilFail = ccwStencilFail_;
        key.ccwStencilDepthFail = ccwStencilDepthFail_;
        key.twoSidedStencilMode = twoSidedStencilMode_;
        key.stencilMask = stencilMask_;
        key.stencilWriteMask = stencilWriteMask_;
        key.referenceStencil = referenceStencil_;
        key.depthBias = depthBias_;
        key.slopeScaleDepthBias = slopeScaleDepthBias_;
        key.primitiveType = static_cast<int>(ToPrimitiveType(primitive));
        key.indexType = static_cast<int>(
            ib == nullptr ? SG_INDEXTYPE_NONE
                          : (ib->IsThirtyTwoBit() ? SG_INDEXTYPE_UINT32 : SG_INDEXTYPE_UINT16));
        key.positionOffset = -1;
        key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.colorOffset = -1;
        key.colorFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.texCoordOffset = -1;
        key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.normalOffset = -1;
        key.normalFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);

        // VertexBuffer's own NOXNA-tagged auto-detect constructor (VertexBuffer(device, count),
        // used by GraphicsDevice::SetVertexBuffer callers who never pass an explicit
        // VertexDeclaration) stores a default-constructed, ELEMENT-LESS VertexDeclaration --
        // UploadValidatedData still calls SetVertexDeclaration with it before every real upload
        // (GFX-043), so this backend sees hasDeclaration()==true with nothing usable inside. That
        // is indistinguishable from "no declaration at all" for this method's purposes, and falls
        // into the identical stride-based fallback below rather than failing "no usable Position
        // element" on a buffer that plainly has one -- just not described via VertexDeclaration.
        if (declaration != nullptr && !declaration->GetVertexElements().empty())
        {
            key.stride = declaration->getVertexStrideProperty();
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            for (const VertexElement& element : declaration->GetVertexElements())
            {
                const sg_vertex_format format = ToVertexFormat(element.getVertexElementFormatProperty());
                if (format == SG_VERTEXFORMAT_INVALID) continue;
                // Only usage index 0 participates: every shader here declares at most one input
                // per usage, so a second set (e.g. a second TEXCOORD for lightmapping) would have
                // nowhere to go.
                if (element.getUsageIndexProperty() != 0) continue;

                const VertexElementUsage usage = element.getVertexElementUsageProperty();
                if (usage == VertexElementUsage::Position && key.positionOffset < 0)
                {
                    key.positionOffset = element.getOffsetProperty();
                    key.positionFormat = static_cast<int>(format);
                }
                else if (usage == VertexElementUsage::Color && key.colorOffset < 0)
                {
                    key.colorOffset = element.getOffsetProperty();
                    key.colorFormat = static_cast<int>(format);
                }
                else if (usage == VertexElementUsage::TextureCoordinate && key.texCoordOffset < 0)
                {
                    key.texCoordOffset = element.getOffsetProperty();
                    key.texCoordFormat = static_cast<int>(format);
                }
                else if (usage == VertexElementUsage::Normal && key.normalOffset < 0)
                {
                    key.normalOffset = element.getOffsetProperty();
                    key.normalFormat = static_cast<int>(format);
                }
            }
        }
        else
        {
            // Reached with no declaration at all (GraphicsDevice's typed, no-explicit-declaration
            // DrawUserPrimitives()/DrawUserIndexedPrimitives() overloads for VertexPositionColor,
            // VertexPositionTexture, VertexPositionColorTexture and VertexPositionNormalTexture --
            // and their *Colored convenience wrappers -- never call SetVertexDeclaration on the
            // temp buffer at all) OR with an element-less one (VertexBuffer's NOXNA auto-detect
            // constructor, see the comment above). Either way there is no usable per-call
            // declaration to read, only the upload stride, which uniquely identifies which of the
            // four built-in layouts this is.
            switch (vb.GetStrideEXT())
            {
                case 16:  // PositionColorStream: float3 position @0, ubyte4n color @12.
                    if (kind != Shader3DKind::Colored)
                        NotYetImplemented(kBackendName,
                            "a textured or lit 3D draw from an undeclared VertexPositionColor "
                            "buffer");
                    key.stride = 16;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.colorOffset = 12;
                    key.colorFormat = static_cast<int>(SG_VERTEXFORMAT_UBYTE4N);
                    break;
                case 20:  // PositionTextureStream: float3 position @0, float2 texcoord @12.
                    if (kind != Shader3DKind::Textured)
                        NotYetImplemented(kBackendName,
                            "a colored or lit 3D draw from an undeclared VertexPositionTexture "
                            "buffer");
                    key.stride = 20;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.texCoordOffset = 12;
                    key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT2);
                    break;
                case 24:  // PositionColorTextureStream: float3 position @0, ubyte4n color @12,
                          // float2 texcoord @16.
                    if (kind != Shader3DKind::Textured)
                        NotYetImplemented(kBackendName,
                            "a colored or lit 3D draw from an undeclared VertexPositionColorTexture "
                            "buffer");
                    key.stride = 24;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.colorOffset = 12;
                    key.colorFormat = static_cast<int>(SG_VERTEXFORMAT_UBYTE4N);
                    key.texCoordOffset = 16;
                    key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT2);
                    break;
                case 32:  // PositionNormalTextureStream: float3 position @0, float3 normal @12,
                          // float2 texcoord @24.
                    if (kind != Shader3DKind::Lit)
                        NotYetImplemented(kBackendName,
                            "a colored or plain-textured 3D draw from an undeclared "
                            "VertexPositionNormalTexture buffer");
                    key.stride = 32;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.normalOffset = 12;
                    key.normalFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.texCoordOffset = 24;
                    key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT2);
                    break;
                default:
                    NotYetImplemented(kBackendName,
                        "a 3D draw from a VertexBuffer with no VertexDeclaration and an "
                        "unrecognised stride");
            }
        }

        if (key.positionOffset < 0 || key.stride <= 0)
        {
            NotYetImplemented(kBackendName,
                "a 3D draw from a VertexDeclaration with no usable Position element");
        }
        if (kind == Shader3DKind::Textured && key.texCoordOffset < 0)
        {
            NotYetImplemented(kBackendName,
                "a textured 3D draw from a VertexDeclaration with no TextureCoordinate element");
        }
        if (kind == Shader3DKind::Lit && (key.normalOffset < 0 || key.texCoordOffset < 0))
        {
            // Both are required, not just Normal: see Get3DPipeline's own comment on why the Lit
            // attribute slots (position, normal, texcoord0, color0) must stay a continuous prefix,
            // and why this is not a real restriction on any of CNA's built-in vertex types.
            NotYetImplemented(kBackendName,
                "a lit 3D draw from a VertexDeclaration with no Normal or no TextureCoordinate "
                "element");
        }

        // Resolved before the pass begins: a Textured draw needs a real texture, and a Lit draw
        // needs one only when actually textured (the white fallback otherwise). Accepts a
        // render-target-as-texture the same way the SpriteBatch path does.
        bool hasTexture = false;
        std::uint32_t textureViewId = 0;
        if (kind == Shader3DKind::Textured || (kind == Shader3DKind::Lit && params.textureEnabled))
        {
            if (params.texture0 != nullptr)
            {
                textureViewId = ResolveSampledTextureViewId(*params.texture0, "a textured 3D draw");
                hasTexture = true;
            }
            else if (kind == Shader3DKind::Textured)
            {
                throw std::runtime_error(
                    "Sokol backend: TextureEnabled draw with no texture bound");
            }
        }
        if (!hasTexture && kind == Shader3DKind::Lit)
        {
            textureViewId = GetDefaultWhiteTexture().GetViewIdEXT();
            hasTexture = true;
        }

        BeginPassIfNeeded();

        sg_apply_pipeline(MakePipelineHandle(Get3DPipeline(key)));

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = MakeBufferHandle(vb.GetBufferIdEXT());
        // baseVertex is folded into the buffer offset rather than passed to sg_draw_ex, so the
        // same code path serves both the indexed and non-indexed shapes.
        bindings.vertex_buffer_offsets[0] = params.baseVertex * key.stride;
        if (ib != nullptr)
            bindings.index_buffer = MakeBufferHandle(ib->GetBufferIdEXT());
        if (hasTexture)
        {
            const SamplerSlotState& sampler = samplerSlots_[0];
            bindings.views[VIEW_cna_tex] = MakeViewHandle(textureViewId);
            bindings.samplers[SMP_cna_smp] = MakeSamplerHandle(
                GetSampler(sampler.filter, sampler.addressU, sampler.addressV,
                          sampler.maxAnisotropy));
        }
        sg_apply_bindings(&bindings);

        const Matrix worldViewProjection = world * view * projection;
        const bool vertexColorActive = params.vertexColorEnabled && key.colorOffset >= 0;

        switch (kind)
        {
            case Shader3DKind::Colored:
            {
                cna_colored3d_vs_params_t uniforms{};
                worldViewProjection.ToColumnMajor(uniforms.mvp);
                uniforms.diffuse[0] = params.diffuseColor[0];
                uniforms.diffuse[1] = params.diffuseColor[1];
                uniforms.diffuse[2] = params.diffuseColor[2];
                uniforms.diffuse[3] = params.diffuseColor[3];
                uniforms.flags[0] = vertexColorActive ? 1.0f : 0.0f;

                sg_range range{&uniforms, sizeof(uniforms)};
                sg_apply_uniforms(UB_cna_colored3d_vs_params, &range);
                break;
            }
            case Shader3DKind::Textured:
            {
                cna_textured3d_vs_params_t vsUniforms{};
                worldViewProjection.ToColumnMajor(vsUniforms.mvp);
                vsUniforms.diffuse[0] = params.diffuseColor[0];
                vsUniforms.diffuse[1] = params.diffuseColor[1];
                vsUniforms.diffuse[2] = params.diffuseColor[2];
                vsUniforms.diffuse[3] = params.diffuseColor[3];
                vsUniforms.flags[0] = vertexColorActive ? 1.0f : 0.0f;
                vsUniforms.alphaTest[0] = params.alphaTest[0];
                vsUniforms.alphaTest[1] = params.alphaTest[1];
                vsUniforms.alphaTest[2] = params.alphaTest[2];
                vsUniforms.alphaTest[3] = params.alphaTest[3];
                vsUniforms.fogVector[0] = params.fogVector[0];
                vsUniforms.fogVector[1] = params.fogVector[1];
                vsUniforms.fogVector[2] = params.fogVector[2];
                vsUniforms.fogVector[3] = params.fogVector[3];

                sg_range vsRange{&vsUniforms, sizeof(vsUniforms)};
                sg_apply_uniforms(UB_cna_textured3d_vs_params, &vsRange);

                cna_textured3d_fs_params_t fsUniforms{};
                fsUniforms.fogColor[0] = params.fogColor[0];
                fsUniforms.fogColor[1] = params.fogColor[1];
                fsUniforms.fogColor[2] = params.fogColor[2];

                sg_range fsRange{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_textured3d_fs_params, &fsRange);
                break;
            }
            case Shader3DKind::Lit:
            {
                const Matrix normalMatrix = Matrix::Transpose(Matrix::Invert(world));

                cna_lit3d_vs_params_t vsUniforms{};
                worldViewProjection.ToColumnMajor(vsUniforms.mvp);
                world.ToColumnMajor(vsUniforms.world);
                normalMatrix.ToColumnMajor(vsUniforms.normalMatrix);
                vsUniforms.flags[0] = vertexColorActive ? 1.0f : 0.0f;
                vsUniforms.alphaTest[0] = params.alphaTest[0];
                vsUniforms.alphaTest[1] = params.alphaTest[1];
                vsUniforms.alphaTest[2] = params.alphaTest[2];
                vsUniforms.alphaTest[3] = params.alphaTest[3];
                vsUniforms.fogVector[0] = params.fogVector[0];
                vsUniforms.fogVector[1] = params.fogVector[1];
                vsUniforms.fogVector[2] = params.fogVector[2];
                vsUniforms.fogVector[3] = params.fogVector[3];

                sg_range vsRange{&vsUniforms, sizeof(vsUniforms)};
                sg_apply_uniforms(UB_cna_lit3d_vs_params, &vsRange);

                cna_lit3d_fs_params_t fsUniforms{};
                fsUniforms.diffuse[0] = params.diffuseColor[0];
                fsUniforms.diffuse[1] = params.diffuseColor[1];
                fsUniforms.diffuse[2] = params.diffuseColor[2];
                fsUniforms.diffuse[3] = params.diffuseColor[3];
                fsUniforms.ambient[0] = params.ambientColor[0];
                fsUniforms.ambient[1] = params.ambientColor[1];
                fsUniforms.ambient[2] = params.ambientColor[2];
                fsUniforms.light0Dir[0] = params.light0Dir[0];
                fsUniforms.light0Dir[1] = params.light0Dir[1];
                fsUniforms.light0Dir[2] = params.light0Dir[2];
                fsUniforms.light0Diffuse[0] = params.light0Diffuse[0];
                fsUniforms.light0Diffuse[1] = params.light0Diffuse[1];
                fsUniforms.light0Diffuse[2] = params.light0Diffuse[2];
                fsUniforms.light0Specular[0] = params.light0Specular[0];
                fsUniforms.light0Specular[1] = params.light0Specular[1];
                fsUniforms.light0Specular[2] = params.light0Specular[2];
                fsUniforms.light1Dir[0] = params.light1Dir[0];
                fsUniforms.light1Dir[1] = params.light1Dir[1];
                fsUniforms.light1Dir[2] = params.light1Dir[2];
                fsUniforms.light1Diffuse[0] = params.light1Diffuse[0];
                fsUniforms.light1Diffuse[1] = params.light1Diffuse[1];
                fsUniforms.light1Diffuse[2] = params.light1Diffuse[2];
                fsUniforms.light1Specular[0] = params.light1Specular[0];
                fsUniforms.light1Specular[1] = params.light1Specular[1];
                fsUniforms.light1Specular[2] = params.light1Specular[2];
                fsUniforms.light2Dir[0] = params.light2Dir[0];
                fsUniforms.light2Dir[1] = params.light2Dir[1];
                fsUniforms.light2Dir[2] = params.light2Dir[2];
                fsUniforms.light2Diffuse[0] = params.light2Diffuse[0];
                fsUniforms.light2Diffuse[1] = params.light2Diffuse[1];
                fsUniforms.light2Diffuse[2] = params.light2Diffuse[2];
                fsUniforms.light2Specular[0] = params.light2Specular[0];
                fsUniforms.light2Specular[1] = params.light2Specular[1];
                fsUniforms.light2Specular[2] = params.light2Specular[2];
                fsUniforms.specularColorAndPower[0] = params.specularColor[0];
                fsUniforms.specularColorAndPower[1] = params.specularColor[1];
                fsUniforms.specularColorAndPower[2] = params.specularColor[2];
                fsUniforms.specularColorAndPower[3] = params.specularPower;
                fsUniforms.eyePosition[0] = params.eyePositionWorld[0];
                fsUniforms.eyePosition[1] = params.eyePositionWorld[1];
                fsUniforms.eyePosition[2] = params.eyePositionWorld[2];
                fsUniforms.emissiveColor[0] = params.emissiveColor[0];
                fsUniforms.emissiveColor[1] = params.emissiveColor[1];
                fsUniforms.emissiveColor[2] = params.emissiveColor[2];
                fsUniforms.fogColor[0] = params.fogColor[0];
                fsUniforms.fogColor[1] = params.fogColor[1];
                fsUniforms.fogColor[2] = params.fogColor[2];

                sg_range fsRange{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_lit3d_fs_params, &fsRange);
                break;
            }
        }

        const int elementCount = ElementCountForPrimitives(primitive, primitiveCount);
        const int baseElement = (ib != nullptr) ? params.startIndex : params.vertexStart;
        sg_draw(baseElement, elementCount, 1);
    }

    void SokolGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                     const Matrix& world,
                                                     const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive,
                                                     int primitiveCount)
    {
        // This entry point carries no effect state, so it means "render the raw vertex colours":
        // a white diffuse and vertex colouring on, matching what the other backends do here.
        GpuDrawParams params{};
        params.vertexColorEnabled = true;
        DrawColored3D(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                            const IIndexBufferBackend& ib,
                                                            const Matrix& world,
                                                            const Matrix& view,
                                                            const Matrix& projection,
                                                            PrimitiveType primitive,
                                                            int primitiveCount)
    {
        GpuDrawParams params{};
        params.vertexColorEnabled = true;
        DrawColored3D(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                const Matrix& world,
                                                const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive,
                                                int primitiveCount,
                                                const GpuDrawParams& params)
    {
        DrawColored3D(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                                       const IIndexBufferBackend& ib,
                                                       const Matrix& world,
                                                       const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive,
                                                       int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        DrawColored3D(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    bool SokolGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // Real: the swapchain carries a genuine depth-stencil buffer (SDL_GL_DEPTH_SIZE 24 /
            // SDL_GL_STENCIL_SIZE 8) that the Clear* family writes through pass load actions.
            case CNA::GraphicsCapability::DepthStencilBuffer:
                return true;
            // Real, but back-buffer only: the sample count is negotiated with the GL context at
            // construction. There are no render targets on this backend to multisample.
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return true;
            // Real: SamplerState.MaxAnisotropy reaches sg_sampler_desc.max_anisotropy whenever
            // TextureFilter::Anisotropic is selected.
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return true;
            // Real as of SOKOL-20: vertex/index buffers, depth-tested draws and the colored-3D
            // pipeline all exist. Texturing and lighting do not, and those draws throw -- but this
            // capability asks whether the 3D pipeline exists at all, and it now does.
            case CNA::GraphicsCapability::ThreeD:
                return true;
            // Real as of SOKOL-29, GL-only: a raw glBeginQuery/glEndQuery GL_SAMPLES_PASSED query,
            // since sokol_gfx exposes no query API of its own. False on any other CNA_SOKOL_API,
            // the same GL-only boundary ReadBackbuffer already declares.
            case CNA::GraphicsCapability::OcclusionQuery:
                return CNA_SOKOL_HAS_GL_READBACK != 0;
            // Real as of SOKOL-27: SokolTexture3DBackend stores real per-mip-level voxels
            // (SetData/GetData round-trip exactly), the same CPU-storage-only shape as
            // SokolTextureCubeBackend -- no sg_image, since nothing samples a volume texture yet.
            case CNA::GraphicsCapability::Texture3D:
                return true;
            // The remaining boundary. Each needs a phase that is not implemented yet -- see
            // plan_sokol.md; every one fails loudly rather than silently no-opping.
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::WireFrame:
            case CNA::GraphicsCapability::CustomEffects:
                return false;
        }
        return false;
    }

    int SokolGraphicsBackend::GetMultiSampleCount() const { return sampleCount_; }

    SokolApiEXT SokolGraphicsBackend::GetApiEXT()
    {
#if defined(SOKOL_GLCORE)
        return SokolApiEXT::GLCore;
#elif defined(SOKOL_GLES3)
        return SokolApiEXT::GLES3;
#elif defined(SOKOL_D3D11)
        return SokolApiEXT::D3D11;
#elif defined(SOKOL_METAL)
        return SokolApiEXT::Metal;
#elif defined(SOKOL_WGPU)
        return SokolApiEXT::WebGPU;
#else
    #error "CNA: no SOKOL_* API define set -- cmake/BackendSelection.cmake's CNA_SOKOL_API is broken"
#endif
    }

    int SokolGraphicsBackend::GetMaxSpriteQuadsPerFrameEXT() { return kMaxSpriteQuads; }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Sokol::SokolGraphicsBackend>(args);
    }
}
