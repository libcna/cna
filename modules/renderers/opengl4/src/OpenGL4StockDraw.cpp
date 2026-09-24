// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0013: OpenGL4's stock-effect draw path. The programs are
// the shared GL corpus (GlStockShaderSources.hpp); program selection, uniform binding, the
// declaration-driven semantic attribute binding, multi-stream/instance stream placement and the
// negative-base-vertex fallback carry EasyGL's measured semantics over raw desktop GL.
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Internal/Graphics/StockVertexSemantics.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Common/GlStockShaderSources.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace CNA::Internal::Renderers::OpenGL4::GL4;

namespace CNA::Internal::Renderers::OpenGL4
{
    namespace
    {
        using CNA::Internal::Graphics::StockProgramInput;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        GLenum ToGLPrimitive(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
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
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        struct VertexAttribFormat
        {
            int componentCount;
            GLenum type;
            bool normalized;
        };

        // Byte4 BLENDINDICES binds through the converting float path on purpose (FX-127): the same
        // semantic is equally legal as Vector4, and the skinned programs declare a float vec4.
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:           return { 1, GL_FLOAT,         false };
            case VertexElementFormat::Vector2:          return { 2, GL_FLOAT,         false };
            case VertexElementFormat::Vector3:          return { 3, GL_FLOAT,         false };
            case VertexElementFormat::Vector4:          return { 4, GL_FLOAT,         false };
            case VertexElementFormat::Color:            return { 4, GL_UNSIGNED_BYTE, true  };
            case VertexElementFormat::Byte4:            return { 4, GL_UNSIGNED_BYTE, false };
            case VertexElementFormat::Short2:           return { 2, GL_SHORT,         false };
            case VertexElementFormat::Short4:           return { 4, GL_SHORT,         false };
            case VertexElementFormat::NormalizedShort2: return { 2, GL_SHORT,         true  };
            case VertexElementFormat::NormalizedShort4: return { 4, GL_SHORT,         true  };
            case VertexElementFormat::HalfVector2:      return { 2, GL_HALF_FLOAT,    false };
            case VertexElementFormat::HalfVector4:      return { 4, GL_HALF_FLOAT,    false };
            }
            throw System::NotSupportedException(
                "OpenGL4: the VertexDeclaration contains an unknown VertexElementFormat.");
        }

        void SetAttribute(GLuint location, const VertexAttribFormat& desc, std::size_t stride,
                          std::size_t byteOffset, GLuint divisor)
        {
            gl4_glEnableVertexAttribArray(location);
            gl4_glVertexAttribPointer(location, desc.componentCount, desc.type,
                                      desc.normalized ? GL_TRUE : GL_FALSE,
                                      static_cast<GLsizei>(stride),
                                      reinterpret_cast<const void*>(byteOffset));
            gl4_glVertexAttribDivisor(location, divisor);
        }

        const OpenGL4VertexBufferRenderer& AsBuffer(const IVertexBufferRenderer* buffer)
        {
            return *static_cast<const OpenGL4VertexBufferRenderer*>(buffer);
        }

        /// Binds @p elementCount elements of @p buffer's declaration at consecutive locations
        /// starting at @p firstLocation, into the VAO currently bound.
        void ConfigureDeclarationAttributes(const OpenGL4VertexBufferRenderer& buffer,
                                            unsigned int firstLocation, int vertexOffset,
                                            unsigned int divisor, std::size_t elementCount)
        {
            const auto& declaration = buffer.GetDeclarationElements();
            if (elementCount > declaration.size() || firstLocation + elementCount > 16)
            {
                throw System::InvalidOperationException(
                    "OpenGL4 instanced drawing requires a complete vertex declaration within the "
                    "16-attribute XNA profile limit.");
            }
            const std::size_t stride = buffer.GetStride();
            gl4_glBindBuffer(GL_ARRAY_BUFFER, buffer.VboHandle());
            for (std::size_t i = 0; i < elementCount; ++i)
            {
                const VertexElement& element = declaration[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const std::size_t byteOffset =
                    static_cast<std::size_t>(std::max(vertexOffset, 0)) * stride +
                    static_cast<std::size_t>(element.getOffsetProperty());
                SetAttribute(firstLocation + static_cast<unsigned int>(i), desc, stride, byteOffset,
                             divisor);
            }
        }

        void DisableDeclarationAttributes(unsigned int firstLocation, std::size_t elementCount)
        {
            for (std::size_t i = 0; i < elementCount; ++i)
            {
                const unsigned int location = firstLocation + static_cast<unsigned int>(i);
                gl4_glDisableVertexAttribArray(location);
                gl4_glVertexAttribDivisor(location, 0);
            }
        }

        /// REMED-GFX-201: attribute locations occupied by the per-vertex streams before @p streamIndex.
        unsigned int FirstLocationForStream(const GpuDrawParams& params, int streamIndex)
        {
            unsigned int location = 0;
            for (int i = 0; i < streamIndex; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0) continue;
                location += static_cast<unsigned int>(
                    AsBuffer(stream.buffer).GetDeclarationElements().size());
            }
            return location;
        }

