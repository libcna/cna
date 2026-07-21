// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/OpenGLES1/OpenGLES1GraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace CNA::Internal::Backends::OpenGLES1
{
    namespace
    {
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
        // docs/opengles1-backend.md).
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
        GLenum ToGLWrapMode(int xnaAddressMode)
        {
            switch (xnaAddressMode)
            {
            case 1: return GL_CLAMP_TO_EDGE;
            case 2: return GL_REPEAT;  // ES1.1 core lacks GL_MIRRORED_REPEAT -- falls back to Wrap.
            default: return GL_REPEAT;  // TextureAddressMode::Wrap = 0
            }
        }

        // XNA TextureFilter ordinal 1 (Point) -> nearest; everything else -> linear (matches the
        // ISpriteBatchBackend::SetSamplerFilter doc comment's own "others map to nearest" wording
        // for the inverse case -- here the default/linear-ish values map to GL_LINEAR).
        GLenum ToGLFilter(int xnaFilter)
        {
            return xnaFilter == 1 ? GL_NEAREST : GL_LINEAR;
        }
    }

    // -------------------------------------------------------------------------
    // OpenGLES1TextureBackend
    // -------------------------------------------------------------------------

    OpenGLES1TextureBackend::OpenGLES1TextureBackend(int width, int height)
        : width_(width), height_(height)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    OpenGLES1TextureBackend::~OpenGLES1TextureBackend()
    {
        if (texture_) glDeleteTextures(1, &texture_);
    }

    void OpenGLES1TextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    void OpenGLES1TextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    void OpenGLES1TextureBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    // -------------------------------------------------------------------------
    // OpenGLES1VertexBufferBackend / OpenGLES1IndexBufferBackend
    // -------------------------------------------------------------------------

    void OpenGLES1VertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        stride_ = stride_in_bytes;
        vertexCount_ = vertex_count;
        data_.resize(static_cast<std::size_t>(vertex_count) * stride_in_bytes);
        if (vertex_count > 0)
            std::memcpy(data_.data(), data, data_.size());
    }

    void OpenGLES1IndexBufferBackend::SetData16(const void* data, int index_count)
    {
        indexCount_ = index_count;
        data_.resize(static_cast<std::size_t>(index_count));
        if (index_count > 0)
            std::memcpy(data_.data(), data, data_.size() * sizeof(uint16_t));
    }

    // -------------------------------------------------------------------------
    // OpenGLES1SpriteBatchBackend
    // -------------------------------------------------------------------------

    void OpenGLES1SpriteBatchBackend::Begin()
    {
        begun_ = true;
    }

    void OpenGLES1SpriteBatchBackend::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGLES1SpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h),
             Microsoft::Xna::Framework::Color::White);
    }

    void OpenGLES1SpriteBatchBackend::Draw(const ITextureBackend& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenGLES1SpriteBatchBackend::Draw(const ITextureBackend& texture,
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

    void OpenGLES1SpriteBatchBackend::FlushBatch()
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
    // OpenGLES1GraphicsBackend — construction / context lifecycle
    // -------------------------------------------------------------------------

    OpenGLES1GraphicsBackend::OpenGLES1GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , swapInterval_(args.swapInterval)
    {
        if (!window_) throw std::runtime_error("OpenGLES1GraphicsBackend initialized with null window.");

        IGraphicsBackend::RegisterForWindow(window_, this);

        CreateGLContext();
        LoadExtensionEntryPoints();

        std::cout << "OpenGLES1GraphicsBackend initialized with OpenGL ES "
                  << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

        SDL_GL_SetSwapInterval(swapInterval_);

        glShadeModel(GL_SMOOTH);
        glEnable(GL_NORMALIZE);
    }

    OpenGLES1GraphicsBackend::~OpenGLES1GraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        DestroyGLContext();
    }

    void OpenGLES1GraphicsBackend::CreateGLContext()
    {
        // Requests a genuine OpenGL ES 1.1 (fixed-function "Common", CM) context -- this is the
        // one attribute set that distinguishes this backend from EasyGL's identical-shaped
        // SDL_GL_CreateContext call (which requests ES 3.0). See docs/opengles1-backend.md for
        // this project's own empirical finding that not every EGL/GLES driver actually implements
        // ES1 context creation despite advertising ES1-capable configs (a real Mesa llvmpipe
        // limitation found during this backend's own bring-up, not a theoretical concern).
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        glContext_ = SDL_GL_CreateContext(window_);
        if (!glContext_)
        {
            throw std::runtime_error(
                std::string("OpenGLES1: SDL_GL_CreateContext failed (no OpenGL ES 1.1 driver "
                            "available on this system): ") + SDL_GetError());
        }
    }

    void OpenGLES1GraphicsBackend::DestroyGLContext()
    {
        if (glContext_)
        {
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
            glContext_ = nullptr;
        }
    }

    void OpenGLES1GraphicsBackend::LoadExtensionEntryPoints()
    {
        glBlendFuncSeparateOES_ = reinterpret_cast<PFNGLBLENDFUNCSEPARATEOESPROC>(
            SDL_GL_GetProcAddress("glBlendFuncSeparateOES"));
        glBlendEquationOES_ = reinterpret_cast<PFNGLBLENDEQUATIONOESPROC>(
            SDL_GL_GetProcAddress("glBlendEquationOES"));
    }

    void OpenGLES1GraphicsBackend::DebugSimulateContextLoss()
    {
        DestroyGLContext();
    }

    void OpenGLES1GraphicsBackend::DebugRestoreContext()
    {
        CreateGLContext();
        LoadExtensionEntryPoints();
        SDL_GL_SetSwapInterval(swapInterval_);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_NORMALIZE);
    }

    // -------------------------------------------------------------------------
    // Presentation / viewport
    // -------------------------------------------------------------------------

    void OpenGLES1GraphicsBackend::GetPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    void OpenGLES1GraphicsBackend::GetLogicalSize(int& width, int& height) const
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

    void OpenGLES1GraphicsBackend::ApplyLogicalViewportAndOrtho2D()
    {
        int physW = 0, physH = 0;
        GetPhysicalSize(physW, physH);
        if (physW > 0 && physH > 0)
            glViewport(0, 0, physW, physH);

        int logW = 0, logH = 0;
        GetLogicalSize(logW, logH);
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

    void OpenGLES1GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void OpenGLES1GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGLES1GraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void OpenGLES1GraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    bool OpenGLES1GraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                             float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool OpenGLES1GraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                            float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical -- see EasyGLGraphicsBackend's identical method for
        // why this is a pure uniform scale with no offset under the default FixedHeightDynamicWidth
        // presentation mode (no letterbox bars, so no offset to invert).
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    void OpenGLES1GraphicsBackend::Present()
    {
        SDL_GL_SwapWindow(window_);
    }

    // -------------------------------------------------------------------------
    // Clears
    // -------------------------------------------------------------------------

    void OpenGLES1GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearDepth(float depth)
    {
        glClearDepthf(depth);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepthf(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGLES1GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // GL's window-space Y grows upward; XNA/CNA's (x,y) here are top-left game coordinates,
        // so flip the row origin before reading, then flip the rows back afterward (matches every
        // other GL-based backend's ReadBackbuffer convention).
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

    void OpenGLES1GraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void OpenGLES1GraphicsBackend::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void OpenGLES1GraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    // -------------------------------------------------------------------------
    // Resource creation
    // -------------------------------------------------------------------------

    std::unique_ptr<ITextureBackend> OpenGLES1GraphicsBackend::CreateTexture(const ImageData& data)
    {
        auto tex = std::make_unique<OpenGLES1TextureBackend>(data.width, data.height);
        tex->UpdatePixels(data.pixels.data(), data.width * 4);
        return tex;
    }

    std::unique_ptr<ISpriteBatchBackend> OpenGLES1GraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<OpenGLES1SpriteBatchBackend>(this);
    }

    std::unique_ptr<IVertexBufferBackend> OpenGLES1GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<OpenGLES1VertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> OpenGLES1GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<OpenGLES1IndexBufferBackend>(index_capacity);
    }

    // -------------------------------------------------------------------------
    // 3D draw — fixed-function vertex layout dispatch
    // -------------------------------------------------------------------------

    namespace
    {
        // Byte strides established across every CNA backend: VertexPositionColor=16,
        // VertexPositionTexture=20, VertexPositionColorTexture=24, VertexPositionNormalTexture=32.
        constexpr std::size_t kStrideColor = 16;
        constexpr std::size_t kStrideTexture = 20;
        constexpr std::size_t kStrideColorTexture = 24;
        constexpr std::size_t kStrideNormalTexture = 32;

        void SetupClientArraysForStride(const uint8_t* base, std::size_t stride, bool wantColor, bool wantTexture, bool wantNormal)
        {
            glVertexPointer(3, GL_FLOAT, static_cast<GLsizei>(stride), base);

            if (wantColor)
            {
                glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(4, GL_UNSIGNED_BYTE, static_cast<GLsizei>(stride), base + 12);
            }
            else
            {
                glDisableClientState(GL_COLOR_ARRAY);
            }

            if (wantTexture)
            {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                const std::size_t uvOffset = stride - 8;
                glTexCoordPointer(2, GL_FLOAT, static_cast<GLsizei>(stride), base + uvOffset);
            }
            else
            {
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }

            if (wantNormal)
            {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, static_cast<GLsizei>(stride), base + 12);
            }
            else
            {
                glDisableClientState(GL_NORMAL_ARRAY);
            }
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
        // documents the rest as an intentional deviation (see docs/opengles1-backend.md).
        void ApplyAlphaTest(const GpuDrawParams& params)
        {
            const float ref = params.alphaTest[0];
            const float tolerance = params.alphaTest[1];
            const float passWeight = params.alphaTest[2];
            const float failWeight = params.alphaTest[3];
            if (passWeight >= failWeight && tolerance <= 0.0f && ref <= 0.0f)
            {
                glDisable(GL_ALPHA_TEST);
                return;
            }
            glEnable(GL_ALPHA_TEST);
            if (tolerance > 0.0f)
                glAlphaFunc(passWeight >= failWeight ? GL_EQUAL : GL_NOTEQUAL, ref);
            else
                glAlphaFunc(passWeight >= failWeight ? GL_GEQUAL : GL_LESS, ref);
        }
    }

    void OpenGLES1GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount)
    {
        const auto& vb = static_cast<const OpenGLES1VertexBufferBackend&>(vb_in);

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

        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Data(), vb.Stride(), /*color*/true, /*texture*/false, /*normal*/false);

        glDrawArrays(ToGLPrimitive(primitive), 0, VertexCountForPrimitives(primitive, primitiveCount));
    }

    void OpenGLES1GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                                PrimitiveType primitive, int primitiveCount)
    {
        const auto& vb = static_cast<const OpenGLES1VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGLES1IndexBufferBackend&>(ib_in);

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

        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Data(), vb.Stride(), /*color*/true, /*texture*/false, /*normal*/false);

        glDrawElements(ToGLPrimitive(primitive), VertexCountForPrimitives(primitive, primitiveCount),
                      GL_UNSIGNED_SHORT, ib.Data());
    }

    void OpenGLES1GraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGLES1VertexBufferBackend&>(vb_in);
        const std::size_t stride = vb.Stride();

        // Skinning/PBR/env-mapping/custom-shader have no fixed-function equivalent on this
        // backend -- see docs/opengles1-backend.md's "Important limitations" -- fall back to the
        // plain colored path rather than silently rendering something wrong.
        if (params.skinned || params.pbr || params.envMapping || params.dualTexture
            || params.customEffectBackend || params.instanceCount > 1)
        {
            DrawColoredPrimitives(vb_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        float projCol[16], viewCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        view.ToColumnMajor(viewCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);

        const bool wantTexture = params.textureEnabled && params.texture0 != nullptr && stride != kStrideColor;
        const bool wantNormal = params.lightingEnabled && stride == kStrideNormalTexture;
        const bool wantColorArray = params.vertexColorEnabled && (stride == kStrideColor || stride == kStrideColorTexture);

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

        if (params.fogEnabled)
        {
            glEnable(GL_FOG);
            glFogf(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, params.fogStart);
            glFogf(GL_FOG_END, params.fogEnd);
            const float fogColor4[4] = {params.fogColor[0], params.fogColor[1], params.fogColor[2], 1.0f};
            glFogfv(GL_FOG_COLOR, fogColor4);
        }
        else
        {
            glDisable(GL_FOG);
        }

        ApplyAlphaTest(params);

        if (wantTexture)
        {
            glEnable(GL_TEXTURE_2D);
            params.texture0->BindGL();
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
        }

        if (!wantColorArray && !params.lightingEnabled)
        {
            // No per-vertex color and no lighting to compute one -- use the flat DiffuseColor as
            // the constant "current color" GL_MODULATE multiplies the texture sample by. When
            // vertex color IS enabled, DiffuseColor is not separately multiplied in (no
            // multiply-both-inputs fixed-function stage without extra GL_COMBINE setup) --
            // documented limitation, see docs/opengles1-backend.md.
            glColor4f(params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2], params.diffuseColor[3]);
        }

        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Data(), stride, wantColorArray, wantTexture, wantNormal);

        glDrawArrays(ToGLPrimitive(primitive), 0, VertexCountForPrimitives(primitive, primitiveCount));

        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
    }

    void OpenGLES1GraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                           const Matrix& world, const Matrix& view, const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount,
                                                           const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGLES1VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGLES1IndexBufferBackend&>(ib_in);
        const std::size_t stride = vb.Stride();

        if (params.skinned || params.pbr || params.envMapping || params.dualTexture
            || params.customEffectBackend || params.instanceCount > 1)
        {
            DrawIndexedColoredPrimitives(vb_in, ib_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        float projCol[16], viewCol[16], mvCol[16];
        projection.ToColumnMajor(projCol);
        view.ToColumnMajor(viewCol);
        (world * view).ToColumnMajor(mvCol);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projCol);

        const bool wantTexture = params.textureEnabled && params.texture0 != nullptr && stride != kStrideColor;
        const bool wantNormal = params.lightingEnabled && stride == kStrideNormalTexture;
        const bool wantColorArray = params.vertexColorEnabled && (stride == kStrideColor || stride == kStrideColorTexture);

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

        if (params.fogEnabled)
        {
            glEnable(GL_FOG);
            glFogf(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, params.fogStart);
            glFogf(GL_FOG_END, params.fogEnd);
            const float fogColor4[4] = {params.fogColor[0], params.fogColor[1], params.fogColor[2], 1.0f};
            glFogfv(GL_FOG_COLOR, fogColor4);
        }
        else
        {
            glDisable(GL_FOG);
        }

        ApplyAlphaTest(params);

        if (wantTexture)
        {
            glEnable(GL_TEXTURE_2D);
            params.texture0->BindGL();
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
        }

        if (!wantColorArray && !params.lightingEnabled)
        {
            glColor4f(params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2], params.diffuseColor[3]);
        }

        glEnableClientState(GL_VERTEX_ARRAY);
        SetupClientArraysForStride(vb.Data(), stride, wantColorArray, wantTexture, wantNormal);

        glDrawElements(ToGLPrimitive(primitive), VertexCountForPrimitives(primitive, primitiveCount),
                      GL_UNSIGNED_SHORT, ib.Data());

        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
    }

    // -------------------------------------------------------------------------
    // Graphics state
    // -------------------------------------------------------------------------

    void OpenGLES1GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                   int colorDstBlend, int alphaDstBlend,
                                                   int colorBlendFunc, int alphaBlendFunc)
    {
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

        if (glBlendEquationOES_)
        {
            // GL_OES_blend_subtract only defines Add/Subtract/ReverseSubtract -- Max/Min
            // (BlendFunction ordinals 3/4) have no ES1.1 equivalent and fall back to Add
            // (documented deviation, see docs/opengles1-backend.md).
            const auto toEquation = [](int xnaBlendFunc) -> GLenum
            {
                switch (xnaBlendFunc)
                {
                case 1: return GL_FUNC_SUBTRACT_OES;
                case 2: return GL_FUNC_REVERSE_SUBTRACT_OES;
                default: return GL_FUNC_ADD_OES;
                }
            };
            glBlendEquationOES_(toEquation(colorBlendFunc));
        }
    }

    void OpenGLES1GraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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

    void OpenGLES1GraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                        bool scissorTestEnable,
                                                        float depthBias,
                                                        float slopeScaleDepthBias)
    {
        (void)fillMode;  // ES1.1 has no glPolygonMode; wireframe emulation is future work.
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

    void OpenGLES1GraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        (void)slot; (void)maxAnisotropy;  // Single texture unit used by this backend's baseline.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGLFilter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGLFilter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(addressU));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(addressV));
    }

    void OpenGLES1GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        int physW, physH;
        GetPhysicalSize(physW, physH);
        glScissor(x, physH - y - h, w, h);
    }

    void OpenGLES1GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        glViewport(x, y, w, h);
        glDepthRangef(minDepth, maxDepth);
    }

    bool OpenGLES1GraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD: return true;
        case CNA::GraphicsCapability::DepthStencilBuffer: return true;
        case CNA::GraphicsCapability::MultiSampleAntiAliasing: return false;
        case CNA::GraphicsCapability::MultipleRenderTargets: return false;
        case CNA::GraphicsCapability::AnisotropicFiltering: return false;
        case CNA::GraphicsCapability::WireFrame: return false;
        case CNA::GraphicsCapability::OcclusionQuery: return false;
        // No programmable shaders at all on this backend -- permanently unsupported, not a
        // "not yet implemented" gap.
        case CNA::GraphicsCapability::CustomEffects: return false;
        default: return true;
        }
    }

}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_OPENGLES1
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<OpenGLES1::OpenGLES1GraphicsBackend>(args);
    }
#endif
}
