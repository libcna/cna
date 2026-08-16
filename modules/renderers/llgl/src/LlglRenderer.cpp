// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "CNA/Internal/Renderers/Common/NotYetImplemented.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

#include <LLGL/Utils/VertexFormat.h>

#include "shaders/llgl_shaders.hpp"


#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Llgl
{
    namespace
    {
        constexpr const char* kRendererName = "LLGL";

        /// Floats in the 3D effect uniform block: see shaders/effect3d_common.glsl.inc. The unlit
        /// shaders declare only the first 32 (128 bytes); the lit shaders declare the full 100
        /// (400 bytes) that follow it. One constant-buffer size serves every 3D pipeline -- a
        /// smaller declared block simply reads a prefix of a larger allocated buffer, which every
        /// graphics API this renderer targets allows.
        constexpr std::size_t kEffectUniformFloats = 100;

        /// Floats in a custom ShaderEffect's uniform block (LLGL-27): 32 floats = 128 bytes,
        /// matching the native Vulkan renderer's own VulkanEffectRenderer::pushConst_ layout --
        /// [0..1]=vpSize (written by QueueSpriteEXT itself, not the effect), [2..3]=padding,
        /// [4..19]=uMatrix, [20..23]=uColor, [24..31]=uFloats (only [24]=uFloat0 is ever written).
        constexpr std::size_t kCustomEffectUniformFloats = 32;

        /// Floats in EnvironmentMapEffect's own dedicated uniform block (see
        /// shaders/env_map3d.vert.glsl's EnvMapParams for the byte layout): 84 floats = 336 bytes.
        /// This effect's field set (Fresnel factor, environment map amount/specular, no per-light
        /// specular or alpha test) does not fit the shared 100-float Transform block, and its
        /// vertex/fragment pair is never linked with any other shader in this renderer, so a
        /// dedicated block -- mirroring the Vulkan renderer's own separate EnvMapParams UBO -- is
        /// cleaner than repurposing unrelated padding fields in the shared one.
        constexpr std::size_t kEnvMapUniformFloats = 84;

        /// Floats in SkinnedEffect's own dedicated parameter uniform block (see
        /// shaders/skinned3d.vert.glsl's SkinnedParams for the byte layout): 92 floats = 368 bytes.
        /// Same reasoning as kEnvMapUniformFloats -- SkinnedEffect's own field set (per-light
        /// specular, WeightsPerVertex, no alpha test) doesn't fit the shared block, and its shader
        /// pair is never linked with any other shader here.
        constexpr std::size_t kSkinnedUniformFloats = 92;

        /// Floats in SkinnedEffect's SEPARATE per-draw bone transform buffer: 72 `mat4`s (the real
        /// XNA `SkinnedEffect.MaxBones`) = 1152 floats = 4608 bytes -- kept in its own buffer pool
        /// rather than folded into kSkinnedUniformFloats above, mirroring the Vulkan renderer's own
        /// BoneBlock/FogParams UBO split (a buffer this much larger than every other per-draw
        /// uniform block in this renderer does not belong sharing one growth/reuse pool with them).
        constexpr std::size_t kSkinnedBoneFloats = 72 * 16;

        /// Floats in PbrEffect's own dedicated uniform block (see shaders/pbr3d.vert.glsl's
        /// PbrParams for the byte layout): 152 floats = 608 bytes. This effect's field set (base
        /// colour factor kept independent from alpha, raw AmbientLightColor, metallic/roughness
        /// factors, normal/occlusion map scales and alpha coverage) doesn't fit the shared
        /// 100-float Transform block, and its
        /// shader pair is never linked with any other shader here.
        constexpr std::size_t kPbrUniformFloats = 152;

        /// Floats per sprite vertex: position (2), texture coordinate (2), colour (4).
        constexpr std::size_t kSpriteVertexFloats = 8;
        constexpr std::size_t kSpriteVertexStride = kSpriteVertexFloats * sizeof(float);
        constexpr std::size_t kSpriteVerticesPerQuad = 6;

        using XnaBlend = Microsoft::Xna::Framework::Graphics::Blend;
        using XnaBlendFunction = Microsoft::Xna::Framework::Graphics::BlendFunction;
        using XnaTextureFilter = Microsoft::Xna::Framework::Graphics::TextureFilter;
        using XnaTextureAddressMode = Microsoft::Xna::Framework::Graphics::TextureAddressMode;

        LLGL::BlendOp MapBlendFactor(int blend)
        {
            switch (static_cast<XnaBlend>(blend))
            {
                case XnaBlend::One:                       return LLGL::BlendOp::One;
                case XnaBlend::Zero:                      return LLGL::BlendOp::Zero;
                case XnaBlend::SourceColor:               return LLGL::BlendOp::SrcColor;
                case XnaBlend::InverseSourceColor:        return LLGL::BlendOp::InvSrcColor;
                case XnaBlend::SourceAlpha:               return LLGL::BlendOp::SrcAlpha;
                case XnaBlend::InverseSourceAlpha:        return LLGL::BlendOp::InvSrcAlpha;
                case XnaBlend::DestinationColor:          return LLGL::BlendOp::DstColor;
                case XnaBlend::InverseDestinationColor:   return LLGL::BlendOp::InvDstColor;
                case XnaBlend::DestinationAlpha:          return LLGL::BlendOp::DstAlpha;
                case XnaBlend::InverseDestinationAlpha:   return LLGL::BlendOp::InvDstAlpha;
                case XnaBlend::BlendFactor:               return LLGL::BlendOp::BlendFactor;
                case XnaBlend::InverseBlendFactor:        return LLGL::BlendOp::InvBlendFactor;
                case XnaBlend::SourceAlphaSaturation:     return LLGL::BlendOp::SrcAlphaSaturate;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown Blend ordinal " + std::to_string(blend));
        }

        LLGL::BlendArithmetic MapBlendFunction(int function)
        {
            switch (static_cast<XnaBlendFunction>(function))
            {
                case XnaBlendFunction::Add:             return LLGL::BlendArithmetic::Add;
                case XnaBlendFunction::Subtract:        return LLGL::BlendArithmetic::Subtract;
                case XnaBlendFunction::ReverseSubtract: return LLGL::BlendArithmetic::RevSubtract;
                case XnaBlendFunction::Max:             return LLGL::BlendArithmetic::Max;
                case XnaBlendFunction::Min:             return LLGL::BlendArithmetic::Min;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown BlendFunction ordinal " +
                std::to_string(function));
        }

        /// Complete min/mag/mip triple for every XNA TextureFilter, not just the two symmetric
        /// ones -- collapsing the six asymmetric entries onto Linear/Point is exactly the defect
        /// REMED-GFX-170 removed from the other renderers.
        void MapTextureFilter(int filter, LLGL::SamplerFilter& minFilter, LLGL::SamplerFilter& magFilter,
                              LLGL::SamplerFilter& mipFilter, bool& anisotropic)
        {
            constexpr LLGL::SamplerFilter kLinear = LLGL::SamplerFilter::Linear;
            constexpr LLGL::SamplerFilter kPoint  = LLGL::SamplerFilter::Nearest;
            anisotropic = false;

            switch (static_cast<XnaTextureFilter>(filter))
            {
                case XnaTextureFilter::Linear:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; return;
                case XnaTextureFilter::Point:
                    minFilter = kPoint;  magFilter = kPoint;  mipFilter = kPoint;  return;
                case XnaTextureFilter::Anisotropic:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; anisotropic = true; return;
                case XnaTextureFilter::LinearMipPoint:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kPoint;  return;
                case XnaTextureFilter::PointMipLinear:
                    minFilter = kPoint;  magFilter = kPoint;  mipFilter = kLinear; return;
                case XnaTextureFilter::MinLinearMagPointMipLinear:
                    minFilter = kLinear; magFilter = kPoint;  mipFilter = kLinear; return;
                case XnaTextureFilter::MinLinearMagPointMipPoint:
                    minFilter = kLinear; magFilter = kPoint;  mipFilter = kPoint;  return;
                case XnaTextureFilter::MinPointMagLinearMipLinear:
                    minFilter = kPoint;  magFilter = kLinear; mipFilter = kLinear; return;
                case XnaTextureFilter::MinPointMagLinearMipPoint:
                    minFilter = kPoint;  magFilter = kLinear; mipFilter = kPoint;  return;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown TextureFilter ordinal " + std::to_string(filter));
        }

        LLGL::SamplerAddressMode MapAddressMode(int addressMode)
        {
            switch (static_cast<XnaTextureAddressMode>(addressMode))
            {
                case XnaTextureAddressMode::Wrap:   return LLGL::SamplerAddressMode::Repeat;
                case XnaTextureAddressMode::Clamp:  return LLGL::SamplerAddressMode::Clamp;
                case XnaTextureAddressMode::Mirror: return LLGL::SamplerAddressMode::Mirror;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown TextureAddressMode ordinal " +
                std::to_string(addressMode));
        }

        /// True when a blend factor ordinal refers to the constant blend colour, i.e. the only case
        /// in which the pipeline needs dynamic blend-factor state at all.
        bool UsesConstantBlendFactor(int blend)
        {
            const XnaBlend value = static_cast<XnaBlend>(blend);
            return value == XnaBlend::BlendFactor || value == XnaBlend::InverseBlendFactor;
        }

        using XnaCompareFunction = Microsoft::Xna::Framework::Graphics::CompareFunction;
        using XnaCullMode = Microsoft::Xna::Framework::Graphics::CullMode;
        using XnaFillMode = Microsoft::Xna::Framework::Graphics::FillMode;
        using XnaVertexElementFormat = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using XnaVertexElementUsage = Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        LLGL::CompareOp MapCompareFunction(int compareFunction)
        {
            switch (static_cast<XnaCompareFunction>(compareFunction))
            {
                case XnaCompareFunction::Always:       return LLGL::CompareOp::AlwaysPass;
                case XnaCompareFunction::Never:        return LLGL::CompareOp::NeverPass;
                case XnaCompareFunction::Less:         return LLGL::CompareOp::Less;
                case XnaCompareFunction::LessEqual:    return LLGL::CompareOp::LessEqual;
                case XnaCompareFunction::Equal:        return LLGL::CompareOp::Equal;
                case XnaCompareFunction::GreaterEqual: return LLGL::CompareOp::GreaterEqual;
                case XnaCompareFunction::Greater:      return LLGL::CompareOp::Greater;
                case XnaCompareFunction::NotEqual:     return LLGL::CompareOp::NotEqual;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown CompareFunction ordinal " +
                std::to_string(compareFunction));
        }

        /// XNA names the winding it CULLS, and treats clockwise as front-facing (the Direct3D
        /// convention). The pipeline is therefore configured with front = clockwise, which is what
        /// makes "cull the counter-clockwise faces" the same thing as culling back faces.
        LLGL::CullMode MapCullMode(int cullMode)
        {
            switch (static_cast<XnaCullMode>(cullMode))
            {
                case XnaCullMode::None:                    return LLGL::CullMode::Disabled;
                case XnaCullMode::CullClockwiseFace:       return LLGL::CullMode::Front;
                case XnaCullMode::CullCounterClockwiseFace: return LLGL::CullMode::Back;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown CullMode ordinal " + std::to_string(cullMode));
        }

        LLGL::PolygonMode MapFillMode(int fillMode)
        {
            switch (static_cast<XnaFillMode>(fillMode))
            {
                case XnaFillMode::Solid:     return LLGL::PolygonMode::Fill;
                case XnaFillMode::WireFrame: return LLGL::PolygonMode::Wireframe;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown FillMode ordinal " + std::to_string(fillMode));
        }

        /// LLGL-53: XNA StencilOperation -> LLGL::StencilOp (mirrors the Vulkan/EasyGL renderers'
        /// own identical `ToVkStencilOp`/`ToEasyGLStencilOp` mapping): `Increment`/`Decrement` WRAP
        /// (`IncWrap`/`DecWrap`), `IncrementSaturation`/`DecrementSaturation` CLAMP
        /// (`IncClamp`/`DecClamp`) -- confirmed against `StencilOperation`'s own Doxygen wording
        /// ("wrapping to 0" vs "clamping to the maximum value").
        LLGL::StencilOp MapStencilOperation(int stencilOperation)
        {
            using XnaStencilOperation = Microsoft::Xna::Framework::Graphics::StencilOperation;
            switch (static_cast<XnaStencilOperation>(stencilOperation))
            {
                case XnaStencilOperation::Keep:                return LLGL::StencilOp::Keep;
                case XnaStencilOperation::Zero:                return LLGL::StencilOp::Zero;
                case XnaStencilOperation::Replace:             return LLGL::StencilOp::Replace;
                case XnaStencilOperation::Increment:           return LLGL::StencilOp::IncWrap;
                case XnaStencilOperation::Decrement:           return LLGL::StencilOp::DecWrap;
                case XnaStencilOperation::IncrementSaturation: return LLGL::StencilOp::IncClamp;
                case XnaStencilOperation::DecrementSaturation: return LLGL::StencilOp::DecClamp;
                case XnaStencilOperation::Invert:              return LLGL::StencilOp::Invert;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown StencilOperation ordinal " +
                std::to_string(stencilOperation));
        }

        LLGL::PrimitiveTopology MapPrimitiveTopology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return LLGL::PrimitiveTopology::TriangleList;
                case PrimitiveType::TriangleStrip: return LLGL::PrimitiveTopology::TriangleStrip;
                case PrimitiveType::LineList:      return LLGL::PrimitiveTopology::LineList;
                case PrimitiveType::LineStrip:     return LLGL::PrimitiveTopology::LineStrip;
                case PrimitiveType::PointListEXT:  return LLGL::PrimitiveTopology::PointList;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown PrimitiveType ordinal " +
                std::to_string(static_cast<int>(primitive)));
        }

        /// Number of vertices (or indices) a draw of @p primitiveCount primitives consumes.
        int CountElementsForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown PrimitiveType ordinal " +
                std::to_string(static_cast<int>(primitive)));
        }

        LLGL::Format MapVertexElementFormat(XnaVertexElementFormat format)
        {
            switch (format)
            {
                case XnaVertexElementFormat::Single:  return LLGL::Format::R32Float;
                case XnaVertexElementFormat::Vector2: return LLGL::Format::RG32Float;
                case XnaVertexElementFormat::Vector3: return LLGL::Format::RGB32Float;
                case XnaVertexElementFormat::Vector4: return LLGL::Format::RGBA32Float;
                // XNA's packed Color is four normalized bytes. CNA's Color stores them R,G,B,A in
                // memory (its packed value is AABBGGRR little-endian), so this is RGBA8UNorm and
                // not the BGRA8 the XNA documentation's own naming might suggest.
                case XnaVertexElementFormat::Color:   return LLGL::Format::RGBA8UNorm;
                case XnaVertexElementFormat::Byte4:   return LLGL::Format::RGBA8UInt;
                case XnaVertexElementFormat::Short2:  return LLGL::Format::RG16SInt;
                case XnaVertexElementFormat::Short4:  return LLGL::Format::RGBA16SInt;
                case XnaVertexElementFormat::NormalizedShort2: return LLGL::Format::RG16SNorm;
                case XnaVertexElementFormat::NormalizedShort4: return LLGL::Format::RGBA16SNorm;
                case XnaVertexElementFormat::HalfVector2: return LLGL::Format::RG16Float;
                case XnaVertexElementFormat::HalfVector4: return LLGL::Format::RGBA16Float;
            }
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: unknown VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// Shader-side name and attribute location for a vertex usage.
        ///
        /// Locations are assigned by USAGE, not by the order elements happen to appear in the
        /// declaration, so two declarations that carry the same semantics in a different order
        /// still feed the same shader inputs. Only the usages the 3D shaders actually declare are
        /// mapped; anything else is refused rather than silently bound to a location the shader
        /// reads as something different.
        bool MapVertexUsage(XnaVertexElementUsage usage, int usageIndex,
                            const char*& name, std::uint32_t& location)
        {
            if (usage == XnaVertexElementUsage::TextureCoordinate && usageIndex == 1)
            {
                name = "texCoord1";
                location = 7;
                return true;
            }
            if (usageIndex != 0)
                return false;
            switch (usage)
            {
                case XnaVertexElementUsage::Position:          name = "position"; location = 0; return true;
                case XnaVertexElementUsage::Color:             name = "color";    location = 1; return true;
                case XnaVertexElementUsage::TextureCoordinate: name = "texCoord"; location = 2; return true;
                case XnaVertexElementUsage::Normal:            name = "normal";   location = 3; return true;
                // SkinnedEffect (LLGL-25): matches the locations the declaration-less stride-52
                // fallback below (case 52) and env_map3d/skinned3d shaders' own attribute
                // declarations already use.
                case XnaVertexElementUsage::BlendWeight:       name = "aBoneWeights"; location = 4; return true;
                case XnaVertexElementUsage::BlendIndices:      name = "aBoneIndices"; location = 5; return true;
                // PbrEffect (LLGL-26 follow-up): the tangent-space TBN basis pbr3d.vert.glsl's own
                // attribute declaration reads at this location.
                case XnaVertexElementUsage::Tangent:           name = "tangent";      location = 6; return true;
                default: return false;
            }
        }

        std::uint64_t MakeVertexLayoutKey(const std::vector<LLGL::VertexAttribute>& attributes)
        {
            // FNV-1a over the fields that genuinely change the input layout. Two layouts that hash
            // equal really are the same layout; nothing else about the buffer takes part.
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            for (const LLGL::VertexAttribute& attribute : attributes)
            {
                mix(static_cast<std::uint64_t>(attribute.location));
                mix(static_cast<std::uint64_t>(attribute.format));
                mix(static_cast<std::uint64_t>(attribute.offset));
                mix(static_cast<std::uint64_t>(attribute.stride));
            }
            return hash;
        }

        std::uint8_t MapColorWriteMask(int colorWriteChannels)
        {
            std::uint8_t mask = 0;
            if (ColorWriteHasRed(colorWriteChannels))   mask |= LLGL::ColorMaskFlags::R;
            if (ColorWriteHasGreen(colorWriteChannels)) mask |= LLGL::ColorMaskFlags::G;
            if (ColorWriteHasBlue(colorWriteChannels))  mask |= LLGL::ColorMaskFlags::B;
            if (ColorWriteHasAlpha(colorWriteChannels)) mask |= LLGL::ColorMaskFlags::A;
            return mask;
        }

        LLGL::VertexFormat MakeSpriteVertexFormat()
        {
            LLGL::VertexFormat format;
            format.AppendAttribute({"position", LLGL::Format::RG32Float});
            format.AppendAttribute({"texCoord", LLGL::Format::RG32Float});
            format.AppendAttribute({"color", LLGL::Format::RGBA32Float});
            format.SetStride(static_cast<std::uint32_t>(kSpriteVertexStride));
            return format;
        }

        bool SupportsShadingLanguage(const LLGL::RenderingCapabilities& caps, LLGL::ShadingLanguage language)
        {
            return std::find(caps.shadingLanguages.begin(), caps.shadingLanguages.end(), language) !=
                   caps.shadingLanguages.end();
        }

        // ---- Runtime GLSL->SPIR-V compile for LlglEffectRenderer (LLGL-27) ----
        // No libshaderc-dev package is available in this environment (see BackendLibraries.cmake's
        // own find_library fallback comment), so there is no shaderc.h to include -- these
        // extern "C" prototypes are hand-declared to match the real C ABI exactly, the same
        // minimal subset another renderer already proved correct against the identical shared
        // library. Opaque handles are
        // all void*; shaderc_shader_kind/shaderc_optimization_level are plain C enums, passed as
        // int.
        extern "C"
        {
            void* shaderc_compiler_initialize();
            void shaderc_compiler_release(void*);
            void* shaderc_compile_options_initialize();
            void shaderc_compile_options_release(void*);
            void shaderc_compile_options_set_optimization_level(void*, int);
            void* shaderc_compile_into_spv(void* compiler, const char* source_text, std::size_t source_text_size,
                                          int shader_kind, const char* input_file_name,
                                          const char* entry_point_name, void* options);
            int shaderc_result_get_compilation_status(void*);
            const char* shaderc_result_get_error_message(void*);
            std::size_t shaderc_result_get_length(void*);
            const char* shaderc_result_get_bytes(void*);
            void shaderc_result_release(void*);
        }

        constexpr int kShadercVertexShader = 0;    // shaderc_glsl_vertex_shader
        constexpr int kShadercFragmentShader = 1;  // shaderc_glsl_fragment_shader
        constexpr int kShadercOptPerformance = 2;  // shaderc_optimization_level_performance

        // Compiles @p source (GLSL) to SPIR-V, replacing @p outSpirv on success. Returns true on
        // success; on failure @p outError holds shaderc's own error message and @p outSpirv is
        // left untouched.
        bool CompileGlslToSpirv(const std::string& source, int shaderKind, const char* filename,
                                std::vector<std::uint8_t>& outSpirv, std::string& outError)
        {
            void* compiler = shaderc_compiler_initialize();
            void* options = shaderc_compile_options_initialize();
            shaderc_compile_options_set_optimization_level(options, kShadercOptPerformance);

            void* result = shaderc_compile_into_spv(compiler, source.data(), source.size(), shaderKind,
                                                    filename, "main", options);

            const int status = shaderc_result_get_compilation_status(result);
            if (status != 0)
            {
                const char* err = shaderc_result_get_error_message(result);
                outError = err != nullptr ? err : "shader compilation failed (no error message)";
                shaderc_result_release(result);
                shaderc_compile_options_release(options);
                shaderc_compiler_release(compiler);
                return false;
            }

            const std::size_t length = shaderc_result_get_length(result);
            const char* bytes = shaderc_result_get_bytes(result);
            outSpirv.assign(bytes, bytes + length);

            shaderc_result_release(result);
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
            return true;
        }

        // LLGL-47: AcquireCustomEffectLayoutEXT() below builds exactly ONE VkDescriptorSetLayout
        // (Vulkan descriptor set 0); creating a VkPipeline from a SPIR-V module that references any
        // OTHER descriptor set is undefined per the Vulkan spec and previously crashed the driver
        // (LLGL::VKGraphicsPSO::CreateVkPipeline, see known_bugs.md) instead of failing cleanly,
        // since shaderc itself compiles multi-set GLSL without complaint. Scanning compiled SPIR-V
        // for OpDecorate/DescriptorSet is a minimal, targeted read of one well-defined, stable part
        // of the binary format -- not a full reflection library (this class's own doc comment
        // already declines SPIRV-Cross as an added dependency, and this scan needs none either).
        bool SpirvUsesOnlyDescriptorSetZero(const std::vector<std::uint8_t>& spirv)
        {
            constexpr std::uint32_t kSpirvMagic = 0x07230203u;
            constexpr std::uint32_t kOpDecorate = 71u;
            constexpr std::uint32_t kDecorationDescriptorSet = 34u;
            constexpr std::size_t kHeaderWords = 5;

            if (spirv.size() < kHeaderWords * sizeof(std::uint32_t))
                return true; // too short to be a real module -- let shader creation report the error

            std::uint32_t magic = 0;
            std::memcpy(&magic, spirv.data(), sizeof(magic));
            if (magic != kSpirvMagic)
                return true; // unexpected byte order/format -- do not guess, let LLGL validate it

            const std::size_t totalWords = spirv.size() / sizeof(std::uint32_t);
            std::size_t wordIndex = kHeaderWords;
            while (wordIndex < totalWords)
            {
                std::uint32_t instructionHeader = 0;
                std::memcpy(&instructionHeader, spirv.data() + wordIndex * sizeof(std::uint32_t),
                           sizeof(instructionHeader));
                const std::uint32_t wordCount = instructionHeader >> 16;
                const std::uint32_t opcode = instructionHeader & 0xFFFFu;
                // Malformed (a zero-length instruction, or one reaching past the module's own word
                // count) -- stop scanning and let LLGL's own shader creation report the real error
                // instead of reading further into memory this module never claimed.
                if (wordCount == 0 || wordIndex + wordCount > totalWords)
                    break;

                // OpDecorate %target DescriptorSet <literal>: target (word 1), decoration (word 2),
                // literal (word 3) -- four words total including the instruction header itself.
                if (opcode == kOpDecorate && wordCount >= 4)
                {
                    std::uint32_t decoration = 0;
                    std::memcpy(&decoration, spirv.data() + (wordIndex + 2) * sizeof(std::uint32_t),
                               sizeof(decoration));
                    if (decoration == kDecorationDescriptorSet)
                    {
                        std::uint32_t setNumber = 0;
                        std::memcpy(&setNumber, spirv.data() + (wordIndex + 3) * sizeof(std::uint32_t),
                                   sizeof(setNumber));
                        if (setNumber != 0)
                            return false;
                    }
                }

                wordIndex += wordCount;
            }
            return true;
        }
    }

    /// A sampled resource resolved from whichever concrete ITextureRenderer the caller handed in.
    /// Deliberately not a member of either texture-owning class: SpriteBatch and the 3D effect path
    /// both accept an arbitrary ITextureRenderer&, which is a plain Texture2D in the common case but
    /// is exactly as often the ITextureRenderer a RenderTarget2D hands back when it is sampled as a
    /// texture (RenderTarget2D inherits Texture2D, and Texture2D::GetRenderer() returns the same
    /// renderer_ pointer regardless of which concrete subtype constructed it) -- render-to-texture
    /// only works at all if both are accepted here.
    struct ResolvedSampledTexture
    {
        LLGL::Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };

    static ResolvedSampledTexture ResolveSampledTexture(const ITextureRenderer& texture)
    {
        if (const auto* plain = dynamic_cast<const LlglTextureRenderer*>(&texture))
            return {plain->GetLlglTexture(), plain->GetWidth(), plain->GetHeight()};
        if (const auto* target = dynamic_cast<const LlglRenderTargetRenderer*>(&texture))
            return {target->GetLlglColorTexture(), target->GetWidth(), target->GetHeight()};
        return {};
    }

    /// Cube-map equivalent of ResolveSampledTexture() above -- EnvironmentMapEffect's own
    /// `EnvironmentMap` property accepts an arbitrary ITextureCubeRenderer&, which is exactly as
    /// often the ITextureCubeRenderer a RenderTargetCube hands back when sampled as a TextureCube
    /// (RenderTargetCube inherits TextureCube) as a plain uploaded TextureCube. A hard
    /// dynamic_cast<const LlglTextureCubeRenderer*> alone would silently fail to sample a
    /// RenderTargetCube face -- the entire real-time-reflection use case this feature exists for.
    static LLGL::Texture* ResolveSampledTextureCube(const ITextureCubeRenderer& cube)
    {
        if (const auto* plain = dynamic_cast<const LlglTextureCubeRenderer*>(&cube))
            return plain->GetLlglTexture();
        if (const auto* target = dynamic_cast<const LlglRenderTargetCubeRenderer*>(&cube))
            return target->GetLlglTexture();
        return nullptr;
    }

    // -----------------------------------------------------------------------------------------
    // LlglTextureRenderer
    // -----------------------------------------------------------------------------------------

    LlglTextureRenderer::LlglTextureRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                                           LLGL::Texture* texture, int width, int height, int mipLevels)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , texture_(texture)
        , width_(width)
        , height_(height)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr || texture_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: texture creation failed");
    }

    LlglTextureRenderer::~LlglTextureRenderer()
    {
        if (texture_ == nullptr)
            return;

        if (owner_ != nullptr)
            owner_->ScheduleTextureReleaseEXT(texture_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*texture_);
    }

    void LlglTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (rgba == nullptr || width_ <= 0 || height_ <= 0)
            return;

        const int tightStride = width_ * 4;
        const int rowStride = stride > 0 ? stride : tightStride;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = 0;
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = rgba;
        imageView.dataSize = static_cast<std::size_t>(rowStride) * static_cast<std::size_t>(height_);
        imageView.rowStride = static_cast<std::uint32_t>(rowStride);

        renderSystem_->WriteTexture(*texture_, region, imageView);
    }

    void LlglTextureRenderer::UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= mipLevels_ || levelW <= 0 || levelH <= 0)
            return;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = rgba;
        imageView.dataSize = static_cast<std::size_t>(levelW) * static_cast<std::size_t>(levelH) * 4u;

        renderSystem_->WriteTexture(*texture_, region, imageView);
    }

    bool LlglTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                      void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*texture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglTextureCubeRenderer
    // -----------------------------------------------------------------------------------------

    LlglTextureCubeRenderer::LlglTextureCubeRenderer(LLGL::RenderSystem* renderSystem,
                                                    LlglRenderer* owner,
                                                    LLGL::Texture* texture, int size, int mipLevels)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , texture_(texture)
        , size_(size)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: cube texture creation failed");

        if (texture_ == nullptr)
        {
            cpuFaces_.resize(static_cast<std::size_t>(mipLevels_) * 6u);
            for (int level = 0; level < mipLevels_; ++level)
            {
                const int dimension = std::max(1, size_ >> level);
                const std::size_t bytes =
                    static_cast<std::size_t>(dimension) * static_cast<std::size_t>(dimension) * 4u;
                for (int face = 0; face < 6; ++face)
                    cpuFaces_[static_cast<std::size_t>(level) * 6u + face].assign(bytes, 0u);
            }
        }
    }

    LlglTextureCubeRenderer::~LlglTextureCubeRenderer()
    {
        if (texture_ == nullptr)
            return;

        if (owner_ != nullptr)
            owner_->ScheduleTextureReleaseEXT(texture_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*texture_);
    }

    bool LlglTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        if (data == nullptr || w <= 0 || h <= 0 || face < 0 || face >= 6 ||
            level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        if (texture_ == nullptr)
        {
            const int dimension = std::max(1, size_ >> level);
            if (x < 0 || y < 0 || x + w > dimension || y + h > dimension)
                return false;
            std::vector<std::uint8_t>& destination =
                cpuFaces_[static_cast<std::size_t>(level) * 6u + static_cast<std::size_t>(face)];
            const auto* source = static_cast<const std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(destination.data() +
                                (static_cast<std::size_t>(y + row) * dimension + x) * 4u,
                            source + static_cast<std::size_t>(row) * w * 4u,
                            static_cast<std::size_t>(w) * 4u);
            }
            return true;
        }

        LLGL::TextureRegion region;
        region.subresource.baseArrayLayer = static_cast<std::uint32_t>(face);
        region.subresource.numArrayLayers = 1;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->WriteTexture(*texture_, region, imageView);
        return true;
    }

    bool LlglTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                         void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || face < 0 || face >= 6 ||
            level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        if (texture_ == nullptr)
        {
            const int dimension = std::max(1, size_ >> level);
            if (x < 0 || y < 0 || x + w > dimension || y + h > dimension)
                return false;
            const std::vector<std::uint8_t>& source =
                cpuFaces_[static_cast<std::size_t>(level) * 6u + static_cast<std::size_t>(face)];
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(destination + static_cast<std::size_t>(row) * w * 4u,
                            source.data() +
                                (static_cast<std::size_t>(y + row) * dimension + x) * 4u,
                            static_cast<std::size_t>(w) * 4u);
            }
            return true;
        }

        LLGL::TextureRegion region;
        region.subresource.baseArrayLayer = static_cast<std::uint32_t>(face);
        region.subresource.numArrayLayers = 1;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*texture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglTexture3DRenderer
    // -----------------------------------------------------------------------------------------

    LlglTexture3DRenderer::LlglTexture3DRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                                               LLGL::Texture* texture, int width, int height, int depth,
                                               int mipLevels)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , texture_(texture)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr || texture_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: volume texture creation failed");
    }

    LlglTexture3DRenderer::~LlglTexture3DRenderer()
    {
        if (texture_ == nullptr)
            return;

        if (owner_ != nullptr)
            owner_->ScheduleTextureReleaseEXT(texture_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*texture_);
    }

    bool LlglTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0 ||
            level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                                     static_cast<std::size_t>(depth) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, z};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                         static_cast<std::uint32_t>(depth)};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->WriteTexture(*texture_, region, imageView);
        return true;
    }

    bool LlglTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                       void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0 ||
            level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                                     static_cast<std::size_t>(depth) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, z};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                         static_cast<std::uint32_t>(depth)};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*texture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglRenderTargetRenderer
    // -----------------------------------------------------------------------------------------

    LlglRenderTargetRenderer::LlglRenderTargetRenderer(LLGL::RenderSystem* renderSystem,
                                                      LLGL::RenderTarget* renderTarget,
                                                      LLGL::Texture* colorTexture,
                                                      LLGL::Texture* depthTexture,
                                                      int width, int height, bool hasRealDepthBuffer,
                                                      LLGL::Buffer* spriteProjectionBuffer,
                                                      LlglRenderer* owner,
                                                      int multiSampleCount, int mipLevels)
        : renderSystem_(renderSystem)
        , renderTarget_(renderTarget)
        , colorTexture_(colorTexture)
        , depthTexture_(depthTexture)
        , width_(width)
        , height_(height)
        , hasRealDepthBuffer_(hasRealDepthBuffer)
        , spriteProjectionBuffer_(spriteProjectionBuffer)
        , owner_(owner)
        , multiSampleCount_(multiSampleCount > 0 ? multiSampleCount : 1)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr || renderTarget_ == nullptr || colorTexture_ == nullptr ||
            spriteProjectionBuffer_ == nullptr)
        {
            throw std::runtime_error(std::string(kRendererName) + " renderer: render target creation failed");
        }
    }

    LlglRenderTargetRenderer::~LlglRenderTargetRenderer()
    {
        if (renderSystem_ == nullptr)
            return;

        // Deferred, like LlglVertexBufferRenderer/LlglIndexBufferRenderer: a RenderTarget2D that
        // goes out of scope before Present() is a perfectly normal pattern (create it, draw into
        // it, sample it, let it die, all within one Draw()), and frameCommands_/FrameCommandBucket
        // may still reference it by raw pointer at that point.
        if (owner_ != nullptr)
        {
            owner_->ScheduleRenderTargetReleaseEXT(renderTarget_, colorTexture_, depthTexture_,
                                                    spriteProjectionBuffer_);
            return;
        }

        if (renderTarget_ != nullptr)
            renderSystem_->Release(*renderTarget_);
        // depthTexture_ is null whenever the depth/stencil attachment was created anonymously (the
        // current CreateRenderTarget2D path always does this): LLGL then owns that buffer as part
        // of the RenderTarget itself and releases it above. This branch only matters if a future
        // caller ever hands this class a concrete depth texture of its own.
        if (depthTexture_ != nullptr)
            renderSystem_->Release(*depthTexture_);
        if (colorTexture_ != nullptr)
            renderSystem_->Release(*colorTexture_);
        if (spriteProjectionBuffer_ != nullptr)
            renderSystem_->Release(*spriteProjectionBuffer_);
    }

    bool LlglRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || level < 0 || level >= mipLevels_ ||
            renderSystem_ == nullptr || colorTexture_ == nullptr)
        {
            return false;
        }

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        // This target's content comes only from draws recorded into the owning renderer's
        // frameCommands_ -- unlike a plain Texture2D, which arrives through an immediate
        // WriteTexture. Reading the colour attachment before those are submitted would see
        // whatever the GPU allocator happened to leave there, not what was actually drawn.
        // For a mip-mapped target (mipLevels_ > 1) this ALSO flushes the GenerateMips() call
        // RecordAndSubmitFrame() queues right after level 0's own render pass -- level 1.. never
        // has real content before that runs.
        if (owner_ != nullptr)
            owner_->FlushPendingFrameEXT();

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*colorTexture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglRenderTargetCubeRenderer
    // -----------------------------------------------------------------------------------------

    LlglRenderTargetCubeRenderer::LlglRenderTargetCubeRenderer(
        LLGL::RenderSystem* renderSystem, LLGL::Texture* cubeTexture, LLGL::Texture* depthTexture,
        const std::array<LLGL::RenderTarget*, 6>& faceTargets, int size, bool hasRealDepthBuffer,
        LLGL::Buffer* spriteProjectionBuffer, LlglRenderer* owner, int multiSampleCount,
        int mipLevels)
        : renderSystem_(renderSystem)
        , cubeTexture_(cubeTexture)
        , depthTexture_(depthTexture)
        , faceTargets_(faceTargets)
        , size_(size)
        , hasRealDepthBuffer_(hasRealDepthBuffer)
        , spriteProjectionBuffer_(spriteProjectionBuffer)
        , owner_(owner)
        , multiSampleCount_(multiSampleCount > 0 ? multiSampleCount : 1)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr || cubeTexture_ == nullptr || depthTexture_ == nullptr ||
            spriteProjectionBuffer_ == nullptr)
        {
            throw std::runtime_error(std::string(kRendererName) + " renderer: render target cube creation failed");
        }
        for (int face = 0; face < 6; ++face)
        {
            if (faceTargets_[static_cast<std::size_t>(face)] == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: render target cube creation failed");
            }
            faceBindings_[static_cast<std::size_t>(face)] = LlglRenderTargetCubeFaceBinding(
                faceTargets_[static_cast<std::size_t>(face)], size_, spriteProjectionBuffer_,
                multiSampleCount_, mipLevels_ > 1 ? cubeTexture_ : nullptr);
        }
    }

    LlglRenderTargetCubeRenderer::~LlglRenderTargetCubeRenderer()
    {
        if (renderSystem_ == nullptr)
            return;

        // Deferred, like LlglRenderTargetRenderer -- a RenderTargetCube that goes out of scope
        // before Present() is a perfectly normal pattern, and frameCommands_/FrameCommandBucket may
        // still reference any of its 6 faces by raw pointer at that point. The shared colour/depth
        // texture and the shared sprite projection buffer must each be released exactly once, so
        // only the FIRST of the 6 calls passes them through -- ScheduleRenderTargetReleaseEXT
        // already treats a null texture/buffer argument as "nothing to do" for that slot.
        if (owner_ != nullptr)
        {
            for (int face = 0; face < 6; ++face)
            {
                owner_->ScheduleRenderTargetReleaseEXT(
                    faceTargets_[static_cast<std::size_t>(face)],
                    face == 0 ? cubeTexture_ : nullptr,
                    face == 0 ? depthTexture_ : nullptr,
                    face == 0 ? spriteProjectionBuffer_ : nullptr);
            }
            return;
        }

        for (LLGL::RenderTarget* renderTarget : faceTargets_)
        {
            if (renderTarget != nullptr)
                renderSystem_->Release(*renderTarget);
        }
        if (depthTexture_ != nullptr)
            renderSystem_->Release(*depthTexture_);
        if (cubeTexture_ != nullptr)
            renderSystem_->Release(*cubeTexture_);
        if (spriteProjectionBuffer_ != nullptr)
            renderSystem_->Release(*spriteProjectionBuffer_);
    }

    bool LlglRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || face < 0 || face >= 6 ||
            level < 0 || level >= mipLevels_ || renderSystem_ == nullptr || cubeTexture_ == nullptr)
        {
            return false;
        }

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        // Same reasoning as LlglRenderTargetRenderer::GetData: this face's content comes only from
        // draws recorded into the owning renderer's frameCommands_, so those must reach the GPU
        // before reading the colour attachment back. For a mip-mapped cube (mipLevels_ > 1) this
        // ALSO flushes the GenerateMips() call RecordAndSubmitFrame() queues right after level 0's
        // own render pass -- level 1.. never has real content before that runs.
        if (owner_ != nullptr)
            owner_->FlushPendingFrameEXT();

        LLGL::TextureRegion region;
        region.subresource.baseArrayLayer = static_cast<std::uint32_t>(face);
        region.subresource.numArrayLayers = 1;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*cubeTexture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglOcclusionQueryRenderer
    // -----------------------------------------------------------------------------------------

    LlglOcclusionQueryRenderer::LlglOcclusionQueryRenderer(LLGL::RenderSystem* renderSystem,
                                                          LLGL::CommandQueue* queue,
                                                          LlglRenderer* owner)
        : renderSystem_(renderSystem)
        , queue_(queue)
        , owner_(owner)
    {
        if (renderSystem_ == nullptr || queue_ == nullptr || owner_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: occlusion query creation failed");
    }

    LlglOcclusionQueryRenderer::~LlglOcclusionQueryRenderer()
    {
        if (queryHeap_ != nullptr)
            owner_->ScheduleQueryHeapReleaseEXT(queryHeap_);
    }

    void LlglOcclusionQueryRenderer::Begin()
    {
        // Never reused across cycles -- see this class's own doc comment for why (LLGL 0.04b's
        // Vulkan module never resets a query pool between uses).
        if (queryHeap_ != nullptr)
            owner_->ScheduleQueryHeapReleaseEXT(queryHeap_);

        LLGL::QueryHeapDescriptor queryDesc;
        queryDesc.type = LLGL::QueryType::SamplesPassed;
        queryDesc.numQueries = 1;
        queryHeap_ = renderSystem_->CreateQueryHeap(queryDesc);
        if (queryHeap_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: query heap creation failed");

        hasResult_ = false;
        pixelCount_ = 0;
        owner_->QueueQueryBeginEXT(queryHeap_);
    }

    void LlglOcclusionQueryRenderer::End()
    {
        if (queryHeap_ == nullptr)
            return;

        owner_->QueueQueryEndEXT(queryHeap_);
    }

    void LlglOcclusionQueryRenderer::ResolveResultEXT() const
    {
        if (queryHeap_ == nullptr || hasResult_)
            return;

        // This query's End() may still be sitting unsubmitted in the owning renderer's frame --
        // LLGL requires BeginQuery/EndQuery to be recorded inside an open render pass, which this
        // renderer only opens at submit time. Forcing a submit-and-wait here is what makes
        // IsComplete()/PixelCount() always answer correctly rather than genuinely asynchronously
        // (see this class's own doc comment).
        owner_->FlushPendingFrameEXT();

        std::uint64_t result = 0;
        if (queue_->QueryResult(*queryHeap_, 0, 1, &result, sizeof(result)))
        {
            pixelCount_ = result;
            hasResult_ = true;
        }
    }

    bool LlglOcclusionQueryRenderer::IsComplete() const
    {
        ResolveResultEXT();
        return hasResult_;
    }

    int LlglOcclusionQueryRenderer::PixelCount() const
    {
        ResolveResultEXT();
        return static_cast<int>(pixelCount_);
    }

    // -----------------------------------------------------------------------------------------
    // LlglEffectRenderer
    // -----------------------------------------------------------------------------------------

    LlglEffectRenderer::LlglEffectRenderer(LlglRenderer& owner)
        : owner_(owner)
    {
    }

    LlglEffectRenderer::~LlglEffectRenderer()
    {
        owner_.ClearCurrentCustomEffectEXT(this);
        owner_.ScheduleEffectResourceReleaseEXT(vertexShader_, fragmentShader_, pipelineCache_);
    }

    bool LlglEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        valid_ = false;

        LLGL::RenderSystem* renderSystem = owner_.GetRenderSystemEXT();
        const LLGL::RenderingCapabilities& caps = owner_.GetRenderingCapsEXT();

        LLGL::ShaderDescriptor vertexDesc;
        LLGL::ShaderDescriptor fragmentDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        fragmentDesc.type = LLGL::ShaderType::Fragment;

        // GLSL is preferred wherever the module offers it, for the same reason the stock sprite
        // shader selection prefers it (LLGL-17): a modern OpenGL module reports SPIR-V too, but
        // this project has no reason to run a GLSL->SPIR-V->GL round trip when the module already
        // accepts the GLSL source directly.
        std::vector<std::uint8_t> vertSpirv;
        std::vector<std::uint8_t> fragSpirv;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = vertSrc.c_str();
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
            fragmentDesc.source = fragSrc.c_str();
            fragmentDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::SPIRV))
        {
            std::string shadercError;
            if (!CompileGlslToSpirv(vertSrc, kShadercVertexShader, "ShaderEffect.vert", vertSpirv, shadercError))
            {
                compileError_ = "vertex shader: " + shadercError;
                return false;
            }
            if (!CompileGlslToSpirv(fragSrc, kShadercFragmentShader, "ShaderEffect.frag", fragSpirv, shadercError))
            {
                compileError_ = "fragment shader: " + shadercError;
                return false;
            }

            // LLGL-47: reject BEFORE shader/pipeline creation rather than crash inside the Vulkan
            // driver later -- see SpirvUsesOnlyDescriptorSetZero()'s own doc comment.
            if (!SpirvUsesOnlyDescriptorSetZero(vertSpirv) || !SpirvUsesOnlyDescriptorSetZero(fragSpirv))
            {
                compileError_ = "shader references a Vulkan descriptor set other than 0, which this "
                                "renderer's single-descriptor-set custom-effect pipeline layout does "
                                "not support";
                return false;
            }

            vertexDesc.source = reinterpret_cast<const char*>(vertSpirv.data());
            vertexDesc.sourceSize = vertSpirv.size();
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";

            fragmentDesc.source = reinterpret_cast<const char*>(fragSpirv.data());
            fragmentDesc.sourceSize = fragSpirv.size();
            fragmentDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            fragmentDesc.entryPoint = "main";
        }
        else
        {
            compileError_ = "the loaded module accepts neither SPIR-V nor GLSL, and this renderer ships no other shader form";
            return false;
        }

        // Scoped to the stock sprite vertex layout (position/texCoord/color) -- see this class's
        // own doc comment for why, matching the native Vulkan renderer's own
        // VulkanEffectRenderer::GetOrCreatePipeline precedent.
        vertexDesc.vertex.inputAttribs = MakeSpriteVertexFormat().attributes;

        vertexShader_ = renderSystem->CreateShader(vertexDesc);
        fragmentShader_ = renderSystem->CreateShader(fragmentDesc);

        for (LLGL::Shader* shader : {vertexShader_, fragmentShader_})
        {
            if (shader == nullptr)
            {
                compileError_ = "shader creation failed";
                return false;
            }
            if (const LLGL::Report* report = shader->GetReport())
            {
                if (report->HasErrors())
                {
                    compileError_ = report->GetText() != nullptr ? report->GetText()
                                                                  : "shader compilation failed (no details)";
                    return false;
                }
            }
        }

        valid_ = true;
        return true;
    }

    void LlglEffectRenderer::Bind()
    {
        if (valid_)
            owner_.SetCurrentCustomEffectEXT(this);
    }

    void LlglEffectRenderer::Unbind()
    {
        owner_.ClearCurrentCustomEffectEXT(this);
    }

    bool LlglEffectRenderer::IsValid() const
    {
        return valid_;
    }

    std::string LlglEffectRenderer::GetCompileError() const
    {
        return compileError_;
    }

    void LlglEffectRenderer::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        std::memcpy(uniformStaging_ + 4, matrix, sizeof(float) * 16);
    }

    void LlglEffectRenderer::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        uniformStaging_[20] = x; uniformStaging_[21] = y; uniformStaging_[22] = z; uniformStaging_[23] = w;
    }

    void LlglEffectRenderer::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        uniformStaging_[20] = x; uniformStaging_[21] = y; uniformStaging_[22] = z;
    }

    void LlglEffectRenderer::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        uniformStaging_[20] = x; uniformStaging_[21] = y;
    }

    void LlglEffectRenderer::SetUniformFloat(const char* /*name*/, float value)
    {
        uniformStaging_[24] = value;
    }

    void LlglEffectRenderer::SetUniformInt(const char* /*name*/, int value)
    {
        uniformStaging_[24] = static_cast<float>(value);
    }

    LLGL::PipelineState* LlglEffectRenderer::AcquirePipeline(std::uint64_t blendKey, bool scissorEnabled,
                                                             int colorAttachmentCount)
    {
        const auto cached = pipelineCache_.find(blendKey);
        if (cached != pipelineCache_.end())
            return cached->second;

        LLGL::GraphicsPipelineDescriptor pipelineDesc;
        pipelineDesc.debugName = "CNA.ShaderEffect";
        pipelineDesc.vertexShader = vertexShader_;
        pipelineDesc.fragmentShader = fragmentShader_;
        pipelineDesc.pipelineLayout = owner_.AcquireCustomEffectLayoutEXT();
        pipelineDesc.renderPass = owner_.GetPrimaryRenderPassEXT();
        pipelineDesc.primitiveTopology = LLGL::PrimitiveTopology::TriangleList;
        owner_.FillCurrentBlendAndRasterStateEXT(pipelineDesc, scissorEnabled, colorAttachmentCount);

        LLGL::PipelineState* pipeline = owner_.GetRenderSystemEXT()->CreatePipelineState(pipelineDesc);
        if (pipeline == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: custom effect pipeline creation failed");
        }
        if (const LLGL::Report* report = pipeline->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: custom effect pipeline link failed: " +
                    (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        pipelineCache_.emplace(blendKey, pipeline);
        return pipeline;
    }

    // -----------------------------------------------------------------------------------------
    // LlglVertexBufferRenderer
    // -----------------------------------------------------------------------------------------

    LlglVertexBufferRenderer::LlglVertexBufferRenderer(LLGL::RenderSystem* renderSystem,
                                                      LlglRenderer* owner, int vertexCapacity)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , vertexCapacity_(vertexCapacity > 0 ? vertexCapacity : 0)
    {
        if (renderSystem_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: null render system for vertex buffer");
    }

    LlglVertexBufferRenderer::~LlglVertexBufferRenderer()
    {
        if (buffer_ == nullptr)
            return;

        // Handed to the renderer rather than released here: a frame recorded earlier this tick may
        // still refer to this buffer, and it is only submitted at Present().
        if (owner_ != nullptr)
            owner_->ScheduleBufferReleaseEXT(buffer_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*buffer_);
    }

    void LlglVertexBufferRenderer::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }

        ResolveVertexAttributes(strideInBytes);

        // LLGL-46: a draw already queued (but not yet submitted) this frame may still reference
        // buffer_'s CURRENT content by raw pointer (QueuePrimitives() only stores the
        // LLGL::Buffer*, replayed later at Present()/FlushPendingFrameEXT() time) -- writing in
        // place or releasing it to grow would corrupt or crash that earlier draw once it finally
        // replays. Flushing runs every such draw against its own correct content right now
        // (a no-op when nothing is queued, e.g. this buffer's very first upload, buffer_ still
        // null), so it is always safe to write in place or reallocate immediately afterward. Every
        // GraphicsDevice::DrawUserPrimitives()/DrawUserIndexedPrimitives() overload's own per-draw
        // temp buffer never reaches this (buffer_ is still null on its one and only SetData call),
        // so this cannot interact with that unrelated internal mechanism.
        if (buffer_ != nullptr && owner_ != nullptr)
            owner_->FlushPendingFrameEXT();

        const std::size_t byteSize = static_cast<std::size_t>(vertexCount) * strideInBytes;
        if (buffer_ == nullptr || byteSize > byteCapacity_)
        {
            if (buffer_ != nullptr)
                renderSystem_->Release(*buffer_);

            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.stride = static_cast<std::uint32_t>(strideInBytes);
            bufferDesc.bindFlags = LLGL::BindFlags::VertexBuffer;
            // The attributes go on the buffer as well as on the shader: OpenGL builds this
            // buffer's vertex array from them, while Vulkan takes the layout from the shader. Both
            // come from the same translation, so the two cannot drift apart.
            bufferDesc.vertexAttribs = attributes_;
            buffer_ = renderSystem_->CreateBuffer(bufferDesc, data);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: vertex buffer creation failed");
            byteCapacity_ = byteSize;
        }
        else
        {
            renderSystem_->WriteBuffer(*buffer_, 0, data, byteSize);
        }

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void LlglVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // Stored, not translated yet: the translation needs the upload stride as a fallback for a
        // declaration that reports none, and SetData() has not run at this point.
        declaration_ = vertexDeclaration;
        hasDeclaration_ = true;
    }

    void LlglVertexBufferRenderer::ResolveVertexAttributes(std::size_t strideInBytes)
    {
        attributes_.clear();

        const auto stride = static_cast<std::uint32_t>(
            declaration_.getVertexStrideProperty() > 0
                ? static_cast<std::uint32_t>(declaration_.getVertexStrideProperty())
                : static_cast<std::uint32_t>(strideInBytes));

        if (hasDeclaration_ && !declaration_.GetVertexElements().empty())
        {
            for (const VertexElement& element : declaration_.GetVertexElements())
            {
                const char* name = nullptr;
                std::uint32_t location = 0;
                if (!MapVertexUsage(element.getVertexElementUsageProperty(),
                                    element.getUsageIndexProperty(), name, location))
                {
                    // Deliberately skipped rather than assigned some spare location: a usage no
                    // shader in this renderer declares has no correct location, and inventing one
                    // would feed the wrong bytes to whatever input happened to sit there.
                    continue;
                }

                LLGL::VertexAttribute attribute;
                attribute.name = name;
                attribute.format = MapVertexElementFormat(element.getVertexElementFormatProperty());
                attribute.location = location;
                attribute.offset = static_cast<std::uint32_t>(element.getOffsetProperty());
                attribute.stride = stride;
                attributes_.push_back(attribute);
            }
            return;
        }

        // No declaration -- this is the path GraphicsDevice::DrawUserPrimitives()'s typed
        // overloads take (LLGL-32): they call CreateVertexBuffer(int) then SetData() straight
        // from their own GPU-packed struct (GraphicsDevice.cpp's GpuVPC/GpuVPT/GpuVPCT/GpuVPNT),
        // never a VertexDeclaration. Those four packed structs (CNA::Internal::Graphics::
        // Position{Color,Texture,ColorTexture,NormalTexture}Stream) have four DISTINCT byte sizes,
        // so the stride alone identifies which one was used -- the same "infer the layout from the
        // stride" precedent the Vulkan renderer's own MakeExt3DKey() already relies on for these
        // exact same stream sizes. Anything else is left empty, and the draw path refuses it by
        // name instead of guessing.
        const auto addAttribute = [&](const char* name, LLGL::Format format,
                                      std::uint32_t location, std::uint32_t offset) {
            LLGL::VertexAttribute attribute;
            attribute.name = name;
            attribute.format = format;
            attribute.location = location;
            attribute.offset = offset;
            attribute.stride = stride;
            attributes_.push_back(attribute);
        };

        switch (strideInBytes)
        {
            case 16: // PositionColorStream (VertexPositionColor): float3 + ubyte4
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("color", LLGL::Format::RGBA8UNorm, 1, 12);
                break;
            case 20: // PositionTextureStream (VertexPositionTexture): float3 + float2
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 12);
                break;
            case 24: // PositionColorTextureStream (VertexPositionColorTexture): float3 + ubyte4 + float2
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("color", LLGL::Format::RGBA8UNorm, 1, 12);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 16);
                break;
            case 32: // PositionNormalTextureStream (VertexPositionNormalTexture): float3 + float3 + float2
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("normal", LLGL::Format::RGB32Float, 3, 12);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 24);
                break;
            // PbrEffect (LLGL-26 follow-up): VertexPositionNormalTangentTexture's own byte layout
            // (float3 pos + float3 normal + float4 tangent + float2 uv) -- matches
            // modules/renderers/llgl/examples/llgl_pbreffect_handderived_test.cpp's own PbrGpuVertex, mirroring the
            // Vulkan renderer's own pbr3d.vert.glsl attribute layout.
            case 48:
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("normal", LLGL::Format::RGB32Float, 3, 12);
                addAttribute("tangent", LLGL::Format::RGBA32Float, 6, 24);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 40);
                break;
            // SkinnedEffect (LLGL-25): VertexPositionNormalTextureSkinned's own GPU-packed stream --
            // float3 pos + float3 normal + float2 uv + float4 blend weights (plain, NOT normalized)
            // + ubyte4 blend indices (a genuine INTEGER attribute -- RGBA8UInt, not RGBA8UNorm --
            // matching every other renderer's own convention, see AcquirePrimitiveSkinnedVertexShader).
            case 52:
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("normal", LLGL::Format::RGB32Float, 3, 12);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 24);
                addAttribute("aBoneWeights", LLGL::Format::RGBA32Float, 4, 32);
                addAttribute("aBoneIndices", LLGL::Format::RGBA8UInt, 5, 48);
                break;
            // SkinnedEffect.VertexColorEnabled (LLGL-37, CNAEXT extension property): the stride-52
            // layout above with a trailing normalized ubyte4 Color APPENDED at offset 52 -- matches
            // every other renderer's own "append rather than insert" convention for this vertex shape
            // (EasyGL/Vulkan/etc's own local SkinnedColorGpuVertex test structs), keeping locations
            // 0-4/offsets 0-51 byte-identical to the no-colour case above. Colour location 1 matches
            // MapVertexUsage()'s own mapping, same as every other colour-carrying layout here.
            case 56:
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("normal", LLGL::Format::RGB32Float, 3, 12);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 24);
                addAttribute("aBoneWeights", LLGL::Format::RGBA32Float, 4, 32);
                addAttribute("aBoneIndices", LLGL::Format::RGBA8UInt, 5, 48);
                addAttribute("color", LLGL::Format::RGBA8UNorm, 1, 52);
                break;
            // SkinnedPbrEffect (LLGL-25 follow-up): VertexPositionNormalTangentTextureSkinned's own
            // byte layout -- the stride-48 PbrGpuVertex layout above with the stride-52 skinning
            // suffix (BlendWeight, BlendIndices) appended, matching
            // modules/renderers/vulkan/examples/vulkan_pbreffect_handderived_test.cpp's own SkinnedPbrGpuVertex.
            case 68:
                addAttribute("position", LLGL::Format::RGB32Float, 0, 0);
                addAttribute("normal", LLGL::Format::RGB32Float, 3, 12);
                addAttribute("tangent", LLGL::Format::RGBA32Float, 6, 24);
                addAttribute("texCoord", LLGL::Format::RG32Float, 2, 40);
                addAttribute("aBoneWeights", LLGL::Format::RGBA32Float, 4, 48);
                addAttribute("aBoneIndices", LLGL::Format::RGBA8UInt, 5, 64);
                break;
            default:
                break;
        }
    }

    // -----------------------------------------------------------------------------------------
    // LlglIndexBufferRenderer
    // -----------------------------------------------------------------------------------------

    LlglIndexBufferRenderer::LlglIndexBufferRenderer(LLGL::RenderSystem* renderSystem,
                                                    LlglRenderer* owner, int indexCapacity,
                                                    bool thirtyTwoBit)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , indexCapacity_(indexCapacity > 0 ? indexCapacity : 0)
        , thirtyTwoBit_(thirtyTwoBit)
    {
        if (renderSystem_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: null render system for index buffer");
    }

    LlglIndexBufferRenderer::~LlglIndexBufferRenderer()
    {
        if (buffer_ == nullptr)
            return;

        // Same deferral as the vertex buffer above, for the same reason.
        if (owner_ != nullptr)
            owner_->ScheduleBufferReleaseEXT(buffer_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*buffer_);
    }

    void LlglIndexBufferRenderer::Upload(const void* data, int indexCount, std::size_t indexSize)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }

        // LLGL-46: see LlglVertexBufferRenderer::SetData()'s own identical comment -- flushing any
        // pending frame first makes it safe to write in place or reallocate right afterward.
        if (buffer_ != nullptr && owner_ != nullptr)
            owner_->FlushPendingFrameEXT();

        const std::size_t byteSize = static_cast<std::size_t>(indexCount) * indexSize;
        if (buffer_ == nullptr || byteSize > byteCapacity_)
        {
            if (buffer_ != nullptr)
                renderSystem_->Release(*buffer_);

            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.bindFlags = LLGL::BindFlags::IndexBuffer;
            bufferDesc.format = (indexSize == 4 ? LLGL::Format::R32UInt : LLGL::Format::R16UInt);
            buffer_ = renderSystem_->CreateBuffer(bufferDesc, data);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: index buffer creation failed");
            byteCapacity_ = byteSize;
        }
        else
        {
            renderSystem_->WriteBuffer(*buffer_, 0, data, byteSize);
        }

        indexCount_ = indexCount;
    }

    void LlglIndexBufferRenderer::SetData16(const void* data, int indexCount)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error(std::string(kRendererName) + " renderer: SetData16 on a 32-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void LlglIndexBufferRenderer::SetData32(const void* data, int indexCount)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error(std::string(kRendererName) + " renderer: SetData32 on a 16-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    // -----------------------------------------------------------------------------------------
    // LlglSpriteBatchRenderer
    // -----------------------------------------------------------------------------------------

    LlglSpriteBatchRenderer::LlglSpriteBatchRenderer(LlglRenderer& owner)
        : owner_(owner)
        , transform_(Matrix::getIdentityProperty())
    {
    }

    void LlglSpriteBatchRenderer::Begin()
    {
        // Nothing to reset here: SpriteBatch::Begin() always calls SetTransformMatrix() (with
        // either the caller's transform or Matrix::getIdentityProperty() as its own default)
        // BEFORE this runs, so unconditionally overwriting transform_ back to identity here
        // silently discarded any custom transformMatrix passed to SpriteBatch::Begin().
    }

    void LlglSpriteBatchRenderer::End()
    {
        // Nothing to flush: every Draw already appended its geometry to the owning renderer's
        // frame, which is recorded and submitted as a single command buffer at Present().
    }

    void LlglSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void LlglSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        textureFilter_ = textureFilter;
    }

    void LlglSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void LlglSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            effect->Apply();
        else
            owner_.SetCurrentCustomEffectEXT(nullptr);
    }

    void LlglSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y),
                                    texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void LlglSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
             SpriteEffects::None, 0.0f);
    }

    void LlglSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float /*layerDepth*/)
    {
        owner_.QueueSpriteEXT(texture, destinationRectangle, sourceRectangle, color, rotation,
                              origin, effects, transform_, textureFilter_, addressU_, addressV_);
    }

    // -----------------------------------------------------------------------------------------
    // LlglRenderer
    // -----------------------------------------------------------------------------------------

    LlglRenderer::LlglRenderer(const GraphicsRendererCreateArgs& args)
    {
        presentationMode_ = static_cast<int>(args.presentationMode);
        swapInterval_ = args.swapInterval;
        requestedSampleCount_ = args.multiSampleCount > 1 ? args.multiSampleCount : 1;

        const int windowWidth = args.surface.drawableSize.width;
        const int windowHeight = args.surface.drawableSize.height;
        virtualWidth_ = args.virtualWidth > 0 ? args.virtualWidth : windowWidth;
        virtualHeight_ = args.virtualHeight > 0 ? args.virtualHeight : windowHeight;

        module_ = Detail::ResolveRendererModule();

        // LLGL 0.04b's OpenGL swap chain can report the requested sample count while still
        // resolving a hard single-sample edge on real GLX hardware. XNA treats an unsupported
        // back-buffer sample request as zero applied samples, so clamp this module to one native
        // sample rather than advertising multisampling that the pixel path does not deliver.
        if (module_ == Detail::RendererModule::OpenGL)
            requestedSampleCount_ = 1;

        LLGL::RenderSystemDescriptor rendererDesc;
        rendererDesc.moduleName = Detail::GetRendererModuleName(module_);

        // CNAEXT diagnostics: CNA_LLGL_DEBUG=1 turns on LLGL's own debug layer and routes its
        // reports to stdout. Off by default -- the debug layer validates every command and is far
        // too costly to leave on -- but invaluable when a draw silently produces nothing.
        static LLGL::RenderingDebugger debugger;
        const char* debugRequest = std::getenv("CNA_LLGL_DEBUG");
        if (debugRequest != nullptr && debugRequest[0] == '1')
        {
            LLGL::Log::RegisterCallbackStd();
            rendererDesc.flags |= LLGL::RenderSystemFlags::DebugDevice;
            rendererDesc.debugger = &debugger;
        }

        LLGL::Report report;
        renderer_ = LLGL::RenderSystem::Load(rendererDesc, &report);
        if (!renderer_)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: failed to load the " +
                Detail::GetRendererModuleName(module_) + " module (" +
                (report.GetText() != nullptr ? report.GetText() : "no details") + ")");
        }

        surface_ = std::make_shared<LlglPlatformSurface>(args.surface, args.isFullScreen);

        LLGL::SwapChainDescriptor swapChainDesc;
        swapChainDesc.resolution = surface_->GetContentSize();
        swapChainDesc.depthBits = 24;
        swapChainDesc.stencilBits = 8;
        swapChainDesc.samples = static_cast<std::uint32_t>(requestedSampleCount_);
        swapChainDesc.fullscreen = args.isFullScreen;

        swapChain_ = renderer_->CreateSwapChain(swapChainDesc, surface_);
        if (swapChain_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: swap chain creation failed");

        swapChain_->SetVsyncInterval(static_cast<std::uint32_t>(swapInterval_ > 0 ? swapInterval_ : 0));

        queue_ = renderer_->GetCommandQueue();
        commands_ = renderer_->CreateCommandBuffer();
        if (queue_ == nullptr || commands_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: command buffer creation failed");

        CreateSpritePipelineResources();
        CreatePrimitivePipelineResources();

        IGraphicsRenderer::RegisterForWindow(surface_->GetWindowId(), this);
    }

    LlglRenderer::~LlglRenderer()
    {
        if (surface_)
            IGraphicsRenderer::UnregisterForWindow(surface_->GetWindowId());

        if (!renderer_)
            return;

        // Any still-live MRT bind's combined LLGL::RenderTarget must be released too -- ordinarily
        // torn down by a later SetRenderTarget2D()/SetRenderTargets() call, which never happens if
        // the game disposes its GraphicsDevice while one is still bound.
        ReleaseCurrentMrtBindingEXT();

        // Drains pendingBufferReleases_/pendingRenderTargetReleases_/pendingTextureReleases_/
        // pendingQueryHeapReleases_ -- a RenderTarget2D/OcclusionQuery destroyed mid-frame defers
        // its actual release until the frame that may reference it is submitted (see
        // ScheduleRenderTargetReleaseEXT/ScheduleQueryHeapReleaseEXT), and the renderer itself can
        // be destroyed before that submit ever happens (e.g. the game disposes its GraphicsDevice
        // without presenting again). Without this, those resources would simply leak.
        ReleasePendingBuffers();

        for (const auto& entry : pipelineCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        pipelineCache_.clear();

        for (const auto& entry : loadRenderPassCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        loadRenderPassCache_.clear();

        for (const auto& entry : samplerCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        samplerCache_.clear();

        for (const auto& entry : primitivePipelineCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitivePipelineCache_.clear();

        for (const auto& entry : primitiveVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitiveVertexShaderCache_.clear();

        for (const auto& entry : primitiveEnvMapVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitiveEnvMapVertexShaderCache_.clear();

        for (const auto& entry : primitiveSkinnedVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitiveSkinnedVertexShaderCache_.clear();

        for (const auto& entry : primitivePbrVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitivePbrVertexShaderCache_.clear();

        for (const auto& entry : primitivePbrSkinnedVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitivePbrSkinnedVertexShaderCache_.clear();

        for (LLGL::Buffer* buffer : pendingBufferReleases_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        pendingBufferReleases_.clear();

        for (LLGL::Buffer* buffer : transformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        transformBuffers_.clear();

        for (LLGL::Buffer* buffer : customEffectUniformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        customEffectUniformBuffers_.clear();

        for (LLGL::Buffer* buffer : envMapUniformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        envMapUniformBuffers_.clear();

        for (LLGL::Buffer* buffer : skinnedUniformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        skinnedUniformBuffers_.clear();

        for (LLGL::Buffer* buffer : skinnedBoneBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        skinnedBoneBuffers_.clear();

        for (LLGL::Buffer* buffer : pbrUniformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        pbrUniformBuffers_.clear();

        if (defaultWhitePbrTexture_ != nullptr)
            renderer_->Release(*defaultWhitePbrTexture_);
        if (defaultFlatNormalPbrTexture_ != nullptr)
            renderer_->Release(*defaultFlatNormalPbrTexture_);

        if (customEffectLayout_ != nullptr)
            renderer_->Release(*customEffectLayout_);

        if (primitiveFragmentShader_ != nullptr)
            renderer_->Release(*primitiveFragmentShader_);
        if (primitiveTexturedFragmentShader_ != nullptr)
            renderer_->Release(*primitiveTexturedFragmentShader_);
        if (primitiveLitFragmentShader_ != nullptr)
            renderer_->Release(*primitiveLitFragmentShader_);
        if (primitiveLitUntexturedFragmentShader_ != nullptr)
            renderer_->Release(*primitiveLitUntexturedFragmentShader_);
        if (primitiveDualTextureFragmentShader_ != nullptr)
            renderer_->Release(*primitiveDualTextureFragmentShader_);
        if (primitiveEnvMapFragmentShader_ != nullptr)
            renderer_->Release(*primitiveEnvMapFragmentShader_);
        if (primitiveSkinnedFragmentShader_ != nullptr)
            renderer_->Release(*primitiveSkinnedFragmentShader_);
        if (primitiveSkinnedColorFragmentShader_ != nullptr)
            renderer_->Release(*primitiveSkinnedColorFragmentShader_);
        if (primitivePbrFragmentShader_ != nullptr)
            renderer_->Release(*primitivePbrFragmentShader_);
        if (primitiveLayout_ != nullptr)
            renderer_->Release(*primitiveLayout_);
        if (primitiveTexturedLayout_ != nullptr)
            renderer_->Release(*primitiveTexturedLayout_);
        if (primitiveDualTextureLayout_ != nullptr)
            renderer_->Release(*primitiveDualTextureLayout_);
        if (primitiveEnvMapLayout_ != nullptr)
            renderer_->Release(*primitiveEnvMapLayout_);
        if (primitiveSkinnedLayout_ != nullptr)
            renderer_->Release(*primitiveSkinnedLayout_);
        if (primitivePbrLayout_ != nullptr)
            renderer_->Release(*primitivePbrLayout_);
        if (primitivePbrSkinnedLayout_ != nullptr)
            renderer_->Release(*primitivePbrSkinnedLayout_);

        if (spriteVertexBuffer_ != nullptr)
            renderer_->Release(*spriteVertexBuffer_);
        if (spriteProjectionBuffer_ != nullptr)
            renderer_->Release(*spriteProjectionBuffer_);
        if (spriteLayout_ != nullptr)
            renderer_->Release(*spriteLayout_);
        if (spriteVertexShader_ != nullptr)
            renderer_->Release(*spriteVertexShader_);
        if (spriteFragmentShader_ != nullptr)
            renderer_->Release(*spriteFragmentShader_);
        if (commands_ != nullptr)
            renderer_->Release(*commands_);
        if (swapChain_ != nullptr)
            renderer_->Release(*swapChain_);
    }

    const char* LlglRenderer::GetRendererNameEXT() const
    {
        if (!renderer_)
            return Detail::GetRendererModuleName(module_);
        return renderer_->GetRendererInfo().rendererName.c_str();
    }

    void LlglRenderer::CreateSpritePipelineResources()
    {
        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();
        const LLGL::VertexFormat vertexFormat = MakeSpriteVertexFormat();

        LLGL::ShaderDescriptor vertexDesc;
        LLGL::ShaderDescriptor fragmentDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        fragmentDesc.type = LLGL::ShaderType::Fragment;

        // The shading language the loaded module reports decides which of the two checked-in
        // shader flavours is used -- not the module name -- so a module that gains or loses a
        // language cannot end up handed a form it never accepted.
        //
        // GLSL is checked FIRST, and that order is load-bearing rather than cosmetic. A modern
        // OpenGL module reports BOTH languages, because desktop GL can ingest SPIR-V through
        // GL_ARB_gl_spirv -- but the SPIR-V shipped here was compiled for Vulkan's binding model,
        // and GL accepts it far enough to be dangerous: the position attribute still arrives, so
        // geometry rasterizes in the right place while the uniform block and every other attribute
        // silently read as zero. That is what made the OpenGL module clear correctly and draw
        // nothing at all (LLGL-17). Preferring GLSL wherever it exists keeps each module on the
        // form its own binding model was authored against; SPIR-V is the fallback for a module
        // that has no GLSL at all, which is exactly Vulkan.
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = Shaders::kSprite2dVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;

            fragmentDesc.source = Shaders::kSprite2dFragGlsl;
            fragmentDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::SPIRV))
        {
            vertexDesc.source = reinterpret_cast<const char*>(Shaders::kSprite2dVertSpv);
            vertexDesc.sourceSize = sizeof(Shaders::kSprite2dVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";

            fragmentDesc.source = reinterpret_cast<const char*>(Shaders::kSprite2dFragSpv);
            fragmentDesc.sourceSize = sizeof(Shaders::kSprite2dFragSpv);
            fragmentDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            fragmentDesc.entryPoint = "main";
        }
        else
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the " + Detail::GetRendererModuleName(module_) +
                " module accepts neither SPIR-V nor GLSL, and this renderer ships no other shader form");
        }

        vertexDesc.vertex.inputAttribs = vertexFormat.attributes;

        spriteVertexShader_ = renderer_->CreateShader(vertexDesc);
        spriteFragmentShader_ = renderer_->CreateShader(fragmentDesc);

        for (LLGL::Shader* shader : {spriteVertexShader_, spriteFragmentShader_})
        {
            if (shader == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: sprite shader creation failed");
            if (const LLGL::Report* shaderReport = shader->GetReport())
            {
                if (shaderReport->HasErrors())
                {
                    throw std::runtime_error(
                        std::string(kRendererName) + " renderer: sprite shader compilation failed: " +
                        (shaderReport->GetText() != nullptr ? shaderReport->GetText() : "no details"));
                }
            }
        }

        LLGL::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Scene", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer, LLGL::StageFlags::VertexStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
        };
        // Desktop GLSL has no separate texture and sampler objects, so the OpenGL flavour of the
        // fragment shader declares a single combined sampler2D; this entry maps it back onto the
        // texture/sampler pair the Vulkan flavour keeps apart.
        layoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2}
        };
        spriteLayout_ = renderer_->CreatePipelineLayout(layoutDesc);
        if (spriteLayout_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: sprite pipeline layout creation failed");

        LLGL::BufferDescriptor projectionDesc;
        projectionDesc.size = sizeof(float) * 16;
        projectionDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
        spriteProjectionBuffer_ = renderer_->CreateBuffer(projectionDesc);
        if (spriteProjectionBuffer_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: sprite constant buffer creation failed");
    }

    bool LlglRenderer::SupportsWireFrameEXT() const
    {
        // Module-dependent, and LLGL exposes no capability flag for it, so this is the empirical
        // answer: verified drawing real wireframe edges on the OpenGL module, verified drawing
        // nothing at all on the Vulkan one.
        return module_ == Detail::RendererModule::OpenGL;
    }

    bool LlglRenderer::IsOpaqueBlendState() const
    {
        // XNA has no separate "blending on" switch: BlendState.Opaque is expressed as One/Zero
        // with Add, arithmetically identical to blending being off. Deriving the enable bit from
        // the factors themselves keeps both representations in agreement without depending on
        // whether SetBlendEnabled() happened to be called first.
        return static_cast<XnaBlend>(colorSrcBlend_) == XnaBlend::One &&
               static_cast<XnaBlend>(colorDstBlend_) == XnaBlend::Zero &&
               static_cast<XnaBlend>(alphaSrcBlend_) == XnaBlend::One &&
               static_cast<XnaBlend>(alphaDstBlend_) == XnaBlend::Zero &&
               static_cast<XnaBlendFunction>(colorBlendFunc_) == XnaBlendFunction::Add &&
               static_cast<XnaBlendFunction>(alphaBlendFunc_) == XnaBlendFunction::Add;
    }

    bool LlglRenderer::UsesConstantBlendFactorState() const
    {
        return UsesConstantBlendFactor(colorSrcBlend_) || UsesConstantBlendFactor(colorDstBlend_) ||
               UsesConstantBlendFactor(alphaSrcBlend_) || UsesConstantBlendFactor(alphaDstBlend_);
    }

    void LlglRenderer::CreatePrimitivePipelineResources()
    {
        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();
        const bool useGlsl = SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL);

        const auto createFragmentShader = [&](const char* glslSource, const std::uint32_t* spirv,
                                              std::size_t spirvSize, const char* what) {
            LLGL::ShaderDescriptor fragmentDesc;
            fragmentDesc.type = LLGL::ShaderType::Fragment;
            if (useGlsl)
            {
                fragmentDesc.source = glslSource;
                fragmentDesc.sourceType = LLGL::ShaderSourceType::CodeString;
            }
            else
            {
                fragmentDesc.source = reinterpret_cast<const char*>(spirv);
                fragmentDesc.sourceSize = spirvSize;
                fragmentDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
                fragmentDesc.entryPoint = "main";
            }

            LLGL::Shader* shader = renderer_->CreateShader(fragmentDesc);
            if (shader == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: " + what + " creation failed");
            if (const LLGL::Report* report = shader->GetReport())
            {
                if (report->HasErrors())
                {
                    throw std::runtime_error(
                        std::string(kRendererName) + " renderer: " + what + " compilation failed: " +
                        (report->GetText() != nullptr ? report->GetText() : "no details"));
                }
            }
            return shader;
        };

        primitiveFragmentShader_ = createFragmentShader(
            Shaders::kUntextured3dFragGlsl, Shaders::kUntextured3dFragSpv,
            sizeof(Shaders::kUntextured3dFragSpv), "untextured 3D fragment shader");
        primitiveTexturedFragmentShader_ = createFragmentShader(
            Shaders::kTextured3dFragGlsl, Shaders::kTextured3dFragSpv,
            sizeof(Shaders::kTextured3dFragSpv), "textured 3D fragment shader");
        primitiveLitFragmentShader_ = createFragmentShader(
            Shaders::kLitTextured3dFragGlsl, Shaders::kLitTextured3dFragSpv,
            sizeof(Shaders::kLitTextured3dFragSpv), "lit textured 3D fragment shader");
        primitiveLitUntexturedFragmentShader_ = createFragmentShader(
            Shaders::kLitUntextured3dFragGlsl, Shaders::kLitUntextured3dFragSpv,
            sizeof(Shaders::kLitUntextured3dFragSpv), "lit untextured 3D fragment shader");
        primitiveDualTextureFragmentShader_ = createFragmentShader(
            Shaders::kDualTextured3dFragGlsl, Shaders::kDualTextured3dFragSpv,
            sizeof(Shaders::kDualTextured3dFragSpv), "dual-textured 3D fragment shader");
        primitiveEnvMapFragmentShader_ = createFragmentShader(
            Shaders::kEnvMap3dFragGlsl, Shaders::kEnvMap3dFragSpv,
            sizeof(Shaders::kEnvMap3dFragSpv), "environment map 3D fragment shader");
        primitiveSkinnedFragmentShader_ = createFragmentShader(
            Shaders::kSkinned3dFragGlsl, Shaders::kSkinned3dFragSpv,
            sizeof(Shaders::kSkinned3dFragSpv), "skinned 3D fragment shader");
        primitiveSkinnedColorFragmentShader_ = createFragmentShader(
            Shaders::kSkinned3dColorFragGlsl, Shaders::kSkinned3dColorFragSpv,
            sizeof(Shaders::kSkinned3dColorFragSpv), "skinned 3D vertex-colour fragment shader");
        primitivePbrFragmentShader_ = createFragmentShader(
            Shaders::kPbr3dFragGlsl, Shaders::kPbr3dFragSpv,
            sizeof(Shaders::kPbr3dFragSpv), "PBR 3D fragment shader");

        // Two layouts rather than one with an optionally-unused texture slot: a pipeline whose
        // layout declares a texture the draw never binds is a validation error on Vulkan, not a
        // harmless spare binding.
        LLGL::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Transform", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
        };
        primitiveLayout_ = renderer_->CreatePipelineLayout(layoutDesc);

        LLGL::PipelineLayoutDescriptor texturedLayoutDesc;
        texturedLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Transform", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
        };
        texturedLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2}
        };
        primitiveTexturedLayout_ = renderer_->CreatePipelineLayout(texturedLayoutDesc);

        // DualTextureEffect: a second, independently bound texture+sampler pair alongside the
        // first. Reuses the plain textured vertex shader (identical vertex-side behaviour), so
        // only the fragment shader and this layout differ from primitiveTexturedLayout_.
        LLGL::PipelineLayoutDescriptor dualTextureLayoutDesc;
        dualTextureLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Transform", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
            LLGL::BindingDescriptor{"colorMap2", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 4},
            LLGL::BindingDescriptor{"samplerState2", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 5},
        };
        dualTextureLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2},
            LLGL::CombinedTextureSamplerDescriptor{"colorMap2", "colorMap2", "samplerState2", 4},
        };
        primitiveDualTextureLayout_ = renderer_->CreatePipelineLayout(dualTextureLayoutDesc);

        // EnvironmentMapEffect: its own dedicated uniform block (EnvMapParams, see
        // env_map3d.vert.glsl) plus the diffuse texture/sampler pair AND a cube map texture/
        // sampler pair. Binding indices follow the same "resource index = position in this list"
        // convention ReplayFrameCommandsList()'s SetResource() calls rely on.
        LLGL::PipelineLayoutDescriptor envMapLayoutDesc;
        envMapLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"EnvMapParams", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
            LLGL::BindingDescriptor{"envMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 4},
            LLGL::BindingDescriptor{"envMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 5},
        };
        envMapLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2},
            LLGL::CombinedTextureSamplerDescriptor{"envMap", "envMap", "envMapSampler", 4},
        };
        primitiveEnvMapLayout_ = renderer_->CreatePipelineLayout(envMapLayoutDesc);

        // SkinnedEffect: its own dedicated parameter uniform block (SkinnedParams) PLUS a second,
        // much larger bone transform block (BoneBlock, 72 mat4s) at its own binding, plus the
        // diffuse texture/sampler pair (SkinnedEffect is always textured, unlike BasicEffect).
        LLGL::PipelineLayoutDescriptor skinnedLayoutDesc;
        skinnedLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"SkinnedParams", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"BoneBlock", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer, LLGL::StageFlags::VertexStage, 2},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 3},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 4},
        };
        skinnedLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 3},
        };
        primitiveSkinnedLayout_ = renderer_->CreatePipelineLayout(skinnedLayoutDesc);

        // PbrEffect: its own dedicated uniform block (PbrParams, see pbr3d.vert.glsl) plus 7
        // texture/sampler pairs -- base colour, normal map, metallic-roughness map, emissive map,
        // and occlusion map. Every slot is always bound (a null game-side map resolves to a 1x1
        // default texture in QueuePrimitives/EnsureDefaultPbrTexturesEXT rather than being left
        // unbound), so unlike EnvironmentMapEffect there is no "throw if missing" branch for the
        // 6 optional maps.
        LLGL::PipelineLayoutDescriptor pbrLayoutDesc;
        pbrLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"PbrParams", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
            LLGL::BindingDescriptor{"normalMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 4},
            LLGL::BindingDescriptor{"normalMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 5},
            LLGL::BindingDescriptor{"metallicRoughnessMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 6},
            LLGL::BindingDescriptor{"metallicRoughnessMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 7},
            LLGL::BindingDescriptor{"emissiveMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 8},
            LLGL::BindingDescriptor{"emissiveMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 9},
            LLGL::BindingDescriptor{"occlusionMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 10},
            LLGL::BindingDescriptor{"occlusionMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 11},
            LLGL::BindingDescriptor{"specularMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 12},
            LLGL::BindingDescriptor{"specularMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 13},
            LLGL::BindingDescriptor{"specularColorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 14},
            LLGL::BindingDescriptor{"specularColorMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 15},
        };
        pbrLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2},
            LLGL::CombinedTextureSamplerDescriptor{"normalMap", "normalMap", "normalMapSampler", 4},
            LLGL::CombinedTextureSamplerDescriptor{"metallicRoughnessMap", "metallicRoughnessMap",
                                                   "metallicRoughnessMapSampler", 6},
            LLGL::CombinedTextureSamplerDescriptor{"emissiveMap", "emissiveMap", "emissiveMapSampler", 8},
            LLGL::CombinedTextureSamplerDescriptor{"occlusionMap", "occlusionMap", "occlusionMapSampler", 10},
            LLGL::CombinedTextureSamplerDescriptor{"specularMap", "specularMap", "specularMapSampler", 12},
            LLGL::CombinedTextureSamplerDescriptor{"specularColorMap", "specularColorMap",
                                                   "specularColorMapSampler", 14},
        };
        primitivePbrLayout_ = renderer_->CreatePipelineLayout(pbrLayoutDesc);

        // SkinnedPbrEffect: the EXACT SAME PbrParams block and 7 texture/sampler pairs as
        // primitivePbrLayout_ above (bindings 1-15, unchanged), plus a SEPARATE BoneBlock (72
        // mat4s) at binding 16 -- deliberately AFTER every texture binding rather than shifting
        // them, so primitivePbrFragmentShader_ (compiled against those exact binding numbers) is
        // reusable verbatim; only the vertex shader differs.
        LLGL::PipelineLayoutDescriptor pbrSkinnedLayoutDesc;
        pbrSkinnedLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"PbrParams", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
            LLGL::BindingDescriptor{"normalMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 4},
            LLGL::BindingDescriptor{"normalMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 5},
            LLGL::BindingDescriptor{"metallicRoughnessMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 6},
            LLGL::BindingDescriptor{"metallicRoughnessMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 7},
            LLGL::BindingDescriptor{"emissiveMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 8},
            LLGL::BindingDescriptor{"emissiveMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 9},
            LLGL::BindingDescriptor{"occlusionMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 10},
            LLGL::BindingDescriptor{"occlusionMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 11},
            LLGL::BindingDescriptor{"specularMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 12},
            LLGL::BindingDescriptor{"specularMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 13},
            LLGL::BindingDescriptor{"specularColorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 14},
            LLGL::BindingDescriptor{"specularColorMapSampler", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 15},
            LLGL::BindingDescriptor{"BoneBlock", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer, LLGL::StageFlags::VertexStage, 16},
        };
        pbrSkinnedLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2},
            LLGL::CombinedTextureSamplerDescriptor{"normalMap", "normalMap", "normalMapSampler", 4},
            LLGL::CombinedTextureSamplerDescriptor{"metallicRoughnessMap", "metallicRoughnessMap",
                                                   "metallicRoughnessMapSampler", 6},
            LLGL::CombinedTextureSamplerDescriptor{"emissiveMap", "emissiveMap", "emissiveMapSampler", 8},
            LLGL::CombinedTextureSamplerDescriptor{"occlusionMap", "occlusionMap", "occlusionMapSampler", 10},
            LLGL::CombinedTextureSamplerDescriptor{"specularMap", "specularMap", "specularMapSampler", 12},
            LLGL::CombinedTextureSamplerDescriptor{"specularColorMap", "specularColorMap",
                                                   "specularColorMapSampler", 14},
        };
        primitivePbrSkinnedLayout_ = renderer_->CreatePipelineLayout(pbrSkinnedLayoutDesc);

        if (primitiveLayout_ == nullptr || primitiveTexturedLayout_ == nullptr ||
            primitiveDualTextureLayout_ == nullptr || primitiveEnvMapLayout_ == nullptr ||
            primitiveSkinnedLayout_ == nullptr || primitivePbrLayout_ == nullptr ||
            primitivePbrSkinnedLayout_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: 3D pipeline layout creation failed");
    }

    LLGL::Shader* LlglRenderer::AcquirePrimitiveVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes, bool textured, bool lit)
    {
        const std::uint64_t key =
            (MakeVertexLayoutKey(attributes) * 2u + (textured ? 1u : 0u)) * 2u + (lit ? 1u : 0u);
        const auto cached = primitiveVertexShaderCache_.find(key);
        if (cached != primitiveVertexShaderCache_.end())
            return cached->second;

        // Which attributes the layout actually carries decides the shader variant: a shader that
        // declares an input the buffer does not supply reads undefined data on Vulkan, so the
        // variant is chosen from the layout rather than from what the effect asked for.
        bool hasColor = false;
        bool hasTexCoord = false;
        bool hasNormal = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) hasColor = true;
            if (attribute.location == 2) hasTexCoord = true;
            if (attribute.location == 3) hasNormal = true;
        }
        if (textured && !hasTexCoord)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: a textured effect needs a vertex layout with "
                "texture coordinates, and this one has none");
        }
        // LLGL-52: lit+textured+no-colour with no normal attribute (e.g. plain VertexPositionTexture)
        // has a defined shader variant below (a fixed (0,0,1) object-space normal) instead of
        // refusing the draw; every OTHER lit-without-normal combination still throws.
        const bool isLitTexturedFlatNormal = lit && textured && !hasColor && !hasNormal;
        if (lit && !hasNormal && !isLitTexturedFlatNormal)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: lighting needs a vertex layout with normals, "
                "and this one has none");
        }

        const char* glslSource = nullptr;
        const std::uint32_t* spirv = nullptr;
        std::size_t spirvSize = 0;
        if (lit && textured)
        {
            if (isLitTexturedFlatNormal)
            {
                glslSource = Shaders::kLitTextured3dFlatNormalVertGlsl;
                spirv = Shaders::kLitTextured3dFlatNormalVertSpv;
                spirvSize = sizeof(Shaders::kLitTextured3dFlatNormalVertSpv);
            }
            else if (hasColor)
            {
                glslSource = Shaders::kLitColoredTextured3dVertGlsl;
                spirv = Shaders::kLitColoredTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kLitColoredTextured3dVertSpv);
            }
            else
            {
                glslSource = Shaders::kLitTextured3dVertGlsl;
                spirv = Shaders::kLitTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kLitTextured3dVertSpv);
            }
        }
        else if (lit && hasColor)
        {
            // LLGL-31: lit, untextured. Requires vertex colours for the same reason the unlit
            // untextured path below does -- there is no fabricated-white-texture fallback here
            // either, just a shader variant with no colorMap binding at all.
            glslSource = Shaders::kLitColored3dVertGlsl;
            spirv = Shaders::kLitColored3dVertSpv;
            spirvSize = sizeof(Shaders::kLitColored3dVertSpv);
        }
        else if (lit)
        {
            // LLGL-52: lit, untextured, with no vertex-colour attribute either (e.g.
            // VertexPositionNormalTexture with TextureEnabled=false, VertexColorEnabled=false,
            // EnableDefaultLighting()) previously fell all the way through to the UNLIT flat3d
            // branch below, whose Transform block/outputs do not match the LIT fragment shader
            // (primitiveLitUntexturedFragmentShader_) that the separate fragment-shader-selection
            // logic always pairs with lit && !textured. Pairs with lit_untextured3d.frag.glsl
            // unchanged -- identical inputs to kLitColored3dVertGlsl above, minus the colour
            // attribute/multiply.
            glslSource = Shaders::kLitFlat3dVertGlsl;
            spirv = Shaders::kLitFlat3dVertSpv;
            spirvSize = sizeof(Shaders::kLitFlat3dVertSpv);
        }
        else if (textured)
        {
            if (hasColor)
            {
                glslSource = Shaders::kColoredTextured3dVertGlsl;
                spirv = Shaders::kColoredTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kColoredTextured3dVertSpv);
            }
            else
            {
                glslSource = Shaders::kTextured3dVertGlsl;
                spirv = Shaders::kTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kTextured3dVertSpv);
            }
        }
        else if (hasColor)
        {
            glslSource = Shaders::kColored3dVertGlsl;
            spirv = Shaders::kColored3dVertSpv;
            spirvSize = sizeof(Shaders::kColored3dVertSpv);
        }
        else
        {
            // LLGL-52: untextured, unlit, no vertex-colour attribute either (e.g. a flat,
            // constant-DiffuseColor-only ModelMeshPart drawn from VertexPositionNormalTexture) --
            // a defined shader variant with no colour attribute declared at all, instead of
            // refusing the draw. Pairs with untextured3d.frag.glsl unchanged: that fragment stage
            // only ever reads vColor/vTexCoord/vFogFactor, and does not care which vertex shader
            // produced them.
            glslSource = Shaders::kFlat3dVertGlsl;
            spirv = Shaders::kFlat3dVertSpv;
            spirvSize = sizeof(Shaders::kFlat3dVertSpv);
        }

        // The attribute list handed to the shader is trimmed to what that variant declares: a
        // draw from a layout that happens to carry attributes the selected shader never reads
        // (texture coordinates on an untextured draw, a normal on an unlit one) must not leave the
        // pipeline expecting an input its shader never declares.
        std::vector<LLGL::VertexAttribute> shaderAttributes;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 2 && !textured)
                continue;
            if (attribute.location == 3 && !lit)
                continue;
            shaderAttributes.push_back(attribute);
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = glslSource;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = reinterpret_cast<const char*>(spirv);
            vertexDesc.sourceSize = spirvSize;
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = shaderAttributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: 3D vertex shader creation failed");
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: 3D vertex shader compilation failed: " +
                    (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitiveVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::Shader* LlglRenderer::AcquirePrimitiveEnvMapVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes)
    {
        const std::uint64_t key = MakeVertexLayoutKey(attributes);
        const auto cached = primitiveEnvMapVertexShaderCache_.find(key);
        if (cached != primitiveEnvMapVertexShaderCache_.end())
            return cached->second;

        // EnvironmentMapEffect is always textured and lit (GpuDrawParams::textureEnabled/
        // lightingEnabled are unconditionally true in EnvironmentMapEffect::FillGpuDrawParams),
        // so unlike AcquirePrimitiveVertexShader() there is only one shader variant -- just the
        // vertex layout (and hence caching) varies.
        bool hasTexCoord = false;
        bool hasNormal = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 2) hasTexCoord = true;
            if (attribute.location == 3) hasNormal = true;
        }
        if (!hasTexCoord || !hasNormal)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: EnvironmentMapEffect needs a vertex layout "
                "with both texture coordinates and normals, and this one is missing at least one");
        }

        std::vector<LLGL::VertexAttribute> shaderAttributes;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) // vertex colour: this shader never reads one
                continue;
            shaderAttributes.push_back(attribute);
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = Shaders::kEnvMap3dVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = reinterpret_cast<const char*>(Shaders::kEnvMap3dVertSpv);
            vertexDesc.sourceSize = sizeof(Shaders::kEnvMap3dVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = shaderAttributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: EnvironmentMapEffect vertex shader creation failed");
        }
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: EnvironmentMapEffect vertex shader "
                    "compilation failed: " + (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitiveEnvMapVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::Shader* LlglRenderer::AcquirePrimitiveSkinnedVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes)
    {
        const std::uint64_t key = MakeVertexLayoutKey(attributes);
        const auto cached = primitiveSkinnedVertexShaderCache_.find(key);
        if (cached != primitiveSkinnedVertexShaderCache_.end())
            return cached->second;

        // SkinnedEffect is always textured and lit (GpuDrawParams::textureEnabled/lightingEnabled
        // are unconditionally true in SkinnedEffect::FillGpuDrawParams); the only shape that
        // actually varies is whether the vertex layout carries a colour attribute (LLGL-37,
        // SkinnedEffect.VertexColorEnabled -- a CNAEXT extension property), which selects a whole
        // separate compiled shader pair rather than an always-declared, conditionally-read
        // attribute -- matching AcquirePrimitiveVertexShader()'s own colored/textured shader-file
        // split, since a shader declaring an input the bound buffer does not supply reads undefined
        // data on Vulkan.
        bool hasColor = false;
        bool hasTexCoord = false;
        bool hasNormal = false;
        bool hasBoneWeights = false;
        bool hasBoneIndices = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) hasColor = true;
            if (attribute.location == 2) hasTexCoord = true;
            if (attribute.location == 3) hasNormal = true;
            if (attribute.location == 4) hasBoneWeights = true;
            if (attribute.location == 5) hasBoneIndices = true;
        }
        if (!hasTexCoord || !hasNormal || !hasBoneWeights || !hasBoneIndices)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SkinnedEffect needs a vertex layout with "
                "texture coordinates, normals, and bone weights/indices, and this one is missing "
                "at least one");
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = hasColor ? Shaders::kSkinned3dColorVertGlsl : Shaders::kSkinned3dVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = hasColor
                ? reinterpret_cast<const char*>(Shaders::kSkinned3dColorVertSpv)
                : reinterpret_cast<const char*>(Shaders::kSkinned3dVertSpv);
            vertexDesc.sourceSize = hasColor ? sizeof(Shaders::kSkinned3dColorVertSpv)
                                             : sizeof(Shaders::kSkinned3dVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = attributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SkinnedEffect vertex shader creation failed");
        }
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: SkinnedEffect vertex shader "
                    "compilation failed: " + (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitiveSkinnedVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::Shader* LlglRenderer::AcquirePrimitivePbrVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes)
    {
        const std::uint64_t key = MakeVertexLayoutKey(attributes);
        const auto cached = primitivePbrVertexShaderCache_.find(key);
        if (cached != primitivePbrVertexShaderCache_.end())
            return cached->second;

        // PbrEffect is always textured and lit (GpuDrawParams::textureEnabled/lightingEnabled are
        // unconditionally true in PbrEffect::FillGpuDrawParams), so like
        // AcquirePrimitiveEnvMapVertexShader() there is only one shader variant -- just the vertex
        // layout (and hence caching) varies.
        bool hasTexCoord = false;
        bool hasTexCoord1 = false;
        bool hasNormal = false;
        bool hasTangent = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 2) hasTexCoord = true;
            if (attribute.location == 7) hasTexCoord1 = true;
            if (attribute.location == 3) hasNormal = true;
            if (attribute.location == 6) hasTangent = true;
        }
        if (!hasTexCoord || !hasNormal || !hasTangent)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: PbrEffect needs a vertex layout with "
                "texture coordinates, normals, and a tangent, and this one is missing at least one");
        }

        std::vector<LLGL::VertexAttribute> shaderAttributes;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) // vertex colour: this shader never reads one
                continue;
            shaderAttributes.push_back(attribute);
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = hasTexCoord1 ? Shaders::kPbr3dDualUvVertGlsl
                                             : Shaders::kPbr3dVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = reinterpret_cast<const char*>(
                hasTexCoord1 ? Shaders::kPbr3dDualUvVertSpv : Shaders::kPbr3dVertSpv);
            vertexDesc.sourceSize = hasTexCoord1 ? sizeof(Shaders::kPbr3dDualUvVertSpv)
                                                 : sizeof(Shaders::kPbr3dVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = shaderAttributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: PbrEffect vertex shader creation failed");
        }
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: PbrEffect vertex shader "
                    "compilation failed: " + (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitivePbrVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::Shader* LlglRenderer::AcquirePrimitivePbrSkinnedVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes)
    {
        const std::uint64_t key = MakeVertexLayoutKey(attributes);
        const auto cached = primitivePbrSkinnedVertexShaderCache_.find(key);
        if (cached != primitivePbrSkinnedVertexShaderCache_.end())
            return cached->second;

        bool hasTexCoord = false;
        bool hasTexCoord1 = false;
        bool hasNormal = false;
        bool hasTangent = false;
        bool hasBoneWeights = false;
        bool hasBoneIndices = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 2) hasTexCoord = true;
            if (attribute.location == 7) hasTexCoord1 = true;
            if (attribute.location == 3) hasNormal = true;
            if (attribute.location == 4) hasBoneWeights = true;
            if (attribute.location == 5) hasBoneIndices = true;
            if (attribute.location == 6) hasTangent = true;
        }
        if (!hasTexCoord || !hasNormal || !hasTangent || !hasBoneWeights || !hasBoneIndices)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SkinnedPbrEffect needs a vertex layout with "
                "texture coordinates, normals, a tangent, and bone weights/indices, and this one is "
                "missing at least one");
        }

        std::vector<LLGL::VertexAttribute> shaderAttributes;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) // vertex colour: this shader never reads one
                continue;
            shaderAttributes.push_back(attribute);
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = hasTexCoord1 ? Shaders::kPbr3dSkinnedDualUvVertGlsl
                                             : Shaders::kPbr3dSkinnedVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = reinterpret_cast<const char*>(
                hasTexCoord1 ? Shaders::kPbr3dSkinnedDualUvVertSpv
                             : Shaders::kPbr3dSkinnedVertSpv);
            vertexDesc.sourceSize = hasTexCoord1 ? sizeof(Shaders::kPbr3dSkinnedDualUvVertSpv)
                                                 : sizeof(Shaders::kPbr3dSkinnedVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = shaderAttributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SkinnedPbrEffect vertex shader creation failed");
        }
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: SkinnedPbrEffect vertex shader "
                    "compilation failed: " + (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitivePbrSkinnedVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::PipelineState* LlglRenderer::AcquirePrimitivePipeline(
        const LlglVertexBufferRenderer& vertexBuffer, PrimitiveType primitive, bool scissorEnabled,
        bool textured, bool lit, bool dualTexture, bool envMapping, bool skinned, bool pbr)
    {
        const std::vector<LLGL::VertexAttribute>& attributes = vertexBuffer.GetVertexAttributes();

        std::uint64_t key = MakeVertexLayoutKey(attributes);
        key = key * 8u + static_cast<std::uint64_t>(static_cast<int>(primitive) & 0x7);
        key = key * 2u + (depthTestEnabled_ ? 1u : 0u);
        key = key * 2u + (depthWriteEnabled_ ? 1u : 0u);
        key = key * 8u + static_cast<std::uint64_t>(depthCompareFunction_ & 0x7);
        key = key * 4u + static_cast<std::uint64_t>(cullMode_ & 0x3);
        key = key * 2u + static_cast<std::uint64_t>(fillMode_ & 0x1);
        key = key * 2u + (scissorEnabled ? 1u : 0u);
        key = key * 2u + (textured ? 1u : 0u);
        key = key * 2u + (lit ? 1u : 0u);
        key = key * 2u + (dualTexture ? 1u : 0u);
        key = key * 2u + (envMapping ? 1u : 0u);
        key = key * 2u + (skinned ? 1u : 0u);
        key = key * 2u + (pbr ? 1u : 0u);
        // Keep the historical low-16-bit positional fold so the original key remains stable, but
        // also retain the complete blend key in its own tuple element below. The old key alone
        // collided whenever two 3D draws differed only in blend factors/functions or
        // ColorWriteChannels; giving the complete value an independent 64-bit budget fixes that
        // collision without making it compete with the already-dense layout/state fold.
        const std::uint64_t blendKey =
            MakeBlendPipelineKey(scissorEnabled, 1, GetPrimarySampleCountEXT());
        key = key * (1024u * 64u) +
              (blendKey & 0xFFFFu);

        // LLGL-53: depth bias and the full front/back stencil state are now part of the pipeline
        // (see pipelineDesc.rasterizer.depthBias/pipelineDesc.stencil below), so two draws that
        // differ ONLY in one of these fields must not share a cached pipeline object. Folding
        // these ~10 fields into the SAME `key` the rest of this function already builds (via more
        // multiply-add steps, even using SMALL per-step multipliers matching every other field's
        // own established style) was tried and MEASURED BROKEN: once the CUMULATIVE multiplier of
        // everything folded in after a given field exceeds 2^64, that field's bits are silently
        // overflowed away entirely. `rasterizerstate_depthbias_test.cpp`'s own A1 check
        // (`DepthBias=3000000` vs `DepthBias=0`, otherwise identical state) computed the IDENTICAL
        // `key` for both -- the biased draw silently reused the zero-bias pipeline, so its bias
        // never took effect. (An XOR + FNV-prime mix was tried before that and failed differently:
        // matching this function's own MakeBlendPipelineKey() sibling's already-documented "tried
        // and produced WRONG RENDERED PIXELS despite every logged descriptor field being provably
        // correct" class of unexplained defect, LLGL-33 -- confirmed here as a second, independent
        // data point, root cause still not understood for either technique.)
        //
        // The fix: `primitivePipelineCache_`'s key uses a tuple (see its own declaration), giving
        // blend state, depth bias and stencil state their OWN dedicated 64-bit budgets instead of
        // competing for bits with `key` or each other.
        std::uint32_t depthBiasBits = 0;
        std::uint32_t slopeScaleDepthBiasBits = 0;
        std::memcpy(&depthBiasBits, &depthBias_, sizeof(float));
        std::memcpy(&slopeScaleDepthBiasBits, &slopeScaleDepthBias_, sizeof(float));
        const std::uint64_t depthBiasKey =
            (static_cast<std::uint64_t>(depthBiasBits) << 32) | slopeScaleDepthBiasBits;
        const std::uint64_t stencilMaskKey =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(stencilMask_)) << 32) |
            static_cast<std::uint32_t>(stencilWriteMask_);
        // `referenceStencil_` is NOT folded in here: the pipeline uses `stencil.referenceDynamic =
        // true` and the reference value is set per-draw via `CommandBuffer::SetStencilReference()`
        // at replay time instead of being baked into the pipeline object -- see the FrameCommand
        // capture below.
        std::uint64_t stencilOpsKey = (stencilRequested_ ? 1u : 0u);
        stencilOpsKey = stencilOpsKey * 2u + (twoSidedStencilMode_ ? 1u : 0u);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(stencilFunction_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(stencilPass_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(stencilFail_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(stencilDepthFail_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(ccwStencilFunction_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(ccwStencilPass_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(ccwStencilFail_ & 0x7);
        stencilOpsKey = stencilOpsKey * 8u + static_cast<std::uint64_t>(ccwStencilDepthFail_ & 0x7);

        const auto pipelineKey =
            std::make_tuple(key, blendKey, depthBiasKey, stencilMaskKey, stencilOpsKey);
        const auto cached = primitivePipelineCache_.find(pipelineKey);
        if (cached != primitivePipelineCache_.end())
            return cached->second;

        // SkinnedPbrEffect (pbr && skinned together) takes priority over both plain PbrEffect and
        // plain SkinnedEffect -- it needs its own vertex shader/layout combining both, even though
        // its fragment shader is PbrEffect's own unchanged (see AcquirePrimitivePbrSkinnedVertexShader
        // and primitivePbrSkinnedLayout_'s own doc comments for why the fragment stage is shared).
        const bool pbrSkinned = pbr && skinned;

        LLGL::GraphicsPipelineDescriptor pipelineDesc;
        pipelineDesc.debugName = pbrSkinned ? "CNA.PbrSkinned3D"
                                : pbr ? "CNA.Pbr3D"
                                : skinned ? "CNA.Skinned3D"
                                : envMapping ? "CNA.EnvMap3D"
                                : dualTexture ? "CNA.DualTexture3D"
                                : lit ? "CNA.Lit3D" : (textured ? "CNA.Textured3D" : "CNA.Colored3D");
        // DualTextureEffect is never lit (GpuDrawParams::lightingEnabled is always false for it),
        // and its vertex-side behaviour is identical to a plain textured draw -- so the vertex
        // shader is the SAME one AcquirePrimitiveVertexShader() already selects for `textured`,
        // and only the fragment shader and pipeline layout differ below. EnvironmentMapEffect,
        // SkinnedEffect and PbrEffect, unlike DualTextureEffect, each need their OWN vertex shader
        // (world-space normal/eye vector; bone weight/index skinning; tangent-space TBN basis), so
        // none of them goes through AcquirePrimitiveVertexShader() at all.
        pipelineDesc.vertexShader = pbrSkinned ? AcquirePrimitivePbrSkinnedVertexShader(attributes)
                                   : pbr ? AcquirePrimitivePbrVertexShader(attributes)
                                   : skinned ? AcquirePrimitiveSkinnedVertexShader(attributes)
                                   : envMapping ? AcquirePrimitiveEnvMapVertexShader(attributes)
                                   : AcquirePrimitiveVertexShader(attributes, textured, lit);
        // SkinnedEffect.VertexColorEnabled (LLGL-37): a colour-carrying skinned vertex layout
        // (stride 56) needs the vertex-colour-reading fragment shader variant instead of the plain
        // one -- see AcquirePrimitiveSkinnedVertexShader()'s own doc comment for why this is a
        // separate compiled shader rather than an always-declared attribute.
        bool skinnedHasColor = false;
        if (skinned)
        {
            for (const LLGL::VertexAttribute& attribute : attributes)
            {
                if (attribute.location == 1) { skinnedHasColor = true; break; }
            }
        }
        // pbrSkinned reuses primitivePbrFragmentShader_ verbatim -- see its own doc comment.
        pipelineDesc.fragmentShader = pbr ? primitivePbrFragmentShader_
                                    : skinned && skinnedHasColor ? primitiveSkinnedColorFragmentShader_
                                    : skinned ? primitiveSkinnedFragmentShader_
                                    : envMapping ? primitiveEnvMapFragmentShader_
                                    : dualTexture ? primitiveDualTextureFragmentShader_
                                    : lit && textured ? primitiveLitFragmentShader_
                                    : lit ? primitiveLitUntexturedFragmentShader_
                                    : textured ? primitiveTexturedFragmentShader_
                                    : primitiveFragmentShader_;
        // Layout selection follows `textured`/`dualTexture`/`envMapping`/`skinned`/`pbr`/`pbrSkinned`
        // (LLGL-31/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect/PbrEffect): a lit-untextured
        // draw's shader declares no colorMap/samplerState binding, so it reuses primitiveLayout_
        // exactly like the unlit-untextured path does, not primitiveTexturedLayout_; a dual-texture
        // draw needs the layout with BOTH texture/sampler pairs; an env-map draw needs its own
        // dedicated layout (its own uniform block plus a texture/sampler pair AND a cube map
        // texture/sampler pair); a skinned draw needs its own dedicated layout too (its own
        // parameter uniform block, a separate bone transform block, and a texture/sampler pair); a
        // PBR draw needs its own dedicated layout with its own uniform block and 7 texture/sampler
        // pairs (base colour, normal, metallic-roughness, emissive, occlusion); a skinned PBR draw
        // needs that SAME PBR layout plus a bone transform block appended at its own binding.
        pipelineDesc.pipelineLayout = pbrSkinned ? primitivePbrSkinnedLayout_
                                     : pbr ? primitivePbrLayout_
                                     : skinned ? primitiveSkinnedLayout_
                                     : envMapping ? primitiveEnvMapLayout_
                                     : dualTexture ? primitiveDualTextureLayout_
                                     : textured ? primitiveTexturedLayout_ : primitiveLayout_;
        pipelineDesc.renderPass = GetPrimaryRenderPassEXT();
        pipelineDesc.primitiveTopology = MapPrimitiveTopology(primitive);
        pipelineDesc.depth.testEnabled = depthTestEnabled_;
        pipelineDesc.depth.writeEnabled = depthWriteEnabled_;
        pipelineDesc.depth.compareOp = MapCompareFunction(depthCompareFunction_);
        pipelineDesc.rasterizer.multiSampleEnabled = (GetPrimarySampleCountEXT() > 1);
        pipelineDesc.rasterizer.scissorTestEnabled = scissorEnabled;
        pipelineDesc.rasterizer.cullMode = MapCullMode(cullMode_);
        if (static_cast<XnaFillMode>(fillMode_) == XnaFillMode::WireFrame && !SupportsWireFrameEXT())
        {
            // LLGL's Vulkan module does not enable the device feature a line polygon mode needs, and
            // the request does not fail there -- it draws nothing at all, neither edges nor fill
            // (measured, not assumed). Silently rendering an empty frame for a state the caller
            // explicitly asked for is exactly the failure mode this project refuses.
            NotYetImplemented(kRendererName, "FillMode::WireFrame on the Vulkan renderer module");
        }
        pipelineDesc.rasterizer.polygonMode = MapFillMode(fillMode_);
        // XNA calls a clockwise winding front-facing, and it means clockwise ON SCREEN. Two Y
        // flips cancel out on the way here -- LLGL's clip space is Y-up, and its Vulkan viewport is
        // submitted with a negated height -- so the winding the rasterizer sees is the winding on
        // screen, and front-facing is simply "not counter-clockwise". Verified rather than
        // reasoned: with frontCCW true, CullClockwiseFace left a screen-clockwise triangle on
        // screen, which is the opposite of what XNA does.
        pipelineDesc.rasterizer.frontCCW = false;
        // LLGL-53: RasterizerState.DepthBias/SlopeScaleDepthBias -- raw XNA units pass straight
        // through to LLGL::DepthBiasDescriptor, the same convention the Vulkan renderer's own
        // `vkCmdSetDepthBias(draw.depthBias, 0.0f, draw.slopeScaleDepthBias)` already establishes
        // (no unit conversion; XNA has no clamp parameter, so `clamp` is always 0).
        pipelineDesc.rasterizer.depthBias.constantFactor = depthBias_;
        pipelineDesc.rasterizer.depthBias.slopeFactor = slopeScaleDepthBias_;
        pipelineDesc.rasterizer.depthBias.clamp = 0.0f;
        // LLGL-53: DepthStencilState's full front/back stencil test. XNA's TwoSidedStencilMode
        // == false uses the SAME (front, clockwise) ops/function for BOTH faces -- confirmed
        // against the Vulkan renderer's own identical fallback ("FNA's own real behavior: CCW
        // fields are simply ignored when this is false, not reset to any default"), which itself
        // mirrors EasyGL's own established convention.
        pipelineDesc.stencil.testEnabled = stencilRequested_;
        pipelineDesc.stencil.referenceDynamic = true;
        pipelineDesc.stencil.front.stencilFailOp = MapStencilOperation(stencilFail_);
        pipelineDesc.stencil.front.depthFailOp = MapStencilOperation(stencilDepthFail_);
        pipelineDesc.stencil.front.depthPassOp = MapStencilOperation(stencilPass_);
        pipelineDesc.stencil.front.compareOp = MapCompareFunction(stencilFunction_);
        pipelineDesc.stencil.front.readMask = static_cast<std::uint32_t>(stencilMask_);
        pipelineDesc.stencil.front.writeMask = static_cast<std::uint32_t>(stencilWriteMask_);
        if (twoSidedStencilMode_)
        {
            pipelineDesc.stencil.back.stencilFailOp = MapStencilOperation(ccwStencilFail_);
            pipelineDesc.stencil.back.depthFailOp = MapStencilOperation(ccwStencilDepthFail_);
            pipelineDesc.stencil.back.depthPassOp = MapStencilOperation(ccwStencilPass_);
            pipelineDesc.stencil.back.compareOp = MapCompareFunction(ccwStencilFunction_);
            pipelineDesc.stencil.back.readMask = static_cast<std::uint32_t>(stencilMask_);
            pipelineDesc.stencil.back.writeMask = static_cast<std::uint32_t>(stencilWriteMask_);
        }
        else
        {
            pipelineDesc.stencil.back = pipelineDesc.stencil.front;
        }
        pipelineDesc.blend.blendFactorDynamic = UsesConstantBlendFactorState();
        pipelineDesc.blend.targets[0].blendEnabled = !IsOpaqueBlendState();
        pipelineDesc.blend.targets[0].srcColor = MapBlendFactor(colorSrcBlend_);
        pipelineDesc.blend.targets[0].dstColor = MapBlendFactor(colorDstBlend_);
        pipelineDesc.blend.targets[0].colorArithmetic = MapBlendFunction(colorBlendFunc_);
        pipelineDesc.blend.targets[0].srcAlpha = MapBlendFactor(alphaSrcBlend_);
        pipelineDesc.blend.targets[0].dstAlpha = MapBlendFactor(alphaDstBlend_);
        pipelineDesc.blend.targets[0].alphaArithmetic = MapBlendFunction(alphaBlendFunc_);
        pipelineDesc.blend.targets[0].colorMask = MapColorWriteMask(colorWriteChannels_[0]);
        pipelineDesc.blend.sampleMask = multiSampleMask_;

        LLGL::PipelineState* pipeline = renderer_->CreatePipelineState(pipelineDesc);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: 3D pipeline creation failed");
        if (const LLGL::Report* report = pipeline->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: 3D pipeline link failed: " +
                    (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitivePipelineCache_.emplace(pipelineKey, pipeline);
        return pipeline;
    }

    void LlglRenderer::FillEffectUniforms(float (&uniforms)[kEffectUniformFloats],
                                                  const float matrix[16],
                                                  const GpuDrawParams* params)
    {
        std::memset(uniforms, 0, sizeof(float) * kEffectUniformFloats);
        std::memcpy(uniforms, matrix, sizeof(float) * 16);

        // Defaults for the fields every draw's shader reads, lit or not: opaque white and an
        // alpha test that always passes -- the same neutral values a stock effect that enables
        // none of these would produce. Fields only the lit shaders read (index 32 on) are left
        // zeroed by the memset above; a zeroed light is an XNA-disabled light (matches
        // DirectionalLight.Enabled's own zeroing), so that default is correct for them too.
        uniforms[16] = 1.0f; uniforms[17] = 1.0f; uniforms[18] = 1.0f; uniforms[19] = 1.0f;
        uniforms[30] = 1.0f; uniforms[31] = 1.0f;

        if (params == nullptr)
            return;

        uniforms[16] = params->diffuseColor[0];
        uniforms[17] = params->diffuseColor[1];
        uniforms[18] = params->diffuseColor[2];
        uniforms[19] = params->diffuseColor[3];

        if (params->fogEnabled)
        {
            uniforms[20] = params->fogColor[0];
            uniforms[21] = params->fogColor[1];
            uniforms[22] = params->fogColor[2];
            // The alpha channel carries the enable bit, so a disabled fog multiplies the whole
            // factor by zero in the shader instead of needing a branch.
            uniforms[23] = 1.0f;
            uniforms[24] = params->fogVector[0];
            uniforms[25] = params->fogVector[1];
            uniforms[26] = params->fogVector[2];
            uniforms[27] = params->fogVector[3];
        }

        uniforms[28] = params->alphaTest[0];
        uniforms[29] = params->alphaTest[1];
        uniforms[30] = params->alphaTest[2];
        uniforms[31] = params->alphaTest[3];

        // Read by the UNLIT colour-carrying shaders only (colored3d/colored_textured3d/
        // dual_textured3d's own extended Transform block declares this slot as
        // vertexColorEnabledPad.x): whether the vertex colour attribute should multiply into the
        // tint at all, matching BasicEffect/DualTextureEffect's real VertexColorEnabled -- a
        // vertex layout that happens to CARRY a colour attribute must not have it silently
        // applied when the effect never asked for it. Safely overwritten by worldMatrix below for
        // a lit draw; the LIT shaders read the equivalent flag from ambientColorLighting.w
        // (uniforms[51]) instead, since this slot becomes worldMatrix[0] for them.
        uniforms[32] = params->vertexColorEnabled ? 1.0f : 0.0f;

        if (!params->lightingEnabled)
            return;

        // From index 32: worldMatrix (16), ambientColorLighting (4), then light0/1/2's
        // dir/diffuse/specular (4 each, 36 total), emissiveColor (4), eyePositionWorld (4),
        // specularColorPower (4) -- see shaders/effect3d_common.glsl.inc for the byte layout this
        // mirrors field for field.
        std::memcpy(uniforms + 32, params->worldColMajor, sizeof(float) * 16);
        uniforms[48] = params->ambientColor[0];
        uniforms[49] = params->ambientColor[1];
        uniforms[50] = params->ambientColor[2];
        uniforms[51] = params->vertexColorEnabled ? 1.0f : 0.0f;

        const float* lightDirs[3]      = {params->light0Dir, params->light1Dir, params->light2Dir};
        const float* lightDiffuses[3]  = {params->light0Diffuse, params->light1Diffuse, params->light2Diffuse};
        const float* lightSpeculars[3] = {params->light0Specular, params->light1Specular, params->light2Specular};
        for (int light = 0; light < 3; ++light)
        {
            const std::size_t base = 52 + static_cast<std::size_t>(light) * 12;
            uniforms[base + 0] = lightDirs[light][0];
            uniforms[base + 1] = lightDirs[light][1];
            uniforms[base + 2] = lightDirs[light][2];
            uniforms[base + 4] = lightDiffuses[light][0];
            uniforms[base + 5] = lightDiffuses[light][1];
            uniforms[base + 6] = lightDiffuses[light][2];
            uniforms[base + 8] = lightSpeculars[light][0];
            uniforms[base + 9] = lightSpeculars[light][1];
            uniforms[base + 10] = lightSpeculars[light][2];
        }

        uniforms[88] = params->emissiveColor[0];
        uniforms[89] = params->emissiveColor[1];
        uniforms[90] = params->emissiveColor[2];
        uniforms[92] = params->eyePositionWorld[0];
        uniforms[93] = params->eyePositionWorld[1];
        uniforms[94] = params->eyePositionWorld[2];
        uniforms[96] = params->specularColor[0];
        uniforms[97] = params->specularColor[1];
        uniforms[98] = params->specularColor[2];
        uniforms[99] = params->specularPower;
    }

    void LlglRenderer::FillEnvMapUniforms(float (&uniforms)[kEnvMapUniformFloats],
                                                  const float matrix[16],
                                                  const GpuDrawParams& params)
    {
        std::memset(uniforms, 0, sizeof(float) * kEnvMapUniformFloats);
        std::memcpy(uniforms, matrix, sizeof(float) * 16);
        std::memcpy(uniforms + 16, params.worldColMajor, sizeof(float) * 16);

        uniforms[32] = params.diffuseColor[0];
        uniforms[33] = params.diffuseColor[1];
        uniforms[34] = params.diffuseColor[2];
        uniforms[35] = params.diffuseColor[3];

        // Pre-folded (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha -- see
        // EnvironmentMapEffect::FillGpuDrawParams() and env_map3d.frag.glsl's own comment on why
        // this is added unscaled rather than re-multiplied by diffuseColor a second time.
        uniforms[36] = params.emissiveColor[0];
        uniforms[37] = params.emissiveColor[1];
        uniforms[38] = params.emissiveColor[2];
        uniforms[39] = params.envMapAmount;

        uniforms[40] = params.light0Dir[0];
        uniforms[41] = params.light0Dir[1];
        uniforms[42] = params.light0Dir[2];
        uniforms[44] = params.light0Diffuse[0];
        uniforms[45] = params.light0Diffuse[1];
        uniforms[46] = params.light0Diffuse[2];

        uniforms[48] = params.light1Dir[0];
        uniforms[49] = params.light1Dir[1];
        uniforms[50] = params.light1Dir[2];
        uniforms[52] = params.light1Diffuse[0];
        uniforms[53] = params.light1Diffuse[1];
        uniforms[54] = params.light1Diffuse[2];

        uniforms[56] = params.light2Dir[0];
        uniforms[57] = params.light2Dir[1];
        uniforms[58] = params.light2Dir[2];
        uniforms[60] = params.light2Diffuse[0];
        uniforms[61] = params.light2Diffuse[1];
        uniforms[62] = params.light2Diffuse[2];

        uniforms[64] = params.eyePositionWorld[0];
        uniforms[65] = params.eyePositionWorld[1];
        uniforms[66] = params.eyePositionWorld[2];

        uniforms[68] = params.envMapSpecular[0];
        uniforms[69] = params.envMapSpecular[1];
        uniforms[70] = params.envMapSpecular[2];
        uniforms[71] = params.fresnelFactor;

        uniforms[72] = params.fresnelEnabled ? 1.0f : 0.0f;

        if (params.fogEnabled)
        {
            uniforms[76] = params.fogColor[0];
            uniforms[77] = params.fogColor[1];
            uniforms[78] = params.fogColor[2];
            // The alpha channel carries the enable bit, matching FillEffectUniforms's own
            // fogColor.a convention.
            uniforms[79] = 1.0f;
            uniforms[80] = params.fogVector[0];
            uniforms[81] = params.fogVector[1];
            uniforms[82] = params.fogVector[2];
            uniforms[83] = params.fogVector[3];
        }
        uniforms[84] = params.alphaTest[0];
        uniforms[85] = params.alphaTest[1];
        uniforms[86] = params.alphaTest[2];
        uniforms[87] = params.alphaTest[3];
    }

    void LlglRenderer::FillSkinnedUniforms(float (&uniforms)[kSkinnedUniformFloats],
                                                   const float matrix[16],
                                                   const GpuDrawParams& params)
    {
        std::memset(uniforms, 0, sizeof(float) * kSkinnedUniformFloats);
        std::memcpy(uniforms, matrix, sizeof(float) * 16);
        std::memcpy(uniforms + 16, params.worldColMajor, sizeof(float) * 16);

        uniforms[32] = params.diffuseColor[0];
        uniforms[33] = params.diffuseColor[1];
        uniforms[34] = params.diffuseColor[2];
        uniforms[35] = params.diffuseColor[3];

        // Pre-folded (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha, matching
        // EnvironmentMapEffect's own convention -- see skinned3d.frag.glsl's own comment.
        uniforms[36] = params.emissiveColor[0];
        uniforms[37] = params.emissiveColor[1];
        uniforms[38] = params.emissiveColor[2];
        // SkinnedEffect.VertexColorEnabled (LLGL-37): reuses emissiveColorPad's own otherwise-free
        // .w component as the gate, matching BasicEffect's identical ambientColorLighting.w reuse
        // trick -- SkinnedEffect has no separate ambient term of its own to occupy this slot.
        // Written unconditionally (not just for the colour-carrying shader variant): harmless for
        // the plain skinned3d.frag.glsl, which never reads it.
        uniforms[39] = params.vertexColorEnabled ? 1.0f : 0.0f;

        const float* lightDirs[3]      = {params.light0Dir, params.light1Dir, params.light2Dir};
        const float* lightDiffuses[3]  = {params.light0Diffuse, params.light1Diffuse, params.light2Diffuse};
        const float* lightSpeculars[3] = {params.light0Specular, params.light1Specular, params.light2Specular};
        for (int light = 0; light < 3; ++light)
        {
            const std::size_t base = 40 + static_cast<std::size_t>(light) * 12;
            uniforms[base + 0] = lightDirs[light][0];
            uniforms[base + 1] = lightDirs[light][1];
            uniforms[base + 2] = lightDirs[light][2];
            uniforms[base + 4] = lightDiffuses[light][0];
            uniforms[base + 5] = lightDiffuses[light][1];
            uniforms[base + 6] = lightDiffuses[light][2];
            uniforms[base + 8] = lightSpeculars[light][0];
            uniforms[base + 9] = lightSpeculars[light][1];
            uniforms[base + 10] = lightSpeculars[light][2];
        }

        uniforms[76] = params.eyePositionWorld[0];
        uniforms[77] = params.eyePositionWorld[1];
        uniforms[78] = params.eyePositionWorld[2];
        uniforms[79] = static_cast<float>(params.weightsPerVertex);

        uniforms[80] = params.specularColor[0];
        uniforms[81] = params.specularColor[1];
        uniforms[82] = params.specularColor[2];
        uniforms[83] = params.specularPower;

        if (params.fogEnabled)
        {
            uniforms[84] = params.fogColor[0];
            uniforms[85] = params.fogColor[1];
            uniforms[86] = params.fogColor[2];
            uniforms[87] = 1.0f;
            uniforms[88] = params.fogVector[0];
            uniforms[89] = params.fogVector[1];
            uniforms[90] = params.fogVector[2];
            uniforms[91] = params.fogVector[3];
        }
    }

    void LlglRenderer::FillSkinnedBoneData(float (&bones)[kSkinnedBoneFloats],
                                                   const GpuDrawParams& params)
    {
        std::memset(bones, 0, sizeof(float) * kSkinnedBoneFloats);
        const int boneCount = std::max(0, std::min(params.boneCount, 72));
        if (boneCount > 0)
        {
            std::memcpy(bones, params.boneTransforms,
                       sizeof(float) * 16 * static_cast<std::size_t>(boneCount));
        }
    }

    void LlglRenderer::FillPbrUniforms(float (&uniforms)[kPbrUniformFloats],
                                               const float matrix[16],
                                               const GpuDrawParams& params)
    {
        std::memset(uniforms, 0, sizeof(float) * kPbrUniformFloats);
        std::memcpy(uniforms, matrix, sizeof(float) * 16);
        std::memcpy(uniforms + 16, params.worldColMajor, sizeof(float) * 16);

        // Base colour factor and alpha are kept independent (not premultiplied) -- see
        // PbrEffect::FillGpuDrawParams()'s own comment and pbr3d.frag.glsl.
        uniforms[32] = params.diffuseColor[0];
        uniforms[33] = params.diffuseColor[1];
        uniforms[34] = params.diffuseColor[2];
        uniforms[35] = params.diffuseColor[3];

        uniforms[36] = params.ambientColor[0];
        uniforms[37] = params.ambientColor[1];
        uniforms[38] = params.ambientColor[2];
        uniforms[39] = params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;

        uniforms[40] = params.emissiveColor[0];
        uniforms[41] = params.emissiveColor[1];
        uniforms[42] = params.emissiveColor[2];
        uniforms[43] = params.pbrMetallicFactor;

        uniforms[44] = params.pbrRoughnessFactor;
        uniforms[45] = static_cast<float>(params.weightsPerVertex);
        uniforms[46] = params.pbrNormalScale;
        uniforms[47] = params.pbrOcclusionStrength;

        uniforms[48] = params.light0Dir[0];
        uniforms[49] = params.light0Dir[1];
        uniforms[50] = params.light0Dir[2];
        uniforms[51] = params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
        uniforms[52] = params.light0Diffuse[0];
        uniforms[53] = params.light0Diffuse[1];
        uniforms[54] = params.light0Diffuse[2];

        uniforms[56] = params.light1Dir[0];
        uniforms[57] = params.light1Dir[1];
        uniforms[58] = params.light1Dir[2];
        uniforms[60] = params.light1Diffuse[0];
        uniforms[61] = params.light1Diffuse[1];
        uniforms[62] = params.light1Diffuse[2];

        uniforms[64] = params.light2Dir[0];
        uniforms[65] = params.light2Dir[1];
        uniforms[66] = params.light2Dir[2];
        uniforms[68] = params.light2Diffuse[0];
        uniforms[69] = params.light2Diffuse[1];
        uniforms[70] = params.light2Diffuse[2];

        uniforms[72] = params.eyePositionWorld[0];
        uniforms[73] = params.eyePositionWorld[1];
        uniforms[74] = params.eyePositionWorld[2];
        uniforms[75] = params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;

        if (params.fogEnabled)
        {
            uniforms[76] = params.fogColor[0];
            uniforms[77] = params.fogColor[1];
            uniforms[78] = params.fogColor[2];
            // The alpha channel carries the enable bit, matching FillEffectUniforms's own
            // fogColor.a convention.
            uniforms[79] = 1.0f;
            uniforms[80] = params.fogVector[0];
            uniforms[81] = params.fogVector[1];
            uniforms[82] = params.fogVector[2];
            uniforms[83] = params.fogVector[3];
        }

        uniforms[88] = params.pbrDielectricF0Unclamped[0];
        uniforms[89] = params.pbrDielectricF0Unclamped[1];
        uniforms[90] = params.pbrDielectricF0Unclamped[2];
        uniforms[91] = params.pbrSpecularFactor;
        for (int row = 0; row < 10; ++row)
            for (int component = 0; component < 4; ++component)
                uniforms[92 + row * 4 + component] =
                    params.pbrTextureTransformRows[row][component];
        uniforms[132] = static_cast<float>(params.pbrTextureCoordinateSetMask & 0x7fu);
        uniforms[133] = params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f;
        for (int row = 0; row < 4; ++row)
            for (int component = 0; component < 4; ++component)
                uniforms[136 + row * 4 + component] =
                    params.pbrSpecularTextureTransformRows[row][component];
    }

    void LlglRenderer::EnsureDefaultPbrTexturesEXT()
    {
        if (defaultWhitePbrTexture_ != nullptr && defaultFlatNormalPbrTexture_ != nullptr)
            return;

        const auto createOnePixel = [&](const std::uint8_t rgba[4]) {
            LLGL::TextureDescriptor textureDesc;
            textureDesc.type = LLGL::TextureType::Texture2D;
            textureDesc.bindFlags = LLGL::BindFlags::Sampled;
            textureDesc.format = LLGL::Format::RGBA8UNorm;
            textureDesc.extent = {1, 1, 1};
            textureDesc.mipLevels = 1;
            textureDesc.miscFlags = 0;

            LLGL::ImageView imageView;
            imageView.format = LLGL::ImageFormat::RGBA;
            imageView.dataType = LLGL::DataType::UInt8;
            imageView.data = rgba;
            imageView.dataSize = 4;

            LLGL::Texture* texture = renderer_->CreateTexture(textureDesc, &imageView);
            if (texture == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: PbrEffect default texture creation failed");
            }
            return texture;
        };

        if (defaultWhitePbrTexture_ == nullptr)
        {
            const std::uint8_t white[4] = {255, 255, 255, 255};
            defaultWhitePbrTexture_ = createOnePixel(white);
        }
        if (defaultFlatNormalPbrTexture_ == nullptr)
        {
            // Decodes (via *2-1 in the fragment shader) to tangent-space (0,0,1) -- an unperturbed
            // surface normal.
            const std::uint8_t flatNormal[4] = {128, 128, 255, 255};
            defaultFlatNormalPbrTexture_ = createOnePixel(flatNormal);
        }
    }

    void LlglRenderer::QueuePrimitives(const LlglVertexBufferRenderer& vertexBuffer,
                                               const LlglIndexBufferRenderer* indexBuffer,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               int vertexStart, int startIndex, int baseVertex,
                                               const GpuDrawParams* params)
    {
        if (primitiveCount <= 0)
            return;

        // Real XNA MRT is meaningfully useful/testable only through a custom ShaderEffect with
        // multiple layout(location=N) out fragment outputs drawn via SpriteBatch -- see
        // LlglMRTBinding's own doc comment. A 3D colour-only draw's fixed single-output fragment
        // shader has nothing to write to slots 1..N-1, so it is refused by name here rather than
        // silently leaving them undefined.
        if (currentRenderTargetRenderer_ != nullptr && currentRenderTargetRenderer_->GetColorAttachmentCount() > 1)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: 3D drawing is not supported while multiple "
                "render targets are bound; draw through a custom ShaderEffect via SpriteBatch instead");
        }

        if (vertexBuffer.GetLlglBuffer() == nullptr || vertexBuffer.GetVertexCount() <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: the vertex buffer holds no data");

        const std::vector<LLGL::VertexAttribute>& attributes = vertexBuffer.GetVertexAttributes();
        if (attributes.empty())
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: this vertex layout is not supported by the "
                "colour-only 3D path (stride " + std::to_string(vertexBuffer.GetStride()) +
                " with no vertex declaration). Supply a VertexDeclaration, or use a "
                "VertexPositionColor layout.");
        }

        const int elementCount = CountElementsForPrimitives(primitive, primitiveCount);

        // The matrix is combined here, once per draw, in XNA's own row-vector order. ToColumnMajor
        // then writes the row-major storage out verbatim, which GLSL reads as column-major -- the
        // transpose, i.e. exactly the column-vector form the shader multiplies with.
        const Matrix combined = world * view * projection;
        float matrix[16] = {};
        combined.ToColumnMajor(matrix);

        // An XNA projection puts depth in [0,1] (the Direct3D convention). A render system whose
        // clip space is [-1,+1] would compress every scene into the upper half of its depth range
        // and stop clipping anything between the camera and the near plane, so the correction is
        // folded into this matrix rather than duplicated in a second shader variant.
        if (renderer_->GetRenderingCaps().clippingRange == LLGL::ClippingRange::MinusOneToOne)
        {
            for (int column = 0; column < 4; ++column)
            {
                float& z = matrix[column * 4 + 2];
                const float w = matrix[column * 4 + 3];
                z = 2.0f * z - w;
            }
        }

        const bool textured = (params != nullptr && params->textureEnabled && params->texture0 != nullptr);
        const bool lit = (params != nullptr && params->lightingEnabled);
        // LLGL-31: lighting without a texture is real now, provided the vertex layout carries
        // colours -- AcquirePrimitiveVertexShader() throws its own clear error otherwise, the same
        // way the unlit untextured path already does.
        const bool dualTexture = (params != nullptr && params->dualTexture);
        if (dualTexture && (params->texture0 == nullptr || params->texture1 == nullptr))
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: DualTextureEffect needs both Texture and "
                "Texture2 bound");
        }
        const bool envMapping = (params != nullptr && params->envMapping);
        if (envMapping && (params->texture0 == nullptr || params->envMap == nullptr))
        {
            // No fabricated-white-texture/cube fallback here either, matching the
            // DualTextureEffect precedent above -- see AcquirePrimitiveVertexShader()'s own
            // comment on this renderer's convention of failing by name instead.
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: EnvironmentMapEffect needs both Texture and "
                "EnvironmentMap bound");
        }

        ResolvedSampledTexture resolvedTexture;
        if (textured)
        {
            resolvedTexture = ResolveSampledTexture(*params->texture0);
            if (resolvedTexture.texture == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: the effect's texture belongs to another renderer");
            }
        }

        ResolvedSampledTexture resolvedTexture2;
        if (dualTexture)
        {
            resolvedTexture2 = ResolveSampledTexture(*params->texture1);
            if (resolvedTexture2.texture == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: DualTextureEffect's Texture2 belongs to "
                    "another renderer");
            }
        }

        LLGL::Texture* resolvedEnvMap = nullptr;
        if (envMapping)
        {
            resolvedEnvMap = ResolveSampledTextureCube(*params->envMap);
            if (resolvedEnvMap == nullptr)
            {
                throw System::NotSupportedException(
                    std::string(kRendererName) +
                    " renderer: EnvironmentMapEffect cube sampling is not supported by the "
                    "validated OpenGL render-system path");
            }
        }

        const bool skinned = (params != nullptr && params->skinned);
        const bool pbr = (params != nullptr && params->pbr);
        if (skinned && !pbr && params->texture0 == nullptr)
        {
            // No fabricated-white-texture fallback here either, matching the DualTextureEffect/
            // EnvironmentMapEffect precedent above -- SkinnedEffect is always textured in real
            // XNA, unlike BasicEffect.
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SkinnedEffect needs Texture bound");
        }
        if (pbr && params->texture0 == nullptr)
        {
            // Texture (base colour) is the one PbrEffect/SkinnedPbrEffect map that is not optional
            // -- matches SkinnedEffect's own precedent above. NormalMap/MetallicRoughnessMap/
            // EmissiveMap/OcclusionMap genuinely CAN be left null in real XNA usage (see
            // PbrEffect::FillGpuDrawParams()), so unlike this, they resolve to a 1x1 default
            // texture below instead of throwing.
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: " +
                (skinned ? "SkinnedPbrEffect" : "PbrEffect") + " needs Texture bound");
        }
        LLGL::Texture* resolvedPbrNormalMap = nullptr;
        LLGL::Texture* resolvedPbrMetallicRoughnessMap = nullptr;
        LLGL::Texture* resolvedPbrEmissiveMap = nullptr;
        LLGL::Texture* resolvedPbrOcclusionMap = nullptr;
        LLGL::Texture* resolvedPbrSpecularMap = nullptr;
        LLGL::Texture* resolvedPbrSpecularColorMap = nullptr;
        if (pbr)
        {
            EnsureDefaultPbrTexturesEXT();
            const auto resolveOrDefault = [&](const ITextureRenderer* map, LLGL::Texture* fallback,
                                              const char* label) {
                if (map == nullptr)
                    return fallback;
                LLGL::Texture* resolved = ResolveSampledTexture(*map).texture;
                if (resolved == nullptr)
                {
                    throw std::runtime_error(
                        std::string(kRendererName) + " renderer: " +
                        (skinned ? "SkinnedPbrEffect's " : "PbrEffect's ") + label +
                        " belongs to another renderer");
                }
                return resolved;
            };
            resolvedPbrNormalMap = resolveOrDefault(params->pbrNormalMap, defaultFlatNormalPbrTexture_,
                                                    "NormalMap");
            resolvedPbrMetallicRoughnessMap = resolveOrDefault(
                params->pbrMetallicRoughnessMap, defaultWhitePbrTexture_, "MetallicRoughnessMap");
            resolvedPbrEmissiveMap = resolveOrDefault(params->pbrEmissiveMap, defaultWhitePbrTexture_,
                                                      "EmissiveMap");
            resolvedPbrOcclusionMap = resolveOrDefault(params->pbrOcclusionMap, defaultWhitePbrTexture_,
                                                       "OcclusionMap");
            resolvedPbrSpecularMap = resolveOrDefault(params->pbrSpecularMap, defaultWhitePbrTexture_,
                                                      "SpecularMapEXT");
            resolvedPbrSpecularColorMap = resolveOrDefault(
                params->pbrSpecularColorMap, defaultWhitePbrTexture_, "SpecularColorMapEXT");
        }

        float uniforms[kEffectUniformFloats] = {};
        std::uint32_t transformIndex = 0;
        float envMapUniforms[kEnvMapUniformFloats] = {};
        std::uint32_t envMapUniformIndex = 0;
        float skinnedUniforms[kSkinnedUniformFloats] = {};
        std::uint32_t skinnedUniformIndex = 0;
        float skinnedBones[kSkinnedBoneFloats] = {};
        std::uint32_t skinnedBoneIndex = 0;
        float pbrUniforms[kPbrUniformFloats] = {};
        std::uint32_t pbrUniformIndex = 0;
        if (envMapping)
        {
            FillEnvMapUniforms(envMapUniforms, matrix, *params);
            envMapUniformIndex =
                static_cast<std::uint32_t>(envMapUniformData_.size() / kEnvMapUniformFloats);
            envMapUniformData_.insert(envMapUniformData_.end(),
                                      std::begin(envMapUniforms), std::end(envMapUniforms));
        }
        else if (pbr)
        {
            // Covers both PbrEffect (skinned == false) and SkinnedPbrEffect (skinned == true):
            // FillPbrUniforms() already writes WeightsPerVertex into PbrParams unconditionally
            // (unused by the unskinned shader, read by the skinned one), so both share this one
            // fill call; SkinnedPbrEffect additionally needs the bone transform array, filled the
            // exact same way -- and into the exact same pool -- as plain SkinnedEffect below.
            FillPbrUniforms(pbrUniforms, matrix, *params);
            pbrUniformIndex = static_cast<std::uint32_t>(pbrUniformData_.size() / kPbrUniformFloats);
            pbrUniformData_.insert(pbrUniformData_.end(),
                                   std::begin(pbrUniforms), std::end(pbrUniforms));

            if (skinned)
            {
                FillSkinnedBoneData(skinnedBones, *params);
                skinnedBoneIndex =
                    static_cast<std::uint32_t>(skinnedBoneData_.size() / kSkinnedBoneFloats);
                skinnedBoneData_.insert(skinnedBoneData_.end(),
                                        std::begin(skinnedBones), std::end(skinnedBones));
            }
        }
        else if (skinned)
        {
            FillSkinnedUniforms(skinnedUniforms, matrix, *params);
            skinnedUniformIndex =
                static_cast<std::uint32_t>(skinnedUniformData_.size() / kSkinnedUniformFloats);
            skinnedUniformData_.insert(skinnedUniformData_.end(),
                                       std::begin(skinnedUniforms), std::end(skinnedUniforms));

            FillSkinnedBoneData(skinnedBones, *params);
            skinnedBoneIndex =
                static_cast<std::uint32_t>(skinnedBoneData_.size() / kSkinnedBoneFloats);
            skinnedBoneData_.insert(skinnedBoneData_.end(),
                                    std::begin(skinnedBones), std::end(skinnedBones));
        }
        else
        {
            FillEffectUniforms(uniforms, matrix, params);
            transformIndex = static_cast<std::uint32_t>(transformData_.size() / kEffectUniformFloats);
            transformData_.insert(transformData_.end(), std::begin(uniforms), std::end(uniforms));
        }

        std::int32_t scissor[4] = {0, 0, 0, 0};
        const bool scissorEnabled = ComputeEffectiveScissor(scissor);

        FrameCommand command;
        command.kind = FrameCommand::Kind::Primitives;
        command.target = currentRenderTargetRenderer_ != nullptr
            ? currentRenderTargetRenderer_->GetLlglRenderTarget()
            : nullptr;
        command.mipRegenColorTexture = GetActiveMipRegenColorTextureEXT();
        command.vertexBuffer = vertexBuffer.GetLlglBuffer();
        command.pipeline = AcquirePrimitivePipeline(vertexBuffer, primitive, scissorEnabled, textured,
                                                     lit, dualTexture, envMapping, skinned, pbr);
        if (textured)
        {
            command.texture = resolvedTexture.texture;
            command.sampler = AcquireSampler(samplerFilter_[0], samplerAddressU_[0], samplerAddressV_[0],
                                             samplerMaxAnisotropy_[0]);
        }
        if (dualTexture)
        {
            // LLGL-49: DualTextureEffect's second texture occupies slot 1 (matches the Vulkan
            // renderer's own slotSamplers_[1] convention), independent of slot 0's own state.
            command.texture2 = resolvedTexture2.texture;
            command.sampler2 = AcquireSampler(samplerFilter_[1], samplerAddressU_[1], samplerAddressV_[1],
                                              samplerMaxAnisotropy_[1]);
        }
        if (envMapping)
        {
            // LLGL-49: EnvironmentMapEffect's cube map also occupies slot 1 -- mutually exclusive
            // with dualTexture per draw, so this never conflicts with the case above.
            command.envMapTexture = resolvedEnvMap;
            command.envMapSampler = AcquireSampler(samplerFilter_[1], samplerAddressU_[1], samplerAddressV_[1],
                                                    samplerMaxAnisotropy_[1]);
        }
        if (pbr)
        {
            // PbrEffect's 7 maps occupy SamplerStates[0..6] (base=0, already captured
            // above via the `textured` branch; matches the Vulkan renderer's own
            // PbrSlotSamplersRawEXT() convention) -- each of the remaining 6 needs its OWN
            // captured sampler rather than reusing slot 0's.
            command.pbrNormalTexture = resolvedPbrNormalMap;
            command.pbrNormalSampler = AcquireSampler(samplerFilter_[1], samplerAddressU_[1],
                                                      samplerAddressV_[1], samplerMaxAnisotropy_[1]);
            command.pbrMetallicRoughnessTexture = resolvedPbrMetallicRoughnessMap;
            command.pbrMetallicRoughnessSampler = AcquireSampler(samplerFilter_[2], samplerAddressU_[2],
                                                                 samplerAddressV_[2], samplerMaxAnisotropy_[2]);
            command.pbrEmissiveTexture = resolvedPbrEmissiveMap;
            command.pbrEmissiveSampler = AcquireSampler(samplerFilter_[3], samplerAddressU_[3],
                                                        samplerAddressV_[3], samplerMaxAnisotropy_[3]);
            command.pbrOcclusionTexture = resolvedPbrOcclusionMap;
            command.pbrOcclusionSampler = AcquireSampler(samplerFilter_[4], samplerAddressU_[4],
                                                         samplerAddressV_[4], samplerMaxAnisotropy_[4]);
            command.pbrSpecularTexture = resolvedPbrSpecularMap;
            command.pbrSpecularSampler = AcquireSampler(samplerFilter_[5], samplerAddressU_[5],
                                                        samplerAddressV_[5], samplerMaxAnisotropy_[5]);
            command.pbrSpecularColorTexture = resolvedPbrSpecularColorMap;
            command.pbrSpecularColorSampler = AcquireSampler(
                samplerFilter_[6], samplerAddressU_[6], samplerAddressV_[6], samplerMaxAnisotropy_[6]);
        }
        command.envMapping = envMapping;
        command.transformIndex = transformIndex;
        command.envMapUniformIndex = envMapUniformIndex;
        command.skinned = skinned;
        command.skinnedUniformIndex = skinnedUniformIndex;
        command.skinnedBoneIndex = skinnedBoneIndex;
        command.pbr = pbr;
        command.pbrUniformIndex = pbrUniformIndex;
        command.vertexCount = static_cast<std::uint32_t>(elementCount);
        command.firstVertex = static_cast<std::uint32_t>(std::max(0, vertexStart));
        command.firstIndex = static_cast<std::uint32_t>(std::max(0, startIndex));
        command.baseVertex = baseVertex;
        std::memcpy(command.blendFactor, blendFactor_, sizeof(command.blendFactor));
        command.usesBlendFactor = UsesConstantBlendFactorState();
        command.referenceStencilValue = static_cast<std::uint32_t>(referenceStencil_);
        std::memcpy(command.scissor, scissor, sizeof(command.scissor));
        command.scissorEnabled = scissorEnabled;
        CaptureFrameCommandViewportEXT(command);

        if (indexBuffer != nullptr)
        {
            if (indexBuffer->GetLlglBuffer() == nullptr || indexBuffer->GetIndexCount() <= 0)
                throw std::runtime_error(std::string(kRendererName) + " renderer: the index buffer holds no data");
            if (startIndex + elementCount > indexBuffer->GetIndexCount())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: the draw needs indices [" +
                    std::to_string(startIndex) + ", " + std::to_string(startIndex + elementCount) +
                    ") but the buffer holds " + std::to_string(indexBuffer->GetIndexCount()));
            }
            command.indexBuffer = indexBuffer->GetLlglBuffer();
        }
        else if (vertexStart + elementCount > vertexBuffer.GetVertexCount())
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the draw needs vertices [" +
                std::to_string(vertexStart) + ", " + std::to_string(vertexStart + elementCount) +
                ") but the buffer holds " + std::to_string(vertexBuffer.GetVertexCount()));
        }

        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    std::uint64_t LlglRenderer::MakeBlendPipelineKey(bool scissorEnabled,
                                                             int colorAttachmentCount,
                                                             int sampleCount) const
    {
        // Packs every field the sprite pipeline is built from, so a cache hit really is the same
        // pipeline: four blend factors, two blend functions, EVERY slot's own colour write mask
        // (LLGL-21 follow-up: slots 1..3 only ever matter while an MRT set is bound, but folding
        // all four in unconditionally is simpler than gating on colorAttachmentCount and costs
        // nothing -- a non-MRT bind always has slots 1..3 at their default all-channels value),
        // scissor, (MRT) how many colour attachments the pipeline's render pass declares -- a
        // pipeline built for a single-target bind is not interchangeable with one built for a
        // multi-target bind even when every other field matches, since the two need
        // different-shaped render passes -- and (MSAA render targets) the sample count the
        // pipeline's rasterizer/render pass were built with, since a single-sample pipeline is not
        // interchangeable with a multisampled one even when both targets have exactly one colour
        // attachment.
        std::uint64_t key = 0;
        key = key * 16u + static_cast<std::uint64_t>(colorSrcBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(colorDstBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(alphaSrcBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(alphaDstBlend_ & 0xF);
        key = key * 8u + static_cast<std::uint64_t>(colorBlendFunc_ & 0x7);
        key = key * 8u + static_cast<std::uint64_t>(alphaBlendFunc_ & 0x7);
        for (int slot = 0; slot < 4; ++slot)
            key = key * 16u + static_cast<std::uint64_t>(colorWriteChannels_[slot] & 0xF);
        key = key * 2u + (scissorEnabled ? 1u : 0u);
        key = key * 8u + static_cast<std::uint64_t>(colorAttachmentCount & 0x7);
        key = key * 64u + static_cast<std::uint64_t>(sampleCount & 0x3F);
        // BlendState.MultiSampleMask (LLGL-33): folded in via the SAME plain positional
        // multiply-add every other field above already uses -- an XOR + hash_combine-style mix
        // was tried first and, in the analogous spot in AcquirePrimitivePipeline's own key (see
        // that function's own doc comment), produced WRONG RENDERED PIXELS despite every logged
        // pipeline descriptor field being provably correct; root cause not fully understood, so
        // the same risky technique is avoided here too even though this specific spot was not
        // independently confirmed broken. `multiSampleMask_` is folded to its own low nibble only
        // (enough to distinguish the default 0xFFFFFFFF from a genuinely different mask like 0,
        // without risking a full 32-bit fold) -- MultiSampleMask is rarely non-default at all, and
        // two DIFFERENT non-default masks sharing a cached pipeline by coincidentally sharing a
        // low nibble is a much smaller, already-accepted class of risk, matching this function's
        // own `& 0x3`/`& 0x7`-style truncations for every other field.
        key = key * 16u + static_cast<std::uint64_t>(multiSampleMask_ & 0xFu);
        return key;
    }

    void LlglRenderer::FillCurrentBlendAndRasterStateEXT(
        LLGL::GraphicsPipelineDescriptor& pipelineDesc, bool scissorEnabled,
        int colorAttachmentCount) const
    {
        pipelineDesc.depth.testEnabled = false;
        pipelineDesc.depth.writeEnabled = false;
        pipelineDesc.rasterizer.multiSampleEnabled = (GetPrimarySampleCountEXT() > 1);
        pipelineDesc.rasterizer.scissorTestEnabled = scissorEnabled;
        pipelineDesc.rasterizer.cullMode = LLGL::CullMode::Disabled;
        // Dynamic blend-factor state is requested only when the blend state actually references the
        // constant blend colour. It is not free: LLGL implements it on OpenGL with glBlendColor,
        // which is genuinely absent from some GL proc tables (this project's own software-rasterized
        // test environment among them) and throws rather than being ignored (LLGL-18). Asking for it
        // unconditionally would make every draw depend on a call that the overwhelming majority of
        // blend states have no use for.
        pipelineDesc.blend.blendFactorDynamic = UsesConstantBlendFactorState();
        // BlendState.MultiSampleMask (LLGL-33): a single sample-coverage bitmask shared by every
        // attachment (XNA has only one, not one per MRT slot). Applied unconditionally by LLGL's
        // Vulkan module (VkPipelineMultisampleStateCreateInfo::pSampleMask, regardless of sample
        // count -- harmless to set even outside MSAA); never applied by the OpenGL module (its own
        // SetSampleMask call is permanently `#if 0`'d out in vendored LLGL).
        pipelineDesc.blend.sampleMask = multiSampleMask_;
        // MRT (LLGL-26 follow-up): every attachment gets slot 0's own blend FACTORS/FUNCTIONS --
        // XNA's BlendState has only one set of those, not one per slot -- but its OWN colour write
        // mask (LLGL-21 follow-up: BlendState.ColorWriteChannels1..3 are real now, not slot 0's
        // mask copied onto every attachment). colorAttachmentCount is 1 outside an MRT bind, so
        // this loop is a single iteration reading colorWriteChannels_[0] for every existing caller
        // that never touched ColorWriteChannels1..3 at all (they keep their all-channels default).
        const int clampedCount = std::max(1, std::min(colorAttachmentCount, 4));
        // LLGL only reads blend.targets[i] PER ATTACHMENT when independentBlendEnabled is true --
        // otherwise every attachment silently reuses targets[0] regardless of what targets[1..3]
        // were set to (confirmed by reading VKGraphicsPSO.cpp's own CreateColorBlendState:
        // `desc.targets[desc.independentBlendEnabled ? i : 0]`). Without this, a per-slot
        // ColorWriteChannels1..3 (LLGL-21 follow-up) would silently do nothing the moment more
        // than one attachment is bound -- the real bug this fixed, found by reading LLGL's own
        // source after a pixel test proved slot 1 was NOT actually being masked despite the
        // pipeline descriptor being built with the right per-slot colorMask values.
        pipelineDesc.blend.independentBlendEnabled = (clampedCount > 1);
        for (int slot = 0; slot < clampedCount; ++slot)
        {
            pipelineDesc.blend.targets[slot].blendEnabled = !IsOpaqueBlendState();
            pipelineDesc.blend.targets[slot].srcColor = MapBlendFactor(colorSrcBlend_);
            pipelineDesc.blend.targets[slot].dstColor = MapBlendFactor(colorDstBlend_);
            pipelineDesc.blend.targets[slot].colorArithmetic = MapBlendFunction(colorBlendFunc_);
            pipelineDesc.blend.targets[slot].srcAlpha = MapBlendFactor(alphaSrcBlend_);
            pipelineDesc.blend.targets[slot].dstAlpha = MapBlendFactor(alphaDstBlend_);
            pipelineDesc.blend.targets[slot].alphaArithmetic = MapBlendFunction(alphaBlendFunc_);
            pipelineDesc.blend.targets[slot].colorMask = MapColorWriteMask(colorWriteChannels_[slot]);
        }
    }

    LLGL::PipelineState* LlglRenderer::AcquireSpritePipeline(bool scissorEnabled)
    {
        const int colorAttachmentCount = GetActiveColorAttachmentCountEXT();
        const std::uint64_t key = MakeBlendPipelineKey(scissorEnabled, colorAttachmentCount,
                                                        GetPrimarySampleCountEXT());
        const auto cached = pipelineCache_.find(key);
        if (cached != pipelineCache_.end())
            return cached->second;

        LLGL::GraphicsPipelineDescriptor pipelineDesc;
        pipelineDesc.debugName = "CNA.Sprite";
        pipelineDesc.vertexShader = spriteVertexShader_;
        pipelineDesc.fragmentShader = spriteFragmentShader_;
        pipelineDesc.pipelineLayout = spriteLayout_;
        pipelineDesc.renderPass = GetPrimaryRenderPassEXT();
        pipelineDesc.primitiveTopology = LLGL::PrimitiveTopology::TriangleList;
        FillCurrentBlendAndRasterStateEXT(pipelineDesc, scissorEnabled, colorAttachmentCount);

        LLGL::PipelineState* pipeline = renderer_->CreatePipelineState(pipelineDesc);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: sprite pipeline creation failed");
        if (const LLGL::Report* pipelineReport = pipeline->GetReport())
        {
            if (pipelineReport->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: sprite pipeline link failed: " +
                    (pipelineReport->GetText() != nullptr ? pipelineReport->GetText() : "no details"));
            }
        }

        pipelineCache_.emplace(key, pipeline);
        return pipeline;
    }

    LLGL::Sampler* LlglRenderer::AcquireSampler(int filter, int addressU, int addressV, int maxAnisotropy)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(filter & 0xF) << 24) |
            (static_cast<std::uint64_t>(addressU & 0xF) << 20) |
            (static_cast<std::uint64_t>(addressV & 0xF) << 16) |
            static_cast<std::uint64_t>(maxAnisotropy & 0xFF);

        const auto cached = samplerCache_.find(key);
        if (cached != samplerCache_.end())
            return cached->second;

        LLGL::SamplerFilter minFilter = LLGL::SamplerFilter::Linear;
        LLGL::SamplerFilter magFilter = LLGL::SamplerFilter::Linear;
        LLGL::SamplerFilter mipFilter = LLGL::SamplerFilter::Linear;
        bool anisotropic = false;
        MapTextureFilter(filter, minFilter, magFilter, mipFilter, anisotropic);

        LLGL::SamplerDescriptor samplerDesc;
        samplerDesc.minFilter = minFilter;
        samplerDesc.magFilter = magFilter;
        samplerDesc.mipMapFilter = mipFilter;
        samplerDesc.addressModeU = MapAddressMode(addressU);
        samplerDesc.addressModeV = MapAddressMode(addressV);
        samplerDesc.addressModeW = LLGL::SamplerAddressMode::Clamp;
        samplerDesc.maxAnisotropy = anisotropic
            ? static_cast<std::uint32_t>(std::clamp(maxAnisotropy, 1, 16))
            : 1u;

        LLGL::Sampler* sampler = renderer_->CreateSampler(samplerDesc);
        if (sampler == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: sampler creation failed");

        samplerCache_.emplace(key, sampler);
        return sampler;
    }

    LlglRenderer::PresentationRect LlglRenderer::ComputePresentationRect() const
    {
        PresentationRect rect;

        const LLGL::Extent2D resolution = swapChain_ != nullptr
            ? swapChain_->GetResolution()
            : LLGL::Extent2D{0, 0};
        const float physicalWidth = static_cast<float>(resolution.width);
        const float physicalHeight = static_cast<float>(resolution.height);
        if (physicalWidth <= 0.0f || physicalHeight <= 0.0f)
            return rect;

        float logicalWidth = virtualWidth_ > 0 ? static_cast<float>(virtualWidth_) : physicalWidth;
        float logicalHeight = virtualHeight_ > 0 ? static_cast<float>(virtualHeight_) : physicalHeight;

        switch (static_cast<CnaPresentationMode>(presentationMode_))
        {
            case CnaPresentationMode::Stretch:
                rect.x = 0.0f;
                rect.y = 0.0f;
                rect.width = physicalWidth;
                rect.height = physicalHeight;
                break;

            case CnaPresentationMode::NativeBackBuffer:
                logicalWidth = physicalWidth;
                logicalHeight = physicalHeight;
                rect.x = 0.0f;
                rect.y = 0.0f;
                rect.width = physicalWidth;
                rect.height = physicalHeight;
                break;

            case CnaPresentationMode::Overscan:
            {
                const float scale = std::max(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }

            case CnaPresentationMode::FixedHeightDynamicWidth:
            {
                // The preferred height stays fixed and the logical width follows the real aspect
                // ratio, so the canvas fills the surface exactly and a wider window simply shows
                // more horizontal content.
                float derivedWidth = std::round(physicalWidth * logicalHeight / physicalHeight);
                if (derivedWidth <= 0.0f)
                    derivedWidth = physicalWidth;
                // LLGL-50: the aspect-derived width is a FLOOR, never a hard override -- a window
                // narrower (relative to its own height) than the game's own requested aspect must
                // not silently shrink an explicitly requested readable back buffer, or make
                // columns the game asked for (PreferredBackBufferWidth) permanently unaddressable
                // by any draw/readback/viewport call, all of which read this same rect.logicalWidth.
                // A wider window is unaffected (derivedWidth already exceeds virtualWidth_ there),
                // matching this mode's own already-tested "a wider window shows more content"
                // contract (llgl_presentation_test.cpp Check E) exactly as before.
                logicalWidth = virtualWidth_ > 0
                    ? std::max(derivedWidth, static_cast<float>(virtualWidth_))
                    : derivedWidth;
                const float scale = std::min(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }

            case CnaPresentationMode::Letterbox:
            default:
            {
                const float scale = std::min(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }
        }

        rect.logicalWidth = logicalWidth;
        rect.logicalHeight = logicalHeight;
        return rect;
    }

    LlglRenderer::PresentationRect LlglRenderer::GetActiveDrawRect() const
    {
        // A render-target-bound draw uses the target's own 1:1 pixel space -- the swap chain's
        // virtual-resolution letterbox/presentation-mode scaling only applies when drawing to the
        // actual window, matching every other renderer's own identical convention for this.
        if (currentRenderTargetRenderer_ != nullptr)
        {
            PresentationRect rect;
            rect.x = 0.0f;
            rect.y = 0.0f;
            rect.width = rect.logicalWidth = static_cast<float>(currentRenderTargetRenderer_->GetWidth());
            rect.height = rect.logicalHeight = static_cast<float>(currentRenderTargetRenderer_->GetHeight());
            return rect;
        }
        return ComputePresentationRect();
    }

    void LlglRenderer::GetViewportSize(int& width, int& height)
    {
        const PresentationRect rect = ComputePresentationRect();
        width = static_cast<int>(rect.logicalWidth);
        height = static_cast<int>(rect.logicalHeight);
    }

    void LlglRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (!surface_)
            return;
        surface_->Update(surface);
        UpdateSwapChainResolution();
    }

    void LlglRenderer::SetVirtualResolution(int width, int height)
    {
        if (width > 0) virtualWidth_ = width;
        if (height > 0) virtualHeight_ = height;

        // GraphicsDevice::Reset resizes and synchronizes the platform window before forwarding the
        // new virtual resolution here. Keep LLGL's swap chain in step immediately: waiting until
        // Present() left GetViewportSize() and every deferred draw queued during the first frame
        // using the constructor-time surface size, even though the window had already adopted the
        // requested back-buffer dimensions.
        UpdateSwapChainResolution();
    }

    void LlglRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = mode;
    }

    void LlglRenderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        if (swapChain_ != nullptr)
            swapChain_->SetVsyncInterval(static_cast<std::uint32_t>(interval > 0 ? interval : 0));
    }

    int LlglRenderer::GetMultiSampleCount() const
    {
        if (swapChain_ == nullptr)
            return 0;
        const std::uint32_t samples = swapChain_->GetSamples();
        return samples > 1 ? static_cast<int>(samples) : 0;
    }

    bool LlglRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        const PresentationRect rect = ComputePresentationRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f)
            return false;

        const float drawableX = surface_->WindowToDrawable(windowX);
        const float drawableY = surface_->WindowToDrawable(windowY);
        logX = (drawableX - rect.x) * rect.logicalWidth / rect.width;
        logY = (drawableY - rect.y) * rect.logicalHeight / rect.height;
        return drawableX >= rect.x && drawableX < rect.x + rect.width
            && drawableY >= rect.y && drawableY < rect.y + rect.height;
    }

    bool LlglRenderer::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        const PresentationRect rect = ComputePresentationRect();
        if (rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
            return false;

        windowX = surface_->DrawableToWindow(
            rect.x + logX * rect.width / rect.logicalWidth);
        windowY = surface_->DrawableToWindow(
            rect.y + logY * rect.height / rect.logicalHeight);
        return true;
    }

    std::unique_ptr<ITextureRenderer> LlglRenderer::CreateTexture(const ImageData& data)
    {
        if (data.width <= 0 || data.height <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: texture has no pixels");

        const std::size_t expected =
            static_cast<std::size_t>(data.width) * static_cast<std::size_t>(data.height) * 4u;

        LLGL::TextureDescriptor textureDesc;
        textureDesc.type = LLGL::TextureType::Texture2D;
        // Sampled for the sprite shader, plus the copy flags WriteTexture/ReadTexture need for
        // Texture2D::SetData()/GetData().
        textureDesc.bindFlags = LLGL::BindFlags::Sampled | LLGL::BindFlags::CopyDst |
                                LLGL::BindFlags::CopySrc;
        textureDesc.format = LLGL::Format::RGBA8UNorm;
        textureDesc.extent = {static_cast<std::uint32_t>(data.width),
                              static_cast<std::uint32_t>(data.height), 1};
        textureDesc.mipLevels = static_cast<std::uint32_t>(data.mipLevels > 0 ? data.mipLevels : 1);
        // No GenerateMips: the shared texture layer uploads each level it wants to exist, and
        // letting LLGL synthesise levels behind it would overwrite genuinely different content.
        textureDesc.miscFlags = 0;

        LLGL::ImageView imageView;
        LLGL::ImageView* initialImage = nullptr;
        if (data.pixels.size() >= expected)
        {
            imageView.format = LLGL::ImageFormat::RGBA;
            imageView.dataType = LLGL::DataType::UInt8;
            imageView.data = data.pixels.data();
            imageView.dataSize = expected;
            initialImage = &imageView;
        }

        LLGL::Texture* texture = renderer_->CreateTexture(textureDesc, initialImage);
        return std::make_unique<LlglTextureRenderer>(renderer_.get(), this, texture, data.width, data.height,
                                                    static_cast<int>(textureDesc.mipLevels));
    }

    std::unique_ptr<ITextureCubeRenderer> LlglRenderer::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        if (size <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: cube texture has no pixels");

        int mipLevels = 1;
        if (mipMap)
        {
            mipLevels = 1;
            for (int s = size; s > 1; s = std::max(1, s / 2))
                ++mipLevels;
        }

        // The supported LLGL/OpenGL contract is transfer-only for TextureCube. Always use the
        // bounded CPU store so the result does not vary with a driver capability bit: upload and
        // readback are exact, while shader sampling and RenderTargetCube reject deterministically.
        return std::make_unique<LlglTextureCubeRenderer>(
            renderer_.get(), this, nullptr, size, mipLevels);
    }

    std::unique_ptr<ITexture3DRenderer> LlglRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        if (w <= 0 || h <= 0 || depth <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: volume texture has no voxels");

        // Depth does not participate in the level count, matching FNA's own Texture3D
        // constructor (and the Vulkan renderer's identical CalculateVulkanTexture3DMipLevels).
        int mipLevels = 1;
        if (mipMap)
        {
            int mw = w, mh = h;
            mipLevels = 1;
            while (mw > 1 || mh > 1)
            {
                mw = std::max(1, mw / 2);
                mh = std::max(1, mh / 2);
                ++mipLevels;
            }
        }

        LLGL::TextureDescriptor textureDesc;
        textureDesc.type = LLGL::TextureType::Texture3D;
        textureDesc.bindFlags = LLGL::BindFlags::Sampled | LLGL::BindFlags::CopyDst |
                                LLGL::BindFlags::CopySrc;
        textureDesc.format = LLGL::Format::RGBA8UNorm;
        textureDesc.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                              static_cast<std::uint32_t>(depth)};
        textureDesc.mipLevels = static_cast<std::uint32_t>(mipLevels);
        // No GenerateMips: the shared texture layer uploads each level it wants to exist, same
        // reasoning as CreateTexture()/CreateTextureCube() above.
        textureDesc.miscFlags = 0;

        // No initial image -- the shared Texture3D layer always follows construction with its own
        // SetData() call per level, exactly like Texture2D's own typed constructors do.
        LLGL::Texture* texture = renderer_->CreateTexture(textureDesc, nullptr);
        return std::make_unique<LlglTexture3DRenderer>(renderer_.get(), this, texture, w, h, depth, mipLevels);
    }

    std::unique_ptr<ISpriteBatchRenderer> LlglRenderer::CreateSpriteBatch()
    {
        return std::make_unique<LlglSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IVertexBufferRenderer> LlglRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<LlglVertexBufferRenderer>(renderer_.get(), this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> LlglRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<LlglIndexBufferRenderer>(renderer_.get(), this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> LlglRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<LlglIndexBufferRenderer>(renderer_.get(), this, index_capacity, true);
    }

    std::unique_ptr<IOcclusionQueryRenderer> LlglRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<LlglOcclusionQueryRenderer>(renderer_.get(), queue_, this);
    }

    std::unique_ptr<IEffectRenderer> LlglRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<LlglEffectRenderer>(*this);
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    LLGL::PipelineLayout* LlglRenderer::AcquireCustomEffectLayoutEXT()
    {
        if (customEffectLayout_ != nullptr)
            return customEffectLayout_;

        // Same three bindings as spriteLayout_ (a custom effect samples the sprite's own texture
        // at the same slots), except binding 1's constant buffer is readable from BOTH stages: a
        // custom fragment shader reads pc.uColor from the very same buffer the vertex shader
        // reads pc.vpSize_pad/pc.uMatrix from, which spriteLayout_'s vertex-only binding does not
        // allow.
        LLGL::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"PC", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
        };
        layoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2}
        };
        customEffectLayout_ = renderer_->CreatePipelineLayout(layoutDesc);
        if (customEffectLayout_ == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: custom effect pipeline layout creation failed");
        }
        return customEffectLayout_;
    }

    void LlglRenderer::QueueClear(long flags, const float color[4], float depth, std::uint32_t stencil)
    {
        FrameCommand command;
        command.kind = FrameCommand::Kind::Clear;
        command.target = currentRenderTargetRenderer_ != nullptr
            ? currentRenderTargetRenderer_->GetLlglRenderTarget()
            : nullptr;
        command.mipRegenColorTexture = GetActiveMipRegenColorTextureEXT();
        command.clearFlags = flags;
        if (color != nullptr)
            std::memcpy(command.clearColor, color, sizeof(command.clearColor));
        command.clearDepth = depth;
        command.clearStencil = stencil;
        CaptureFrameCommandViewportEXT(command);
        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    void LlglRenderer::QueueQueryBeginEXT(LLGL::QueryHeap* queryHeap)
    {
        if (queryHeap == nullptr)
            return;

        FrameCommand command;
        command.kind = FrameCommand::Kind::QueryBegin;
        command.target = currentRenderTargetRenderer_ != nullptr
            ? currentRenderTargetRenderer_->GetLlglRenderTarget()
            : nullptr;
        command.mipRegenColorTexture = GetActiveMipRegenColorTextureEXT();
        command.queryHeap = queryHeap;
        frameCommands_.push_back(command);
    }

    void LlglRenderer::QueueQueryEndEXT(LLGL::QueryHeap* queryHeap)
    {
        if (queryHeap == nullptr)
            return;

        FrameCommand command;
        command.kind = FrameCommand::Kind::QueryEnd;
        command.target = currentRenderTargetRenderer_ != nullptr
            ? currentRenderTargetRenderer_->GetLlglRenderTarget()
            : nullptr;
        command.mipRegenColorTexture = GetActiveMipRegenColorTextureEXT();
        command.queryHeap = queryHeap;
        frameCommands_.push_back(command);
    }

    void LlglRenderer::Clear(float r, float g, float b, float a)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::Color, color, 1.0f, 0);
    }

    void LlglRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::ColorDepth, color, depth, 0);
    }

    void LlglRenderer::ClearDepth(float depth)
    {
        QueueClear(LLGL::ClearFlags::Depth, nullptr, depth, 0);
    }

    void LlglRenderer::ClearStencil(int stencil)
    {
        QueueClear(LLGL::ClearFlags::Stencil, nullptr, 1.0f, static_cast<std::uint32_t>(stencil));
    }

    void LlglRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        QueueClear(LLGL::ClearFlags::DepthStencil, nullptr, depth, static_cast<std::uint32_t>(stencil));
    }

    void LlglRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::Color | LLGL::ClearFlags::Stencil, color, 1.0f,
                   static_cast<std::uint32_t>(stencil));
    }

    void LlglRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::All, color, depth, static_cast<std::uint32_t>(stencil));
    }

    bool LlglRenderer::ComputeEffectiveScissor(std::int32_t outRect[4]) const
    {
        const PresentationRect rect = GetActiveDrawRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f ||
            rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
        {
            return false;
        }

        const float scaleX = rect.width / rect.logicalWidth;
        const float scaleY = rect.height / rect.logicalHeight;

        // A sub-viewport clips drawing in XNA. Sprite geometry is baked into window pixels at
        // queue time (see QueueSpriteEXT) and its GPU viewport always stays the whole target (see
        // CaptureFrameCommandViewportEXT()'s doc comment), so THAT clipping has to come from here
        // instead. Primitives now also get a real narrowed GPU viewport (which clips on its own),
        // so this intersection is redundant-but-harmless for them -- kept unconditional so both
        // kinds share one code path. Either way, this is further intersected with any scissor the
        // game set itself.
        float left = rect.x;
        float top = rect.y;
        float right = rect.x + rect.width;
        float bottom = rect.y + rect.height;
        bool restricted = false;

        if (viewportSet_)
        {
            left = std::max(left, rect.x + static_cast<float>(viewportRect_[0]) * scaleX);
            top = std::max(top, rect.y + static_cast<float>(viewportRect_[1]) * scaleY);
            right = std::min(right, rect.x + static_cast<float>(viewportRect_[0] + viewportRect_[2]) * scaleX);
            bottom = std::min(bottom, rect.y + static_cast<float>(viewportRect_[1] + viewportRect_[3]) * scaleY);
            restricted = true;
        }

        if (scissorTestEnabled_ && scissorRectSet_)
        {
            left = std::max(left, rect.x + static_cast<float>(scissorRect_[0]) * scaleX);
            top = std::max(top, rect.y + static_cast<float>(scissorRect_[1]) * scaleY);
            right = std::min(right, rect.x + static_cast<float>(scissorRect_[0] + scissorRect_[2]) * scaleX);
            bottom = std::min(bottom, rect.y + static_cast<float>(scissorRect_[1] + scissorRect_[3]) * scaleY);
            restricted = true;
        }

        if (!restricted)
            return false;

        outRect[0] = static_cast<std::int32_t>(std::lround(left));
        outRect[1] = static_cast<std::int32_t>(std::lround(top));
        outRect[2] = static_cast<std::int32_t>(std::max(0.0f, std::round(right - left)));
        outRect[3] = static_cast<std::int32_t>(std::max(0.0f, std::round(bottom - top)));
        return true;
    }

    void LlglRenderer::CaptureFrameCommandViewportEXT(FrameCommand& command) const
    {
        const LLGL::Extent2D resolution = currentRenderTargetRenderer_ != nullptr
            ? LLGL::Extent2D{static_cast<std::uint32_t>(std::max(0, currentRenderTargetRenderer_->GetWidth())),
                             static_cast<std::uint32_t>(std::max(0, currentRenderTargetRenderer_->GetHeight()))}
            : swapChain_->GetResolution();

        command.viewport[0] = 0.0f;
        command.viewport[1] = 0.0f;
        command.viewport[2] = static_cast<float>(resolution.width);
        command.viewport[3] = static_cast<float>(resolution.height);
        command.viewportMinDepth = 0.0f;
        command.viewportMaxDepth = 1.0f;

        if (command.kind != FrameCommand::Kind::Primitives || !viewportSet_)
            return;

        const PresentationRect rect = GetActiveDrawRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f ||
            rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
        {
            return;
        }

        const float scaleX = rect.width / rect.logicalWidth;
        const float scaleY = rect.height / rect.logicalHeight;
        command.viewport[0] = rect.x + static_cast<float>(viewportRect_[0]) * scaleX;
        command.viewport[1] = rect.y + static_cast<float>(viewportRect_[1]) * scaleY;
        command.viewport[2] = static_cast<float>(viewportRect_[2]) * scaleX;
        command.viewport[3] = static_cast<float>(viewportRect_[3]) * scaleY;
        // LLGL-53: the depth range is not spatial, so it needs no rect-style rescaling -- XNA's
        // MinDepth/MaxDepth pass straight through to the GPU viewport's own depth range.
        command.viewportMinDepth = minDepth_;
        command.viewportMaxDepth = maxDepth_;
    }

    void LlglRenderer::QueueSpriteEXT(const ITextureRenderer& texture,
                                              const Rectangle& destination,
                                              const Rectangle& source,
                                              const Color& color,
                                              float rotation,
                                              const Vector2& origin,
                                              SpriteEffects effects,
                                              const Matrix& transform,
                                              int filter, int addressU, int addressV)
    {
        const ResolvedSampledTexture resolved = ResolveSampledTexture(texture);
        if (resolved.texture == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SpriteBatch was given a texture from another renderer");
        }

        if (destination.Width == 0 || destination.Height == 0 ||
            source.Width == 0 || source.Height == 0 ||
            resolved.width <= 0 || resolved.height <= 0)
        {
            return;
        }

        const PresentationRect rect = GetActiveDrawRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f ||
            rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
        {
            return;
        }

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);

        std::array<Vector2, 4> points{
            Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};

        const float sinRotation = std::sin(rotation);
        const float cosRotation = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * cosRotation - point.Y * sinRotation +
                                   static_cast<float>(destination.X);
            const float rotatedY = point.X * sinRotation + point.Y * cosRotation +
                                   static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / static_cast<float>(resolved.width);
        float v0 = static_cast<float>(source.Y) / static_cast<float>(resolved.height);
        float u1 = static_cast<float>(source.X + source.Width) / static_cast<float>(resolved.width);
        float v1 = static_cast<float>(source.Y + source.Height) / static_cast<float>(resolved.height);

        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);

        const std::array<Vector2, 4> uv{
            Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int kQuadIndices[kSpriteVerticesPerQuad] = {0, 1, 2, 2, 1, 3};

        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };

        // A sub-Viewport is VIEWPORT-LOCAL in XNA/FNA (SpriteBatch.cs PrepRenderState): sprite
        // (0,0) is the viewport's own top-left corner, translated -- not rescaled -- by
        // Viewport.X/Y (see spritebatch_custom_viewport_test.cpp's own extensive comment on why
        // Width/Height only matter for the internal NDC math and cancel out of the final screen
        // position). This renderer never changes the GPU rasterizer viewport for sprites (it stays
        // the whole target, see CaptureFrameCommandViewportEXT()'s doc comment), so that offset
        // has to be baked into the geometry here instead.
        const float viewportOffsetX = viewportSet_ ? static_cast<float>(viewportRect_[0]) : 0.0f;
        const float viewportOffsetY = viewportSet_ ? static_cast<float>(viewportRect_[1]) : 0.0f;

        const auto firstVertex = static_cast<std::uint32_t>(spriteVertexData_.size() / kSpriteVertexFloats);
        for (const int cornerIndex : kQuadIndices)
        {
            // Baked straight into window pixels: the letterbox offset and scale live in the
            // geometry, which leaves the GPU viewport free to stay at the full window and the
            // projection constant for the whole frame.
            const float px = rect.x + (viewportOffsetX + points[cornerIndex].X) * rect.width / rect.logicalWidth;
            const float py = rect.y + (viewportOffsetY + points[cornerIndex].Y) * rect.height / rect.logicalHeight;
            spriteVertexData_.insert(spriteVertexData_.end(),
                {px, py, uv[cornerIndex].X, uv[cornerIndex].Y, rgba[0], rgba[1], rgba[2], rgba[3]});
        }

        std::int32_t scissor[4] = {0, 0, 0, 0};
        const bool scissorEnabled = ComputeEffectiveScissor(scissor);

        FrameCommand command;
        command.kind = FrameCommand::Kind::Sprite;
        command.target = currentRenderTargetRenderer_ != nullptr
            ? currentRenderTargetRenderer_->GetLlglRenderTarget()
            : nullptr;
        command.mipRegenColorTexture = GetActiveMipRegenColorTextureEXT();
        command.texture = resolved.texture;
        // SpriteBatch always samples through slot 0 (LLGL-49): filter/addressU/addressV are this
        // draw's own explicit SpriteBatch.Begin() arguments, but MaxAnisotropy has no such
        // per-draw override, so it still comes from GraphicsDevice.SamplerStates[0].
        command.sampler = AcquireSampler(filter, addressU, addressV, samplerMaxAnisotropy_[0]);

        if (currentCustomEffect_ != nullptr)
        {
            // A custom effect's own uniform buffer entirely replaces the stock projection buffer
            // (RT-specific or the frame-global one) at resource index 0 -- see
            // AcquireCustomEffectLayoutEXT's doc comment for why the binding SHAPE is identical.
            const int colorAttachmentCount = GetActiveColorAttachmentCountEXT();
            command.pipeline = currentCustomEffect_->AcquirePipeline(
                MakeBlendPipelineKey(scissorEnabled, colorAttachmentCount, GetPrimarySampleCountEXT()),
                scissorEnabled, colorAttachmentCount);
            command.hasCustomEffectUniform = true;
            command.customEffectUniformIndex =
                static_cast<std::uint32_t>(customEffectUniformData_.size() / kCustomEffectUniformFloats);
            const float* staging = currentCustomEffect_->GetUniformStaging();
            customEffectUniformData_.insert(customEffectUniformData_.end(),
                                            staging, staging + kCustomEffectUniformFloats);
            // vpSize: the PHYSICAL extent spriteVertexData_'s positions above are already baked
            // into -- the stock shader's own projection (UploadFrameResources) divides by the
            // real swap-chain resolution, NOT by rect.width/height, which is the LETTERBOXED
            // destination size under a scaled presentation and can be smaller than the window.
            // A render target has no letterboxing at all (GetActiveDrawRect's RT branch already
            // sets rect.width/logicalWidth to the same 1:1 value this uses), so the two agree
            // there. Not part of the effect's own uniform staging, since it depends on WHERE this
            // draw lands, not on anything the game's SetUniformX() calls control.
            float vpWidth = rect.width;
            float vpHeight = rect.height;
            if (currentRenderTargetRenderer_ == nullptr)
            {
                const LLGL::Extent2D resolution = swapChain_->GetResolution();
                vpWidth = static_cast<float>(resolution.width);
                vpHeight = static_cast<float>(resolution.height);
            }
            const std::size_t base =
                command.customEffectUniformIndex * kCustomEffectUniformFloats;
            customEffectUniformData_[base + 0] = vpWidth;
            customEffectUniformData_[base + 1] = vpHeight;
        }
        else
        {
            command.pipeline = AcquireSpritePipeline(scissorEnabled);
            command.projectionBuffer = currentRenderTargetRenderer_ != nullptr
                ? currentRenderTargetRenderer_->GetSpriteProjectionBuffer()
                : nullptr;
        }

        command.firstVertex = firstVertex;
        command.vertexCount = static_cast<std::uint32_t>(kSpriteVerticesPerQuad);
        std::memcpy(command.blendFactor, blendFactor_, sizeof(command.blendFactor));
        command.usesBlendFactor = UsesConstantBlendFactorState();
        std::memcpy(command.scissor, scissor, sizeof(command.scissor));
        command.scissorEnabled = scissorEnabled;
        CaptureFrameCommandViewportEXT(command);
        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    void LlglRenderer::UpdateSwapChainResolution()
    {
        if (swapChain_ == nullptr || !surface_)
            return;

        const LLGL::Extent2D contentSize = surface_->GetContentSize();
        if (contentSize.width == 0 || contentSize.height == 0)
            return;

        const LLGL::Extent2D current = swapChain_->GetResolution();
        if (contentSize.width != current.width || contentSize.height != current.height)
        {
            swapChain_->ResizeBuffers(contentSize);
            backbufferCacheValid_ = false;
        }
    }

    void LlglRenderer::UploadFrameResources()
    {
        const LLGL::Extent2D resolution = swapChain_->GetResolution();
        const float physicalWidth = static_cast<float>(resolution.width);
        const float physicalHeight = static_cast<float>(resolution.height);
        if (physicalWidth <= 0.0f || physicalHeight <= 0.0f)
            return;

        // Column-major orthographic projection from window pixels (Y down, origin top-left) to
        // clip space. Clip space is Y-up on every LLGL module: LLGL submits Vulkan viewports with
        // a negated height, which flips Vulkan's natively Y-down clip space to match OpenGL's.
        // RenderingCapabilities::screenOrigin does NOT describe this -- it describes where the
        // origin of viewport and scissor RECTANGLES sits, which LLGL normalizes to upper-left
        // everywhere -- so keying the sign off it renders the whole scene upside down (found by
        // reading back real pixels: the texture's bottom row appeared at the top of the sprite).
        const float projection[16] = {
            2.0f / physicalWidth, 0.0f,                   0.0f, 0.0f,
            0.0f,                 -2.0f / physicalHeight, 0.0f, 0.0f,
            0.0f,                 0.0f,                   1.0f, 0.0f,
            -1.0f,                1.0f,                   0.0f, 1.0f
        };
        renderer_->WriteBuffer(*spriteProjectionBuffer_, 0, projection, sizeof(projection));

        // One constant buffer per 3D draw recorded this frame. The pool only ever grows, so a
        // steady-state frame allocates nothing.
        const std::size_t transformCount = transformData_.size() / kEffectUniformFloats;
        while (transformBuffers_.size() < transformCount)
        {
            LLGL::BufferDescriptor transformDesc;
            transformDesc.size = sizeof(float) * kEffectUniformFloats;
            transformDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(transformDesc);
            if (buffer == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: transform buffer creation failed");
            transformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < transformCount; ++index)
        {
            renderer_->WriteBuffer(*transformBuffers_[index], 0,
                                   transformData_.data() + index * kEffectUniformFloats,
                                   sizeof(float) * kEffectUniformFloats);
        }

        // One small constant buffer per custom-effect sprite draw this frame -- same reasoning as
        // transformBuffers_ above: SetUniformX() can legitimately change between two Draw() calls
        // inside one Begin()/End() block, so a single shared buffer overwritten in place would
        // leak the LAST draw's values onto every earlier one once the frame is replayed.
        const std::size_t customEffectUniformCount =
            customEffectUniformData_.size() / kCustomEffectUniformFloats;
        while (customEffectUniformBuffers_.size() < customEffectUniformCount)
        {
            LLGL::BufferDescriptor uniformDesc;
            uniformDesc.size = sizeof(float) * kCustomEffectUniformFloats;
            uniformDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(uniformDesc);
            if (buffer == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: custom effect uniform buffer creation failed");
            }
            customEffectUniformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < customEffectUniformCount; ++index)
        {
            renderer_->WriteBuffer(*customEffectUniformBuffers_[index], 0,
                                   customEffectUniformData_.data() + index * kCustomEffectUniformFloats,
                                   sizeof(float) * kCustomEffectUniformFloats);
        }

        // One constant buffer per EnvironmentMapEffect draw this frame -- same growth/reuse
        // discipline as transformBuffers_, but its own pool because this effect's uniform block
        // has a different shape/size (kEnvMapUniformFloats, not kEffectUniformFloats).
        const std::size_t envMapUniformCount = envMapUniformData_.size() / kEnvMapUniformFloats;
        while (envMapUniformBuffers_.size() < envMapUniformCount)
        {
            LLGL::BufferDescriptor envMapDesc;
            envMapDesc.size = sizeof(float) * kEnvMapUniformFloats;
            envMapDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(envMapDesc);
            if (buffer == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: EnvironmentMapEffect uniform buffer creation failed");
            }
            envMapUniformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < envMapUniformCount; ++index)
        {
            renderer_->WriteBuffer(*envMapUniformBuffers_[index], 0,
                                   envMapUniformData_.data() + index * kEnvMapUniformFloats,
                                   sizeof(float) * kEnvMapUniformFloats);
        }

        // Two constant buffer pools per SkinnedEffect draw this frame -- the small parameter block
        // (kSkinnedUniformFloats) and the much larger 72-bone transform block (kSkinnedBoneFloats),
        // kept as separate pools for the same reason as envMapUniformBuffers_ above (own shape/size).
        const std::size_t skinnedUniformCount = skinnedUniformData_.size() / kSkinnedUniformFloats;
        while (skinnedUniformBuffers_.size() < skinnedUniformCount)
        {
            LLGL::BufferDescriptor skinnedDesc;
            skinnedDesc.size = sizeof(float) * kSkinnedUniformFloats;
            skinnedDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(skinnedDesc);
            if (buffer == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: SkinnedEffect uniform buffer creation failed");
            }
            skinnedUniformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < skinnedUniformCount; ++index)
        {
            renderer_->WriteBuffer(*skinnedUniformBuffers_[index], 0,
                                   skinnedUniformData_.data() + index * kSkinnedUniformFloats,
                                   sizeof(float) * kSkinnedUniformFloats);
        }

        const std::size_t skinnedBoneCount = skinnedBoneData_.size() / kSkinnedBoneFloats;
        while (skinnedBoneBuffers_.size() < skinnedBoneCount)
        {
            LLGL::BufferDescriptor boneDesc;
            boneDesc.size = sizeof(float) * kSkinnedBoneFloats;
            boneDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(boneDesc);
            if (buffer == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: SkinnedEffect bone buffer creation failed");
            }
            skinnedBoneBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < skinnedBoneCount; ++index)
        {
            renderer_->WriteBuffer(*skinnedBoneBuffers_[index], 0,
                                   skinnedBoneData_.data() + index * kSkinnedBoneFloats,
                                   sizeof(float) * kSkinnedBoneFloats);
        }

        // One constant buffer per PbrEffect draw this frame -- same growth/reuse discipline as
        // envMapUniformBuffers_/skinnedUniformBuffers_ above, own pool because this effect's
        // uniform block has its own shape/size.
        const std::size_t pbrUniformCount = pbrUniformData_.size() / kPbrUniformFloats;
        while (pbrUniformBuffers_.size() < pbrUniformCount)
        {
            LLGL::BufferDescriptor pbrDesc;
            pbrDesc.size = sizeof(float) * kPbrUniformFloats;
            pbrDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(pbrDesc);
            if (buffer == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: PbrEffect uniform buffer creation failed");
            }
            pbrUniformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < pbrUniformCount; ++index)
        {
            renderer_->WriteBuffer(*pbrUniformBuffers_[index], 0,
                                   pbrUniformData_.data() + index * kPbrUniformFloats,
                                   sizeof(float) * kPbrUniformFloats);
        }

        if (spriteVertexData_.empty())
            return;

        const std::size_t vertexCount = spriteVertexData_.size() / kSpriteVertexFloats;
        const std::size_t byteSize = spriteVertexData_.size() * sizeof(float);

        if (spriteVertexBuffer_ == nullptr || vertexCount > spriteVertexCapacity_)
        {
            if (spriteVertexBuffer_ != nullptr)
                renderer_->Release(*spriteVertexBuffer_);

            const LLGL::VertexFormat vertexFormat = MakeSpriteVertexFormat();
            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.bindFlags = LLGL::BindFlags::VertexBuffer;
            bufferDesc.vertexAttribs = vertexFormat.attributes;
            spriteVertexBuffer_ = renderer_->CreateBuffer(bufferDesc, spriteVertexData_.data());
            if (spriteVertexBuffer_ == nullptr)
                throw std::runtime_error(std::string(kRendererName) + " renderer: sprite vertex buffer creation failed");
            spriteVertexCapacity_ = vertexCount;
        }
        else
        {
            renderer_->WriteBuffer(*spriteVertexBuffer_, 0, spriteVertexData_.data(), byteSize);
        }
    }

    void LlglRenderer::ScheduleBufferReleaseEXT(LLGL::Buffer* buffer)
    {
        if (buffer == nullptr)
            return;

        // Nothing recorded refers to it, so there is nothing to wait for.
        if (frameCommands_.empty())
        {
            renderer_->Release(*buffer);
            return;
        }

        pendingBufferReleases_.push_back(buffer);
    }

    void LlglRenderer::ScheduleTextureReleaseEXT(LLGL::Texture* texture)
    {
        if (texture == nullptr)
            return;

        // Nothing recorded refers to it, so there is nothing to wait for -- same reasoning as
        // ScheduleBufferReleaseEXT.
        if (frameCommands_.empty())
        {
            renderer_->Release(*texture);
            return;
        }

        pendingTextureReleases_.push_back(texture);
    }

    void LlglRenderer::ScheduleRenderTargetReleaseEXT(LLGL::RenderTarget* renderTarget,
                                                              LLGL::Texture* colorTexture,
                                                              LLGL::Texture* depthTexture,
                                                              LLGL::Buffer* spriteProjectionBuffer)
    {
        // Nothing recorded refers to it, so there is nothing to wait for -- same reasoning as
        // ScheduleBufferReleaseEXT.
        const bool deferred = !frameCommands_.empty();

        if (renderTarget != nullptr)
        {
            if (deferred) pendingRenderTargetReleases_.push_back(renderTarget);
            else renderer_->Release(*renderTarget);
        }
        // depthTexture is null whenever the depth/stencil attachment was created anonymously (the
        // current CreateRenderTarget2D path always does this) -- see LlglRenderTargetRenderer's
        // destructor comment.
        if (depthTexture != nullptr)
        {
            if (deferred) pendingTextureReleases_.push_back(depthTexture);
            else renderer_->Release(*depthTexture);
        }
        if (colorTexture != nullptr)
        {
            if (deferred) pendingTextureReleases_.push_back(colorTexture);
            else renderer_->Release(*colorTexture);
        }
        if (spriteProjectionBuffer != nullptr)
        {
            if (deferred) pendingBufferReleases_.push_back(spriteProjectionBuffer);
            else renderer_->Release(*spriteProjectionBuffer);
        }
    }

    void LlglRenderer::ScheduleQueryHeapReleaseEXT(LLGL::QueryHeap* queryHeap)
    {
        if (queryHeap == nullptr)
            return;

        // Nothing recorded refers to it, so there is nothing to wait for -- same reasoning as
        // ScheduleBufferReleaseEXT.
        if (frameCommands_.empty())
        {
            renderer_->Release(*queryHeap);
            return;
        }

        pendingQueryHeapReleases_.push_back(queryHeap);
    }

    void LlglRenderer::ScheduleEffectResourceReleaseEXT(
        LLGL::Shader* vertexShader, LLGL::Shader* fragmentShader,
        const std::map<std::uint64_t, LLGL::PipelineState*>& pipelines)
    {
        const bool deferred = !frameCommands_.empty();

        for (const auto& entry : pipelines)
        {
            if (entry.second == nullptr)
                continue;
            if (deferred) pendingPipelineReleases_.push_back(entry.second);
            else renderer_->Release(*entry.second);
        }
        if (vertexShader != nullptr)
        {
            if (deferred) pendingShaderReleases_.push_back(vertexShader);
            else renderer_->Release(*vertexShader);
        }
        if (fragmentShader != nullptr)
        {
            if (deferred) pendingShaderReleases_.push_back(fragmentShader);
            else renderer_->Release(*fragmentShader);
        }
    }

    void LlglRenderer::ReleasePendingBuffers()
    {
        if (pendingBufferReleases_.empty() && pendingRenderTargetReleases_.empty() &&
            pendingTextureReleases_.empty() && pendingQueryHeapReleases_.empty() &&
            pendingShaderReleases_.empty() && pendingPipelineReleases_.empty())
        {
            return;
        }

        // The submitted frame may still be reading these resources. Waiting only happens on the
        // frames where a resource actually died, which is not a hot path.
        queue_->WaitIdle();
        for (LLGL::Buffer* buffer : pendingBufferReleases_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        pendingBufferReleases_.clear();

        // Render targets before their own textures -- same order LlglRenderTargetRenderer's own
        // destructor uses.
        for (LLGL::RenderTarget* target : pendingRenderTargetReleases_)
        {
            if (target != nullptr)
                renderer_->Release(*target);
        }
        pendingRenderTargetReleases_.clear();
        for (LLGL::Texture* texture : pendingTextureReleases_)
        {
            if (texture != nullptr)
                renderer_->Release(*texture);
        }
        pendingTextureReleases_.clear();
        for (LLGL::QueryHeap* queryHeap : pendingQueryHeapReleases_)
        {
            if (queryHeap != nullptr)
                renderer_->Release(*queryHeap);
        }
        pendingQueryHeapReleases_.clear();

        // Pipelines before their own shader modules -- a pipeline references the shaders it was
        // built from.
        for (LLGL::PipelineState* pipeline : pendingPipelineReleases_)
        {
            if (pipeline != nullptr)
                renderer_->Release(*pipeline);
        }
        pendingPipelineReleases_.clear();
        for (LLGL::Shader* shader : pendingShaderReleases_)
        {
            if (shader != nullptr)
                renderer_->Release(*shader);
        }
        pendingShaderReleases_.clear();
    }

    void LlglRenderer::FlushPendingFrameEXT()
    {
        if (frameCommands_.empty())
            return;

        UploadFrameResources();
        RecordAndSubmitFrame();
        queue_->WaitIdle();
        ReleasePendingBuffers();

        frameCommands_.clear();
        spriteVertexData_.clear();
        transformData_.clear();
        customEffectUniformData_.clear();
        envMapUniformData_.clear();
        skinnedUniformData_.clear();
        skinnedBoneData_.clear();
        pbrUniformData_.clear();
        frameSubmitted_ = true;
    }

    LLGL::RenderPass* LlglRenderer::AcquireLoadRenderPassEXT(std::uint32_t numColorAttachments,
                                                                    bool hasDepthStencil,
                                                                    std::uint32_t sampleCount)
    {
        const std::uint32_t samples = sampleCount > 0 ? sampleCount : 1;
        const std::uint64_t key = (static_cast<std::uint64_t>(numColorAttachments) << 40) |
                                   (static_cast<std::uint64_t>(hasDepthStencil ? 1u : 0u) << 32) |
                                   static_cast<std::uint64_t>(samples);

        const auto found = loadRenderPassCache_.find(key);
        if (found != loadRenderPassCache_.end())
            return found->second;

        LLGL::RenderPassDescriptor passDesc;
        passDesc.samples = samples;
        const LLGL::AttachmentFormatDescriptor colorFormat{
            swapChain_->GetColorFormat(), LLGL::AttachmentLoadOp::Load, LLGL::AttachmentStoreOp::Store};
        for (std::uint32_t i = 0; i < numColorAttachments && i < LLGL_MAX_NUM_COLOR_ATTACHMENTS; ++i)
            passDesc.colorAttachments[i] = colorFormat;
        if (hasDepthStencil)
        {
            const LLGL::AttachmentFormatDescriptor depthStencilFormat{
                swapChain_->GetDepthStencilFormat(), LLGL::AttachmentLoadOp::Load, LLGL::AttachmentStoreOp::Store};
            passDesc.depthAttachment = depthStencilFormat;
            passDesc.stencilAttachment = depthStencilFormat;
        }

        LLGL::RenderPass* pass = renderer_->CreateRenderPass(passDesc);
        if (pass == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: load render pass creation failed");

        loadRenderPassCache_[key] = pass;
        return pass;
    }

    std::vector<LlglRenderer::FrameCommandBucket> LlglRenderer::GroupFrameCommandsByTargetEXT() const
    {
        // LLGL-45: segmented in TRUE public order -- a new segment starts only when the target
        // actually CHANGES from the immediately preceding command, so two genuinely adjacent
        // commands on the same target (no other target's command between them) still land in one
        // segment exactly as before, but a target revisited later in the frame (after another
        // target's or the swap chain's own commands ran) becomes its OWN new segment in its own
        // original position, instead of being retroactively merged into whichever segment first
        // used that target. This replaces the old "group by target identity, swap chain always
        // last" scheme, which could let a composite-onto-backbuffer draw sample a target BEFORE
        // that target's own later-queued producer had run, or let a backbuffer draw queued between
        // two binds of the same off-screen target see that target's FINAL content instead of the
        // content it actually had at the time it was drawn (see known_bugs.md's now-fixed
        // "render-target/back-buffer replay does not preserve public call order" entry).
        std::vector<FrameCommandBucket> segments;
        for (const FrameCommand& command : frameCommands_)
        {
            if (segments.empty() || segments.back().target != command.target)
            {
                FrameCommandBucket segment;
                segment.target = command.target;
                segments.push_back(std::move(segment));
            }
            segments.back().commands.push_back(&command);
        }

        // Every RecordAndSubmitFrame/CaptureBackbuffer caller needs a swap-chain pass regardless of
        // whether this frame drew to it directly (Present() must always submit something, and
        // CaptureBackbuffer's framebuffer copy can only run inside one) -- appended at the end only
        // when the frame's own true order never touched the swap chain at all; when it did, that
        // segment already exists in its own correct position and is not duplicated or moved.
        const bool hasSwapChainSegment = std::any_of(segments.begin(), segments.end(),
            [](const FrameCommandBucket& segment) { return segment.target == nullptr; });
        if (!hasSwapChainSegment)
            segments.push_back(FrameCommandBucket{});

        return segments;
    }

    void LlglRenderer::ReplayFrameCommandsList(const std::vector<const FrameCommand*>& commands)
    {
        for (const FrameCommand* commandPtr : commands)
        {
            const FrameCommand& command = *commandPtr;
            switch (command.kind)
            {
                case FrameCommand::Kind::Clear:
                {
                    commands_->SetViewport(LLGL::Viewport{command.viewport[0], command.viewport[1],
                                                          command.viewport[2], command.viewport[3],
                                                          command.viewportMinDepth, command.viewportMaxDepth});
                    LLGL::ClearValue clearValue;
                    std::memcpy(clearValue.color, command.clearColor, sizeof(clearValue.color));
                    clearValue.depth = command.clearDepth;
                    clearValue.stencil = command.clearStencil;
                    commands_->Clear(command.clearFlags, clearValue);
                    break;
                }

                case FrameCommand::Kind::Primitives:
                {
                    // command.pbr is checked before command.skinned: SkinnedPbrEffect sets BOTH,
                    // and needs the PBR branch (its own uniform shape) plus the bone buffer,
                    // not the plain-SkinnedEffect uniform buffer.
                    const bool uniformBufferBound = command.envMapping
                        ? command.envMapUniformIndex < envMapUniformBuffers_.size()
                        : command.pbr
                        ? (command.pbrUniformIndex < pbrUniformBuffers_.size() &&
                           (!command.skinned || command.skinnedBoneIndex < skinnedBoneBuffers_.size()))
                        : command.skinned
                        ? (command.skinnedUniformIndex < skinnedUniformBuffers_.size() &&
                           command.skinnedBoneIndex < skinnedBoneBuffers_.size())
                        : command.transformIndex < transformBuffers_.size();
                    if (command.vertexBuffer == nullptr || command.pipeline == nullptr ||
                        !uniformBufferBound)
                    {
                        break;
                    }

                    commands_->SetViewport(LLGL::Viewport{command.viewport[0], command.viewport[1],
                                                          command.viewport[2], command.viewport[3],
                                                          command.viewportMinDepth, command.viewportMaxDepth});
                    commands_->SetPipelineState(*command.pipeline);
                    if (command.usesBlendFactor)
                        commands_->SetBlendFactor(command.blendFactor);
                    // LLGL-53: the pipeline's stencil.referenceDynamic is always true for this
                    // command kind, so a reference value must be set on every Primitives draw
                    // regardless of whether stencil testing is actually enabled -- LLGL requires a
                    // dynamic state to be set before the draw that uses its pipeline, not gated
                    // behind whether that state happens to matter for this particular pipeline.
                    commands_->SetStencilReference(command.referenceStencilValue);
                    if (command.scissorEnabled)
                    {
                        commands_->SetScissor(LLGL::Scissor{command.scissor[0], command.scissor[1],
                                                            command.scissor[2], command.scissor[3]});
                    }
                    if (command.envMapping)
                    {
                        commands_->SetResource(0, *envMapUniformBuffers_[command.envMapUniformIndex]);
                        if (command.texture != nullptr && command.sampler != nullptr)
                        {
                            commands_->SetResource(1, *command.texture);
                            commands_->SetResource(2, *command.sampler);
                        }
                        if (command.envMapTexture != nullptr && command.envMapSampler != nullptr)
                        {
                            commands_->SetResource(3, *command.envMapTexture);
                            commands_->SetResource(4, *command.envMapSampler);
                        }
                    }
                    else if (command.pbr)
                    {
                        commands_->SetResource(0, *pbrUniformBuffers_[command.pbrUniformIndex]);
                        // Each of the 7 texture units binds its OWN captured sampler
                        // (SamplerStates[0..6]) instead of all of them sharing slot 0's.
                        if (command.texture != nullptr && command.sampler != nullptr)
                        {
                            commands_->SetResource(1, *command.texture);
                            commands_->SetResource(2, *command.sampler);
                        }
                        if (command.pbrNormalTexture != nullptr && command.pbrNormalSampler != nullptr)
                        {
                            commands_->SetResource(3, *command.pbrNormalTexture);
                            commands_->SetResource(4, *command.pbrNormalSampler);
                        }
                        if (command.pbrMetallicRoughnessTexture != nullptr && command.pbrMetallicRoughnessSampler != nullptr)
                        {
                            commands_->SetResource(5, *command.pbrMetallicRoughnessTexture);
                            commands_->SetResource(6, *command.pbrMetallicRoughnessSampler);
                        }
                        if (command.pbrEmissiveTexture != nullptr && command.pbrEmissiveSampler != nullptr)
                        {
                            commands_->SetResource(7, *command.pbrEmissiveTexture);
                            commands_->SetResource(8, *command.pbrEmissiveSampler);
                        }
                        if (command.pbrOcclusionTexture != nullptr && command.pbrOcclusionSampler != nullptr)
                        {
                            commands_->SetResource(9, *command.pbrOcclusionTexture);
                            commands_->SetResource(10, *command.pbrOcclusionSampler);
                        }
                        if (command.pbrSpecularTexture != nullptr && command.pbrSpecularSampler != nullptr)
                        {
                            commands_->SetResource(11, *command.pbrSpecularTexture);
                            commands_->SetResource(12, *command.pbrSpecularSampler);
                        }
                        if (command.pbrSpecularColorTexture != nullptr && command.pbrSpecularColorSampler != nullptr)
                        {
                            commands_->SetResource(13, *command.pbrSpecularColorTexture);
                            commands_->SetResource(14, *command.pbrSpecularColorSampler);
                        }
                        // SkinnedPbrEffect: the bone transform array, reused verbatim from
                        // SkinnedEffect's own pool -- bone data is effect-agnostic. Bound at
                        // resource index 15, matching primitivePbrSkinnedLayout_'s own BoneBlock
                        // binding placed AFTER every PBR texture pair rather than shifting them.
                        if (command.skinned)
                            commands_->SetResource(15, *skinnedBoneBuffers_[command.skinnedBoneIndex]);
                    }
                    else if (command.skinned)
                    {
                        commands_->SetResource(0, *skinnedUniformBuffers_[command.skinnedUniformIndex]);
                        commands_->SetResource(1, *skinnedBoneBuffers_[command.skinnedBoneIndex]);
                        if (command.texture != nullptr && command.sampler != nullptr)
                        {
                            commands_->SetResource(2, *command.texture);
                            commands_->SetResource(3, *command.sampler);
                        }
                    }
                    else
                    {
                        commands_->SetResource(0, *transformBuffers_[command.transformIndex]);
                        if (command.texture != nullptr && command.sampler != nullptr)
                        {
                            commands_->SetResource(1, *command.texture);
                            commands_->SetResource(2, *command.sampler);
                        }
                        if (command.texture2 != nullptr && command.sampler2 != nullptr)
                        {
                            commands_->SetResource(3, *command.texture2);
                            commands_->SetResource(4, *command.sampler2);
                        }
                    }
                    commands_->SetVertexBuffer(*command.vertexBuffer);
                    if (command.indexBuffer != nullptr)
                    {
                        commands_->SetIndexBuffer(*command.indexBuffer);
                        commands_->DrawIndexed(command.vertexCount, command.firstIndex, command.baseVertex);
                    }
                    else
                    {
                        commands_->Draw(command.vertexCount, command.firstVertex);
                    }
                    break;
                }

                case FrameCommand::Kind::Sprite:
                {
                    if (command.texture == nullptr || command.sampler == nullptr ||
                        command.pipeline == nullptr || spriteVertexBuffer_ == nullptr)
                    {
                        break;
                    }

                    commands_->SetViewport(LLGL::Viewport{command.viewport[0], command.viewport[1],
                                                          command.viewport[2], command.viewport[3],
                                                          command.viewportMinDepth, command.viewportMaxDepth});
                    commands_->SetPipelineState(*command.pipeline);
                    if (command.usesBlendFactor)
                        commands_->SetBlendFactor(command.blendFactor);
                    if (command.scissorEnabled)
                    {
                        commands_->SetScissor(LLGL::Scissor{command.scissor[0], command.scissor[1],
                                                            command.scissor[2], command.scissor[3]});
                    }
                    if (command.hasCustomEffectUniform)
                        commands_->SetResource(0, *customEffectUniformBuffers_[command.customEffectUniformIndex]);
                    else
                    {
                        commands_->SetResource(0, command.projectionBuffer != nullptr
                            ? *command.projectionBuffer : *spriteProjectionBuffer_);
                    }
                    commands_->SetResource(1, *command.texture);
                    commands_->SetResource(2, *command.sampler);
                    commands_->SetVertexBuffer(*spriteVertexBuffer_);
                    commands_->Draw(command.vertexCount, command.firstVertex);
                    break;
                }

                case FrameCommand::Kind::QueryBegin:
                {
                    if (command.queryHeap != nullptr)
                        commands_->BeginQuery(*command.queryHeap);
                    break;
                }

                case FrameCommand::Kind::QueryEnd:
                {
                    if (command.queryHeap != nullptr)
                        commands_->EndQuery(*command.queryHeap);
                    break;
                }
            }
        }
    }

    void LlglRenderer::RecordAndSubmitFrame()
    {
        const std::vector<FrameCommandBucket> buckets = GroupFrameCommandsByTargetEXT();

        commands_->Begin();
        for (const FrameCommandBucket& bucket : buckets)
        {
            LLGL::RenderTarget& renderTarget = bucket.target != nullptr
                ? *bucket.target
                : static_cast<LLGL::RenderTarget&>(*swapChain_);
            const LLGL::Extent2D resolution = renderTarget.GetResolution();
            // LLGL-45: real PreserveContents -- every segment reloads this target's own actual
            // prior content (see AcquireLoadRenderPassEXT's own doc comment for why this is safe
            // even for a first-ever segment).
            LLGL::RenderPass* loadPass = AcquireLoadRenderPassEXT(
                renderTarget.GetNumColorAttachments(),
                renderTarget.HasDepthAttachment() || renderTarget.HasStencilAttachment(),
                renderTarget.GetSamples());

            // LLGL 0.04b's deferred OpenGL command buffer unconditionally feeds this pointer to
            // memcpy even when numClearValues is zero. Keep the zero-count contract but provide a
            // valid address so UBSan does not flag the dependency's zero-byte copy.
            const LLGL::ClearValue unusedClearValue{};
            commands_->BeginRenderPass(renderTarget, loadPass, 0, &unusedClearValue);
            commands_->SetViewport(LLGL::Viewport{0.0f, 0.0f,
                                                  static_cast<float>(resolution.width),
                                                  static_cast<float>(resolution.height)});
            ReplayFrameCommandsList(bucket.commands);
            commands_->EndRenderPass();
            // LLGL-26 mip-mapped render target follow-up: regenerate levels 1.. from the level 0
            // this pass just rendered (or MSAA-resolved) into, the LLGL equivalent of the Vulkan
            // renderer's own vkCmdBlitImage cascade. Every command in one bucket shares the same
            // target, so any one of them carries the same answer; GenerateMips() must run OUTSIDE
            // the render pass it draws from (a transfer/compute-stage command, not a draw one).
            if (LLGL::Texture* mipTexture = FindMipRegenColorTextureEXT(bucket))
                commands_->GenerateMips(*mipTexture);
        }
        commands_->End();
        queue_->Submit(*commands_);
    }

    void LlglRenderer::Present()
    {
        if (swapChain_ == nullptr)
            return;

        UpdateSwapChainResolution();

        // A frame that ReadBackbuffer() already recorded and submitted is not recorded a second
        // time: re-entering the render pass would begin from an undefined attachment and could
        // present something other than what was read back.
        if (!frameCommands_.empty() || !frameSubmitted_)
        {
            UploadFrameResources();
            RecordAndSubmitFrame();
        }

        swapChain_->Present();
        ReleasePendingBuffers();

        frameCommands_.clear();
        spriteVertexData_.clear();
        transformData_.clear();
        customEffectUniformData_.clear();
        envMapUniformData_.clear();
        skinnedUniformData_.clear();
        skinnedBoneData_.clear();
        pbrUniformData_.clear();
        frameSubmitted_ = false;
        backbufferCacheValid_ = false;
    }

    void LlglRenderer::CaptureBackbuffer()
    {
        // SDL can deliver the final drawable extent after ApplyChanges() but before the next
        // Present(). Readback is itself a frame boundary, so synchronize here as well; otherwise
        // the first capture after a grow can expose the previous-sized swap-chain image.
        UpdateSwapChainResolution();
        const LLGL::Extent2D resolution = swapChain_->GetResolution();
        if (resolution.width == 0 || resolution.height == 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: the swap chain has no pixels to read");

        LLGL::TextureDescriptor stagingDesc;
        stagingDesc.type = LLGL::TextureType::Texture2D;
        // CopyDst is what CopyTextureFromFramebuffer writes through and CopySrc is what
        // ReadTexture reads through; without them LLGL rejects the copy and the caller would be
        // handed this texture's zero-initialised contents as if they were the frame.
        stagingDesc.bindFlags = LLGL::BindFlags::CopyDst | LLGL::BindFlags::CopySrc;
        // The staging texture takes the swap chain's OWN colour format. The copy is a raw image
        // transfer with no channel reordering, so a swap chain that presents B8G8R8A8 (which is
        // what the Vulkan module selects here) would hand back byte-swapped pixels through an
        // RGBA8 staging texture. Matching the format makes ReadTexture's conversion to
        // ImageFormat::RGBA the single place where channel order is resolved.
        stagingDesc.format = swapChain_->GetColorFormat();
        stagingDesc.extent = {resolution.width, resolution.height, 1};
        stagingDesc.mipLevels = 1;
        stagingDesc.miscFlags = 0;

        LLGL::Texture* staging = renderer_->CreateTexture(stagingDesc);
        if (staging == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: readback texture creation failed");

        // LLGL allows the framebuffer copy only inside a render pass, and the caller expects to
        // read what THIS frame drew -- so the pending frame is recorded and the copy appended to
        // the swap chain's own pass, instead of reading whatever the previous Present() left
        // behind. Any RenderTarget2D passes this frame also queued are recorded alongside it (see
        // GroupFrameCommandsByTargetEXT), each in its own pass.
        UploadFrameResources();

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = 0;
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {resolution.width, resolution.height, 1};

        const std::vector<FrameCommandBucket> buckets = GroupFrameCommandsByTargetEXT();

        // LLGL-45: the frame's true order can now touch the swap chain more than once (e.g. draw
        // to the back buffer, then to an off-screen target, then to the back buffer again) --
        // the framebuffer copy must read whichever state is CURRENT as of the end of everything
        // queued so far, i.e. the LAST swap-chain segment, not just any one of them.
        std::size_t lastSwapChainSegment = buckets.size();
        for (std::size_t i = 0; i < buckets.size(); ++i)
        {
            if (buckets[i].target == nullptr)
                lastSwapChainSegment = i;
        }

        commands_->Begin();
        for (std::size_t i = 0; i < buckets.size(); ++i)
        {
            const FrameCommandBucket& bucket = buckets[i];
            LLGL::RenderTarget& renderTarget = bucket.target != nullptr
                ? *bucket.target
                : static_cast<LLGL::RenderTarget&>(*swapChain_);
            const LLGL::Extent2D bucketResolution = renderTarget.GetResolution();
            // See RecordAndSubmitFrame()'s own identical call for why this runs here.
            LLGL::RenderPass* loadPass = AcquireLoadRenderPassEXT(
                renderTarget.GetNumColorAttachments(),
                renderTarget.HasDepthAttachment() || renderTarget.HasStencilAttachment(),
                renderTarget.GetSamples());

            // See RecordAndSubmitFrame() for the pinned LLGL zero-clear-value pointer boundary.
            const LLGL::ClearValue unusedClearValue{};
            commands_->BeginRenderPass(renderTarget, loadPass, 0, &unusedClearValue);
            commands_->SetViewport(LLGL::Viewport{0.0f, 0.0f,
                                                  static_cast<float>(bucketResolution.width),
                                                  static_cast<float>(bucketResolution.height)});
            ReplayFrameCommandsList(bucket.commands);

            if (i == lastSwapChainSegment)
            {
                // LLGL-51: a queued Primitives draw with a smaller-than-target viewport also
                // carries an "effective scissor" matching that viewport (see
                // ComputeEffectiveScissor()'s own doc comment -- this is intentional, XNA-style
                // sub-viewport clipping) and the LAST such draw replayed above leaves that
                // rectangle set as GL_SCISSOR_BOX. LLGL's own CopyTextureFromFramebuffer ends in
                // a glBlitFramebuffer call, which -- unlike the raw framebuffer-to-texture copy
                // that precedes it -- IS clipped by an enabled scissor test: confirmed by live
                // instrumentation of the vendored LLGL OpenGL module, a leftover scissor rect
                // from the frame's last sub-viewport draw silently confined the whole backbuffer
                // readback to that draw's own small rectangle, reading zero-initialised (black)
                // staging-texture memory everywhere else. Re-issuing a full-resolution scissor
                // immediately before the copy does not need to disable the test itself (there is
                // no public LLGL API for that outside a pipeline bind) -- widening the box to
                // cover the whole destination has the same effect, since nothing inside it is
                // ever clipped.
                commands_->SetScissor(LLGL::Scissor{0, 0,
                                                     static_cast<int>(bucketResolution.width),
                                                     static_cast<int>(bucketResolution.height)});
                commands_->CopyTextureFromFramebuffer(*staging, region, LLGL::Offset2D{0, 0});
            }

            commands_->EndRenderPass();
            // See RecordAndSubmitFrame()'s own identical call for why this runs here.
            if (LLGL::Texture* mipTexture = FindMipRegenColorTextureEXT(bucket))
                commands_->GenerateMips(*mipTexture);
        }
        commands_->End();
        queue_->Submit(*commands_);
        queue_->WaitIdle();
        ReleasePendingBuffers();

        frameCommands_.clear();
        spriteVertexData_.clear();
        transformData_.clear();
        customEffectUniformData_.clear();
        envMapUniformData_.clear();
        skinnedUniformData_.clear();
        skinnedBoneData_.clear();
        pbrUniformData_.clear();
        frameSubmitted_ = true;

        backbufferCache_.assign(
            static_cast<std::size_t>(resolution.width) * static_cast<std::size_t>(resolution.height) * 4u, 0);

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = backbufferCache_.data();
        imageView.dataSize = backbufferCache_.size();

        renderer_->ReadTexture(*staging, region, imageView);
        renderer_->Release(*staging);

        backbufferCacheWidth_ = static_cast<int>(resolution.width);
        backbufferCacheHeight_ = static_cast<int>(resolution.height);
        backbufferCacheValid_ = true;
    }

    void LlglRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: invalid ReadBackbuffer region");
        if (swapChain_ == nullptr)
            throw std::runtime_error(std::string(kRendererName) + " renderer: no swap chain to read back");

        // The whole back buffer is captured once and every region of the same frame is served from
        // that capture. Re-entering the swap chain's render pass for a second copy is not an
        // option: its colour attachment is loaded with Undefined, so the second pass would begin
        // from discarded content and hand back an image the frame never contained.
        if (!backbufferCacheValid_)
            CaptureBackbuffer();

        // The caller's region is in logical (virtual-resolution) coordinates, which coincide with
        // window pixels only when the presentation is 1:1. Under any letterbox or scale, one
        // logical pixel covers a block of window pixels.
        const PresentationRect rect = ComputePresentationRect();
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (rect.logicalWidth > 0.0f && rect.logicalHeight > 0.0f && rect.width > 0.0f && rect.height > 0.0f)
        {
            offsetX = rect.x;
            offsetY = rect.y;
            scaleX = rect.width / rect.logicalWidth;
            scaleY = rect.height / rect.logicalHeight;
        }

        const bool oneToOne = (offsetX == 0.0f && offsetY == 0.0f && scaleX == 1.0f && scaleY == 1.0f);

        // A scaled logical pixel is resolved to the window pixel at the CENTRE of the block it
        // covers -- nearest-neighbour, deliberately not an average. Every value handed back is
        // then a colour the frame genuinely contained at a known position; averaging would invent
        // colours that were never rendered, which is precisely what a pixel test must not be given.
        const auto sampleWindowX = [&](int logicalX) {
            const float centre = offsetX + (static_cast<float>(logicalX) + 0.5f) * scaleX;
            return std::clamp(static_cast<int>(centre), 0, backbufferCacheWidth_ - 1);
        };
        const auto sampleWindowY = [&](int logicalY) {
            const float centre = offsetY + (static_cast<float>(logicalY) + 0.5f) * scaleY;
            return std::clamp(static_cast<int>(centre), 0, backbufferCacheHeight_ - 1);
        };

        if (backbufferCacheWidth_ <= 0 || backbufferCacheHeight_ <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: the captured back buffer is empty");

        if (oneToOne)
        {
            if (x < 0 || y < 0 || x + w > backbufferCacheWidth_ || y + h > backbufferCacheHeight_)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: ReadBackbuffer region lies outside the back buffer");
            }

            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(backbufferCacheWidth_) +
                     static_cast<std::size_t>(x)) * 4u;
                std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u,
                            backbufferCache_.data() + sourceOffset,
                            static_cast<std::size_t>(w) * 4u);
            }
            return;
        }

        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = sampleWindowY(y + row);
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = sampleWindowX(x + column);
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceRow) * static_cast<std::size_t>(backbufferCacheWidth_) +
                     static_cast<std::size_t>(sourceColumn)) * 4u;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(column)) * 4u;
                std::memcpy(pixels + destinationOffset, backbufferCache_.data() + sourceOffset, 4u);
            }
        }
    }

    std::unique_ptr<IRenderTargetRenderer> LlglRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap,
        int multiSampleCount)
    {
        if (w <= 0 || h <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: render target has no pixels");

        // Requested sample count, clamped only to "at least 1" here -- the REAL, device-clamped
        // value is read back from the created LLGL::RenderTarget itself below (GetSamples()),
        // matching GetMultiSampleCount()'s own swap-chain precedent.
        const std::uint32_t requestedSamples =
            multiSampleCount > 1 ? static_cast<std::uint32_t>(multiSampleCount) : 1u;

        // Same formula as RenderTarget2D.cpp's own CalculateMipLevels (and the Vulkan renderer's
        // identical CalculateVulkanRTMipLevels) -- must match exactly, since the XNA-level
        // RenderTarget2D.LevelCount the game sees is computed independently, client-side, and
        // GetData(level) below rejects anything this renderer did not actually allocate.
        int mipLevels = 1;
        if (mipMap)
        {
            int mw = w, mh = h;
            mipLevels = 1;
            while (mw > 1 || mh > 1)
            {
                mw = std::max(1, mw / 2);
                mh = std::max(1, mh / 2);
                ++mipLevels;
            }
        }

        LLGL::TextureDescriptor colorDesc;
        colorDesc.type = LLGL::TextureType::Texture2D;
        // Sampled so it can be drawn with SpriteBatch/the 3D path afterwards, CopySrc for
        // GetData(), ColorAttachment so it can be bound as a render target at all. This texture is
        // always single-sample: when MSAA is requested it becomes the RESOLVE target below, never
        // the multisampled attachment itself (a multisampled Texture2D cannot be sampled by this
        // renderer's plain sampler2D shaders).
        colorDesc.bindFlags = LLGL::BindFlags::ColorAttachment | LLGL::BindFlags::Sampled |
                              LLGL::BindFlags::CopySrc;
        // Matches the swap chain's own colour format -- see this method's header doc comment on
        // why every render target shares the back buffer's attachment signature.
        colorDesc.format = swapChain_->GetColorFormat();
        colorDesc.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
        // A real mip chain when mipMap is requested (LLGL-26 mip-mapped render target follow-up):
        // the render-target ATTACHMENT below still only ever binds level 0 (AttachmentDescriptor's
        // own mipLevel default), and RecordAndSubmitFrame()/CaptureBackbuffer() regenerate levels
        // 1.. from it via LLGL::CommandBuffer::GenerateMips() after every render pass this target
        // appears in -- see this method's own header doc comment.
        colorDesc.mipLevels = static_cast<std::uint32_t>(mipLevels);
        colorDesc.miscFlags = 0;

        LLGL::Texture* colorTexture = renderer_->CreateTexture(colorDesc);
        if (colorTexture == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: render target colour texture creation failed");
        }

        LLGL::RenderTargetDescriptor targetDesc;
        targetDesc.resolution = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
        targetDesc.samples = requestedSamples;
        if (requestedSamples > 1)
        {
            // Anonymous (textureless) multisampled colour attachment -- LLGL allocates and owns
            // this internally at RenderTargetDescriptor::samples, exactly like the anonymous
            // depth/stencil attachment just below already does. colorTexture (single-sample) is
            // the RESOLVE target instead: LLGL resolves the multisampled draw into it automatically
            // at the end of each render pass this renderer replays, so GetData()/sampling afterwards
            // read fully-resolved pixels without this renderer doing anything else.
            targetDesc.colorAttachments[0] = LLGL::AttachmentDescriptor{colorDesc.format};
            targetDesc.resolveAttachments[0] = LLGL::AttachmentDescriptor{colorTexture};
        }
        else
        {
            targetDesc.colorAttachments[0] = LLGL::AttachmentDescriptor{colorTexture};
        }
        // Anonymous (textureless) attachment: LLGL allocates and owns this buffer as part of the
        // RenderTarget itself, always at the swap chain's own depth/stencil format, regardless of
        // the requested DepthFormat -- see this method's header doc comment. Implicitly created at
        // targetDesc.samples too, matching the colour attachment's own sample count (a render
        // target's attachments must all share one sample count).
        targetDesc.depthStencilAttachment = LLGL::AttachmentDescriptor{swapChain_->GetDepthStencilFormat()};

        LLGL::RenderTarget* renderTarget = renderer_->CreateRenderTarget(targetDesc);
        if (renderTarget == nullptr)
        {
            renderer_->Release(*colorTexture);
            throw std::runtime_error(std::string(kRendererName) + " renderer: render target creation failed");
        }
        // The REAL, device-clamped sample count -- LLGL "silently reduces" an unsupported request
        // (RenderTargetDescriptor::samples' own documented behaviour) rather than failing, exactly
        // like the swap chain's own GetSamples() already reports for the back buffer.
        const int appliedSamples = static_cast<int>(renderTarget->GetSamples());

        // This target's own fixed pixel-to-clip-space projection -- identical in shape to
        // UploadFrameResources()'s swap-chain one, but computed once here (a render target's
        // resolution is immutable after construction, unlike the swap chain's, which can resize)
        // rather than re-uploaded into a shared buffer every frame. Without this, every sprite
        // queued while this target is bound would be transformed by the SWAP CHAIN's projection --
        // e.g. a 64x64 target's pixel coordinates read through an 800x480 projection collapse into
        // a tiny corner of the target's clip space instead of filling it (found by reading back
        // real pixels: the drawn quad was entirely missing from the sampled region).
        const float projection[16] = {
            2.0f / static_cast<float>(w), 0.0f,                            0.0f, 0.0f,
            0.0f,                         -2.0f / static_cast<float>(h),   0.0f, 0.0f,
            0.0f,                         0.0f,                            1.0f, 0.0f,
            -1.0f,                        1.0f,                            0.0f, 1.0f
        };
        LLGL::BufferDescriptor projectionDesc;
        projectionDesc.size = sizeof(projection);
        projectionDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
        LLGL::Buffer* spriteProjectionBuffer = renderer_->CreateBuffer(projectionDesc, projection);
        if (spriteProjectionBuffer == nullptr)
        {
            renderer_->Release(*renderTarget);
            renderer_->Release(*colorTexture);
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: render target projection buffer creation failed");
        }

        // DepthFormat::None is ordinal 0; anything else asks for a real depth (and possibly
        // stencil) buffer. The physical buffer is always allocated above -- this only changes what
        // HasRealDepthBuffer() reports to the shared RenderTarget2D layer.
        const bool hasRealDepthBuffer = depthFormat != 0;

        return std::make_unique<LlglRenderTargetRenderer>(renderer_.get(), renderTarget, colorTexture,
                                                          nullptr, w, h, hasRealDepthBuffer,
                                                          spriteProjectionBuffer, this, appliedSamples,
                                                          mipLevels);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> LlglRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool /*preserveContents*/, bool mipMap,
        int multiSampleCount)
    {
        if (size <= 0)
            throw std::runtime_error(std::string(kRendererName) + " renderer: render target cube has no pixels");

        throw System::NotSupportedException(
            std::string(kRendererName) +
            " renderer: RenderTargetCube is not supported by the validated OpenGL render-system path");

        // Requested sample count (LLGL-34, mirrors CreateRenderTarget2D's own LLGL-26 MSAA
        // follow-up) -- the REAL, device-clamped value is read back from face 0's own created
        // LLGL::RenderTarget below (GetSamples()), not assumed from this request.
        const std::uint32_t requestedSamples =
            multiSampleCount > 1 ? static_cast<std::uint32_t>(multiSampleCount) : 1u;

        // Same formula as CreateRenderTarget2D's own LLGL-26 mip-mapped-render-target follow-up
        // (RenderTarget2D.cpp's own CalculateMipLevels, cube faces mirrored identically by
        // RenderTargetCube.cpp's own CalculateMipLevels(size) since a cube face is always square)
        // -- must match exactly, since the XNA-level RenderTargetCube.LevelCount the game sees is
        // computed independently, client-side, and GetData(level) below rejects anything this
        // renderer did not actually allocate.
        int mipLevels = 1;
        if (mipMap)
        {
            int m = size;
            mipLevels = 1;
            while (m > 1)
            {
                m = std::max(1, m / 2);
                ++mipLevels;
            }
        }

        LLGL::TextureDescriptor colorDesc;
        colorDesc.type = LLGL::TextureType::TextureCube;
        colorDesc.bindFlags = LLGL::BindFlags::ColorAttachment | LLGL::BindFlags::Sampled |
                              LLGL::BindFlags::CopySrc;
        // Matches the swap chain's own colour format, exactly like CreateRenderTarget2D -- every
        // cached sprite/primitive pipeline needs a matching attachment signature to be reusable
        // across render passes. Always single-sample: when MSAA is requested this becomes the
        // RESOLVE target for each face's own anonymous multisampled colour attachment below,
        // exactly like CreateRenderTarget2D's own colorTexture never being the multisampled
        // attachment itself.
        colorDesc.format = swapChain_->GetColorFormat();
        colorDesc.extent = {static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), 1};
        colorDesc.arrayLayers = 6;
        // A real mip chain when mipMap is requested (LLGL-35, mirrors CreateRenderTarget2D's own
        // LLGL-26 mip-mapped render target follow-up): each face's own render-target ATTACHMENT
        // below still only ever binds level 0 (AttachmentDescriptor's own mipLevel default), and
        // RecordAndSubmitFrame()/CaptureBackbuffer() regenerate levels 1.. from it via
        // LLGL::CommandBuffer::GenerateMips() after every render pass any face of this cube
        // appears in -- see LlglRenderTargetCubeFaceBinding's own GetMipRegenColorTextureEXT()
        // override for why this happens once PER FACE bound in a frame rather than once per cube.
        colorDesc.mipLevels = static_cast<std::uint32_t>(mipLevels);
        colorDesc.miscFlags = 0;

        LLGL::Texture* cubeTexture = renderer_->CreateTexture(colorDesc);
        if (cubeTexture == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: render target cube colour texture creation failed");
        }

        // ONE shared depth/stencil texture for the whole cube -- matches FNA's own RenderTargetCube,
        // which allocates exactly one depth/stencil buffer regardless of face count, mirrored by the
        // Vulkan renderer's own precedent (VulkanRenderTargetCubeRenderer's own depthImage_, "promoted
        // to MSAA samples when this cube engages MSAA"). Unlike CreateRenderTarget2D's anonymous
        // (textureless) attachment, this needs to be a real, explicitly-owned texture so all 6 face
        // AttachmentDescriptors can reference the SAME one -- including under MSAA (LLGL-34): a
        // real explicit multisampled texture needs LLGL::TextureType::Texture2DMS rather than
        // Texture2D (TextureDescriptor::samples "is only used for multi-sampled textures", i.e.
        // Texture2DMS/Texture2DMSArray -- confirmed by reading LLGL's own TextureFlags.h), so the
        // type switches along with requestedSamples; every attachment referencing this texture
        // below must (and does) share its exact sample count, per LLGL's own RenderTargetFlags.h
        // ("this texture must have the same number of samples as specified by
        // RenderTargetDescriptor::samples"). Depth content is not preserved/shared ACROSS separate
        // binds of different faces any more than it ever was (each face's own render pass is the
        // only thing that ever reads it back), so promoting this one texture to MSAA changes memory
        // footprint only, not any game-visible behaviour.
        LLGL::TextureDescriptor depthDesc;
        depthDesc.type = requestedSamples > 1 ? LLGL::TextureType::Texture2DMS : LLGL::TextureType::Texture2D;
        depthDesc.bindFlags = LLGL::BindFlags::DepthStencilAttachment;
        depthDesc.format = swapChain_->GetDepthStencilFormat();
        depthDesc.extent = {static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), 1};
        // Texture2DMS/Texture2DMSArray require mipLevels to be 0 or 1 (TextureDescriptor's own doc
        // comment) -- 1 already satisfies both this and the single-sample path's own requirement.
        depthDesc.mipLevels = 1;
        depthDesc.samples = requestedSamples;
        depthDesc.miscFlags = 0;

        LLGL::Texture* depthTexture = renderer_->CreateTexture(depthDesc);
        if (depthTexture == nullptr)
        {
            renderer_->Release(*cubeTexture);
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: render target cube depth texture creation failed");
        }

        std::array<LLGL::RenderTarget*, 6> faceTargets{};
        for (int face = 0; face < 6; ++face)
        {
            LLGL::RenderTargetDescriptor targetDesc;
            targetDesc.resolution = {static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size)};
            targetDesc.samples = requestedSamples;
            // The project's own face order (0=+X..5=-Z) already matches the array-layer convention
            // LLGL documents for cube textures -- no remapping needed, same as LlglTextureCubeRenderer.
            if (requestedSamples > 1)
            {
                // Anonymous (textureless) multisampled colour attachment, one per face -- LLGL
                // allocates and owns each face's own transient MSAA storage internally (never
                // shared across faces, unlike the depth texture above: each face is a SEPARATE
                // LLGL::RenderTarget, and colour is resolved into the persistent cubeTexture at the
                // end of every render pass anyway, so per-face transient storage costs nothing
                // extra worth avoiding). Mirrors CreateRenderTarget2D's own anonymous MSAA colour
                // attachment exactly.
                targetDesc.colorAttachments[0] = LLGL::AttachmentDescriptor{colorDesc.format};
                targetDesc.resolveAttachments[0] =
                    LLGL::AttachmentDescriptor{cubeTexture, 0, static_cast<std::uint32_t>(face)};
            }
            else
            {
                targetDesc.colorAttachments[0] =
                    LLGL::AttachmentDescriptor{cubeTexture, 0, static_cast<std::uint32_t>(face)};
            }
            targetDesc.depthStencilAttachment = LLGL::AttachmentDescriptor{depthTexture};

            LLGL::RenderTarget* faceTarget = renderer_->CreateRenderTarget(targetDesc);
            if (faceTarget == nullptr)
            {
                for (int cleanupFace = 0; cleanupFace < face; ++cleanupFace)
                    renderer_->Release(*faceTargets[static_cast<std::size_t>(cleanupFace)]);
                renderer_->Release(*depthTexture);
                renderer_->Release(*cubeTexture);
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: render target cube face " +
                    std::to_string(face) + " creation failed");
            }
            faceTargets[static_cast<std::size_t>(face)] = faceTarget;
        }

        // The REAL, device-clamped sample count -- every face was created from the same targetDesc
        // request, so face 0's own applied value is correct for all 6 (matches CreateRenderTarget2D's
        // own GetSamples() readback precedent).
        const int appliedSamples =
            static_cast<int>(faceTargets[0]->GetSamples());

        // Shared by all 6 faces: a cube face is always square, and a face's resolution never
        // changes after construction, so one projection buffer serves every face -- same reasoning
        // as CreateRenderTarget2D's own single (per-target) projection buffer.
        const float projection[16] = {
            2.0f / static_cast<float>(size), 0.0f,                              0.0f, 0.0f,
            0.0f,                            -2.0f / static_cast<float>(size),  0.0f, 0.0f,
            0.0f,                            0.0f,                              1.0f, 0.0f,
            -1.0f,                           1.0f,                              0.0f, 1.0f
        };
        LLGL::BufferDescriptor projectionDesc;
        projectionDesc.size = sizeof(projection);
        projectionDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
        LLGL::Buffer* spriteProjectionBuffer = renderer_->CreateBuffer(projectionDesc, projection);
        if (spriteProjectionBuffer == nullptr)
        {
            for (LLGL::RenderTarget* faceTarget : faceTargets)
                renderer_->Release(*faceTarget);
            renderer_->Release(*depthTexture);
            renderer_->Release(*cubeTexture);
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: render target cube projection buffer creation failed");
        }

        const bool hasRealDepthBuffer = depthFormat != 0;

        return std::make_unique<LlglRenderTargetCubeRenderer>(
            renderer_.get(), cubeTexture, depthTexture, faceTargets, size, hasRealDepthBuffer,
            spriteProjectionBuffer, this, appliedSamples, mipLevels);
    }

    void LlglRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        ReleaseCurrentMrtBindingEXT();

        if (rt == nullptr)
        {
            currentRenderTargetRenderer_ = nullptr;
            return;
        }

        auto* target = dynamic_cast<LlglRenderTargetRenderer*>(rt);
        if (target == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SetRenderTarget2D was given a target from another renderer");
        }

        currentRenderTargetRenderer_ = target;
    }

    void LlglRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }

        if (count > 1)
        {
            SetMultipleRenderTargetsEXT(renderTargets, count);
            return;
        }

        if (renderTargets[0].IsRenderTarget2D())
        {
            SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }

        ReleaseCurrentMrtBindingEXT();

        auto* cubeRenderer = dynamic_cast<LlglRenderTargetCubeRenderer*>(renderTargets[0].GetRenderTargetCube());
        if (cubeRenderer == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: SetRenderTargets was given a RenderTargetCube from another renderer");
        }
        const int face = renderTargets[0].GetCubeFace();
        if (face < 0 || face >= 6)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: RenderTargetCube face " + std::to_string(face) +
                " is out of range");
        }
        currentRenderTargetRenderer_ = cubeRenderer->GetFaceBinding(face);
    }

    void LlglRenderer::ReleaseCurrentMrtBindingEXT()
    {
        if (currentMrtBinding_ == nullptr)
            return;

        // colorTexture/depthTexture are both null here: the N colour attachments are borrowed from
        // the bound RenderTarget2D renderers (still owned and released by them, see LlglMRTBinding's
        // own doc comment) and the depth/stencil attachment is anonymous, owned internally by
        // renderTarget_ itself -- only the combined LLGL::RenderTarget is this binding's own to
        // release, exactly like CreateRenderTarget2D's own anonymous-depth case.
        ScheduleRenderTargetReleaseEXT(currentMrtBinding_->GetLlglRenderTarget(), nullptr, nullptr, nullptr);
        if (currentRenderTargetRenderer_ == currentMrtBinding_.get())
            currentRenderTargetRenderer_ = nullptr;
        currentMrtBinding_.reset();
    }

    void LlglRenderer::SetMultipleRenderTargetsEXT(const RenderTargetBindingDescriptor* renderTargets,
                                                           int count)
    {
        // XNA's own MAX_RENDERTARGET_BINDINGS; RenderTargetDescriptor::colorAttachments[] itself
        // holds LLGL_MAX_NUM_COLOR_ATTACHMENTS (8) slots, comfortably more.
        constexpr int kMaxColorAttachments = 4;
        if (count < 2 || count > kMaxColorAttachments)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: multiple render targets need 2-4 slots, got " +
                std::to_string(count));
        }

        std::array<LlglRenderTargetRenderer*, kMaxColorAttachments> slots{};
        for (int i = 0; i < count; ++i)
        {
            // Scoped to RenderTarget2D slots only in this first cut -- see LlglMRTBinding's own doc
            // comment for why mixing in a RenderTargetCube face is refused by name instead of
            // silently degrading.
            if (!renderTargets[i].IsRenderTarget2D())
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: multiple render targets support "
                    "RenderTarget2D slots only, not a RenderTargetCube face");
            }
            auto* target = dynamic_cast<LlglRenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D());
            if (target == nullptr)
            {
                throw std::runtime_error(
                    std::string(kRendererName) + " renderer: SetRenderTargets was given a target from "
                    "another renderer");
            }
            if (target->GetMipRegenColorTextureEXT() != nullptr)
            {
                throw System::NotSupportedException(
                    std::string(kRendererName) +
                    " renderer: mip-mapped RenderTarget2D slots are not supported in an MRT bind");
            }
            slots[static_cast<std::size_t>(i)] = target;
        }

        // Dimensions, applied sample count and duplicate-subresource checks already happened at the
        // shared GraphicsDevice::SetRenderTargets layer before this renderer was ever called -- slot
        // 0's own width/height/projection buffer/applied MultiSampleCount are correct for every
        // slot (that shared layer's own "render targets must have matching applied sample counts"
        // check already rejects a mixed-MSAA bind before this method ever runs).
        const int width = slots[0]->GetWidth();
        const int height = slots[0]->GetHeight();
        const int requestedSamples = slots[0]->GetSampleCount();

        LLGL::RenderTargetDescriptor targetDesc;
        targetDesc.resolution = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        // LLGL-54: preserve every slot's own already-applied MSAA sample count instead of silently
        // dropping it (this used to omit `targetDesc.samples` entirely, defaulting to 1/no-MSAA
        // regardless of what the individual RenderTarget2D slots were actually created with).
        // Matches CreateRenderTarget2D()'s own single-target pattern: an anonymous (textureless)
        // multisampled colour attachment per slot, LLGL-owned, with each slot's own already-created
        // single-sample texture as ITS OWN resolve target -- LLGL resolves each attachment
        // independently at the end of the render pass, so GetData()/sampling afterwards reads
        // fully-resolved pixels per slot with no extra work here.
        targetDesc.samples = requestedSamples;
        for (int i = 0; i < count; ++i)
        {
            LLGL::Texture* colorTexture = slots[static_cast<std::size_t>(i)]->GetLlglColorTexture();
            if (requestedSamples > 1)
            {
                targetDesc.colorAttachments[static_cast<std::size_t>(i)] =
                    LLGL::AttachmentDescriptor{swapChain_->GetColorFormat()};
                targetDesc.resolveAttachments[static_cast<std::size_t>(i)] =
                    LLGL::AttachmentDescriptor{colorTexture};
            }
            else
            {
                targetDesc.colorAttachments[static_cast<std::size_t>(i)] =
                    LLGL::AttachmentDescriptor{colorTexture};
            }
        }
        // Fresh, anonymous depth attachment -- LLGL allocates and owns it internally as part of
        // renderTarget_, exactly like CreateRenderTarget2D's own single-target depth attachment,
        // rather than trying to share/preserve any one slot's own depth buffer (see LlglMRTBinding's
        // doc comment on why). Implicitly created at targetDesc.samples too, matching every colour
        // attachment's own sample count -- a render target's attachments must all share one.
        targetDesc.depthStencilAttachment = LLGL::AttachmentDescriptor{swapChain_->GetDepthStencilFormat()};

        LLGL::RenderTarget* renderTarget = renderer_->CreateRenderTarget(targetDesc);
        if (renderTarget == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: multiple-render-target creation failed");
        }
        // The REAL, device-clamped sample count -- LLGL "silently reduces" an unsupported request
        // rather than failing, exactly like the single-target path's own `appliedSamples`.
        const int appliedSamples = static_cast<int>(renderTarget->GetSamples());

        ReleaseCurrentMrtBindingEXT();
        currentMrtBinding_ = std::make_unique<LlglMRTBinding>(
            renderTarget, width, height, count, slots[0]->GetSpriteProjectionBuffer(), appliedSamples);
        currentRenderTargetRenderer_ = currentMrtBinding_.get();
    }

    void LlglRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        if (UsesConstantBlendFactor(colorSrcBlend) || UsesConstantBlendFactor(alphaSrcBlend) ||
            UsesConstantBlendFactor(colorDstBlend) || UsesConstantBlendFactor(alphaDstBlend))
        {
            throw System::NotSupportedException(
                std::string(kRendererName) +
                " renderer: BlendFactor/InverseBlendFactor is not supported by the pinned "
                "OpenGL render system");
        }

        colorSrcBlend_ = colorSrcBlend;
        alphaSrcBlend_ = alphaSrcBlend;
        colorDstBlend_ = colorDstBlend;
        alphaDstBlend_ = alphaDstBlend;
        colorBlendFunc_ = colorBlendFunc;
        alphaBlendFunc_ = alphaBlendFunc;
        // Every slot's write mask is applied now (LLGL-21 follow-up): slot 0 outside an MRT bind,
        // all four while a real MRT set (LLGL-26 follow-up) is bound -- see
        // FillCurrentBlendAndRasterStateEXT's own per-slot loop.
        for (int slot = 0; slot < 4; ++slot)
            colorWriteChannels_[slot] = writeState.colorWriteChannels[slot];
        // BlendState.MultiSampleMask is real now too (LLGL-33): LLGL::BlendDescriptor::sampleMask
        // maps to a genuine per-sample coverage bitmask on the Vulkan module (VkPipelineMultisample
        // StateCreateInfo::pSampleMask, applied unconditionally, not gated behind alphaToCoverage --
        // confirmed by reading VKGraphicsPSO.cpp's own CreateMultisampleState); the OpenGL module
        // never applies it at all (LLGL's own GLBlendState::Bind has the SetSampleMask call
        // `#if 0`'d out with a `//TODO` -- confirmed by reading GLBlendState.cpp directly), a real,
        // permanent LLGL-side limitation on that module, not a CNA defect.
        multiSampleMask_ = static_cast<unsigned int>(writeState.multiSampleMask);
    }

    void LlglRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void LlglRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                 int maxAnisotropy)
    {
        // LLGL-49: any slot beyond kTrackedSamplerSlotCount has no shader binding to reach on this
        // renderer (every stock effect's own texture units fit in slots 0..6 -- see
        // samplerFilter_'s own doc comment for the exact mapping).
        if (slot < 0 || slot >= kTrackedSamplerSlotCount)
            return;

        samplerFilter_[slot] = filter;
        samplerAddressU_[slot] = addressU;
        samplerAddressV_[slot] = addressV;
        samplerMaxAnisotropy_[slot] = maxAnisotropy;
    }

    void LlglRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            throw System::NotSupportedException(
                std::string(kRendererName) +
                " renderer: RasterizerState depth bias is not supported by the validated OpenGL path");
        }

        // Cull mode, fill mode and both depth biases belong to the 3D pipeline: a sprite quad is
        // always front-facing, solid, and unaffected by depth. They are recorded so the 3D path
        // can consume them when it lands.
        cullMode_ = cullMode;
        fillMode_ = fillMode;
        scissorTestEnabled_ = scissorTestEnable;
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void LlglRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        scissorRectSet_ = true;
    }

    void LlglRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;
        minDepth_ = minDepth;
        maxDepth_ = maxDepth;

        const PresentationRect rect = GetActiveDrawRect();
        // LLGL-53: a depth range other than XNA's own [0,1] default must still narrow the GPU
        // viewport away from the full-target default even when the RECT itself is unchanged --
        // matches the rect-only check this flag used to be, now widened so `viewportSet_` means
        // "this Viewport differs from the implicit full-target one in ANY of its five fields".
        viewportSet_ = !(x == 0 && y == 0 &&
                         static_cast<float>(w) == rect.logicalWidth &&
                         static_cast<float>(h) == rect.logicalHeight &&
                         minDepth == 0.0f && maxDepth == 1.0f);
    }

    void LlglRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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
        if (stencilEnable)
        {
            throw System::NotSupportedException(
                std::string(kRendererName) +
                " renderer: stencil testing is not supported by the validated OpenGL path");
        }

        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
        // LLGL-53: the full stencil state is now applied (see AcquirePrimitivePipeline()'s
        // `pipelineDesc.stencil`) -- previously only `stencilRequested_` (enabled/disabled) was
        // recorded and the swap chain's real stencil buffer/ClearStencil worked, but no draw path
        // ever consumed an actual stencil test.
        stencilRequested_ = stencilEnable;
        stencilFunction_ = stencilFunc;
        stencilPass_ = stencilPass;
        stencilFail_ = stencilFail;
        stencilDepthFail_ = stencilDepthFail;
        stencilMask_ = stencilMask;
        stencilWriteMask_ = stencilWriteMask;
        referenceStencil_ = referenceStencil;
        twoSidedStencilMode_ = twoSidedStencilMode;
        ccwStencilFunction_ = ccwStencilFunc;
        ccwStencilPass_ = ccwStencilPass;
        ccwStencilFail_ = ccwStencilFail;
        ccwStencilDepthFail_ = ccwStencilDepthFail;
    }

    void LlglRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void LlglRenderer::SetDepthTestEnabled(bool enabled)
    {
        depthTestEnabled_ = enabled;
    }

    void LlglRenderer::SetBlendEnabled(bool enabled)
    {
        // Recorded only: the sprite pipeline derives its blend-enable bit from the blend factors
        // themselves (see AcquireSpritePipeline), which cannot disagree with the applied state.
        blendEnabled_ = enabled;
    }

    void LlglRenderer::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
    }

    void LlglRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                     const Matrix& world, const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount)
    {
        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferRenderer*>(&vb);
        if (vertexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the vertex buffer belongs to another renderer");
        }

        QueuePrimitives(*vertexBuffer, nullptr, world, view, projection, primitive, primitiveCount,
                        0, 0, 0, nullptr);
    }

    void LlglRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                            const IIndexBufferRenderer& ib,
                                                            const Matrix& world, const Matrix& view,
                                                            const Matrix& projection,
                                                            PrimitiveType primitive, int primitiveCount)
    {
        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferRenderer*>(&vb);
        const auto* indexBuffer = dynamic_cast<const LlglIndexBufferRenderer*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the vertex or index buffer belongs to another renderer");
        }

        QueuePrimitives(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount,
                        0, 0, 0, nullptr);
    }

    void LlglRenderer::RejectUnsupportedDrawParams(const GpuDrawParams& params) const
    {
        // What this path can honour: vertex colours, a diffuse colour and alpha, one or two
        // textures (DualTextureEffect), fog, the alpha test, PbrEffect, and SkinnedPbrEffect.
        // Everything else fails by name rather than quietly rendering something that merely looks
        // plausible -- an unlit surface where lighting was asked for is a wrong answer, not a
        // degraded one.
        const char* unsupported = nullptr;
        if (params.customEffectRenderer != nullptr)            unsupported = "custom ShaderEffect";
        else if (params.vertexStreamCount > 1)                unsupported = "multi-stream vertex input";
        else if (params.vertexStreamCount == 1 &&
                 params.vertexStreams[0].instanceFrequency > 0)
                                                               unsupported = "per-instance vertex input";
        else if (params.instanceCount > 1)                    unsupported = "instanced drawing";

        if (unsupported != nullptr)
            NotYetImplemented(kRendererName, unsupported);
    }

    void LlglRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        RejectUnsupportedDrawParams(params);

        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferRenderer*>(&vb);
        if (vertexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the vertex buffer belongs to another renderer");
        }

        QueuePrimitives(*vertexBuffer, nullptr, world, view, projection, primitive, primitiveCount,
                        params.vertexStart, 0, 0, &params);
    }

    void LlglRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                       const IIndexBufferRenderer& ib,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        RejectUnsupportedDrawParams(params);

        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferRenderer*>(&vb);
        const auto* indexBuffer = dynamic_cast<const LlglIndexBufferRenderer*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kRendererName) + " renderer: the vertex or index buffer belongs to another renderer");
        }

        // startIndex and baseVertex are honoured rather than dropped: the shared interface's own
        // default implementation forwards neither, which silently turns a sub-range draw into a
        // draw of the wrong range (a defect other renderers in this project have had filed against
        // them by name).
        QueuePrimitives(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount,
                        0, params.startIndex, params.baseVertex, &params);
    }

    bool LlglRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::DepthStencilBuffer:
                return swapChain_ != nullptr &&
                       swapChain_->HasDepthAttachment() && swapChain_->HasStencilAttachment();

            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                // The pinned OpenGL module's reported sample count is not backed by an
                // antialiased resolve, and the Vulkan module is compile/diagnostic coverage only
                // until its validation errors are resolved. Neither is a supported CNA MSAA path.
                return false;

            case CNA::GraphicsCapability::AnisotropicFiltering:
                return static_cast<bool>(renderer_) &&
                       renderer_->GetRenderingCaps().limits.maxAnisotropy > 1;

            // The 3D path is real: vertex and index buffers draw, with depth test, depth write,
            // cull mode, fill mode and the validated stock-effect family applied (LLGL-24/25).
            case CNA::GraphicsCapability::ThreeD:
                return true;

            case CNA::GraphicsCapability::WireFrame:
                return SupportsWireFrameEXT();

            // LLGL-28: real occlusion queries via LLGL::QueryHeap(QueryType::SamplesPassed).
            case CNA::GraphicsCapability::OcclusionQuery:
                return true;

            // LLGL-27: real ShaderEffect, scoped to SpriteBatch draws (see LlglEffectRenderer's
            // own doc comment).
            case CNA::GraphicsCapability::CustomEffects:
                return true;

            // LLGL-26: real volume texture storage via CreateTexture3D -- create/upload/readback,
            // not sampling one from a 3D shader (nothing in this renderer samples Texture3D yet).
            case CNA::GraphicsCapability::Texture3D:
                return true;

            // LLGL-26 MRT follow-up: real 2-4 slot RenderTarget2D binds drawn through SpriteBatch/a
            // custom ShaderEffect -- see LlglMRTBinding's own doc comment for the scope boundary
            // (RenderTarget2D slots only, no 3D colour-only draws while one is bound).
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return true;

            // AlphaBlend, NonPremultiplied and Additive all have runtime pixel coverage. Only the
            // separate constant BlendFactor/InverseBlendFactor route is refused above because the
            // pinned OpenGL module does not expose glBlendColor.
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
        }
        return false;
    }

    int LlglRenderer::GetMaxTextureDimension() const
    {
        if (!renderer_)
            return IGraphicsRenderer::GetMaxTextureDimension();

        const std::uint32_t maxSize = renderer_->GetRenderingCaps().limits.max2DTextureSize;
        return maxSize > 0 ? static_cast<int>(maxSize) : IGraphicsRenderer::GetMaxTextureDimension();
    }
}

#ifdef CNA_RENDERER_LLGL
namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Llgl { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Llgl::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<CNA::Internal::Renderers::Llgl::LlglRenderer>(args);
    }
}
#endif
