// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Sokol/SokolRenderer.hpp"

#include "CNA/Internal/Renderers/Common/NotYetImplemented.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "sokol_log.h"
#include "sokol_gfx.h"

#include "shaders/sokol_shaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_set>

// The GL back-buffer readback below calls glReadPixels directly: sokol_gfx has no readback API of
// its own, and on this project's target platform (SOKOL_GLCORE on Linux) sokol renders into the
// platform window's default framebuffer, which glReadPixels can read straight out of. Restricted to the
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

namespace CNA::Internal::Renderers::Sokol
{
    namespace
    {
        constexpr const char* kRendererName = "Sokol";

        CNA::Platform::GlContextDescription RequestedContext(const int multiSampleCount)
        {
            CNA::Platform::GlContextDescription description;
#if defined(SOKOL_GLCORE)
            // sokol_gfx's GLCORE backend assumes OpenGL 4.1 core features.
            description.majorVersion = 4;
            description.minorVersion = 1;
            description.profile = CNA::Platform::GlProfile::Core;
#elif defined(SOKOL_GLES3)
            description.majorVersion = 3;
            description.minorVersion = 0;
            description.profile = CNA::Platform::GlProfile::Es;
#else
            throw std::runtime_error(
                "SOKOL renderer currently implements only GLCORE/GLES3 platform contexts");
#endif
            description.depthBits = 24;
            description.stencilBits = 8;
            description.multisampleBuffers = multiSampleCount > 1 ? 1 : 0;
            description.multisampleSamples = multiSampleCount > 1 ? multiSampleCount : 0;
            description.doubleBuffer = true;
            return description;
        }

        /// Per-frame sprite capacity. 16384 quads is exactly 65536 vertices, the largest run a
        /// uint16 index buffer can address, so this is the natural ceiling for the shared quad
        /// index buffer rather than an arbitrary number.
        constexpr int kMaxSpriteQuads = 16384;
        constexpr int kSpriteVerticesPerQuad = 4;
        constexpr int kSpriteIndicesPerQuad = 6;

        // --- XNA ordinal -> sokol_gfx enum translations -----------------------------------
        //
        // Every raw int crossing IGraphicsRenderer is a Microsoft::Xna::Framework::Graphics enum
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

        /// plan_sokol.md SOKOL-44. `SokolEffectRenderer::BindTexture()`/`BindTextureCube()` only
        /// ever RECORD the `ITextureRenderer*`/`ITextureCubeRenderer*` they are given -- realized
        /// later, at draw time, by `ApplyPendingTextureBindsEXT()` -- but not every XNA-layer
        /// texture wrapper keeps that pointer valid for as long as the wrapper itself lives.
        /// `SokolTextureCubeRenderer`/`SokolRenderTargetRenderer`/`SokolRenderTargetCubeRenderer` all
        /// mutate the SAME C++ object in place on every upload (only the internal `sg_image`
        /// handle changes, e.g. `SokolTextureRenderer::RecreateImage()`'s own doc comment) -- but
        /// `Texture2D::SetData(const Color*, int)` (unlike its own sibling overloads) replaces
        /// `Texture2D::renderer_` with an ENTIRELY NEW renderer object, destroying the old one
        /// immediately. A pointer captured by `BindTexture()` before that call would dangle by the
        /// time `ApplyPendingTextureBindsEXT()` runs. This tiny registry -- populated by every
        /// constructor/destructor of the four renderer classes `ResolveSampledTextureImageId()`/
        /// `ResolveSampledCubeImageId()` accept -- lets that later dereference check the pointer is
        /// still a real, currently-alive object first, rather than assuming it. A pointer that is
        /// no longer live is silently skipped (matches the graceful "id 0, texture stays unbound"
        /// degradation this same call already has for other invalid inputs), not dereferenced.
        std::unordered_set<const void*>& LiveSampledRendererRegistryEXT()
        {
            static std::unordered_set<const void*> registry;
            return registry;
        }

        /// Packs GraphicsDevice.BlendFactor's four 0..1 floats (GraphicsDevice divides the public
        /// 8-bit Color by 255.0f before calling SetBlendFactor) back into 0xRRGGBBAA so the pipeline
        /// cache keys (plan_sokol.md SOKOL-40) can compare/hash it as a single exact integer instead
        /// of four floats that would need epsilon comparison for no real precision benefit.
        std::uint32_t PackBlendFactorEXT(const float factor[4])
        {
            const auto channel = [](float value) -> std::uint32_t {
                const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
            };
            return (channel(factor[0]) << 24) | (channel(factor[1]) << 16)
                 | (channel(factor[2]) << 8) | channel(factor[3]);
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

#if CNA_SOKOL_HAS_GL_READBACK
        /// Raw-GL topology for the custom-ShaderEffect draw path (plan_sokol.md SOKOL-28), which
        /// bypasses sg_pipeline entirely and so cannot use ToPrimitiveType()'s sg_primitive_type
        /// result -- the same PrimitiveType-to-topology mapping, just for glDrawArrays/glDrawElements
        /// instead of sg_draw().
        GLenum ToGLPrimitiveTopology(Microsoft::Xna::Framework::Graphics::PrimitiveType primitive)
        {
            using Microsoft::Xna::Framework::Graphics::PrimitiveType;
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return GL_TRIANGLES;
                case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
                case PrimitiveType::LineList:      return GL_LINES;
                case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
                case PrimitiveType::PointListEXT:  return GL_POINTS;
            }
            return GL_TRIANGLES;
        }

        /// Raw-GL depth-compare function for the custom-ShaderEffect draw path (plan_sokol.md
        /// SOKOL-26 MRT depth-ownership test), which bypasses sg_pipeline entirely and so cannot
        /// use ToCompareFunc()'s sg_compare_func result -- the same CompareFunction-ordinal mapping
        /// ToCompareFunc() uses, just for glDepthFunc() instead of sg_depth_state.compare.
        GLenum ToGLCompareFunc(int compareFunction)
        {
            switch (compareFunction)
            {
                case 0:  return GL_ALWAYS;   // CompareFunction::Always
                case 1:  return GL_NEVER;    // Never
                case 2:  return GL_LESS;     // Less
                case 3:  return GL_LEQUAL;   // LessEqual
                case 4:  return GL_EQUAL;    // Equal
                case 5:  return GL_GEQUAL;   // GreaterEqual
                case 6:  return GL_GREATER;  // Greater
                case 7:  return GL_NOTEQUAL; // NotEqual
                default: return GL_LEQUAL;
            }
        }

        /// Raw-GL blend factor for the custom-ShaderEffect draw paths (plan_sokol.md SOKOL-41),
        /// the same Blend-ordinal mapping ToBlendFactor() uses for sg_blend_factor, just for
        /// glBlendFuncSeparate(). GL has a single GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR
        /// pair (fed by glBlendColor()) where sokol_gfx's SG_BLENDFACTOR_BLEND_COLOR distinguishes
        /// nothing further either -- both APIs apply the same one constant to every channel.
        GLenum ToGLBlendFactor(int blend)
        {
            switch (blend)
            {
                case 0:  return GL_ONE;                       // Blend::One
                case 1:  return GL_ZERO;                       // Zero
                case 2:  return GL_SRC_COLOR;                  // SourceColor
                case 3:  return GL_ONE_MINUS_SRC_COLOR;        // InverseSourceColor
                case 4:  return GL_SRC_ALPHA;                  // SourceAlpha
                case 5:  return GL_ONE_MINUS_SRC_ALPHA;        // InverseSourceAlpha
                case 6:  return GL_DST_COLOR;                  // DestinationColor
                case 7:  return GL_ONE_MINUS_DST_COLOR;        // InverseDestinationColor
                case 8:  return GL_DST_ALPHA;                  // DestinationAlpha
                case 9:  return GL_ONE_MINUS_DST_ALPHA;        // InverseDestinationAlpha
                case 10: return GL_CONSTANT_COLOR;             // BlendFactor
                case 11: return GL_ONE_MINUS_CONSTANT_COLOR;   // InverseBlendFactor
                case 12: return GL_SRC_ALPHA_SATURATE;         // SourceAlphaSaturation
                default: return GL_ONE;
            }
        }

        /// Raw-GL blend equation, the same BlendFunction-ordinal mapping ToBlendOp() uses for
        /// sg_blend_op, just for glBlendEquationSeparate().
        GLenum ToGLBlendOp(int blendFunction)
        {
            switch (blendFunction)
            {
                case 0:  return GL_FUNC_ADD;              // BlendFunction::Add
                case 1:  return GL_FUNC_SUBTRACT;         // Subtract
                case 2:  return GL_FUNC_REVERSE_SUBTRACT; // ReverseSubtract
                case 3:  return GL_MAX;                   // Max
                case 4:  return GL_MIN;                   // Min
                default: return GL_FUNC_ADD;
            }
        }

        /// Raw-GL stencil op, the same StencilOperation-ordinal mapping ToStencilOp() uses for
        /// sg_stencil_op, just for glStencilOpSeparate(). Increment/Decrement (wrapping) map to
        /// GL_INCR_WRAP/GL_DECR_WRAP; IncrementSaturation/DecrementSaturation (clamping) map to
        /// plain GL_INCR/GL_DECR -- the same wrap-vs-clamp distinction sokol's own
        /// SG_STENCILOP_INCR_WRAP vs SG_STENCILOP_INCR_CLAMP makes.
        GLenum ToGLStencilOp(int stencilOperation)
        {
            switch (stencilOperation)
            {
                case 0:  return GL_KEEP;      // StencilOperation::Keep
                case 1:  return GL_ZERO;      // Zero
                case 2:  return GL_REPLACE;   // Replace
                case 3:  return GL_INCR_WRAP; // Increment
                case 4:  return GL_DECR_WRAP; // Decrement
                case 5:  return GL_INCR;      // IncrementSaturation
                case 6:  return GL_DECR;      // DecrementSaturation
                case 7:  return GL_INVERT;    // Invert
                default: return GL_KEEP;
            }
        }

        /// Raw-GL face-culling mode, the same CullMode-ordinal mapping this file's ToCullMode()
        /// uses for sg_cull_mode (see that function's own doc comment for the CW-winding
        /// reasoning), just for glEnable(GL_CULL_FACE)/glCullFace(). CullMode::None (0) has no GL
        /// enum of its own -- callers must glDisable(GL_CULL_FACE) instead of calling this for
        /// that case, matching Get3DPipeline's own desc.cull_mode branch shape. Every caller must
        /// also glFrontFace(GL_CW) -- this renderer's sg_pipeline_desc.face_winding is fixed at
        /// SG_FACEWINDING_CW everywhere, and GL's own default front face is CCW, so leaving it
        /// unset would cull the WRONG faces the first time this raw-GL path runs before any
        /// sg_pipeline draw has incidentally already left glFrontFace(GL_CW) behind.
        GLenum ToGLCullFace(int cullMode)
        {
            return cullMode == 1 ? GL_FRONT : GL_BACK;  // 1=CullClockwiseFace, 2=CullCounterClockwiseFace
        }

        /// The raw-GL attribute shape needed to bind one VertexElement for the custom-ShaderEffect
        /// draw path: component count, GL scalar type, whether values are normalized to [0,1]/
        /// [-1,1], and whether the attribute must be read as a true integer
        /// (glVertexAttribIPointer) rather than converted to float (glVertexAttribPointer). Mirrors
        /// EasyGLRenderer::DescribeVertexElementFormat()'s identical table -- Byte4 is the
        /// one format needing the integer path (XNA's own BLENDINDICES-style format, matching this
        /// renderer's own skinned3d.glsl blendindices0 finding: SOKOL-35's `in uvec4` discovery).
        struct CustomEffectAttribFormat
        {
            int componentCount;
            GLenum type;
            bool normalized;
            bool isInteger;
        };

        CustomEffectAttribFormat DescribeCustomEffectVertexFormat(
            Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            switch (format)
            {
                case VertexElementFormat::Single:           return {1, GL_FLOAT,         false, false};
                case VertexElementFormat::Vector2:          return {2, GL_FLOAT,         false, false};
                case VertexElementFormat::Vector3:          return {3, GL_FLOAT,         false, false};
                case VertexElementFormat::Vector4:          return {4, GL_FLOAT,         false, false};
                case VertexElementFormat::Color:            return {4, GL_UNSIGNED_BYTE, true,  false};
                case VertexElementFormat::Byte4:            return {4, GL_UNSIGNED_BYTE, false, true};
                case VertexElementFormat::Short2:           return {2, GL_SHORT,         false, false};
                case VertexElementFormat::Short4:           return {4, GL_SHORT,         false, false};
                case VertexElementFormat::NormalizedShort2: return {2, GL_SHORT,         true,  false};
                case VertexElementFormat::NormalizedShort4: return {4, GL_SHORT,         true,  false};
                case VertexElementFormat::HalfVector2:      return {2, GL_HALF_FLOAT,    false, false};
                case VertexElementFormat::HalfVector4:      return {4, GL_HALF_FLOAT,    false, false};
            }
            return {3, GL_FLOAT, false, false};
        }

        /// One resolved vertex attribute for the custom-ShaderEffect draw path: byte offset plus
        /// the GL shape DescribeCustomEffectVertexFormat() already computes.
        struct CustomEffectAttribLayout
        {
            int offset;
            CustomEffectAttribFormat desc;
        };

        /// Resolves the full ordered attribute layout for a custom-ShaderEffect 3D draw
        /// (plan_sokol.md SOKOL-28): from the real VertexDeclaration when one was supplied, else
        /// the same fixed set of recognised undeclared-VertexBuffer byte strides
        /// EasyGLRenderer::ApplyLayout()'s own fallback switch uses (in the same field
        /// order) -- VertexBuffer's own CNAEXT-tagged auto-detect constructor stores a
        /// default-constructed, ELEMENT-LESS VertexDeclaration (see DrawColored3D's own identical
        /// comment on this), which is what modules/renderers/easygl/examples/easygl_shadereffect_3d_test.cpp's
        /// VertexPositionNormalTexture buffer actually has. Returns empty for neither case, so the
        /// caller can throw a clear error rather than binding a garbage layout.
        std::vector<CustomEffectAttribLayout> BuildCustomEffectAttribLayoutEXT(
            const VertexDeclaration* declaration, int fallbackStride)
        {
            std::vector<CustomEffectAttribLayout> result;
            if (declaration != nullptr && !declaration->GetVertexElements().empty())
            {
                for (const auto& element : declaration->GetVertexElements())
                {
                    result.push_back({element.getOffsetProperty(),
                                      DescribeCustomEffectVertexFormat(
                                          element.getVertexElementFormatProperty())});
                }
                return result;
            }

            switch (fallbackStride)
            {
                case 16: // VertexPositionColor: float3 position, ubyte4n color
                    result.push_back({0,  {3, GL_FLOAT, false, false}});
                    result.push_back({12, {4, GL_UNSIGNED_BYTE, true, false}});
                    break;
                case 20: // VertexPositionTexture: float3 position, float2 texcoord
                    result.push_back({0,  {3, GL_FLOAT, false, false}});
                    result.push_back({12, {2, GL_FLOAT, false, false}});
                    break;
                case 24: // VertexPositionColorTexture: float3 position, ubyte4n color, float2 texcoord
                    result.push_back({0,  {3, GL_FLOAT, false, false}});
                    result.push_back({12, {4, GL_UNSIGNED_BYTE, true, false}});
                    result.push_back({16, {2, GL_FLOAT, false, false}});
                    break;
                case 32: // VertexPositionNormalTexture: float3 position, float3 normal, float2 texcoord
                    result.push_back({0,  {3, GL_FLOAT, false, false}});
                    result.push_back({12, {3, GL_FLOAT, false, false}});
                    result.push_back({24, {2, GL_FLOAT, false, false}});
                    break;
                case 52: // VertexPositionNormalTextureSkinned: + float4 weights, ubyte4 indices
                    result.push_back({0,  {3, GL_FLOAT, false, false}});
                    result.push_back({12, {3, GL_FLOAT, false, false}});
                    result.push_back({24, {2, GL_FLOAT, false, false}});
                    result.push_back({32, {4, GL_FLOAT, false, false}});
                    result.push_back({48, {4, GL_UNSIGNED_BYTE, false, true}});
                    break;
                default:
                    break;
            }
            return result;
        }
#endif

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
        /// plain SokolTextureRenderer or a SokolRenderTargetRenderer sampled as a texture (a target
        /// rendered to in an earlier pass) -- the two are unrelated class hierarchies (a render
        /// target's ITextureRenderer half has no CPU-side pixel data at all), so this tries both
        /// rather than assuming one.
        std::uint32_t ResolveSampledTextureViewId(const ITextureRenderer& texture, const char* context)
        {
            if (const auto* plain = dynamic_cast<const SokolTextureRenderer*>(&texture))
                return plain->GetViewIdEXT();
            if (const auto* renderTarget = dynamic_cast<const SokolRenderTargetRenderer*>(&texture))
                return renderTarget->GetColorTextureViewIdEXT();
            throw std::runtime_error(
                std::string("Sokol renderer: ") + context + " was handed a foreign texture renderer");
        }

        /// Same shape as ResolveSampledTextureViewId, for a cube texture (plan_sokol.md SOKOL-34):
        /// accepts either a plain SokolTextureCubeRenderer or a SokolRenderTargetCubeRenderer sampled
        /// as an environment map (a genuine, real-XNA-legal dynamic-reflection pattern -- both
        /// implement ITextureCubeRenderer).
        std::uint32_t ResolveSampledCubeViewId(const ITextureCubeRenderer& cube, const char* context)
        {
            if (const auto* plain = dynamic_cast<const SokolTextureCubeRenderer*>(&cube))
                return plain->GetViewIdEXT();
            if (const auto* renderTarget = dynamic_cast<const SokolRenderTargetCubeRenderer*>(&cube))
                return renderTarget->GetTextureViewIdEXT();
            throw std::runtime_error(
                std::string("Sokol renderer: ") + context + " was handed a foreign texture-cube renderer");
        }

        /// Same shape as ResolveSampledTextureViewId, but resolves the underlying sg_image id
        /// rather than a sampling view -- needed by SokolEffectRenderer::BindTexture() (plan_sokol.md
        /// SOKOL-28), which queries the raw GL texture handle via sg_gl_query_image_info() and has
        /// no use for a view (custom-effect texture binds are raw glBindTexture calls, not
        /// sg_bindings).
        std::uint32_t ResolveSampledTextureImageId(const ITextureRenderer& texture, const char* context)
        {
            if (const auto* plain = dynamic_cast<const SokolTextureRenderer*>(&texture))
                return plain->GetImageIdEXT();
            if (const auto* renderTarget = dynamic_cast<const SokolRenderTargetRenderer*>(&texture))
                return renderTarget->GetColorImageIdEXT();
            throw std::runtime_error(
                std::string("Sokol renderer: ") + context + " was handed a foreign texture renderer");
        }

        /// Cube-texture analog of ResolveSampledTextureImageId.
        std::uint32_t ResolveSampledCubeImageId(const ITextureCubeRenderer& cube, const char* context)
        {
            if (const auto* plain = dynamic_cast<const SokolTextureCubeRenderer*>(&cube))
                return plain->GetImageIdEXT();
            if (const auto* renderTarget = dynamic_cast<const SokolRenderTargetCubeRenderer*>(&cube))
                return renderTarget->GetImageIdEXT();
            throw std::runtime_error(
                std::string("Sokol renderer: ") + context + " was handed a foreign texture-cube renderer");
        }

