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
//
// NOTE on PortableGL's `GL_*` constants: they are values of PortableGL's OWN sequential enum, not
// the numeric OpenGL tokens. Nothing below may assume an XNA ordinal happens to equal one of them;
// every enum crossing this boundary is translated by an explicit switch that rejects what it does
// not recognize.

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include "portablegl.h"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::PortableGL
{
    namespace
    {
        /// The renderer's public CNA identity, used verbatim in every diagnostic below.
        constexpr const char* kRendererName = "PORTABLEGL";

        [[noreturn]] void Unsupported(const std::string& message)
        {
            throw System::NotSupportedException(std::string(kRendererName) + ": " + message);
        }

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

        /// Leaves exactly attrib slots [0, usedCount) enabled and every other slot PortableGL has
        /// disabled. PortableGL's vertex_stage walks ALL GL_MAX_VERTEX_ATTRIBS slots on every draw
        /// and fetches each one whose `enabled` flag is set, straight out of the buffer/offset/
        /// stride that slot's last glVertexAttribPointer recorded -- it has no notion of "the
        /// current program only declares N inputs". A slot left enabled by a previous draw that
        /// used a different program is therefore still read, at that draw's stride, for as many
        /// vertices as the CURRENT draw submits, which reads past the end of the older (smaller)
        /// buffer as soon as the new draw is the longer one. Every draw entry point below states
        /// its complete attrib-enable set through this helper instead of only enabling the slots
        /// it wants, so no cross-program state can survive.
        void SelectVertexAttribArrays(int usedCount)
        {
            for (int i = 0; i < GL_MAX_VERTEX_ATTRIBS; ++i)
            {
                if (i < usedCount)
                    glEnableVertexAttribArray(static_cast<GLuint>(i));
                else
                    glDisableVertexAttribArray(static_cast<GLuint>(i));
            }
        }

        /// Maps a public `Graphics::CompareFunction` ordinal (Always=0, Never=1, Less=2,
        /// LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7) to PortableGL's own
        /// depth/stencil comparison enum. Used for both the depth and the stencil comparison --
        /// XNA expresses them with the same enum, and so does PortableGL.
        GLenum ToPglCompareFunc(int compareFunctionOrdinal, const char* what)
        {
            switch (compareFunctionOrdinal)
            {
            case 0: return GL_ALWAYS;
            case 1: return GL_NEVER;
            case 2: return GL_LESS;
            case 3: return GL_LEQUAL;
            case 4: return GL_EQUAL;
            case 5: return GL_GEQUAL;
            case 6: return GL_GREATER;
            case 7: return GL_NOTEQUAL;
            default:
                Unsupported(std::string("unsupported ") + what + " CompareFunction ordinal " +
                            std::to_string(compareFunctionOrdinal));
            }
        }

        /// Maps a public `Graphics::StencilOperation` ordinal to PortableGL's own stencil op enum.
        /// XNA's `Increment`/`Decrement` WRAP and its `*Saturation` variants CLAMP -- the opposite
        /// naming to GL's, where `GL_INCR`/`GL_DECR` clamp and the `_WRAP` forms wrap. The mapping
        /// below is therefore deliberately crossed, matching EasyGL's own table exactly.
        GLenum ToPglStencilOp(int stencilOperationOrdinal)
        {
            switch (stencilOperationOrdinal)
            {
            case 0: return GL_KEEP;
            case 1: return GL_ZERO;
            case 2: return GL_REPLACE;
            case 3: return GL_INCR_WRAP;   // StencilOperation::Increment (wraps)
            case 4: return GL_DECR_WRAP;   // StencilOperation::Decrement (wraps)
            case 5: return GL_INCR;        // StencilOperation::IncrementSaturation (clamps)
            case 6: return GL_DECR;        // StencilOperation::DecrementSaturation (clamps)
            case 7: return GL_INVERT;
            default:
                Unsupported("unsupported StencilOperation ordinal " +
                            std::to_string(stencilOperationOrdinal));
            }
        }

        /// Maps a public `Graphics::Blend` ordinal to PortableGL's own blend-factor enum. Same
        /// table every other CNA renderer transcribes; PortableGL's `blend_pixel` implements all
        /// thirteen, including the constant-colour pair `BlendFactor`/`InverseBlendFactor` and
        /// `SourceAlphaSaturation`.
        GLenum ToPglBlendFactor(int blendOrdinal)
        {
            switch (blendOrdinal)
            {
            case  0: return GL_ONE;
            case  1: return GL_ZERO;
            case  2: return GL_SRC_COLOR;
            case  3: return GL_ONE_MINUS_SRC_COLOR;
            case  4: return GL_SRC_ALPHA;
            case  5: return GL_ONE_MINUS_SRC_ALPHA;
            case  6: return GL_DST_COLOR;
            case  7: return GL_ONE_MINUS_DST_COLOR;
            case  8: return GL_DST_ALPHA;
            case  9: return GL_ONE_MINUS_DST_ALPHA;
            case 10: return GL_CONSTANT_COLOR;
            case 11: return GL_ONE_MINUS_CONSTANT_COLOR;
            case 12: return GL_SRC_ALPHA_SATURATE;
            default:
                Unsupported("unsupported Blend ordinal " + std::to_string(blendOrdinal));
            }
        }

        /// Maps a public `Graphics::BlendFunction` ordinal to PortableGL's own blend equation.
        GLenum ToPglBlendEquation(int blendFunctionOrdinal)
        {
            switch (blendFunctionOrdinal)
            {
            case 0: return GL_FUNC_ADD;
            case 1: return GL_FUNC_SUBTRACT;
            case 2: return GL_FUNC_REVERSE_SUBTRACT;
            case 3: return GL_MAX;
            case 4: return GL_MIN;
            default:
                Unsupported("unsupported BlendFunction ordinal " +
                            std::to_string(blendFunctionOrdinal));
            }
        }

        /// Maps a public `Graphics::TextureFilter` ordinal onto PortableGL's single per-texture
        /// filter.
        ///
        /// PortableGL stores `min_filter` and `mag_filter` but `texture2D()` samples with
        /// `mag_filter` only, and a PortableGL texture always has exactly one level (there is no
        /// mip chain: upstream's `glGenerateMipmap` is a documented no-op and this renderer never
        /// allocates one). So the four ordinals whose whole purpose is to make minification differ
        /// from magnification cannot be represented at all, and neither can `Anisotropic`; those
        /// are refused rather than silently downgraded to the nearest expressible filter. The two
        /// remaining mip-qualified ordinals (`LinearMipPoint`, `PointMipLinear`) have min == mag
        /// and only differ in a mip component that a single-level texture cannot exercise, so they
        /// are exactly representable.
        GLenum ToPglTextureFilter(int textureFilterOrdinal)
        {
            switch (textureFilterOrdinal)
            {
            case 0: return GL_LINEAR;   // Linear
            case 1: return GL_NEAREST;  // Point
            case 3: return GL_LINEAR;   // LinearMipPoint  (min == mag == Linear)
            case 4: return GL_NEAREST;  // PointMipLinear  (min == mag == Point)
            case 2:
                Unsupported(
                    "TextureFilter::Anisotropic is not supported -- PortableGL has no anisotropic "
                    "sampler, which is why SupportsCapability(AnisotropicFiltering) reports false.");
            case 5: case 6: case 7: case 8:
                Unsupported(
                    "a TextureFilter whose minification and magnification components differ is not "
                    "supported -- PortableGL's texture2D() samples with a single filter and its "
                    "textures have exactly one mip level, so the request cannot be represented.");
            default:
                Unsupported("unsupported TextureFilter ordinal " +
                            std::to_string(textureFilterOrdinal));
            }
        }

        /// Maps a public `Graphics::TextureAddressMode` ordinal onto PortableGL's wrap enum.
        GLenum ToPglTextureAddress(int textureAddressModeOrdinal)
        {
            switch (textureAddressModeOrdinal)
            {
            case 0: return GL_REPEAT;          // Wrap
            case 1: return GL_CLAMP_TO_EDGE;   // Clamp
            case 2: return GL_MIRRORED_REPEAT; // Mirror
            default:
                Unsupported("unsupported TextureAddressMode ordinal " +
                            std::to_string(textureAddressModeOrdinal));
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

        // ---- Fragment output conversion ---------------------------------------------------------
        //
        // PortableGL converts a fragment's [0,1] float colour to bytes by TRUNCATING `v * 255.0f`
        // (its own v4_to_Color documents that choice against the rounding alternatives), and it
        // does so at the very end of draw_pixel -- AFTER blending. Every other CNA renderer, and
        // every real GPU, rounds to nearest there.
        //
        // The visible consequence is not the byte -> float -> byte round trip, which is exact for
        // all 256 values: it is the perspective-correct interpolation PortableGL runs between the
        // vertex and fragment stage, whose float result for a constant attribute lands an ulp
        // BELOW the exact value about as often as above. Truncation turns that half-ulp of noise
        // into a whole missing LSB, so an ordinary Color(200, 30, 90) rasterizes as (199, 29, 89).
        //
        // The fix is to quantize the fragment colour onto the 8-bit grid the CNA colour model
        // already uses, NOT to bias it. Quantizing removes the interpolation noise (which is what
        // was actually wrong) and leaves an exactly representable n/255, so PortableGL's truncation
        // reproduces n exactly. Crucially it is neutral for blending, which consumes this value as
        // the source colour and source alpha: alpha 0 stays exactly 0 (a fully transparent source
        // still leaves the destination bit-for-bit unchanged), alpha 255 stays exactly 1 (an opaque
        // source still overwrites exactly), and no systematic +0.5/255 term is injected into either
        // factor. An earlier revision of this renderer added exactly that bias, which silently made
        // every alpha-blended result wrong by half an LSB and turned a transparent source into a
        // faintly visible one.
        //
        // What remains -- and is documented as a limitation rather than hidden -- is that the
        // BLENDED result is still truncated by PortableGL itself, so a partial-alpha blend can land
        // one LSB below a round-to-nearest GPU. Every case whose blend factors are exactly 0 or 1
        // (Opaque, an opaque source under AlphaBlend/NonPremultiplied, a fully transparent source)
        // is exact.
        float QuantizeToByteGrid(float v)
        {
            // Written so that NaN takes the first branch and becomes 0 rather than propagating into
            // the framebuffer. GL clamps a fragment colour to [0,1] for a fixed-point framebuffer
            // before blending, so clamping here is the specified behaviour, not an approximation.
            if (!(v > 0.0f)) return 0.0f;
            if (v >= 1.0f) return 1.0f;
            return std::floor(v * 255.0f + 0.5f) / 255.0f;
        }

        vec4 EmitFragmentColor(float r, float g, float b, float a)
        {
            return vec4{QuantizeToByteGrid(r), QuantizeToByteGrid(g),
                        QuantizeToByteGrid(b), QuantizeToByteGrid(a)};
        }

        // ---- Colored 3D shader pair (VertexPositionColor) -------------------------------------
        //
        // Uniform is a single column-major MVP matrix, uploaded via Matrix::ToColumnMajor() --
        // the same CNA row-major -> GL column-major conversion EasyGLRenderer::DrawColoredPrimitives
        // performs before its own glUniformMatrix4fv call. The multiply below is therefore the
        // ordinary GLSL "column-major matrix times column vector" convention
        // (`gl_Position = mvp * in_vertex`), matching PortableGL's own documented shader-writing
        // model (see the header's own smooth_vs/smooth_fs example).
        //
        // The fragment stage implements the SAME unlit stock-effect formula every other CNA
        // renderer's colored program implements (see EasyGLRenderer's `prog_colored_` source):
        //
        //     vc        = VertexColorEnabled ? interpolated vertex colour : (1,1,1,1)
        //     FragColor = vc * DiffuseColor
        //
        // where DiffuseColor is GpuDrawParams::diffuseColor, i.e. BasicEffect's own
        // (DiffuseColor [+ EmissiveColor] * Alpha, Alpha). BasicEffect's default
        // VertexColorEnabled is FALSE, so a draw that does not opt in genuinely renders the flat
        // diffuse colour here, exactly as it does everywhere else in CNA.

        struct ColoredUniforms
        {
            float mvp[16];
            float diffuse[4];
            float vertexColorEnabled;
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
            const bool useVertexColor = u->vertexColorEnabled > 0.5f;
            vs_output[0] = useVertexColor ? color.x : 1.0f;
            vs_output[1] = useVertexColor ? color.y : 1.0f;
            vs_output[2] = useVertexColor ? color.z : 1.0f;
            vs_output[3] = useVertexColor ? color.w : 1.0f;
        }

        void ColoredFragmentShader(float* fs_input, Shader_Builtins* builtins, void* uniforms)
        {
            const auto* u = static_cast<const ColoredUniforms*>(uniforms);
            builtins->gl_FragColor = EmitFragmentColor(
                fs_input[0] * u->diffuse[0], fs_input[1] * u->diffuse[1],
                fs_input[2] * u->diffuse[2], fs_input[3] * u->diffuse[3]);
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
            builtins->gl_FragColor = EmitFragmentColor(
                texel.x * fs_input[2], texel.y * fs_input[3],
                texel.z * fs_input[4], texel.w * fs_input[5]);
        }

        // ---- Draw-parameter translation ---------------------------------------------------------

        /// The byte offset of the first attribute record a draw fetches, computed in the wide type
        /// before it is narrowed to the pointer-sized offset PortableGL stores.
        std::ptrdiff_t AttributeBaseBytes(long long firstElement, std::size_t stride)
        {
            return static_cast<std::ptrdiff_t>(firstElement * static_cast<long long>(stride));
        }
    }

    PortableGLRenderer::ColoredDrawState PortableGLRenderer::TranslateDrawParams(
        const GpuDrawParams& p, const char* route)
    {
        const auto refuse = [&](const std::string& what) {
            Unsupported("the " + std::string(route) + " route cannot execute " + what +
                        ". This renderer implements the unlit VertexPositionColor stock-effect "
                        "subset only (VertexColorEnabled, DiffuseColor, Alpha); an unsupported "
                        "configuration is refused rather than rendered as something else.");
        };

        if (p.textureEnabled || p.texture0 != nullptr || p.texture1 != nullptr)
            refuse("a textured effect");
        if (p.lightingEnabled)
            refuse("BasicEffect lighting");
        if (p.fogEnabled)
            refuse("BasicEffect fog");
        if (p.dualTexture)
            refuse("DualTextureEffect");
        if (p.envMapping || p.envMap != nullptr)
            refuse("EnvironmentMapEffect");
        if (p.skinned)
            refuse("SkinnedEffect");
        if (p.pbr)
            refuse("PbrEffect");
        if (p.customEffectRenderer != nullptr)
            refuse("a compiled custom Effect program");
        // GpuDrawParams' documented "always pass" default is {0, 0, 1, 1}. Anything else is a real
        // AlphaTestEffect configuration, which needs a discarding fragment shader this renderer's
        // programs are deliberately not created with (pglCreateProgram's `fragdepth_or_discard` is
        // GL_FALSE, which lets PortableGL keep early fragment processing).
        if (p.alphaTest[0] != 0.0f || p.alphaTest[1] != 0.0f ||
            p.alphaTest[2] != 1.0f || p.alphaTest[3] != 1.0f)
            refuse("an alpha test");
        if (p.instanceCount > 1)
            refuse("instanced submission");
        if (p.cpu2DColorMatrixEnabled)
            refuse("a ColorMatrixEffect");

        RejectUnsupportedStreamCombination(p, kRendererName);

        ColoredDrawState state;
        state.diffuse[0] = p.diffuseColor[0];
        state.diffuse[1] = p.diffuseColor[1];
        state.diffuse[2] = p.diffuseColor[2];
        state.diffuse[3] = p.diffuseColor[3];
        state.vertexColorEnabled = p.vertexColorEnabled;
        if (const GpuVertexStreamBinding* stream = FirstPerVertexStream(p))
            state.streamVertexOffset = stream->vertexOffset;
        state.vertexStart = p.vertexStart;
        state.startIndex = p.startIndex;
        state.baseVertex = p.baseVertex;
        state.minVertexIndex = p.minVertexIndex;
        state.numVertices = p.numVertices;
        return state;
    }

    // =========================================================================================
    // PortableGLVertexBufferRenderer
    // =========================================================================================
    //
    // Resource-lifetime note (verified, not assumed): the three handle classes below keep a raw
    // pointer to the owning renderer's glContext and re-make it current from their own
    // destructors. GraphicsDevice::Dispose() disposes every registered GraphicsResource -- which
    // is what VertexBuffer/IndexBuffer/Texture2D are, and each of their Dispose(bool) overrides
    // resets the renderer handle this class implements -- BEFORE destroyNativeResources() resets
    // the IGraphicsRenderer itself. So the context these destructors touch is always still alive.
    // That ordering is a GraphicsDevice-level guarantee shared by every renderer (EasyGL's
    // glDeleteTextures and its ResourceRegistry back-pointer have the identical requirement), not
    // anything PortableGL-specific, so no extra ownership machinery is added here.

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
        if (vertex_count < 0)
            throw std::runtime_error("PortableGLVertexBufferRenderer::SetData: negative vertex count");

        MakeCurrent(pglContext_);
        stride_ = stride_in_bytes;
        vertexCount_ = vertex_count;
        glBindBuffer(GL_ARRAY_BUFFER, glBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(stride_in_bytes * static_cast<std::size_t>(vertex_count)),
                    data, GL_STATIC_DRAW);
    }

    void PortableGLVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // REMED-GFX-DECL-GUARD: this renderer selects its native input layout from the upload
        // stride, so the declaration is what every draw has to be checked against -- two different
        // declarations can share a stride, and the one that does not describe
        // Position0@0 Vector3 + Color0@12 Color would otherwise be read as if it did.
        declared_.Remember(vertexDeclaration);
    }

    // =========================================================================================
    // PortableGLIndexBufferRenderer
    // =========================================================================================

    PortableGLIndexBufferRenderer::PortableGLIndexBufferRenderer(
        void* pglContext, int indexCapacity, bool thirtyTwoBit)
        : pglContext_(pglContext), indexCount_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
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
        if (index_count < 0)
            throw std::runtime_error("PortableGLIndexBufferRenderer::SetData16: negative index count");
        if (thirtyTwoBit_)
            throw std::runtime_error(
                "PortableGLIndexBufferRenderer::SetData16: upload width does not match the declared 32-bit buffer");

        MakeCurrent(pglContext_);
        indexCount_ = index_count;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(sizeof(std::uint16_t) * static_cast<std::size_t>(index_count)),
                    data, GL_STATIC_DRAW);
    }

    void PortableGLIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        if (index_count < 0)
            throw std::runtime_error("PortableGLIndexBufferRenderer::SetData32: negative index count");
        if (!thirtyTwoBit_)
            throw std::runtime_error(
                "PortableGLIndexBufferRenderer::SetData32: upload width does not match the declared 16-bit buffer");

        MakeCurrent(pglContext_);
        indexCount_ = index_count;
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
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error(
                "PortableGLTextureRenderer: a texture must have a positive width and height");

        MakeCurrent(pglContext_);
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // CNA's own default sampler for an unspecified SpriteBatch batch is SamplerState::LinearClamp
        // (SpriteBatch::Begin resolves a null samplerState to it), so that is what a freshly created
        // texture starts at. Every SpriteBatch draw re-applies the batch's own resolved SamplerState
        // through ApplySamplerState() below, so this only describes a texture nothing has sampled yet.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // PortableGL's glTexImage2D mallocs (does not zero) its storage and only fills it when a
        // non-null pointer is supplied, so a texture created without pixels would otherwise expose
        // uninitialized memory to the first sampler. Uploading an explicit transparent-black image
        // keeps a not-yet-populated texture deterministic.
        if (data.pixels.size() >= static_cast<std::size_t>(width_) *
                                  static_cast<std::size_t>(height_) * 4u)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        data.pixels.data());
        }
        else
        {
            const std::vector<std::uint8_t> blank(
                static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u, 0u);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        blank.data());
        }
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

    void PortableGLTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        // Without this override the interface default is a no-op, so every Texture2D::SetData()
        // (which is how a game changes a texture's level-0 pixels after construction) was accepted
        // and discarded, leaving PortableGL sampling the construction-time image forever.
        if (rgba == nullptr)
            throw std::runtime_error("PortableGLTextureRenderer::UpdatePixels: null pixel data");
        if (stride != width_ * 4)
            throw std::runtime_error(
                "PortableGLTextureRenderer::UpdatePixels: expected a tightly packed RGBA8 row pitch");

        MakeCurrent(pglContext_);
        glBindTexture(GL_TEXTURE_2D, glTexture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    void PortableGLTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                      int levelW, int levelH)
    {
        // See the declaration: level 0 is the only image a PortableGL texture has. Levels above it
        // are kept by Texture2D itself and are not renderer state here.
        if (level == 0)
            UpdatePixels(rgba, levelW * 4);
        (void)levelH;
    }

    void PortableGLTextureRenderer::ApplySamplerState(int filter, int addressU, int addressV) const
    {
        // Translate everything first: a refused ordinal must not leave the texture half-updated.
        const GLenum pglFilter = ToPglTextureFilter(filter);
        const GLenum pglWrapS = ToPglTextureAddress(addressU);
        const GLenum pglWrapT = ToPglTextureAddress(addressV);

        MakeCurrent(pglContext_);
        glBindTexture(GL_TEXTURE_2D, glTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, pglWrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, pglWrapT);
        // GL_TEXTURE_MAG_FILTER deliberately last: upstream's set_texparami() assigns BOTH
        // min_filter and mag_filter for the MAG pname (and only min_filter for MIN), so setting MAG
        // last is what leaves the pair consistent. Only filters whose min and mag components are
        // equal reach here (see ToPglTextureFilter), so one value is the whole truth.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pglFilter);
    }

    // =========================================================================================
    // PortableGLSpriteBatchRenderer
    // =========================================================================================

    PortableGLSpriteBatchRenderer::PortableGLSpriteBatchRenderer(PortableGLRenderer& owner) : owner_(owner) {}

    void PortableGLSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // SpriteBatch::Begin() forwards its `effect` argument here before anything is drawn, and
        // catches whatever this throws to leave the SpriteBatch reusable. Inheriting the
        // interface's no-op default would have meant a batch that asked for a custom Effect drew
        // silently with the built-in sprite shader instead -- the exact "accepted and ignored"
        // shape SupportsCapability(CustomEffects) == false promises does not happen.
        if (effect != nullptr)
            Unsupported(
                "custom SpriteBatch Effects are not supported -- PortableGL executes C function "
                "pointers as its shader stage and has no compiler a CNA Effect could be built "
                "into, which is why SupportsCapability(CustomEffects) reports false.");
    }

    void PortableGLSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Validate now, at Begin() time, rather than at the first Draw(): SpriteBatch::Begin()
        // treats a throw here as "this batch never began".
        (void)ToPglTextureFilter(textureFilter);
        samplerFilter_ = textureFilter;
    }

    void PortableGLSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        (void)ToPglTextureAddress(addressU);
        (void)ToPglTextureAddress(addressV);
        samplerAddressU_ = addressU;
        samplerAddressV_ = addressV;
    }

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

        // The batch's own resolved SamplerState (SpriteBatch::Begin resolves a null one to
        // SamplerState::LinearClamp) reaches the texture that is about to be sampled, not just a
        // cached CNA object. PortableGL keeps filter/wrap on the texture object itself, so this is
        // where a batch's sampler actually becomes real.
        texture.ApplySamplerState(samplerFilter_, samplerAddressU_, samplerAddressV_);

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

        /// pglSetUniform() stores the POINTER it is handed, so the uniform blocks must outlive the
        /// draw that installs them. Owning them here (rather than on the caller's stack) means the
        /// current program never refers to a dead frame between draws.
        ColoredUniforms coloredUniforms{};
        TexturedUniforms texturedUniforms{};

        bool depthTestEnabled = false;
        bool blendEnabled = false;

        /// Target-0 BlendState.ColorWriteChannels, kept so a colour clear can neutralize it and
        /// then restore it (XNA's Clear ignores the write mask; PortableGL's glClear honours it).
        int colorWriteChannels = 15;

        /// The stencil half of the active DepthStencilState, kept so SetReferenceStencil() can
        /// re-issue the comparison with only the reference value changed -- GraphicsDevice.
        /// ReferenceStencil is an independent device property that must take effect on its own.
        bool stencilEnable = false;
        int stencilFunc = 0;          // CompareFunction::Always
        int ccwStencilFunc = 0;
        int stencilMask = 0x7FFFFFFF;
        int stencilWriteMask = 0x7FFFFFFF;
        int referenceStencil = 0;
        bool twoSidedStencilMode = false;

        /// The active RasterizerState/ScissorRectangle, kept because PortableGL's own glScissor()
        /// narrows the rasterizer bounds whether or not the scissor test is enabled (see
        /// ApplyScissorState), so the rectangle may only be handed over while the test is on.
        bool scissorTestEnable = false;
        int scissorX = 0;
        int scissorY = 0;
        int scissorW = 0;
        int scissorH = 0;
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
        // Stated explicitly rather than inherited from PortableGL's default: every winding decision
        // this renderer makes (culling, and which half of a two-sided DepthStencilState applies to
        // which face) is expressed against "counter-clockwise in window coordinates is the front
        // face", which is the same convention EasyGL/OpenGL4/Magnum use.
        glFrontFace(GL_CCW);
        // BlendState's real factors/equations arrive through ApplyBlendState; this is only the
        // state before GraphicsDevice has pushed one, and it matches BlendState's own default
        // (Opaque: One/Zero, Add) so the two cannot disagree at frame zero.
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

        impl_->scissorW = virtualWidth;
        impl_->scissorH = virtualHeight;

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
        // XNA's Clear ignores BlendState.ColorWriteChannels while PortableGL's glClear honours the
        // colour mask, so the mask is neutralized across the clear and restored afterwards --
        // exactly what EasyGL does for the same reason.
        const bool maskActive = impl_->colorWriteChannels != 15;
        if (maskActive) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT);
        if (maskActive)
            glColorMask(ColorWriteHasRed(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasGreen(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasBlue(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasAlpha(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE);
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
        // The scissor rectangle is expressed in XNA's top-left space, so its PortableGL Y position
        // depends on the framebuffer height that just changed. GraphicsDevice resets the public
        // rectangle to the new full target right after this, but re-deriving it here keeps the
        // native state consistent even for a caller that drives the renderer directly.
        impl_->scissorX = 0;
        impl_->scissorY = 0;
        impl_->scissorW = width;
        impl_->scissorH = height;
        ApplyScissorState();
    }

    void PortableGLRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w < 0 || h < 0)
            throw std::runtime_error("PortableGLRenderer::ReadBackbuffer: negative width/height");

        MakeCurrent(&impl_->context);
        const int fbW = virtualWidth_;
        const int fbH = virtualHeight_;
        const auto* buf = reinterpret_cast<const std::uint8_t*>(impl_->backbuffer);

        // PortableGL keeps OpenGL's bottom-left window-coordinate origin in its API but stores the
        // resulting image TOP row first, so that its back buffer can be handed straight to a
        // windowing system: every fragment write is
        // `((pix_t*)back_buffer.lastrow)[-y*w + x]`, with `lastrow = buf + (h-1)*w*sizeof(pix_t)`,
        // which puts GL y=0 (the bottom of the viewport) in the LAST row of memory and GL y=h-1
        // (the top) in the first. CNA's public ReadBackbuffer contract is also top-row-first, so
        // the two agree and no vertical flip belongs here -- an earlier flip in this method
        // silently mirrored every non-symmetric frame, which only the orientation-sensitive
        // pixel oracles in the interleave/edge-case tests could see.
        for (int row = 0; row < h; ++row)
        {
            const int pglRow = y + row;
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

    int PortableGLRenderer::GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const
    {
        return 0;  // SurfaceFormat::Color -- PortableGL's fixed 32-bit pixel layout
    }

    int PortableGLRenderer::GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const
    {
        return 3;  // DepthFormat::Depth24Stencil8 -- PortableGL's unconditional PGL_D24S8 buffer
    }

    std::unique_ptr<ITextureRenderer> PortableGLRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<PortableGLTextureRenderer>(&impl_->context, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> PortableGLRenderer::CreateSpriteBatch()
    {
        return std::make_unique<PortableGLSpriteBatchRenderer>(*this);
    }

    void PortableGLRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* /*renderTargets*/,
                                              int count)
    {
        if (count > 0)
            Unsupported(
                "render targets are not supported -- a PortableGL context owns exactly one "
                "framebuffer, and CreateRenderTarget2D()/CreateRenderTargetCube() create none, "
                "which is why SupportsCapability(MultipleRenderTargets) reports false. A binding "
                "is refused rather than accepted and silently drawn into the back buffer.");
        // count == 0 restores the default back buffer, which is the only target this renderer has.
    }

    void PortableGLRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt != nullptr)
            Unsupported("render targets are not supported (see SetRenderTargets).");
    }

    void PortableGLRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int /*face*/)
    {
        if (rt != nullptr)
            Unsupported("render-target cube faces are not supported (see SetRenderTargets).");
    }

    void PortableGLRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        // XNA's Clear ignores DepthStencilState.DepthBufferWriteEnable and
        // BlendState.ColorWriteChannels; PortableGL's glClear honours glDepthMask and the colour
        // mask. Both are neutralized here and restored afterwards, exactly as EasyGL does.
        const bool maskActive = impl_->colorWriteChannels != 15;
        if (maskActive) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (maskActive)
            glColorMask(ColorWriteHasRed(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasGreen(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasBlue(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasAlpha(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE);
    }

    void PortableGLRenderer::ClearDepth(float depth)
    {
        MakeCurrent(&impl_->context);
        glClearDepthf(depth);
        glDepthMask(GL_TRUE);
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
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void PortableGLRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        const bool maskActive = impl_->colorWriteChannels != 15;
        if (maskActive) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (maskActive)
            glColorMask(ColorWriteHasRed(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasGreen(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasBlue(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasAlpha(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE);
    }

    void PortableGLRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        MakeCurrent(&impl_->context);
        glClearColor(r, g, b, a);
        glClearDepthf(depth);
        glClearStencil(stencil);
        const bool maskActive = impl_->colorWriteChannels != 15;
        if (maskActive) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (maskActive)
            glColorMask(ColorWriteHasRed(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasGreen(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasBlue(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                        ColorWriteHasAlpha(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE);
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

    void PortableGLRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc,
                                             const BlendWriteState& writeState)
    {
        // Translate everything before touching PortableGL, so a refused ordinal cannot leave the
        // output merger half-updated.
        const GLenum srcRGB = ToPglBlendFactor(colorSrcBlend);
        const GLenum dstRGB = ToPglBlendFactor(colorDstBlend);
        const GLenum srcA   = ToPglBlendFactor(alphaSrcBlend);
        const GLenum dstA   = ToPglBlendFactor(alphaDstBlend);
        const GLenum eqRGB  = ToPglBlendEquation(colorBlendFunc);
        const GLenum eqA    = ToPglBlendEquation(alphaBlendFunc);

        MakeCurrent(&impl_->context);

        // BlendState::Opaque is (One, Zero) on both channels with Add -- arithmetically identical
        // to blending being off, and the fast path every other CNA renderer selects for it.
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        impl_->blendEnabled = blendEnabled;
        if (blendEnabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        // Installed unconditionally: the factors/equations must describe the requested state even
        // when the fast path above disabled blending, so a later SetBlendEnabled(true) cannot
        // resurrect a stale function.
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
        glBlendEquationSeparate(eqRGB, eqA);

        // Only slot 0's mask can mean anything here: PortableGL owns a single colour attachment,
        // SetRenderTargets() refuses every non-empty binding and
        // SupportsCapability(MultipleRenderTargets) reports false, so slots 1..3 describe
        // attachments that can never exist on this renderer. They are inert rather than dropped --
        // there is no state they could control.
        impl_->colorWriteChannels = writeState.colorWriteChannels[0];
        glColorMask(ColorWriteHasRed(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                    ColorWriteHasGreen(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                    ColorWriteHasBlue(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE,
                    ColorWriteHasAlpha(impl_->colorWriteChannels) ? GL_TRUE : GL_FALSE);

        // BlendState.MultiSampleMask is a coverage mask over MSAA samples. PortableGL rasterizes
        // exactly one sample per pixel and SupportsCapability(MultiSampleAntiAliasing) reports
        // false, so there is no coverage to mask; the value reaches the renderer and only the rare
        // non-default path is unimplemented. This is the same documented gap EasyGL records for
        // the same field, not a silent drop.
        (void)writeState.multiSampleMask;
    }

    void PortableGLRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        MakeCurrent(&impl_->context);
        glBlendColor(r, g, b, a);
    }

    void PortableGLRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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
        // Validate before touching any PGL state, so a rejected ordinal cannot leave the context
        // half-updated -- the same order SoftwareRenderer::ApplyDepthStencilState validates in.
        const GLenum pglDepthFunc = ToPglCompareFunc(depthFunc, "depth");
        const GLenum pglStencilFunc = ToPglCompareFunc(stencilFunc, "stencil");
        const GLenum pglStencilFail = ToPglStencilOp(stencilFail);
        const GLenum pglStencilDepthFail = ToPglStencilOp(stencilDepthFail);
        const GLenum pglStencilPass = ToPglStencilOp(stencilPass);
        // XNA's TwoSidedStencilMode=false leaves the CounterClockwise* fields simply unused (FNA's
        // own behaviour), so they are neither validated nor installed then.
        const GLenum pglCcwFunc = twoSidedStencilMode
            ? ToPglCompareFunc(ccwStencilFunc, "counter-clockwise stencil") : pglStencilFunc;
        const GLenum pglCcwFail = twoSidedStencilMode ? ToPglStencilOp(ccwStencilFail) : pglStencilFail;
        const GLenum pglCcwDepthFail = twoSidedStencilMode
            ? ToPglStencilOp(ccwStencilDepthFail) : pglStencilDepthFail;
        const GLenum pglCcwPass = twoSidedStencilMode ? ToPglStencilOp(ccwStencilPass) : pglStencilPass;

        MakeCurrent(&impl_->context);
        impl_->depthTestEnabled = depthEnable;
        if (depthEnable) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(depthWriteEnable ? GL_TRUE : GL_FALSE);
        glDepthFunc(pglDepthFunc);

        impl_->stencilEnable = stencilEnable;
        impl_->stencilFunc = stencilFunc;
        impl_->ccwStencilFunc = twoSidedStencilMode ? ccwStencilFunc : stencilFunc;
        impl_->stencilMask = stencilMask;
        impl_->stencilWriteMask = stencilWriteMask;
        impl_->referenceStencil = referenceStencil;
        impl_->twoSidedStencilMode = twoSidedStencilMode;

        if (stencilEnable) glEnable(GL_STENCIL_TEST);
        else glDisable(GL_STENCIL_TEST);

        // Face assignment: PortableGL classifies a triangle as front facing when its window-space
        // winding is counter-clockwise (is_front_facing(), with the glFrontFace(GL_CCW) this
        // renderer states in its constructor), and XNA's CounterClockwise* half of a
        // DepthStencilState describes exactly the counter-clockwise-on-screen faces. So the CCW
        // half installs on PGL's FRONT and the primary (clockwise) half on PGL's BACK. This is the
        // same orientation VulkanRenderer's own differential two-sided-stencil finding settled on,
        // and PortableGL_Stencil's two-sided check is the differential test that pins it here.
        const GLint ref = std::clamp(referenceStencil, 0, 255);
        const auto mask = static_cast<GLuint>(stencilMask);
        const auto writeMask = static_cast<GLuint>(stencilWriteMask);
        glStencilFuncSeparate(GL_BACK, pglStencilFunc, ref, mask);
        glStencilOpSeparate(GL_BACK, pglStencilFail, pglStencilDepthFail, pglStencilPass);
        glStencilMaskSeparate(GL_BACK, writeMask);
        glStencilFuncSeparate(GL_FRONT, pglCcwFunc, ref, mask);
        glStencilOpSeparate(GL_FRONT, pglCcwFail, pglCcwDepthFail, pglCcwPass);
        glStencilMaskSeparate(GL_FRONT, writeMask);
    }

    void PortableGLRenderer::SetReferenceStencil(int value)
    {
        // GraphicsDevice.ReferenceStencil is an independent device property that must take effect
        // without a full DepthStencilState re-application, which is why the comparison half of the
        // active state is cached above.
        const GLenum pglStencilFunc = ToPglCompareFunc(impl_->stencilFunc, "stencil");
        const GLenum pglCcwFunc = ToPglCompareFunc(impl_->ccwStencilFunc, "counter-clockwise stencil");

        MakeCurrent(&impl_->context);
        impl_->referenceStencil = value;
        // Upstream's glStencilFunc*() computes clampi(ref, 0, 255) but discards the result, so the
        // clamp real OpenGL performs on the reference value is done here instead.
        const GLint ref = std::clamp(value, 0, 255);
        const auto mask = static_cast<GLuint>(impl_->stencilMask);
        glStencilFuncSeparate(GL_BACK, pglStencilFunc, ref, mask);
        glStencilFuncSeparate(GL_FRONT, pglCcwFunc, ref, mask);
    }

    void PortableGLRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                  bool scissorTestEnable,
                                                  float depthBias,
                                                  float slopeScaleDepthBias)
    {
        // Explicit mapping, never an ordinal coincidence. XNA CullMode: None=0,
        // CullClockwiseFace=1, CullCounterClockwiseFace=2. With glFrontFace(GL_CCW) a
        // counter-clockwise-on-screen triangle is PortableGL's FRONT face, so culling the
        // clockwise faces means culling GL_BACK and vice versa -- the identical table EasyGL,
        // OpenGL4 and Magnum use.
        GLenum cullFace = GL_BACK;
        bool cullEnabled = true;
        switch (cullMode)
        {
        case 0: cullEnabled = false; break;
        case 1: cullFace = GL_BACK;  break;
        case 2: cullFace = GL_FRONT; break;
        default:
            Unsupported("unsupported CullMode ordinal " + std::to_string(cullMode));
        }

        GLenum polygonMode = GL_FILL;
        switch (fillMode)
        {
        case 0: polygonMode = GL_FILL; break;
        case 1: polygonMode = GL_LINE; break;   // FillMode::WireFrame
        default:
            Unsupported("unsupported FillMode ordinal " + std::to_string(fillMode));
        }

        MakeCurrent(&impl_->context);
        if (cullEnabled)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cullFace);
        }
        else
        {
            glDisable(GL_CULL_FACE);
        }

        glPolygonMode(GL_FRONT_AND_BACK, polygonMode);

        // Polygon offset is enabled only for a genuinely non-zero bias. Beyond avoiding pointless
        // work, upstream's calc_poly_offset() divides by the per-edge dx/dy of the first edge
        // without guarding a zero denominator, so a triangle whose first edge is axis-aligned AND
        // depth-constant yields a NaN depth slope. Leaving the offset disabled for the (dominant)
        // zero-bias case keeps that function out of the pipeline entirely.
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            glPolygonOffset(slopeScaleDepthBias, depthBias);
            glEnable(GL_POLYGON_OFFSET_FILL);
        }
        else
        {
            glPolygonOffset(0.0f, 0.0f);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        impl_->scissorTestEnable = scissorTestEnable;
        ApplyScissorState();
    }

    void PortableGLRenderer::ApplyScissorState()
    {
        // PortableGL folds the scissor rectangle into the "always on" rasterizer clip bounds
        // (c->lx/ly/ux/uy) the instant glScissor() is called, whether or not GL_SCISSOR_TEST is
        // enabled -- and glClear() with the test disabled clears `ux * uy` pixels linearly from the
        // start of the buffer. Handing over a sub-rectangle while the test is off would therefore
        // clip drawing and corrupt clears. So the rectangle only ever reaches PortableGL while the
        // test is enabled; disabling the test is what restores full-framebuffer bounds.
        if (!impl_->scissorTestEnable)
        {
            glDisable(GL_SCISSOR_TEST);
            return;
        }
        // XNA's rectangle is top-left based; PortableGL's window origin is bottom-left.
        const int w = std::max(0, impl_->scissorW);
        const int h = std::max(0, impl_->scissorH);
        const int flippedY = virtualHeight_ - impl_->scissorY - h;
        glScissor(impl_->scissorX, flippedY, w, h);
        glEnable(GL_SCISSOR_TEST);
    }

    void PortableGLRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        MakeCurrent(&impl_->context);
        impl_->scissorX = x;
        impl_->scissorY = y;
        impl_->scissorW = w;
        impl_->scissorH = h;
        ApplyScissorState();
    }

    void PortableGLRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0)
            return;  // invalid rect -- leave viewport state unchanged (EasyGL's own convention)
        MakeCurrent(&impl_->context);
        // XNA's viewport is top-left based; PortableGL's window origin is bottom-left.
        glViewport(x, virtualHeight_ - y - h, w, h);
        glDepthRangef(minDepth, maxDepth);
    }

    void PortableGLRenderer::ApplySamplerState(int /*slot*/, int /*filter*/,
                                               int /*addressU*/, int /*addressV*/,
                                               int /*maxAnisotropy*/)
    {
        // Deliberately inert -- see the declaration's documentation. No PortableGL draw path can
        // sample a texture through a GraphicsDevice sampler slot: the 3D route refuses every
        // textured effect configuration, and SpriteBatch carries its own resolved SamplerState
        // through SetSamplerFilter()/SetSamplerAddressMode(), which this renderer implements for
        // real. GraphicsDevice pushes all 16 slots before every single draw, so throwing here would
        // reject ordinary drawing rather than an unsupported feature.
    }

    void PortableGLRenderer::ApplySamplerMipState(int /*slot*/, int /*maxMipLevel*/, float /*lodBias*/)
    {
        // Deliberately inert -- see the declaration's documentation. PortableGL textures carry
        // exactly one level, so there is no mip selection for these controls to influence.
    }

    std::unique_ptr<IVertexBufferRenderer> PortableGLRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<PortableGLVertexBufferRenderer>(&impl_->context, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> PortableGLRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<PortableGLIndexBufferRenderer>(
            &impl_->context, index_capacity, /*thirtyTwoBit=*/false);
    }

    std::unique_ptr<IIndexBufferRenderer> PortableGLRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<PortableGLIndexBufferRenderer>(
            &impl_->context, index_capacity, /*thirtyTwoBit=*/true);
    }

    void PortableGLRenderer::DrawColoredCommon(const IVertexBufferRenderer& vbIn,
                                               const Matrix& world,
                                               const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive,
                                               int primitiveCount,
                                               const ColoredDrawState& state,
                                               const char* route)
    {
        const auto& vb = static_cast<const PortableGLVertexBufferRenderer&>(vbIn);
        if (vb.StrideInBytes() != 16)
            Unsupported(
                std::string("the ") + route + " route supports only the 16-byte "
                "VertexPositionColor layout; the bound vertex buffer's stride is " +
                std::to_string(vb.StrideInBytes()) + " bytes.");

        // REMED-GFX-DECL-GUARD: a stride does not determine element composition. A declaration
        // that puts something else in the same 16 bytes is refused here, before any native draw,
        // rather than reinterpreted as Position0@0 Vector3 + Color0@12 Color.
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            vb.DeclaredLayout(), 16,
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            kRendererName, route);

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const long long firstElement =
            static_cast<long long>(state.streamVertexOffset) + static_cast<long long>(state.vertexStart);
        if (firstElement < 0 ||
            firstElement + vertexCount > static_cast<long long>(vb.GetVertexCount()))
            throw std::runtime_error(
                std::string("PortableGLRenderer: the ") + route +
                " draw reads vertices [" + std::to_string(firstElement) + ", " +
                std::to_string(firstElement + vertexCount) + ") but the bound vertex buffer holds " +
                std::to_string(vb.GetVertexCount()) + ".");

        MakeCurrent(&impl_->context);

        const Matrix combined = world * view * projection;
        combined.ToColumnMajor(impl_->coloredUniforms.mvp);
        impl_->coloredUniforms.diffuse[0] = state.diffuse[0];
        impl_->coloredUniforms.diffuse[1] = state.diffuse[1];
        impl_->coloredUniforms.diffuse[2] = state.diffuse[2];
        impl_->coloredUniforms.diffuse[3] = state.diffuse[3];
        impl_->coloredUniforms.vertexColorEnabled = state.vertexColorEnabled ? 1.0f : 0.0f;

        glUseProgram(impl_->coloredProgram);
        pglSetUniform(&impl_->coloredUniforms);

        // The stream's own VertexBufferBinding.VertexOffset shifts the attribute base; the
        // start-vertex term rides in glDrawArrays' own `first`, which PortableGL adds to the
        // attribute index it fetches (offset + stride * i). GraphicsDevice folds a single stream's
        // whole offset into vertexStart, so for an ordinary draw this base is zero -- but reading
        // both means the renderer never silently drops one of them.
        const std::ptrdiff_t attribBase = AttributeBaseBytes(state.streamVertexOffset, 16u);
        glBindBuffer(GL_ARRAY_BUFFER, vb.GLBufferHandle());
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16,
                              reinterpret_cast<const GLvoid*>(attribBase));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16,
                              reinterpret_cast<const GLvoid*>(attribBase + 12));
        SelectVertexAttribArrays(2);

        glDrawArrays(ToPglMode(primitive), static_cast<GLint>(state.vertexStart), vertexCount);
    }

    void PortableGLRenderer::DrawIndexedColoredCommon(const IVertexBufferRenderer& vbIn,
                                                      const IIndexBufferRenderer& ibIn,
                                                      const Matrix& world,
                                                      const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive,
                                                      int primitiveCount,
                                                      const ColoredDrawState& state,
                                                      const char* route)
    {
        const auto& vb = static_cast<const PortableGLVertexBufferRenderer&>(vbIn);
        const auto& ib = static_cast<const PortableGLIndexBufferRenderer&>(ibIn);
        if (vb.StrideInBytes() != 16)
            Unsupported(
                std::string("the ") + route + " route supports only the 16-byte "
                "VertexPositionColor layout; the bound vertex buffer's stride is " +
                std::to_string(vb.StrideInBytes()) + " bytes.");

        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            vb.DeclaredLayout(), 16,
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            kRendererName, route);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (state.startIndex < 0 ||
            static_cast<long long>(state.startIndex) + indexCount >
                static_cast<long long>(ib.GetIndexCount()))
            throw std::runtime_error(
                std::string("PortableGLRenderer: the ") + route + " draw reads indices [" +
                std::to_string(state.startIndex) + ", " +
                std::to_string(static_cast<long long>(state.startIndex) + indexCount) +
                ") but the bound index buffer holds " + std::to_string(ib.GetIndexCount()) + ".");

        // PortableGL 0.100.0 exposes no glDrawElementsBaseVertex, and baseVertex must not be
        // ignored: it is added to every decoded index before the vertex fetch. PortableGL fetches
        // attribute i from `buffer + offset + stride * i`, so advancing `offset` by
        // baseVertex * stride is exactly the same arithmetic, with no index staging, no copy and
        // no change to the caller's index buffer. The binding's own VertexOffset advances it too,
        // for the same reason and in the same units.
        const long long baseElement =
            static_cast<long long>(state.streamVertexOffset) + static_cast<long long>(state.baseVertex);
        // The declared decoded-index window [minVertexIndex, +numVertices) is the range the caller
        // guarantees; check it against the buffer so an out-of-range base cannot make PortableGL
        // read outside the upload. numVertices == 0 marks the legacy route, which declares no
        // window -- the base itself is still required to address a real vertex there.
        const long long firstDeclared = baseElement + static_cast<long long>(state.minVertexIndex);
        const long long declaredCount =
            state.numVertices > 0 ? static_cast<long long>(state.numVertices) : 1;
        if (firstDeclared < 0 ||
            firstDeclared + declaredCount > static_cast<long long>(vb.GetVertexCount()))
            throw std::runtime_error(
                std::string("PortableGLRenderer: the ") + route +
                " draw declares vertices [" + std::to_string(firstDeclared) + ", " +
                std::to_string(firstDeclared + declaredCount) +
                ") but the bound vertex buffer holds " + std::to_string(vb.GetVertexCount()) + ".");

        MakeCurrent(&impl_->context);

        const Matrix combined = world * view * projection;
        combined.ToColumnMajor(impl_->coloredUniforms.mvp);
        impl_->coloredUniforms.diffuse[0] = state.diffuse[0];
        impl_->coloredUniforms.diffuse[1] = state.diffuse[1];
        impl_->coloredUniforms.diffuse[2] = state.diffuse[2];
        impl_->coloredUniforms.diffuse[3] = state.diffuse[3];
        impl_->coloredUniforms.vertexColorEnabled = state.vertexColorEnabled ? 1.0f : 0.0f;

        glUseProgram(impl_->coloredProgram);
        pglSetUniform(&impl_->coloredUniforms);

        const std::ptrdiff_t attribBase = AttributeBaseBytes(baseElement, 16u);
        glBindBuffer(GL_ARRAY_BUFFER, vb.GLBufferHandle());
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16,
                              reinterpret_cast<const GLvoid*>(attribBase));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16,
                              reinterpret_cast<const GLvoid*>(attribBase + 12));
        SelectVertexAttribArrays(2);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.GLBufferHandle());
        const GLenum indexType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t indexSize = ib.IsThirtyTwoBit() ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        // startIndex selects an index ELEMENT; glDrawElements takes a BYTE offset into the bound
        // element array buffer (PortableGL adds it to the buffer's data pointer verbatim), so the
        // element count is converted with this buffer's own index width.
        const auto indexByteOffset =
            static_cast<std::ptrdiff_t>(static_cast<std::size_t>(state.startIndex) * indexSize);
        glDrawElements(ToPglMode(primitive), indexCount, indexType,
                       reinterpret_cast<const GLvoid*>(indexByteOffset));
    }

    void PortableGLRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                   const Matrix& world,
                                                   const Matrix& view,
                                                   const Matrix& projection,
                                                   PrimitiveType primitive,
                                                   int primitiveCount)
    {
        // The interface documents this legacy route as "equivalent to BasicEffect with
        // VertexColorEnabled = true", which is exactly ColoredDrawState's default.
        DrawColoredCommon(vb, world, view, projection, primitive, primitiveCount,
                          ColoredDrawState{}, "legacy-colored");
    }

    void PortableGLRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                          const IIndexBufferRenderer& ib,
                                                          const Matrix& world,
                                                          const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          int primitiveCount)
    {
        DrawIndexedColoredCommon(vb, ib, world, view, projection, primitive, primitiveCount,
                                 ColoredDrawState{}, "legacy-indexed-colored");
    }

    void PortableGLRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                              const Matrix& world,
                                              const Matrix& view,
                                              const Matrix& projection,
                                              PrimitiveType primitive,
                                              int primitiveCount,
                                              const GpuDrawParams& params)
    {
        // Overriding this is what makes GraphicsDevice::DrawPrimitives' own parameters real. The
        // interface's default forwards to DrawColoredPrimitives, which has no channel for
        // vertexStart, the binding's VertexOffset, or any effect state -- so every one of them was
        // silently discarded and every draw started at vertex zero with vertex colours forced on.
        DrawColoredCommon(vb, world, view, projection, primitive, primitiveCount,
                          TranslateDrawParams(params, "ordinary"), "ordinary");
    }

    void PortableGLRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                     const IIndexBufferRenderer& ib,
                                                     const Matrix& world,
                                                     const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive,
                                                     int primitiveCount,
                                                     const GpuDrawParams& params)
    {
        DrawIndexedColoredCommon(vb, ib, world, view, projection, primitive, primitiveCount,
                                 TranslateDrawParams(params, "ordinary-indexed"), "ordinary-indexed");
    }

    bool PortableGLRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        // Each of these has a real implementation, a public CNA path that reaches it, and a
        // permanent PortableGL regression test that fails if the implementation is removed.
        case CNA::GraphicsCapability::ThreeD:               // DrawPrimitivesEx/DrawIndexedPrimitivesEx
        case CNA::GraphicsCapability::DepthStencilBuffer:   // PGL_D24S8, ApplyDepthStencilState
        case CNA::GraphicsCapability::StencilBuffer:        // the stencil half of ApplyDepthStencilState
        case CNA::GraphicsCapability::AdditiveBlending:     // ApplyBlendState's full factor/equation map
        case CNA::GraphicsCapability::WireFrame:            // ApplyRasterizerState -> glPolygonMode(GL_LINE)
            return true;
        // Not implemented, and reported as such rather than silently no-opped:
        //  - MultiSampleAntiAliasing: PortableGL rasterizes one sample per pixel.
        //  - MultipleRenderTargets / Texture3D: CreateRenderTarget2D()/CreateRenderTargetCube()/
        //    CreateTexture3D() keep the interface's nullptr defaults, and SetRenderTargets()
        //    refuses a non-empty binding outright.
        //  - OcclusionQuery: CreateOcclusionQuery() keeps the nullptr default.
        //  - AnisotropicFiltering: PortableGL has one filter per texture and no anisotropy;
        //    ToPglTextureFilter() refuses TextureFilter::Anisotropic.
        //  - CustomEffects: PortableGL's shader stage is a pair of C function pointers, so there is
        //    nothing to compile a CNA Effect into; CreateEffectRenderer() keeps the nullptr default
        //    and PortableGLSpriteBatchRenderer::SetCustomEffect() refuses a non-null Effect.
        //  - MultiStreamVertexInput / Instancing: the colored route binds exactly one stream and
        //    RejectUnsupportedStreamCombination()/TranslateDrawParams() refuse anything else.
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

        combined.ToColumnMajor(impl_->texturedUniforms.mvp);
        impl_->texturedUniforms.texture = glTexture;

        // Two triangles: (0,1,2) and (0,2,3), corners ordered TL, TR, BR, BL -- matching the
        // caller's own PortableGLSpriteBatchRenderer::Draw() corner layout exactly. In XNA's
        // top-left screen space that order is clockwise, which is the front-facing winding under
        // SpriteBatch's own default RasterizerState::CullCounterClockwise.
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
        pglSetUniform(&impl_->texturedUniforms);

        glBindBuffer(GL_ARRAY_BUFFER, impl_->quadBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        const GLsizei stride = 8 * sizeof(float);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(0));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(2 * sizeof(float)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(4 * sizeof(float)));
        SelectVertexAttribArrays(3);

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
