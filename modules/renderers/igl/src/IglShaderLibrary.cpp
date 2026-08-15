// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglShaderLibrary.hpp"

#include <string>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        /// Desktop OpenGL floor for this renderer: 4.1 core is the version the platform GL context
        /// is requested at, and everything the generated shaders use (uniform blocks, `texture()`,
        /// explicit attribute locations) is core there. Explicit `binding =` layout qualifiers are
        /// deliberately NOT used on this path -- they are GL 4.2+ -- so blocks and samplers are
        /// wired up by name through RenderPipelineDesc's own maps instead.
        constexpr const char* kOpenGlVersionDirective = "#version 410 core\n";

        /// IGL's Vulkan device prepends `#version 460` itself when the source has none. Emitting it
        /// here anyway keeps both backends going through one code path and makes the compiled text
        /// self-describing when it is logged after a compile failure.
        constexpr const char* kVulkanVersionDirective = "#version 460\n";

        [[nodiscard]] bool HasAttribute(const std::uint32_t mask, const std::uint32_t slot)
        {
            return (mask & VertexAttributeBit(slot)) != 0;
        }

        [[nodiscard]] std::string Location(const std::uint32_t location)
        {
            return "layout(location = " + std::to_string(location) + ") ";
        }

        /// Declares a uniform block for the active backend. Vulkan needs the descriptor set index
        /// IGL binds uniform buffers into (`kBindPoint_Buffers` == 1); OpenGL 4.1 gets a plain
        /// std140 block whose binding point is assigned through `uniformBlockBindingMap`.
        [[nodiscard]] std::string UniformBlockPrefix(const bool vulkan, const std::uint32_t binding)
        {
            if (vulkan)
            {
                return "layout(set = 1, binding = " + std::to_string(binding) + ", std140) uniform ";
            }
            return "layout(std140) uniform ";
        }

        /// Declares a sampler for the active backend. Vulkan needs the descriptor set IGL binds
        /// combined image samplers into (`kBindPoint_CombinedImageSamplers` == 0); OpenGL 4.1 gets a
        /// plain uniform whose texture unit is assigned through `fragmentUnitSamplerMap`.
        [[nodiscard]] std::string SamplerDeclaration(const bool vulkan,
                                                     const std::uint32_t binding,
                                                     const char* type,
                                                     const char* name)
        {
            std::string declaration;
            if (vulkan)
                declaration += "layout(set = 0, binding = " + std::to_string(binding) + ") ";
            declaration += "uniform ";
            declaration += type;
            declaration += " ";
            declaration += name;
            declaration += ";\n";
            return declaration;
        }

        /** @brief The shared `CnaEffect` and `CnaBones` block declarations. */
        [[nodiscard]] std::string UniformBlocks(const bool vulkan)
        {
            std::string source;
            source += UniformBlockPrefix(vulkan, UniformBufferBinding::Effect);
            source += R"(CnaEffect {
    mat4 uWorldViewProjection;
    mat4 uWorld;
    mat4 uWorldInverseTranspose;
    vec4 uDiffuseColor;
    vec4 uEmissiveColor;
    vec4 uSpecularColor;
    vec4 uAmbientColor;
    vec4 uEyePosition;
    vec4 uFogColor;
    vec4 uFogVector;
    vec4 uAlphaTest;
    vec4 uEnvMapSpecular;
    vec4 uPbrFactors;
    vec4 uLightDirection[3];
    vec4 uLightDiffuse[3];
    vec4 uLightSpecular[3];
    vec4 uColorMatrix[4];
    vec4 uColorOffset;
    ivec4 uFlags;
} cna;

)";
            source += UniformBlockPrefix(vulkan, UniformBufferBinding::Bones);
            source += R"(CnaBones {
    mat4 uBones[72];
} cnaBones;

)";
            return source;
        }

        /** @brief Feature-bit helpers shared by both stages, so the two never disagree. */
        constexpr const char* kFeatureHelpers = R"(
bool cnaHas(int bit) { return (cna.uFlags.x & bit) != 0; }