        /// True when @p texture is a RenderTarget2D's colour attachment rather than a plain,
        /// CPU-uploaded texture (REMED-GFX-147). A render target's colour image is written by real
        /// GPU rasterization, whose framebuffer-origin convention places row 0 (CNA's logical top)
        /// at OpenGL's HIGH y (v=1 when later sampled) -- the opposite of a plain SokolTextureRenderer,
        /// whose CPU pixel buffer is uploaded byte-for-byte via sg_make_image with no Y-flip, so its
        /// row 0 lands at v=0 directly. Sampling both kinds with the same UV convention silently
        /// mirrors every render-target source vertically; the fragment shaders' `uRtFlipV`/`rtFlipV`
        /// uniform (sprite_fs_params/textured3d_fs_params/lit3d_fs_params) corrects it per-draw, the
        /// same per-slot-uniform shape EasyGL's own uRtFlipV and bgfx's u_rtFlipV use for the
        /// identical finding.
        bool IsRenderTargetSourceEXT(const ITextureRenderer& texture)
        {
            return dynamic_cast<const SokolRenderTargetRenderer*>(&texture) != nullptr;
        }

#if CNA_SOKOL_HAS_GL_READBACK
        /// Reads back a rectangle of an sg_image's colour content via a throwaway GL FBO, GL-only
        /// (plan_sokol.md SOKOL-38). sokol_gfx has no read-back API of its own, but
        /// sg_gl_query_image_info() exposes the raw GL texture handle backing any image regardless
        /// of whether it is the currently active render target -- unlike ReadBackbuffer(), which
        /// piggybacks on whatever FBO sokol_gfx itself left bound after ending the active pass, this
        /// needs its own independent FBO since RenderTarget2D::GetData()/RenderTargetCube::GetData()
        /// must work whether or not the target is currently bound. @p attachTarget is
        /// GL_TEXTURE_2D for a RenderTarget2D or GL_TEXTURE_CUBE_MAP_POSITIVE_X + face for one face
        /// of a RenderTargetCube. @p level selects the mip level to attach (0 for a non-mipmapped
        /// target). Returns false (leaving @p data untouched) if the image has no raw GL texture or
        /// the throwaway FBO cannot be completed -- neither should happen for an image this renderer
        /// itself created as a colour attachment, but there is no fabricated-pixel fallback either
        /// way.
        bool ReadColorImagePixelsViaGL(std::uint32_t colorImageId, GLenum attachTarget, int level,
                                       int imageHeight, int x, int y, int w, int h, void* data)
        {
            if (colorImageId == 0) return false;

            const sg_gl_image_info info = sg_gl_query_image_info(MakeImageHandle(colorImageId));
            const GLuint texId = info.tex[info.active_slot];
            if (texId == 0) return false;

            GLint previousFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, attachTarget, texId, level);

            const bool complete =
                glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (complete)
            {
                // Top-left origin (every CNA caller) -> GL's bottom-left framebuffer origin, the
                // same flip ReadBackbuffer applies to the window's own framebuffer.
                const int glY = imageHeight - (y + h);
                std::vector<std::uint8_t> raw(static_cast<std::size_t>(w) * h * 4u);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

                auto* destination = static_cast<std::uint8_t*>(data);
                for (int row = 0; row < h; ++row)
                {
                    const int sourceRow = h - 1 - row;
                    std::memcpy(destination + static_cast<std::size_t>(row) * w * 4u,
                                raw.data() + static_cast<std::size_t>(sourceRow) * w * 4u,
                                static_cast<std::size_t>(w) * 4u);
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
            glDeleteFramebuffers(1, &fbo);
            return complete;
        }

        /// Regenerates every mip level above 0 of an sg_image from its current level-0 content via
        /// a raw glGenerateMipmap, GL-only (plan_sokol.md SOKOL-39). Mirrors
        /// EasyGLRenderTargetRenderer's own "auto-generate on unbind" contract (matching real D3D9
        /// XNA's D3DUSAGE_AUTOGENMIPMAP semantics). @p bindTarget is GL_TEXTURE_2D or
        /// GL_TEXTURE_CUBE_MAP. Silently does nothing if the image has no raw GL texture -- this is
        /// only ever called on a target this renderer itself created with mipMap=true, so that
        /// should not happen in practice.
        void RegenerateMipmapsViaGL(std::uint32_t colorImageId, GLenum bindTarget)
        {
            if (colorImageId == 0) return;

            const sg_gl_image_info info = sg_gl_query_image_info(MakeImageHandle(colorImageId));
            const GLuint texId = info.tex[info.active_slot];
            if (texId == 0) return;

            const GLenum bindingQuery =
                (bindTarget == GL_TEXTURE_CUBE_MAP) ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D;
            GLint previousTex = 0;
            glGetIntegerv(bindingQuery, &previousTex);

            glBindTexture(bindTarget, texId);
            glGenerateMipmap(bindTarget);
            glBindTexture(bindTarget, static_cast<GLuint>(previousTex));
        }
#endif
    }

    // ---------------------------------------------------------------------------------------
    // SokolTextureRenderer
    // ---------------------------------------------------------------------------------------

    SokolTextureRenderer::SokolTextureRenderer(const ImageData& data)
        : width_(data.width)
        , height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Sokol renderer: texture dimensions must be positive");
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
        LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
    }

    SokolTextureRenderer::~SokolTextureRenderer()
    {
        LiveSampledRendererRegistryEXT().erase(this);  // plan_sokol.md SOKOL-44
        DestroyImage();
    }

    void SokolTextureRenderer::DestroyImage()
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

    void SokolTextureRenderer::RecreateImage()
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
            throw std::runtime_error("Sokol renderer: sg_make_image failed for a Texture2D");
        }

        sg_view_desc viewDesc = {};
        viewDesc.texture.image = MakeImageHandle(imageId_);
        viewDesc.label = "cna_texture2d_view";
        viewId_ = sg_make_view(&viewDesc).id;
        if (sg_query_view_state(MakeViewHandle(viewId_)) != SG_RESOURCESTATE_VALID)
        {
            viewId_ = 0;
            DestroyImage();
            throw std::runtime_error("Sokol renderer: sg_make_view failed for a Texture2D");
        }
    }

    int SokolTextureRenderer::GetWidth() const { return width_; }

    int SokolTextureRenderer::GetHeight() const { return height_; }

    void SokolTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
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

    void SokolTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= mipLevels_) return;

        auto& target = levels_[static_cast<std::size_t>(level)];
        const std::size_t supplied = static_cast<std::size_t>(std::max(levelW, 0))
                                   * static_cast<std::size_t>(std::max(levelH, 0)) * 4u;
        std::memcpy(target.data(), rgba, std::min(target.size(), supplied));
        RecreateImage();
    }

    bool SokolTextureRenderer::GetData(int level, int x, int y, int w, int h,
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
    // SokolTextureCubeRenderer
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

    int SokolTextureCubeRenderer::LevelDim(int level) const
    {
        return std::max(1, size_ >> level);
    }

    SokolTextureCubeRenderer::SokolTextureCubeRenderer(int size, bool mipMap)
        : size_(size)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        if (size <= 0)
            throw std::runtime_error("Sokol renderer: TextureCube size must be positive");

        levels_.resize(static_cast<std::size_t>(levelCount_));
        for (int level = 0; level < levelCount_; ++level)
        {
            const int dim = LevelDim(level);
            const std::size_t faceBytes = static_cast<std::size_t>(dim) * static_cast<std::size_t>(dim) * 4u;
            for (auto& face : levels_[static_cast<std::size_t>(level)])
                face.assign(faceBytes, 0u);
        }
        RecreateImage();
        LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
    }

    SokolTextureCubeRenderer::~SokolTextureCubeRenderer()
    {
        LiveSampledRendererRegistryEXT().erase(this);  // plan_sokol.md SOKOL-44
        DestroyImage();
    }

    void SokolTextureCubeRenderer::DestroyImage()
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

    void SokolTextureCubeRenderer::RecreateImage()
    {
        // Recreated as immutable rather than updated in place -- see this class's own doc comment
        // (mirrors SokolTextureRenderer::RecreateImage()'s identical reasoning).
        DestroyImage();

        sg_image_desc desc = {};
        desc.type = SG_IMAGETYPE_CUBE;
        desc.usage.immutable = true;
        desc.width = size_;
        desc.height = size_;
        desc.num_mipmaps = levelCount_;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.label = "cna_texture_cube";

        // sg_image_data.mip_levels[level] is one CONTIGUOUS range per mip level for a cube image
        // (all 6 faces concatenated -- see this class's own doc comment), unlike this class's own
        // per-face-per-level storage, so each level's 6 face buffers are concatenated into a
        // temporary blob that only needs to outlive the synchronous sg_make_image() call below.
        std::vector<std::vector<std::uint8_t>> combined(static_cast<std::size_t>(levelCount_));
        for (int level = 0; level < levelCount_; ++level)
        {
            const auto& faces = levels_[static_cast<std::size_t>(level)];
            auto& blob = combined[static_cast<std::size_t>(level)];
            blob.reserve(faces[0].size() * 6u);
            for (const auto& face : faces)
                blob.insert(blob.end(), face.begin(), face.end());
            desc.data.mip_levels[level].ptr = blob.data();
            desc.data.mip_levels[level].size = blob.size();
        }

        imageId_ = sg_make_image(&desc).id;
        if (sg_query_image_state(MakeImageHandle(imageId_)) != SG_RESOURCESTATE_VALID)
        {
            imageId_ = 0;
            throw std::runtime_error("Sokol renderer: sg_make_image failed for a TextureCube");
        }

        sg_view_desc viewDesc = {};
        viewDesc.texture.image = MakeImageHandle(imageId_);
        viewDesc.label = "cna_texture_cube_view";
        viewId_ = sg_make_view(&viewDesc).id;
        if (sg_query_view_state(MakeViewHandle(viewId_)) != SG_RESOURCESTATE_VALID)
        {
            viewId_ = 0;
            DestroyImage();
            throw std::runtime_error("Sokol renderer: sg_make_view failed for a TextureCube");
        }
    }

    bool SokolTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
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
        RecreateImage();
        return true;
    }

    bool SokolTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
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
    // SokolTexture3DRenderer
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

    int SokolTexture3DRenderer::LevelWidth(int level) const { return std::max(1, width_ >> level); }
    int SokolTexture3DRenderer::LevelHeight(int level) const { return std::max(1, height_ >> level); }
    int SokolTexture3DRenderer::LevelDepth(int level) const { return std::max(1, depth_ >> level); }

    SokolTexture3DRenderer::SokolTexture3DRenderer(int width, int height, int depth, bool mipMap)
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

    bool SokolTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
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

    bool SokolTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
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
    // SokolRenderTargetRenderer
    // ---------------------------------------------------------------------------------------

    namespace
    {
        /// Mirrors Texture3D.cpp's own CalculateMipLevels(width, height) (rectangular, not just
        /// square) -- the same formula CalculateVolumeMipLevels above already reuses.
        int CalculateRenderTargetMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }
    }

    SokolRenderTargetRenderer::SokolRenderTargetRenderer(int width, int height, bool hasDepthStencil,
                                                        int multiSampleCount, bool mipMap)
        : width_(width)
        , height_(height)
        , mipMap_(mipMap)
        , levelCount_(mipMap ? CalculateRenderTargetMipLevels(width, height) : 1)
    {
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Sokol renderer: render target dimensions must be positive");

        // Clamp to the driver's real maximum, mirroring EasyGLRenderer's identical
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
        // levelCount_ (plan_sokol.md SOKOL-39): sokol_gfx's GL renderer allocates real GL storage
        // for every level up front via glTexStorage2D regardless of whether data is supplied, so
        // RegenerateMipmapsIfNeededEXT()'s glGenerateMipmap has valid storage to write into even
        // though only level 0 is ever rendered into directly (there is no public XNA API to bind a
        // specific RenderTarget2D mip level as the active target).
        colorDesc.num_mipmaps = levelCount_;
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
            throw std::runtime_error("Sokol renderer: sg_make_image failed for a RenderTarget2D's colour image");
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
                    "Sokol renderer: sg_make_image failed for a RenderTarget2D's MSAA colour image");
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
            throw std::runtime_error("Sokol renderer: sg_make_view (colour attachment) failed for a RenderTarget2D");
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
                    "Sokol renderer: sg_make_view (resolve attachment) failed for a RenderTarget2D");
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
            throw std::runtime_error("Sokol renderer: sg_make_view (texture) failed for a RenderTarget2D");
        }

        if (!hasDepthStencil)
        {
            LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
            return;
        }

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
            throw std::runtime_error("Sokol renderer: sg_make_image failed for a RenderTarget2D's depth-stencil image");
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
            throw std::runtime_error("Sokol renderer: sg_make_view (depth-stencil attachment) failed for a RenderTarget2D");
        }
        LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
    }

    SokolRenderTargetRenderer::~SokolRenderTargetRenderer()
    {
        LiveSampledRendererRegistryEXT().erase(this);  // plan_sokol.md SOKOL-44
        if (depthStencilAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(depthStencilAttachmentViewId_));
        if (depthStencilImageId_ != 0) sg_destroy_image(MakeImageHandle(depthStencilImageId_));
        if (colorTextureViewId_ != 0) sg_destroy_view(MakeViewHandle(colorTextureViewId_));
        if (resolveAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(resolveAttachmentViewId_));
        if (colorAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(colorAttachmentViewId_));
        if (msaaColorImageId_ != 0) sg_destroy_image(MakeImageHandle(msaaColorImageId_));
        if (colorImageId_ != 0) sg_destroy_image(MakeImageHandle(colorImageId_));
    }

    int SokolRenderTargetRenderer::GetWidth() const { return width_; }

    int SokolRenderTargetRenderer::GetHeight() const { return height_; }

    void SokolRenderTargetRenderer::BindAsRenderTarget() {}

    void SokolRenderTargetRenderer::UnbindAsRenderTarget() {}

    bool SokolRenderTargetRenderer::HasRealDepthBuffer(bool depthFormatWasRequested) const
    {
        (void)depthFormatWasRequested;
        return depthStencilAttachmentViewId_ != 0;
    }

    bool SokolRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (data == nullptr || level < 0 || level >= levelCount_) return false;
        const int levelWidth = MipDimension(width_, level);
        const int levelHeight = MipDimension(height_, level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > levelWidth || y + h > levelHeight) return false;
        if (dataLength < w * h * 4) return false;

        return ReadColorImagePixelsViaGL(colorImageId_, GL_TEXTURE_2D, level, levelHeight, x, y, w, h, data);
#else
        (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
        return false;
#endif
    }

    void SokolRenderTargetRenderer::RegenerateMipmapsIfNeededEXT() const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (!mipMap_) return;
        RegenerateMipmapsViaGL(colorImageId_, GL_TEXTURE_2D);
