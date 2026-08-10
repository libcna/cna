// SPDX-License-Identifier: MS-PL
//
// Real, working PortableGL (rswinkle/PortableGL) graphics renderer. `portablegl.h` is included
// here WITHOUT `#define PORTABLEGL_IMPLEMENTATION` -- that single define lives in
// PortableGLImpl.cpp (mirroring modules/renderers/sokol/src/SokolImpl.cpp's identical split), so
// this translation unit only sees PortableGL's declarations and links against the real
// definitions.
//
// PortableGL is a global-namespace C library. The only symbol name it defines that also exists as
// a CNA type alias in an ENCLOSING namespace is `Color` (`CNA::Internal::Renderers::Color`, a
// `using` alias for `Microsoft::Xna::Framework::Color` declared by IGraphicsRenderer.hpp). This
// file never needs PortableGL's own `::Color` type, so that alias is never shadowed and no
// `PGL_PREFIX_TYPES` renaming is needed -- every other PortableGL type used below (`vec4`,
// `Shader_Builtins`, `glContext`, the `GL_*` enum values) has no CNA counterpart to collide with.

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include "portablegl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::PortableGL
{
    namespace
    {
        /// Makes @p pglContext (a real `glContext*`, smuggled through the public header as
        /// `void*` so it never has to include portablegl.h) PortableGL's active context. Every
        /// method below that touches PGL state calls this first -- PGL keeps exactly one "current
        /// context" in a file-scope global, the same design real OpenGL's context/thread binding
        /// mirrors, so this is this renderer's equivalent of a native `wglMakeCurrent`/`eglMakeCurrent`.
        void MakeCurrent(void* pglContext)
        {
            set_glContext(static_cast<glContext*>(pglContext));
        }

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
                throw std::runtime_error("PortableGLRenderer: unrecognized PrimitiveType");
            }
        }

        GLenum ToPglMode(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
            default:
                throw std::runtime_error("PortableGLRenderer: unrecognized PrimitiveType");
            }
        }

        // ---- Colored 3D shader pair (VertexPositionColor) -------------------------------------
        //
        // Uniform is a single column-major MVP matrix, uploaded via Matrix::ToColumnMajor() --
        // the same CNA row-major -> GL column-major conversion EasyGLRenderer::DrawColoredPrimitives
        // performs before its own glUniformMatrix4fv call. The multiply below is therefore the
        // ordinary GLSL "column-major matrix times column vector" convention
        // (`gl_Position = mvp * in_vertex`), matching PortableGL's own documented shader-writing
        // model (see the header's own smooth_vs/smooth_fs example).

        struct ColoredUniforms
        {
            float mvp[16];
        };

        vec4 MultiplyColumnMajor(const float m[16], const vec4& v)
        {
            vec4 out{};
            out.x = m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w;
            out.y = m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w;
            out.z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w;
            out.w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w;
            return out;
        }

        void ColoredVertexShader(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
        {
            const auto* u = static_cast<const ColoredUniforms*>(uniforms);
            builtins->gl_Position = MultiplyColumnMajor(u->mvp, vertex_attribs[0]);
            const vec4& color = vertex_attribs[1];
            vs_output[0] = color.x;
            vs_output[1] = color.y;
            vs_output[2] = color.z;
            vs_output[3] = color.w;
        }

        void ColoredFragmentShader(float* fs_input, Shader_Builtins* builtins, void* /*uniforms*/)
        {
            builtins->gl_FragColor = vec4{fs_input[0], fs_input[1], fs_input[2], fs_input[3]};
        }

        // ---- Textured 2D shader pair (SpriteBatch quads) ---------------------------------------
        //
        // PortableGL fragment shaders sample a texture by calling texture2D(handle, u, v) with the
        // raw PGL texture handle passed explicitly -- there is no GLSL sampler-uniform indirection
        // in this software pipeline -- so the handle rides along inside the uniform struct itself.

        struct TexturedUniforms
        {
            float mvp[16];
            GLuint texture;
        };

        void TexturedVertexShader(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
        {
            const auto* u = static_cast<const TexturedUniforms*>(uniforms);
            builtins->gl_Position = MultiplyColumnMajor(u->mvp, vertex_attribs[0]);
            const vec4& uv = vertex_attribs[1];
            const vec4& color = vertex_attribs[2];
            vs_output[0] = uv.x;
            vs_output[1] = uv.y;
            vs_output[2] = color.x;
            vs_output[3] = color.y;
            vs_output[4] = color.z;
            vs_output[5] = color.w;
        }

        void TexturedFragmentShader(float* fs_input, Shader_Builtins* builtins, void* uniforms)
        {
            const auto* u = static_cast<const TexturedUniforms*>(uniforms);
            const vec4 texel = texture2D(u->texture, fs_input[0], fs_input[1]);
            builtins->gl_FragColor = vec4{
                texel.x * fs_input[2], texel.y * fs_input[3],
                texel.z * fs_input[4], texel.w * fs_input[5]};
        }
    }

    // =========================================================================================
    // PortableGLVertexBufferRenderer
    // =========================================================================================

    PortableGLVertexBufferRenderer::PortableGLVertexBufferRenderer(void* pglContext, int vertexCapacity)
        : pglContext_(pglContext), vertexCount_(vertexCapacity)
    {
        MakeCurrent(pglContext_);
        GLuint buf = 0;
        glGenBuffers(1, &buf);
        // PortableGL's own glGenBuffers does not initialize glBuffer::type -- only glBindBuffer
        // does ("Note type isn't set till binding", portablegl.h's own comment on glBindBuffer) --
        // so a buffer that is generated but never subsequently bound/uploaded (e.g. created and
        // then destroyed with no SetData() in between) leaves that field uninitialized, and
        // glDeleteBuffers unconditionally reads it to clear c->bound_buffers[type]. Binding once
        // here, before this handle can ever reach the destructor unbound, keeps that field always
        // well-defined (UBSan-verified fix).
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBuffer_ = buf;
    }

    PortableGLVertexBufferRenderer::~PortableGLVertexBufferRenderer()
    {
        if (glBuffer_ != 0)
        {
            MakeCurrent(pglContext_);
            GLuint buf = glBuffer_;
            glDeleteBuffers(1, &buf);
        }
    }

    void PortableGLVertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        MakeCurrent(pglContext_);
        stride_ = stride_in_bytes;
        vertexCount_ = vertex_count;
        glBindBuffer(GL_ARRAY_BUFFER, glBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(stride_in_bytes * static_cast<std::size_t>(vertex_count)),
                    data, GL_STATIC_DRAW);
    }

    // =========================================================================================
    // PortableGLIndexBufferRenderer
    // =========================================================================================

    PortableGLIndexBufferRenderer::PortableGLIndexBufferRenderer(void* pglContext, int indexCapacity)
        : pglContext_(pglContext), indexCount_(indexCapacity)
    {
        MakeCurrent(pglContext_);
        GLuint buf = 0;
        glGenBuffers(1, &buf);
        // See PortableGLVertexBufferRenderer's identical constructor comment.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
        glBuffer_ = buf;
    }

    PortableGLIndexBufferRenderer::~PortableGLIndexBufferRenderer()
    {
        if (glBuffer_ != 0)
        {
            MakeCurrent(pglContext_);
            GLuint buf = glBuffer_;
            glDeleteBuffers(1, &buf);
        }
    }

    void PortableGLIndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        MakeCurrent(pglContext_);
        indexCount_ = index_count;
        thirtyTwoBit_ = false;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(sizeof(std::uint16_t) * static_cast<std::size_t>(index_count)),
                    data, GL_STATIC_DRAW);
    }

    void PortableGLIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        MakeCurrent(pglContext_);
        indexCount_ = index_count;
        thirtyTwoBit_ = true;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(sizeof(std::uint32_t) * static_cast<std::size_t>(index_count)),
                    data, GL_STATIC_DRAW);
    }

    // =========================================================================================
    // PortableGLTextureRenderer
    // =========================================================================================

    PortableGLTextureRenderer::PortableGLTextureRenderer(void* pglContext, const ImageData& data)
        : pglContext_(pglContext), width_(data.width), height_(data.height)
    {
        MakeCurrent(pglContext_);
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // Deterministic point sampling by default -- CLAMP_TO_EDGE keeps sprite-quad edges from
        // bleeding into the wrap seam; a game-selected SamplerState is not yet plumbed through
        // this v1 renderer (see PortableGLRenderer's class doc).
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    data.pixels.data());
        glTexture_ = tex;
    }

    PortableGLTextureRenderer::~PortableGLTextureRenderer()
    {
        if (glTexture_ != 0)
        {
            MakeCurrent(pglContext_);
            GLuint tex = glTexture_;
            glDeleteTextures(1, &tex);
        }
    }

    // =========================================================================================
    // PortableGLSpriteBatchRenderer
    // =========================================================================================

    PortableGLSpriteBatchRenderer::PortableGLSpriteBatchRenderer(PortableGLRenderer& owner) : owner_(owner) {}

    void PortableGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
            Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
            0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void PortableGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                             const Rectangle& destinationRectangle,
                                             const Rectangle& sourceRectangle,
                                             const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
            SpriteEffects::None, 0.0f);
    }

    void PortableGLSpriteBatchRenderer::Draw(const ITextureRenderer& textureIn,
                                             const Rectangle& destinationRectangle,
                                             const Rectangle& sourceRectangle,
                                             const Color& color,
                                             float rotation,
                                             const Vector2& origin,
                                             SpriteEffects effects,
                                             float layerDepth)
    {
        (void)layerDepth; // v1: no depth-sorted batching, every Draw() is submitted immediately.
        if (!begun_)
            throw std::runtime_error("PortableGLSpriteBatchRenderer::Draw: Draw() called before Begin()");

        const auto& texture = static_cast<const PortableGLTextureRenderer&>(textureIn);

        const float texW = static_cast<float>(std::max(1, texture.GetWidth()));
        const float texH = static_cast<float>(std::max(1, texture.GetHeight()));
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

        const float r = color.getRProperty() / 255.0f;
        const float g = color.getGProperty() / 255.0f;
        const float b = color.getBProperty() / 255.0f;
        const float a = color.getAProperty() / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);
        const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
        const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
        const float ox = origin.X;
        const float oy = origin.Y;
        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        // Local (pre-rotation, pre-translation) corners, matching the Software renderer's own
        // SpriteBatch corner layout exactly (TL, TR, BR, BL) -- see SoftwareSpriteBatch.cpp.
        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);
        const auto placeCorner = [&](float px, float py, float outPixels[2]) {
            outPixels[0] = dx + px * cosR - py * sinR;
            outPixels[1] = dy + px * sinR + py * cosR;
        };

        float positions[4][2];
        placeCorner(p0x, p0y, positions[0]);
        placeCorner(p1x, p1y, positions[1]);
        placeCorner(p2x, p2y, positions[2]);
        placeCorner(p3x, p3y, positions[3]);

        const float uvs[4][2] = {{u1, v1}, {u2, v1}, {u2, v2}, {u1, v2}};
        const float colors[4][4] = {{r, g, b, a}, {r, g, b, a}, {r, g, b, a}, {r, g, b, a}};

        owner_.DrawTexturedQuadEXT(texture.GLTextureHandle(), positions, uvs, colors, transform_);
    }

    // =========================================================================================
    // PortableGLRenderer::Impl
    // =========================================================================================

    struct PortableGLRenderer::Impl
    {
        glContext context{};
        pix_t* backbuffer = nullptr;
        GLuint coloredProgram = 0;
        GLuint texturedProgram = 0;
        /// Reused dynamic vertex buffer for immediate-mode SpriteBatch quads (2 triangles, 6
        /// vertices of {vec2 pos, vec2 uv, vec4 color} = 32 bytes each), re-uploaded per Draw().
        GLuint quadBuffer = 0;
        bool depthTestEnabled = false;
        bool blendEnabled = false;
    };

    // =========================================================================================
    // PortableGLRenderer
    // =========================================================================================

    PortableGLRenderer::PortableGLRenderer(int virtualWidth, int virtualHeight)
        : impl_(std::make_unique<Impl>()), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        pix_t* backbuf = nullptr;
        if (!init_glContext(&impl_->context, &backbuf, virtualWidth, virtualHeight))
            throw std::runtime_error("PortableGLRenderer: init_glContext failed");
        impl_->backbuffer = backbuf;

        MakeCurrent(&impl_->context);
        glViewport(0, 0, virtualWidth, virtualHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepthf(1.0f);
        glClearStencil(0);
        // A fixed simple-alpha blend function -- ApplyBlendState's full factor/function mapping
        // is not implemented in v1 (see SupportsCapability(AdditiveBlending) == false), so
        // SetBlendEnabled(true) always means ordinary alpha blending, matching every other
        // minimal renderer's honest v1 boundary.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const GLenum coloredInterp[4] = {PGL_SMOOTH4};
        impl_->coloredProgram = pglCreateProgram(ColoredVertexShader, ColoredFragmentShader, 4,
                                                 const_cast<GLenum*>(coloredInterp), GL_FALSE);

        const GLenum texturedInterp[6] = {PGL_SMOOTH4, PGL_SMOOTH2};
        impl_->texturedProgram = pglCreateProgram(TexturedVertexShader, TexturedFragmentShader, 6,
                                                  const_cast<GLenum*>(texturedInterp), GL_FALSE);

        GLuint quadBuf = 0;
        glGenBuffers(1, &quadBuf);
        // See PortableGLVertexBufferRenderer's constructor comment -- this handle must be bound
        // once before it can reach ~PortableGLRenderer() unbound (e.g. a run that never issues a
        // single SpriteBatch draw).
        glBindBuffer(GL_ARRAY_BUFFER, quadBuf);
        impl_->quadBuffer = quadBuf;
    }

    PortableGLRenderer::~PortableGLRenderer()
    {
        MakeCurrent(&impl_->context);
        if (impl_->quadBuffer != 0)
        {
            GLuint buf = impl_->quadBuffer;
            glDeleteBuffers(1, &buf);
        }
        free_glContext(&impl_->context);
    }

    void PortableGLRenderer::Clear(float r, float g, float b, float a)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void PortableGLRenderer::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void PortableGLRenderer::SetVirtualResolution(int width, int height)
    {
        MakeCurrent(&impl_->context);
        if (!pglResizeFramebuffer(width, height))
            throw std::runtime_error("PortableGLRenderer: pglResizeFramebuffer failed");
        impl_->backbuffer = static_cast<pix_t*>(pglGetBackBuffer());
        virtualWidth_ = width;
        virtualHeight_ = height;
        glViewport(0, 0, width, height);
    }

    void PortableGLRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w < 0 || h < 0)
            throw std::runtime_error("PortableGLRenderer::ReadBackbuffer: negative width/height");

        MakeCurrent(&impl_->context);
        const int fbW = virtualWidth_;
        const int fbH = virtualHeight_;
        const auto* buf = reinterpret_cast<const std::uint8_t*>(impl_->backbuffer);

        // PortableGL's back_buffer stores row 0 (lowest memory address) as the BOTTOM row of the
        // image -- ordinary OpenGL bottom-left-origin convention (confirmed by init_glContext's
        // own `lastrow = buf + (h-1)*w*sizeof(pix_t)` computation). CNA's public ReadBackbuffer
        // contract is top-row-first (matching every other renderer's documented row order, e.g.
        // EasyGLRenderer::ReadBackbuffer's own "OpenGL origin is bottom-left; flip y" comment), so
        // each requested row is remapped here exactly the same way.
        for (int row = 0; row < h; ++row)
        {
            const int srcY = y + row;
            const int pglRow = fbH - 1 - srcY;
            if (pglRow < 0 || pglRow >= fbH)
            {
                std::memset(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u, 0,
                           static_cast<std::size_t>(w) * 4u);
                continue;
            }
            for (int col = 0; col < w; ++col)
            {
                const int srcX = x + col;
                const std::size_t dstIndex =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(col)) * 4u;
                if (srcX < 0 || srcX >= fbW)
                {
                    std::memset(pixels + dstIndex, 0, 4);
                    continue;
                }
                const std::size_t srcIndex =
                    (static_cast<std::size_t>(pglRow) * static_cast<std::size_t>(fbW) + static_cast<std::size_t>(srcX)) * 4u;
                std::memcpy(pixels + dstIndex, buf + srcIndex, 4);
            }
        }
    }

    std::unique_ptr<ITextureRenderer> PortableGLRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<PortableGLTextureRenderer>(&impl_->context, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> PortableGLRenderer::CreateSpriteBatch()
    {
        return std::make_unique<PortableGLSpriteBatchRenderer>(*this);
    }

    void PortableGLRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearDepth(float depth)
    {
        MakeCurrent(&impl_->context);
        glClearDepthf(depth);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearStencil(int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearStencil(stencil);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearDepthf(depth);
        glClearStencil(stencil);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void PortableGLRenderer::SetDepthTestEnabled(bool enabled)
    {
        MakeCurrent(&impl_->context);
        impl_->depthTestEnabled = enabled;
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void PortableGLRenderer::SetBlendEnabled(bool enabled)
    {
        MakeCurrent(&impl_->context);
        impl_->blendEnabled = enabled;
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void PortableGLRenderer::SetDepthWriteEnabled(bool enabled)
    {
        MakeCurrent(&impl_->context);
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    std::unique_ptr<IVertexBufferRenderer> PortableGLRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<PortableGLVertexBufferRenderer>(&impl_->context, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> PortableGLRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<PortableGLIndexBufferRenderer>(&impl_->context, index_capacity);
    }

    void PortableGLRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vbIn,
                                                   const Matrix& world,
                                                   const Matrix& view,
                                                   const Matrix& projection,
                                                   PrimitiveType primitive,
                                                   int primitiveCount)
    {
        const auto& vb = static_cast<const PortableGLVertexBufferRenderer&>(vbIn);
        if (vb.StrideInBytes() != 16)
            throw std::runtime_error(
                "PortableGLRenderer::DrawColoredPrimitives: only the 16-byte VertexPositionColor "
                "layout is supported by this v1 renderer.");

        MakeCurrent(&impl_->context);

        const Matrix combined = world * view * projection;
        ColoredUniforms uniforms{};
        combined.ToColumnMajor(uniforms.mvp);

        glUseProgram(impl_->coloredProgram);
        pglSetUniform(&uniforms);

        glBindBuffer(GL_ARRAY_BUFFER, vb.GLBufferHandle());
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<const GLvoid*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, reinterpret_cast<const GLvoid*>(12));
        glEnableVertexAttribArray(1);

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        glDrawArrays(ToPglMode(primitive), 0, vertexCount);
    }

    void PortableGLRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vbIn,
                                                          const IIndexBufferRenderer& ibIn,
                                                          const Matrix& world,
                                                          const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          int primitiveCount)
    {
        const auto& vb = static_cast<const PortableGLVertexBufferRenderer&>(vbIn);
        const auto& ib = static_cast<const PortableGLIndexBufferRenderer&>(ibIn);
        if (vb.StrideInBytes() != 16)
            throw std::runtime_error(
                "PortableGLRenderer::DrawIndexedColoredPrimitives: only the 16-byte "
                "VertexPositionColor layout is supported by this v1 renderer.");

        MakeCurrent(&impl_->context);

        const Matrix combined = world * view * projection;
        ColoredUniforms uniforms{};
        combined.ToColumnMajor(uniforms.mvp);

        glUseProgram(impl_->coloredProgram);
        pglSetUniform(&uniforms);

        glBindBuffer(GL_ARRAY_BUFFER, vb.GLBufferHandle());
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<const GLvoid*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, reinterpret_cast<const GLvoid*>(12));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.GLBufferHandle());
        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum indexType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        glDrawElements(ToPglMode(primitive), indexCount, indexType, nullptr);
    }

    bool PortableGLRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        // Real vertex/index buffers, real DrawColoredPrimitives/DrawIndexedColoredPrimitives,
        // real depth/stencil clears and state -- PortableGL's default configuration is
        // PGL_D24S8 (combined depth24+stencil8), so both are genuinely backed.
        case CNA::GraphicsCapability::ThreeD:
        case CNA::GraphicsCapability::DepthStencilBuffer:
        case CNA::GraphicsCapability::StencilBuffer:
            return true;
        // Not implemented in this v1 renderer -- see the class doc's scope note. Each of these
        // is left at the honest "false" rather than silently no-opping a claimed feature:
        // MultiSampleAntiAliasing (PortableGL has no MSAA), MultipleRenderTargets/OcclusionQuery/
        // Texture3D (CreateRenderTarget2D/CreateOcclusionQuery/CreateTexture3D are not overridden
        // and keep IGraphicsRenderer's nullptr defaults), AnisotropicFiltering/WireFrame
        // (ApplySamplerState/ApplyRasterizerState are not overridden), CustomEffects
        // (CreateEffectRenderer is not overridden), MultiStreamVertexInput/Instancing (only the
        // single-stream DrawColoredPrimitives/DrawIndexedColoredPrimitives routes are
        // implemented), AdditiveBlending (SetBlendEnabled always selects the one fixed
        // SRC_ALPHA/ONE_MINUS_SRC_ALPHA function, not ApplyBlendState's full per-BlendState
        // factor/function mapping).
        default:
            return false;
        }
    }

    void PortableGLRenderer::DrawTexturedQuadEXT(unsigned int glTexture,
                                                 const float positionsPixels[4][2],
                                                 const float uvs[4][2],
                                                 const float colorsRgba01[4][4],
                                                 const Matrix& spriteTransform)
    {
        MakeCurrent(&impl_->context);

        // Bakes the SpriteBatch ortho projection (0,0 top-left .. virtualWidth,virtualHeight
        // bottom-right) as GraphicsDevice.Viewport.Width/Height dictate -- REMED-GFX-072's
        // established SpriteBatch ortho convention, same left/right/bottom/top argument order
        // EasyGLRenderer's own SpriteBatch uses -- so vertex positions are supplied in ordinary
        // destination pixel space and the vertex shader alone performs the pixel -> NDC mapping.
        // `spriteTransform` composes on top exactly as EasyGLRenderer's own
        // `combined = transform_ * orthoM` does, so SetTransformMatrix() is honored for real.
        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(virtualWidth_), static_cast<float>(virtualHeight_), 0.0f, 0.0f, 1.0f);
        const Matrix combined = spriteTransform * orthoM;

        TexturedUniforms uniforms{};
        combined.ToColumnMajor(uniforms.mvp);
        uniforms.texture = glTexture;

        // Two triangles: (0,1,2) and (0,2,3), corners ordered TL, TR, BR, BL -- matching the
        // caller's own PortableGLSpriteBatchRenderer::Draw() corner layout exactly.
        const int order[6] = {0, 1, 2, 0, 2, 3};
        float vertices[6][8];
        for (int i = 0; i < 6; ++i)
        {
            const int c = order[i];
            vertices[i][0] = positionsPixels[c][0];
            vertices[i][1] = positionsPixels[c][1];
            vertices[i][2] = uvs[c][0];
            vertices[i][3] = uvs[c][1];
            vertices[i][4] = colorsRgba01[c][0];
            vertices[i][5] = colorsRgba01[c][1];
            vertices[i][6] = colorsRgba01[c][2];
            vertices[i][7] = colorsRgba01[c][3];
        }

        glUseProgram(impl_->texturedProgram);
        pglSetUniform(&uniforms);

        glBindBuffer(GL_ARRAY_BUFFER, impl_->quadBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        const GLsizei stride = 8 * sizeof(float);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(4 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<PortableGL::PortableGLRenderer>(args.virtualWidth, args.virtualHeight);
    }
}