const int CNA_TEXTURE_ENABLED       = 1;
const int CNA_VERTEX_COLOR_ENABLED  = 2;
const int CNA_LIGHTING_ENABLED      = 4;
const int CNA_PER_PIXEL_LIGHTING    = 8;
const int CNA_FOG_ENABLED           = 16;
const int CNA_DUAL_TEXTURE          = 32;
const int CNA_ENV_MAPPING           = 64;
const int CNA_FRESNEL_ENABLED       = 128;
const int CNA_SKINNED               = 256;
const int CNA_PBR                   = 512;
const int CNA_ALPHA_TEST_ENABLED    = 1024;
const int CNA_COLOR_MATRIX          = 2048;
const int CNA_NORMAL_MAP            = 4096;
const int CNA_METALLIC_ROUGHNESS_MAP = 8192;
const int CNA_EMISSIVE_MAP          = 16384;
const int CNA_OCCLUSION_MAP         = 32768;

// Blinn-Phong contribution of the three XNA directional lights, in FNA's own formulation:
// a light's diffuse term is gated by step(0, dot(N, L)) so a back-facing surface contributes
// nothing, and the specular term is gated by the same factor before the power is taken.
void cnaComputeLights(vec3 normal, vec3 eyeVector, out vec3 diffuse, out vec3 specular)
{
    diffuse = vec3(0.0);
    specular = vec3(0.0);
    float specularPower = max(cna.uEmissiveColor.w, 0.0001);
    for (int i = 0; i < 3; ++i)
    {
        vec3 lightDir = -cna.uLightDirection[i].xyz;
        vec3 halfway = normalize(eyeVector + lightDir);
        float dotL = dot(normal, lightDir);
        float dotH = dot(normal, halfway);
        float zeroL = step(0.0, dotL);
        diffuse += cna.uLightDiffuse[i].rgb * (zeroL * dotL);
        specular += cna.uLightSpecular[i].rgb * pow(max(dotH, 0.0) * zeroL, specularPower);
    }
}
)";

        /** @brief Vertex stage. */
        [[nodiscard]] std::string BuildVertexShader(const std::uint32_t attributeMask,
                                                     const bool vulkan)
        {
            std::string source = vulkan ? kVulkanVersionDirective : kOpenGlVersionDirective;

            source += Location(VertexAttributeSlot::Position) + "in vec3 aPosition;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Normal))
                source += Location(VertexAttributeSlot::Normal) + "in vec3 aNormal;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Color))
                source += Location(VertexAttributeSlot::Color) + "in vec4 aColor;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::TexCoord0))
                source += Location(VertexAttributeSlot::TexCoord0) + "in vec2 aTexCoord0;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::TexCoord1))
                source += Location(VertexAttributeSlot::TexCoord1) + "in vec2 aTexCoord1;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::BlendIndices))
                source += Location(VertexAttributeSlot::BlendIndices) + "in vec4 aBlendIndices;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::BlendWeights))
                source += Location(VertexAttributeSlot::BlendWeights) + "in vec4 aBlendWeights;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Tangent))
                source += Location(VertexAttributeSlot::Tangent) + "in vec4 aTangent;\n";
            source += "\n";

            // Every varying is always written, whether or not the matching attribute exists, so one
            // fragment shader serves every vertex layout.
            source += R"(layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord0;
layout(location = 2) out vec2 vTexCoord1;
layout(location = 3) out vec3 vWorldPosition;
layout(location = 4) out vec3 vWorldNormal;
layout(location = 5) out vec3 vSpecular;
layout(location = 6) out vec4 vObjectPosition;
layout(location = 7) out vec4 vWorldTangent;

)";
            source += UniformBlocks(vulkan);
            source += kFeatureHelpers;

            source += R"(