        /// REMED-GFX-201: binds every per-vertex stream of a custom-effect draw into the bound VAO.
        bool ConfigureMultiStreamAttributes(const GpuDrawParams& params)
        {
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0) continue;
                const auto* buffer = static_cast<const OpenGL4VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty())
                    return false;
                if (i == 0 && stream.vertexOffset == 0)
                    continue;   // exactly what ApplyLayout() already configured
                ConfigureDeclarationAttributes(*buffer, FirstLocationForStream(params, i),
                                               stream.vertexOffset, 0,
                                               buffer->GetDeclarationElements().size());
            }
            return true;
        }

        void RestoreSingleStreamAttributes(const GpuDrawParams& params)
        {
            for (int i = params.vertexStreamCount - 1; i >= 0; --i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0) continue;
                const auto* buffer = static_cast<const OpenGL4VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty()) continue;
                if (i == 0)
                {
                    if (stream.vertexOffset != 0)
                        ConfigureDeclarationAttributes(*buffer, 0, 0, 0,
                                                       buffer->GetDeclarationElements().size());
                    continue;
                }
                DisableDeclarationAttributes(FirstLocationForStream(params, i),
                                             buffer->GetDeclarationElements().size());
            }
        }

        /// REMED-GFX-202: the stock instanced shaders read their per-instance world matrix at the
        /// fixed locations 12..15; a ShaderEffect continues straight after the per-vertex streams.
        constexpr unsigned int kStockInstanceBaseLocation = 12u;
        constexpr unsigned int kMaxAttributeLocations = 16u;

        unsigned int PerVertexLocationCount(const GpuDrawParams& params)
        {
            unsigned int total = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0) continue;
                total += static_cast<unsigned int>(
                    AsBuffer(stream.buffer).GetDeclarationElements().size());
            }
            return total;
        }

        struct InstanceStreamPlacement
        {
            unsigned int firstLocation = 0;
            std::size_t elementCount = 0;
        };

        struct InstanceStreamPlacements
        {
            std::array<InstanceStreamPlacement, kMaxVertexStreams> entries{};
            int count = 0;
        };

        /// FNA3D skips an element whose shader input does not exist, so an over-long per-instance
        /// declaration loses its tail; a stream that receives no location at all is refused.
        bool PlaceInstanceStreams(const GpuDrawParams& params, unsigned int baseLocation,
                                  InstanceStreamPlacements& placements)
        {
            placements.count = 0;
            unsigned int location = baseLocation;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0) continue;
                const auto* buffer = static_cast<const OpenGL4VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty()) return false;
                if (location >= kMaxAttributeLocations) return false;
                const std::size_t available = kMaxAttributeLocations - location;
                const std::size_t count = std::min(buffer->GetDeclarationElements().size(), available);
                placements.entries[static_cast<std::size_t>(placements.count++)] =
                    InstanceStreamPlacement{location, count};
                location += static_cast<unsigned int>(count);
            }
            return true;
        }

        bool NamesNormal(const std::vector<VertexElement>& declaration)
        {
            return CNA::Internal::Graphics::DeclarationNamesUsageEXT(declaration,
                                                                     VertexElementUsage::Normal);
        }

        /// REMED-GFX-234 / WEBGPU-158: a declaration that names no Normal cannot be a lit vertex,
        /// whatever its stride and whatever LightingEnabled says -- the draw is unlit and keeps its
        /// declared colour. A buffer with no declaration keeps the effect's answer. Only BasicEffect
        /// lighting is decided this way; the environment-map, skinned and PBR programs always read
        /// a normal. Every bound per-vertex stream is asked, because a mesh may keep its normals
        /// in a second buffer.
        bool DeclarationRulesOutLighting(const OpenGL4VertexBufferRenderer& vb,
                                         const GpuDrawParams& params)
        {
            if (!params.lightingEnabled || params.pbr || params.skinned || params.envMapping ||
                params.customEffectRenderer != nullptr || params.customEffectRequested)
                return false;
            bool declared = false;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0 || stream.buffer == nullptr) continue;
                const auto& declaration = AsBuffer(stream.buffer).GetDeclarationElements();
                if (declaration.empty()) continue;
                declared = true;
                if (NamesNormal(declaration)) return false;
            }
            if (!declared)
            {
                const auto& declaration = vb.GetDeclarationElements();
                if (declaration.empty()) return false;
                if (NamesNormal(declaration)) return false;
            }
            return true;
        }

        /// The draw parameters the stock program is chosen and bound with: @p params itself, or an
        /// unlit copy in @p scratch when the declaration rules lighting out.
        const GpuDrawParams& StockParamsFor(const OpenGL4VertexBufferRenderer& vb,
                                            const GpuDrawParams& params, GpuDrawParams& scratch)
        {
            if (!DeclarationRulesOutLighting(vb, params)) return params;
            scratch = params;
            scratch.lightingEnabled = false;
            return scratch;
        }

        // Task 1079: a ShaderEffect's own program and the World/View/Projection names every
        // original XNA sample's .fx source declares.
        void BindCustomEffectMatrices(IEffectRenderer& renderer, const Matrix& world,
                                      const Matrix& view, const Matrix& projection)
        {
            renderer.Bind();
            float worldCM[16], viewCM[16], projCM[16];
            world.ToColumnMajor(worldCM);
            view.ToColumnMajor(viewCM);
            projection.ToColumnMajor(projCM);
            renderer.SetUniformMat4("World", worldCM);
            renderer.SetUniformMat4("View", viewCM);
            renderer.SetUniformMat4("Projection", projCM);
        }

        // REMED-GFX-218 / SAMPLE-005 / SOFTWARE-130: what each stock program declares, in attribute
        // location order -- one table shared by validation and binding.
        constexpr StockProgramInput kPos{
            VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
        constexpr StockProgramInput kColor{
            VertexElementUsage::Color, 0, VertexElementFormat::Color, "aColor"};
        constexpr StockProgramInput kUv{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        constexpr StockProgramInput kUv1{
            VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};
        constexpr StockProgramInput kNormal{
            VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};
        constexpr StockProgramInput kTangent{
            VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4, "aTangent"};
        constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4, "aBoneIndices",
            VertexElementFormat::Vector4};

        constexpr StockProgramInput kColoredInputs[] = {kPos, kColor};
        constexpr StockProgramInput kTexturedInputs[] = {kPos, kUv};
        constexpr StockProgramInput kColTexturedInputs[] = {kPos, kColor, kUv};
        constexpr StockProgramInput kDualTexturedInputs[] = {kPos, kUv, kUv1};
        constexpr StockProgramInput kDualTexturedColoredInputs[] = {kPos, kColor, kUv, kUv1};
        constexpr StockProgramInput kEnvMappedInputs[] = {kPos, kNormal, kUv};
        constexpr StockProgramInput kLitInputs[] = {kPos, kNormal, kUv, kColor};
        constexpr StockProgramInput kSkinnedInputs[] = {kPos, kNormal, kUv, kWeights, kIndices,
                                                        kColor};
        constexpr StockProgramInput kPbrInputs[] = {kPos, kNormal, kTangent, kUv};
        constexpr StockProgramInput kPbrDualUvInputs[] = {kPos, kNormal, kTangent, kUv, kUv1, kColor};
        constexpr StockProgramInput kPbrSkinnedInputs[] = {kPos, kNormal, kTangent, kUv, kWeights,
                                                           kIndices};
        constexpr StockProgramInput kPbrSkinnedDualUvInputs[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1};
        constexpr StockProgramInput kPbrSkinnedDualUvColorInputs[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1, kColor};

        struct StockInputTable
        {
            const StockProgramInput* inputs;
            std::size_t count;
            const char* name;
        };

        template <std::size_t N>
        constexpr StockInputTable Table(const StockProgramInput (&inputs)[N], const char* name)
        {
            return StockInputTable{inputs, N, name};
        }
    }

    // ------------------------------------------------------------------------------------
    // Stock programs
    // ------------------------------------------------------------------------------------

    void OpenGL4StockProgram::Resolve()
    {
        const auto at = [this](const char* name) { return prog.UniformLocation(name); };
        loc_wvp = at("uWVP");
        loc_normalmat = at("uNormalMatrix");
        loc_world = at("uWorld");
        loc_diffuse = at("uDiffuseColor");
        loc_ambient = at("uAmbientColor");
        loc_lighting_enabled = at("uLightingEnabled");
        loc_l0dir = at("uLight0Dir");
        loc_l0diff = at("uLight0Diffuse");
        loc_l1dir = at("uLight1Dir");
        loc_l1diff = at("uLight1Diffuse");
        loc_l2dir = at("uLight2Dir");
        loc_l2diff = at("uLight2Diffuse");
        loc_l0spec = at("uLight0Specular");
        loc_l1spec = at("uLight1Specular");
        loc_l2spec = at("uLight2Specular");
        loc_specularcolor = at("uSpecularColor");
        loc_specularpower = at("uSpecularPower");
        loc_texture = at("uTexture");
        loc_texture2 = at("uTexture2");
        loc_shadowmap = at("uShadowMap");
        loc_lightviewproj = at("uLightViewProj");
        loc_shadows_on = at("uShadowsEnabled");
        loc_shadow_bias = at("uShadowBias");
        loc_shadow_texel = at("uShadowTexel");
        loc_shadow_pcf = at("uShadowPcfRadius");
        loc_cascade_count = at("uCascadeCount");
        loc_cascade_mats = at("uCascadeMatrices[0]");
        loc_cascade_splits = at("uCascadeSplits");
        loc_cascade_viewz = at("uCascadeViewZ");
        loc_cascade_blend = at("uCascadeBlend");
        loc_cascade_debug = at("uCascadeDebug");
        loc_punctual_kind = at("uPunctualKind");
        loc_punctual_pos = at("uPunctualPosition");
        loc_punctual_dir = at("uPunctualDirection");
        loc_punctual_diff = at("uPunctualDiffuse");
        loc_punctual_range = at("uPunctualRange");
        loc_punctual_cosin = at("uPunctualCosInner");
        loc_punctual_cosout = at("uPunctualCosOuter");
        loc_punctual_bias = at("uPunctualBias");
        loc_punctual_hasmap = at("uPunctualHasShadow");
        loc_punctual_cube = at("uPunctualCube");
        loc_punctual_map = at("uPunctualMap");
        loc_punctual_vp = at("uPunctualViewProj");
        loc_punctual_texel = at("uPunctualTexel");
        loc_ibl_enabled = at("uIblEnabled");
        loc_ibl_irradiance = at("uIblIrradiance");
        loc_ibl_specular = at("uIblSpecular");
        loc_ibl_brdf = at("uIblBrdfLut");
        loc_ibl_mipcount = at("uIblMipCount");
        loc_ibl_intensity = at("uIblIntensity");
        loc_envmap = at("uEnvMap");
        loc_envmap_amount = at("uEnvMapAmount");
        loc_envmap_spec = at("uEnvMapSpecular");
        loc_fresnel_enabled = at("uFresnelEnabled");
        loc_fresnel_factor = at("uFresnelFactor");
        loc_emissive = at("uEmissiveColor");
        loc_eyepos = at("uEyePosition");
        loc_bones = at("uBones[0]");
        loc_weightsPerVertex = at("uWeightsPerVertex");
        loc_alphatest = at("uAlphaTest");
        loc_fog_vector = at("uFogVector");
        loc_fog_color = at("uFogColor");
        loc_vertexcolor = at("uVertexColorEnabled");
        loc_pbr_normalmap = at("uNormalMap");
        loc_pbr_mr = at("uMetallicRoughnessMap");
        loc_pbr_emissivemap = at("uEmissiveMap");
        loc_pbr_occlusionmap = at("uOcclusionMap");
        loc_pbr_specularmap = at("uSpecularMap");
        loc_pbr_specularcolormap = at("uSpecularColorMap");
        loc_pbr_metallic = at("uMetallicFactor");
        loc_pbr_roughness = at("uRoughnessFactor");
        loc_pbr_dielectric_fresnel = at("uDielectricFresnel");
        loc_pbr_specular_fresnel_inputs = at("uSpecularFresnelInputs");
        loc_pbr_srgb = at("uSrgb");
        loc_pbr_normalscale = at("uNormalScale");
        loc_pbr_occlstrength = at("uOcclusionStrength");
        loc_pbr_texcoordsets = at("uTextureCoordinateSets");
        loc_pbr_occlusiontexcoordset = at("uOcclusionTextureCoordinateSet");
        loc_pbr_specular_texcoordsets = at("uSpecularTextureCoordinateSets");
        for (std::size_t row = 0; row < loc_pbr_texture_transform_rows.size(); ++row)
            loc_pbr_texture_transform_rows[row] =
                at(("uTextureTransformRows[" + std::to_string(row) + "]").c_str());
        for (std::size_t row = 0; row < loc_pbr_specular_texture_transform_rows.size(); ++row)
            loc_pbr_specular_texture_transform_rows[row] =
                at(("uSpecularTextureTransformRows[" + std::to_string(row) + "]").c_str());
        loc_rt_flip_v = at("uRtFlipV");
        loc_rt_flip_v_hi = at("uRtFlipVHi");
        loc_instanced = at("uCnaInstanced");
    }

    void OpenGL4Renderer::EnsureStockProgram(OpenGL4StockProgram& program, StockProgramShape shape,
                                             bool dualUv)
    {
        if (program.ready) return;

        namespace S = GlStockShaders;
        S::GlStockProgramSource source;
        const char* label = "colored";
        switch (shape)
        {
        case StockProgramShape::Colored:             source = S::Colored3DSource(); break;
        case StockProgramShape::Textured:            source = S::Textured3DSource(); label = "textured"; break;
        case StockProgramShape::ColoredTextured:     source = S::ColoredTextured3DSource(); label = "col+textured"; break;
        case StockProgramShape::Lit:                 source = S::Lit3DSource(); label = "lit+textured"; break;
        case StockProgramShape::LitVertexLit:        source = S::Lit3DVertexLitSource(); label = "lit+textured vertexlit"; break;
        case StockProgramShape::DualTextured:        source = S::DualTextured3DSource(); label = "dual_textured"; break;
        case StockProgramShape::DualTexturedColored: source = S::DualTexturedColored3DSource(); label = "dual_textured_colored"; break;
        case StockProgramShape::EnvMapped:           source = S::EnvMapped3DSource(); label = "env_mapped"; break;
        case StockProgramShape::Skinned:             source = S::SkinnedSource(); label = "skinned"; break;
        case StockProgramShape::SkinnedVertexLit:    source = S::SkinnedVertexLitSource(); label = "skinned vertexlit"; break;
        // Desktop core always has textureLod, so the IBL declarations take their explicit-LOD form.
        case StockProgramShape::Pbr:
            source = S::PbrSource(dualUv, true);
            label = dualUv ? "pbr_dual_uv" : "pbr";
            break;
        case StockProgramShape::PbrSkinned:
            source = S::PbrSkinnedSource(dualUv, true);
            label = dualUv ? "pbr_skinned_dual_uv" : "pbr_skinned";
            break;
        }

        const std::string vertex = AdaptGlslEs300ForDesktopCore(
            S::AdaptStockVertexShaderForOpenGL(source.vertex.c_str()));
        const std::string fragment = AdaptGlslEs300ForDesktopCore(source.fragment);
        if (!program.prog.Compile(vertex, fragment))
        {
            // A stock program that does not build is a renderer defect, not a content problem: say
            // so on the GL-error channel the test gate watches, and refuse the draw.
            const std::string message = std::string("[OpenGL4 GL Error] stock program '") + label +
                                        "' failed to build: " + program.prog.GetError();
            CNA::Logger::Error(message, CNA::LogCategory::RENDER);
            throw std::runtime_error(message);
        }
        program.Resolve();
        program.ready = true;
    }

    // REMED-GFX-218 / SOFTWARE-130: the one place that decides which stock program a draw gets --
    // from the effect state, never from the byte stride.
    OpenGL4Renderer::StockProgramShape OpenGL4Renderer::SelectStockProgramShape(
        const GpuDrawParams& params)
    {
        if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned;
        if (params.pbr) return StockProgramShape::Pbr;
        // MOD-840: a shadow-receiving draw is forced onto the per-pixel family.
        const bool receivesShadow = params.shadowsEnabled && params.shadowMap != nullptr;
        if (params.skinned)
        {
            return (params.lightingEnabled && !params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::SkinnedVertexLit
                       : StockProgramShape::Skinned;
        }
        if (params.envMapping) return StockProgramShape::EnvMapped;
        if (params.dualTexture)
        {
            return params.vertexColorEnabled ? StockProgramShape::DualTexturedColored
                                             : StockProgramShape::DualTextured;
        }
        if (params.lightingEnabled)
        {
            return (!params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::LitVertexLit
                       : StockProgramShape::Lit;
        }
        if (params.textureEnabled)
        {
            return params.vertexColorEnabled ? StockProgramShape::ColoredTextured
                                             : StockProgramShape::Textured;
        }
        return StockProgramShape::Colored;
    }

    OpenGL4StockProgram& OpenGL4Renderer::SelectProgram(std::size_t stride,
                                                        const GpuDrawParams& params)
    {
        const StockProgramShape shape = SelectStockProgramShape(params);
        OpenGL4StockProgram* program = &progColored_;
        bool dualUv = false;
        switch (shape)
        {
        case StockProgramShape::PbrSkinned:
            // GLTF-463: stride 80 is stride 76 with a colour appended; both take the dual-UV program.
            dualUv = stride == 76 || stride == 80;
            program = dualUv ? &progPbrSkinnedDualUv_ : &progPbrSkinned_;
            break;
        case StockProgramShape::Pbr:
            dualUv = stride == 60;
            program = dualUv ? &progPbrDualUv_ : &progPbr_;
            break;
        case StockProgramShape::SkinnedVertexLit:    program = &progSkinnedVertexLit_; break;
        case StockProgramShape::Skinned:             program = &progSkinned_; break;
        case StockProgramShape::EnvMapped:           program = &progEnvMapped_; break;
        case StockProgramShape::DualTexturedColored: program = &progDualTexturedColored_; break;
        case StockProgramShape::DualTextured:        program = &progDualTextured_; break;
        case StockProgramShape::Textured:            program = &progTextured_; break;
        case StockProgramShape::ColoredTextured:     program = &progColTextured_; break;
        case StockProgramShape::LitVertexLit:        program = &progLitTexturedVertexLit_; break;
        case StockProgramShape::Lit:                 program = &progLitTextured_; break;
        case StockProgramShape::Colored:             break;
        }
        EnsureStockProgram(*program, shape, dualUv);
        return *program;
    }

    namespace
    {
        StockInputTable StockInputsFor(OpenGL4Renderer::StockProgramShape shape, std::size_t stride);
    }

    void OpenGL4Renderer::RequireDeclarationFitsStockProgram(
        const std::vector<VertexElement>& declaredElements, std::size_t stride,
        const GpuDrawParams& params) const
    {
        const StockInputTable table = StockInputsFor(SelectStockProgramShape(params), stride);
        std::array<StockProgramInput, 8> activeInputs{};
        std::size_t activeCount = 0;
        for (std::size_t i = 0; i < table.count; ++i)
            if (StockEffectUsesVertexSemantic(params, table.inputs[i].usage,
                                              table.inputs[i].usageIndex))
                activeInputs[activeCount++] = table.inputs[i];
        CNA::Internal::Graphics::RequireDeclarationMatchesStockProgram(
            declaredElements, activeInputs.data(), activeCount, "OpenGL4", table.name);
    }

    bool OpenGL4Renderer::ConfigureDeclarationForStockProgram(OpenGL4VertexBufferRenderer& buffer,
                                                             std::size_t stride,
                                                             const GpuDrawParams& params)
    {
        if (buffer.GetDeclarationElements().empty()) return false;
        const StockInputTable table = StockInputsFor(SelectStockProgramShape(params), stride);

        gl4_glBindVertexArray(buffer.VaoHandle());
        for (std::size_t location = 0; location < table.count; ++location)
        {
            const auto glLocation = static_cast<GLuint>(location);
            gl4_glDisableVertexAttribArray(glLocation);
            gl4_glVertexAttribDivisor(glLocation, 0);

            const StockProgramInput& input = table.inputs[location];
            if (!StockEffectUsesVertexSemantic(params, input.usage, input.usageIndex)) continue;

            const OpenGL4VertexBufferRenderer* sourceBuffer = nullptr;
            const VertexElement* sourceElement = nullptr;
            std::size_t sourceStride = buffer.GetStride();
            std::size_t sourceBaseOffset = 0;
            const auto findInStream = [&](const OpenGL4VertexBufferRenderer& candidate,
                                          std::size_t candidateStride, int vertexOffset,
                                          const GpuVertexStreamBinding* stream) {
                const auto& declaration = candidate.GetDeclarationElements();
                for (std::size_t i = 0; i < declaration.size(); ++i)
                {
                    const VertexElement& element = declaration[i];
                    const int usageIndex = stream != nullptr
                        ? stream->EffectiveUsageIndex(i, element.getUsageIndexProperty())
                        : element.getUsageIndexProperty();
                    if (element.getVertexElementUsageProperty() == input.usage &&
                        usageIndex == input.usageIndex)
                    {
                        sourceBuffer = &candidate;
                        sourceElement = &element;
                        sourceStride = candidateStride;
                        sourceBaseOffset =
                            static_cast<std::size_t>(std::max(vertexOffset, 0)) * candidateStride;
                        return true;
                    }
                }
                return false;
            };

            for (int streamIndex = 0;
                 sourceElement == nullptr && streamIndex < params.vertexStreamCount; ++streamIndex)
            {
                const GpuVertexStreamBinding& stream =
                    params.vertexStreams[static_cast<std::size_t>(streamIndex)];
                if (stream.instanceFrequency != 0 || stream.buffer == nullptr) continue;
                const auto& candidate = AsBuffer(stream.buffer);
                const std::size_t candidateStride = stream.strideInBytes > 0
                    ? static_cast<std::size_t>(stream.strideInBytes)
                    : candidate.GetStride();
                findInStream(candidate, candidateStride, stream.vertexOffset, &stream);
            }
            if (sourceElement == nullptr)
                findInStream(buffer, buffer.GetStride(), 0, nullptr);
            if (sourceElement == nullptr) continue;

            const VertexAttribFormat desc =
                DescribeVertexElementFormat(sourceElement->getVertexElementFormatProperty());
            gl4_glBindBuffer(GL_ARRAY_BUFFER, sourceBuffer->VboHandle());
            SetAttribute(glLocation, desc, sourceStride,
                         sourceBaseOffset + static_cast<std::size_t>(sourceElement->getOffsetProperty()),
                         0);
        }
        // The VAO stays bound: the caller draws through the remapped layout, then restores it.
        return true;
    }

    void OpenGL4Renderer::RestoreDeclarationLayout(OpenGL4VertexBufferRenderer& buffer)
    {
        buffer.ApplyLayout(buffer.GetStride());
    }

    // ------------------------------------------------------------------------------------
    // Default textures
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::EnsureDefaultTexture(unsigned int& texture, const std::uint8_t rgba[4])
    {
        if (texture != 0) return;
        // Created on whichever unit is active, so that unit's binding is put back afterwards: the
        // fallbacks are made lazily in the middle of BindDrawParams, where the active unit may
        // already hold the previous fallback -- without the restore, creating the white
        // metallic-roughness fallback replaced the flat normal just bound on unit 1, and the
        // first PBR draw of a process was lit with a (1,1,1) normal.
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        // One level, declared as such: the per-slot sampler objects carry mip filters (XNA's
        // Linear/Point ordinals name a mip term), which would otherwise make a one-level texture
        // mipmap-incomplete and sample as black.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    }

    void OpenGL4Renderer::EnsureDefaultBlackCubeTexture()
    {
        if (defaultBlackCubeTexture_ != 0) return;
        static const std::uint8_t black[4] = {0, 0, 0, 255};
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &previous);
        glGenTextures(1, &defaultBlackCubeTexture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, defaultBlackCubeTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int face = 0; face < 6; ++face)
            glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA8, 1, 1,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(previous));
    }

    void OpenGL4Renderer::BindDefaultTexture(unsigned int texture, int unit, unsigned int target)
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(target, texture);
    }

    // ------------------------------------------------------------------------------------
    // Uniforms
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::BindDrawParams(OpenGL4StockProgram& p, const Matrix& world,
                                         const Matrix& view, const Matrix& projection,
                                         const GpuDrawParams& params)
    {
        static const std::uint8_t kWhite[4] = {255, 255, 255, 255};
        static const std::uint8_t kBlack[4] = {0, 0, 0, 255};
        static const std::uint8_t kFlatNormal[4] = {128, 128, 255, 255};

        const auto u1f = [](int loc, float x) { if (loc >= 0) gl4_glUniform1f(loc, x); };
        const auto u2f = [](int loc, float x, float y) { if (loc >= 0) gl4_glUniform2f(loc, x, y); };
        const auto u3f = [](int loc, float x, float y, float z) {
            if (loc >= 0) gl4_glUniform3f(loc, x, y, z);
        };
        const auto u4f = [](int loc, float x, float y, float z, float w) {
            if (loc >= 0) gl4_glUniform4f(loc, x, y, z, w);
        };
        const auto u1i = [](int loc, int x) { if (loc >= 0) gl4_glUniform1i(loc, x); };
        const auto um4 = [](int loc, const float* m) {
            if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
        };

        // REMED-GFX-147: one render-target orientation flag per sampled unit, filled beside each
        // unit's own bind so the flag and the resource it describes cannot disagree.
        float rtFlipV[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        // XNA 4.0's Direct3D 9 coordinates address pixel centres, OpenGL's pixel corners: the
        // just-under-half-pixel (63/128) displacement Wine/MonoGame use keeps D3D's top-left fill
        // convention. REMED-GFX-235: not while the destination is multisampled, where the
        // translation would remove coverage from the outer samples instead.
        int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
        GetGlViewport(viewportX, viewportY, viewportWidth, viewportHeight);
        bool multisampledDestination = false;
        if (bound_->rt2D != nullptr && bound_->rt2D->GetMultiSampleCount() > 0)
            multisampledDestination = true;
        if (bound_->cube != nullptr && bound_->cube->GetMultiSampleCount() > 0)
            multisampledDestination = true;
        for (int slot = 0; slot < bound_->mrtCount; ++slot)
        {
            const OpenGL4MrtBinding& target = bound_->mrt[static_cast<std::size_t>(slot)];
            if ((target.rt2D != nullptr && target.rt2D->GetMultiSampleCount() > 0) ||
                (target.cube != nullptr && target.cube->GetMultiSampleCount() > 0))
                multisampledDestination = true;
        }
        Matrix xnaPixelCenter = Matrix::getIdentityProperty();
        if (viewportWidth > 0 && viewportHeight > 0 && !multisampledDestination)
        {
            xnaPixelCenter = Matrix::CreateTranslation(
                xnaPixelCenterScale_ / static_cast<float>(viewportWidth),
                -xnaPixelCenterScale_ / static_cast<float>(viewportHeight), 0.0f);
        }

        const Matrix wvp = world * view * projection * xnaPixelCenter;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);
        um4(p.loc_wvp, wvpCol);
        u1f(p.loc_instanced, FirstInstanceStream(params) != nullptr ? 1.0f : 0.0f);

        // transpose(inverse(world3x3)) via cofactors, so non-uniform scale does not skew normals.
        if (p.loc_normalmat >= 0)
        {
            const float* w = params.worldColMajor;
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            const float nm[9] = {
                (e * i - f * h) * invDet, -(b * i - c * h) * invDet, (b * f - c * e) * invDet,
                -(d * i - f * g) * invDet, (a * i - c * g) * invDet, -(a * f - c * d) * invDet,
                (d * h - e * g) * invDet, -(a * h - b * g) * invDet, (a * e - b * d) * invDet,
            };
            gl4_glUniformMatrix3fv(p.loc_normalmat, 1, GL_FALSE, nm);
        }
        um4(p.loc_world, params.worldColMajor);

        u4f(p.loc_diffuse, params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2],
            params.diffuseColor[3]);
        u1f(p.loc_lighting_enabled, params.lightingEnabled ? 1.0f : 0.0f);
        u1f(p.loc_vertexcolor, params.vertexColorEnabled ? 1.0f : 0.0f);

        // BasicEffect-family lighting: the programs that declare an ambient term.
        if (p.loc_ambient >= 0)
        {
            if (params.lightingEnabled)
            {
                u3f(p.loc_ambient, params.ambientColor[0], params.ambientColor[1],
                    params.ambientColor[2]);
                u3f(p.loc_l0dir, params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
                u3f(p.loc_l0diff, params.light0Diffuse[0], params.light0Diffuse[1],
                    params.light0Diffuse[2]);
                u3f(p.loc_l1dir, params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
                u3f(p.loc_l1diff, params.light1Diffuse[0], params.light1Diffuse[1],
                    params.light1Diffuse[2]);
                u3f(p.loc_l2dir, params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
                u3f(p.loc_l2diff, params.light2Diffuse[0], params.light2Diffuse[1],
                    params.light2Diffuse[2]);
                u3f(p.loc_l0spec, params.light0Specular[0], params.light0Specular[1],
                    params.light0Specular[2]);
                u3f(p.loc_l1spec, params.light1Specular[0], params.light1Specular[1],
                    params.light1Specular[2]);
                u3f(p.loc_l2spec, params.light2Specular[0], params.light2Specular[1],
                    params.light2Specular[2]);
                u3f(p.loc_specularcolor, params.specularColor[0], params.specularColor[1],
                    params.specularColor[2]);
                u1f(p.loc_specularpower, params.specularPower);
            }
            else
            {
                // No lighting: full ambient (so the diffuse colour passes), no light contribution.
                u3f(p.loc_ambient, 1.0f, 1.0f, 1.0f);
                u3f(p.loc_l0dir, 0.0f, -1.0f, 0.0f);
                u3f(p.loc_l0diff, 0.0f, 0.0f, 0.0f);
                u3f(p.loc_l1dir, 0.0f, -1.0f, 0.0f);
                u3f(p.loc_l1diff, 0.0f, 0.0f, 0.0f);
                u3f(p.loc_l2dir, 0.0f, -1.0f, 0.0f);
                u3f(p.loc_l2diff, 0.0f, 0.0f, 0.0f);
                u3f(p.loc_l0spec, 0.0f, 0.0f, 0.0f);
                u3f(p.loc_l1spec, 0.0f, 0.0f, 0.0f);
                u3f(p.loc_l2spec, 0.0f, 0.0f, 0.0f);
            }
        }

        u3f(p.loc_emissive, params.emissiveColor[0], params.emissiveColor[1],
            params.emissiveColor[2]);

        // The environment-map and skinned programs have no ambient uniform and light always.
        if (p.loc_ambient < 0)
        {
            u3f(p.loc_l0dir, params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
            u3f(p.loc_l0diff, params.light0Diffuse[0], params.light0Diffuse[1],
                params.light0Diffuse[2]);
            u3f(p.loc_l1dir, params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
            u3f(p.loc_l1diff, params.light1Diffuse[0], params.light1Diffuse[1],
                params.light1Diffuse[2]);
            u3f(p.loc_l2dir, params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
            u3f(p.loc_l2diff, params.light2Diffuse[0], params.light2Diffuse[1],
                params.light2Diffuse[2]);
            u3f(p.loc_l0spec, params.light0Specular[0], params.light0Specular[1],
                params.light0Specular[2]);
            u3f(p.loc_l1spec, params.light1Specular[0], params.light1Specular[1],
                params.light1Specular[2]);
            u3f(p.loc_l2spec, params.light2Specular[0], params.light2Specular[1],
                params.light2Specular[2]);
            u3f(p.loc_specularcolor, params.specularColor[0], params.specularColor[1],
                params.specularColor[2]);
            u1f(p.loc_specularpower, params.specularPower);
        }

        u3f(p.loc_eyepos, params.eyePositionWorld[0], params.eyePositionWorld[1],
            params.eyePositionWorld[2]);

        if (p.loc_bones >= 0 && params.boneCount > 0)
            gl4_glUniformMatrix4fv(p.loc_bones, params.boneCount, GL_FALSE, params.boneTransforms);
        u1i(p.loc_weightsPerVertex, params.weightsPerVertex);

        u1f(p.loc_envmap_amount, params.envMapAmount);
        u3f(p.loc_envmap_spec, params.envMapSpecular[0], params.envMapSpecular[1],
            params.envMapSpecular[2]);
        u1f(p.loc_fresnel_enabled, params.fresnelEnabled ? 1.0f : 0.0f);
        u1f(p.loc_fresnel_factor, params.fresnelFactor);

        // Units 1..6 are bound before unit 0 so unit 0 is the active unit last.
        if (p.loc_envmap >= 0)
        {
            u1i(p.loc_envmap, 1);
            if (params.envMap)
                params.envMap->BindGL(1);
            else
            {
                EnsureDefaultBlackCubeTexture();
                BindDefaultTexture(defaultBlackCubeTexture_, 1, GL_TEXTURE_CUBE_MAP);
            }
        }

        if (p.loc_texture2 >= 0)
        {
            u1i(p.loc_texture2, 1);
            rtFlipV[1] = SampledRowOrderIsBottomUp(params.texture1) ? 1.0f : 0.0f;
            if (params.texture1)
                params.texture1->BindGL(1);
            else
            {
                EnsureDefaultTexture(defaultBlackTexture_, kBlack);
                BindDefaultTexture(defaultBlackTexture_, 1, GL_TEXTURE_2D);
            }
        }

        // PBR maps, each falling back to the texture whose sampled value is its "map absent"
        // constant (flat normal; white for factor/tint/unoccluded/specular identity).
        const auto bindPbrMap = [&](int loc, int unit, const ITextureRenderer* texture,
                                    unsigned int& fallback, const std::uint8_t rgba[4]) {
            if (loc < 0) return;
            u1i(loc, unit);
            rtFlipV[unit] = SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
            if (texture)
                texture->BindGL(unit);
            else
            {
                EnsureDefaultTexture(fallback, rgba);
                BindDefaultTexture(fallback, unit, GL_TEXTURE_2D);
            }
        };
        bindPbrMap(p.loc_pbr_normalmap, 1, params.pbrNormalMap, defaultFlatNormalTexture_, kFlatNormal);
        bindPbrMap(p.loc_pbr_mr, 2, params.pbrMetallicRoughnessMap, defaultWhiteTexture_, kWhite);
        bindPbrMap(p.loc_pbr_emissivemap, 3, params.pbrEmissiveMap, defaultWhiteTexture_, kWhite);
        bindPbrMap(p.loc_pbr_occlusionmap, 4, params.pbrOcclusionMap, defaultWhiteTexture_, kWhite);
        bindPbrMap(p.loc_pbr_specularmap, 5, params.pbrSpecularMap, defaultWhiteTexture_, kWhite);
        bindPbrMap(p.loc_pbr_specularcolormap, 6, params.pbrSpecularColorMap, defaultWhiteTexture_,
                   kWhite);

        u1f(p.loc_pbr_metallic, params.pbrMetallicFactor);
        u1f(p.loc_pbr_roughness, params.pbrRoughnessFactor);
        u4f(p.loc_pbr_dielectric_fresnel, params.pbrDielectricF0[0], params.pbrDielectricF0[1],
            params.pbrDielectricF0[2], params.pbrDielectricF90);
        u4f(p.loc_pbr_specular_fresnel_inputs, params.pbrDielectricF0Unclamped[0],
            params.pbrDielectricF0Unclamped[1], params.pbrDielectricF0Unclamped[2],
            params.pbrSpecularFactor);
        u1f(p.loc_pbr_normalscale, params.pbrNormalScale);
        u1f(p.loc_pbr_occlstrength, params.pbrOcclusionStrength);
        {
            const std::uint32_t mask = params.pbrTextureCoordinateSetMask;
            const auto bit = [mask](int index) {
                return (mask & (std::uint32_t{1} << index)) != 0 ? 1.0f : 0.0f;
            };
            u4f(p.loc_pbr_texcoordsets, bit(0), bit(1), bit(2), bit(3));
            u1f(p.loc_pbr_occlusiontexcoordset, bit(4));
            u2f(p.loc_pbr_specular_texcoordsets, bit(5), bit(6));
        }
        for (std::size_t row = 0; row < p.loc_pbr_texture_transform_rows.size(); ++row)
        {
            const float* values = params.pbrTextureTransformRows[row];
            u4f(p.loc_pbr_texture_transform_rows[row], values[0], values[1], values[2], values[3]);
        }
        for (std::size_t row = 0; row < p.loc_pbr_specular_texture_transform_rows.size(); ++row)
        {
            const float* values = params.pbrSpecularTextureTransformRows[row];
            u4f(p.loc_pbr_specular_texture_transform_rows[row], values[0], values[1], values[2],
                values[3]);
        }
        u4f(p.loc_pbr_srgb, params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
            params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f,
            params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f,
            params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f);

        // Unit 0.
        if (p.loc_texture >= 0)
        {
            u1i(p.loc_texture, 0);
            rtFlipV[0] = SampledRowOrderIsBottomUp(params.texture0) ? 1.0f : 0.0f;
            if (params.texture0)
                params.texture0->BindGL(0);
            // GSC-0004: a classic stock effect that samples an unbound texture reads XNA's opaque
            // black. BasicEffect with TextureEnabled=false does not sample in XNA, but both lit
            // programs multiply unit 0 in unconditionally, so it gets the white identity.
            else if (!params.pbr && params.textureEnabled)
            {
                EnsureDefaultTexture(defaultBlackTexture_, kBlack);
                BindDefaultTexture(defaultBlackTexture_, 0, GL_TEXTURE_2D);
            }
            else
            {
                EnsureDefaultTexture(defaultWhiteTexture_, kWhite);
                BindDefaultTexture(defaultWhiteTexture_, 0, GL_TEXTURE_2D);
            }
        }

        // Shadow map (MOD-835), unit 7. Every uniform is uploaded even when shadows are off, so a
        // stale light matrix can never survive into the next shadowed draw.
        if (p.loc_shadows_on >= 0)
        {
            const bool haveShadow = params.shadowsEnabled && params.shadowMap != nullptr;
            u1f(p.loc_shadows_on, haveShadow ? 1.0f : 0.0f);
            u1f(p.loc_shadow_bias, params.shadowDepthBias);
            um4(p.loc_lightviewproj, params.lightViewProjColMajor);
            if (p.loc_shadow_pcf >= 0)
            {
                const int radius = std::clamp(params.shadowPcfRadius, 0, 2);
                u1f(p.loc_shadow_pcf, static_cast<float>(radius));
            }
            if (p.loc_shadow_texel >= 0)
            {
                const int width = haveShadow ? params.shadowMap->GetWidth() : 1;
                const int height = haveShadow ? params.shadowMap->GetHeight() : 1;
                u2f(p.loc_shadow_texel, width > 0 ? 1.0f / static_cast<float>(width) : 0.0f,
                    height > 0 ? 1.0f / static_cast<float>(height) : 0.0f);
            }
            if (p.loc_cascade_count >= 0)
            {
                const int count = (haveShadow && params.cascadeCount > 0)
                                      ? std::min(params.cascadeCount, 4) : 0;
                u1f(p.loc_cascade_count, static_cast<float>(count));
                if (p.loc_cascade_mats >= 0)
                    gl4_glUniformMatrix4fv(p.loc_cascade_mats, 4, GL_FALSE,
                                           params.cascadeMatricesColMajor);
                u4f(p.loc_cascade_splits, params.cascadeSplits[0], params.cascadeSplits[1],
                    params.cascadeSplits[2], params.cascadeSplits[3]);
                u4f(p.loc_cascade_viewz, params.cascadeViewZRow[0], params.cascadeViewZRow[1],
                    params.cascadeViewZRow[2], params.cascadeViewZRow[3]);
                u1f(p.loc_cascade_blend, params.cascadeBlendBand);
                u1f(p.loc_cascade_debug, params.cascadeDebugTint ? 1.0f : 0.0f);
            }

            // Punctual light (MOD-1005), units 8 and 9.
            if (p.loc_punctual_kind >= 0)
            {
                const int kind = (params.punctualKind < 0 || params.punctualKind > 2)
                                     ? 0 : params.punctualKind;
                const bool haveCube = kind == 1 && params.punctualShadowCube != nullptr;
                const bool haveMap = kind == 2 && params.punctualShadowMap != nullptr;
                u1f(p.loc_punctual_kind, static_cast<float>(kind));
                u3f(p.loc_punctual_pos, params.punctualPosition[0], params.punctualPosition[1],
                    params.punctualPosition[2]);
                u3f(p.loc_punctual_dir, params.punctualDirection[0], params.punctualDirection[1],
                    params.punctualDirection[2]);
                u3f(p.loc_punctual_diff, params.punctualDiffuse[0], params.punctualDiffuse[1],
                    params.punctualDiffuse[2]);
                u1f(p.loc_punctual_range, params.punctualRange > 0.0f ? params.punctualRange : 1.0f);
                u1f(p.loc_punctual_cosin, params.punctualCosInner);
                u1f(p.loc_punctual_cosout, params.punctualCosOuter);
                u1f(p.loc_punctual_bias, params.punctualShadowBias);
                u1f(p.loc_punctual_hasmap, (haveCube || haveMap) ? 1.0f : 0.0f);
                um4(p.loc_punctual_vp, params.punctualViewProjColMajor);
                if (p.loc_punctual_texel >= 0)
                {
                    const int width = haveMap ? params.punctualShadowMap->GetWidth() : 1;
                    const int height = haveMap ? params.punctualShadowMap->GetHeight() : 1;
                    u2f(p.loc_punctual_texel, width > 0 ? 1.0f / static_cast<float>(width) : 0.0f,
                        height > 0 ? 1.0f / static_cast<float>(height) : 0.0f);
                }
                if (p.loc_punctual_cube >= 0)
                {
                    u1i(p.loc_punctual_cube, 8);
                    if (haveCube)
                        params.punctualShadowCube->BindGL(8);
                    else
                    {
                        // A complete cube on the unit keeps the samplerCube input defined.
                        EnsureDefaultBlackCubeTexture();
                        BindDefaultTexture(defaultBlackCubeTexture_, 8, GL_TEXTURE_CUBE_MAP);
                    }
                }
                if (p.loc_punctual_map >= 0)
                {
                    u1i(p.loc_punctual_map, 9);
                    if (haveMap)
                        params.punctualShadowMap->BindGL(9);
                    else
                    {
                        EnsureDefaultTexture(defaultWhiteTexture_, kWhite);
                        BindDefaultTexture(defaultWhiteTexture_, 9, GL_TEXTURE_2D);
                    }
                }
            }
            if (p.loc_shadowmap >= 0)
            {
                u1i(p.loc_shadowmap, 7);
                if (haveShadow)
                    params.shadowMap->BindGL(7);
                else
                {
                    // White is "infinitely far": nothing occludes.
                    EnsureDefaultTexture(defaultWhiteTexture_, kWhite);
                    BindDefaultTexture(defaultWhiteTexture_, 7, GL_TEXTURE_2D);
                }
            }
        }

        // Image-based lighting (MOD-1225), units 10-12.
        if (p.loc_ibl_enabled >= 0)
        {
            const bool haveIbl = params.iblEnabled && params.iblIrradiance != nullptr &&
                                 params.iblPrefilteredSpecular != nullptr &&
                                 params.iblBrdfLut != nullptr;
            u1f(p.loc_ibl_enabled, haveIbl ? 1.0f : 0.0f);
            u1f(p.loc_ibl_mipcount, static_cast<float>(params.iblPrefilteredMipCount > 0
                                                           ? params.iblPrefilteredMipCount : 1));
            u1f(p.loc_ibl_intensity, params.iblIntensity);
            if (p.loc_ibl_irradiance >= 0)
            {
                u1i(p.loc_ibl_irradiance, 10);
                if (haveIbl) params.iblIrradiance->BindGL(10);
                else
                {
                    EnsureDefaultBlackCubeTexture();
                    BindDefaultTexture(defaultBlackCubeTexture_, 10, GL_TEXTURE_CUBE_MAP);
                }
            }
            if (p.loc_ibl_specular >= 0)
            {
                u1i(p.loc_ibl_specular, 11);
                if (haveIbl) params.iblPrefilteredSpecular->BindGL(11);
                else
                {
                    EnsureDefaultBlackCubeTexture();
                    BindDefaultTexture(defaultBlackCubeTexture_, 11, GL_TEXTURE_CUBE_MAP);
                }
            }
            if (p.loc_ibl_brdf >= 0)
            {
                u1i(p.loc_ibl_brdf, 12);
                if (haveIbl) params.iblBrdfLut->BindGL(12);
                else
                {
                    EnsureDefaultTexture(defaultWhiteTexture_, kWhite);
                    BindDefaultTexture(defaultWhiteTexture_, 12, GL_TEXTURE_2D);
                }
            }
        }
        gl4_glActiveTexture(GL_TEXTURE0);

        u4f(p.loc_alphatest, params.alphaTest[0], params.alphaTest[1], params.alphaTest[2],
            params.alphaTest[3]);
        // REMED-GFX-010: FNA's view-space fog vector; zero when fog is off.
        u4f(p.loc_fog_vector, params.fogVector[0], params.fogVector[1], params.fogVector[2],
            params.fogVector[3]);
        u3f(p.loc_fog_color, params.fogColor[0], params.fogColor[1], params.fogColor[2]);

        // Uploaded unconditionally: program state outlives the draw.
        u4f(p.loc_rt_flip_v, rtFlipV[0], rtFlipV[1], rtFlipV[2], rtFlipV[3]);
        u4f(p.loc_rt_flip_v_hi, rtFlipV[4], rtFlipV[5], rtFlipV[6], 0.0f);
    }

    // ------------------------------------------------------------------------------------
    // Index handling
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::BindNegativeBaseVertexIndices(const OpenGL4IndexBufferRenderer& ib,
                                                        int startIndex, int indexCount,
                                                        int baseVertex)
    {
        if (baseVertex >= 0)
            throw System::InvalidOperationException(
                "OpenGL4 negative-base fallback requires a negative baseVertex");

        const std::size_t indexSize = ib.IsThirtyTwoBit() ? sizeof(std::uint32_t)
                                                          : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(std::max(indexCount, 0)) * indexSize;
        const auto& source = ib.GetCpuBytes();
        negativeBaseVertexScratch_.assign(byteCount, 0);
        for (int i = 0; i < indexCount; ++i)
        {
            const std::int64_t sourceElement = static_cast<std::int64_t>(startIndex) + i;
            if (sourceElement < 0 || sourceElement >= ib.GetIndexCount())
                continue;   // SOFTWARE-322: an undefined native fetch becomes a safe zero here
            const std::size_t sourceOffset = static_cast<std::size_t>(sourceElement) * indexSize;
            if (sourceOffset > source.size() || indexSize > source.size() - sourceOffset)
                continue;
            const std::size_t targetOffset = static_cast<std::size_t>(i) * indexSize;
            if (ib.IsThirtyTwoBit())
            {
                std::uint32_t sourceIndex = 0;
                std::memcpy(&sourceIndex, source.data() + sourceOffset, indexSize);
                const std::int64_t effective = static_cast<std::int64_t>(sourceIndex) + baseVertex;
                if (effective < 0 || effective > (std::numeric_limits<std::uint32_t>::max)())
                    continue;
                const auto rebased = static_cast<std::uint32_t>(effective);
                std::memcpy(negativeBaseVertexScratch_.data() + targetOffset, &rebased, indexSize);
            }
            else
            {
                std::uint16_t sourceIndex = 0;
                std::memcpy(&sourceIndex, source.data() + sourceOffset, indexSize);
                const std::int64_t effective = static_cast<std::int64_t>(sourceIndex) + baseVertex;
                if (effective < 0 || effective > (std::numeric_limits<std::uint16_t>::max)())
                    continue;
                const auto rebased = static_cast<std::uint16_t>(effective);
                std::memcpy(negativeBaseVertexScratch_.data() + targetOffset, &rebased, indexSize);
            }
        }

        if (negativeBaseVertexIbo_ == 0) gl4_glGenBuffers(1, &negativeBaseVertexIbo_);
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, negativeBaseVertexIbo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(negativeBaseVertexScratch_.size()),
                         negativeBaseVertexScratch_.data(), GL_DYNAMIC_DRAW);
    }

    void OpenGL4Renderer::DrawIndexedWithBaseVertexFallback(const OpenGL4IndexBufferRenderer& ib,
                                                            GLenum primitive, int indexCount,
                                                            GLenum indexType,
                                                            const void* indexOffset,
                                                            int startIndex, int baseVertex,
                                                            bool instanced, int instanceCount)
    {
        // A negative base is valid in XNA when the stored indices compensate it. Folding it into
        // the index slice avoids driver-dependent handling of negative native base vertices.
        const bool foldNegativeIndices = baseVertex < 0;
        if (foldNegativeIndices)
            BindNegativeBaseVertexIndices(ib, startIndex, indexCount, baseVertex);
        else
            gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());

        const void* effectiveOffset = foldNegativeIndices ? nullptr : indexOffset;
        const int effectiveBaseVertex = foldNegativeIndices ? 0 : baseVertex;
        if (instanced)
        {
            if (effectiveBaseVertex == 0)
                gl4_glDrawElementsInstanced(primitive, indexCount, indexType, effectiveOffset,
                                            instanceCount);
            else
                gl4_glDrawElementsInstancedBaseVertex(primitive, indexCount, indexType,
                                                      effectiveOffset, instanceCount,
                                                      effectiveBaseVertex);
        }
        else if (effectiveBaseVertex == 0)
        {
            glDrawElements(primitive, indexCount, indexType, effectiveOffset);
        }
        else
        {
            gl4_glDrawElementsBaseVertex(primitive, indexCount, indexType, effectiveOffset,
                                         effectiveBaseVertex);
        }
    }

    // ------------------------------------------------------------------------------------
    // Draws
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vbIn,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection, PrimitiveType primitive,
                                                int primitiveCount)
    {
        ApplyStencilPrimitiveTopology(primitive);
        EnsureStockProgram(progColored_, StockProgramShape::Colored, false);
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vbIn);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);
        progColored_.prog.Use();
        if (progColored_.loc_wvp >= 0)
            gl4_glUniformMatrix4fv(progColored_.loc_wvp, 1, GL_FALSE, wvpCol);
        // This route carries no BasicEffect state: raw vertex colours.
        if (progColored_.loc_diffuse >= 0)
            gl4_glUniform4f(progColored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        if (progColored_.loc_vertexcolor >= 0)
            gl4_glUniform1f(progColored_.loc_vertexcolor, 1.0f);
        if (progColored_.loc_alphatest >= 0)
            gl4_glUniform4f(progColored_.loc_alphatest, 0.0f, 0.0f, 1.0f, 1.0f);
        if (progColored_.loc_fog_vector >= 0)
            gl4_glUniform4f(progColored_.loc_fog_vector, 0.0f, 0.0f, 0.0f, 0.0f);

        gl4_glBindVertexArray(vb.VaoHandle());
        glDrawArrays(ToGLPrimitive(primitive), 0, VertexCountForPrimitives(primitive, primitiveCount));
        gl4_glBindVertexArray(0);
    }

    void OpenGL4Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vbIn,
                                                       const IIndexBufferRenderer& ibIn,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        ApplyStencilPrimitiveTopology(primitive);
        EnsureStockProgram(progColored_, StockProgramShape::Colored, false);
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vbIn);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ibIn);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);
        progColored_.prog.Use();
        if (progColored_.loc_wvp >= 0)
            gl4_glUniformMatrix4fv(progColored_.loc_wvp, 1, GL_FALSE, wvpCol);
        if (progColored_.loc_diffuse >= 0)
            gl4_glUniform4f(progColored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        if (progColored_.loc_vertexcolor >= 0)
            gl4_glUniform1f(progColored_.loc_vertexcolor, 1.0f);
        if (progColored_.loc_alphatest >= 0)
            gl4_glUniform4f(progColored_.loc_alphatest, 0.0f, 0.0f, 1.0f, 1.0f);
        if (progColored_.loc_fog_vector >= 0)
            gl4_glUniform4f(progColored_.loc_fog_vector, 0.0f, 0.0f, 0.0f, 0.0f);

        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        glDrawElements(ToGLPrimitive(primitive), VertexCountForPrimitives(primitive, primitiveCount),
                       ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vbIn, const Matrix& world,
                                           const Matrix& view, const Matrix& projection,
                                           PrimitiveType primitive, int primitiveCount,
                                           const GpuDrawParams& paramsIn)
    {
        ApplyStencilPrimitiveTopology(primitive);
        auto& vb = const_cast<OpenGL4VertexBufferRenderer&>(
            static_cast<const OpenGL4VertexBufferRenderer&>(vbIn));
        GpuDrawParams unlitScratch;
        const GpuDrawParams& params = StockParamsFor(vb, paramsIn, unlitScratch);
        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        // REMED-GFX-218: validated before the VAO is touched or a program selected. A custom
        // ShaderEffect owns its own element-order convention and is not checked here.
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgram(vb.GetDeclarationElements(), layoutStride, params);

        const bool multiStream = HasMultipleVertexStreams(params);
        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (params.customEffectRenderer)
        {
            gl4_glBindVertexArray(vb.VaoHandle());
            if (multiStream && !ConfigureMultiStreamAttributes(params))
            {
                gl4_glBindVertexArray(0);
                throw System::InvalidOperationException(
                    "OpenGL4 multi-stream drawing requires every bound VertexBuffer to carry a "
                    "VertexDeclaration.");
            }
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            glDrawArrays(ToGLPrimitive(primitive), params.vertexStart, vertexCount);
            if (multiStream) RestoreSingleStreamAttributes(params);
            gl4_glBindVertexArray(0);
            return;
        }

        OpenGL4StockProgram& p = SelectProgram(layoutStride, params);
        p.prog.Use();
        BindDrawParams(p, world, view, projection, params);
        gl4_glBindVertexArray(vb.VaoHandle());
        const bool semanticLayout = ConfigureDeclarationForStockProgram(vb, layoutStride, params);
        glDrawArrays(ToGLPrimitive(primitive), params.vertexStart, vertexCount);
        gl4_glBindVertexArray(0);
        if (semanticLayout) RestoreDeclarationLayout(vb);
    }

    void OpenGL4Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vbIn,
                                                  const IIndexBufferRenderer& ibIn,
                                                  const Matrix& world, const Matrix& view,
                                                  const Matrix& projection, PrimitiveType primitive,
                                                  int primitiveCount, const GpuDrawParams& paramsIn)
    {
        ApplyStencilPrimitiveTopology(primitive);
        auto& vb = const_cast<OpenGL4VertexBufferRenderer&>(
            static_cast<const OpenGL4VertexBufferRenderer&>(vbIn));
        GpuDrawParams unlitScratch;
        const GpuDrawParams& params = StockParamsFor(vb, paramsIn, unlitScratch);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ibIn);
        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgram(vb.GetDeclarationElements(), layoutStride, params);

        const bool multiStream = HasMultipleVertexStreams(params);
        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum indexType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(std::max(params.startIndex, 0)) * indexSize);

        if (params.customEffectRenderer)
        {
            gl4_glBindVertexArray(vb.VaoHandle());
            if (multiStream && !ConfigureMultiStreamAttributes(params))
            {
                gl4_glBindVertexArray(0);
                throw System::InvalidOperationException(
                    "OpenGL4 multi-stream drawing requires every bound VertexBuffer to carry a "
                    "VertexDeclaration.");
            }
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            DrawIndexedWithBaseVertexFallback(ib, ToGLPrimitive(primitive), indexCount, indexType,
                                              indexOffset, params.startIndex, params.baseVertex,
                                              false, 0);
            if (multiStream) RestoreSingleStreamAttributes(params);
            gl4_glBindVertexArray(0);
            return;
        }

        OpenGL4StockProgram& p = SelectProgram(layoutStride, params);
        p.prog.Use();
        BindDrawParams(p, world, view, projection, params);
        gl4_glBindVertexArray(vb.VaoHandle());
        const bool semanticLayout = ConfigureDeclarationForStockProgram(vb, layoutStride, params);
        DrawIndexedWithBaseVertexFallback(ib, ToGLPrimitive(primitive), indexCount, indexType,
                                          indexOffset, params.startIndex, params.baseVertex, false,
                                          0);
        gl4_glBindVertexArray(0);
        if (semanticLayout) RestoreDeclarationLayout(vb);
    }

    void OpenGL4Renderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vbIn,
                                                    const IIndexBufferRenderer& ibIn,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    int instanceCount, const GpuDrawParams& paramsIn)
    {
        ApplyStencilPrimitiveTopology(primitive);
        auto& vb = const_cast<OpenGL4VertexBufferRenderer&>(
            static_cast<const OpenGL4VertexBufferRenderer&>(vbIn));
        GpuDrawParams unlitScratch;
        const GpuDrawParams& params = StockParamsFor(vb, paramsIn, unlitScratch);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ibIn);
        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgram(vb.GetDeclarationElements(), layoutStride, params);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum indexType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(std::max(params.startIndex, 0)) * indexSize);

        const bool multiStream = HasMultipleVertexStreams(params);
        const GpuVertexStreamBinding* firstPerVertex = FirstPerVertexStream(params);
        const bool reconfigurePerVertex =
            multiStream || (firstPerVertex != nullptr && firstPerVertex->vertexOffset != 0);
        if (reconfigurePerVertex && vb.GetDeclarationElements().empty())
            throw System::InvalidOperationException(
                "OpenGL4 instanced drawing cannot apply a nonzero vertex-buffer offset without a "
                "VertexDeclaration.");

        InstanceStreamPlacements placements;
        const unsigned int instanceBaseLocation = params.customEffectRenderer != nullptr
            ? PerVertexLocationCount(params)
            : kStockInstanceBaseLocation;
        if (FirstInstanceStream(params) != nullptr)
        {
            if ((params.customEffectRenderer == nullptr &&
                 instanceBaseLocation < PerVertexLocationCount(params)) ||
                !PlaceInstanceStreams(params, instanceBaseLocation, placements))
            {
                throw System::InvalidOperationException(
                    "OpenGL4 instanced drawing requires a complete per-instance declaration "
                    "within the 16-attribute XNA profile limit.");
            }
        }

        const bool semanticLayout = params.customEffectRenderer == nullptr &&
                                    ConfigureDeclarationForStockProgram(vb, layoutStride, params);
        gl4_glBindVertexArray(vb.VaoHandle());
        if (params.customEffectRenderer != nullptr && reconfigurePerVertex &&
            !ConfigureMultiStreamAttributes(params))
        {
            gl4_glBindVertexArray(0);
            throw System::InvalidOperationException(
                "OpenGL4 multi-stream drawing requires every bound VertexBuffer to carry a "
                "VertexDeclaration.");
        }
        {
            std::size_t placementIndex = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0) continue;
                const InstanceStreamPlacement& placement = placements.entries[placementIndex++];
                ConfigureDeclarationAttributes(AsBuffer(stream.buffer), placement.firstLocation,
                                               stream.vertexOffset,
                                               static_cast<unsigned int>(stream.instanceFrequency),
                                               placement.elementCount);
            }
        }

        OpenGL4StockProgram* stock = nullptr;
        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
        }
        else
        {
            stock = &SelectProgram(layoutStride, params);
            stock->prog.Use();
            BindDrawParams(*stock, world, view, projection, params);
        }

        DrawIndexedWithBaseVertexFallback(ib, ToGLPrimitive(primitive), indexCount, indexType,
                                          indexOffset, params.startIndex, params.baseVertex, true,
                                          instanceCount);

        for (int i = placements.count; i-- > 0;)
        {
            const InstanceStreamPlacement& placement = placements.entries[static_cast<std::size_t>(i)];
            DisableDeclarationAttributes(placement.firstLocation, placement.elementCount);
        }
        if (reconfigurePerVertex && params.customEffectRenderer != nullptr)
            RestoreSingleStreamAttributes(params);
        if (stock != nullptr && stock->loc_instanced >= 0)
            gl4_glUniform1f(stock->loc_instanced, 0.0f);
        gl4_glBindVertexArray(0);
        if (semanticLayout) RestoreDeclarationLayout(vb);
    }

    namespace
    {
        StockInputTable StockInputsFor(OpenGL4Renderer::StockProgramShape shape, std::size_t stride)
        {
            using Shape = OpenGL4Renderer::StockProgramShape;
            switch (shape)
            {
            case Shape::PbrSkinned:
                if (stride == 80) return Table(kPbrSkinnedDualUvColorInputs, "pbr_skinned3d");
                if (stride == 76) return Table(kPbrSkinnedDualUvInputs, "pbr_skinned3d");
                return Table(kPbrSkinnedInputs, "pbr_skinned3d");
            case Shape::Pbr:
                if (stride == 60) return Table(kPbrDualUvInputs, "pbr3d");
                return Table(kPbrInputs, "pbr3d");
            case Shape::SkinnedVertexLit: return Table(kSkinnedInputs, "skinned3d_vertexlit");
            case Shape::Skinned: return Table(kSkinnedInputs, "skinned3d");
            case Shape::EnvMapped: return Table(kEnvMappedInputs, "env_mapped3d");
            case Shape::DualTexturedColored:
                return Table(kDualTexturedColoredInputs, "dual_textured_colored3d");
            case Shape::DualTextured: return Table(kDualTexturedInputs, "dual_textured3d");
            case Shape::Textured: return Table(kTexturedInputs, "textured3d");
            case Shape::ColoredTextured: return Table(kColTexturedInputs, "colored_textured3d");
            case Shape::LitVertexLit: return Table(kLitInputs, "lit_textured3d_vertexlit");
            case Shape::Lit: return Table(kLitInputs, "lit_textured3d");
            case Shape::Colored: break;
            }
            return Table(kColoredInputs, "colored3d");
        }
    }
}
