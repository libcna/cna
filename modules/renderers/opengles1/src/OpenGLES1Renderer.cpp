// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenGLES1/OpenGLES1Renderer.hpp"
#include "CNA/Internal/Renderers/Common/FixedFunctionArrayLayoutSupport.hpp"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace CNA::Internal::Renderers::OpenGLES1
{
    namespace
    {
        CNA::Platform::GlContextDescription RequestedContext(
            const int multiSampleCount, const bool withMultisampling)
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 1;
            description.minorVersion = 1;
            description.profile = CNA::Platform::GlProfile::Es;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            if (withMultisampling && multiSampleCount > 1)
            {
                description.multisampleBuffers = 1;
                description.multisampleSamples = multiSampleCount;
            }
            return description;
        }

        GLenum ToGLPrimitive(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:       return GL_LINES;
            case PrimitiveType::LineStrip:      return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:   return GL_POINTS;
            default:
                throw std::runtime_error("OpenGLES1: unrecognized primitive type");
            }
        }

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:       return primitiveCount * 2;
            case PrimitiveType::LineStrip:      return primitiveCount + 1;
            case PrimitiveType::PointListEXT:   return primitiveCount;
            default:
                throw std::runtime_error("OpenGLES1: unrecognized primitive type");
            }
        }

        // XNA Blend enum ordinals -> GL blend factor. One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9,
        // BlendFactor=10, InverseBlendFactor=11, SourceAlphaSaturation=12.
        //
        // ES 1.1 has no glBlendColor (GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR do not exist
        // in this profile at all, unlike desktop GL 1.4+) -- BlendFactor/InverseBlendFactor fall
        // back to SourceAlpha/InverseSourceAlpha, a documented deviation (see
        // docs/opengles1-renderer.md).
        GLenum ToGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
            case 1:  return GL_ZERO;
            case 2:  return GL_SRC_COLOR;
            case 3:  return GL_ONE_MINUS_SRC_COLOR;
            case 4:  return GL_SRC_ALPHA;
            case 5:  return GL_ONE_MINUS_SRC_ALPHA;
            case 6:  return GL_DST_COLOR;
            case 7:  return GL_ONE_MINUS_DST_COLOR;
            case 8:  return GL_DST_ALPHA;
            case 9:  return GL_ONE_MINUS_DST_ALPHA;
            case 10: return GL_SRC_ALPHA;
            case 11: return GL_ONE_MINUS_SRC_ALPHA;
            case 12: return GL_SRC_ALPHA_SATURATE;
            default: return GL_ONE;  // Blend::One = 0
            }
        }

        // XNA CompareFunction ordinals: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
        // GreaterEqual=5, Greater=6, NotEqual=7.
        GLenum ToGLCompareFunc(int xnaCompare)
        {
            switch (xnaCompare)
            {
            case 1: return GL_NEVER;
            case 2: return GL_LESS;
            case 3: return GL_LEQUAL;
            case 4: return GL_EQUAL;
            case 5: return GL_GEQUAL;
            case 6: return GL_GREATER;
            case 7: return GL_NOTEQUAL;
            default: return GL_ALWAYS;  // CompareFunction::Always = 0
            }
        }

        // XNA StencilOperation ordinals: Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4,
        // IncrementSaturation=5, DecrementSaturation=6, Invert=7.
        GLenum ToGLStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
            case 1: return GL_ZERO;
            case 2: return GL_REPLACE;
            case 3: return GL_INCR_WRAP_OES;
            case 4: return GL_DECR_WRAP_OES;
            case 5: return GL_INCR;
            case 6: return GL_DECR;
            case 7: return GL_INVERT;
            default: return GL_KEEP;  // StencilOperation::Keep = 0
            }
        }

        // XNA TextureAddressMode ordinals: Wrap=0, Clamp=1, Mirror=2.
        // ES 1.1 core has only GL_REPEAT and GL_CLAMP_TO_EDGE; mirrored repeat lives in the
        // optional GL_OES_texture_mirrored_repeat, so Mirror is honoured where the driver exposes
        // it and degrades to Wrap (a documented deviation) where it does not.
        GLenum ToGLWrapMode(int xnaAddressMode, bool mirroredRepeatSupported)
        {
            switch (xnaAddressMode)
            {
            case 1: return GL_CLAMP_TO_EDGE;
            case 2: return mirroredRepeatSupported ? GL_MIRRORED_REPEAT_OES : GL_REPEAT;
            default: return GL_REPEAT;  // TextureAddressMode::Wrap = 0
            }
        }

        // XNA TextureFilter ordinal 1 (Point) -> nearest; everything else -> linear (matches the
        // ISpriteBatchRenderer::SetSamplerFilter doc comment's own "others map to nearest" wording
        // for the inverse case -- here the default/linear-ish values map to GL_LINEAR).
        // XNA TextureFilter ordinals: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        // PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        // MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8.
        //
        // Magnification has no mip component by definition, so only the "Mag" half of each mode
        // matters here. Returning GL_LINEAR for everything except Point (as this did originally)
        // silently ignored the four Min*Mag* modes.
        GLenum ToGLMagFilter(int xnaFilter)
        {
            switch (xnaFilter)
            {
            case 1:  // Point
            case 5:  // MinLinearMagPointMipLinear
            case 6:  // MinLinearMagPointMipPoint
                return GL_NEAREST;
            default:
                return GL_LINEAR;
            }
        }

        // Minification without a mip chain: the mip half of the mode is irrelevant, so this only
        // distinguishes point from linear. Mip-aware minification filters are not selected yet --
        // see OPENGLES1-85's row for what remains.
        GLenum ToGLMinFilter(int xnaFilter)
        {
            switch (xnaFilter)
            {
            case 1:  // Point
            case 4:  // PointMipLinear
            case 7:  // MinPointMagLinearMipLinear
            case 8:  // MinPointMagLinearMipPoint
                return GL_NEAREST;
            default:
                return GL_LINEAR;
            }
        }
    }

    // -------------------------------------------------------------------------
    // OpenGLES1TextureRenderer
    // -------------------------------------------------------------------------

    OpenGLES1TextureRenderer::OpenGLES1TextureRenderer(OpenGLES1Renderer* owner, int width, int height)
        : owner_(owner), width_(width), height_(height)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (owner_) owner_->RegisterTextureEXT(this);
    }

    void OpenGLES1TextureRenderer::ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels)
    {
        // Declining the share is what makes "recovery disabled" actually free memory.
        if (owner_ && !owner_->IsContextRecoveryEnabledEXT()) return;
        pixels_ = std::move(pixels);
    }

    bool OpenGLES1TextureRenderer::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (level != 0 || !data || w <= 0 || h <= 0) return false;
        if (dataLength < w * h * 4) return false;
        auto* pixels = static_cast<uint8_t*>(data);

        // Preferred route: attach this texture to a scratch framebuffer and read it back, so the
        // caller sees what the GPU actually holds rather than what was last uploaded.
        if (owner_ && owner_->HasFramebufferObjectsEXT() && texture_ != 0)
        {
            GLint previousFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previousFbo);

            GLuint scratch = 0;
            owner_->GenFramebufferEXT(&scratch);
            owner_->BindFramebufferEXT(scratch);
            owner_->AttachTexture2DEXT(texture_);

            if (owner_->IsFramebufferCompleteEXT())
            {
                const int flippedY = height_ - y - h;
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(x, flippedY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                std::vector<uint8_t> row(static_cast<std::size_t>(w) * 4);
                for (int top = 0, bottom = h - 1; top < bottom; ++top, --bottom)
                {
                    uint8_t* topRow = pixels + static_cast<std::size_t>(top) * w * 4;
                    uint8_t* bottomRow = pixels + static_cast<std::size_t>(bottom) * w * 4;
                    std::memcpy(row.data(), topRow, row.size());
                    std::memcpy(topRow, bottomRow, row.size());
                    std::memcpy(bottomRow, row.data(), row.size());
                }

                owner_->BindFramebufferEXT(static_cast<GLuint>(previousFbo));
                owner_->DeleteFramebufferEXT(&scratch);
                return true;
            }

            owner_->BindFramebufferEXT(static_cast<GLuint>(previousFbo));
            owner_->DeleteFramebufferEXT(&scratch);
        }

        // Fallback: the shared CPU copy. Correct for anything uploaded through SetData, which is
        // every ordinary texture -- just not evidence of the GPU's own contents.
        if (!pixels_ || pixels_->empty()) return false;
        if (pixels_->size() < static_cast<std::size_t>(width_) * height_ * 4) return false;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t src = (static_cast<std::size_t>(y + row) * width_ + x) * 4;
            if (src + static_cast<std::size_t>(w) * 4 > pixels_->size()) return false;
            std::memcpy(pixels + static_cast<std::size_t>(row) * w * 4, pixels_->data() + src,
                        static_cast<std::size_t>(w) * 4);
        }
        return true;
    }

    void OpenGLES1TextureRenderer::RestoreAfterContextLoss()
    {
        // The old name belonged to the destroyed context; asking GL to delete it would be
        // meaningless, so just take a fresh one and rebuild the whole object.
        texture_ = 0;
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Without a CPU copy there is nothing to restore from -- leave the (now empty) texture
        // rather than sampling from a dead name.
        if (!pixels_ || pixels_->empty()) return;
        if (pixels_->size() < static_cast<std::size_t>(width_) * height_ * 4) return;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels_->data());
    }

    OpenGLES1TextureRenderer::~OpenGLES1TextureRenderer()
    {
        if (owner_) owner_->UnregisterTextureEXT(this);
        if (texture_) glDeleteTextures(1, &texture_);
    }

    void OpenGLES1TextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    void OpenGLES1TextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    void OpenGLES1TextureRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    // -------------------------------------------------------------------------
    // OpenGLES1VertexBufferRenderer / OpenGLES1IndexBufferRenderer
    // -------------------------------------------------------------------------

    OpenGLES1VertexBufferRenderer::OpenGLES1VertexBufferRenderer(OpenGLES1Renderer* owner, int vertexCapacity)
        : owner_(owner), vertexCapacity_(vertexCapacity)
    {
        glGenBuffers(1, &buffer_);
        if (owner_) owner_->RegisterVertexBufferEXT(this);
    }

    OpenGLES1VertexBufferRenderer::~OpenGLES1VertexBufferRenderer()
    {
        if (owner_) owner_->UnregisterVertexBufferEXT(this);
        if (buffer_) glDeleteBuffers(1, &buffer_);
    }

    void OpenGLES1VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        stride_ = stride_in_bytes;
        vertexCount_ = vertex_count;
        const std::size_t bytes = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_DYNAMIC_DRAW);

        // Shadowed only to survive a context loss (OPENGLES1-80); draws never read this copy.
        if (owner_ && !owner_->IsContextRecoveryEnabledEXT())
        {
            cpuShadow_.clear();
            cpuShadow_.shrink_to_fit();
            return;
        }
        cpuShadow_.resize(bytes);
        if (bytes > 0 && data) std::memcpy(cpuShadow_.data(), data, bytes);
    }

    void OpenGLES1VertexBufferRenderer::DropCpuShadowEXT()
    {
        cpuShadow_.clear();
        cpuShadow_.shrink_to_fit();
    }

    void OpenGLES1VertexBufferRenderer::RestoreAfterContextLoss()
    {
        // The old name died with the old context -- deleting it would be meaningless.
        buffer_ = 0;
        glGenBuffers(1, &buffer_);
        if (cpuShadow_.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuShadow_.size()),
                     cpuShadow_.data(), GL_DYNAMIC_DRAW);
    }

    void OpenGLES1VertexBufferRenderer::Bind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    }

    OpenGLES1IndexBufferRenderer::OpenGLES1IndexBufferRenderer(OpenGLES1Renderer* owner, int indexCapacity,
                                                             bool thirtyTwoBit)
        : owner_(owner), indexCapacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
        glGenBuffers(1, &buffer_);
        if (owner_) owner_->RegisterIndexBufferEXT(this);
    }

    OpenGLES1IndexBufferRenderer::~OpenGLES1IndexBufferRenderer()
    {
        if (owner_) owner_->UnregisterIndexBufferEXT(this);
        if (buffer_) glDeleteBuffers(1, &buffer_);
    }

    void OpenGLES1IndexBufferRenderer::RestoreAfterContextLoss()
    {
        buffer_ = 0;
        glGenBuffers(1, &buffer_);
        if (cpuShadow_.empty()) return;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_);

        if (thirtyTwoBit_)
        {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(cpuShadow_.size() * sizeof(uint32_t)),
                         cpuShadow_.data(), GL_DYNAMIC_DRAW);
            return;
        }

        // The shadow is widened; narrow it back for the GPU copy.
        std::vector<uint16_t> narrow(cpuShadow_.begin(), cpuShadow_.end());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(narrow.size() * sizeof(uint16_t)),
                     narrow.data(), GL_DYNAMIC_DRAW);
    }

    void OpenGLES1IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        indexCount_ = index_count;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(static_cast<std::size_t>(index_count) * sizeof(uint16_t)),
                    data, GL_DYNAMIC_DRAW);

        // Widened on the CPU side so the shadow has one shape regardless of index size.
        const auto* src = static_cast<const uint16_t*>(data);
        cpuShadow_.assign(src, src + (index_count > 0 ? index_count : 0));
    }

    void OpenGLES1IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error("OpenGLES1IndexBufferRenderer: SetData32 on a 16-bit index buffer");

        indexCount_ = index_count;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(static_cast<std::size_t>(index_count) * sizeof(uint32_t)),
                    data, GL_DYNAMIC_DRAW);

        const auto* src = static_cast<const uint32_t*>(data);
        cpuShadow_.assign(src, src + (index_count > 0 ? index_count : 0));
    }

    void OpenGLES1IndexBufferRenderer::Bind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_);
    }

    // -------------------------------------------------------------------------
    // OpenGLES1SpriteBatchRenderer
    // -------------------------------------------------------------------------

    void OpenGLES1SpriteBatchRenderer::Begin()
    {
        begun_ = true;
    }

    void OpenGLES1SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGLES1SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h),
             Microsoft::Xna::Framework::Color::White);
    }

    void OpenGLES1SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenGLES1SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           float layerDepth)
    {
        if (!begun_) throw std::runtime_error("Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if ((int)effects & (int)SpriteEffects::FlipHorizontally) std::swap(u1, u2);
        if ((int)effects & (int)SpriteEffects::FlipVertically) std::swap(v1, v2);

        const float r = (float)color.getRProperty() / 255.0f;
        const float g = (float)color.getGProperty() / 255.0f;
        const float b = (float)color.getBProperty() / 255.0f;
        const float a = (float)color.getAProperty() / 255.0f;

        const float dx = (float)destinationRectangle.X;
        const float dy = (float)destinationRectangle.Y;
        const float dw = (float)destinationRectangle.Width;
        const float dh = (float)destinationRectangle.Height;

        const float sw = (float)sourceRectangle.Width;
        const float sh = (float)sourceRectangle.Height;

        const float ox = origin.X;
        const float oy = origin.Y;

        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        pendingVertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        pendingVertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        pendingVertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }

    void OpenGLES1SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        if (owner_) owner_->ApplyLogicalViewportAndOrtho2D();

        {
            // GL applies MODELVIEW then PROJECTION to each vertex (column-vector convention),
            // matching XNA's row-vector `vertex * transform_ * orthoProjection` as long as each
            // matrix is independently loaded in its own already-transposed column-major form —
            // no need to premultiply transform_ into a single combined matrix first.
            float m[16];
            transform_.ToColumnMajor(m);
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(m);
        }

        glEnable(GL_TEXTURE_2D);
        currentTexture_->BindGL();
        if (owner_) owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);

        const std::size_t stride = sizeof(Vertex);
        const uint8_t* base = reinterpret_cast<const uint8_t*>(pendingVertices_.data());
        glVertexPointer(2, GL_FLOAT, static_cast<GLsizei>(stride), base + offsetof(Vertex, x));
        glTexCoordPointer(2, GL_FLOAT, static_cast<GLsizei>(stride), base + offsetof(Vertex, u));
        glColorPointer(4, GL_FLOAT, static_cast<GLsizei>(stride), base + offsetof(Vertex, r));

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(pendingIndices_.size()),
                       GL_UNSIGNED_SHORT, pendingIndices_.data());

        glDisableClientState(GL_COLOR_ARRAY);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    // -------------------------------------------------------------------------
    // OpenGLES1Renderer — construction / context lifecycle
    // -------------------------------------------------------------------------

    OpenGLES1Renderer::OpenGLES1Renderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , platformGlService_(&RequirePlatformGlContext(args.glContext, "OPENGLES1"))
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , swapInterval_(args.swapInterval)
        , requestedMultiSampleCount_(args.multiSampleCount)
    {
        RequirePlatformGlWindow(args.surface, "OPENGLES1");

        CreateGLContext();
        LoadExtensionEntryPoints();

        std::cout << "OpenGLES1Renderer initialized with OpenGL ES "
                  << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

        platformContext_->SetSwapInterval(swapInterval_);

        glShadeModel(GL_SMOOTH);
        glEnable(GL_NORMALIZE);
        IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
    }

    OpenGLES1Renderer::~OpenGLES1Renderer()
    {
        // Release the carrier texture while its context is still current; destroying the context
        // would take it anyway, but leaving a live name behind makes leak tooling noisier.
        if (whiteTexture_)
        {
            glDeleteTextures(1, &whiteTexture_);
            whiteTexture_ = 0;
        }

        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
    }

    void OpenGLES1Renderer::CreateGLContext()
    {
        // Requests a genuine OpenGL ES 1.1 (fixed-function "Common", CM) context -- this is the
        // one attribute set that distinguishes this renderer from EasyGL's ES 3.0 request. See
        // docs/opengles1-renderer.md for
        // this project's own empirical finding that not every EGL/GLES driver actually implements
        // ES1 context creation despite advertising ES1-capable configs (a real Mesa llvmpipe
        // limitation found during this renderer's own bring-up, not a theoretical concern).
        // OPENGLES1-87: backbuffer MSAA. Requesting samples the driver cannot give makes context
        // creation fail outright, so a failed attempt is retried without them rather than taking
        // the whole device down over an optional quality setting.
        const bool wantMsaa = requestedMultiSampleCount_ > 1;
        try
        {
            platformContext_ = std::make_unique<PlatformGlContextOwner>(
                *platformGlService_, surface_.GetWindowId(),
                RequestedContext(requestedMultiSampleCount_, wantMsaa));
        }
        catch (const CNA::Platform::PlatformException&)
        {
            if (!wantMsaa) throw;
            platformContext_ = std::make_unique<PlatformGlContextOwner>(
                *platformGlService_, surface_.GetWindowId(),
                RequestedContext(requestedMultiSampleCount_, false));
        }

        // Report what was actually granted, never what was asked for.
        const auto granted = platformContext_->GetAttributes();
        actualMultiSampleCount_ =
            (granted.multisampleBuffers > 0 && granted.multisampleSamples > 1)
                ? granted.multisampleSamples : 0;
    }

    void OpenGLES1Renderer::DestroyGLContext()
    {
        platformContext_.reset();
    }

    namespace
    {
        bool HasGLExtension(const char* name)
        {
            const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if (!extensions) return false;
            return std::string_view(extensions).find(name) != std::string_view::npos;
        }
    }

    void OpenGLES1Renderer::LoadExtensionEntryPoints()
    {
        glBlendFuncSeparateOES_ = reinterpret_cast<PFNGLBLENDFUNCSEPARATEOESPROC>(
            LoadPlatformGlProcAddress("glBlendFuncSeparateOES"));
        glBlendEquationOES_ = reinterpret_cast<PFNGLBLENDEQUATIONOESPROC>(
            LoadPlatformGlProcAddress("glBlendEquationOES"));

        // GL_OES_framebuffer_object (OPENGLES1-72): optional -- RenderTarget2D support is gated on
        // every one of these resolving, not just the extension string (a driver could advertise
        // the string but only implement part of it; resolving is the real capability check).
        // Pure state-only extension -- no entry points to resolve, so the string is the whole
        // capability check (unlike the FBO/cube-map ones below).
        mirroredRepeatSupported_ = HasGLExtension("GL_OES_texture_mirrored_repeat");
        elementIndexUintSupported_ = HasGLExtension("GL_OES_element_index_uint");

        // OPENGLES1-88: Min/Max blend equations and a separate alpha equation.
        blendMinMaxSupported_ = HasGLExtension("GL_EXT_blend_minmax");
        glBlendEquationSeparateOES_ = reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEOESPROC>(
            LoadPlatformGlProcAddress("glBlendEquationSeparateOES"));
        if (!HasGLExtension("GL_OES_blend_equation_separate"))
            glBlendEquationSeparateOES_ = nullptr;

        // OPENGLES1-92: a 1x1 white carrier texture for the vertex-colour x DiffuseColor combine.
        // Recreated here rather than in the constructor so it survives DebugRestoreContext().
        whiteTexture_ = 0;
        glGenTextures(1, &whiteTexture_);
        glBindTexture(GL_TEXTURE_2D, whiteTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        {
            static constexpr uint8_t kWhite[4] = {255, 255, 255, 255};
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhite);
        }

        // OPENGLES1-86: anisotropic filtering. The cap is queried rather than assumed -- never
        // request more than the driver grants.
        maxAnisotropy_ = 1.0f;
        if (HasGLExtension("GL_EXT_texture_filter_anisotropic"))
        {
            GLfloat cap = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &cap);
            if (cap > 1.0f) maxAnisotropy_ = cap;
        }

        glGenerateMipmapOES_ = reinterpret_cast<PFNGLGENERATEMIPMAPOESPROC>(
            LoadPlatformGlProcAddress("glGenerateMipmapOES"));

        fboSupported_ = HasGLExtension("GL_OES_framebuffer_object");
        if (fboSupported_)
        {
            glGenFramebuffersOES_ = reinterpret_cast<PFNGLGENFRAMEBUFFERSOESPROC>(LoadPlatformGlProcAddress("glGenFramebuffersOES"));
            glBindFramebufferOES_ = reinterpret_cast<PFNGLBINDFRAMEBUFFEROESPROC>(LoadPlatformGlProcAddress("glBindFramebufferOES"));
            glDeleteFramebuffersOES_ = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSOESPROC>(LoadPlatformGlProcAddress("glDeleteFramebuffersOES"));
            glFramebufferTexture2DOES_ = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DOESPROC>(LoadPlatformGlProcAddress("glFramebufferTexture2DOES"));
            glFramebufferRenderbufferOES_ = reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFEROESPROC>(LoadPlatformGlProcAddress("glFramebufferRenderbufferOES"));
            glCheckFramebufferStatusOES_ = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSOESPROC>(LoadPlatformGlProcAddress("glCheckFramebufferStatusOES"));
            glGenRenderbuffersOES_ = reinterpret_cast<PFNGLGENRENDERBUFFERSOESPROC>(LoadPlatformGlProcAddress("glGenRenderbuffersOES"));
            glBindRenderbufferOES_ = reinterpret_cast<PFNGLBINDRENDERBUFFEROESPROC>(LoadPlatformGlProcAddress("glBindRenderbufferOES"));
            glDeleteRenderbuffersOES_ = reinterpret_cast<PFNGLDELETERENDERBUFFERSOESPROC>(LoadPlatformGlProcAddress("glDeleteRenderbuffersOES"));
            glRenderbufferStorageOES_ = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEOESPROC>(LoadPlatformGlProcAddress("glRenderbufferStorageOES"));
            fboSupported_ = glGenFramebuffersOES_ && glBindFramebufferOES_ && glDeleteFramebuffersOES_
                          && glFramebufferTexture2DOES_ && glFramebufferRenderbufferOES_
                          && glCheckFramebufferStatusOES_ && glGenRenderbuffersOES_
                          && glBindRenderbufferOES_ && glDeleteRenderbuffersOES_ && glRenderbufferStorageOES_;
        }

        // GL_OES_texture_cube_map (OPENGLES1-74): also gates real EnvironmentMapEffect support
        // (glTexGeniOES + GL_REFLECTION_MAP_OES, part of the same extension).
        cubeMapSupported_ = HasGLExtension("GL_OES_texture_cube_map");
        if (cubeMapSupported_)
        {
            glTexGeniOES_ = reinterpret_cast<PFNGLTEXGENIOESPROC>(LoadPlatformGlProcAddress("glTexGeniOES"));
            cubeMapSupported_ = glTexGeniOES_ != nullptr;
        }

        GLint maxTextureUnits = 1;
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
        maxTextureUnits_ = maxTextureUnits;
    }

    void OpenGLES1Renderer::DebugSimulateContextLoss()
    {
        // The name dies with the context; clearing it keeps "0 means none" true in between.
        whiteTexture_ = 0;
        DestroyGLContext();
    }

    void OpenGLES1Renderer::DebugRestoreContext()
    {
        CreateGLContext();
        LoadExtensionEntryPoints();
        platformContext_->SetSwapInterval(swapInterval_);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_NORMALIZE);

        // Every GL object died with the old context, so each live resource is rebuilt from its own
        // CPU-side copy -- otherwise a restored context samples every texture as plain white and
        // draws from dead buffer names.
        for (auto* texture : liveTextures_)
            if (texture) texture->RestoreAfterContextLoss();
        for (auto* buffer : liveVertexBuffers_)
            if (buffer) buffer->RestoreAfterContextLoss();
        for (auto* buffer : liveIndexBuffers_)
            if (buffer) buffer->RestoreAfterContextLoss();
    }

    // -------------------------------------------------------------------------
    // Presentation / viewport
    // -------------------------------------------------------------------------

    void OpenGLES1Renderer::GetPhysicalSize(int& width, int& height) const
    {
        surface_.GetDrawableSize(width, height);
    }

    void OpenGLES1Renderer::GetLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSize(width, height);
            return;
        }
        int physW, physH;
        GetPhysicalSize(physW, physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>((double)physW * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    bool OpenGLES1Renderer::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (!currentRenderTarget_) return false;
        width = currentRenderTarget_->GetWidth();
        height = currentRenderTarget_->GetHeight();
        return true;
    }

    void OpenGLES1Renderer::ApplyLogicalViewportAndOrtho2D()
    {
        int physW = 0, physH = 0;
        int logW = 0, logH = 0;
        // Task 1078-equivalent: a SpriteBatch flush while a RenderTarget2D is bound must size its
        // viewport/orthographic projection to the RT, not the window (see
        // EasyGLRenderer::FlushBatch's identical GetCurrentRenderTarget2DSize() check).
        if (GetCurrentRenderTarget2DSize(physW, physH))
        {
            glViewport(0, 0, physW, physH);
            logW = physW;
            logH = physH;
        }
        else
        {
            GetPhysicalSize(physW, physH);
            if (physW > 0 && physH > 0)
                glViewport(0, 0, physW, physH);
            GetLogicalSize(logW, logH);
        }
        if (logW <= 0 || logH <= 0)
        {
            logW = physW;
            logH = physH;
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        // Top-left origin, matching XNA's SpriteBatch pixel convention (y grows downward).
        glOrthof(0.0f, static_cast<float>(logW), static_cast<float>(logH), 0.0f, -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void OpenGLES1Renderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void OpenGLES1Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void OpenGLES1Renderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGLES1Renderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void OpenGLES1Renderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    bool OpenGLES1Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                             float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = surface_.WindowToDrawable(windowX) * scale;
        logY = surface_.WindowToDrawable(windowY) * scale;
        return true;
    }

    bool OpenGLES1Renderer::TransformLogicalToWindow(float logX, float logY,
                                                            float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical -- see EasyGLRenderer's identical method for
        // why this is a pure uniform scale with no offset under the default FixedHeightDynamicWidth
        // presentation mode (no letterbox bars, so no offset to invert).
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = surface_.DrawableToWindow(logX * invScale);
        windowY = surface_.DrawableToWindow(logY * invScale);
        return true;
    }

    void OpenGLES1Renderer::Present()
    {
        platformContext_->SwapBuffers();
    }

    // -------------------------------------------------------------------------
    // Clears
    // -------------------------------------------------------------------------

    void OpenGLES1Renderer::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearDepth(float depth)
    {
        glClearDepthf(depth);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepthf(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // GL's window-space Y grows upward; XNA/CNA's (x,y) here are top-left game coordinates,
        // so flip the row origin before reading, then flip the rows back afterward (matches every
        // other GL-based renderer's ReadBackbuffer convention).
        int physW, physH;
        GetPhysicalSize(physW, physH);
        const int flippedY = physH - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, flippedY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        std::vector<uint8_t> row(static_cast<std::size_t>(w) * 4);
        for (int top = 0, bottom = h - 1; top < bottom; ++top, --bottom)
        {
            uint8_t* topRow = pixels + static_cast<std::size_t>(top) * w * 4;
            uint8_t* bottomRow = pixels + static_cast<std::size_t>(bottom) * w * 4;
            std::memcpy(row.data(), topRow, row.size());
            std::memcpy(topRow, bottomRow, row.size());
            std::memcpy(bottomRow, row.data(), row.size());
        }
    }

    // -------------------------------------------------------------------------
    // Depth/blend toggles
    // -------------------------------------------------------------------------

    void OpenGLES1Renderer::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void OpenGLES1Renderer::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void OpenGLES1Renderer::SetDepthWriteEnabled(bool enabled)
    {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    // -------------------------------------------------------------------------
    // Resource creation
    // -------------------------------------------------------------------------

    void OpenGLES1Renderer::RegisterTextureEXT(OpenGLES1TextureRenderer* texture)
    {
        if (texture) liveTextures_.push_back(texture);
    }

    void OpenGLES1Renderer::UnregisterTextureEXT(OpenGLES1TextureRenderer* texture)
    {
        if (!texture) return;
        liveTextures_.erase(std::remove(liveTextures_.begin(), liveTextures_.end(), texture),
                            liveTextures_.end());
    }

    void OpenGLES1Renderer::SetContextRecoveryEnabled(bool enabled)
    {
        contextRecoveryEnabled_ = enabled;
        if (enabled) return;

        // Actually release the memory. Texture2D dropping its own copy achieves nothing on its own,
        // because ShareCpuPixels gave this renderer shared ownership of the very same buffer.
        for (auto* texture : liveTextures_)
            if (texture) texture->DropCpuShadowEXT();
        for (auto* buffer : liveVertexBuffers_)
            if (buffer) buffer->DropCpuShadowEXT();

        // Index shadows stay: wireframe emulation reads them, so dropping them would break
        // rendering rather than just recovery (see OPENGLES1-76).
    }

    void OpenGLES1Renderer::RegisterVertexBufferEXT(OpenGLES1VertexBufferRenderer* buffer)
    {
        if (buffer) liveVertexBuffers_.push_back(buffer);
    }

    void OpenGLES1Renderer::UnregisterVertexBufferEXT(OpenGLES1VertexBufferRenderer* buffer)
    {
        if (!buffer) return;
        liveVertexBuffers_.erase(std::remove(liveVertexBuffers_.begin(), liveVertexBuffers_.end(), buffer),
                                 liveVertexBuffers_.end());
    }

    void OpenGLES1Renderer::RegisterIndexBufferEXT(OpenGLES1IndexBufferRenderer* buffer)
    {
        if (buffer) liveIndexBuffers_.push_back(buffer);
    }

    void OpenGLES1Renderer::UnregisterIndexBufferEXT(OpenGLES1IndexBufferRenderer* buffer)
    {
        if (!buffer) return;
        liveIndexBuffers_.erase(std::remove(liveIndexBuffers_.begin(), liveIndexBuffers_.end(), buffer),
                                liveIndexBuffers_.end());
    }

    std::unique_ptr<ITextureRenderer> OpenGLES1Renderer::CreateTexture(const ImageData& data)
    {
        auto tex = std::make_unique<OpenGLES1TextureRenderer>(this, data.width, data.height);
        tex->UpdatePixels(data.pixels.data(), data.width * 4);
        return tex;
    }

    std::unique_ptr<ISpriteBatchRenderer> OpenGLES1Renderer::CreateSpriteBatch()
    {
        return std::make_unique<OpenGLES1SpriteBatchRenderer>(this);
    }

    std::unique_ptr<IVertexBufferRenderer> OpenGLES1Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<OpenGLES1VertexBufferRenderer>(this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGLES1Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<OpenGLES1IndexBufferRenderer>(this, index_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGLES1Renderer::CreateIndexBuffer32(int index_capacity)
    {
        // GL_UNSIGNED_INT indices are not ES 1.1 core. The base implementation rejects the
        // request explicitly; it must never manufacture a 16-bit buffer for a 32-bit declaration.
        if (!elementIndexUintSupported_)
            return IGraphicsRenderer::CreateIndexBuffer32(index_capacity);

        return std::make_unique<OpenGLES1IndexBufferRenderer>(this, index_capacity, /*thirtyTwoBit=*/true);
    }

    // -------------------------------------------------------------------------
    // OpenGLES1RenderTargetRenderer (OPENGLES1-72, GL_OES_framebuffer_object)
    // -------------------------------------------------------------------------

    OpenGLES1RenderTargetRenderer::OpenGLES1RenderTargetRenderer(OpenGLES1Renderer* owner,
                                                                int width, int height, int depthFormat,
                                                                bool mipMap)
        : owner_(owner), width_(width), height_(height), mipMap_(mipMap)
    {
        // Mip generation needs glGenerateMipmapOES; without it the request is honoured as a
        // single-level target rather than pretending to have a chain.
        if (mipMap_ && !owner_->HasGenerateMipmapEXT()) mipMap_ = false;

        if (mipMap_)
        {
            int levelW = width, levelH = height;
            levelCount_ = 1;
            while (levelW > 1 || levelH > 1)
            {
                levelW = levelW > 1 ? levelW / 2 : 1;
                levelH = levelH > 1 ? levelH / 2 : 1;
                ++levelCount_;
            }
        }

        glGenTextures(1, &colorTexture_);
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        mipMap_ ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Every level needs defined storage before glGenerateMipmapOES writes into it -- otherwise
        // levels 1+ are GL-incomplete and the texture samples as undefined. Same lesson EasyGL's
        // own render target already records.
        {
            int levelW = width, levelH = height;
            for (int level = 0; level < levelCount_; ++level)
            {
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                levelW = levelW > 1 ? levelW / 2 : 1;
                levelH = levelH > 1 ? levelH / 2 : 1;
            }
        }

        owner_->glGenFramebuffersOES_(1, &fbo_);
        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, fbo_);
        owner_->glFramebufferTexture2DOES_(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, colorTexture_, 0);

        // DepthFormat raw ordinal: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3. GL_OES_depth24/
        // GL_OES_packed_depth_stencil are separate, less-universal extensions than
        // GL_OES_framebuffer_object itself -- silently falls back to a 16-bit depth-only
        // renderbuffer when either is absent (documented deviation, see docs/opengles1-renderer.md).
        if (depthFormat != 0)
        {
            owner_->glGenRenderbuffersOES_(1, &depthRenderbuffer_);
            owner_->glBindRenderbufferOES_(GL_RENDERBUFFER_OES, depthRenderbuffer_);

            GLenum internalFormat = GL_DEPTH_COMPONENT16_OES;
            hasDepth_ = true;
            hasStencil_ = false;
            if (depthFormat == 3 && HasGLExtension("GL_OES_packed_depth_stencil"))
            {
                internalFormat = GL_DEPTH24_STENCIL8_OES;
                hasStencil_ = true;
            }
            else if (depthFormat >= 2 && HasGLExtension("GL_OES_depth24"))
            {
                internalFormat = GL_DEPTH_COMPONENT24_OES;
            }

            owner_->glRenderbufferStorageOES_(GL_RENDERBUFFER_OES, internalFormat, width, height);
            owner_->glFramebufferRenderbufferOES_(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES,
                                                  GL_RENDERBUFFER_OES, depthRenderbuffer_);
            if (hasStencil_)
                owner_->glFramebufferRenderbufferOES_(GL_FRAMEBUFFER_OES, GL_STENCIL_ATTACHMENT_OES,
                                                      GL_RENDERBUFFER_OES, depthRenderbuffer_);
        }

        const GLenum status = owner_->glCheckFramebufferStatusOES_(GL_FRAMEBUFFER_OES);
        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE_OES)
        {
            throw std::runtime_error(
                "OpenGLES1: RenderTarget2D framebuffer incomplete (GL_OES_framebuffer_object "
                "status 0x" + std::to_string(status) + ")");
        }
    }

    OpenGLES1RenderTargetRenderer::~OpenGLES1RenderTargetRenderer()
    {
        if (depthRenderbuffer_) owner_->glDeleteRenderbuffersOES_(1, &depthRenderbuffer_);
        if (fbo_) owner_->glDeleteFramebuffersOES_(1, &fbo_);
        if (colorTexture_) glDeleteTextures(1, &colorTexture_);
    }

    void OpenGLES1RenderTargetRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
    }

    void OpenGLES1RenderTargetRenderer::BindAsRenderTarget()
    {
        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, fbo_);
    }

    void OpenGLES1RenderTargetRenderer::UnbindAsRenderTarget()
    {
        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, 0);

        // Regenerate the chain from the freshly rendered level 0, mirroring FNA3D's own
        // ResolveTarget behaviour (and EasyGL's copy of it).
        if (mipMap_ && owner_->HasGenerateMipmapEXT())
        {
            glBindTexture(GL_TEXTURE_2D, colorTexture_);
            owner_->GenerateMipmapEXT(GL_TEXTURE_2D);
        }
    }

    bool OpenGLES1RenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        // Only level 0 exists here -- this renderer does not generate mips (see CreateRenderTarget2D).
        if (level != 0 || !data || w <= 0 || h <= 0) return false;
        if (dataLength < w * h * 4) return false;
        if (!owner_ || !owner_->glBindFramebufferOES_ || fbo_ == 0) return false;

        // ES 1.1 has no glGetTexImage, so the only way back out of the colour attachment is to
        // read it through its own FBO. Restore the previously bound target afterwards rather than
        // assuming the backbuffer was current -- GetData() may be called while another target is
        // still bound.
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previousFbo);
        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, fbo_);

        // Same top-left-origin convention as ReadBackbuffer(): flip the requested origin into GL's
        // bottom-up window space, then flip the returned rows back.
        auto* pixels = static_cast<uint8_t*>(data);
        const int flippedY = height_ - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, flippedY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        std::vector<uint8_t> row(static_cast<std::size_t>(w) * 4);
        for (int top = 0, bottom = h - 1; top < bottom; ++top, --bottom)
        {
            uint8_t* topRow = pixels + static_cast<std::size_t>(top) * w * 4;
            uint8_t* bottomRow = pixels + static_cast<std::size_t>(bottom) * w * 4;
            std::memcpy(row.data(), topRow, row.size());
            std::memcpy(topRow, bottomRow, row.size());
            std::memcpy(bottomRow, row.data(), row.size());
        }

        owner_->glBindFramebufferOES_(GL_FRAMEBUFFER_OES, static_cast<GLuint>(previousFbo));
        return true;
    }


    // -------------------------------------------------------------------------
    // OpenGLES1RenderTargetCubeRenderer (OPENGLES1-84)
    // -------------------------------------------------------------------------

    namespace
    {
        // Cube face ordinals follow XNA's CubeMapFace: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
        GLenum CubeFaceTarget(int face)
        {
            static constexpr GLenum kFaces[6] = {
                GL_TEXTURE_CUBE_MAP_POSITIVE_X_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_X_OES,
                GL_TEXTURE_CUBE_MAP_POSITIVE_Y_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_OES,
                GL_TEXTURE_CUBE_MAP_POSITIVE_Z_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_OES,
            };
            return kFaces[(face >= 0 && face < 6) ? face : 0];
        }
    }

    OpenGLES1RenderTargetCubeRenderer::OpenGLES1RenderTargetCubeRenderer(OpenGLES1Renderer* owner,
                                                                       int size, int depthFormat)
        : owner_(owner), size_(size)
    {
        glGenTextures(1, &cubeTexture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP_OES, cubeTexture_);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Every face needs defined storage before it can be a colour attachment.
        for (int face = 0; face < 6; ++face)
            glTexImage2D(CubeFaceTarget(face), 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        owner_->GenFramebufferEXT(&fbo_);
        owner_->BindFramebufferEXT(fbo_);

        if (depthFormat != 0)
        {
            // One depth buffer shared by all six faces: only one face is ever the draw target at a
            // time, so a per-face buffer would waste memory for no behavioural difference.
            owner_->GenRenderbufferEXT(&depthRenderbuffer_);
            owner_->BindRenderbufferEXT(depthRenderbuffer_);
            owner_->RenderbufferStorageEXT(GL_DEPTH_COMPONENT16_OES, size, size);
            owner_->AttachDepthRenderbufferEXT(depthRenderbuffer_);
            hasDepth_ = true;
        }

        owner_->BindFramebufferEXT(0);
    }

    OpenGLES1RenderTargetCubeRenderer::~OpenGLES1RenderTargetCubeRenderer()
    {
        if (owner_)
        {
            if (depthRenderbuffer_) owner_->DeleteRenderbufferEXT(&depthRenderbuffer_);
            if (fbo_) owner_->DeleteFramebufferEXT(&fbo_);
        }
        if (cubeTexture_) glDeleteTextures(1, &cubeTexture_);
    }

    void OpenGLES1RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        owner_->BindFramebufferEXT(fbo_);
        owner_->AttachCubeFaceEXT(CubeFaceTarget(face), cubeTexture_);
    }

    void OpenGLES1RenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        owner_->BindFramebufferEXT(0);
    }

    void OpenGLES1RenderTargetCubeRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP_OES, cubeTexture_);
    }

    bool OpenGLES1RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                   void* data, int dataLength) const
    {
        if (level != 0 || !data || w <= 0 || h <= 0) return false;
        if (dataLength < w * h * 4) return false;
        if (face < 0 || face > 5) return false;
        if (!owner_ || fbo_ == 0) return false;

        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previousFbo);
        owner_->BindFramebufferEXT(fbo_);
        owner_->AttachCubeFaceEXT(CubeFaceTarget(face), cubeTexture_);

        auto* pixels = static_cast<uint8_t*>(data);
        const int flippedY = size_ - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, flippedY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        std::vector<uint8_t> row(static_cast<std::size_t>(w) * 4);
        for (int top = 0, bottom = h - 1; top < bottom; ++top, --bottom)
        {
            uint8_t* topRow = pixels + static_cast<std::size_t>(top) * w * 4;
            uint8_t* bottomRow = pixels + static_cast<std::size_t>(bottom) * w * 4;
            std::memcpy(row.data(), topRow, row.size());
            std::memcpy(topRow, bottomRow, row.size());
            std::memcpy(bottomRow, row.data(), row.size());
        }

        owner_->BindFramebufferEXT(static_cast<GLuint>(previousFbo));
        return true;
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenGLES1Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // Cube mip generation and multisampling are not implemented (documented gap).
        //
        // preserveContents needs no action here: the colour attachment IS the cube texture, so its
        // contents survive every unbind by construction. RenderTargetUsage::DiscardContents is
        // therefore only a missed optimisation on this renderer, never a wrong result.
        (void)preserveContents; (void)mipMap; (void)multiSampleCount;
        if (!fboSupported_ || !cubeMapSupported_) return nullptr;
        return std::make_unique<OpenGLES1RenderTargetCubeRenderer>(this, size, depthFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> OpenGLES1Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        (void)preserveContents;
        // Render-target multisampling stays unimplemented: no framebuffer-multisample extension is
        // exposed by this driver (documented gap, see docs/opengles1-renderer.md). mipMap IS now
        // honoured -- see OPENGLES1-85.
        (void)multiSampleCount;
        if (!fboSupported_) return nullptr;
        return std::make_unique<OpenGLES1RenderTargetRenderer>(this, w, h, depthFormat, mipMap);
    }

    void OpenGLES1Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt) rt->BindAsRenderTarget();
        else if (currentRenderTarget_) currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = rt;
    }

    void OpenGLES1Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            if (currentRenderTargetCube_)
            {
                currentRenderTargetCube_->UnbindAsRenderTarget();
                currentRenderTargetCube_ = nullptr;
            }
            SetRenderTarget2D(nullptr);
            return;
        }

        // ES 1.1 core has no MRT mechanism -- and no extension in the CM registry adds one. Refuse
        // rather than silently binding only the first target, which would produce a frame that
        // quietly lied about where the other slots went.
        if (count > 1)
            throw System::NotSupportedException(
                "OpenGLES1: multiple simultaneous render targets are not available on OpenGL ES "
                "1.1 -- SupportsCapability(MultipleRenderTargets) reports false for this renderer.");

        if (renderTargets[0].IsRenderTargetCubeFace())
        {
            auto* cube = dynamic_cast<OpenGLES1RenderTargetCubeRenderer*>(
                renderTargets[0].GetRenderTargetCube());
            if (!cube)
                throw System::NotSupportedException(
                    "OpenGLES1: this render-target cube was not created by the OpenGLES1 renderer.");
            SetRenderTarget2D(nullptr);
            cube->BindAsRenderTargetFace(renderTargets[0].GetCubeFace());
            currentRenderTargetCube_ = cube;
            return;
        }

        if (currentRenderTargetCube_)
        {
            currentRenderTargetCube_->UnbindAsRenderTarget();
            currentRenderTargetCube_ = nullptr;
        }
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
    }

    // -------------------------------------------------------------------------
    // OpenGLES1TextureCubeRenderer (OPENGLES1-74, GL_OES_texture_cube_map)
    // -------------------------------------------------------------------------

    OpenGLES1TextureCubeRenderer::OpenGLES1TextureCubeRenderer(OpenGLES1Renderer* owner, int size)
        : owner_(owner), size_(size)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP_OES, texture_);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        static constexpr GLenum kFaces[6] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_X_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_OES,
        };
        for (GLenum face : kFaces)
            glTexImage2D(face, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    OpenGLES1TextureCubeRenderer::~OpenGLES1TextureCubeRenderer()
    {
        if (texture_) glDeleteTextures(1, &texture_);
    }

    bool OpenGLES1TextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                              const void* data, int dataLength)
    {
        (void)dataLength;
        if (face < 0 || face > 5 || !data || w <= 0 || h <= 0) return false;
        static constexpr GLenum kFaces[6] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_X_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_OES,
        };
        glBindTexture(GL_TEXTURE_CUBE_MAP_OES, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (x == 0 && y == 0 && w == size_ && h == size_)
            glTexImage2D(kFaces[face], level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        else
            glTexSubImage2D(kFaces[face], level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        return true;
    }

    bool OpenGLES1TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        // Only level 0 is stored by this renderer (CreateTextureCube ignores mipMap).
        if (level != 0 || !data || w <= 0 || h <= 0) return false;
        if (face < 0 || face > 5) return false;
        if (dataLength < w * h * 4) return false;
        if (!owner_ || !owner_->HasFramebufferObjectsEXT() || texture_ == 0) return false;

        static constexpr GLenum kFaces[6] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_X_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_OES,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z_OES, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_OES,
        };

        // ES 1.1 has no glGetTexImage, so the face is attached to a scratch framebuffer and read
        // through it -- the same route Texture2D::GetData and the render-target cube already take.
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previousFbo);

        GLuint scratch = 0;
        owner_->GenFramebufferEXT(&scratch);
        owner_->BindFramebufferEXT(scratch);
        owner_->AttachCubeFaceEXT(kFaces[face], texture_);

        bool ok = false;
        if (owner_->IsFramebufferCompleteEXT())
        {
            // Deliberately NOT flipped, unlike Texture2D::GetData. This class's own SetData hands
            // raw x/y straight to glTexSubImage2D, i.e. it works in GL's bottom-up space; reading
            // back through the same space is what makes a SetData/GetData round trip exact. A flip
            // here would only reintroduce the mismatch on every sub-rectangle.
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<uint8_t*>(data));
            ok = true;
        }

        owner_->BindFramebufferEXT(static_cast<GLuint>(previousFbo));
        owner_->DeleteFramebufferEXT(&scratch);
        return ok;
    }

    void OpenGLES1TextureCubeRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP_OES, texture_);
    }

    std::unique_ptr<ITextureCubeRenderer> OpenGLES1Renderer::CreateTextureCube(int size, bool mipMap, int surfaceFormat)
    {
        (void)mipMap; (void)surfaceFormat;
        if (!cubeMapSupported_) return nullptr;
        return std::make_unique<OpenGLES1TextureCubeRenderer>(this, size);
    }

    // -------------------------------------------------------------------------
    // 3D draw — fixed-function vertex layout dispatch
    // -------------------------------------------------------------------------

    namespace
    {
        // Byte strides established across every CNA renderer: VertexPositionColor=16,
        // VertexPositionTexture=20, VertexPositionColorTexture=24, VertexPositionNormalTexture=32.
        constexpr std::size_t kStrideColor = 16;
        constexpr std::size_t kStrideTexture = 20;
        constexpr std::size_t kStrideColorTexture = 24;
        constexpr std::size_t kStrideNormalTexture = 32;
        // Position(12) + TexCoord0(8) + TexCoord1(8): the only layout carrying two independent UV
        // sets, which DualTextureEffect's own vertex format uses.
        constexpr std::size_t kStrideDualTexture = 28;

        // `stride`-relative offsets, not raw pointers -- OPENGLES1-73's real VBO is bound to
        // GL_ARRAY_BUFFER by the caller first, so glVertexPointer/glColorPointer/glTexCoordPointer/
        // glNormalPointer all interpret their pointer argument as a byte offset into it.
        // OPENGLES1-92: XNA multiplies per-vertex Color by BasicEffect.DiffuseColor. Fixed-function
        // has a single "current colour" input, so the two cannot both feed GL_MODULATE directly --
        // but a GL_COMBINE stage can multiply GL_PRIMARY_COLOR (the vertex colour) by GL_CONSTANT
        // (the diffuse tint). The stage needs an enabled texture unit to exist on, hence the 1x1
        // white carrier texture; the texture's own value never enters the result.
        //
        // Only engaged when the combination actually occurs, so every other draw is untouched.
        bool NeedsVertexColorTimesDiffuse(const GpuDrawParams& params, bool wantColorArray)
        {
            if (!wantColorArray || params.lightingEnabled) return false;
            return params.diffuseColor[0] < 1.0f || params.diffuseColor[1] < 1.0f
                || params.diffuseColor[2] < 1.0f || params.diffuseColor[3] < 1.0f;
        }

        void SetupVertexColorTimesDiffuse(unsigned int whiteTexture, const GpuDrawParams& params,
                                          bool wantTexture)
        {
            // Untextured draws put the stage on unit 0; textured draws leave unit 0 doing
            // texture x primary and chain the tint on unit 1, which the plain-textured branch never
            // otherwise uses (dual-texture and environment-map draws are separate branches).
            const GLenum unit = wantTexture ? GL_TEXTURE1 : GL_TEXTURE0;
            const GLenum src0 = wantTexture ? GL_PREVIOUS : GL_PRIMARY_COLOR;

            glActiveTexture(unit);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, whiteTexture);

            const float tint[4] = {params.diffuseColor[0], params.diffuseColor[1],
                                   params.diffuseColor[2], params.diffuseColor[3]};
            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, tint);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, src0);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, src0);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

            glActiveTexture(GL_TEXTURE0);
        }

        /// plans/plan_gltf.md GLTF-473. The twin of SetupClientArraysForStride below, and it deliberately
        /// takes the SAME arguments: this function states, and the shared guard checks, exactly the
        /// offsets that one is about to program, so the two cannot drift into disagreeing.
        ///
        /// The offsets themselves are not re-derived here -- they are copied from the pointer setup
        /// verbatim, because what is being audited is what that code does, not what it ought to do.
        ///
        /// @param stride The record stride the route strides the buffer by.
        /// @param wantColor Whether a colour array is about to be enabled.
        /// @param wantTexture Whether a texture-coordinate array is about to be enabled.
        /// @param wantNormal Whether a normal array is about to be enabled.
        /// @param wantDualTexture Whether a second texture unit's array is about to be enabled.
        /// @param route Name of the draw route, for the diagnostic.
        /// @param unsupportedSemantic The effect that sent the draw here, or null for a direct call.
        /// @throws System::NotSupportedException When an array would read the wrong bytes.
        void RequireClientArraysMatchStrideEXT(std::size_t stride, bool wantColor, bool wantTexture,
                                               bool wantNormal, bool wantDualTexture,
                                               const char* route, const char* unsupportedSemantic)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            const auto require = [&](VertexElementUsage usage, int usageIndex, int offset,
                                     const char* arrayName) {
                CNA::Internal::Renderers::RequireFixedFunctionClientArrayEXT(
                    static_cast<int>(stride), usage, usageIndex, offset, arrayName, "OPENGLES1",
                    route, unsupportedSemantic);
            };

            // Position is the one array every route binds, and it is at offset 0 in every record
            // CNA has -- stated rather than assumed, so a record that ever moves it is caught here
            // instead of drawing from the wrong vertex.
            require(VertexElementUsage::Position, 0, 0, "glVertexPointer");

            if (wantColor)
            {
                require(VertexElementUsage::Color, 0, 12, "glColorPointer");
            }
            if (wantTexture)
            {
                const bool dualUv = (stride == kStrideDualTexture);
                require(VertexElementUsage::TextureCoordinate, 0,
                        dualUv ? 12 : static_cast<int>(stride) - 8, "glTexCoordPointer");
                if (wantDualTexture)
                {
                    // Unit 1 shares unit 0's set on every layout except the dual-UV one, which is
                    // this renderer's own record and therefore not in the canonical table at all --
                    // the guard abstains for it, and the usage index stays 0 for the shared case.
                    require(VertexElementUsage::TextureCoordinate, dualUv ? 1 : 0,
                            dualUv ? 20 : static_cast<int>(stride) - 8, "glTexCoordPointer(unit 1)");
                }
            }
            if (wantNormal)
            {
                require(VertexElementUsage::Normal, 0, 12, "glNormalPointer");
            }
        }

        void SetupClientArraysForStride(std::size_t stride, bool wantColor, bool wantTexture,
                                        bool wantNormal, bool wantDualTexture = false)
        {
            glVertexPointer(3, GL_FLOAT, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(std::size_t{0}));

            if (wantColor)
            {
                glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(4, GL_UNSIGNED_BYTE, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(std::size_t{12}));
            }
            else
            {
                glDisableClientState(GL_COLOR_ARRAY);
            }

            if (wantTexture)
            {
                // The dual-UV layout carries TexCoord0 right after the position and TexCoord1 after
                // that; every other layout has a single UV set in its last 8 bytes, which both
                // texture units then share.
                const bool dualUv = (stride == kStrideDualTexture);
                const std::size_t uv0Offset = dualUv ? 12 : stride - 8;
                const std::size_t uv1Offset = dualUv ? 20 : stride - 8;

                glClientActiveTexture(GL_TEXTURE0);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(uv0Offset));

                if (wantDualTexture)
                {
                    glClientActiveTexture(GL_TEXTURE1);
                    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                    glTexCoordPointer(2, GL_FLOAT, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(uv1Offset));
                }

                // Leave unit 0 selected so later client-array calls have a predictable target.
                glClientActiveTexture(GL_TEXTURE0);
            }
            else
            {
                glClientActiveTexture(GL_TEXTURE0);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }

            if (wantNormal)
            {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(std::size_t{12}));
            }
            else
            {
                glDisableClientState(GL_NORMAL_ARRAY);
            }
        }

        // OPENGLES1-71 (DualTextureEffect): real ES 1.1 multitexturing. Both units sample the SAME
        // UV set (matches FNA's own DualTextureEffect vertex format -- one texture-coordinate
        // pair, not two) -- reproduces dual_texture3d.frag.glsl's exact formula
        // `(tex0.rgb*2, tex0.a) * tex1 * diffuseTint` via two GL_COMBINE stages: unit 0 computes
        // `tex0 * primaryColor` scaled 2x (RGB only), unit 1 modulates that by its own texture
        // sample.
        void SetupDualTexture(OpenGLES1Renderer& renderer, const GpuDrawParams& params, std::size_t stride)
        {
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);
            params.texture0->BindGL();
            renderer.ApplySamplerToBoundTextureEXT(0, GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
            glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);

            glActiveTexture(GL_TEXTURE1);
            glEnable(GL_TEXTURE_2D);
            params.texture1->BindGL();
            renderer.ApplySamplerToBoundTextureEXT(1, GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
            glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);

            // Client-side array pointers are deliberately NOT set here: glTexCoordPointer captures
            // whatever GL_ARRAY_BUFFER is bound when it is called, and the vertex buffer is not
            // bound until after this function runs. SetupClientArraysForStride() does it instead.
            glClientActiveTexture(GL_TEXTURE0);
        }

        // OPENGLES1-74 (EnvironmentMapEffect): real reflection-vector cube-map sampling via
        // GL_OES_texture_cube_map's GL_REFLECTION_MAP_OES automatic texture-coordinate generation
        // (the classic fixed-function environment-mapping technique -- the GPU recomputes the
        // reflection vector from the per-vertex normal and the current MODELVIEW at draw time, no
        // vertex-side UV data needed at all). Blended with unit 0's already-lit base color via a
        // GL_INTERPOLATE combine stage, factor = envMapAmount (a GL_CONSTANT texture-environment
        // colour's alpha channel). Fresnel edge-weighting (`fresnelEnabled`/`fresnelFactor`) has no
        // fixed-function equivalent (would need a genuinely per-vertex-varying blend factor, not a
        // single constant) and is intentionally not applied -- documented deviation, see
        // docs/opengles1-renderer.md.
        void SetupEnvironmentMap(OpenGLES1Renderer& renderer, const GpuDrawParams& params)
        {
            glActiveTexture(GL_TEXTURE1);
            glEnable(GL_TEXTURE_CUBE_MAP_OES);
            params.envMap->BindGL();
            renderer.ApplySamplerToBoundTextureEXT(1, GL_TEXTURE_CUBE_MAP_OES);
            renderer.glTexGeniOES_(GL_TEXTURE_GEN_STR_OES, GL_TEXTURE_GEN_MODE_OES, GL_REFLECTION_MAP_OES);
            glEnable(GL_TEXTURE_GEN_STR_OES);

            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
            const float envColor[4] = {0.0f, 0.0f, 0.0f, std::clamp(params.envMapAmount, 0.0f, 1.0f)};
            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envColor);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

            glClientActiveTexture(GL_TEXTURE1);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTexture(GL_TEXTURE0);
            glActiveTexture(GL_TEXTURE0);
        }

        // Resets texture unit 1 to inactive -- called at the top of the plain single-texture path
        // so leftover multitexture state from a previous DualTextureEffect/EnvironmentMapEffect
        // draw doesn't leak into this one.
        void DisableSecondTextureUnit()
        {
            glActiveTexture(GL_TEXTURE1);
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_TEXTURE_CUBE_MAP_OES);
            glDisable(GL_TEXTURE_GEN_STR_OES);
            glClientActiveTexture(GL_TEXTURE1);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTexture(GL_TEXTURE0);
            glActiveTexture(GL_TEXTURE0);
        }

        // OPENGLES1-76: FillMode::WireFrame has no glPolygonMode equivalent in ES 1.1 -- re-expands
        // each triangle into a GL_LINES edge list, mirroring
        // EasyGLRenderer::DrawWireframe's identical technique. `indices` is null for a
        // non-indexed draw (vertex sequence read directly 0..N-1).
        std::vector<uint32_t> BuildWireframeLineIndices(PrimitiveType primitive, int primitiveCount,
                                                        const uint32_t* indices)
        {
            std::vector<uint32_t> lines;
            auto readSrc = [&](int pos) -> uint32_t
            {
                return indices ? indices[pos] : static_cast<uint32_t>(pos);
            };
            auto edge = [&](uint32_t a, uint32_t b) { lines.push_back(a); lines.push_back(b); };
            if (primitive == PrimitiveType::TriangleList)
            {
                for (int t = 0; t < primitiveCount; ++t)
                {
                    const uint32_t a = readSrc(3 * t), b = readSrc(3 * t + 1), c = readSrc(3 * t + 2);
                    edge(a, b); edge(b, c); edge(c, a);
                }
            }
            else if (primitive == PrimitiveType::TriangleStrip)
            {
                for (int t = 0; t < primitiveCount; ++t)
                {
                    const uint32_t a = readSrc(t), b = readSrc(t + 1), c = readSrc(t + 2);
                    edge(a, b); edge(b, c); edge(c, a);
                }
            }
            return lines;
        }

        // Issues the actual GL_LINES draw for a wireframe expansion, from CPU-side memory --
        // GL_ELEMENT_ARRAY_BUFFER must be unbound first so glDrawElements' pointer argument is
        // interpreted as a real client pointer, not an offset into whatever IBO the real
        // (non-wireframe) draw had bound.
        void DrawWireframeLines(const std::vector<uint32_t>& lines, bool elementIndexUintSupported)
        {
            if (lines.empty()) return;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            // The expansion is built in 32 bits so one code path serves both index sizes, but
            // GL_UNSIGNED_INT itself needs GL_OES_element_index_uint -- narrow the copy when the
            // driver lacks it. Indices above 65535 cannot occur there anyway, since a 32-bit index
            // buffer is never handed out without the extension (see CreateIndexBuffer32).
            if (elementIndexUintSupported)
            {
                glDrawElements(GL_LINES, static_cast<GLsizei>(lines.size()), GL_UNSIGNED_INT, lines.data());
                return;
            }

            std::vector<uint16_t> narrow(lines.begin(), lines.end());
            glDrawElements(GL_LINES, static_cast<GLsizei>(narrow.size()), GL_UNSIGNED_SHORT, narrow.data());
        }

        // Sets up GL_LIGHTn (n=0..2) from a BasicEffect-shaped GpuDrawParams, called with
        // GL_MODELVIEW == view only (not world*view) so world-space light directions/positions
        // are transformed exclusively by the camera, matching the classic fixed-function idiom for
        // world-space lights (glLightfv bakes in whatever GL_MODELVIEW is current at call time).
        void ApplyLighting(const GpuDrawParams& params)
        {
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, params.ambientColor);

            auto applyLight = [](GLenum light, const float* dir, const float* diffuse, const float* specular)
            {
                const bool enabled = diffuse[0] != 0.0f || diffuse[1] != 0.0f || diffuse[2] != 0.0f
                                    || specular[0] != 0.0f || specular[1] != 0.0f || specular[2] != 0.0f;
                if (!enabled) { glDisable(light); return; }
                glEnable(light);
                // w=0 -> directional light; XNA light directions point FROM the surface, GL
                // expects the direction TO the light, hence the negation.
                const float position[4] = {-dir[0], -dir[1], -dir[2], 0.0f};
                glLightfv(light, GL_POSITION, position);
                const float diffuse4[4] = {diffuse[0], diffuse[1], diffuse[2], 1.0f};
                glLightfv(light, GL_DIFFUSE, diffuse4);
                const float specular4[4] = {specular[0], specular[1], specular[2], 1.0f};
                glLightfv(light, GL_SPECULAR, specular4);
                const float ambient4[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                glLightfv(light, GL_AMBIENT, ambient4);
            };

            applyLight(GL_LIGHT0, params.light0Dir, params.light0Diffuse, params.light0Specular);
            applyLight(GL_LIGHT1, params.light1Dir, params.light1Diffuse, params.light1Specular);
            applyLight(GL_LIGHT2, params.light2Dir, params.light2Diffuse, params.light2Specular);

            const float diffuseMat[4] = {params.diffuseColor[0], params.diffuseColor[1],
                                         params.diffuseColor[2], params.diffuseColor[3]};
            glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuseMat);
            const float specularMat[4] = {params.specularColor[0], params.specularColor[1],
                                          params.specularColor[2], 1.0f};
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specularMat);
            const float emissiveMat[4] = {params.emissiveColor[0], params.emissiveColor[1],
                                          params.emissiveColor[2], 1.0f};
            glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emissiveMat);
            const float shininess = std::clamp(params.specularPower, 0.0f, 128.0f);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
        }

        // AlphaTestEffect's real GpuDrawParams::alphaTest is a 4-way tolerance-band test that has
        // no single-comparison fixed-function equivalent (glAlphaFunc takes exactly one GLenum
        // compare function). Approximates the common cases (a plain >= / <= / == cutoff) and
        // documents the rest as an intentional deviation (see docs/opengles1-renderer.md).
        // FNA/XNA computes fog as `saturate((viewZ + fogStart) / (fogStart - fogEnd))`, which maps
        // onto GL's own linear ramp for ordinary ranges -- but it also defines a degenerate case
        // GL cannot express: when fogStart == fogEnd, everything is 100% fogged, where GL would
        // divide by zero. Emulated with a ramp that saturates immediately.
        //
        // Deliberately NOT emulated: inverted ranges (fogEnd < fogStart). FNA's signed-viewZ form
        // still produces a gradient there, while fixed-function fog works from a distance and
        // clamps. See docs/opengles1-renderer.md -- EasyGL diverges from the reference on that case
        // too, so it is not an ES1-specific fault.
        //
        // REMED-GFX-010 replaced GpuDrawParams' scalar fogStart/fogEnd with the FNA fog VECTOR,
        // which every shader renderer dots against the object-space position. A fixed-function
        // pipeline has no such dot product -- glFog wants the two scalars back. They are recovered
        // exactly, because the vector is built from them and from the same world*view matrix this
        // draw is about to load:
        //
        //     fogVector.xyz = {M13,M23,M33} * scale,  fogVector.w = (M43 + fogStart) * scale
        //     scale         = 1 / (fogStart - fogEnd)
        //
        // so projecting fogVector.xyz back onto the matrix's own eye-Z row recovers `scale`, and
        // the w term then yields fogStart and fogEnd. This is an inversion, not an approximation.
        //
        // @param params The draw's stock-effect parameters.
        // @param mvCol The world*view matrix in GL column-major order, as just loaded into
        //        GL_MODELVIEW -- its eye-Z row is what the fog vector was built against.
        void ApplyFog(const GpuDrawParams& params, const float* mvCol)
        {
            const bool fogVectorIsZero =
                params.fogVector[0] == 0.0f && params.fogVector[1] == 0.0f &&
                params.fogVector[2] == 0.0f && params.fogVector[3] == 0.0f;

            // An all-zero vector is FNA's own "fog disabled" encoding: the dot product is 0, so the
            // keep factor is 1 and fog is a true no-op. Honour it even if the flag disagrees.
            if (!params.fogEnabled || fogVectorIsZero)
            {
                glDisable(GL_FOG);
                return;
            }

            glEnable(GL_FOG);
            glFogf(GL_FOG_MODE, GL_LINEAR);

            // Eye-space Z row of world*view: z_view = zx*x + zy*y + zz*z + zw.
            const float zx = mvCol[2], zy = mvCol[6], zz = mvCol[10], zw = mvCol[14];
            const float denom = zx * zx + zy * zy + zz * zz;
            const float numer = params.fogVector[0] * zx + params.fogVector[1] * zy +
                                params.fogVector[2] * zz;
            const float scale = denom > 0.0f ? numer / denom : 0.0f;

            if (scale == 0.0f)
            {
                // fogStart == fogEnd. FNA encodes it as {0,0,0,1}: the dot is 1 everywhere, so the
                // keep factor is 0 and the whole draw is fogged. A world*view that collapses the
                // eye-Z axis entirely lands here too, and is fogged for the same reason -- there is
                // no depth left to build a ramp from.
                //
                // GL's visibility factor is (end - z) / (end - start), clamped to [0,1], with z the
                // eye distance (never negative). start = -1, end = 0 gives -z, which is <= 0 for
                // every z including z == 0 -- so it clamps to "no visibility", i.e. fully fogged
                // everywhere. A near-zero-width ramp would NOT do: geometry sitting exactly at the
                // eye would still come out unfogged.
                glFogf(GL_FOG_START, -1.0f);
                glFogf(GL_FOG_END, 0.0f);
            }
            else
            {
                const float fogStart = params.fogVector[3] / scale - zw;
                const float fogEnd = fogStart - 1.0f / scale;
                glFogf(GL_FOG_START, fogStart);
                glFogf(GL_FOG_END, fogEnd);
            }

            const float fogColor4[4] = {params.fogColor[0], params.fogColor[1], params.fogColor[2], 1.0f};
            glFogfv(GL_FOG_COLOR, fogColor4);
        }

        void ApplyAlphaTest(const GpuDrawParams& params)
        {
            // The vector's shape comes from AlphaTestEffect and is evaluated by the shader-based
            // renderers as:
            //     at = (y > 0) ? (|a - x| < y ? z : w)
            //                  : (a < x       ? z : w)
            //     discard when at < 0
            // So z is the branch taken when the comparison holds and w the branch taken when it
            // does not -- they are *branch outcomes*, not "pass"/"fail" weights.
            const float ref = params.alphaTest[0];
            const float tolerance = params.alphaTest[1];
            const float whenLess = params.alphaTest[2];
            const float whenGreaterEqual = params.alphaTest[3];

            // No AlphaTestEffect in play leaves the whole vector zeroed.
            if (whenLess == 0.0f && whenGreaterEqual == 0.0f)
            {
                glDisable(GL_ALPHA_TEST);
                return;
            }

            const bool keepLess = whenLess >= 0.0f;
            const bool keepGreaterEqual = whenGreaterEqual >= 0.0f;

            // CompareFunction::Always -- both branches survive, so the test is a no-op.
            if (keepLess && keepGreaterEqual)
            {
                glDisable(GL_ALPHA_TEST);
                return;
            }

            glEnable(GL_ALPHA_TEST);

            // CompareFunction::Never -- neither branch survives.
            if (!keepLess && !keepGreaterEqual)
            {
                glAlphaFunc(GL_NEVER, 0.0f);
                return;
            }

            // Documented deviation: GL_EQUAL/GL_NOTEQUAL compare exactly and cannot express the
            // effect's tolerance band (see docs/opengles1-renderer.md).
            if (tolerance > 0.0f)
                glAlphaFunc(keepLess ? GL_EQUAL : GL_NOTEQUAL, ref);
            else
                glAlphaFunc(keepLess ? GL_LESS : GL_GEQUAL, ref);
        }
    }

    void OpenGLES1Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount)
    {
        // REMED-GFX-DECL-GUARD: the fixed-function pointer setup below selects its layout from
        // the buffer stride alone, so a declaration that stride cannot represent is refused
        // rather than rendered from the wrong bytes.
        RequireFaithfulDeclarationEXT(vb_in, "colored-nonindexed");
        // plans/plan_gltf.md GLTF-473: this route binds a colour at offset 12, which is where a
        // colour lives in exactly two of CNA's records (stride 16 and stride 24). Every other
        // stride keeps something else there -- the NORMAL, in every PBR and skinned one -- so a
        // buffer bound here that is not one of those two is refused rather than read from the
        // wrong bytes. `RequireFaithfulDeclarationEXT` above cannot catch it: a stride-60 PBR
        // record IS faithfully declared, it is simply not a colour record.
        RequireClientArraysMatchStrideEXT(
            static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in).Stride(), /*color*/true,
            /*texture*/false, /*normal*/false, /*dualTexture*/false, "colored-nonindexed",
            // A direct call names no effect: the caller bound this buffer to this route itself.
            /*unsupportedSemantic*/nullptr);
        const auto& vb = static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in);

        float projCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvCol);

        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        DisableSecondTextureUnit();

        vb.Bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Stride(), /*color*/true, /*texture*/false, /*normal*/false);

        if (wireframe_ && (primitive == PrimitiveType::TriangleList || primitive == PrimitiveType::TriangleStrip))
            DrawWireframeLines(BuildWireframeLineIndices(primitive, primitiveCount, nullptr),
                               elementIndexUintSupported_);
        else
            glDrawArrays(ToGLPrimitive(primitive), 0, VertexCountForPrimitives(primitive, primitiveCount));
    }

    void OpenGLES1Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
                                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                                PrimitiveType primitive, int primitiveCount)
    {
        // REMED-GFX-DECL-GUARD: the fixed-function pointer setup below selects its layout from
        // the buffer stride alone, so a declaration that stride cannot represent is refused
        // rather than rendered from the wrong bytes.
        RequireFaithfulDeclarationEXT(vb_in, "colored-indexed");
        // plans/plan_gltf.md GLTF-473: this route binds a colour at offset 12, which is where a
        // colour lives in exactly two of CNA's records (stride 16 and stride 24). Every other
        // stride keeps something else there -- the NORMAL, in every PBR and skinned one -- so a
        // buffer bound here that is not one of those two is refused rather than read from the
        // wrong bytes. `RequireFaithfulDeclarationEXT` above cannot catch it: a stride-60 PBR
        // record IS faithfully declared, it is simply not a colour record.
        RequireClientArraysMatchStrideEXT(
            static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in).Stride(), /*color*/true,
            /*texture*/false, /*normal*/false, /*dualTexture*/false, "colored-indexed",
            // A direct call names no effect: the caller bound this buffer to this route itself.
            /*unsupportedSemantic*/nullptr);
        const auto& vb = static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const OpenGLES1IndexBufferRenderer&>(ib_in);

        float projCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvCol);

        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        DisableSecondTextureUnit();

        vb.Bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Stride(), /*color*/true, /*texture*/false, /*normal*/false);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (wireframe_ && (primitive == PrimitiveType::TriangleList || primitive == PrimitiveType::TriangleStrip))
        {
            DrawWireframeLines(BuildWireframeLineIndices(primitive, primitiveCount, ib.CpuShadow().data()),
                               elementIndexUintSupported_);
        }
        else
        {
            ib.Bind();
            glDrawElements(ToGLPrimitive(primitive), indexCount,
                           ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(std::size_t{0}));
        }
    }

    void OpenGLES1Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        // REMED-GFX-DECL-GUARD: the fixed-function pointer setup below selects its layout from
        // the buffer stride alone, so a declaration that stride cannot represent is refused
        // rather than rendered from the wrong bytes.
        RequireFaithfulDeclarationEXT(vb_in, "ordinary-nonindexed");
        const auto& vb = static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in);
        const std::size_t stride = vb.Stride();

        // OPENGLES1-71/74: DualTextureEffect (real ES 1.1 multitexturing) and EnvironmentMapEffect
        // (real GL_REFLECTION_MAP_OES cube sampling) ARE implemented -- but only when the vertex
        // stride/extension/texture preconditions they need are actually met; otherwise this falls
        // through to the same "no fixed-function equivalent" colored-path fallback as skinning/
        // PBR/custom-shader/instancing (see docs/opengles1-renderer.md's "Important limitations").
        const bool wantDualTexture = params.dualTexture && params.texture0 && params.texture1
                                    && SupportsSecondTextureUnit()
                                    && (stride == kStrideTexture || stride == kStrideColorTexture
                                        || stride == kStrideDualTexture);
        const bool wantEnvMap = params.envMapping && params.envMap && cubeMapSupported_
                               && stride == kStrideNormalTexture;

        if (params.skinned || params.pbr || params.customEffectRenderer || params.instanceCount > 1
            || (params.dualTexture && !wantDualTexture) || (params.envMapping && !wantEnvMap))
        {
            // plans/plan_gltf.md GLTF-473. Naming WHY a draw is about to leave the programmable-effect world
            // matters as much as refusing it: "unsupported vertex stride" sends a reader looking at the
            // buffer, when the actual missing piece is the effect. PBR is tested first for the same
            // reason EasyGL's own SelectStockProgram tests it first -- SkinnedPbrEffect sets both flags,
            // and it is the PBR half that has no fixed-function equivalent.
            const char* const unsupportedSemantic =
                  params.pbr                  ? "PbrEffect/SkinnedPbrEffect (ES 1.1 has no programmable "
                                                "pipeline, so metallic-roughness shading has no "
                                                "fixed-function equivalent)"
                : params.skinned              ? "SkinnedEffect (ES 1.1 has no vertex skinning without the "
                                                "rare GL_OES_matrix_palette extension)"
                : params.customEffectRenderer ? "a custom ShaderEffect (ES 1.1 has no shader compiler)"
                : params.instanceCount > 1    ? "hardware instancing (ES 1.1 has no instancing mechanism)"
                : params.dualTexture          ? "DualTextureEffect on this vertex layout or without a "
                                                "second texture unit"
                : params.envMapping           ? "EnvironmentMapEffect on this vertex layout or without "
                                                "GL_OES_texture_cube_map"
                :                               "an effect combination this renderer has no "
                                                "fixed-function equivalent for";
            // The colour route binds a colour at offset 12. Refuse HERE, where the effect that
            // forced the fallback is still known, rather than one call later where it is not.
            RequireClientArraysMatchStrideEXT(stride, /*color*/true, /*texture*/false,
                                              /*normal*/false, /*dualTexture*/false,
                                              "ordinary-nonindexed fallback", unsupportedSemantic);
            DrawColoredPrimitives(vb_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        // plans/plan_gltf.md GLTF-473: the ordinary path's own offsets, checked against the same canonical
        // table. These three are pure predicates and the guard touches nothing, so both sit ABOVE
        // the first glMatrixMode below rather than beside the pointer setup they describe: a
        // refusal must leave the context exactly as it found it, or a caller that catches it draws
        // its next frame through a projection matrix this draw already overwrote.
        //
        // The colour and normal arms are already stride-gated above, but the texture arm is not --
        // it derives its offset as `stride - 8`, which is where UV0 happens to sit in the records
        // this route was written for and is NOT where it sits in the rigid PBR dual-UV record
        // (stride 60 keeps UV0 at 40, not 52).
        const bool wantTexture = params.textureEnabled && params.texture0 != nullptr && stride != kStrideColor;
        const bool wantNormal = (params.lightingEnabled || wantEnvMap) && stride == kStrideNormalTexture;
        const bool wantColorArray = params.vertexColorEnabled && (stride == kStrideColor || stride == kStrideColorTexture);
        RequireClientArraysMatchStrideEXT(stride, wantColorArray,
                                          wantTexture || wantDualTexture || wantEnvMap, wantNormal,
                                          wantDualTexture, "ordinary-nonindexed",
                                          /*unsupportedSemantic*/nullptr);

        float projCol[16], viewCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        view.ToColumnMajor(viewCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);

        if (params.lightingEnabled)
        {
            // Lights are defined in world space; apply them under a VIEW-only modelview so GL's
            // implicit "transform by current MODELVIEW at call time" behavior puts them correctly
            // into eye space (see ApplyLighting's own comment).
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(viewCol);
            glEnable(GL_LIGHTING);
            ApplyLighting(params);
        }
        else
        {
            glDisable(GL_LIGHTING);
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvCol);

        ApplyFog(params, mvCol);

        ApplyAlphaTest(params);

        if (wantDualTexture)
        {
            SetupDualTexture(*this, params, stride);
        }
        else
        {
            DisableSecondTextureUnit();
            if (wantTexture)
            {
                glEnable(GL_TEXTURE_2D);
                params.texture0->BindGL();
                ApplySamplerToBoundTextureEXT(0, GL_TEXTURE_2D);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            }
            else
            {
                glDisable(GL_TEXTURE_2D);
            }
            if (wantEnvMap) SetupEnvironmentMap(*this, params);
        }

        if (!wantColorArray && !params.lightingEnabled)
        {
            // No per-vertex color and no lighting to compute one -- use the flat DiffuseColor as
            // the constant "current color" GL_MODULATE multiplies the texture sample by.
            glColor4f(params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2], params.diffuseColor[3]);
        }
        else if (!wantDualTexture && !wantEnvMap
                 && NeedsVertexColorTimesDiffuse(params, wantColorArray))
        {
            // OPENGLES1-92: fold DiffuseColor in on top of the per-vertex colour.
            SetupVertexColorTimesDiffuse(whiteTexture_, params, wantTexture);
        }

        vb.Bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(stride, wantColorArray, wantTexture || wantDualTexture || wantEnvMap,
                                   wantNormal, wantDualTexture);

        if (wireframe_ && (primitive == PrimitiveType::TriangleList || primitive == PrimitiveType::TriangleStrip))
            DrawWireframeLines(BuildWireframeLineIndices(primitive, primitiveCount, nullptr),
                               elementIndexUintSupported_);
        else
            glDrawArrays(ToGLPrimitive(primitive), 0, VertexCountForPrimitives(primitive, primitiveCount));

        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
    }

    void OpenGLES1Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
                                                           const Matrix& world, const Matrix& view, const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount,
                                                           const GpuDrawParams& params)
    {
        // REMED-GFX-DECL-GUARD: the fixed-function pointer setup below selects its layout from
        // the buffer stride alone, so a declaration that stride cannot represent is refused
        // rather than rendered from the wrong bytes.
        RequireFaithfulDeclarationEXT(vb_in, "ordinary-indexed");
        const auto& vb = static_cast<const OpenGLES1VertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const OpenGLES1IndexBufferRenderer&>(ib_in);
        const std::size_t stride = vb.Stride();

        const bool wantDualTexture = params.dualTexture && params.texture0 && params.texture1
                                    && SupportsSecondTextureUnit()
                                    && (stride == kStrideTexture || stride == kStrideColorTexture
                                        || stride == kStrideDualTexture);
        const bool wantEnvMap = params.envMapping && params.envMap && cubeMapSupported_
                               && stride == kStrideNormalTexture;

        if (params.skinned || params.pbr || params.customEffectRenderer || params.instanceCount > 1
            || (params.dualTexture && !wantDualTexture) || (params.envMapping && !wantEnvMap))
        {
            // plans/plan_gltf.md GLTF-473. Naming WHY a draw is about to leave the programmable-effect world
            // matters as much as refusing it: "unsupported vertex stride" sends a reader looking at the
            // buffer, when the actual missing piece is the effect. PBR is tested first for the same
            // reason EasyGL's own SelectStockProgram tests it first -- SkinnedPbrEffect sets both flags,
            // and it is the PBR half that has no fixed-function equivalent.
            const char* const unsupportedSemantic =
                  params.pbr                  ? "PbrEffect/SkinnedPbrEffect (ES 1.1 has no programmable "
                                                "pipeline, so metallic-roughness shading has no "
                                                "fixed-function equivalent)"
                : params.skinned              ? "SkinnedEffect (ES 1.1 has no vertex skinning without the "
                                                "rare GL_OES_matrix_palette extension)"
                : params.customEffectRenderer ? "a custom ShaderEffect (ES 1.1 has no shader compiler)"
                : params.instanceCount > 1    ? "hardware instancing (ES 1.1 has no instancing mechanism)"
                : params.dualTexture          ? "DualTextureEffect on this vertex layout or without a "
                                                "second texture unit"
                : params.envMapping           ? "EnvironmentMapEffect on this vertex layout or without "
                                                "GL_OES_texture_cube_map"
                :                               "an effect combination this renderer has no "
                                                "fixed-function equivalent for";
            // The colour route binds a colour at offset 12. Refuse HERE, where the effect that
            // forced the fallback is still known, rather than one call later where it is not.
            RequireClientArraysMatchStrideEXT(stride, /*color*/true, /*texture*/false,
                                              /*normal*/false, /*dualTexture*/false,
                                              "ordinary-indexed fallback", unsupportedSemantic);
            DrawIndexedColoredPrimitives(vb_in, ib_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        // plans/plan_gltf.md GLTF-473: the ordinary path's own offsets, checked against the same canonical
        // table. These three are pure predicates and the guard touches nothing, so both sit ABOVE
        // the first glMatrixMode below rather than beside the pointer setup they describe: a
        // refusal must leave the context exactly as it found it, or a caller that catches it draws
        // its next frame through a projection matrix this draw already overwrote.
        //
        // The colour and normal arms are already stride-gated above, but the texture arm is not --
        // it derives its offset as `stride - 8`, which is where UV0 happens to sit in the records
        // this route was written for and is NOT where it sits in the rigid PBR dual-UV record
        // (stride 60 keeps UV0 at 40, not 52).
        const bool wantTexture = params.textureEnabled && params.texture0 != nullptr && stride != kStrideColor;
        const bool wantNormal = (params.lightingEnabled || wantEnvMap) && stride == kStrideNormalTexture;
        const bool wantColorArray = params.vertexColorEnabled && (stride == kStrideColor || stride == kStrideColorTexture);
        RequireClientArraysMatchStrideEXT(stride, wantColorArray,
                                          wantTexture || wantDualTexture || wantEnvMap, wantNormal,
                                          wantDualTexture, "ordinary-indexed",
                                          /*unsupportedSemantic*/nullptr);

        float projCol[16], viewCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        view.ToColumnMajor(viewCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);

        if (params.lightingEnabled)
        {
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(viewCol);
            glEnable(GL_LIGHTING);
            ApplyLighting(params);
        }
        else
        {
            glDisable(GL_LIGHTING);
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvCol);

        ApplyFog(params, mvCol);

        ApplyAlphaTest(params);

        if (wantDualTexture)
        {
            SetupDualTexture(*this, params, stride);
        }
        else
        {
            DisableSecondTextureUnit();
            if (wantTexture)
            {
                glEnable(GL_TEXTURE_2D);
                params.texture0->BindGL();
                ApplySamplerToBoundTextureEXT(0, GL_TEXTURE_2D);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            }
            else
            {
                glDisable(GL_TEXTURE_2D);
            }
            if (wantEnvMap) SetupEnvironmentMap(*this, params);
        }

        if (!wantColorArray && !params.lightingEnabled)
        {
            glColor4f(params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2], params.diffuseColor[3]);
        }
        else if (!wantDualTexture && !wantEnvMap
                 && NeedsVertexColorTimesDiffuse(params, wantColorArray))
        {
            // OPENGLES1-92: fold DiffuseColor in on top of the per-vertex colour.
            SetupVertexColorTimesDiffuse(whiteTexture_, params, wantTexture);
        }

        vb.Bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(stride, wantColorArray, wantTexture || wantDualTexture || wantEnvMap,
                                   wantNormal, wantDualTexture);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (wireframe_ && (primitive == PrimitiveType::TriangleList || primitive == PrimitiveType::TriangleStrip))
        {
            DrawWireframeLines(BuildWireframeLineIndices(primitive, primitiveCount, ib.CpuShadow().data()),
                               elementIndexUintSupported_);
        }
        else
        {
            ib.Bind();
            glDrawElements(ToGLPrimitive(primitive), indexCount,
                           ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(std::size_t{0}));
        }

        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
    }

    // -------------------------------------------------------------------------
    // Graphics state
    // -------------------------------------------------------------------------

    void OpenGLES1Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                   int colorDstBlend, int alphaDstBlend,
                                                   int colorBlendFunc, int alphaBlendFunc,
                                                   const BlendWriteState& writeState)
    {
        // REMED-GFX-077 slot 0. glColorMask is core ES 1.1, so the write channels are honoured for
        // real. Applied before the Opaque early-out below, because ColorWriteChannels is
        // independent of whether blending itself is on.
        //
        // Slots 1..3 have no subject on this renderer: ES 1.1 has no MRT mechanism, and
        // SetRenderTargets refuses a count above 1, so no second slot can ever be bound.
        // MultiSampleMask likewise has no ES 1.1 equivalent (no glSampleMaski in the CM registry);
        // it is not applied, and SupportsCapability reports MSAA from the real sample count.
        const int channels = writeState.colorWriteChannels[0];
        glColorMask(static_cast<GLboolean>((channels & 1) != 0),
                    static_cast<GLboolean>((channels & 2) != 0),
                    static_cast<GLboolean>((channels & 4) != 0),
                    static_cast<GLboolean>((channels & 8) != 0));

        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero -> effectively no blending.
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1
                                    && alphaSrcBlend == 0 && alphaDstBlend == 1);
        if (!blendEnabled) { glDisable(GL_BLEND); return; }
        glEnable(GL_BLEND);

        if (glBlendFuncSeparateOES_)
        {
            glBlendFuncSeparateOES_(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend),
                                    ToGLBlendFactor(alphaSrcBlend), ToGLBlendFactor(alphaDstBlend));
        }
        else
        {
            // GL_OES_blend_func_separate unavailable -- fall back to a single non-separate
            // glBlendFunc from the color channel's factors (documented deviation).
            glBlendFunc(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend));
        }

        if (glBlendEquationOES_ || glBlendEquationSeparateOES_)
        {
            // GL_OES_blend_subtract covers Add/Subtract/ReverseSubtract; Max/Min (BlendFunction
            // ordinals 3/4) need GL_EXT_blend_minmax and fall back to Add only where that is
            // absent -- this driver has it, so they are honoured.
            const bool minMax = blendMinMaxSupported_;
            const auto toEquation = [minMax](int xnaBlendFunc) -> GLenum
            {
                switch (xnaBlendFunc)
                {
                case 1: return GL_FUNC_SUBTRACT_OES;
                case 2: return GL_FUNC_REVERSE_SUBTRACT_OES;
                case 3: return minMax ? static_cast<GLenum>(GL_MAX_EXT) : static_cast<GLenum>(GL_FUNC_ADD_OES);
                case 4: return minMax ? static_cast<GLenum>(GL_MIN_EXT) : static_cast<GLenum>(GL_FUNC_ADD_OES);
                default: return GL_FUNC_ADD_OES;
                }
            };

            if (glBlendEquationSeparateOES_)
                glBlendEquationSeparateOES_(toEquation(colorBlendFunc), toEquation(alphaBlendFunc));
            else
                glBlendEquationOES_(toEquation(colorBlendFunc));
        }
    }

    void OpenGLES1Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                          int depthFunc,
                                                          bool stencilEnable, int stencilFunc,
                                                          int stencilPass, int stencilFail, int stencilDepthFail,
                                                          int stencilMask, int stencilWriteMask, int referenceStencil,
                                                          bool twoSidedStencilMode,
                                                          int ccwStencilFunc, int ccwStencilPass,
                                                          int ccwStencilFail, int ccwStencilDepthFail)
    {
        if (depthEnable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(depthWriteEnable ? GL_TRUE : GL_FALSE);
        if (depthEnable) glDepthFunc(ToGLCompareFunc(depthFunc));

        if (stencilEnable) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (stencilEnable)
        {
            // ES1.1 core has no two-sided stencil (GL_OES_stencil8/two-sided variants are
            // separate, less-common extensions) -- applies the CW (front) face's state only,
            // documented deviation.
            (void)twoSidedStencilMode; (void)ccwStencilFunc; (void)ccwStencilPass;
            (void)ccwStencilFail; (void)ccwStencilDepthFail;
            glStencilFunc(ToGLCompareFunc(stencilFunc), referenceStencil, static_cast<GLuint>(stencilMask));
            glStencilOp(ToGLStencilOp(stencilFail), ToGLStencilOp(stencilDepthFail), ToGLStencilOp(stencilPass));
            glStencilMask(static_cast<GLuint>(stencilWriteMask));
        }
    }

    void OpenGLES1Renderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                        bool scissorTestEnable,
                                                        float depthBias,
                                                        float slopeScaleDepthBias)
    {
        // FillMode::WireFrame=1 -- ES1.1 has no glPolygonMode; OPENGLES1-76 emulates it by
        // re-expanding triangles into GL_LINES at draw time instead (see Draw*'s own wireframe_
        // checks and BuildWireframeLineIndices()).
        wireframe_ = (fillMode == 1);
        (void)depthBias; (void)slopeScaleDepthBias;  // ES1.1 has no glPolygonOffset.

        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2.
        if (cullMode == 0)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT);
        }
        if (scissorTestEnable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }

    void OpenGLES1Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {

        // Remember it: GraphicsDevice pushes sampler state down BEFORE the draw call binds its
        // textures, and GL stores filter/wrap on the texture object, so applying it here alone
        // would configure whichever texture happened to be bound at the time. The draw paths
        // re-apply it through ApplySamplerToBoundTextureEXT() once the right texture is bound.
        if (slot >= 0 && slot < kMaxSamplerSlots)
        {
            samplerFilter_[slot] = filter;
            samplerAddressU_[slot] = addressU;
            samplerAddressV_[slot] = addressV;
            samplerAnisotropy_[slot] = maxAnisotropy;
        }

        // Still apply immediately as well, which is what the SpriteBatch path (already binding
        // before it calls this) relies on.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGLMinFilter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGLMagFilter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(addressU, mirroredRepeatSupported_));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(addressV, mirroredRepeatSupported_));
        ApplyAnisotropy(GL_TEXTURE_2D, maxAnisotropy);
    }

    void OpenGLES1Renderer::ApplyAnisotropy(unsigned int target, int requested) const
    {
        if (maxAnisotropy_ <= 1.0f) return;   // extension absent -- nothing to set
        const float clamped = std::clamp(static_cast<float>(requested), 1.0f, maxAnisotropy_);
        glTexParameterf(static_cast<GLenum>(target), GL_TEXTURE_MAX_ANISOTROPY_EXT, clamped);
    }

    void OpenGLES1Renderer::ApplySamplerToBoundTextureEXT(int slot, unsigned int target) const
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        const GLenum t = static_cast<GLenum>(target);
        glTexParameteri(t, GL_TEXTURE_MIN_FILTER, ToGLMinFilter(samplerFilter_[slot]));
        glTexParameteri(t, GL_TEXTURE_MAG_FILTER, ToGLMagFilter(samplerFilter_[slot]));
        glTexParameteri(t, GL_TEXTURE_WRAP_S, ToGLWrapMode(samplerAddressU_[slot], mirroredRepeatSupported_));
        glTexParameteri(t, GL_TEXTURE_WRAP_T, ToGLWrapMode(samplerAddressV_[slot], mirroredRepeatSupported_));
        ApplyAnisotropy(target, samplerAnisotropy_[slot]);
    }

    void OpenGLES1Renderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        glScissor(x, physH - y - h, w, h);
    }

    void OpenGLES1Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        glViewport(x, y, w, h);
        glDepthRangef(minDepth, maxDepth);
    }

    bool OpenGLES1Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD: return true;
        case CNA::GraphicsCapability::DepthStencilBuffer: return true;
        case CNA::GraphicsCapability::MultiSampleAntiAliasing: return actualMultiSampleCount_ > 1;
        case CNA::GraphicsCapability::MultipleRenderTargets: return false;
        case CNA::GraphicsCapability::AnisotropicFiltering: return maxAnisotropy_ > 1.0f;
        // OPENGLES1-76: emulated via GL_LINES re-expansion (see ApplyRasterizerState/Draw*).
        case CNA::GraphicsCapability::WireFrame: return true;
        // ES 1.1 core has no occlusion-query mechanism at all (confirmed: no such extension
        // exists in the Khronos ES1.1 CM registry) -- a permanent gap, not "not yet implemented".
        case CNA::GraphicsCapability::OcclusionQuery: return false;
        // No programmable shaders at all on this renderer -- permanently unsupported, not a
        // "not yet implemented" gap.
        case CNA::GraphicsCapability::CustomEffects: return false;
        // REMED-CONTENT-004: no volume textures. GL_OES_texture_3D is an optional extension this
        // renderer does not use, and CreateTexture3D is left at IGraphicsRenderer's nullptr default,
        // so Texture3D's constructor must be allowed to refuse rather than build on a resource
        // that was never created.
        case CNA::GraphicsCapability::Texture3D: return false;
        // REMED-GFX-201: not implemented here. The fixed-function pointer setup binds one
        // GL_ARRAY_BUFFER and reads every attribute out of it at stride offsets, so there is no
        // second per-vertex stream to resolve.
        case CNA::GraphicsCapability::MultiStreamVertexInput: return false;
        // No instancing of any kind in the fixed-function ES 1.1 Common profile (no shaders, no
        // attribute divisors) -- DrawInstancedPrimitivesEx stays the shared base-class refusal.
        case CNA::GraphicsCapability::Instancing: return false;
        default: return true;
        }
    }

}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_OPENGLES1
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace OpenGLES1 { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> OpenGLES1::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenGLES1::OpenGLES1Renderer>(args);
    }
#endif
}