void main()
{
    vec4 objectPosition = vec4(aPosition, 1.0);
    vec3 objectNormal = vec3(0.0, 0.0, 1.0);
    vec4 objectTangent = vec4(1.0, 0.0, 0.0, 1.0);
)";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Normal))
                source += "    objectNormal = aNormal;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Tangent))
                source += "    objectTangent = aTangent;\n";

            if (HasAttribute(attributeMask, VertexAttributeSlot::BlendIndices) &&
                HasAttribute(attributeMask, VertexAttributeSlot::BlendWeights))
            {
                source += R"(
    if (cnaHas(CNA_SKINNED))
    {
        // XNA's Skin(vin, boneCount) sums only the first `weightsPerVertex` weight/index pairs,
        // so a 1- or 2-weight effect must not silently pick up the remaining components.
        int weights = clamp(cna.uFlags.y, 1, 4);
        mat4 skin = mat4(0.0);
        for (int i = 0; i < 4; ++i)
        {
            if (i >= weights) break;
            int boneIndex = int(aBlendIndices[i] + 0.5);
            boneIndex = clamp(boneIndex, 0, max(cna.uFlags.z - 1, 0));
            skin += cnaBones.uBones[boneIndex] * aBlendWeights[i];
        }
        objectPosition = skin * objectPosition;
        objectNormal = mat3(skin) * objectNormal;
        objectTangent = vec4(mat3(skin) * objectTangent.xyz, objectTangent.w);
    }
)";
            }

            source += R"(
    vObjectPosition = objectPosition;
    gl_Position = cna.uWorldViewProjection * objectPosition;

    vWorldPosition = (cna.uWorld * objectPosition).xyz;
    vWorldNormal = normalize(mat3(cna.uWorldInverseTranspose) * objectNormal);
    vWorldTangent = vec4(normalize(mat3(cna.uWorld) * objectTangent.xyz), objectTangent.w);

    vec4 color = cna.uDiffuseColor;
)";
            if (HasAttribute(attributeMask, VertexAttributeSlot::Color))
            {
                source += R"(    if (cnaHas(CNA_VERTEX_COLOR_ENABLED))
        color *= aColor;
)";
            }

            source += R"(
    vSpecular = vec3(0.0);
    if (cnaHas(CNA_LIGHTING_ENABLED) && !cnaHas(CNA_PER_PIXEL_LIGHTING) && !cnaHas(CNA_PBR))
    {
        vec3 eyeVector = normalize(cna.uEyePosition.xyz - vWorldPosition);
        vec3 diffuse;
        vec3 specular;
        cnaComputeLights(vWorldNormal, eyeVector, diffuse, specular);
        // CNA folds ambient into the DiffuseColor multiply rather than pre-baking FNA's
        // "ambient + emissive" uniform; the net result is identical.
        color.rgb = (cna.uAmbientColor.rgb + diffuse) * color.rgb + cna.uEmissiveColor.rgb;
        vSpecular = specular * cna.uSpecularColor.rgb;
    }

    vColor = color;
    vTexCoord0 = vec2(0.0);
    vTexCoord1 = vec2(0.0);
)";
            if (HasAttribute(attributeMask, VertexAttributeSlot::TexCoord0))
                source += "    vTexCoord0 = aTexCoord0;\n";
            if (HasAttribute(attributeMask, VertexAttributeSlot::TexCoord1))
                source += "    vTexCoord1 = aTexCoord1;\n";
            source += "}\n";
            return source;
        }

        /**
         * @brief Fragment stage; identical for every vertex layout but not for every target set.
         *
         * @p colorAttachmentCount decides how many `out vec4` slots are declared. A Vulkan pipeline
         * requires the fragment shader's outputs and the render pass's colour attachments to
         * correspond, so a shader that always declared four would be invalid for the single-target
         * case that every ordinary draw uses.
         */
        [[nodiscard]] std::string BuildFragmentShader(const bool vulkan,
                                                       const int colorAttachmentCount)
        {
            std::string source = vulkan ? kVulkanVersionDirective : kOpenGlVersionDirective;

            source += R"(layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 3) in vec3 vWorldPosition;
layout(location = 4) in vec3 vWorldNormal;
layout(location = 5) in vec3 vSpecular;
layout(location = 6) in vec4 vObjectPosition;
layout(location = 7) in vec4 vWorldTangent;

)";
            for (int i = 0; i < colorAttachmentCount; ++i)
            {
                source += "layout(location = " + std::to_string(i) + ") out vec4 outColor" +
                          std::to_string(i) + ";\n";
            }
            source += "\n";
            source += UniformBlocks(vulkan);
            source += SamplerDeclaration(vulkan, TextureUnit::Texture0, "sampler2D", "uTexture0");
            source += SamplerDeclaration(vulkan, TextureUnit::Texture1, "sampler2D", "uTexture1");
            source += SamplerDeclaration(vulkan, TextureUnit::EnvironmentMap, "samplerCube", "uEnvMap");
            source += SamplerDeclaration(vulkan, TextureUnit::NormalMap, "sampler2D", "uNormalMap");
            source += SamplerDeclaration(vulkan, TextureUnit::MetallicRoughnessMap, "sampler2D",
                                         "uMetallicRoughnessMap");
            source += SamplerDeclaration(vulkan, TextureUnit::EmissiveMap, "sampler2D", "uEmissiveMap");
            source += SamplerDeclaration(vulkan, TextureUnit::OcclusionMap, "sampler2D", "uOcclusionMap");
            source += "\n";
            source += kFeatureHelpers;

            source += R"(