#endif
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderTargetCubeRenderer
    // ---------------------------------------------------------------------------------------

    SokolRenderTargetCubeRenderer::SokolRenderTargetCubeRenderer(int size, bool hasDepthStencil,
                                                                bool mipMap)
        : size_(size)
        , mipMap_(mipMap)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        if (size <= 0)
            throw std::runtime_error("Sokol renderer: cube render target size must be positive");

        sg_image_desc colorDesc = {};
        colorDesc.type = SG_IMAGETYPE_CUBE;
        colorDesc.usage.color_attachment = true;
        colorDesc.width = size_;
        colorDesc.height = size_;
        // levelCount_ (plan_sokol.md SOKOL-39): see SokolRenderTargetRenderer's identical comment.
        colorDesc.num_mipmaps = levelCount_;
        colorDesc.sample_count = 1;
        colorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
        colorDesc.label = "cna_render_target_cube_color";
        imageId_ = sg_make_image(&colorDesc).id;
        if (sg_query_image_state(MakeImageHandle(imageId_)) != SG_RESOURCESTATE_VALID)
        {
            imageId_ = 0;
            throw std::runtime_error(
                "Sokol renderer: sg_make_image failed for a RenderTargetCube's colour image");
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
                    "Sokol renderer: sg_make_view (colour attachment) failed for a RenderTargetCube face");
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
                "Sokol renderer: sg_make_view (texture) failed for a RenderTargetCube");
        }

        if (!hasDepthStencil)
        {
            LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
            return;
        }

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
                "Sokol renderer: sg_make_image failed for a RenderTargetCube's depth-stencil image");
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
                "Sokol renderer: sg_make_view (depth-stencil attachment) failed for a RenderTargetCube");
        }
        LiveSampledRendererRegistryEXT().insert(this);  // plan_sokol.md SOKOL-44
    }

    SokolRenderTargetCubeRenderer::~SokolRenderTargetCubeRenderer()
    {
        LiveSampledRendererRegistryEXT().erase(this);  // plan_sokol.md SOKOL-44
        if (depthStencilAttachmentViewId_ != 0) sg_destroy_view(MakeViewHandle(depthStencilAttachmentViewId_));
        if (depthStencilImageId_ != 0) sg_destroy_image(MakeImageHandle(depthStencilImageId_));
        if (textureViewId_ != 0) sg_destroy_view(MakeViewHandle(textureViewId_));
        for (std::uint32_t viewId : colorAttachmentViewIds_)
            if (viewId != 0) sg_destroy_view(MakeViewHandle(viewId));
        if (imageId_ != 0) sg_destroy_image(MakeImageHandle(imageId_));
    }

    int SokolRenderTargetCubeRenderer::GetSize() const { return size_; }

    void SokolRenderTargetCubeRenderer::BindAsRenderTargetFace(int /*face*/) {}

    void SokolRenderTargetCubeRenderer::UnbindAsRenderTarget() {}

    bool SokolRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (data == nullptr || level < 0 || level >= levelCount_) return false;
        if (face < 0 || face >= 6) return false;
        const int levelSize = MipDimension(size_, level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        const GLenum faceTarget = static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
        return ReadColorImagePixelsViaGL(imageId_, faceTarget, level, levelSize, x, y, w, h, data);
#else
        (void)face; (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
        return false;
#endif
    }

    void SokolRenderTargetCubeRenderer::RegenerateMipmapsIfNeededEXT() const
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (!mipMap_) return;
        RegenerateMipmapsViaGL(imageId_, GL_TEXTURE_CUBE_MAP);
#endif
    }

    bool SokolRenderTargetCubeRenderer::HasRealDepthBuffer(bool depthFormatWasRequested) const
    {
        (void)depthFormatWasRequested;
        return depthStencilAttachmentViewId_ != 0;
    }

    // ---------------------------------------------------------------------------------------
    // SokolVertexBufferRenderer
    // ---------------------------------------------------------------------------------------

    SokolVertexBufferRenderer::SokolVertexBufferRenderer(int vertexCapacity, SokolRenderer* owner)
        : capacity_(vertexCapacity)
        , ownerEXT_(owner)
    {
    }

    SokolVertexBufferRenderer::~SokolVertexBufferRenderer()
    {
        if (bufferId_ != 0) sg_destroy_buffer(MakeBufferHandle(bufferId_));
    }

    void SokolVertexBufferRenderer::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }

        const std::size_t requiredBytes = static_cast<std::size_t>(vertexCount) * strideInBytes;

        // plan_sokol.md SOKOL-24: cheap path -- reuse the existing dynamic buffer in place. Only
        // safe when the new data still fits what was actually allocated and this exact buffer
        // has not already been updated once this frame (sokol_gfx permits exactly one
        // sg_update_buffer() per buffer per frame; a second call trips a hard SOKOL_ASSERT, not
        // just a validation warning). ownerEXT_ is only null where no frame counter exists to
        // check against, so that case always falls through to recreate below.
        if (bufferId_ != 0 && ownerEXT_ != nullptr && requiredBytes <= allocatedBytesEXT_
            && lastUpdateFrameEXT_ != ownerEXT_->GetFrameIndexEXT())
        {
            sg_range range{ data, requiredBytes };
            sg_update_buffer(MakeBufferHandle(bufferId_), &range);
            lastUpdateFrameEXT_ = ownerEXT_->GetFrameIndexEXT();
            vertexCount_ = vertexCount;
            stride_ = strideInBytes;
            return;
        }

        // Recreate: first upload, the data outgrew what is currently allocated, or a same-frame
        // repeat upload would violate sokol_gfx's once-per-frame update rule.
        if (bufferId_ != 0)
        {
            sg_destroy_buffer(MakeBufferHandle(bufferId_));
            bufferId_ = 0;
        }

        // Sized to this VertexBuffer's own declared capacity, not just this call's vertexCount --
        // VertexBuffer::ValidateSetDataRange() (the XNA layer) never lets a SetData call exceed
        // that capacity, so a same-shape upload next frame can reuse this allocation via the
        // cheap path above instead of recreating again.
        const std::size_t allocBytes = std::max<std::size_t>(
            requiredBytes, static_cast<std::size_t>(capacity_) * strideInBytes);

        sg_buffer_desc desc = {};
        desc.usage.vertex_buffer = true;
        desc.usage.dynamic_update = true;
        desc.size = allocBytes;
        desc.label = "cna_vertex_buffer";
        bufferId_ = sg_make_buffer(&desc).id;
        if (sg_query_buffer_state(MakeBufferHandle(bufferId_)) != SG_RESOURCESTATE_VALID)
        {
            bufferId_ = 0;
            throw std::runtime_error("Sokol renderer: sg_make_buffer failed for a VertexBuffer");
        }
        allocatedBytesEXT_ = allocBytes;
        lastUpdateFrameEXT_ = 0;

        // Dynamic/stream buffers must be created with no initial data (sokol_gfx validation:
        // "sg_buffer_desc.data.ptr must be null for dynamic/stream buffers") -- seed the content
        // with an immediate sg_update_buffer() instead. Always legal here: a just-created buffer
        // has never been updated, so this can never be the disallowed second update this frame.
        sg_range range{ data, requiredBytes };
        sg_update_buffer(MakeBufferHandle(bufferId_), &range);
        if (ownerEXT_ != nullptr) lastUpdateFrameEXT_ = ownerEXT_->GetFrameIndexEXT();

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void SokolVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // Consumed by DrawColored3D, which builds its pipeline's vertex layout from the real
        // element offsets rather than inferring one from the byte stride.
        declaration_ = vertexDeclaration;
        hasDeclaration_ = true;
    }

    int SokolVertexBufferRenderer::GetVertexCount() const { return vertexCount_; }

    // ---------------------------------------------------------------------------------------
    // SokolIndexBufferRenderer
    // ---------------------------------------------------------------------------------------

    SokolIndexBufferRenderer::SokolIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit,
                                                       SokolRenderer* owner)
        : capacity_(indexCapacity)
        , thirtyTwoBit_(thirtyTwoBit)
        , ownerEXT_(owner)
    {
    }

    SokolIndexBufferRenderer::~SokolIndexBufferRenderer()
    {
        if (bufferId_ != 0) sg_destroy_buffer(MakeBufferHandle(bufferId_));
    }

    void SokolIndexBufferRenderer::Upload(const void* data, int indexCount, std::size_t indexSize)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }

        const std::size_t requiredBytes = static_cast<std::size_t>(indexCount) * indexSize;

        // Same cheap-path reasoning as SokolVertexBufferRenderer::SetData (plan_sokol.md SOKOL-24).
        if (bufferId_ != 0 && ownerEXT_ != nullptr && requiredBytes <= allocatedBytesEXT_
            && lastUpdateFrameEXT_ != ownerEXT_->GetFrameIndexEXT())
        {
            sg_range range{ data, requiredBytes };
            sg_update_buffer(MakeBufferHandle(bufferId_), &range);
            lastUpdateFrameEXT_ = ownerEXT_->GetFrameIndexEXT();
            indexCount_ = indexCount;
            return;
        }

        if (bufferId_ != 0)
        {
            sg_destroy_buffer(MakeBufferHandle(bufferId_));
            bufferId_ = 0;
        }

        // Sized to this IndexBuffer's own declared capacity at @p indexSize, not just this call's
        // indexCount -- same reasoning as SokolVertexBufferRenderer::SetData's own allocBytes.
        const std::size_t allocBytes = std::max<std::size_t>(
            requiredBytes, static_cast<std::size_t>(capacity_) * indexSize);

        sg_buffer_desc desc = {};
        desc.usage.index_buffer = true;
        desc.usage.dynamic_update = true;
        desc.size = allocBytes;
        desc.label = "cna_index_buffer";
        bufferId_ = sg_make_buffer(&desc).id;
        if (sg_query_buffer_state(MakeBufferHandle(bufferId_)) != SG_RESOURCESTATE_VALID)
        {
            bufferId_ = 0;
            throw std::runtime_error("Sokol renderer: sg_make_buffer failed for an IndexBuffer");
        }
        allocatedBytesEXT_ = allocBytes;
        lastUpdateFrameEXT_ = 0;

        // See SokolVertexBufferRenderer::SetData's own comment on seeding a dynamic buffer's
        // initial content via sg_update_buffer() rather than sg_buffer_desc.data.
        sg_range range{ data, requiredBytes };
        sg_update_buffer(MakeBufferHandle(bufferId_), &range);
        if (ownerEXT_ != nullptr) lastUpdateFrameEXT_ = ownerEXT_->GetFrameIndexEXT();

        indexCount_ = indexCount;
    }

    void SokolIndexBufferRenderer::SetData16(const void* data, int indexCount)
    {
        thirtyTwoBit_ = false;
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void SokolIndexBufferRenderer::SetData32(const void* data, int indexCount)
    {
        thirtyTwoBit_ = true;
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    int SokolIndexBufferRenderer::GetIndexCount() const { return indexCount_; }

    bool SokolIndexBufferRenderer::IsThirtyTwoBit() const { return thirtyTwoBit_; }

    // ---------------------------------------------------------------------------------------
    // SokolOcclusionQueryRenderer
    // ---------------------------------------------------------------------------------------

    SokolOcclusionQueryRenderer::SokolOcclusionQueryRenderer(SokolRenderer* owner)
        : owner_(owner)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        GLuint id = 0;
        glGenQueries(1, &id);
        queryId_ = id;
#endif
    }

    SokolOcclusionQueryRenderer::~SokolOcclusionQueryRenderer()
    {
        // plan_sokol.md SOKOL-43: unconditional, regardless of active_ -- a query destroyed while
        // still active must not leave the context's active-query slot pointing at a now-dangling
        // object. ReleaseOcclusionQueryEXT() itself no-ops if this instance never held the slot.
        if (owner_ != nullptr) owner_->ReleaseOcclusionQueryEXT(this);
#if CNA_SOKOL_HAS_GL_READBACK
        if (queryId_ != 0)
        {
            const GLuint id = queryId_;
            glDeleteQueries(1, &id);
        }
#endif
    }

    void SokolOcclusionQueryRenderer::Begin()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        // See this method's own header doc: a repeated Begin() with no intervening End() must not
        // reach glBeginQuery a second time, or GL raises GL_INVALID_OPERATION that stays pending
        // until sokol_gfx's own next GL call trips over it.
        if (queryId_ == 0 || active_) return;
        // plan_sokol.md SOKOL-43: OpenGL permits only one active GL_SAMPLES_PASSED query per
        // context, not per object -- a DIFFERENT query's outstanding Begin() must be refused here
        // too, the same silent-absorb contract as a repeated Begin() on this same object.
        if (owner_ != nullptr && !owner_->TryActivateOcclusionQueryEXT(this)) return;
        active_ = true;
        // SOKOL_GLCORE is desktop GL, which reports the real sample count (GL_SAMPLES_PASSED);
        // GLES3 has no such query at all, only the any-samples boolean EasyGL's own GLES3 target
        // uses -- matching that here keeps this branch meaningful if SOKOL_GLES3 is ever the
        // active configure-time API (docs/sokol-renderer.md: only GLCORE is implemented/verified).
    #if defined(SOKOL_GLCORE)
        glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    #else
        glBeginQuery(GL_ANY_SAMPLES_PASSED, queryId_);
    #endif
#endif
    }

    void SokolOcclusionQueryRenderer::End()
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
        if (owner_ != nullptr) owner_->ReleaseOcclusionQueryEXT(this);
#endif
    }

    bool SokolOcclusionQueryRenderer::IsComplete() const
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

    int SokolOcclusionQueryRenderer::PixelCount() const
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
    // SokolEffectRenderer
    // ---------------------------------------------------------------------------------------

    SokolEffectRenderer::~SokolEffectRenderer()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ != 0) glDeleteProgram(programId_);
#endif
    }

    bool SokolEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ != 0)
        {
            glDeleteProgram(programId_);
            programId_ = 0;
        }

        auto compileStage = [](GLenum stage, const std::string& src,
                               const char* stageName, std::string& error) -> GLuint
        {
            const GLuint shader = glCreateShader(stage);
            const char* srcPtr = src.c_str();
            const auto srcLen = static_cast<GLint>(src.size());
            glShaderSource(shader, 1, &srcPtr, &srcLen);
            glCompileShader(shader);
            GLint status = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (status == GL_FALSE)
            {
                GLint logLen = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                std::string log(static_cast<std::size_t>(std::max(logLen, 1)), '\0');
                glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                error = std::string(stageName) + ": " + log.c_str();
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        };

        const GLuint vs = compileStage(GL_VERTEX_SHADER, vertSrc, "VS", compileError_);
        if (vs == 0) return false;
        const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc, "FS", compileError_);
        if (fs == 0)
        {
            glDeleteShader(vs);
            return false;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        // The shaders themselves are no longer needed once linked -- glDeleteShader on an attached
        // shader only marks it for deletion; GL keeps it alive until glDetachShader/program
        // destruction, so this is safe to do unconditionally here.
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_FALSE)
        {
            GLint logLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(static_cast<std::size_t>(std::max(logLen, 1)), '\0');
            glGetProgramInfoLog(program, logLen, nullptr, log.data());
            compileError_ = std::string("Link: ") + log.c_str();
            glDeleteProgram(program);
            return false;
        }

        programId_ = program;
        return true;
#else
        compileError_ =
            "Sokol renderer: custom ShaderEffect compilation requires CNA_SOKOL_API=GLCORE";
        return false;
#endif
    }

    void SokolEffectRenderer::Bind()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ != 0) glUseProgram(programId_);
#endif
    }

    void SokolEffectRenderer::Unbind()
    {
        // No unbind needed -- the next Bind() or sg_reset_state_cache()-bracketed sokol_gfx draw
        // overrides the current program regardless, matching EasyGLEffectRenderer::Unbind()'s own
        // no-op.
    }

    bool SokolEffectRenderer::IsValid() const
    {
        return programId_ != 0;
    }

    std::string SokolEffectRenderer::GetCompileError() const
    {
        return compileError_;
    }

    // SetUniformXxx all use glProgramUniform* (GL 4.1 core, ARB_separate_shader_objects) rather
    // than plain glUniform*: the latter writes to whatever program glUseProgram last bound, not
    // necessarily this one, and IEffectRenderer's contract explicitly allows a caller to set
    // uniforms at any time relative to Bind() (real XNA/ShaderEffect code calls Effect::Apply()
    // -- which does NOT bind this renderer's GL program at all, only Effect::OnApply()/
    // SetCurrentEffect() -- then SetTexture()/SetUniformXxx(), all before the actual draw call
    // that finally calls Bind()). glProgramUniform* writes directly to the named program object
    // regardless of what is currently bound, matching that call order exactly. This is the same
    // gap Bind()-then-set ordering has that EasyGLEffectRenderer's own program wrapper avoids the
    // identical way.

    void SokolEffectRenderer::SetUniformFloat(const char* name, float value)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform1f(programId_, loc, value);
#endif
    }

    void SokolEffectRenderer::SetUniformInt(const char* name, int value)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform1i(programId_, loc, value);
#endif
    }

    void SokolEffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform2f(programId_, loc, x, y);
#endif
    }

    void SokolEffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform3f(programId_, loc, x, y, z);
#endif
    }

    void SokolEffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform4f(programId_, loc, x, y, z, w);
#endif
    }

    void SokolEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniformMatrix4fv(programId_, loc, 1, GL_FALSE, matrix);
#endif
    }

    void SokolEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0 || count <= 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform1fv(programId_, loc, count, values);
#endif
    }

    void SokolEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (programId_ == 0 || count <= 0) return;
        const GLint loc = glGetUniformLocation(programId_, name);
        if (loc >= 0) glProgramUniform2fv(programId_, loc, count, values);
#endif
    }

    void SokolEffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (texture == nullptr) return;
        // Recorded, not resolved/applied immediately -- see PendingTextureBind's and
        // ApplyPendingTextureBindsEXT()'s own doc comments for why (plan_sokol.md SOKOL-44).
        for (auto& pending : pendingTextureBinds_)
        {
            if (pending.unit == unit)
            {
                pending.texture = texture;
                pending.cubeTexture = nullptr;
                pending.isCube = false;
                return;
            }
        }
        pendingTextureBinds_.push_back({unit, texture, nullptr, false});
#endif
    }

    void SokolEffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (texture == nullptr) return;
        for (auto& pending : pendingTextureBinds_)
        {
            if (pending.unit == unit)
            {
                pending.texture = nullptr;
                pending.cubeTexture = texture;
                pending.isCube = true;
                return;
            }
        }
        pendingTextureBinds_.push_back({unit, nullptr, texture, true});
#endif
    }

    void SokolEffectRenderer::ApplyPendingTextureBindsEXT()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        for (const PendingTextureBind& pending : pendingTextureBinds_)
        {
            // plan_sokol.md SOKOL-44: resolved HERE, at draw time, not when BindTexture()/
            // BindTextureCube() was originally called -- the source's current sg_image id may have
            // changed (or been destroyed and recreated) via SetData() in between. The liveness
            // check guards against a rarer, more serious variant of the same problem: some XNA-
            // layer texture wrappers (Texture2D::SetData(const Color*, int) in particular) replace
            // their entire renderer object rather than mutating it in place, which would leave this
            // pointer dangling -- see LiveSampledRendererRegistryEXT()'s own doc comment.
            std::uint32_t imageId = 0;
            if (pending.isCube)
            {
                if (pending.cubeTexture == nullptr) continue;
                if (LiveSampledRendererRegistryEXT().count(pending.cubeTexture) == 0) continue;
                imageId = ResolveSampledCubeImageId(*pending.cubeTexture,
                                                     "a custom-effect cube-texture bind");
            }
            else
            {
                if (pending.texture == nullptr) continue;
                if (LiveSampledRendererRegistryEXT().count(pending.texture) == 0) continue;
                imageId = ResolveSampledTextureImageId(*pending.texture,
                                                        "a custom-effect texture bind");
            }
            const sg_gl_image_info info = sg_gl_query_image_info(MakeImageHandle(imageId));
            const GLuint texId = info.tex[info.active_slot];
            if (texId == 0) continue;
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(pending.unit));
            // Clears whatever sampler object a prior sg_apply_bindings() left bound to this unit,
            // so this texture samples with its own default GL parameters rather than inheriting
            // stale sokol_gfx sampler state (sokol_gfx's GL renderer binds sampler objects via
            // glBindSampler(), not glTexParameteri() per-texture).
            glBindSampler(static_cast<GLuint>(pending.unit), 0);
            glBindTexture(pending.isCube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, texId);
        }
        glActiveTexture(GL_TEXTURE0);
#endif
    }

    // ---------------------------------------------------------------------------------------
    // SokolSpriteBatchRenderer
    // ---------------------------------------------------------------------------------------

    SokolSpriteBatchRenderer::SokolSpriteBatchRenderer(SokolRenderer& renderer)
        : renderer_(renderer)
    {
    }

    SokolSpriteBatchRenderer::~SokolSpriteBatchRenderer() = default;

    void SokolSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        pendingVertices_.clear();
        currentTexture_ = nullptr;
    }

    void SokolSpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void SokolSpriteBatchRenderer::SetTransformMatrix(const Matrix& m) { transform_ = m; }

    void SokolSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            // Flush while the OLD effect (or lack of one) is still in effect, matching
            // EasyGLSpriteBatchRenderer::SetCustomEffect's own contract -- otherwise pending sprites
            // drawn under the previous shader would flush under the new one instead.
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void SokolSpriteBatchRenderer::SetSamplerFilter(int textureFilter) { pendingFilter_ = textureFilter; }

    void SokolSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void SokolSpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty() || currentTexture_ == nullptr)
        {
            pendingVertices_.clear();
            currentTexture_ = nullptr;
            return;
        }

        renderer_.DrawSpriteRunEXT(*currentTexture_, pendingVertices_, transform_,
                                  pendingFilter_, pendingAddressU_, pendingAddressV_, customEffect_);
        pendingVertices_.clear();
        currentTexture_ = nullptr;
    }

    void SokolSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h),
             Rectangle(0, 0, w, h), Microsoft::Xna::Framework::Color::White);
    }

    void SokolSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color,
             0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void SokolSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth)
    {
        (void)layerDepth; // Sort order is resolved by the shared SpriteBatch before it reaches here.

        if (!begun_) throw std::runtime_error("Sokol renderer: SpriteBatch Draw called before Begin()");

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
    // SokolRenderer -- cache keys
    // ---------------------------------------------------------------------------------------

    bool SokolRenderer::PipelineKey::operator==(const PipelineKey& other) const
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
            && sampleCount == other.sampleCount
            && colorAttachmentCount == other.colorAttachmentCount
            && blendFactorPacked == other.blendFactorPacked;
    }

    std::size_t SokolRenderer::PipelineKeyHash::operator()(const PipelineKey& key) const
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
        mix(static_cast<std::size_t>(key.colorAttachmentCount));
        mix(static_cast<std::size_t>(key.blendFactorPacked));
        return hash;
    }

    bool SokolRenderer::Pipeline3DKey::operator==(const Pipeline3DKey& other) const
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
            && colorAttachmentCount == other.colorAttachmentCount
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
            && normalFormat == other.normalFormat
            && blendWeightOffset == other.blendWeightOffset
            && blendWeightFormat == other.blendWeightFormat
            && blendIndicesOffset == other.blendIndicesOffset
            && blendIndicesFormat == other.blendIndicesFormat
            && blendFactorPacked == other.blendFactorPacked;
    }

    std::size_t SokolRenderer::Pipeline3DKeyHash::operator()(const Pipeline3DKey& key) const
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
        mix(static_cast<std::size_t>(key.colorAttachmentCount));
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
        mix(static_cast<std::size_t>(key.blendWeightOffset + 1));
        mix(static_cast<std::size_t>(key.blendWeightFormat));
        mix(static_cast<std::size_t>(key.blendIndicesOffset + 1));
        mix(static_cast<std::size_t>(key.blendIndicesFormat));
        mix(static_cast<std::size_t>(key.blendFactorPacked));
        return hash;
    }

    bool SokolRenderer::PipelineInstanced3DKey::operator==(
        const PipelineInstanced3DKey& other) const
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
            && colorAttachmentCount == other.colorAttachmentCount
            && cullMode == other.cullMode
            && primitiveType == other.primitiveType
            && indexType == other.indexType
            && stride == other.stride
            && instanceStepRate == other.instanceStepRate
            && positionOffset == other.positionOffset
            && positionFormat == other.positionFormat
            && blendFactorPacked == other.blendFactorPacked;
    }

    std::size_t SokolRenderer::PipelineInstanced3DKeyHash::operator()(
        const PipelineInstanced3DKey& key) const
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
        mix(static_cast<std::size_t>(key.colorAttachmentCount));
        mix(static_cast<std::size_t>(key.cullMode));
        mix(static_cast<std::size_t>(key.primitiveType));
        mix(static_cast<std::size_t>(key.indexType));
        mix(static_cast<std::size_t>(key.stride));
        mix(static_cast<std::size_t>(key.instanceStepRate));
        mix(static_cast<std::size_t>(key.positionOffset));
        mix(static_cast<std::size_t>(key.positionFormat));
        mix(static_cast<std::size_t>(key.blendFactorPacked));
        return hash;
    }

    bool SokolRenderer::SamplerKey::operator==(const SamplerKey& other) const
    {
        return filter == other.filter && addressU == other.addressU
            && addressV == other.addressV && maxAnisotropy == other.maxAnisotropy;
    }

    std::size_t SokolRenderer::SamplerKeyHash::operator()(const SamplerKey& key) const
    {
        return (static_cast<std::size_t>(key.filter) << 24)
             ^ (static_cast<std::size_t>(key.addressU) << 16)
             ^ (static_cast<std::size_t>(key.addressV) << 8)
             ^ static_cast<std::size_t>(key.maxAnisotropy);
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- lifetime
    // ---------------------------------------------------------------------------------------

    SokolRenderer::SokolRenderer(const GraphicsRendererCreateArgs& args)
        : platformContext_(std::make_unique<PlatformGlContextOwner>(
              RequirePlatformGlContext(args.glContext, "SOKOL"),
              RequirePlatformGlWindow(args.surface, "SOKOL"),
              RequestedContext(args.multiSampleCount)))
        , surface_(args.surface)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , sampleCount_(args.multiSampleCount > 1 ? args.multiSampleCount : 1)
        , swapInterval_(args.swapInterval)
    {
        platformContext_->SetSwapInterval(swapInterval_);
        const auto granted = platformContext_->GetAttributes();
        sampleCount_ = granted.multisampleBuffers > 0 && granted.multisampleSamples > 1
            ? granted.multisampleSamples : 1;

        try
        {
            SetupSokol();
            CreateSpriteResources();
        }
        catch (...)
        {
            // Transactional construction: a partially initialised renderer must never reach the
            // window registry or a caller's unique_ptr.
            if (sg_isvalid()) sg_shutdown();
            throw;
        }

        IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
    }

    SokolRenderer::~SokolRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());

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
#if CNA_SOKOL_HAS_GL_READBACK
        // Raw GL object, not tracked by sokol_gfx at all -- must be freed while the GL context is
        // still current, same reasoning as defaultWhiteTexture_ above.
        if (customEffectVaoId_ != 0)
        {
            glDeleteVertexArrays(1, &customEffectVaoId_);
            customEffectVaoId_ = 0;
        }
