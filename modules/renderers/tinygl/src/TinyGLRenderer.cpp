// SPDX-License-Identifier: MS-PL
//
// Real, working TinyGL (C-Chads/tinygl) graphics renderer -- a CPU implementation of a
// fixed-function OpenGL 1.x subset, with no shaders anywhere in its design.
//
// TinyGL is a global-namespace C library. `GL/gl.h` carries its own `extern "C"` guard; `zbuffer.h`
// does not, so it is wrapped below. Its `GL_*` constants are the real numeric OpenGL tokens, but
// nothing here assumes an XNA ordinal happens to equal one: every enum crossing this boundary is
// translated by an explicit switch that rejects what it does not recognize.
//
// THE governing constraint (TINYGL-0, tinygl-spike/README.md): TinyGL answers an argument
// combination it cannot handle by calling gl_fatal_error(), which prints a message and TERMINATES
// THE PROCESS. There is no error flag to check and no recoverable path. Every validation below
// therefore runs BEFORE the corresponding TinyGL call, not after it.

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"

#include <GL/gl.h>
extern "C" {
#include <zbuffer.h>
}

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::TinyGL
{
    namespace
    {
        /// The renderer's public CNA identity, used verbatim in every diagnostic below.
        constexpr const char* kRendererName = "TINYGL";

        /// Stride of the two vertex layouts the fixed-function routes can fetch.
        constexpr std::size_t kPositionColorStride = 16;
        constexpr std::size_t kPositionColorTextureStride = 24;

        /// TinyGL's own discard key (TGL_NO_DRAW_COLOR in zfeatures.h). A textured fragment whose
        /// texel matches this colour is not drawn at all -- the only transparency TinyGL has.
        constexpr std::uint8_t kNoDrawR = 0xFF;
        constexpr std::uint8_t kNoDrawG = 0x00;
        constexpr std::uint8_t kNoDrawB = 0xFF;

        /// TinyGL silently rounds framebuffer widths DOWN to a multiple of four. CNA instead
        /// rounds the private allocation UP and keeps reporting/clipping to the requested logical
        /// size, so no public column disappears.
        int PaddedFramebufferWidth(int logicalWidth)
        {
            if (logicalWidth > std::numeric_limits<int>::max() - 3)
                throw std::runtime_error("TinyGLRenderer: framebuffer width is too large to align.");
            return (logicalWidth + 3) & ~3;
        }

        [[noreturn]] void Unsupported(const std::string& message)
        {
            throw System::NotSupportedException(std::string(kRendererName) + ": " + message);
        }

        /// Exactly one TinyGL context can exist per process: glInit()/glClose() install and tear
        /// down a file-scope global inside the library, and there is no "make current" entry point
        /// to switch between several. Tracked here so a second renderer fails loudly at
        /// construction instead of silently corrupting the first one's context.
        bool g_contextLive = false;

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw std::runtime_error("TinyGLRenderer: unrecognized PrimitiveType");
            }
        }

        GLenum ToTinyGLMode(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
            default:
                throw std::runtime_error("TinyGLRenderer: unrecognized PrimitiveType");
            }
        }

        /// Maps a public `Graphics::Blend` ordinal onto the factor TinyGL's rasterizer really
        /// executes in the SOURCE slot. Its factor switch (zbuffer.h, TGL_BLEND_FUNC) has cases for
        /// GL_ONE, GL_ZERO and GL_ONE_MINUS_SRC_COLOR there and nothing else -- every other value
        /// falls through to `default:` and silently behaves as GL_ONE, which is exactly why this
        /// refuses instead of forwarding.
        GLint ToTinyGLSourceFactor(int blendOrdinal)
        {
            switch (blendOrdinal)
            {
            case 0: return GL_ONE;                     // One
            case 1: return GL_ZERO;                    // Zero
            case 3: return GL_ONE_MINUS_SRC_COLOR;     // InverseSourceColor
            default:
                Unsupported(
                    "source Blend ordinal " + std::to_string(blendOrdinal) +
                    " is not executable: TinyGL's rasterizer implements only One, Zero and "
                    "InverseSourceColor as source factors, and has no alpha channel at all.");
            }
        }

        /// Destination-slot counterpart of ToTinyGLSourceFactor(). The asymmetry is upstream's:
        /// the destination switch has a case for GL_ONE_MINUS_DST_COLOR and none for
        /// GL_ONE_MINUS_SRC_COLOR, so the accepted set genuinely differs per slot.
        GLint ToTinyGLDestFactor(int blendOrdinal)
        {
            switch (blendOrdinal)
            {
            case 0: return GL_ONE;                     // One
            case 1: return GL_ZERO;                    // Zero
            case 7: return GL_ONE_MINUS_DST_COLOR;     // InverseDestinationColor
            default:
                Unsupported(
                    "destination Blend ordinal " + std::to_string(blendOrdinal) +
                    " is not executable: TinyGL's rasterizer implements only One, Zero and "
                    "InverseDestinationColor as destination factors, and has no alpha channel "
                    "at all.");
            }
        }

        GLenum ToTinyGLBlendEquation(int blendFunctionOrdinal)
        {
            switch (blendFunctionOrdinal)
            {
            case 0: return GL_FUNC_ADD;              // Add
            case 1: return GL_FUNC_SUBTRACT;         // Subtract
            case 2: return GL_FUNC_REVERSE_SUBTRACT; // ReverseSubtract
            default:
                Unsupported(
                    "BlendFunction ordinal " + std::to_string(blendFunctionOrdinal) +
                    " is not executable: TinyGL's blend equation switch implements only Add, "
                    "Subtract and ReverseSubtract.");
            }
        }

        /// True for the exact factor+function signature of `BlendState::AlphaBlend`
        /// (One, InverseSourceAlpha on both channels, Add) or `BlendState::NonPremultiplied`
        /// (SourceAlpha, InverseSourceAlpha on both channels, Add). Both are matched on their
        /// COMPLETE signature -- a custom state that merely shares some factors is not one of them
        /// and falls through to the refusing path.
        bool IsXnaAlphaPreset(int colorSrc, int alphaSrc, int colorDst, int alphaDst,
                              int colorFunc, int alphaFunc)
        {
            if (colorFunc != 0 || alphaFunc != 0) return false; // both must be Add
            const bool alphaBlend =
                colorSrc == 0 && alphaSrc == 0 && colorDst == 5 && alphaDst == 5;
            const bool nonPremultiplied =
                colorSrc == 4 && alphaSrc == 4 && colorDst == 5 && alphaDst == 5;
            return alphaBlend || nonPremultiplied;
        }

        /// Validates a `TextureFilter` ordinal. TinyGL takes exactly one nearest texel sample per
        /// fragment and its glTexParameteri is an upstream no-op, so no ordinal selects anything --
        /// but `Anisotropic` is refused because SupportsCapability(AnisotropicFiltering) reports
        /// false, and an unknown ordinal is refused because it is a caller error.
        void ValidateTextureFilter(int textureFilterOrdinal)
        {
            switch (textureFilterOrdinal)
            {
            case 0: // Linear
            case 1: // Point
            case 3: case 4: case 5: case 6: case 7: case 8: // the mixed min/mag/mip filters
                return;
            case 2:
                Unsupported(
                    "TextureFilter::Anisotropic is not supported -- TinyGL takes a single nearest "
                    "texel sample per fragment, which is why "
                    "SupportsCapability(AnisotropicFiltering) reports false.");
            default:
                Unsupported("unsupported TextureFilter ordinal " +
                            std::to_string(textureFilterOrdinal));
            }
        }

        void ValidateTextureAddressMode(int addressModeOrdinal)
        {
            switch (addressModeOrdinal)
            {
            case 0: // Wrap -- what TinyGL's masked texel fetch actually performs
            case 1: // Clamp
            case 2: // Mirror
                return;
            default:
                Unsupported("unsupported TextureAddressMode ordinal " +
                            std::to_string(addressModeOrdinal));
            }
        }

        /// TinyGL's PIXEL in ZB_MODE_RGBA is 0x00RRGGBB; the alpha byte is never written by any
        /// part of the pipeline, so readback reports every pixel as fully opaque.
        void DecodeBackbufferPixel(std::uint32_t pixel, std::uint8_t* rgba)
        {
            rgba[0] = static_cast<std::uint8_t>((pixel >> 16) & 0xFFu);
            rgba[1] = static_cast<std::uint8_t>((pixel >> 8) & 0xFFu);
            rgba[2] = static_cast<std::uint8_t>(pixel & 0xFFu);
            rgba[3] = 0xFF;
        }
    }

    // =============================================================================================
    // Vertex buffer
    // =============================================================================================

    TinyGLVertexBufferRenderer::TinyGLVertexBufferRenderer(int vertexCapacity)
        : vertexCount_(vertexCapacity > 0 ? vertexCapacity : 0)
    {
    }

    void TinyGLVertexBufferRenderer::SetData(const void* data, int vertex_count,
                                             std::size_t stride_in_bytes)
    {
        if (data == nullptr || vertex_count <= 0 || stride_in_bytes == 0)
        {
            vertexCount_ = 0;
            stride_ = stride_in_bytes;
            storage_.clear();
            return;
        }
        const std::size_t bytes = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        storage_.resize(bytes);
        std::memcpy(storage_.data(), data, bytes);
        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
    }

    void TinyGLVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declared_.Remember(vertexDeclaration);
    }

    // =============================================================================================
    // Index buffer
    // =============================================================================================

    TinyGLIndexBufferRenderer::TinyGLIndexBufferRenderer(int indexCapacity)
        : indexCount_(indexCapacity > 0 ? indexCapacity : 0)
    {
        if (indexCount_ > 0) indices_.assign(static_cast<std::size_t>(indexCount_), 0u);
    }

    void TinyGLIndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        if (data == nullptr || index_count <= 0)
        {
            indices_.clear();
            indexCount_ = 0;
            thirtyTwoBit_ = false;
            return;
        }
        const auto* source = static_cast<const std::uint16_t*>(data);
        indices_.resize(static_cast<std::size_t>(index_count));
        for (int i = 0; i < index_count; ++i) indices_[static_cast<std::size_t>(i)] = source[i];
        indexCount_ = index_count;
        thirtyTwoBit_ = false;
    }

    void TinyGLIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        if (data == nullptr || index_count <= 0)
        {
            indices_.clear();
            indexCount_ = 0;
            thirtyTwoBit_ = true;
            return;
        }
        const auto* source = static_cast<const std::uint32_t*>(data);
        indices_.assign(source, source + index_count);
        indexCount_ = index_count;
        thirtyTwoBit_ = true;
    }

    std::uint32_t TinyGLIndexBufferRenderer::IndexAt(int position) const
    {
        if (position < 0 || static_cast<std::size_t>(position) >= indices_.size()) return 0u;
        return indices_[static_cast<std::size_t>(position)];
    }

    // =============================================================================================
    // Texture
    // =============================================================================================

    TinyGLTextureRenderer::TinyGLTextureRenderer(const ImageData& data)
        : width_(data.width), height_(data.height)
    {
        if (data.mipLevels != 1)
            Unsupported(
                "Texture2D mip chains are not supported -- TinyGL stores and samples level 0 only.");
        const std::size_t expected =
            static_cast<std::size_t>(std::max(data.width, 0)) *
            static_cast<std::size_t>(std::max(data.height, 0)) * 4u;
        shadowRgba_.assign(expected, 0u);
        if (data.pixels.size() >= expected && expected > 0)
            std::memcpy(shadowRgba_.data(), data.pixels.data(), expected);

        GLuint names[2] = {};
        glGenTextures(2, names);
        glOpaqueTexture_ = names[0];
        glCutoutTexture_ = names[1];
        Upload();
    }

    TinyGLTextureRenderer::~TinyGLTextureRenderer()
    {
        const GLuint names[2] = {glOpaqueTexture_, glCutoutTexture_};
        glDeleteTextures(2, names);
        glOpaqueTexture_ = 0;
        glCutoutTexture_ = 0;
    }

    void TinyGLTextureRenderer::Upload()
    {
        if (width_ <= 0 || height_ <= 0 || glOpaqueTexture_ == 0 || glCutoutTexture_ == 0) return;

        // TINYGL-0 fact B: glTexImage2D accepts GL_RGB/GL_UNSIGNED_BYTE only. Keep ordinary RGB in
        // one object; UploadCutout() folds alpha into TinyGL's colour key in the second object.
        const std::size_t texelCount =
            static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        std::vector<std::uint8_t> opaqueRgb(texelCount * 3u);
        for (std::size_t i = 0; i < texelCount; ++i)
        {
            const std::uint8_t r = shadowRgba_[i * 4 + 0];
            const std::uint8_t g = shadowRgba_[i * 4 + 1];
            const std::uint8_t b = shadowRgba_[i * 4 + 2];
            // An opaque texel that happens to BE the key colour would otherwise disappear. Nudging
            // green by one is the smallest change that keeps it visible. TinyGL's key test is
            // unconditional, so the ordinary texture must apply the same nudge too.
            if (r == kNoDrawR && g == kNoDrawG && b == kNoDrawB)
            {
                opaqueRgb[i * 3 + 0] = kNoDrawR;
                opaqueRgb[i * 3 + 1] = 1;
                opaqueRgb[i * 3 + 2] = kNoDrawB;
            }
            else
            {
                opaqueRgb[i * 3 + 0] = r;
                opaqueRgb[i * 3 + 1] = g;
                opaqueRgb[i * 3 + 2] = b;
            }
        }

        // Validated shape only -- see this file's header comment on gl_fatal_error().
        glBindTexture(GL_TEXTURE_2D, static_cast<GLint>(glOpaqueTexture_));
        glTexImage2D(GL_TEXTURE_2D, 0, 3, width_, height_, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     opaqueRgb.data());
        cutoutAlphaMultiplier_ = -1.0f;
        UploadCutout(1.0f);
    }

    void TinyGLTextureRenderer::UploadCutout(float alphaMultiplier) const
    {
        alphaMultiplier = std::clamp(alphaMultiplier, 0.0f, 1.0f);
        if (cutoutAlphaMultiplier_ == alphaMultiplier) return;

        const std::size_t texelCount =
            static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        std::vector<std::uint8_t> cutoutRgb(texelCount * 3u);
        hasCutoutTexels_ = false;
        for (std::size_t i = 0; i < texelCount; ++i)
        {
            const std::uint8_t r = shadowRgba_[i * 4 + 0];
            const std::uint8_t g = shadowRgba_[i * 4 + 1];
            const std::uint8_t b = shadowRgba_[i * 4 + 2];
            const std::uint8_t a = shadowRgba_[i * 4 + 3];
            if (static_cast<float>(a) * alphaMultiplier <
                static_cast<float>(kAlphaCutoutThreshold))
            {
                cutoutRgb[i * 3 + 0] = kNoDrawR;
                cutoutRgb[i * 3 + 1] = kNoDrawG;
                cutoutRgb[i * 3 + 2] = kNoDrawB;
                hasCutoutTexels_ = true;
            }
            else if (r == kNoDrawR && g == kNoDrawG && b == kNoDrawB)
            {
                cutoutRgb[i * 3 + 0] = kNoDrawR;
                cutoutRgb[i * 3 + 1] = 1;
                cutoutRgb[i * 3 + 2] = kNoDrawB;
            }
            else
            {
                cutoutRgb[i * 3 + 0] = r;
                cutoutRgb[i * 3 + 1] = g;
                cutoutRgb[i * 3 + 2] = b;
            }
        }

        glBindTexture(GL_TEXTURE_2D, static_cast<GLint>(glCutoutTexture_));
        glTexImage2D(GL_TEXTURE_2D, 0, 3, width_, height_, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     cutoutRgb.data());
        cutoutAlphaMultiplier_ = alphaMultiplier;
    }

    unsigned int TinyGLTextureRenderer::GLTextureHandle(bool cutout, float alphaMultiplier) const
    {
        if (cutout) UploadCutout(alphaMultiplier);
        return cutout ? glCutoutTexture_ : glOpaqueTexture_;
    }

    void TinyGLTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (rgba == nullptr || width_ <= 0 || height_ <= 0) return;
        const int rowBytes = width_ * 4;
        const int sourcePitch = stride > 0 ? stride : rowBytes;
        for (int y = 0; y < height_; ++y)
        {
            std::memcpy(shadowRgba_.data() + static_cast<std::size_t>(y) * rowBytes,
                        rgba + static_cast<std::size_t>(y) * sourcePitch,
                        static_cast<std::size_t>(rowBytes));
        }
        Upload();
    }

    bool TinyGLTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                        void* data, int dataLength) const
    {
        if (level != 0 || data == nullptr) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
        if (x + w > width_ || y + h > height_) return false;
        if (static_cast<long long>(dataLength) <
            static_cast<long long>(w) * static_cast<long long>(h) * 4LL) return false;
        auto* dest = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(width_) +
                 static_cast<std::size_t>(x)) * 4u;
            std::memcpy(dest + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u,
                        shadowRgba_.data() + sourceOffset,
                        static_cast<std::size_t>(w) * 4u);
        }
        return true;
    }

    // =============================================================================================
    // Renderer
    // =============================================================================================

    struct TinyGLRenderer::Impl
    {
        ZBuffer* zb = nullptr;
        /// Last colour installed by Clear*(); TinyGL's glClearColor is state, not an argument.
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        /// Recorded device state that no TinyGL draw can consult -- see the header's docs on
        /// SetBlendFactor()/SetScissorRect()/ApplySamplerState().
        float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        int scissor[4] = {0, 0, 0, 0};
        /// True while the installed BlendState is one of the two XNA alpha presets, which this
        /// renderer executes as TinyGL's colour-key cutout rather than as a blend equation.
        bool alphaCutoutMode = false;
        /// Active XNA viewport, whose coordinates are top-left based just like TinyGL's rasterizer.
        /// SpriteBatch positions are local to this rectangle.
        int viewportX = 0;
        int viewportY = 0;
        int viewportW = 1;
        int viewportH = 1;

        /// De-interleaved float arrays handed to TinyGL's vertex-array pointers, rebuilt per draw.
        /// They must outlive the draw because TinyGL stores the pointers, not the data.
        std::vector<float> scratchPositions;
        std::vector<float> scratchColors;
        std::vector<float> scratchTexCoords;
    };

    TinyGLRenderer::TinyGLRenderer(int virtualWidth, int virtualHeight)
        : impl_(std::make_unique<Impl>()),
          virtualWidth_(virtualWidth > 0 ? virtualWidth : 1),
          virtualHeight_(virtualHeight > 0 ? virtualHeight : 1)
    {
        if (g_contextLive)
            throw std::runtime_error(
                "TinyGLRenderer: a TinyGL context already exists. TinyGL keeps its context in one "
                "process-wide global (glInit/glClose) and offers no make-current entry point, so "
                "exactly one TinyGLRenderer may exist at a time.");

        impl_->zb = ZB_open(PaddedFramebufferWidth(virtualWidth_), virtualHeight_, ZB_MODE_RGBA, nullptr);
        if (impl_->zb == nullptr)
            throw std::runtime_error("TinyGLRenderer: ZB_open() failed to allocate the framebuffer.");
        glInit(impl_->zb);
        g_contextLive = true;

        glViewport(0, 0, virtualWidth_, virtualHeight_);
        impl_->viewportW = virtualWidth_;
        impl_->viewportH = virtualHeight_;
        // XNA's front faces are clockwise in screen space; TinyGL's default is counter-clockwise,
        // so the winding is stated explicitly rather than inherited.
        glFrontFace(GL_CW);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(1);
        glShadeModel(GL_SMOOTH);
    }

    TinyGLRenderer::~TinyGLRenderer()
    {
        if (impl_ && impl_->zb != nullptr)
        {
            glClose();
            ZB_close(impl_->zb);
            impl_->zb = nullptr;
            g_contextLive = false;
        }
    }

    void TinyGLRenderer::Clear(float r, float g, float b, float a)
    {
        impl_->clearColor[0] = r;
        impl_->clearColor[1] = g;
        impl_->clearColor[2] = b;
        impl_->clearColor[3] = a;
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void TinyGLRenderer::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void TinyGLRenderer::SetVirtualResolution(int width, int height)
    {
        const int newWidth = width > 0 ? width : 1;
        const int newHeight = height > 0 ? height : 1;
        if (newWidth == virtualWidth_ && newHeight == virtualHeight_) return;

        // ZB_resize reallocates the colour and depth planes in place; the GL context stays valid
        // and keeps pointing at the same ZBuffer, so no glInit()/glClose() cycle is needed.
        ZB_resize(impl_->zb, nullptr, PaddedFramebufferWidth(newWidth), newHeight);
        virtualWidth_ = newWidth;
        virtualHeight_ = newHeight;
        glViewport(0, 0, virtualWidth_, virtualHeight_);
        impl_->viewportX = 0;
        impl_->viewportY = 0;
        impl_->viewportW = virtualWidth_;
        impl_->viewportH = virtualHeight_;
    }

    void TinyGLRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0) return;
        const ZBuffer* zb = impl_->zb;
        for (int row = 0; row < h; ++row)
        {
            const int sourceY = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int sourceX = x + col;
                std::uint8_t* dest =
                    pixels + (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                              static_cast<std::size_t>(col)) * 4u;
                if (sourceX < 0 || sourceY < 0 || sourceX >= virtualWidth_ ||
                    sourceY >= virtualHeight_)
                {
                    dest[0] = dest[1] = dest[2] = 0;
                    dest[3] = 0xFF;
                    continue;
                }
                const auto* rowBase = reinterpret_cast<const std::uint8_t*>(zb->pbuf) +
                                      static_cast<std::size_t>(sourceY) *
                                          static_cast<std::size_t>(zb->linesize);
                DecodeBackbufferPixel(reinterpret_cast<const std::uint32_t*>(rowBase)[sourceX], dest);
            }
        }
    }

    int TinyGLRenderer::GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const
    {
        return 0; // SurfaceFormat::Color
    }

    int TinyGLRenderer::GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const
    {
        return 1; // DepthFormat::Depth16 -- TinyGL's zbuf is GLushort and there is no stencil plane
    }

    std::unique_ptr<ITextureRenderer> TinyGLRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<TinyGLTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> TinyGLRenderer::CreateSpriteBatch()
    {
        return std::make_unique<TinyGLSpriteBatchRenderer>(*this);
    }

    void TinyGLRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* /*renderTargets*/,
                                          int count)
    {
        if (count > 0)
            Unsupported(
                "render targets are not supported -- TinyGL owns exactly one framebuffer per "
                "context and has no framebuffer-object concept, which is why "
                "CreateRenderTarget2D()/CreateRenderTargetCube() are not implemented.");
    }

    void TinyGLRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt != nullptr)
            Unsupported("render targets are not supported -- TinyGL owns exactly one framebuffer "
                        "per context.");
    }

    void TinyGLRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int /*face*/)
    {
        if (rt != nullptr)
            Unsupported("cube render targets are not supported -- TinyGL owns exactly one "
                        "framebuffer per context and has no cube texture type.");
    }

    void TinyGLRenderer::ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/)
    {
        // TinyGL clears the whole depth plane to its own fixed far value. Upstream records
        // glClearDepth but glopClear never reads it, so the requested depth cannot be honoured.
        impl_->clearColor[0] = r;
        impl_->clearColor[1] = g;
        impl_->clearColor[2] = b;
        impl_->clearColor[3] = a;
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void TinyGLRenderer::ClearDepth(float /*depth*/)
    {
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void TinyGLRenderer::ClearStencil(int /*stencil*/)
    {
        // Clearing a stencil plane that does not exist is a no-op, exactly as it is in real
        // OpenGL: glClear(GL_STENCIL_BUFFER_BIT) against a framebuffer with no stencil attachment
        // is legal and does nothing. What this renderer refuses is the promise that would be
        // false -- enabling the stencil TEST (see ApplyDepthStencilState()).
    }

    void TinyGLRenderer::ClearDepthAndStencil(float depth, int /*stencil*/)
    {
        ClearDepth(depth);
    }

    void TinyGLRenderer::ClearColorAndStencil(float r, float g, float b, float a, int /*stencil*/)
    {
        Clear(r, g, b, a);
    }

    void TinyGLRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                                   int /*stencil*/)
    {
        ClearColorAndDepth(r, g, b, a, depth);
    }

    void TinyGLRenderer::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void TinyGLRenderer::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void TinyGLRenderer::SetDepthWriteEnabled(bool enabled)
    {
        glDepthMask(enabled ? 1 : 0);
    }

    void TinyGLRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& writeState)
    {
        // ColorWriteChannels::All == 15. TinyGL has no glColorMask, so any narrower mask -- on any
        // of the four MRT slots -- would be a claim this renderer cannot honour.
        for (int slot = 0; slot < 4; ++slot)
        {
            if (writeState.colorWriteChannels[slot] != 15)
                Unsupported(
                    "ColorWriteChannels on target " + std::to_string(slot) +
                    " selects a subset of RGBA, which TinyGL cannot express -- it has no "
                    "glColorMask and no per-channel write control.");
        }
        if (writeState.multiSampleMask != 0xFFFFFFFFu)
            Unsupported(
                "BlendState.MultiSampleMask cannot be honoured -- TinyGL rasterizes exactly one "
                "sample per pixel and has no coverage-mask state.");

        if (IsXnaAlphaPreset(colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                             colorBlendFunc, alphaBlendFunc))
        {
            // Executed as TinyGL's own 1-bit colour-key cutout: the discard decision is made per
            // fragment by its triangle rasterizer against TGL_NO_DRAW_COLOR, which
            // TinyGLTextureRenderer::Upload() writes for every texel below the alpha threshold.
            // Blending itself stays off -- an approximation recorded in docs/tinygl-renderer.md,
            // not a claim of real alpha compositing.
            impl_->alphaCutoutMode = true;
            glDisable(GL_BLEND);
            return;
        }

        // XNA carries separate RGB and alpha factors; TinyGL's rasterizer has one factor pair for
        // the whole pixel and no alpha channel to apply the second pair to. Accepting a state whose
        // halves disagree would silently drop one of them.
        if (colorSrcBlend != alphaSrcBlend || colorDstBlend != alphaDstBlend ||
            colorBlendFunc != alphaBlendFunc)
            Unsupported(
                "a BlendState whose RGB and alpha halves differ cannot be expressed -- TinyGL "
                "applies one factor pair and one equation to the whole pixel and has no alpha "
                "channel.");

        const GLint source = ToTinyGLSourceFactor(colorSrcBlend);
        const GLint dest = ToTinyGLDestFactor(colorDstBlend);
        const GLenum equation = ToTinyGLBlendEquation(colorBlendFunc);
        impl_->alphaCutoutMode = false;

        // BlendState::Opaque -- (One, Zero) with Add -- is the identity, so blending is switched
        // off entirely rather than executed, matching every other CNA renderer's own fast path.
        if (source == GL_ONE && dest == GL_ZERO && equation == GL_FUNC_ADD)
        {
            glDisable(GL_BLEND);
            return;
        }

        glBlendFunc(source, dest);
        glBlendEquation(equation);
        glEnable(GL_BLEND);
    }

    void TinyGLRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        impl_->blendFactor[0] = r;
        impl_->blendFactor[1] = g;
        impl_->blendFactor[2] = b;
        impl_->blendFactor[3] = a;
    }

    void TinyGLRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                int depthFunc,
                                                bool stencilEnable, int /*stencilFunc*/,
                                                int /*stencilPass*/, int /*stencilFail*/,
                                                int /*stencilDepthFail*/,
                                                int /*stencilMask*/, int /*stencilWriteMask*/,
                                                int /*referenceStencil*/,
                                                bool /*twoSidedStencilMode*/,
                                                int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        if (stencilEnable)
            Unsupported(
                "the stencil test cannot be enabled -- TinyGL's ZBuffer has no stencil plane, "
                "which is why SupportsCapability(StencilBuffer) reports false.");

        // TinyGL has no glDepthFunc at all. Its rasterizer's one comparison is
        // ZCMPSIMP(z, zpix) == (z >= zpix) over a z-buffer in which a LARGER stored value is
        // nearer, which is LessEqual in XNA's own depth convention -- CompareFunction::LessEqual
        // == 3, and also XNA's DepthStencilState::Default. Every other request, Less included,
        // would be accepted here and then not performed.
        if (depthEnable && depthFunc != 3)
            Unsupported(
                "CompareFunction ordinal " + std::to_string(depthFunc) +
                " cannot be used as the depth comparison -- TinyGL implements no glDepthFunc and "
                "compares with LessEqual unconditionally.");

        if (depthEnable) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(depthWriteEnable ? 1 : 0);
    }

    void TinyGLRenderer::SetReferenceStencil(int value)
    {
        if (value != 0)
            Unsupported("ReferenceStencil cannot be set -- TinyGL has no stencil plane.");
    }

    void TinyGLRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                              bool scissorTestEnable,
                                              float depthBias,
                                              float slopeScaleDepthBias)
    {
        if (scissorTestEnable)
            Unsupported(
                "ScissorTestEnable cannot be honoured -- TinyGL implements no glScissor and its "
                "rasterizer clips only against the viewport.");
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
            Unsupported(
                "a non-zero DepthBias/SlopeScaleDepthBias cannot be honoured -- TinyGL's "
                "glPolygonOffset stores its arguments and its rasterizer never reads them.");
        if (cullMode < 0 || cullMode > 2)
            Unsupported("unsupported CullMode ordinal " + std::to_string(cullMode));
        if (fillMode < 0 || fillMode > 1)
            Unsupported("unsupported FillMode ordinal " + std::to_string(fillMode));

        switch (cullMode)
        {
        case 0: // None
            glDisable(GL_CULL_FACE);
            break;
        case 1: // CullClockwiseFace -- XNA's clockwise faces are TinyGL's front faces (glFrontFace(GL_CW))
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case 2: // CullCounterClockwiseFace
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
        }

        switch (fillMode)
        {
        case 0: // Solid
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case 1: // WireFrame
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            break;
        }
    }

    void TinyGLRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        impl_->scissor[0] = x;
        impl_->scissor[1] = y;
        impl_->scissor[2] = w;
        impl_->scissor[3] = h;
    }

    void TinyGLRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (minDepth != 0.0f || maxDepth != 1.0f)
            Unsupported(
                "a Viewport depth range other than 0..1 cannot be honoured -- TinyGL implements "
                "no glDepthRange.");
        if (w <= 0 || h <= 0)
            Unsupported("a Viewport with a non-positive width or height cannot be installed -- "
                        "TinyGL's glViewport treats that as a fatal error and terminates.");
        if (x < 0 || y < 0 || x > virtualWidth_ - w || y > virtualHeight_ - h)
            Unsupported("a Viewport outside the logical backbuffer cannot be installed safely.");

        // Unlike desktop OpenGL, TinyGL's gl_eval_viewport() applies a negative Y scale and writes
        // directly into a top-row-first ZBuffer. Its viewport origin therefore already matches XNA.
        glViewport(x, y, w, h);
        impl_->viewportX = x;
        impl_->viewportY = y;
        impl_->viewportW = w;
        impl_->viewportH = h;
    }

    void TinyGLRenderer::ApplySamplerState(int /*slot*/, int filter, int addressU, int addressV,
                                           int /*maxAnisotropy*/)
    {
        ValidateTextureFilter(filter);
        ValidateTextureAddressMode(addressU);
        ValidateTextureAddressMode(addressV);
    }

    void TinyGLRenderer::ApplySamplerMipState(int /*slot*/, int maxMipLevel, float lodBias)
    {
        if (maxMipLevel != 0 || lodBias != 0.0f)
            Unsupported(
                "mip-level selection cannot be honoured -- TinyGL textures store level 0 only.");
    }

    std::unique_ptr<IVertexBufferRenderer> TinyGLRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<TinyGLVertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> TinyGLRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<TinyGLIndexBufferRenderer>(index_capacity);
    }

    // ---- Draw path ------------------------------------------------------------------------------

    TinyGLRenderer::FixedFunctionDrawState
    TinyGLRenderer::TranslateDrawParams(const GpuDrawParams& params, const char* route)
    {
        const std::string where = std::string(" (") + route + ")";

        if (params.lightingEnabled)
            Unsupported("BasicEffect lighting is not supported" + where +
                        " -- TinyGL's own glLight* pipeline lights per vertex from material state "
                        "this renderer does not translate, so enabling it would render something "
                        "other than what XNA specifies.");
        if (params.fogEnabled)
            Unsupported("fog is not supported" + where + ".");
        if (params.skinned)
            Unsupported("SkinnedEffect is not supported" + where +
                        " -- TinyGL has no vertex shader stage to evaluate bone transforms in.");
        if (params.dualTexture)
            Unsupported("DualTextureEffect is not supported" + where +
                        " -- TinyGL has a single texture unit.");
        if (params.envMapping)
            Unsupported("EnvironmentMapEffect is not supported" + where +
                        " -- TinyGL has no cube texture type.");
        if (params.pbr)
            Unsupported("the PBR effect is not supported" + where + ".");
        if (params.instanceCount > 1)
            Unsupported("instanced drawing is not supported" + where +
                        " -- TinyGL's vertex-array path draws exactly one instance.");
        if (params.alphaTest[1] != 0.0f || params.alphaTest[0] != 0.0f)
            Unsupported("AlphaTestEffect is not supported" + where +
                        " -- TinyGL has no alpha channel to test.");
        if (params.vertexStreamCount > 1)
            Unsupported("multi-stream vertex input is not supported" + where +
                        " -- the fixed-function routes bind exactly one stream.");
        if (params.texture1 != nullptr)
            Unsupported("a second texture layer is not supported" + where + ".");
        if (params.envMap != nullptr)
            Unsupported("an environment cube map is not supported" + where + ".");
        if (params.cpu2DColorMatrixEnabled)
            Unsupported("the CPU 2D colour matrix is not supported" + where + ".");

        FixedFunctionDrawState state;
        state.diffuse[0] = params.diffuseColor[0];
        state.diffuse[1] = params.diffuseColor[1];
        state.diffuse[2] = params.diffuseColor[2];
        state.diffuse[3] = params.diffuseColor[3];
        state.vertexColorEnabled = params.vertexColorEnabled;
        state.vertexStart = params.vertexStart;
        state.startIndex = params.startIndex;
        state.baseVertex = params.baseVertex;
        if (params.vertexStreamCount == 1)
            state.streamVertexOffset = params.vertexStreams[0].vertexOffset;

        if (params.textureEnabled)
        {
            const auto* texture = dynamic_cast<const TinyGLTextureRenderer*>(params.texture0);
            if (texture == nullptr)
                Unsupported("a textured draw was requested" + where +
                            " but no TinyGL texture is bound to unit 0.");
            state.texture = texture;
        }
        return state;
    }

    std::size_t TinyGLRenderer::BindVertexArrays(const TinyGLVertexBufferRenderer& vb,
                                                 const FixedFunctionDrawState& state,
                                                 const char* route)
    {
        const std::size_t stride = vb.StrideInBytes();
        const bool textured = state.texture != nullptr;
        const std::size_t expected = textured ? kPositionColorTextureStride : kPositionColorStride;

        if (stride != expected)
            Unsupported(
                std::string("the ") + route + " draw needs the " +
                (textured ? "VertexPositionColorTexture (stride 24)" : "VertexPositionColor (stride 16)") +
                " layout; the bound vertex buffer's stride is " + std::to_string(stride) + " bytes.");

        // A stride does not determine element composition: a declaration that puts something else
        // in the same bytes is refused here, before any TinyGL call, rather than reinterpreted.
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            vb.DeclaredLayout(), static_cast<int>(expected),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            kRendererName, route);

        // TinyGL's vertex-array pointers are NOT OpenGL's. Its arrays are `GLfloat*` -- glColorPointer
        // ignores the `type` argument entirely and always reads floats -- and its stride counts EXTRA
        // FLOATS between records, not bytes (arrays.c: `i = idx * (size + stride)`). An interleaved
        // XNA record with a packed 4-byte colour cannot be described that way at all, so the buffer
        // is de-interleaved into three tightly packed float arrays here and TinyGL is handed exactly
        // the shape its API defines. Every transform, clip, raster and texel decision after this
        // point is still TinyGL's.
        const int vertexCount = vb.GetVertexCount();
        const std::uint8_t* base = vb.Bytes();
        if (stride != 0 && (base == nullptr ||
            vb.ByteSize() < static_cast<std::size_t>(vertexCount) * stride))
            throw std::runtime_error(
                std::string("TinyGLRenderer: the ") + route +
                " draw reads a vertex buffer that has not received a complete SetData upload.");
        auto& positions = impl_->scratchPositions;
        auto& colors = impl_->scratchColors;
        auto& texCoords = impl_->scratchTexCoords;
        positions.resize(static_cast<std::size_t>(vertexCount) * 3u);
        colors.resize(static_cast<std::size_t>(vertexCount) * 4u);
        if (textured) texCoords.resize(static_cast<std::size_t>(vertexCount) * 2u);

        for (int i = 0; i < vertexCount; ++i)
        {
            const std::uint8_t* record = base + static_cast<std::size_t>(i) * stride;
            float position[3];
            std::memcpy(position, record, sizeof(position));
            positions[static_cast<std::size_t>(i) * 3u + 0] = position[0];
            positions[static_cast<std::size_t>(i) * 3u + 1] = position[1];
            positions[static_cast<std::size_t>(i) * 3u + 2] = position[2];

            // XNA packs Color as four unsigned bytes in RGBA order at offset 12 of both layouts.
            for (int c = 0; c < 4; ++c)
                colors[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(c)] =
                    (static_cast<float>(record[12 + c]) / 255.0f) * state.diffuse[c];

            if (textured)
            {
                float uv[2];
                std::memcpy(uv, record + 16, sizeof(uv));
                texCoords[static_cast<std::size_t>(i) * 2u + 0] = uv[0];
                texCoords[static_cast<std::size_t>(i) * 2u + 1] = uv[1];
            }
        }

        unsigned int selectedTexture = 0;
        if (textured)
        {
            // BasicEffect.Alpha is uniform and already carried in diffuse[3]. This may lazily
            // refresh the cutout object for that multiplier; it is deliberately after every
            // recoverable validation above and before client GL state.
            selectedTexture = state.texture->GLTextureHandle(
                impl_->alphaCutoutMode, state.diffuse[3]);
        }

        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, positions.data());

        if (state.vertexColorEnabled)
        {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_FLOAT, 0, colors.data());
        }
        else
        {
            // BasicEffect with VertexColorEnabled off renders DiffuseColor, so the packed vertex
            // colour is deliberately not read: TinyGL's current colour is what modulates instead.
            glDisableClientState(GL_COLOR_ARRAY);
            glColor4f(state.diffuse[0], state.diffuse[1], state.diffuse[2], state.diffuse[3]);
        }

        if (textured)
        {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, 0, texCoords.data());
            glBindTexture(GL_TEXTURE_2D, static_cast<GLint>(selectedTexture));
            glEnable(GL_TEXTURE_2D);
        }
        else
        {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisable(GL_TEXTURE_2D);
        }
        return stride;
    }

    void TinyGLRenderer::UnbindVertexArrays(bool textured)
    {
        if (textured)
        {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisable(GL_TEXTURE_2D);
        }
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    void TinyGLRenderer::LoadDrawMatrices(const Matrix& world, const Matrix& view,
                                          const Matrix& projection)
    {
        float columnMajor[16];
        projection.ToColumnMajor(columnMajor);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(columnMajor);

        // TinyGL applies GL_MODELVIEW then GL_PROJECTION, exactly like desktop GL, so world * view
        // is the modelview matrix.
        const Matrix modelView = world * view;
        modelView.ToColumnMajor(columnMajor);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(columnMajor);
    }

    void TinyGLRenderer::DrawCommon(const IVertexBufferRenderer& vbBase,
                                    const Matrix& world, const Matrix& view,
                                    const Matrix& projection,
                                    PrimitiveType primitive, int primitiveCount,
                                    const FixedFunctionDrawState& state, const char* route)
    {
        if (primitiveCount <= 0) return;
        const auto* vb = dynamic_cast<const TinyGLVertexBufferRenderer*>(&vbBase);
        if (vb == nullptr)
            throw std::runtime_error("TinyGLRenderer: the bound vertex buffer was not created by "
                                     "this renderer.");

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum mode = ToTinyGLMode(primitive);
        const long long firstElement =
            static_cast<long long>(state.streamVertexOffset) + static_cast<long long>(state.vertexStart);
        if (firstElement < 0 ||
            firstElement + vertexCount > static_cast<long long>(vb->GetVertexCount()))
            throw std::runtime_error(
                std::string("TinyGLRenderer: the ") + route + " draw reads vertices [" +
                std::to_string(firstElement) + ", " + std::to_string(firstElement + vertexCount) +
                ") but the bound vertex buffer holds " + std::to_string(vb->GetVertexCount()) + ".");

        BindVertexArrays(*vb, state, route);
        LoadDrawMatrices(world, view, projection);
        glDrawArrays(mode, static_cast<GLint>(firstElement), vertexCount);
        UnbindVertexArrays(state.texture != nullptr);
    }

    void TinyGLRenderer::DrawIndexedCommon(const IVertexBufferRenderer& vbBase,
                                           const IIndexBufferRenderer& ibBase,
                                           const Matrix& world, const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive, int primitiveCount,
                                           const FixedFunctionDrawState& state, const char* route)
    {
        if (primitiveCount <= 0) return;
        const auto* vb = dynamic_cast<const TinyGLVertexBufferRenderer*>(&vbBase);
        const auto* ib = dynamic_cast<const TinyGLIndexBufferRenderer*>(&ibBase);
        if (vb == nullptr || ib == nullptr)
            throw std::runtime_error("TinyGLRenderer: the bound vertex/index buffer was not "
                                     "created by this renderer.");

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum mode = ToTinyGLMode(primitive);
        if (state.startIndex < 0 || state.startIndex + indexCount > ib->GetIndexCount())
            throw std::runtime_error(
                std::string("TinyGLRenderer: the ") + route + " draw reads indices [" +
                std::to_string(state.startIndex) + ", " +
                std::to_string(state.startIndex + indexCount) +
                ") but the bound index buffer holds " + std::to_string(ib->GetIndexCount()) + ".");

        // TinyGL has no glDrawElements (TINYGL-0). glArrayElement() is its own indexed fetch: it
        // reads element i from every enabled client array and emits the vertex, so replaying the
        // index list between glBegin/glEnd is a genuine indexed draw, not a CPU-side expansion into
        // a temporary vertex array.
        const long long vertexBase =
            static_cast<long long>(state.baseVertex) + static_cast<long long>(state.streamVertexOffset);
        std::vector<GLint> elements(static_cast<std::size_t>(indexCount));
        for (int i = 0; i < indexCount; ++i)
        {
            const long long element =
                vertexBase + static_cast<long long>(ib->IndexAt(state.startIndex + i));
            if (element < 0 || element >= static_cast<long long>(vb->GetVertexCount()))
            {
                throw std::runtime_error(
                    std::string("TinyGLRenderer: the ") + route + " draw decoded vertex index " +
                    std::to_string(element) + ", outside the bound vertex buffer's " +
                    std::to_string(vb->GetVertexCount()) + " vertices.");
            }
            elements[static_cast<std::size_t>(i)] = static_cast<GLint>(element);
        }

        BindVertexArrays(*vb, state, route);
        LoadDrawMatrices(world, view, projection);
        glBegin(mode);
        for (const GLint element : elements) glArrayElement(element);
        glEnd();
        UnbindVertexArrays(state.texture != nullptr);
    }

    void TinyGLRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount)
    {
        DrawCommon(vb, world, view, projection, primitive, primitiveCount,
                   FixedFunctionDrawState{}, "DrawColoredPrimitives");
    }

    void TinyGLRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                      const IIndexBufferRenderer& ib,
                                                      const Matrix& world, const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount)
    {
        DrawIndexedCommon(vb, ib, world, view, projection, primitive, primitiveCount,
                          FixedFunctionDrawState{}, "DrawIndexedColoredPrimitives");
    }

    void TinyGLRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount,
                                          const GpuDrawParams& params)
    {
        const FixedFunctionDrawState state = TranslateDrawParams(params, "DrawPrimitivesEx");
        DrawCommon(vb, world, view, projection, primitive, primitiveCount, state,
                   "DrawPrimitivesEx");
    }

    void TinyGLRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                 const IIndexBufferRenderer& ib,
                                                 const Matrix& world, const Matrix& view,
                                                 const Matrix& projection,
                                                 PrimitiveType primitive, int primitiveCount,
                                                 const GpuDrawParams& params)
    {
        const FixedFunctionDrawState state = TranslateDrawParams(params, "DrawIndexedPrimitivesEx");
        DrawIndexedCommon(vb, ib, world, view, projection, primitive, primitiveCount, state,
                          "DrawIndexedPrimitivesEx");
    }

    bool TinyGLRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        // Each of these has a real implementation, a public CNA path that reaches it, and a
        // permanent TinyGL regression test that fails if the implementation is removed.
        case CNA::GraphicsCapability::ThreeD:             // DrawPrimitivesEx/DrawIndexedPrimitivesEx
        case CNA::GraphicsCapability::WireFrame:          // ApplyRasterizerState -> glPolygonMode(GL_LINE)
            return true;
        // Not implemented, and reported as such rather than silently no-opped:
        //  - DepthStencilBuffer/StencilBuffer: TinyGL's ZBuffer has a depth plane and no stencil
        //    plane; ApplyDepthStencilState() refuses stencilEnable and stencil clears are legal
        //    absent-plane no-ops.
        //    DepthStencilBuffer is false because the pair is what the capability names -- the depth
        //    half alone is real, and ApplyDepthStencilState()/SetDepthTestEnabled() implement it.
        //  - AdditiveBlending: BlendState::Additive is (SourceAlpha, One), and TinyGL's factor
        //    switch has no SourceAlpha case; ApplyBlendState() refuses it.
        //  - MultiSampleAntiAliasing: TinyGL rasterizes one sample per pixel.
        //  - MultipleRenderTargets / Texture3D: CreateRenderTarget2D()/CreateRenderTargetCube()/
        //    CreateTexture3D() keep the interface's nullptr defaults, and SetRenderTargets()
        //    refuses a non-empty binding outright.
        //  - OcclusionQuery: CreateOcclusionQuery() keeps the nullptr default.
        //  - AnisotropicFiltering: one nearest sample per fragment; ValidateTextureFilter()
        //    refuses TextureFilter::Anisotropic.
        //  - CustomEffects: TinyGL is fixed-function and has no shader stage of any kind, so there
        //    is nothing to compile a CNA Effect into; CreateEffectRenderer() keeps the nullptr
        //    default and TinyGLSpriteBatchRenderer::SetCustomEffect() refuses a non-null Effect.
        //  - MultiStreamVertexInput / Instancing: TranslateDrawParams() refuses both.
        default:
            return false;
        }
    }

    void TinyGLRenderer::DrawTexturedQuadEXT(const TinyGLTextureRenderer& texture,
                                             const float positionsPixels[4][2],
                                             const float uvs[4][2],
                                             const float colorsRgba01[4][4],
                                             const Matrix& spriteTransform)
    {
        // The SpriteBatch ortho projection (0,0 top-left .. virtualWidth,virtualHeight
        // bottom-right), so corner positions arrive in ordinary destination pixel space and the
        // fixed-function transform alone performs the pixel -> NDC mapping. `spriteTransform`
        // composes on top exactly as EasyGLRenderer's own SpriteBatch does.
        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(impl_->viewportW), static_cast<float>(impl_->viewportH), 0.0f,
            0.0f, 1.0f);
        const Matrix combined = spriteTransform * orthoM;

        float columnMajor[16];
        combined.ToColumnMajor(columnMajor);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(columnMajor);

        const Matrix identity = Matrix::getIdentityProperty();
        identity.ToColumnMajor(columnMajor);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(columnMajor);

        // Two triangles: (0,1,2) and (0,2,3), corners ordered TL, TR, BR, BL. In XNA's top-left
        // screen space that order is clockwise, which is the front-facing winding this renderer
        // states with glFrontFace(GL_CW).
        const int order[6] = {0, 1, 2, 0, 2, 3};
        float positions[6][3];
        float texCoords[6][2];
        float colors[6][4];
        for (int i = 0; i < 6; ++i)
        {
            const int corner = order[i];
            positions[i][0] = positionsPixels[corner][0];
            positions[i][1] = positionsPixels[corner][1];
            positions[i][2] = 0.0f;
            texCoords[i][0] = uvs[corner][0];
            texCoords[i][1] = uvs[corner][1];
            for (int c = 0; c < 4; ++c) colors[i][c] = colorsRgba01[corner][c];
        }

        glBindTexture(GL_TEXTURE_2D,
                      static_cast<GLint>(texture.GLTextureHandle(
                          impl_->alphaCutoutMode, colorsRgba01[0][3])));
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, &positions[0][0]);
        glColorPointer(4, GL_FLOAT, 0, &colors[0][0]);
        glTexCoordPointer(2, GL_FLOAT, 0, &texCoords[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_2D);
    }

    // =============================================================================================
    // SpriteBatch
    // =============================================================================================

    TinyGLSpriteBatchRenderer::TinyGLSpriteBatchRenderer(TinyGLRenderer& owner) : owner_(owner) {}

    void TinyGLSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            Unsupported(
                "custom SpriteBatch Effects are not supported -- TinyGL is fixed-function and has "
                "no shader stage of any kind, which is why SupportsCapability(CustomEffects) "
                "reports false.");
    }

    void TinyGLSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Validate at Begin() time rather than at the first Draw(): SpriteBatch::Begin() treats a
        // throw here as "this batch never began".
        ValidateTextureFilter(textureFilter);
        samplerFilter_ = textureFilter;
    }

    void TinyGLSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        ValidateTextureAddressMode(addressU);
        ValidateTextureAddressMode(addressV);
        samplerAddressU_ = addressU;
        samplerAddressV_ = addressV;
    }

    void TinyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        if (!begun_)
            throw std::runtime_error("TinyGLSpriteBatchRenderer::Draw: Draw() called before Begin()");
        Draw(texture,
             Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void TinyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        if (!begun_)
            throw std::runtime_error("TinyGLSpriteBatchRenderer::Draw: Draw() called before Begin()");
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void TinyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float /*layerDepth*/)
    {
        if (!begun_)
            throw std::runtime_error("TinyGLSpriteBatchRenderer::Draw: Draw() called before Begin()");
        const auto* tinyTexture = dynamic_cast<const TinyGLTextureRenderer*>(&texture);
        if (tinyTexture == nullptr)
            throw std::runtime_error("TinyGLRenderer: the drawn texture was not created by this "
                                     "renderer.");
        const int textureWidth = tinyTexture->GetWidth();
        const int textureHeight = tinyTexture->GetHeight();
        if (textureWidth <= 0 || textureHeight <= 0) return;

        float u0 = static_cast<float>(sourceRectangle.X) / static_cast<float>(textureWidth);
        float v0 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(textureHeight);
        float u1 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) /
                   static_cast<float>(textureWidth);
        float v1 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) /
                   static_cast<float>(textureHeight);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
            std::swap(u0, u1);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
            std::swap(v0, v1);

        const float left = static_cast<float>(destinationRectangle.X);
        const float top = static_cast<float>(destinationRectangle.Y);
        const float width = static_cast<float>(destinationRectangle.Width);
        const float height = static_cast<float>(destinationRectangle.Height);
        const float sourceWidth = static_cast<float>(std::max(1, sourceRectangle.Width));
        const float sourceHeight = static_cast<float>(std::max(1, sourceRectangle.Height));
        const float scaleX = width / sourceWidth;
        const float scaleY = height / sourceHeight;

        // Corners relative to the rotation origin, then rotated and translated back -- the same
        // TL, TR, BR, BL order DrawTexturedQuadEXT() expects.
        const float ox = origin.X * scaleX;
        const float oy = origin.Y * scaleY;
        const float localX[4] = {-ox, width - ox, width - ox, -ox};
        const float localY[4] = {-oy, -oy, height - oy, height - oy};
        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        float positions[4][2];
        for (int i = 0; i < 4; ++i)
        {
            positions[i][0] = left + localX[i] * cosR - localY[i] * sinR;
            positions[i][1] = top + localX[i] * sinR + localY[i] * cosR;
        }
        const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;
        const float colors[4][4] = {{r, g, b, a}, {r, g, b, a}, {r, g, b, a}, {r, g, b, a}};

        owner_.DrawTexturedQuadEXT(*tinyTexture, positions, uvs, colors, transform_);
    }

}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<TinyGL::TinyGLRenderer>(args.virtualWidth, args.virtualHeight);
    }
}