const float CNA_PI = 3.14159265358979;

float cnaDistributionGGX(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(CNA_PI * d * d, 1e-5);
}

float cnaGeometrySmith(float nDotV, float nDotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = nDotV / (nDotV * (1.0 - k) + k);
    float gl = nDotL / (nDotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 cnaFresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// glTF metallic-roughness BRDF over the same three directional lights the Blinn-Phong path uses,
// so a PbrEffect and a BasicEffect lit by the same scene agree on where the light comes from.
vec3 cnaShadePbr(vec3 baseColor, vec3 normal, vec3 eyeVector, float metallic, float roughness)
{
    roughness = clamp(roughness, 0.04, 1.0);
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 diffuseColor = baseColor * (1.0 - metallic);
    float nDotV = max(dot(normal, eyeVector), 1e-4);

    vec3 result = vec3(0.0);
    for (int i = 0; i < 3; ++i)
    {
        vec3 lightDir = -cna.uLightDirection[i].xyz;
        float nDotL = dot(normal, lightDir);
        if (nDotL <= 0.0) continue;

        vec3 halfway = normalize(eyeVector + lightDir);
        float nDotH = max(dot(normal, halfway), 0.0);
        float vDotH = max(dot(eyeVector, halfway), 0.0);

        float d = cnaDistributionGGX(nDotH, roughness);
        float g = cnaGeometrySmith(nDotV, nDotL, roughness);
        vec3 f = cnaFresnelSchlick(vDotH, f0);

        vec3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1e-5);
        vec3 kd = (vec3(1.0) - f) * (1.0 - metallic);
        result += (kd * diffuseColor / CNA_PI + specular) * cna.uLightDiffuse[i].rgb * nDotL;
    }
    return result;
}

void main()
{
    vec4 color = vColor;

    if (cnaHas(CNA_TEXTURE_ENABLED))
        color *= texture(uTexture0, vTexCoord0);

    if (cnaHas(CNA_DUAL_TEXTURE))
        color *= texture(uTexture1, vTexCoord1) * 2.0;

    vec3 normal = normalize(vWorldNormal);
    vec3 eyeVector = normalize(cna.uEyePosition.xyz - vWorldPosition);

    if (cnaHas(CNA_NORMAL_MAP))
    {
        vec3 tangent = normalize(vWorldTangent.xyz - normal * dot(normal, vWorldTangent.xyz));
        vec3 bitangent = cross(normal, tangent) * vWorldTangent.w;
        vec3 sampled = texture(uNormalMap, vTexCoord0).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, bitangent, normal) * sampled);
    }

    if (cnaHas(CNA_PBR))
    {
        float metallic = cna.uPbrFactors.x;
        float roughness = cna.uPbrFactors.y;
        if (cnaHas(CNA_METALLIC_ROUGHNESS_MAP))
        {
            vec4 mr = texture(uMetallicRoughnessMap, vTexCoord0);
            roughness *= mr.g;
            metallic *= mr.b;
        }

        float occlusion = 1.0;
        if (cnaHas(CNA_OCCLUSION_MAP))
            occlusion = texture(uOcclusionMap, vTexCoord0).r;

        vec3 shaded = cnaShadePbr(color.rgb, normal, eyeVector, metallic, roughness);
        shaded += cna.uAmbientColor.rgb * color.rgb * occlusion;
        shaded += cna.uEmissiveColor.rgb;
        if (cnaHas(CNA_EMISSIVE_MAP))
            shaded += texture(uEmissiveMap, vTexCoord0).rgb;
        color = vec4(shaded, color.a);
    }
    else if (cnaHas(CNA_LIGHTING_ENABLED) && cnaHas(CNA_PER_PIXEL_LIGHTING))
    {
        vec3 diffuse;
        vec3 specular;
        cnaComputeLights(normal, eyeVector, diffuse, specular);
        color.rgb = (cna.uAmbientColor.rgb + diffuse) * color.rgb + cna.uEmissiveColor.rgb;
        color.rgb += specular * cna.uSpecularColor.rgb;
    }
    else
    {
        color.rgb += vSpecular;
    }

    if (cnaHas(CNA_ENV_MAPPING))
    {
        vec4 envColor = texture(uEnvMap, reflect(-eyeVector, normal));
        float amount = cna.uSpecularColor.w;
        if (cnaHas(CNA_FRESNEL_ENABLED))
        {
            amount *= pow(max(1.0 - abs(dot(eyeVector, normal)), 0.0), cna.uAmbientColor.w);
        }
        color.rgb = mix(color.rgb, envColor.rgb, clamp(amount, 0.0, 1.0));
        color.rgb += envColor.a * cna.uEnvMapSpecular.rgb;
    }

    if (cnaHas(CNA_FOG_ENABLED))
    {
        float keep = 1.0 - clamp(dot(vObjectPosition, cna.uFogVector), 0.0, 1.0);
        color.rgb = mix(cna.uFogColor.rgb, color.rgb, keep);
    }

    if (cnaHas(CNA_COLOR_MATRIX))
    {
        vec4 source = color;
        color = vec4(dot(cna.uColorMatrix[0], source),
                     dot(cna.uColorMatrix[1], source),
                     dot(cna.uColorMatrix[2], source),
                     dot(cna.uColorMatrix[3], source)) + cna.uColorOffset;
        color = clamp(color, 0.0, 1.0);
    }

    if (cnaHas(CNA_ALPHA_TEST_ENABLED))
    {
        // XNA's rule, verbatim: a positive tolerance means "pass when |a - ref| < tolerance"
        // (the Equal/NotEqual comparisons), otherwise "pass when a < ref"; the selected weight
        // going negative is what discards the fragment.
        float weight = ((cna.uAlphaTest.y > 0.0)
                            ? (abs(color.a - cna.uAlphaTest.x) < cna.uAlphaTest.y)
                            : (color.a < cna.uAlphaTest.x))
                           ? cna.uAlphaTest.z
                           : cna.uAlphaTest.w;
        if (weight < 0.0)
            discard;
    }

)";
            // XNA has no multi-output stock effect: a game that binds several render targets and
            // draws with a stock effect gets the same colour in every slot, which is exactly what
            // FNA's own stock shaders produce. A game that wants distinct outputs writes its own
            // Effect, which takes the custom-shader path instead.
            for (int i = 0; i < colorAttachmentCount; ++i)
                source += "    outColor" + std::to_string(i) + " = color;\n";
            source += "}\n";
            return source;
        }
    }

    IglShaderSources BuildEffectShaderSources(const std::uint32_t attributeMask,
                                              const bool vulkan,
                                              const int colorAttachmentCount)
    {
        IglShaderSources sources;
        sources.vertex = BuildVertexShader(attributeMask, vulkan);
        sources.fragment = BuildFragmentShader(vulkan, colorAttachmentCount < 1 ? 1
                                                                                : colorAttachmentCount);
        return sources;
    }

    IglShaderSources AdaptCustomShaderSources(const std::string& vertexSource,
                                              const std::string& fragmentSource,
                                              const bool vulkan)
    {
        const auto adapt = [vulkan](const std::string& source) {
            if (source.find("#version") != std::string::npos)
                return source;
            return std::string(vulkan ? kVulkanVersionDirective : kOpenGlVersionDirective) + source;
        };

        IglShaderSources sources;
        sources.vertex = adapt(vertexSource);
        sources.fragment = adapt(fragmentSource);
        return sources;
    }

    const char* GetSamplerUniformName(const std::uint32_t unit)
    {
        switch (unit)
        {
            case TextureUnit::Texture0:             return "uTexture0";
            case TextureUnit::Texture1:             return "uTexture1";
            case TextureUnit::EnvironmentMap:       return "uEnvMap";
            case TextureUnit::NormalMap:            return "uNormalMap";
            case TextureUnit::MetallicRoughnessMap: return "uMetallicRoughnessMap";
            case TextureUnit::EmissiveMap:          return "uEmissiveMap";
            case TextureUnit::OcclusionMap:         return "uOcclusionMap";
            default:                                return "";
        }
    }

    const char* GetUniformBlockName(const std::uint32_t binding)
    {
        switch (binding)
        {
            case UniformBufferBinding::Effect: return "CnaEffect";
            case UniformBufferBinding::Bones:  return "CnaBones";
            default:                           return "";
        }
    }
}