#endif
        if (sg_isvalid()) sg_shutdown();
    }

    void SokolRenderer::SetupSokol()
    {
        sg_desc desc = {};
        desc.logger.func = slog_func;
        desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        desc.environment.defaults.sample_count = sampleCount_;
        sg_setup(&desc);

        if (!sg_isvalid())
            throw std::runtime_error("Sokol renderer: sg_setup() did not produce a valid sokol_gfx context");
    }

    void SokolRenderer::CreateSpriteResources()
    {
        spriteShaderId_ = sg_make_shader(cna_sprite_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(spriteShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: sprite shader creation failed");

        // Streaming vertex buffer: each SpriteBatch flush appends its own run and draws from the
        // returned byte offset, which is what makes an arbitrary number of flushes per frame legal
        // under sokol_gfx's one-update-per-buffer-per-frame rule.
        sg_buffer_desc vertexDesc = {};
        vertexDesc.usage.vertex_buffer = true;
        vertexDesc.usage.stream_update = true;
        vertexDesc.size = static_cast<std::size_t>(kMaxSpriteQuads) * kSpriteVerticesPerQuad
                        * sizeof(SokolSpriteBatchRenderer::Vertex);
        vertexDesc.label = "cna_sprite_vertices";
        spriteVertexBufferId_ = sg_make_buffer(&vertexDesc).id;
        if (sg_query_buffer_state(MakeBufferHandle(spriteVertexBufferId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: sprite vertex buffer creation failed");

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
            throw std::runtime_error("Sokol renderer: sprite index buffer creation failed");

        colored3dShaderId_ = sg_make_shader(cna_colored3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(colored3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: colored-3D shader creation failed");

        textured3dShaderId_ = sg_make_shader(cna_textured3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(textured3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: textured-3D shader creation failed");

        lit3dShaderId_ = sg_make_shader(cna_lit3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(lit3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: lit-3D shader creation failed");

        dualTextured3dShaderId_ = sg_make_shader(cna_dualtextured3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(dualTextured3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: dual-textured-3D shader creation failed");

        skinned3dShaderId_ = sg_make_shader(cna_skinned3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(skinned3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: skinned-3D shader creation failed");

        instanced3dShaderId_ = sg_make_shader(cna_instanced3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(instanced3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: instanced-3D shader creation failed");

        envmap3dShaderId_ = sg_make_shader(cna_envmap3d_shader_desc(sg_query_backend())).id;
        if (sg_query_shader_state(MakeShaderHandle(envmap3dShaderId_)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: environment-mapped-3D shader creation failed");
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- pass management
    // ---------------------------------------------------------------------------------------

    void SokolRenderer::QueueClear(bool color, float r, float g, float b, float a,
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

    void SokolRenderer::BeginPassIfNeeded()
    {
        if (passActive_) return;

        sg_pass pass = {};

        // LOAD unless an explicit Clear* asked otherwise: a pass restarted mid-frame (because a
        // clear or a state change closed the previous one) must not discard what was already drawn.
        // This is also what makes a bound render target's content survive across separate
        // SetRenderTarget(s) bind/unbind cycles regardless of RenderTargetUsage -- matching the
        // EasyGL renderer's own documented simplification: a real FBO naturally preserves content,
        // and an explicit Clear() (which GraphicsDevice itself issues for a DiscardContents
        // target immediately after binding) is what actually discards it, on every renderer.
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
                // sampled later (SokolRenderTargetRenderer::GetColorTextureViewIdEXT()).
                pass.attachments.resolves[0] = MakeViewHandle(resolveAttachmentViewId);
                pass.action.colors[0].store_action = SG_STOREACTION_DONTCARE;
            }

            // plan_sokol.md SOKOL-26 MRT: slots 1..N-1, only ever populated when currentRenderTarget_
            // is the primary 2D target (mrtExtraTargets_ is never non-empty alongside a bound cube
            // face). Every slot shares the SAME pending clear colour as slot 0 -- ClearOptions has no
            // per-attachment granularity in XNA, so device.Clear() clears every active MRT attachment
            // identically, matching easygl_mrt_test.cpp's own "DiscardContents clears every active
            // MRT attachment" expectation.
            for (std::size_t i = 0; i < mrtExtraTargets_.size(); ++i)
            {
                SokolRenderTargetRenderer* extra = mrtExtraTargets_[i];
                const int slot = static_cast<int>(i) + 1;
                pass.action.colors[slot].load_action =
                    pendingClearColor_ ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
                pass.action.colors[slot].store_action = SG_STOREACTION_STORE;
                pass.action.colors[slot].clear_value.r = pendingClear_[0];
                pass.action.colors[slot].clear_value.g = pendingClear_[1];
                pass.action.colors[slot].clear_value.b = pendingClear_[2];
                pass.action.colors[slot].clear_value.a = pendingClear_[3];
                pass.attachments.colors[slot] = MakeViewHandle(extra->GetColorAttachmentViewIdEXT());
                const std::uint32_t extraResolveId = extra->GetResolveAttachmentViewIdEXT();
                if (extraResolveId != 0)
                {
                    pass.attachments.resolves[slot] = MakeViewHandle(extraResolveId);
                    pass.action.colors[slot].store_action = SG_STOREACTION_DONTCARE;
                }
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

    void SokolRenderer::EndPassIfActive()
    {
        if (!passActive_) return;
        sg_end_pass();
        passActive_ = false;
    }

    void SokolRenderer::ApplyPendingViewportAndScissor()
    {
        if (!passActive_) return;

#if CNA_SOKOL_HAS_GL_READBACK
        // See SetViewport's own doc comment: sokol_gfx has no viewport depth-range call, so this
        // reaches past it, GL-only, the same escape hatch ReadBackbuffer/SokolOcclusionQueryRenderer
        // already use.
        glDepthRangef(viewportMinDepth_, viewportMaxDepth_);
#endif

        if (currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr)
        {
            // A render target's pixel space IS logical space -- no window letterbox scaling
            // applies to it at all (GraphicsDevice::ResetViewportAndScissorForRenderTarget already
            // resets the public Viewport/ScissorRectangle to the target's own size in target-pixel
            // units on every bind, so any SetViewport/SetScissorRect this renderer receives while a
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

    void SokolRenderer::GetCurrentTargetSizeEXT(int& width, int& height) const
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

    bool SokolRenderer::CurrentPassHasDepthStencilAttachmentEXT() const
    {
        if (currentRenderTargetCube_ != nullptr)
            return currentRenderTargetCube_->GetDepthStencilAttachmentViewIdEXT() != 0;
        if (currentRenderTarget_ != nullptr)
            return currentRenderTarget_->GetDepthStencilAttachmentViewIdEXT() != 0;
        return true;
    }

    int SokolRenderer::CurrentPassSampleCountEXT() const
    {
        // RenderTargetCube is never multisampled -- sokol_gfx's own validation layer hard-rejects
        // a CUBE image with sample_count > 1 (see CreateRenderTargetCube's comment).
        if (currentRenderTargetCube_ != nullptr) return 1;
        if (currentRenderTarget_ != nullptr)
        {
            const int rtSampleCount = currentRenderTarget_->GetMultiSampleCount();
            return rtSampleCount > 1 ? rtSampleCount : 1;
        }
        return sampleCount_;
    }

    int SokolRenderer::CurrentPassColorAttachmentCountEXT() const
    {
        // mrtExtraTargets_ is only ever populated alongside currentRenderTarget_ (slot 0) -- a
        // RenderTargetCube face combined with other targets in one set is not implemented (see
        // SetRenderTargets's own comment), and the swapchain is never MRT.
        return 1 + static_cast<int>(mrtExtraTargets_.size());
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- clears and present
    // ---------------------------------------------------------------------------------------

    void SokolRenderer::Clear(float r, float g, float b, float a)
    {
        QueueClear(true, r, g, b, a, false, 1.0f, false, 0);
    }

    void SokolRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        QueueClear(true, r, g, b, a, true, depth, false, 0);
    }

    void SokolRenderer::ClearDepth(float depth)
    {
        QueueClear(false, 0, 0, 0, 0, true, depth, false, 0);
    }

    void SokolRenderer::ClearStencil(int stencil)
    {
        QueueClear(false, 0, 0, 0, 0, false, 1.0f, true, stencil);
    }

    void SokolRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        QueueClear(false, 0, 0, 0, 0, true, depth, true, stencil);
    }

    void SokolRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        QueueClear(true, r, g, b, a, false, 1.0f, true, stencil);
    }

    void SokolRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        QueueClear(true, r, g, b, a, true, depth, true, stencil);
    }

    void SokolRenderer::Present()
    {
        // Begin before ending so a frame whose only content was a Clear() still produces the pass
        // that actually performs it.
        BeginPassIfNeeded();
        EndPassIfActive();
        sg_commit();
        platformContext_->SwapBuffers();
        // plan_sokol.md SOKOL-24: sg_commit() is sokol_gfx's own frame boundary (it is what
        // advances the internal frame counter sg_update_buffer()'s once-per-frame rule is
        // checked against) -- advance this renderer's own counter right alongside it.
        ++frameIndexEXT_;
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- presentation geometry
    // ---------------------------------------------------------------------------------------

    void SokolRenderer::GetPhysicalSizeEXT(int& width, int& height) const
    {
        surface_.GetDrawableSize(width, height);
    }

    void SokolRenderer::GetLogicalSizeEXT(int& width, int& height) const
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

    void SokolRenderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSizeEXT(width, height);
    }

    void SokolRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void SokolRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SokolRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void SokolRenderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    bool SokolRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);
        if (physicalHeight <= 0) return false;

        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physicalHeight);
        logX = surface_.WindowToDrawable(windowX) * scale;
        logY = surface_.WindowToDrawable(windowY) * scale;
        return true;
    }

    bool SokolRenderer::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSizeEXT(physicalWidth, physicalHeight);
        if (physicalHeight <= 0) return false;

        const float inverseScale = static_cast<float>(physicalHeight) / static_cast<float>(virtualHeight_);
        windowX = surface_.DrawableToWindow(logX * inverseScale);
        windowY = surface_.DrawableToWindow(logY * inverseScale);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- resource creation
    // ---------------------------------------------------------------------------------------

    std::unique_ptr<ITextureRenderer> SokolRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SokolTextureRenderer>(data);
    }

    std::unique_ptr<ITextureCubeRenderer> SokolRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        (void)surfaceFormat;  // always stored as RGBA8, matching CreateTexture
        return std::make_unique<SokolTextureCubeRenderer>(size, mipMap);
    }

    std::unique_ptr<ITexture3DRenderer> SokolRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        (void)surfaceFormat;  // always stored as RGBA8, matching CreateTexture
        return std::make_unique<SokolTexture3DRenderer>(w, h, depth, mipMap);
    }

    std::unique_ptr<IOcclusionQueryRenderer> SokolRenderer::CreateOcclusionQuery()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        return std::make_unique<SokolOcclusionQueryRenderer>(this);
#else
        return nullptr;
#endif
    }

    bool SokolRenderer::TryActivateOcclusionQueryEXT(SokolOcclusionQueryRenderer* query)
    {
        if (activeOcclusionQueryEXT_ != nullptr && activeOcclusionQueryEXT_ != query) return false;
        activeOcclusionQueryEXT_ = query;
        return true;
    }

    void SokolRenderer::ReleaseOcclusionQueryEXT(SokolOcclusionQueryRenderer* query)
    {
        if (activeOcclusionQueryEXT_ == query) activeOcclusionQueryEXT_ = nullptr;
    }

    std::unique_ptr<IEffectRenderer> SokolRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<SokolEffectRenderer>();
        renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    std::unique_ptr<ISpriteBatchRenderer> SokolRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SokolSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IVertexBufferRenderer> SokolRenderer::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<SokolVertexBufferRenderer>(vertexCapacity, this);
    }

    std::unique_ptr<IIndexBufferRenderer> SokolRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<SokolIndexBufferRenderer>(indexCapacity, false, this);
    }

    std::unique_ptr<IIndexBufferRenderer> SokolRenderer::CreateIndexBuffer32(int indexCapacity)
    {
        return std::make_unique<SokolIndexBufferRenderer>(indexCapacity, true, this);
    }

    std::unique_ptr<IRenderTargetRenderer> SokolRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // preserveContents is intentionally unused -- see this method's own header doc and
        // BeginPassIfNeeded's identical comment for why every bind already behaves that way.
        (void)preserveContents;
        // multiSampleCount is real as of plan_sokol.md SOKOL-26: SokolRenderTargetRenderer clamps it
        // to the driver's GL_MAX_SAMPLES and allocates the matching MSAA colour (and, when a depth
        // format was requested, depth-stencil) image plus a resolve target, following sokol_gfx.h's
        // own documented offscreen-MSAA workflow. GetMultiSampleCount() reports the real, clamped
        // value, matching every other renderer's own convention. mipMap is real as of SOKOL-39.

        return std::make_unique<SokolRenderTargetRenderer>(w, h, depthFormat != 0, multiSampleCount,
                                                           mipMap);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SokolRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // See CreateRenderTarget2D's identical reasoning for preserveContents. multiSampleCount is
        // always ignored here, unlike RenderTarget2D's real MSAA support (plan_sokol.md SOKOL-26):
        // sokol_gfx's own validation layer hard-rejects a CUBE image with sample_count > 1
        // (VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE) -- a permanent API boundary, not a
        // "not implemented yet" gap, matching WebGPU's/D3D9's own declared cube-MSAA boundaries.
        // mipMap is real as of SOKOL-39.
        (void)preserveContents;
        (void)multiSampleCount;

        return std::make_unique<SokolRenderTargetCubeRenderer>(size, depthFormat != 0, mipMap);
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- state
    // ---------------------------------------------------------------------------------------

    void SokolRenderer::RegenerateOutgoingMipsIfNeededEXT()
    {
        // Regenerate the OUTGOING target's mip chain now that its content is flushed
        // (plan_sokol.md SOKOL-39), mirroring EasyGLRenderTargetRenderer's own "auto-generate on
        // unbind" contract. No-op on a target that was not created with mipMap=true.
        if (currentRenderTarget_ != nullptr) currentRenderTarget_->RegenerateMipmapsIfNeededEXT();
        if (currentRenderTargetCube_ != nullptr) currentRenderTargetCube_->RegenerateMipmapsIfNeededEXT();
        for (SokolRenderTargetRenderer* extra : mrtExtraTargets_)
            extra->RegenerateMipmapsIfNeededEXT();
    }

    void SokolRenderer::BindSingleRenderTarget2D(SokolRenderTargetRenderer* rt)
    {
        if (currentRenderTarget_ == rt && currentRenderTargetCube_ == nullptr
            && mrtExtraTargets_.empty())
            return;
        // Mirrors Present()'s identical "begin before ending" comment: a Clear() queued against the
        // target about to be replaced, with no draw ever following it, never called
        // BeginPassIfNeeded() at all -- with no pass ever opened, EndPassIfActive() alone would
        // silently drop the pending clear instead of ever reaching the GPU (it only closes an
        // ALREADY-open pass). Realizing it here makes "bind, Clear(), unbind" alone a legal, visible
        // producer, matching every other renderer's own Clear() semantics.
        if (pendingClearColor_ || pendingClearDepth_ || pendingClearStencil_)
            BeginPassIfNeeded();
        EndPassIfActive();
        RegenerateOutgoingMipsIfNeededEXT();
        currentRenderTarget_ = rt;
        currentRenderTargetCube_ = nullptr;
        mrtExtraTargets_.clear();
        // Any pending Clear* from before this bind belongs to whatever was previously active
        // (the back buffer, or a different target); GraphicsDevice always issues its own
        // DiscardContents Clear() AFTER this call returns (see GraphicsDevice::SetRenderTarget(s)'s
        // own sequencing), so nothing here should carry a stale pending clear into the new target.
        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;
    }

    void SokolRenderer::BindRenderTargetCubeFace(SokolRenderTargetCubeRenderer* rt, int face)
    {
        if (currentRenderTargetCube_ == rt && currentRenderTargetCubeFace_ == face
            && currentRenderTarget_ == nullptr && mrtExtraTargets_.empty())
            return;
        // See BindSingleRenderTarget2D's identical comment.
        if (pendingClearColor_ || pendingClearDepth_ || pendingClearStencil_)
            BeginPassIfNeeded();
        EndPassIfActive();
        RegenerateOutgoingMipsIfNeededEXT();
        currentRenderTarget_ = nullptr;
        currentRenderTargetCube_ = rt;
        currentRenderTargetCubeFace_ = face;
        mrtExtraTargets_.clear();
        // See BindSingleRenderTarget2D's identical comment.
        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;
    }

    void SokolRenderer::BindRenderTargets2D(const std::vector<SokolRenderTargetRenderer*>& targets)
    {
        // targets.size() >= 2 always -- SetRenderTargets already special-cases count == 1.
        if (currentRenderTarget_ == targets[0] && currentRenderTargetCube_ == nullptr
            && mrtExtraTargets_.size() == targets.size() - 1
            && std::equal(mrtExtraTargets_.begin(), mrtExtraTargets_.end(), targets.begin() + 1))
            return;
        // See BindSingleRenderTarget2D's identical comment.
        if (pendingClearColor_ || pendingClearDepth_ || pendingClearStencil_)
            BeginPassIfNeeded();
        EndPassIfActive();
        RegenerateOutgoingMipsIfNeededEXT();
        currentRenderTarget_ = targets[0];
        currentRenderTargetCube_ = nullptr;
        mrtExtraTargets_.assign(targets.begin() + 1, targets.end());
        // See BindSingleRenderTarget2D's identical comment.
        pendingClearColor_ = false;
        pendingClearDepth_ = false;
        pendingClearStencil_ = false;
    }

    void SokolRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        BindSingleRenderTarget2D(static_cast<SokolRenderTargetRenderer*>(rt));
    }

    void SokolRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                int count)
    {
        if (renderTargets == nullptr || count == 0)
        {
            BindSingleRenderTarget2D(nullptr);
            return;
        }
        if (count == 1)
        {
            if (renderTargets[0].IsRenderTargetCubeFace())
            {
                BindRenderTargetCubeFace(
                    static_cast<SokolRenderTargetCubeRenderer*>(renderTargets[0].GetRenderTargetCube()),
                    renderTargets[0].GetCubeFace());
                return;
            }

            BindSingleRenderTarget2D(
                static_cast<SokolRenderTargetRenderer*>(renderTargets[0].GetRenderTarget2D()));
            return;
        }

        // plan_sokol.md SOKOL-26 MRT: RenderTarget2D-only, matching EasyGLRenderer's own
        // choice to reject a RenderTargetCube face inside a multi-slot set entirely (dimensions,
        // applied-sample-count and duplicate-subresource checks already happened generically in
        // GraphicsDevice::SetRenderTargets before this renderer is ever reached).
        std::vector<SokolRenderTargetRenderer*> targets;
        targets.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                NotYetImplemented(kRendererName,
                    "a RenderTargetCube face bound alongside other render targets in one MRT set");
            targets.push_back(static_cast<SokolRenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D()));
        }
        BindRenderTargets2D(targets);
    }

    void SokolRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
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
        colorWriteChannels_[0] = writeState.colorWriteChannels[0];
        colorWriteChannels_[1] = writeState.colorWriteChannels[1];
        colorWriteChannels_[2] = writeState.colorWriteChannels[2];
        colorWriteChannels_[3] = writeState.colorWriteChannels[3];

        // XNA has no separate "blending on" switch: Opaque is expressed as One/Zero/Add, which is
        // exactly a disabled blend unit, so it is detected rather than requiring a second call.
        const bool isOpaque = colorSrcBlend == 0 && colorDstBlend == 1
                           && alphaSrcBlend == 0 && alphaDstBlend == 1
                           && colorBlendFunc == 0 && alphaBlendFunc == 0;
        blendEnabled_ = !isOpaque;

        // BlendState.MultiSampleMask has no sokol_gfx equivalent -- sokol exposes alpha-to-coverage
        // but no per-sample coverage mask -- so a non-default mask is intentionally not honoured
        // here. Recorded as a known gap in docs/sokol-renderer.md rather than silently pretended.
    }

    void SokolRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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

    void SokolRenderer::ApplyRasterizerState(int cullMode, int fillMode,
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

        // Real as of SOKOL-23: DrawColored3D reads fillMode_ and, when WireFrame, re-expands the
        // draw's triangles into a LINES-topology draw (BuildWireframeLineIndicesEXT()'s own doc
        // comment) -- the same technique EasyGLRenderer::DrawWireframe() uses, ported here
        // despite sokol_gfx exposing no native polygon-fill-mode API at all. Only reaches the
        // stock 3D pipeline; SpriteBatch never requests WireFrame (matches XNA), and a custom
        // ShaderEffect draw's own topology is the caller's responsibility, same as raw GL.
    }

    void SokolRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
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

    void SokolRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void SokolRenderer::SetReferenceStencil(int value) { referenceStencil_ = value; }

    void SokolRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        if (passActive_) ApplyPendingViewportAndScissor();
    }

    void SokolRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportSet_ = true;
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;

        // sokol_gfx's own viewport call (sg_apply_viewport) carries no depth-range parameter, but
        // the raw GL context underneath it is already reached for other purposes
        // (SokolOcclusionQueryRenderer, ReadBackbuffer) -- glDepthRangef is the same escape hatch,
        // applied in ApplyPendingViewportAndScissor alongside the viewport/scissor rect it already
        // reapplies on every bind and every SetViewport/SetScissorRect call. GL-only, matching
        // ReadBackbuffer's own boundary: ignored (stays at the GL default 0..1) on any non-GL
        // CNA_SOKOL_API. Unlike glBeginQuery/glEndQuery, glDepthRangef never raises a GL error for
        // any input (the spec clamps both values into [0,1] instead), so it needs none of
        // SokolOcclusionQueryRenderer's pending-error guarding.
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;

        if (passActive_) ApplyPendingViewportAndScissor();
    }

    void SokolRenderer::SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }

    void SokolRenderer::SetBlendEnabled(bool enabled) { blendEnabled_ = enabled; }

    void SokolRenderer::SetDepthWriteEnabled(bool enabled) { depthWriteEnabled_ = enabled; }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- caches and sprite drawing
    // ---------------------------------------------------------------------------------------

    std::uint32_t SokolRenderer::GetSpritePipeline()
    {
        // Needed here too, not just for the 3D pipelines -- a SpriteBatch draw into a depth-less
        // render target hits the exact same sokol_gfx pipeline/pass pixel-format mismatch otherwise.
        const bool hasDepthAttachment = CurrentPassHasDepthStencilAttachmentEXT();
        // Likewise: a SpriteBatch draw into a multisampled RenderTarget2D hits the identical
        // sample_count mismatch the 3D pipelines guard against (plan_sokol.md SOKOL-26).
        const int sampleCount = CurrentPassSampleCountEXT();
        // plan_sokol.md SOKOL-26 MRT: a SpriteBatch draw into a bound multi-render-target set hits
        // the identical color_count mismatch the 3D pipelines guard against -- see this pipeline's
        // own desc.color_count/desc.colors[] loop below.
        const int colorAttachmentCount = CurrentPassColorAttachmentCountEXT();

        const PipelineKey key{
            blendColorSrc_, blendAlphaSrc_, blendColorDst_, blendAlphaDst_,
            blendColorFunc_, blendAlphaFunc_, colorWriteChannels_[0],
            blendEnabled_, depthTestEnabled_, depthWriteEnabled_, depthFunc_, hasDepthAttachment,
            sampleCount, colorAttachmentCount, PackBlendFactorEXT(blendFactor_)};

        if (const auto found = pipelineCache_.find(key); found != pipelineCache_.end())
            return found->second;

        sg_pipeline_desc desc = {};
        desc.shader = MakeShaderHandle(spriteShaderId_);
        desc.layout.buffers[0].stride = static_cast<int>(sizeof(SokolSpriteBatchRenderer::Vertex));
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
        // plan_sokol.md SOKOL-26 MRT: sokol_gfx requires color_count to exactly match the active
        // pass's real attachment count. sprite3d.glsl (like every stock fragment shader) has only
        // ever declared output location 0, so slots 1..N-1 simply receive no write from this
        // pipeline -- matching EasyGLRenderer's identical "the 2D pipeline writes colour
        // attachment 0 only" MRT behaviour (see rendertarget_pass_boundary_test.cpp's own M2 check).
        desc.color_count = key.colorAttachmentCount;
        for (int slot = 0; slot < key.colorAttachmentCount; ++slot)
        {
            desc.colors[slot].pixel_format = SG_PIXELFORMAT_RGBA8;
            desc.colors[slot].write_mask = ToColorMask(key.colorWriteChannels);
            desc.colors[slot].blend.enabled = key.blendEnabled;
            desc.colors[slot].blend.src_factor_rgb = ToBlendFactor(key.colorSrcBlend);
            desc.colors[slot].blend.dst_factor_rgb = ToBlendFactor(key.colorDstBlend);
            desc.colors[slot].blend.op_rgb = ToBlendOp(key.colorBlendFunc);
            desc.colors[slot].blend.src_factor_alpha = ToBlendFactor(key.alphaSrcBlend);
            desc.colors[slot].blend.dst_factor_alpha = ToBlendFactor(key.alphaDstBlend);
            desc.colors[slot].blend.op_alpha = ToBlendOp(key.alphaBlendFunc);
        }
        desc.blend_color.r = blendFactor_[0];
        desc.blend_color.g = blendFactor_[1];
        desc.blend_color.b = blendFactor_[2];
        desc.blend_color.a = blendFactor_[3];
        desc.label = "cna_sprite_pipeline";

        const std::uint32_t pipelineId = sg_make_pipeline(&desc).id;
        if (sg_query_pipeline_state(MakePipelineHandle(pipelineId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: sprite pipeline creation failed");

        pipelineCache_.emplace(key, pipelineId);
        return pipelineId;
    }

    std::uint32_t SokolRenderer::GetSampler(int filter, int addressU, int addressV,
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
            throw std::runtime_error("Sokol renderer: sampler creation failed");

        samplerCache_.emplace(key, samplerId);
        return samplerId;
    }

    void SokolRenderer::DrawSpriteRunEXT(
        const ITextureRenderer& texture,
        const std::vector<SokolSpriteBatchRenderer::Vertex>& vertices,
        const Matrix& transform,
        int filter, int addressU, int addressV,
        Effect* customEffect)
    {
        if (vertices.empty()) return;

        BeginPassIfNeeded();

        const std::size_t byteCount = vertices.size() * sizeof(SokolSpriteBatchRenderer::Vertex);
        const sg_buffer vertexBuffer = MakeBufferHandle(spriteVertexBufferId_);
        if (sg_query_buffer_will_overflow(vertexBuffer, byteCount))
        {
            throw std::runtime_error(
                "Sokol renderer: exceeded the per-frame sprite capacity of "
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

        const int quadCount = static_cast<int>(vertices.size()) / kSpriteVerticesPerQuad;

#if CNA_SOKOL_HAS_GL_READBACK
        // plan_sokol.md SOKOL-28: a custom ShaderEffect bound via SpriteBatch.Begin(..., effect)
        // bypasses sg_pipeline/sg_bindings entirely, the same raw-GL-bracketed-by-
        // sg_reset_state_cache() shape DrawCustomEffect3D uses for the 3D draw path -- see
        // SokolEffectRenderer's own doc comment for why. Binds the SAME compiled program the
        // Effect itself owns (Effect::GetEffectRendererPtr()) rather than a second, independent
        // copy, so any ShaderEffect::SetUniformXxx() call the caller already made (which writes to
        // that program) is visible to this draw -- matching EasyGLSpriteBatchRenderer::FlushBatch's
        // own "bind the SAME compiled program the Effect itself owns" reasoning.
        SokolEffectRenderer* effectRenderer = nullptr;
        if (customEffect != nullptr)
        {
            effectRenderer = dynamic_cast<SokolEffectRenderer*>(customEffect->GetEffectRendererPtr());
            if (effectRenderer != nullptr && !effectRenderer->IsValid())
                effectRenderer = nullptr;
        }
        if (effectRenderer != nullptr)
        {
            sg_reset_state_cache();

            effectRenderer->Bind();
            float projCM[16];
            combined.ToColumnMajor(projCM);
            effectRenderer->SetUniformMat4("projection", projCM);

            const std::uint32_t imageId = ResolveSampledTextureImageId(texture, "SpriteBatch");
            const sg_gl_image_info imageInfo = sg_gl_query_image_info(MakeImageHandle(imageId));
            const GLuint texId = imageInfo.tex[imageInfo.active_slot];
            const sg_gl_sampler_info samplerInfo =
                sg_gl_query_sampler_info(MakeSamplerHandle(GetSampler(filter, addressU, addressV, 1)));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            glBindSampler(0, samplerInfo.smp);
            // Realizes any extra ShaderEffect::SetTexture(unit>0, ...) calls the caller made before
            // SpriteBatch.End()/this flush -- SetTexture()'s own doc comment names SpriteBatch as a
            // caller of this, but before plan_sokol.md SOKOL-41 it was never actually invoked here.
            // Unit 0 above is authoritative for the sprite's own primary texture; a caller is not
            // expected to SetTexture(0, ...) on a SpriteBatch-bound effect (see SetTexture()'s own
            // "unit 0 is normally driven by the caller" doc comment), so no ordering conflict.
            effectRenderer->ApplyPendingTextureBindsEXT();

            // plan_sokol.md SOKOL-41: full BlendState/DepthStencilState/RasterizerState, not just
            // the texture/program bind -- this raw-GL draw bypasses sg_pipeline entirely, so none
            // of it was ever applied to anything for it before, unlike GetSpritePipeline's own
            // sg_pipeline_desc.
            ApplyCustomEffectRasterStateEXT();
            ApplyCustomEffectColorMasksEXT();

            if (customEffectVaoId_ == 0) glGenVertexArrays(1, &customEffectVaoId_);
            glBindVertexArray(customEffectVaoId_);

            const sg_gl_buffer_info vbInfo = sg_gl_query_buffer_info(vertexBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, vbInfo.buf[vbInfo.active_slot]);
            // Matches easygl_shader_effect_test.cpp's own expected attribute contract exactly:
            // location 0 = vec2 aPos, 1 = vec2 aTexCoord, 2 = vec4 aColor -- the same layout
            // EasyGLSpriteBatchRenderer's own VAO uses for both its built-in and custom-effect
            // sprite draws, so a GLSL source already written against that renderer's convention
            // works unmodified here too.
            constexpr auto kVertexStride = static_cast<GLsizei>(sizeof(SokolSpriteBatchRenderer::Vertex));
            const auto base = static_cast<std::uintptr_t>(vertexOffset);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kVertexStride,
                                  reinterpret_cast<const void*>(base + offsetof(SokolSpriteBatchRenderer::Vertex, x)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kVertexStride,
                                  reinterpret_cast<const void*>(base + offsetof(SokolSpriteBatchRenderer::Vertex, u)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kVertexStride,
                                  reinterpret_cast<const void*>(base + offsetof(SokolSpriteBatchRenderer::Vertex, r)));

            const sg_gl_buffer_info ibInfo =
                sg_gl_query_buffer_info(MakeBufferHandle(spriteIndexBufferId_));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibInfo.buf[ibInfo.active_slot]);
            glDrawElements(GL_TRIANGLES, quadCount * kSpriteIndicesPerQuad, GL_UNSIGNED_SHORT, nullptr);

            ResetCustomEffectColorMasksEXT();
            glBindVertexArray(0);
            sg_reset_state_cache();
            return;
        }
#else
        (void)customEffect;
#endif

        const std::uint32_t textureViewId = ResolveSampledTextureViewId(texture, "SpriteBatch");

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

        cna_sprite_fs_params_t fsParams{};
        fsParams.rtFlipV = IsRenderTargetSourceEXT(texture) ? 1.0f : 0.0f;
        sg_range fsRange{&fsParams, sizeof(fsParams)};
        sg_apply_uniforms(UB_cna_sprite_fs_params, &fsRange);

        sg_draw(0, quadCount * kSpriteIndicesPerQuad, 1);
    }

    // ---------------------------------------------------------------------------------------
    // SokolRenderer -- readback, 3D stubs, capabilities
    // ---------------------------------------------------------------------------------------

    void SokolRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("Sokol renderer: ReadBackbuffer called with an empty region");

        const bool boundToCustomTarget =
            currentRenderTarget_ != nullptr || currentRenderTargetCube_ != nullptr;

        // BeginPassIfNeeded() first, for the same reason Present() does it: a Clear() only records
        // a pending pass action, so a Clear()-then-read-back sequence with nothing drawn in
        // between would otherwise read whatever the previous frame's buffer swap left behind.
        // Ending the pass then flushes everything, and glFinish waits for the driver to actually
        // produce the pixels. sg_commit() is deliberately not called -- it would reset the sprite
        // streaming buffer's append offset mid-frame, and a readback is not the end of a frame.
        BeginPassIfNeeded();
        EndPassIfActive();
        glFinish();

        // GraphicsDevice.GetBackBufferData reads from whatever is CURRENTLY BOUND, matching the
        // established cross-renderer convention in this codebase (EasyGLRenderer::
        // ReadBackbuffer reads from whatever FBO is bound_; the DIRECTX3/ASCII/Software renderers document
        // the identical behaviour) -- not "always the literal presented window surface", which an
        // earlier version of this method assumed and refused to honour instead. sokol_gfx's own GL
        // renderer does not rebind GL_FRAMEBUFFER back to 0 inside sg_end_pass() for an offscreen
        // pass (only GLES3 restores it, and only after an MSAA resolve, to the SAME pass FBO) --
        // so after EndPassIfActive() above, the currently bound GL framebuffer is exactly the
        // render target's own FBO (or, for a multisampled target, sg_end_pass()'s own MSAA-resolve
        // blit target), and a plain glReadPixels below reads its content correctly with no extra
        // binding call needed.
        int physicalWidth = 0;
        int physicalHeight = 0;
        double scale = 1.0;
        if (boundToCustomTarget)
        {
            // A render target's pixel space IS logical space -- no window letterbox scaling
            // applies, mirroring ApplyPendingViewportAndScissor's identical reasoning.
            GetCurrentTargetSizeEXT(physicalWidth, physicalHeight);
        }
        else
        {
            GetPhysicalSizeEXT(physicalWidth, physicalHeight);

            int logicalWidth = 0;
            int logicalHeight = 0;
            GetLogicalSizeEXT(logicalWidth, logicalHeight);

            scale = (logicalHeight > 0)
                ? static_cast<double>(physicalHeight) / static_cast<double>(logicalHeight)
                : 1.0;
        }
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
        NotYetImplemented(kRendererName, "ReadBackbuffer on a non-GL CNA_SOKOL_API");
#endif
    }

    std::uint32_t SokolRenderer::Get3DPipeline(const Pipeline3DKey& key)
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

            case Shader3DKind::DualTextured:
                // Same attribute shape as Textured: position + texcoord0, optional trailing color0.
                desc.shader = MakeShaderHandle(dualTextured3dShaderId_);
                desc.layout.attrs[ATTR_cna_dualtextured3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_dualtextured3d_position].offset = key.positionOffset;
                desc.layout.attrs[ATTR_cna_dualtextured3d_texcoord0].format =
                    static_cast<sg_vertex_format>(key.texCoordFormat);
                desc.layout.attrs[ATTR_cna_dualtextured3d_texcoord0].offset = key.texCoordOffset;
                if (key.colorOffset >= 0)
                {
                    desc.layout.attrs[ATTR_cna_dualtextured3d_color0].format =
                        static_cast<sg_vertex_format>(key.colorFormat);
                    desc.layout.attrs[ATTR_cna_dualtextured3d_color0].offset = key.colorOffset;
                }
                break;

            case Shader3DKind::Skinned:
                // Position, normal, texcoord0, blendweight0 and blendindices0 are all required in
                // the declaration for this kind (checked in DrawColored3D before a key is ever
                // built). color0 is declared LAST in skinned3d.glsl specifically so it lands in
                // the trailing attribute slot (see that file's own comment) -- sokol_gfx requires
                // every valid attribute slot to be a continuous prefix with no gap, and color0 is
                // the only one of these five that may legitimately be absent.
                desc.shader = MakeShaderHandle(skinned3dShaderId_);
                desc.layout.attrs[ATTR_cna_skinned3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_skinned3d_position].offset = key.positionOffset;
                desc.layout.attrs[ATTR_cna_skinned3d_normal].format =
                    static_cast<sg_vertex_format>(key.normalFormat);
                desc.layout.attrs[ATTR_cna_skinned3d_normal].offset = key.normalOffset;
                desc.layout.attrs[ATTR_cna_skinned3d_texcoord0].format =
                    static_cast<sg_vertex_format>(key.texCoordFormat);
                desc.layout.attrs[ATTR_cna_skinned3d_texcoord0].offset = key.texCoordOffset;
                desc.layout.attrs[ATTR_cna_skinned3d_blendweight0].format =
                    static_cast<sg_vertex_format>(key.blendWeightFormat);
                desc.layout.attrs[ATTR_cna_skinned3d_blendweight0].offset = key.blendWeightOffset;
                desc.layout.attrs[ATTR_cna_skinned3d_blendindices0].format =
                    static_cast<sg_vertex_format>(key.blendIndicesFormat);
                desc.layout.attrs[ATTR_cna_skinned3d_blendindices0].offset = key.blendIndicesOffset;
                if (key.colorOffset >= 0)
                {
                    desc.layout.attrs[ATTR_cna_skinned3d_color0].format =
                        static_cast<sg_vertex_format>(key.colorFormat);
                    desc.layout.attrs[ATTR_cna_skinned3d_color0].offset = key.colorOffset;
                }
                break;

            case Shader3DKind::EnvMapped:
                // Position, normal and texcoord0 are all required in the declaration for this kind
                // (checked in DrawColored3D before a key is ever built) -- exactly 3 continuous
                // slots, no optional trailing color0 (envmap3d.glsl has no color0 attribute at
                // all; real XNA's EnvironmentMapEffect has no VertexColorEnabled property).
                desc.shader = MakeShaderHandle(envmap3dShaderId_);
                desc.layout.attrs[ATTR_cna_envmap3d_position].format =
                    static_cast<sg_vertex_format>(key.positionFormat);
                desc.layout.attrs[ATTR_cna_envmap3d_position].offset = key.positionOffset;
                desc.layout.attrs[ATTR_cna_envmap3d_normal].format =
                    static_cast<sg_vertex_format>(key.normalFormat);
                desc.layout.attrs[ATTR_cna_envmap3d_normal].offset = key.normalOffset;
                desc.layout.attrs[ATTR_cna_envmap3d_texcoord0].format =
                    static_cast<sg_vertex_format>(key.texCoordFormat);
                desc.layout.attrs[ATTR_cna_envmap3d_texcoord0].offset = key.texCoordOffset;
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
            // format regardless of the requested DepthFormat (docs/sokol-renderer.md), so a real
            // stencil plane exists whenever hasDepthAttachment does -- there is no depth-only case
            // to distinguish here, unlike D3D/Vulkan renderers that honour DepthFormat precisely.
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
        // plan_sokol.md SOKOL-26 MRT: see GetSpritePipeline's identical comment -- every stock 3D
        // fragment shader (colored3d/textured3d/lit3d/dualtextured3d/skinned3d/envmap3d.glsl) has
        // only ever declared output location 0, so slots 1..N-1 simply receive no write.
        desc.color_count = key.colorAttachmentCount;
        for (int slot = 0; slot < key.colorAttachmentCount; ++slot)
        {
            desc.colors[slot].pixel_format = SG_PIXELFORMAT_RGBA8;
            desc.colors[slot].write_mask = ToColorMask(key.colorWriteChannels);
            desc.colors[slot].blend.enabled = key.blendEnabled;
            desc.colors[slot].blend.src_factor_rgb = ToBlendFactor(key.colorSrcBlend);
            desc.colors[slot].blend.dst_factor_rgb = ToBlendFactor(key.colorDstBlend);
            desc.colors[slot].blend.op_rgb = ToBlendOp(key.colorBlendFunc);
            desc.colors[slot].blend.src_factor_alpha = ToBlendFactor(key.alphaSrcBlend);
            desc.colors[slot].blend.dst_factor_alpha = ToBlendFactor(key.alphaDstBlend);
            desc.colors[slot].blend.op_alpha = ToBlendOp(key.alphaBlendFunc);
        }
        desc.blend_color.r = blendFactor_[0];
        desc.blend_color.g = blendFactor_[1];
        desc.blend_color.b = blendFactor_[2];
        desc.blend_color.a = blendFactor_[3];
        desc.label = "cna_3d_pipeline";

        const std::uint32_t pipelineId = sg_make_pipeline(&desc).id;
        if (sg_query_pipeline_state(MakePipelineHandle(pipelineId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: 3D pipeline creation failed");

        pipeline3dCache_.emplace(key, pipelineId);
        return pipelineId;
    }

    std::uint32_t SokolRenderer::GetInstanced3DPipeline(const PipelineInstanced3DKey& key)
    {
        if (const auto found = pipelineInstanced3dCache_.find(key);
            found != pipelineInstanced3dCache_.end())
            return found->second;

        sg_pipeline_desc desc = {};
        desc.shader = MakeShaderHandle(instanced3dShaderId_);

        // Slot 0: per-vertex mesh data, step_func defaults to SG_VERTEXSTEP_PER_VERTEX -- only
        // Position is read (see instanced3d.glsl's own doc comment).
        desc.layout.attrs[ATTR_cna_instanced3d_position].buffer_index = 0;
        desc.layout.attrs[ATTR_cna_instanced3d_position].format =
            static_cast<sg_vertex_format>(key.positionFormat);
        desc.layout.attrs[ATTR_cna_instanced3d_position].offset = key.positionOffset;
        desc.layout.buffers[0].stride = key.stride;

        // Slot 1: per-instance World matrix, 4 column-major vec4 attributes at fixed offsets
        // (InstanceColumns is always exactly a mat4 -- there is no declaration to read here, unlike
        // slot 0's Position element).
        desc.layout.attrs[ATTR_cna_instanced3d_instCol0].buffer_index = 1;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol0].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol0].offset = 0;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol1].buffer_index = 1;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol1].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol1].offset = 16;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol2].buffer_index = 1;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol2].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol2].offset = 32;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol3].buffer_index = 1;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol3].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_cna_instanced3d_instCol3].offset = 48;
        desc.layout.buffers[1].stride = 64;
        desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        // REMED-GFX-202: XNA's VertexBufferBinding.InstanceFrequency is a step RATE, not a flag --
        // it advances the per-instance record once every N instances. sokol_gfx expresses it
        // natively (glVertexAttribDivisor on the GL renderers), so it is honoured rather than
        // refused; the key carries it because sokol_gfx bakes it into the pipeline.
        desc.layout.buffers[1].step_rate = key.instanceStepRate;

        desc.index_type = static_cast<sg_index_type>(key.indexType);
        desc.primitive_type = static_cast<sg_primitive_type>(key.primitiveType);
        desc.cull_mode = ToCullMode(key.cullMode);
        desc.face_winding = SG_FACEWINDING_CW;
        desc.sample_count = key.sampleCount;
        if (key.hasDepthAttachment)
        {
            desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            desc.depth.compare = key.depthTestEnabled ? ToCompareFunc(key.depthFunc) : SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = key.depthTestEnabled && key.depthWriteEnabled;

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
            desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
            desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
            desc.depth.write_enabled = false;
            desc.stencil.enabled = false;
        }
        desc.depth.bias = key.depthBias;
        desc.depth.bias_slope_scale = key.slopeScaleDepthBias;
        // plan_sokol.md SOKOL-26 MRT: see GetSpritePipeline's identical comment -- instanced3d.glsl
        // has only ever declared output location 0, so slots 1..N-1 simply receive no write.
        desc.color_count = key.colorAttachmentCount;
        for (int slot = 0; slot < key.colorAttachmentCount; ++slot)
        {
            desc.colors[slot].pixel_format = SG_PIXELFORMAT_RGBA8;
            desc.colors[slot].write_mask = ToColorMask(key.colorWriteChannels);
            desc.colors[slot].blend.enabled = key.blendEnabled;
            desc.colors[slot].blend.src_factor_rgb = ToBlendFactor(key.colorSrcBlend);
            desc.colors[slot].blend.dst_factor_rgb = ToBlendFactor(key.colorDstBlend);
            desc.colors[slot].blend.op_rgb = ToBlendOp(key.colorBlendFunc);
            desc.colors[slot].blend.src_factor_alpha = ToBlendFactor(key.alphaSrcBlend);
            desc.colors[slot].blend.dst_factor_alpha = ToBlendFactor(key.alphaDstBlend);
            desc.colors[slot].blend.op_alpha = ToBlendOp(key.alphaBlendFunc);
        }
        desc.blend_color.r = blendFactor_[0];
        desc.blend_color.g = blendFactor_[1];
        desc.blend_color.b = blendFactor_[2];
        desc.blend_color.a = blendFactor_[3];
        desc.label = "cna_instanced_3d_pipeline";

        const std::uint32_t pipelineId = sg_make_pipeline(&desc).id;
        if (sg_query_pipeline_state(MakePipelineHandle(pipelineId)) != SG_RESOURCESTATE_VALID)
            throw std::runtime_error("Sokol renderer: instanced-3D pipeline creation failed");

        pipelineInstanced3dCache_.emplace(key, pipelineId);
        return pipelineId;
    }

    SokolTextureRenderer& SokolRenderer::GetDefaultWhiteTexture()
    {
        if (!defaultWhiteTexture_)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            defaultWhiteTexture_ = std::make_unique<SokolTextureRenderer>(white);
        }
        return *defaultWhiteTexture_;
    }

    std::vector<std::uint32_t> SokolRenderer::BuildWireframeLineIndicesEXT(
        const SokolIndexBufferRenderer* ib, PrimitiveType primitive, int primitiveCount,
        int startIndex, int vertexStart)
    {
        std::vector<std::uint32_t> lines;
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            return lines;
        if (primitiveCount <= 0) return lines;

#if CNA_SOKOL_HAS_GL_READBACK
        const int vertexCount = ElementCountForPrimitives(primitive, primitiveCount);
        std::vector<std::uint32_t> src(static_cast<std::size_t>(vertexCount));
        if (ib != nullptr)
        {
            // This renderer keeps no CPU shadow of index data (unlike EasyGL's own
            // DrawWireframe(), which reads its own CPU-side copy) -- read the original values
            // straight off the GPU instead, the same GL-only escape hatch every other raw-GL
            // detour in this file already uses.
            const sg_gl_buffer_info ibInfo =
                sg_gl_query_buffer_info(MakeBufferHandle(ib->GetBufferIdEXT()));
            const GLuint ibGlId = ibInfo.buf[ibInfo.active_slot];
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibGlId);
            if (ib->IsThirtyTwoBit())
            {
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                                   static_cast<GLintptr>(startIndex) * 4,
                                   static_cast<GLsizeiptr>(vertexCount) * 4, src.data());
            }
            else
            {
                std::vector<std::uint16_t> src16(static_cast<std::size_t>(vertexCount));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                                   static_cast<GLintptr>(startIndex) * 2,
                                   static_cast<GLsizeiptr>(vertexCount) * 2, src16.data());
                for (int i = 0; i < vertexCount; ++i)
                    src[static_cast<std::size_t>(i)] = src16[static_cast<std::size_t>(i)];
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        else
        {
            // Non-indexed: vertex order is exactly sequential, the same "no ib -> firstVertex+pos"
            // fallback EasyGLRenderer::DrawWireframe() itself uses.
            for (int i = 0; i < vertexCount; ++i)
                src[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(vertexStart + i);
        }

        const auto edge = [&lines](std::uint32_t a, std::uint32_t b)
        {
            lines.push_back(a);
            lines.push_back(b);
        };
        if (primitive == PrimitiveType::TriangleList)
        {
            for (int t = 0; t < primitiveCount; ++t)
            {
                const std::uint32_t a = src[static_cast<std::size_t>(3 * t)];
                const std::uint32_t b = src[static_cast<std::size_t>(3 * t + 1)];
                const std::uint32_t c = src[static_cast<std::size_t>(3 * t + 2)];
                edge(a, b); edge(b, c); edge(c, a);
            }
        }
        else  // TriangleStrip: primitiveCount triangles over primitiveCount+2 vertices.
        {
            for (int t = 0; t < primitiveCount; ++t)
            {
                const std::uint32_t a = src[static_cast<std::size_t>(t)];
                const std::uint32_t b = src[static_cast<std::size_t>(t + 1)];
                const std::uint32_t c = src[static_cast<std::size_t>(t + 2)];
                edge(a, b); edge(b, c); edge(c, a);
            }
        }
#else
        (void)ib; (void)startIndex; (void)vertexStart;
#endif
        return lines;
    }

    void SokolRenderer::DrawColored3D(const IVertexBufferRenderer& vbIn,
                                             const IIndexBufferRenderer* ibIn,
                                             const Matrix& world,
                                             const Matrix& view,
                                             const Matrix& projection,
                                             PrimitiveType primitive,
                                             int primitiveCount,
                                             const GpuDrawParams& params)
    {
        if (primitiveCount <= 0) return;

        // Everything this renderer cannot shade yet is refused here rather than rendered as an
        // untextured, unlit approximation that would look plausible and be wrong. Plain texturing
        // (SOKOL-21), lighting (SOKOL-21), dual-texturing (SOKOL-33), skinning (SOKOL-35),
        // environment mapping (SOKOL-34) and custom ShaderEffect draws (SOKOL-28) are real below;
        // PBR is a distinct stock effect with its own vertex/uniform contract this renderer does
        // not implement yet.
        if (params.pbr)
        {
            NotYetImplemented(kRendererName, "PBR 3D draws");
        }
        if (params.instanceCount > 1)
            NotYetImplemented(kRendererName, "instanced draws");
        // REMED-GFX-201: this route binds exactly the one stream `vb` names. A declaration split
        // across several buffers would need every element re-slotted, which this renderer's
        // single-buffer pipeline layout cannot express, so it is refused before any pipeline is
        // built rather than rendered from stream 0 alone. GraphicsCapability::MultiStreamVertexInput
        // answers false, so the shared layer already rejects it one step earlier; this is the
        // renderer-side guarantee for a harness that calls Draw*PrimitivesEx directly.
        RejectUnsupportedStreamCombination(params, kRendererName);

        const auto& vb = static_cast<const SokolVertexBufferRenderer&>(vbIn);
        if (vb.GetBufferIdEXT() == 0 || vb.GetVertexCount() <= 0) return;

        const VertexDeclaration* declaration = vb.GetDeclarationEXT();
        // REMED-GFX-DECL-GUARD: pure, and before any pass, pipeline or binding exists, so a
        // refused draw leaves the device exactly as it was.
        RequireFaithfulDeclarationEXT(declaration, vb.GetStrideEXT(),
                                      ibIn != nullptr ? "ordinary-indexed" : "ordinary-nonindexed");

        // Resolved before the pipeline key is built: the index width is part of the pipeline.
        const SokolIndexBufferRenderer* ib = nullptr;
        if (ibIn != nullptr)
        {
            ib = static_cast<const SokolIndexBufferRenderer*>(ibIn);
            if (ib->GetBufferIdEXT() == 0 || ib->GetIndexCount() <= 0) return;
        }

        if (params.customEffectRenderer != nullptr)
        {
            DrawCustomEffect3D(vb, ib, declaration, world, view, projection, primitive,
                               primitiveCount, params);
            return;
        }

        const Shader3DKind kind = params.envMapping      ? Shader3DKind::EnvMapped
                                 : params.skinned         ? Shader3DKind::Skinned
                                 : params.lightingEnabled ? Shader3DKind::Lit
                                 : params.dualTexture     ? Shader3DKind::DualTextured
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
        key.colorWriteChannels = colorWriteChannels_[0];
        key.blendEnabled = blendEnabled_;
        key.depthTestEnabled = depthTestEnabled_;
        key.depthWriteEnabled = depthWriteEnabled_;
        key.depthFunc = depthFunc_;
        key.cullMode = cullMode_;
        key.hasDepthAttachment = CurrentPassHasDepthStencilAttachmentEXT();
        key.sampleCount = CurrentPassSampleCountEXT();
        key.colorAttachmentCount = CurrentPassColorAttachmentCountEXT();
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
        key.blendFactorPacked = PackBlendFactorEXT(blendFactor_);
        // plan_sokol.md SOKOL-23: RasterizerState.FillMode == WireFrame draws the exact same
        // vertex layout/shader through a LINES-topology pipeline instead of the requested
        // triangle one -- see BuildWireframeLineIndicesEXT()'s own doc comment for why sokol_gfx
        // needing no native polygon-fill-mode API at all makes this possible. The scratch index
        // buffer built below is always 32-bit, regardless of the source's own width.
        const bool wireframeActive = fillMode_ == 1  // FillMode::WireFrame
            && (primitive == PrimitiveType::TriangleList
                || primitive == PrimitiveType::TriangleStrip);
        key.primitiveType = static_cast<int>(
            wireframeActive ? SG_PRIMITIVETYPE_LINES : ToPrimitiveType(primitive));
        key.indexType = static_cast<int>(
            wireframeActive  ? SG_INDEXTYPE_UINT32
            : ib == nullptr ? SG_INDEXTYPE_NONE
                            : (ib->IsThirtyTwoBit() ? SG_INDEXTYPE_UINT32 : SG_INDEXTYPE_UINT16));
        key.positionOffset = -1;
        key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.colorOffset = -1;
        key.colorFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.texCoordOffset = -1;
        key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.normalOffset = -1;
        key.normalFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.blendWeightOffset = -1;
        key.blendWeightFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);
        key.blendIndicesOffset = -1;
        key.blendIndicesFormat = static_cast<int>(SG_VERTEXFORMAT_INVALID);

        // VertexBuffer's own CNAEXT-tagged auto-detect constructor (VertexBuffer(device, count),
        // used by GraphicsDevice::SetVertexBuffer callers who never pass an explicit
        // VertexDeclaration) stores a default-constructed, ELEMENT-LESS VertexDeclaration --
        // UploadValidatedData still calls SetVertexDeclaration with it before every real upload
        // (GFX-043), so this renderer sees hasDeclaration()==true with nothing usable inside. That
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
                else if (usage == VertexElementUsage::BlendWeight && key.blendWeightOffset < 0)
                {
                    key.blendWeightOffset = element.getOffsetProperty();
                    key.blendWeightFormat = static_cast<int>(format);
                }
                else if (usage == VertexElementUsage::BlendIndices && key.blendIndicesOffset < 0)
                {
                    key.blendIndicesOffset = element.getOffsetProperty();
                    key.blendIndicesFormat = static_cast<int>(format);
                }
            }
        }
        else
        {
            // Reached with no declaration at all (GraphicsDevice's typed, no-explicit-declaration
            // DrawUserPrimitives()/DrawUserIndexedPrimitives() overloads for VertexPositionColor,
            // VertexPositionTexture, VertexPositionColorTexture and VertexPositionNormalTexture --
            // and their *Colored convenience wrappers -- never call SetVertexDeclaration on the
            // temp buffer at all) OR with an element-less one (VertexBuffer's CNAEXT auto-detect
            // constructor, see the comment above). Either way there is no usable per-call
            // declaration to read, only the upload stride, which uniquely identifies which of the
            // four built-in layouts this is.
            switch (vb.GetStrideEXT())
            {
                case 16:  // PositionColorStream: float3 position @0, ubyte4n color @12.
                    if (kind != Shader3DKind::Colored)
                        NotYetImplemented(kRendererName,
                            "a textured or lit 3D draw from an undeclared VertexPositionColor "
                            "buffer");
                    key.stride = 16;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.colorOffset = 12;
                    key.colorFormat = static_cast<int>(SG_VERTEXFORMAT_UBYTE4N);
                    break;
                case 20:  // PositionTextureStream: float3 position @0, float2 texcoord @12.
                    if (kind != Shader3DKind::Textured && kind != Shader3DKind::DualTextured)
                        NotYetImplemented(kRendererName,
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
                    if (kind != Shader3DKind::Textured && kind != Shader3DKind::DualTextured)
                        NotYetImplemented(kRendererName,
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
                    if (kind != Shader3DKind::Lit && kind != Shader3DKind::EnvMapped)
                        NotYetImplemented(kRendererName,
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
                case 52:  // VertexPositionNormalTextureSkinned: float3 position @0, float3
                          // normal @12, float2 texcoord @24, float4 blendweight @32, ubyte4
                          // blendindices @48.
                    if (kind != Shader3DKind::Skinned)
                        NotYetImplemented(kRendererName,
                            "a non-skinned 3D draw from an undeclared "
                            "VertexPositionNormalTextureSkinned buffer");
                    key.stride = 52;
                    key.positionOffset = 0;
                    key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.normalOffset = 12;
                    key.normalFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
                    key.texCoordOffset = 24;
                    key.texCoordFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT2);
                    key.blendWeightOffset = 32;
                    key.blendWeightFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT4);
                    key.blendIndicesOffset = 48;
                    key.blendIndicesFormat = static_cast<int>(SG_VERTEXFORMAT_UBYTE4);
                    break;
                default:
                    NotYetImplemented(kRendererName,
                        "a 3D draw from a VertexBuffer with no VertexDeclaration and an "
                        "unrecognised stride");
            }
        }

        if (key.positionOffset < 0 || key.stride <= 0)
        {
            NotYetImplemented(kRendererName,
                "a 3D draw from a VertexDeclaration with no usable Position element");
        }
        if ((kind == Shader3DKind::Textured || kind == Shader3DKind::DualTextured)
            && key.texCoordOffset < 0)
        {
            NotYetImplemented(kRendererName,
                "a textured 3D draw from a VertexDeclaration with no TextureCoordinate element");
        }
        if (kind == Shader3DKind::Lit && (key.normalOffset < 0 || key.texCoordOffset < 0))
        {
            // Both are required, not just Normal: see Get3DPipeline's own comment on why the Lit
            // attribute slots (position, normal, texcoord0, color0) must stay a continuous prefix,
            // and why this is not a real restriction on any of CNA's built-in vertex types.
            NotYetImplemented(kRendererName,
                "a lit 3D draw from a VertexDeclaration with no Normal or no TextureCoordinate "
                "element");
        }
        if (kind == Shader3DKind::Skinned
            && (key.normalOffset < 0 || key.texCoordOffset < 0
                || key.blendWeightOffset < 0 || key.blendIndicesOffset < 0))
        {
            NotYetImplemented(kRendererName,
                "a skinned 3D draw from a VertexDeclaration with no Normal, no "
                "TextureCoordinate, no BlendWeight or no BlendIndices element");
        }
        if (kind == Shader3DKind::EnvMapped && (key.normalOffset < 0 || key.texCoordOffset < 0))
        {
            NotYetImplemented(kRendererName,
                "an environment-mapped 3D draw from a VertexDeclaration with no Normal or no "
                "TextureCoordinate element");
        }

        // Resolved before the pass begins: a Textured draw needs a real texture, and a Lit draw
        // needs one only when actually textured (the white fallback otherwise). Accepts a
        // render-target-as-texture the same way the SpriteBatch path does.
        bool hasTexture = false;
        std::uint32_t textureViewId = 0;
        bool textureIsRenderTarget = false;
        if (kind == Shader3DKind::Textured || kind == Shader3DKind::DualTextured
            || kind == Shader3DKind::Skinned || kind == Shader3DKind::EnvMapped
            || (kind == Shader3DKind::Lit && params.textureEnabled))
        {
            if (params.texture0 != nullptr)
            {
                textureViewId = ResolveSampledTextureViewId(*params.texture0, "a textured 3D draw");
                textureIsRenderTarget = IsRenderTargetSourceEXT(*params.texture0);
                hasTexture = true;
            }
            else if (kind == Shader3DKind::Textured)
            {
                throw std::runtime_error(
                    "Sokol renderer: TextureEnabled draw with no texture bound");
            }
        }
        if (!hasTexture
            && (kind == Shader3DKind::Lit || kind == Shader3DKind::DualTextured
                || kind == Shader3DKind::Skinned || kind == Shader3DKind::EnvMapped))
        {
            // Lit/Skinned/EnvMapped: the 1x1 opaque-white fallback for an untextured lit/skinned/
            // environment-mapped draw (SkinnedEffect/EnvironmentMapEffect::FillGpuDrawParams both
            // always set textureEnabled=true but texture0 stays null when the caller never assigned
            // the effect's own Texture property). DualTextured: the same null-fallback convention
            // EasyGL/Vulkan/Bgfx already established for DualTextureEffect's own Texture slot (this
            // codebase's docs/dualtextureeffect-support.md Task 386).
            textureViewId = GetDefaultWhiteTexture().GetViewIdEXT();
            hasTexture = true;
        }

        // EnvironmentMapEffect's cube map -- unlike the base texture above, there is no
        // established null-fallback convention for it in this codebase (every existing cross-
        // renderer test always sets one), so a missing cube map is refused the same way an
        // explicitly-required TextureEnabled base texture is above, rather than silently sampling
        // an unbound resource.
        std::uint32_t envMapViewId = 0;
        if (kind == Shader3DKind::EnvMapped)
        {
            if (params.envMap == nullptr)
            {
                throw std::runtime_error(
                    "Sokol renderer: EnvironmentMapEffect draw with no cube map bound");
            }
            envMapViewId = ResolveSampledCubeViewId(*params.envMap, "an environment-mapped 3D draw");
        }

        // DualTextureEffect's second slot (Texture2) -- same null-fallback convention as the
        // first (Task 387). Sampled with the SAME SamplerStates[0] as texture0, matching this
        // codebase's own established simplification (EasyGLRenderer::EnsureDualTextured3DProgram
        // binds unit 1 with no distinct per-slot sampler lookup either), not real XNA's separate
        // s0/s1 sampler registers.
        std::uint32_t texture1ViewId = 0;
        bool texture1IsRenderTarget = false;
        if (kind == Shader3DKind::DualTextured)
        {
            if (params.texture1 != nullptr)
            {
                texture1ViewId =
                    ResolveSampledTextureViewId(*params.texture1, "a dual-textured 3D draw");
                texture1IsRenderTarget = IsRenderTargetSourceEXT(*params.texture1);
            }
            else
            {
                texture1ViewId = GetDefaultWhiteTexture().GetViewIdEXT();
            }
        }

        // plan_sokol.md SOKOL-23: built before BeginPassIfNeeded()/sg_apply_pipeline() since it
        // issues its own raw GL calls (reading the source index buffer back off the GPU) that
        // sokol_gfx's own state cache does not know about.
        std::vector<std::uint32_t> wireframeIndices;
        if (wireframeActive)
        {
            wireframeIndices = BuildWireframeLineIndicesEXT(ib, primitive, primitiveCount,
                                                             params.startIndex, params.vertexStart);
#if CNA_SOKOL_HAS_GL_READBACK
            sg_reset_state_cache();
#endif
            if (wireframeIndexBufferId_ != 0)
            {
                sg_destroy_buffer(MakeBufferHandle(wireframeIndexBufferId_));
                wireframeIndexBufferId_ = 0;
            }
            sg_buffer_desc wireframeDesc = {};
            wireframeDesc.usage.index_buffer = true;
            wireframeDesc.usage.immutable = true;
            wireframeDesc.size = wireframeIndices.size() * sizeof(std::uint32_t);
            wireframeDesc.data.ptr = wireframeIndices.data();
            wireframeDesc.data.size = wireframeDesc.size;
            wireframeDesc.label = "cna_wireframe_line_indices";
            wireframeIndexBufferId_ = sg_make_buffer(&wireframeDesc).id;
            if (sg_query_buffer_state(MakeBufferHandle(wireframeIndexBufferId_)) != SG_RESOURCESTATE_VALID)
                throw std::runtime_error("Sokol renderer: wireframe line-index buffer creation failed");
        }

        BeginPassIfNeeded();

        sg_apply_pipeline(MakePipelineHandle(Get3DPipeline(key)));

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = MakeBufferHandle(vb.GetBufferIdEXT());
        // baseVertex is folded into the buffer offset rather than passed to sg_draw_ex, so the
        // same code path serves both the indexed and non-indexed shapes.
        bindings.vertex_buffer_offsets[0] = params.baseVertex * key.stride;
        if (wireframeActive)
            bindings.index_buffer = MakeBufferHandle(wireframeIndexBufferId_);
        else if (ib != nullptr)
            bindings.index_buffer = MakeBufferHandle(ib->GetBufferIdEXT());
        if (hasTexture)
        {
            const SamplerSlotState& sampler = samplerSlots_[0];
            bindings.views[VIEW_cna_tex] = MakeViewHandle(textureViewId);
            bindings.samplers[SMP_cna_smp] = MakeSamplerHandle(
                GetSampler(sampler.filter, sampler.addressU, sampler.addressV,
                          sampler.maxAnisotropy));
        }
        if (kind == Shader3DKind::DualTextured)
        {
            const SamplerSlotState& sampler = samplerSlots_[0];
            bindings.views[VIEW_cna_tex2] = MakeViewHandle(texture1ViewId);
            bindings.samplers[SMP_cna_smp2] = MakeSamplerHandle(
                GetSampler(sampler.filter, sampler.addressU, sampler.addressV,
                          sampler.maxAnisotropy));
        }
        if (kind == Shader3DKind::EnvMapped)
        {
            // Reuses slot 0's sampler state (no distinct SamplerStates[1] tracking in this
            // codebase's GpuDrawParams model), the same simplification DualTextured's own second
            // texture slot already makes.
            const SamplerSlotState& sampler = samplerSlots_[0];
            bindings.views[VIEW_cna_envTex] = MakeViewHandle(envMapViewId);
            bindings.samplers[SMP_cna_envSmp] = MakeSamplerHandle(
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
                fsUniforms.rtFlipV[0] = textureIsRenderTarget ? 1.0f : 0.0f;

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
                fsUniforms.rtFlipV[0] = textureIsRenderTarget ? 1.0f : 0.0f;

                sg_range fsRange{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_lit3d_fs_params, &fsRange);
                break;
            }
            case Shader3DKind::DualTextured:
            {
                cna_dualtextured3d_vs_params_t vsUniforms{};
                worldViewProjection.ToColumnMajor(vsUniforms.mvp);
                vsUniforms.diffuse[0] = params.diffuseColor[0];
                vsUniforms.diffuse[1] = params.diffuseColor[1];
                vsUniforms.diffuse[2] = params.diffuseColor[2];
                vsUniforms.diffuse[3] = params.diffuseColor[3];
                vsUniforms.flags[0] = vertexColorActive ? 1.0f : 0.0f;
                vsUniforms.fogVector[0] = params.fogVector[0];
                vsUniforms.fogVector[1] = params.fogVector[1];
                vsUniforms.fogVector[2] = params.fogVector[2];
                vsUniforms.fogVector[3] = params.fogVector[3];

                sg_range vsRange{&vsUniforms, sizeof(vsUniforms)};
                sg_apply_uniforms(UB_cna_dualtextured3d_vs_params, &vsRange);

                cna_dualtextured3d_fs_params_t fsUniforms{};
                fsUniforms.fogColor[0] = params.fogColor[0];
                fsUniforms.fogColor[1] = params.fogColor[1];
                fsUniforms.fogColor[2] = params.fogColor[2];
                fsUniforms.rtFlipV[0] = textureIsRenderTarget ? 1.0f : 0.0f;
                fsUniforms.rtFlipV[1] = texture1IsRenderTarget ? 1.0f : 0.0f;

                sg_range fsRange2{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_dualtextured3d_fs_params, &fsRange2);
                break;
            }
            case Shader3DKind::Skinned:
            {
                const Matrix normalMatrix = Matrix::Transpose(Matrix::Invert(world));

                cna_skinned3d_vs_params_t vsUniforms{};
                worldViewProjection.ToColumnMajor(vsUniforms.mvp);
                world.ToColumnMajor(vsUniforms.world);
                normalMatrix.ToColumnMajor(vsUniforms.normalMatrix);
                vsUniforms.flags[0] = vertexColorActive ? 1.0f : 0.0f;
                vsUniforms.flags[1] = static_cast<float>(params.weightsPerVertex);
                vsUniforms.alphaTest[0] = params.alphaTest[0];
                vsUniforms.alphaTest[1] = params.alphaTest[1];
                vsUniforms.alphaTest[2] = params.alphaTest[2];
                vsUniforms.alphaTest[3] = params.alphaTest[3];
                vsUniforms.fogVector[0] = params.fogVector[0];
                vsUniforms.fogVector[1] = params.fogVector[1];
                vsUniforms.fogVector[2] = params.fogVector[2];
                vsUniforms.fogVector[3] = params.fogVector[3];
                // GpuDrawParams::boneTransforms is already column-major mat4-per-bone, zero-filled
                // beyond boneCount -- the exact memory shape cna_skinned3d_vs_params_t::bones[72][16]
                // expects, so a straight copy needs no per-bone Matrix round-trip.
                static_assert(sizeof(vsUniforms.bones) == sizeof(params.boneTransforms),
                              "skinned3d bone-palette uniform size must match GpuDrawParams");
                std::memcpy(vsUniforms.bones, params.boneTransforms, sizeof(vsUniforms.bones));

                sg_range vsRange{&vsUniforms, sizeof(vsUniforms)};
                sg_apply_uniforms(UB_cna_skinned3d_vs_params, &vsRange);

                cna_skinned3d_fs_params_t fsUniforms{};
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
                fsUniforms.rtFlipV[0] = textureIsRenderTarget ? 1.0f : 0.0f;

                sg_range fsRange3{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_skinned3d_fs_params, &fsRange3);
                break;
            }
            case Shader3DKind::EnvMapped:
            {
                const Matrix normalMatrix = Matrix::Transpose(Matrix::Invert(world));

                cna_envmap3d_vs_params_t vsUniforms{};
                worldViewProjection.ToColumnMajor(vsUniforms.mvp);
                world.ToColumnMajor(vsUniforms.world);
                normalMatrix.ToColumnMajor(vsUniforms.normalMatrix);
                vsUniforms.eyePosition[0] = params.eyePositionWorld[0];
                vsUniforms.eyePosition[1] = params.eyePositionWorld[1];
                vsUniforms.eyePosition[2] = params.eyePositionWorld[2];
                vsUniforms.flags[0] = params.fresnelEnabled ? 1.0f : 0.0f;
                vsUniforms.flags[1] = params.fresnelFactor;
                vsUniforms.flags[2] = params.envMapAmount;
                vsUniforms.fogVector[0] = params.fogVector[0];
                vsUniforms.fogVector[1] = params.fogVector[1];
                vsUniforms.fogVector[2] = params.fogVector[2];
                vsUniforms.fogVector[3] = params.fogVector[3];

                sg_range vsRange{&vsUniforms, sizeof(vsUniforms)};
                sg_apply_uniforms(UB_cna_envmap3d_vs_params, &vsRange);

                // GpuDrawParams::diffuseColor/emissiveColor both arrive pre-multiplied by alpha for
                // this effect (EnvironmentMapEffect::FillGpuDrawParams's own doc comment) -- see
                // envmap3d.glsl's own doc comment for why this differs from lit3d's convention.
                cna_envmap3d_fs_params_t fsUniforms{};
                fsUniforms.diffuse[0] = params.diffuseColor[0];
                fsUniforms.diffuse[1] = params.diffuseColor[1];
                fsUniforms.diffuse[2] = params.diffuseColor[2];
                fsUniforms.diffuse[3] = params.diffuseColor[3];
                fsUniforms.emissiveColor[0] = params.emissiveColor[0];
                fsUniforms.emissiveColor[1] = params.emissiveColor[1];
                fsUniforms.emissiveColor[2] = params.emissiveColor[2];
                fsUniforms.light0Dir[0] = params.light0Dir[0];
                fsUniforms.light0Dir[1] = params.light0Dir[1];
                fsUniforms.light0Dir[2] = params.light0Dir[2];
                fsUniforms.light0Diffuse[0] = params.light0Diffuse[0];
                fsUniforms.light0Diffuse[1] = params.light0Diffuse[1];
                fsUniforms.light0Diffuse[2] = params.light0Diffuse[2];
                fsUniforms.light1Dir[0] = params.light1Dir[0];
                fsUniforms.light1Dir[1] = params.light1Dir[1];
                fsUniforms.light1Dir[2] = params.light1Dir[2];
                fsUniforms.light1Diffuse[0] = params.light1Diffuse[0];
                fsUniforms.light1Diffuse[1] = params.light1Diffuse[1];
                fsUniforms.light1Diffuse[2] = params.light1Diffuse[2];
                fsUniforms.light2Dir[0] = params.light2Dir[0];
                fsUniforms.light2Dir[1] = params.light2Dir[1];
                fsUniforms.light2Dir[2] = params.light2Dir[2];
                fsUniforms.light2Diffuse[0] = params.light2Diffuse[0];
                fsUniforms.light2Diffuse[1] = params.light2Diffuse[1];
                fsUniforms.light2Diffuse[2] = params.light2Diffuse[2];
                fsUniforms.envMapSpecular[0] = params.envMapSpecular[0];
                fsUniforms.envMapSpecular[1] = params.envMapSpecular[1];
                fsUniforms.envMapSpecular[2] = params.envMapSpecular[2];
                fsUniforms.alphaTest[0] = params.alphaTest[0];
                fsUniforms.alphaTest[1] = params.alphaTest[1];
                fsUniforms.alphaTest[2] = params.alphaTest[2];
                fsUniforms.alphaTest[3] = params.alphaTest[3];
                fsUniforms.fogColor[0] = params.fogColor[0];
                fsUniforms.fogColor[1] = params.fogColor[1];
                fsUniforms.fogColor[2] = params.fogColor[2];
                fsUniforms.rtFlipV[0] = textureIsRenderTarget ? 1.0f : 0.0f;

                sg_range fsRange4{&fsUniforms, sizeof(fsUniforms)};
                sg_apply_uniforms(UB_cna_envmap3d_fs_params, &fsRange4);
                break;
            }
        }

        // plan_sokol.md SOKOL-23: the wireframe scratch buffer starts fresh at index 0 and holds
        // exactly the doubled-edge line list, regardless of the source draw's own startIndex/
        // vertexStart -- those were already folded into BuildWireframeLineIndicesEXT()'s readback.
        const int elementCount = wireframeActive
            ? static_cast<int>(wireframeIndices.size())
            : ElementCountForPrimitives(primitive, primitiveCount);
        const int baseElement = wireframeActive ? 0
            : (ib != nullptr) ? params.startIndex : params.vertexStart;
        sg_draw(baseElement, elementCount, 1);
    }

    void SokolRenderer::ApplyCustomEffectRasterStateEXT()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        // Depth/stencil -- mirrors Get3DPipeline's own desc.depth/desc.stencil branches exactly:
        // with no real depth-stencil attachment in the active pass, both are functionally disabled
        // (matching XNA's no-op-if-no-depth-buffer behaviour); with one, depth stays enabled with
        // compare=ALWAYS/write=false whenever depthTestEnabled_ is false, the same "functionally
        // disabled but not GL_DEPTH_TEST-disabled" shape those pipelines use.
        if (CurrentPassHasDepthStencilAttachmentEXT())
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(depthTestEnabled_ ? ToGLCompareFunc(depthFunc_) : GL_ALWAYS);
            glDepthMask((depthTestEnabled_ && depthWriteEnabled_) ? GL_TRUE : GL_FALSE);

            if (stencilEnabled_)
            {
                glEnable(GL_STENCIL_TEST);
                glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(stencilFunc_), referenceStencil_,
                                      static_cast<GLuint>(stencilMask_ & 0xFF));
                glStencilOpSeparate(GL_FRONT, ToGLStencilOp(stencilFail_),
                                    ToGLStencilOp(stencilDepthFail_), ToGLStencilOp(stencilPass_));
                // CounterClockwiseStencil* only applies when twoSidedStencilMode_ is set; otherwise
                // GL_BACK mirrors GL_FRONT, matching Get3DPipeline's own
                // "desc.stencil.back = twoSidedStencilMode ? ... : front" fallback.
                const int backFunc = twoSidedStencilMode_ ? ccwStencilFunc_ : stencilFunc_;
                const int backFail = twoSidedStencilMode_ ? ccwStencilFail_ : stencilFail_;
                const int backDepthFail = twoSidedStencilMode_ ? ccwStencilDepthFail_ : stencilDepthFail_;
                const int backPass = twoSidedStencilMode_ ? ccwStencilPass_ : stencilPass_;
                glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(backFunc), referenceStencil_,
                                      static_cast<GLuint>(stencilMask_ & 0xFF));
                glStencilOpSeparate(GL_BACK, ToGLStencilOp(backFail), ToGLStencilOp(backDepthFail),
                                    ToGLStencilOp(backPass));
                glStencilMask(static_cast<GLuint>(stencilWriteMask_ & 0xFF));
            }
            else
            {
                glDisable(GL_STENCIL_TEST);
            }
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_STENCIL_TEST);
        }

        // Blend, including the constant colour -- unlike the sg_pipeline paths (plan_sokol.md
        // SOKOL-40), this raw-GL draw applies BlendFactor live on every draw, so it has no pipeline
        // cache to go stale.
        if (blendEnabled_)
        {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(ToGLBlendFactor(blendColorSrc_), ToGLBlendFactor(blendColorDst_),
                                ToGLBlendFactor(blendAlphaSrc_), ToGLBlendFactor(blendAlphaDst_));
            glBlendEquationSeparate(ToGLBlendOp(blendColorFunc_), ToGLBlendOp(blendAlphaFunc_));
            glBlendColor(blendFactor_[0], blendFactor_[1], blendFactor_[2], blendFactor_[3]);
        }
        else
        {
            glDisable(GL_BLEND);
        }

        // Culling + winding -- every sg_pipeline in this renderer fixes face_winding to
        // SG_FACEWINDING_CW (see ToCullMode()'s own doc comment), so this raw-GL path must apply
        // the same glFrontFace(GL_CW) explicitly rather than relying on a prior stock-pipeline
        // draw having incidentally already left GL in that state.
        if (cullMode_ == 0)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(ToGLCullFace(cullMode_));
        }
        glFrontFace(GL_CW);

        // Depth bias -- the same RasterizerState.DepthBias/SlopeScaleDepthBias -> glPolygonOffset
        // mapping Get3DPipeline bakes into sg_depth_state.bias/bias_slope_scale.
        if (depthBias_ != 0.0f || slopeScaleDepthBias_ != 0.0f)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(slopeScaleDepthBias_, depthBias_);
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
#endif
    }

    void SokolRenderer::ApplyCustomEffectColorMasksEXT()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        const int activeColorAttachments = 1 + static_cast<int>(mrtExtraTargets_.size());
        for (int slot = 0; slot < activeColorAttachments; ++slot)
        {
            const int mask = colorWriteChannels_[slot];
            glColorMaski(static_cast<GLuint>(slot),
                        (mask & 1) != 0 ? GL_TRUE : GL_FALSE, (mask & 2) != 0 ? GL_TRUE : GL_FALSE,
                        (mask & 4) != 0 ? GL_TRUE : GL_FALSE, (mask & 8) != 0 ? GL_TRUE : GL_FALSE);
        }
#endif
    }

    void SokolRenderer::ResetCustomEffectColorMasksEXT()
    {
#if CNA_SOKOL_HAS_GL_READBACK
        const int activeColorAttachments = 1 + static_cast<int>(mrtExtraTargets_.size());
        for (int slot = 0; slot < activeColorAttachments; ++slot)
            glColorMaski(static_cast<GLuint>(slot), GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#endif
    }

    void SokolRenderer::DrawCustomEffect3D(const SokolVertexBufferRenderer& vb,
                                                  const SokolIndexBufferRenderer* ib,
                                                  const VertexDeclaration* declaration,
                                                  const Matrix& world,
                                                  const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive,
                                                  int primitiveCount,
                                                  const GpuDrawParams& params)
    {
#if CNA_SOKOL_HAS_GL_READBACK
        const bool hasDeclaration = declaration != nullptr && !declaration->GetVertexElements().empty();
        const int stride = hasDeclaration ? declaration->getVertexStrideProperty()
                                          : static_cast<int>(vb.GetStrideEXT());
        const std::vector<CustomEffectAttribLayout> layout =
            BuildCustomEffectAttribLayoutEXT(declaration, stride);
        if (layout.empty())
        {
            throw std::runtime_error(
                "Sokol renderer: a custom ShaderEffect draw needs an explicit VertexDeclaration or "
                "one of this renderer's recognised undeclared-VertexBuffer strides (16/20/24/32/52)");
        }

        BeginPassIfNeeded();
        // Everything below is a raw GL call bypassing sg_pipeline/sg_bindings entirely (see
        // SokolEffectRenderer's own doc comment for why) -- bracketed by sg_reset_state_cache() on
        // both sides per sokol_gfx.h's own documented interleaving contract, so sokol_gfx's next
        // sg_apply_pipeline()/sg_apply_bindings() call (from a later stock-effect or sprite draw)
        // does not skip a GL call it thinks is already in effect from before this detour.
        sg_reset_state_cache();

        if (customEffectVaoId_ == 0) glGenVertexArrays(1, &customEffectVaoId_);
        glBindVertexArray(customEffectVaoId_);

        const sg_gl_buffer_info vbInfo = sg_gl_query_buffer_info(MakeBufferHandle(vb.GetBufferIdEXT()));
        const GLuint vbGlId = vbInfo.buf[vbInfo.active_slot];
        glBindBuffer(GL_ARRAY_BUFFER, vbGlId);

        // Attribute location = the element's own index within the resolved layout, matching
        // EasyGLRenderer::ApplyLayout()'s established "layout(location=N) == Nth field of
        // the declaration" convention -- the caller's own GLSL is expected to declare its inputs
        // in the same order, not by name.
        for (std::size_t i = 0; i < layout.size(); ++i)
        {
            const CustomEffectAttribLayout& entry = layout[i];
            const auto location = static_cast<GLuint>(i);
            const void* offset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(entry.offset));
            glEnableVertexAttribArray(location);
            if (entry.desc.isInteger)
                glVertexAttribIPointer(location, entry.desc.componentCount, entry.desc.type, stride,
                                       offset);
            else
                glVertexAttribPointer(location, entry.desc.componentCount, entry.desc.type,
                                      entry.desc.normalized ? GL_TRUE : GL_FALSE, stride, offset);
        }

        // Matches EasyGLRenderer's own BindCustomEffectMatrices(): World/View/Projection by
        // fixed name, the convention every original XNA sample's own .fx source already declares.
        // Bind() (glUseProgram) must happen before ApplyPendingTextureBindsEXT() below only in the
        // sense that both must happen before the draw -- SetUniformXxx() itself needs no particular
        // order relative to Bind() (see SetUniformXxx's own comment on glProgramUniform*), but a
        // real GL draw call does need a program actually bound via glUseProgram.
        auto* effectRenderer = static_cast<SokolEffectRenderer*>(params.customEffectRenderer);
        effectRenderer->Bind();
        float worldCM[16];
        float viewCM[16];
        float projCM[16];
        world.ToColumnMajor(worldCM);
        view.ToColumnMajor(viewCM);
        projection.ToColumnMajor(projCM);
        effectRenderer->SetUniformMat4("World", worldCM);
        effectRenderer->SetUniformMat4("View", viewCM);
        effectRenderer->SetUniformMat4("Projection", projCM);
        // Realizes every BindTexture()/BindTextureCube() call the caller made earlier (e.g. via
        // ShaderEffect::SetTexture() before this draw call was ever reached) -- see
        // ApplyPendingTextureBindsEXT()'s own doc comment for why this can only safely happen here,
        // not at BindTexture() call time.
        effectRenderer->ApplyPendingTextureBindsEXT();

        // plan_sokol.md SOKOL-41: full depth/stencil/blend/cull/winding/depth-bias state, not just
        // depth -- this raw-GL draw bypasses sg_pipeline entirely, so none of BlendState/
        // DepthStencilState/RasterizerState was ever applied to anything for it before, unlike
        // every stock-effect/sprite pipeline (Get3DPipeline/GetSpritePipeline bake all of it into
        // their own sg_pipeline_desc). Exercised end-to-end by easygl_mrt_test.cpp's own
        // TestDepthOwnership (plan_sokol.md SOKOL-26) for the depth half.
        ApplyCustomEffectRasterStateEXT();

        // ColorWriteChannels0..3 (plan_sokol.md SOKOL-26 MRT): see ApplyCustomEffectColorMasksEXT()/
        // ResetCustomEffectColorMasksEXT()'s own doc comments for why this needs its own before/
        // after pair around the draw rather than living inside ApplyCustomEffectRasterStateEXT().
        ApplyCustomEffectColorMasksEXT();

        const GLenum topology = ToGLPrimitiveTopology(primitive);
        const int elementCount = ElementCountForPrimitives(primitive, primitiveCount);
        if (ib != nullptr)
        {
            const sg_gl_buffer_info ibInfo =
                sg_gl_query_buffer_info(MakeBufferHandle(ib->GetBufferIdEXT()));
            const GLuint ibGlId = ibInfo.buf[ibInfo.active_slot];
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibGlId);
            const GLenum indexType = ib->IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            const int indexSize = ib->IsThirtyTwoBit() ? 4 : 2;
            const void* indexOffset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSize));
            if (params.baseVertex == 0)
                glDrawElements(topology, elementCount, indexType, indexOffset);
            else
                glDrawElementsBaseVertex(topology, elementCount, indexType, indexOffset,
                                         params.baseVertex);
        }
        else
        {
            glDrawArrays(topology, params.vertexStart, elementCount);
        }

        ResetCustomEffectColorMasksEXT();

        glBindVertexArray(0);
        sg_reset_state_cache();
#else
        (void)vb; (void)ib; (void)declaration; (void)world; (void)view; (void)projection;
        (void)primitive; (void)primitiveCount; (void)params;
        NotYetImplemented(kRendererName, "custom ShaderEffect draws (non-GL CNA_SOKOL_API)");
#endif
    }

    void SokolRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                     const Matrix& world,
                                                     const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive,
                                                     int primitiveCount)
    {
        // This entry point carries no effect state, so it means "render the raw vertex colours":
        // a white diffuse and vertex colouring on, matching what the other renderers do here.
        GpuDrawParams params{};
        params.vertexColorEnabled = true;
        DrawColored3D(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                            const IIndexBufferRenderer& ib,
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

    void SokolRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                const Matrix& world,
                                                const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive,
                                                int primitiveCount,
                                                const GpuDrawParams& params)
    {
        DrawColored3D(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                       const IIndexBufferRenderer& ib,
                                                       const Matrix& world,
                                                       const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive,
                                                       int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        DrawColored3D(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void SokolRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vbIn,
                                                         const IIndexBufferRenderer& ibIn,
                                                         const Matrix& world,
                                                         const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive,
                                                         int primitiveCount,
                                                         int instanceCount,
                                                         const GpuDrawParams& params)
    {
        // REMED-GFX-201/202: the bound streams now arrive as one array covering both input rates,
        // so the per-instance buffer this route needs is the lowest-slot entry whose
        // InstanceFrequency is greater than zero. Neither shape this renderer cannot express is
        // silently reduced to a subset: a second per-vertex or per-instance stream is refused
        // before anything native happens (instanced3d.glsl declares exactly one mesh record and
        // exactly one instance record, and GraphicsCapability::MultiStreamVertexInput answers
        // false so the shared layer already rejects it a step earlier).
        RejectUnsupportedStreamCombination(params, kRendererName);

        const GpuVertexStreamBinding* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr || instanceStream->buffer == nullptr)
        {
            // No per-instance stream -- fall back to a real, working non-instanced draw rather
            // than throwing, matching VulkanRenderer/DirectX11Renderer's own identical
            // fallback contract.
            DrawColored3D(vbIn, &ibIn, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (primitiveCount <= 0) return;

        const auto& vb = static_cast<const SokolVertexBufferRenderer&>(vbIn);
        if (vb.GetBufferIdEXT() == 0 || vb.GetVertexCount() <= 0) return;

        const auto& ib = static_cast<const SokolIndexBufferRenderer&>(ibIn);
        if (ib.GetBufferIdEXT() == 0 || ib.GetIndexCount() <= 0) return;

        const auto& instVb = static_cast<const SokolVertexBufferRenderer&>(*instanceStream->buffer);
        if (instVb.GetBufferIdEXT() == 0 || instVb.GetVertexCount() <= 0) return;

        // REMED-GFX-DECL-GUARD: the geometry stream's declaration is read here exactly as it is on
        // the ordinary routes. The instance stream is not checked against it -- its layout is
        // instanced3d.glsl's fixed 64-byte matrix, which the stride check above already pins.
        RequireFaithfulDeclarationEXT(vb.GetDeclarationEXT(), vb.GetStrideEXT(), "instanced");

        // instanced3d.glsl reads the per-instance World matrix as four column-major vec4s at fixed
        // offsets, so a stream declaring anything else would be fetched as a matrix it is not.
        // Refusing is the honest answer; the same shape WickedRenderer already refuses.
        if (instanceStream->strideInBytes != 0 && instanceStream->strideInBytes != 64)
        {
            throw std::runtime_error(
                "Sokol renderer: the per-instance stream must be a 64-byte Matrix (stride " +
                std::to_string(instanceStream->strideInBytes) + " was bound).");
        }

        PipelineInstanced3DKey key{};
        key.colorSrcBlend = blendColorSrc_;
        key.alphaSrcBlend = blendAlphaSrc_;
        key.colorDstBlend = blendColorDst_;
        key.alphaDstBlend = blendAlphaDst_;
        key.colorBlendFunc = blendColorFunc_;
        key.alphaBlendFunc = blendAlphaFunc_;
        key.colorWriteChannels = colorWriteChannels_[0];
        key.blendEnabled = blendEnabled_;
        key.depthTestEnabled = depthTestEnabled_;
        key.depthWriteEnabled = depthWriteEnabled_;
        key.depthFunc = depthFunc_;
        key.cullMode = cullMode_;
        key.hasDepthAttachment = CurrentPassHasDepthStencilAttachmentEXT();
        key.sampleCount = CurrentPassSampleCountEXT();
        key.colorAttachmentCount = CurrentPassColorAttachmentCountEXT();
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
        key.blendFactorPacked = PackBlendFactorEXT(blendFactor_);
        key.primitiveType = static_cast<int>(ToPrimitiveType(primitive));
        key.indexType = static_cast<int>(ib.IsThirtyTwoBit() ? SG_INDEXTYPE_UINT32 : SG_INDEXTYPE_UINT16);
        // REMED-GFX-202: the step rate is the bound stream's own InstanceFrequency, never an
        // assumed 1. Zero cannot reach here (a zero-frequency entry is a per-vertex stream by
        // definition), so no clamping is needed.
        key.instanceStepRate = instanceStream->instanceFrequency;

        // Only Position is read (instanced3d.glsl's own doc comment); resolved from the mesh
        // buffer's declaration when one was supplied, else offset 0 / FLOAT3 -- every one of this
        // codebase's built-in vertex layouts (and the position-only 12-byte layout
        // modules/renderers/webgpu/examples/webgpu_instanced3d_test.cpp itself uses) puts Position first.
        key.stride = static_cast<int>(vb.GetStrideEXT());
        key.positionOffset = 0;
        key.positionFormat = static_cast<int>(SG_VERTEXFORMAT_FLOAT3);
        if (const VertexDeclaration* declaration = vb.GetDeclarationEXT();
            declaration != nullptr && !declaration->GetVertexElements().empty())
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            for (const VertexElement& element : declaration->GetVertexElements())
            {
                if (element.getUsageIndexProperty() != 0) continue;
                if (element.getVertexElementUsageProperty() != VertexElementUsage::Position) continue;
                const sg_vertex_format format = ToVertexFormat(element.getVertexElementFormatProperty());
                if (format != SG_VERTEXFORMAT_INVALID)
                {
                    key.positionOffset = element.getOffsetProperty();
                    key.positionFormat = static_cast<int>(format);
                }
                break;
            }
        }
        if (key.stride <= 0) return;

        BeginPassIfNeeded();

        sg_apply_pipeline(MakePipelineHandle(GetInstanced3DPipeline(key)));

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = MakeBufferHandle(vb.GetBufferIdEXT());
        bindings.vertex_buffer_offsets[0] = params.baseVertex * key.stride;
        bindings.vertex_buffers[1] = MakeBufferHandle(instVb.GetBufferIdEXT());
        // REMED-GFX-202: the instanced route folds nothing into baseVertex, so the per-instance
        // stream carries its whole public VertexBufferBinding.VertexOffset here, in instance
        // ELEMENTS -- the same `binding.vertexOffset * stride` FNA3D's own drivers apply. The
        // record size is the pipeline's own 64-byte matrix, not the binding's declared stride,
        // which is 0 for a buffer built without a VertexDeclaration.
        bindings.vertex_buffer_offsets[1] = instanceStream->vertexOffset * 64;
        bindings.index_buffer = MakeBufferHandle(ib.GetBufferIdEXT());
        sg_apply_bindings(&bindings);

        cna_instanced3d_vs_params_t uniforms{};
        // World is reconstructed per-instance in the vertex shader; only view*projection goes in
        // the uniform (mirrors VulkanRenderer::FillInstancedPushConst's identical shape).
        const Matrix vp = view * projection;
        vp.ToColumnMajor(uniforms.vp);
        uniforms.diffuseColor[0] = params.diffuseColor[0];
        uniforms.diffuseColor[1] = params.diffuseColor[1];
        uniforms.diffuseColor[2] = params.diffuseColor[2];
        uniforms.diffuseColor[3] = params.diffuseColor[3];

        sg_range range{&uniforms, sizeof(uniforms)};
        sg_apply_uniforms(UB_cna_instanced3d_vs_params, &range);

        const int elementCount = ElementCountForPrimitives(primitive, primitiveCount);
        const int instCountClamped = std::max(1, instanceCount);
        sg_draw(params.startIndex, elementCount, instCountClamped);
    }

    bool SokolRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // Real: the platform context requests a 24-bit depth/8-bit stencil swapchain that the
            // Clear* family writes through pass load actions.
            case CNA::GraphicsCapability::DepthStencilBuffer:
                return true;
            // Real for both the back buffer (sample count negotiated with the GL context at
            // construction) and RenderTarget2D (plan_sokol.md SOKOL-26: a separate multisample-only
            // colour image plus a single-sample resolve image, clamped to the driver's real
            // GL_MAX_SAMPLES).
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return true;
            // Real: SamplerState.MaxAnisotropy reaches sg_sampler_desc.max_anisotropy whenever
            // TextureFilter::Anisotropic is selected.
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return true;
            // Real as of SOKOL-20, and considerably more since: vertex/index buffers, depth-tested
            // draws, the colored-3D pipeline, texturing, up to 3 real per-pixel directional lights,
            // DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, instanced draws and a custom
            // ShaderEffect all exist. Only PBR shading throws.
            case CNA::GraphicsCapability::ThreeD:
                return true;
            // Real as of SOKOL-29, GL-only: a raw glBeginQuery/glEndQuery GL_SAMPLES_PASSED query,
            // since sokol_gfx exposes no query API of its own. False on any other CNA_SOKOL_API,
            // the same GL-only boundary ReadBackbuffer already declares.
            case CNA::GraphicsCapability::OcclusionQuery:
                return CNA_SOKOL_HAS_GL_READBACK != 0;
            // Real as of SOKOL-27: SokolTexture3DRenderer stores real per-mip-level voxels
            // (SetData/GetData round-trip exactly), the same CPU-storage-only shape as
            // SokolTextureCubeRenderer -- no sg_image, since nothing samples a volume texture yet.
            case CNA::GraphicsCapability::Texture3D:
                return true;
            // Real as of SOKOL-28, GL-only: SokolEffectRenderer compiles real GLSL at runtime via
            // raw glCreateShader/glCompileShader/glLinkProgram, the same GL-only boundary
            // OcclusionQuery above already declares (SokolEffectRenderer::CompileProgram
            // deterministically fails, and IsValid() reports false, on any other CNA_SOKOL_API).
            case CNA::GraphicsCapability::CustomEffects:
                return CNA_SOKOL_HAS_GL_READBACK != 0;
            // Real as of SOKOL-26 MRT: 2-4 RenderTarget2D targets bound together via a real
            // multi-attachment sg_pass, verified with a custom ShaderEffect writing distinct
            // per-slot outputs (only a custom effect can usefully target a slot beyond 0 -- see
            // DrawColored3D/DrawSpriteRunEXT's own MRT guards for why a stock-pipeline draw while
            // MRT is bound is still refused).
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return true;
            // REMED-GFX-209: true, and the answer is about the OBSERVABLE contract, not about
            // which native mechanism delivers it. `RasterizerState.FillMode == WireFrame` really
            // does produce a wireframe here on every triangle route, indexed and non-indexed
            // alike (plan_sokol.md SOKOL-23: DrawColored3D re-expands triangles into GL_LINES and
            // swaps the pipeline's topology, since sokol_gfx bakes topology into the pipeline and
            // exposes no polygon-fill-mode toggle). The shared asymmetric-triangle oracle measures
            // exactly that -- interior 0/1089 under WireFrame against 1089/1089 under Solid, all
            // three edge probes lit, and no stale state across alternating draws.
            //
            // This deliberately does NOT copy EasyGL's `false`. That answer is recorded in
            // GraphicsDeviceCapabilityTests.cpp as the one report known to be wrong (its own
            // CPU-expansion path renders a genuine wireframe too), and REMED-GFX-219 exists to
            // correct it; reproducing it here would add a second under-report rather than inherit
            // a convention. A caller that gates on this query gets a working feature.
            case CNA::GraphicsCapability::WireFrame:
                return true;
            // REMED-GFX-201: false. The 3D pipeline builds one sokol_gfx vertex-buffer layout from
            // one buffer's declaration, and the instanced route binds exactly one per-vertex plus
            // one per-instance stream, so a declaration split across several buffers -- or several
            // per-instance streams at their own frequencies -- has nowhere to go. Both routes
            // refuse the shape outright (RejectUnsupportedStreamCombination) rather than rendering
            // from a subset, and GraphicsDevice rejects it a step earlier on the strength of this
            // answer.
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                return false;
            // REMED-GFX-202: real as of SOKOL-36. DrawInstancedPrimitivesEx binds the per-instance
            // World-matrix stream at slot 1 with SG_VERTEXSTEP_PER_INSTANCE and the binding's own
            // InstanceFrequency as sokol_gfx's step_rate; sg_draw's instance count drives the
            // native instanced draw. Unconditional here, not device-dependent: sokol_gfx's GLCORE
            // renderer requires GL 4.1, where instanced drawing and vertex attribute divisors are
            // core, not extensions.
            case CNA::GraphicsCapability::Instancing:
                return true;
            case CNA::GraphicsCapability::StencilBuffer:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
        }
        // Every enumerator above is answered explicitly and there is no default arm, so a
        // capability added to CNA::GraphicsCapability after this renderer was written is a compiler
        // warning here rather than a silent answer. This return exists only for a value cast in
        // from outside the enumeration.
        return false;
    }

    int SokolRenderer::GetMultiSampleCount() const { return sampleCount_; }

    SokolApiEXT SokolRenderer::GetApiEXT()
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
    #error "CNA: no SOKOL_* API define set -- cmake/RendererSelection.cmake's CNA_SOKOL_API is broken"
#endif
    }

    int SokolRenderer::GetMaxSpriteQuadsPerFrameEXT() { return kMaxSpriteQuads; }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Sokol::SokolRenderer>(args);
    }
}
