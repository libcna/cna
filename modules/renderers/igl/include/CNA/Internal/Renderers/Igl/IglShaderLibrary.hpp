// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::Igl
{
    /**
     * @brief Vertex attribute locations this renderer's generated shaders always use. CNAEXT.
     *
     * XNA describes a vertex with `VertexElementUsage`; a shader needs a fixed location. This is the
     * single mapping between the two, used both when a `VertexDeclaration` is translated into an
     * `igl::VertexInputStateDesc` and when the shader text declaring those inputs is generated, so
     * the two can never disagree.
     */
    namespace VertexAttributeSlot
    {
        /** @brief Object-space position (`vec3`); the one attribute every layout must carry. */
        inline constexpr std::uint32_t Position = 0;
        /** @brief Object-space normal (`vec3`). */
        inline constexpr std::uint32_t Normal = 1;
        /** @brief Vertex colour (`vec4`, normalized from RGBA8). */
        inline constexpr std::uint32_t Color = 2;
        /** @brief Primary texture coordinate (`vec2`). */
        inline constexpr std::uint32_t TexCoord0 = 3;
        /** @brief Secondary texture coordinate (`vec2`), used by DualTextureEffect. */
        inline constexpr std::uint32_t TexCoord1 = 4;
        /** @brief Bone indices (`vec4`), used by SkinnedEffect. */
        inline constexpr std::uint32_t BlendIndices = 5;
        /** @brief Bone weights (`vec4`), used by SkinnedEffect. */
        inline constexpr std::uint32_t BlendWeights = 6;
        /** @brief Tangent (`vec4`), used by PbrEffect's normal mapping. */
        inline constexpr std::uint32_t Tangent = 7;
        /** @brief Number of attribute slots the generated shaders know about. */
        inline constexpr std::uint32_t Count = 8;
    }

    /** @brief Bit for @ref VertexAttributeSlot @p slot inside a shader-variant attribute mask. */
    [[nodiscard]] constexpr std::uint32_t VertexAttributeBit(const std::uint32_t slot) noexcept
    {
        return 1u << slot;
    }

    /**
     * @brief Texture units the generated shaders sample from. CNAEXT.
     *
     * These are the `index` values passed to `igl::IRenderCommandEncoder::bindTexture`, the Vulkan
     * `set = 0` binding numbers, and the keys of the OpenGL `fragmentUnitSamplerMap` -- one table
     * so a unit cannot mean different things on the two backends.
     */
    namespace TextureUnit
    {
        /** @brief Diffuse texture / SpriteBatch source / DualTextureEffect layer 0. */
        inline constexpr std::uint32_t Texture0 = 0;
        /** @brief DualTextureEffect's second layer. */
        inline constexpr std::uint32_t Texture1 = 1;
        /** @brief EnvironmentMapEffect's cube map. */
        inline constexpr std::uint32_t EnvironmentMap = 2;
        /** @brief PbrEffect's tangent-space normal map. */
        inline constexpr std::uint32_t NormalMap = 3;
        /** @brief PbrEffect's metallic-roughness map (glTF packing: G=roughness, B=metallic). */
        inline constexpr std::uint32_t MetallicRoughnessMap = 4;
        /** @brief PbrEffect's emissive map. */
        inline constexpr std::uint32_t EmissiveMap = 5;
        /** @brief PbrEffect's occlusion map (R channel). */
        inline constexpr std::uint32_t OcclusionMap = 6;
        /** @brief `KHR_materials_specular`'s scalar strength map (A channel). */
        inline constexpr std::uint32_t SpecularMap = 7;
        /** @brief `KHR_materials_specular`'s colour map (RGB, sRGB-encoded). */
        inline constexpr std::uint32_t SpecularColorMap = 8;
        /** @brief Number of units the generated fragment shader declares. */
        inline constexpr std::uint32_t Count = 9;
    }

    /**
     * @brief Uniform-buffer binding points. CNAEXT.
     *
     * The `index` passed to `igl::IRenderCommandEncoder::bindBuffer`, the Vulkan `set = 1` binding
     * numbers, and the keys of the OpenGL `uniformBlockBindingMap`.
     */
    namespace UniformBufferBinding
    {
        /** @brief The per-draw effect block. */
        inline constexpr std::uint32_t Effect = 0;
        /** @brief The SkinnedEffect bone-matrix block. */
        inline constexpr std::uint32_t Bones = 1;
        /**
         * @brief A custom `ShaderEffect`'s own parameter block (plan_igl.md IGL-43).
         *
         * Only a custom effect declares this; the generated shaders do not, so the binding is free
         * for one. A Vulkan custom shader that wants parameters declares
         * `layout(set = 1, binding = 2, std140) uniform <name> { ... };` and CNA fills it.
         */
        inline constexpr std::uint32_t CustomEffect = 2;
    }

    /**
     * @brief Feature bits packed into `CnaEffect.uFlags.x`. CNAEXT.
     *
     * The generated shader is one uber-shader per vertex layout rather than one per stock-effect
     * permutation: the permutation is a uniform, not a `#define`. That keeps the pipeline cache
     * proportional to the vertex layouts a game actually uses instead of to the product of every
     * effect switch, at the cost of some dynamic branching -- a deliberate trade, documented in
     * docs/igl-renderer.md, for a renderer whose goal is XNA feature coverage.
     */
    namespace EffectFeature
    {
        /** @brief Sample `uTexture0` and modulate by it. */
        inline constexpr std::int32_t TextureEnabled = 1 << 0;
        /** @brief Modulate by the interpolated vertex colour. */
        inline constexpr std::int32_t VertexColorEnabled = 1 << 1;
        /** @brief Evaluate the three directional lights. */
        inline constexpr std::int32_t LightingEnabled = 1 << 2;
        /** @brief Evaluate lighting in the fragment stage instead of the vertex stage. */
        inline constexpr std::int32_t PerPixelLighting = 1 << 3;
        /** @brief Blend towards the fog colour using `uFogVector`. */
        inline constexpr std::int32_t FogEnabled = 1 << 4;
        /** @brief Modulate by `uTexture1 * 2`, DualTextureEffect's overlay. */
        inline constexpr std::int32_t DualTexture = 1 << 5;
        /** @brief Blend in a reflection sampled from `uEnvMap`. */
        inline constexpr std::int32_t EnvMapping = 1 << 6;
        /** @brief Weight the env-map blend by a Fresnel term. */
        inline constexpr std::int32_t FresnelEnabled = 1 << 7;
        /** @brief Transform position and normal by the bone palette. */
        inline constexpr std::int32_t Skinned = 1 << 8;
        /** @brief Shade with the metallic-roughness BRDF instead of Blinn-Phong. */
        inline constexpr std::int32_t Pbr = 1 << 9;
        /** @brief Apply the XNA alpha-test rule and `discard` on failure. */
        inline constexpr std::int32_t AlphaTestEnabled = 1 << 10;
        /** @brief Apply CNA's fixed ColorMatrixEffect to the final 2D colour. */
        inline constexpr std::int32_t ColorMatrix = 1 << 11;
        /** @brief Perturb the shading normal with `uNormalMap`. */
        inline constexpr std::int32_t NormalMap = 1 << 12;
        /** @brief Modulate PBR metallic/roughness by `uMetallicRoughnessMap`. */
        inline constexpr std::int32_t MetallicRoughnessMap = 1 << 13;
        /** @brief Add `uEmissiveMap`. */
        inline constexpr std::int32_t EmissiveMap = 1 << 14;
        /** @brief Darken ambient by `uOcclusionMap`. */
        inline constexpr std::int32_t OcclusionMap = 1 << 15;
        /** @brief Sample `KHR_materials_specular`'s scalar strength from `uSpecularMap`. */
        inline constexpr std::int32_t SpecularMap = 1 << 16;
        /** @brief Sample `KHR_materials_specular`'s colour from `uSpecularColorMap`. */
        inline constexpr std::int32_t SpecularColorMap = 1 << 17;
        /** @brief The base-colour texture's samples are sRGB-encoded and must be decoded. */
        inline constexpr std::int32_t BaseColorSrgb = 1 << 18;
        /** @brief The emissive texture's samples are sRGB-encoded and must be decoded. */
        inline constexpr std::int32_t EmissiveSrgb = 1 << 19;
        /** @brief The specular colour texture's samples are sRGB-encoded and must be decoded. */
        inline constexpr std::int32_t SpecularColorSrgb = 1 << 20;
        /** @brief Encode the shaded PBR result back to sRGB before writing it. */
        inline constexpr std::int32_t EncodeOutputSrgb = 1 << 21;
    }

    /**
     * @brief The `CnaEffect` std140 uniform block, mirrored for the CPU side. CNAEXT.
     *
     * Every member is `vec4`- or `mat4`-shaped precisely so the C++ and GLSL layouts cannot drift:
     * std140 rounds every one of these to a 16-byte boundary anyway, so there is no implicit
     * padding for a compiler to place differently.
     */
    struct IglEffectUniforms
    {
        /** @brief `projection * view * world`, column-major. */
        float worldViewProjection[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /** @brief World matrix, column-major; positions the fragment stage shades in world space. */
        float world[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /** @brief Inverse-transpose of the world matrix, column-major, for normals. */
        float worldInverseTranspose[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /** @brief Material diffuse colour (rgb) and alpha (a). */
        float diffuseColor[4] = {1, 1, 1, 1};
        /** @brief Emissive colour (rgb) and Blinn-Phong specular exponent (a). */
        float emissiveColor[4] = {0, 0, 0, 16};
        /** @brief Material specular colour (rgb) and EnvironmentMapEffect's blend amount (a). */
        float specularColor[4] = {1, 1, 1, 0};
        /** @brief Ambient light colour (rgb) and the Fresnel exponent (a). */
        float ambientColor[4] = {0, 0, 0, 1};
        /** @brief Camera world-space position (xyz). */
        float eyePosition[4] = {0, 0, 0, 1};
        /** @brief Fog colour (rgb). */
        float fogColor[4] = {0, 0, 0, 0};
        /** @brief FNA's fog vector; `saturate(dot(objectPosition, fogVector))` is the fog factor. */
        float fogVector[4] = {0, 0, 0, 0};
        /** @brief Alpha test: reference, tolerance, pass weight, fail weight. */
        float alphaTest[4] = {0, 0, 1, 1};
        /** @brief EnvironmentMapEffect's specular tint (rgb), scaled by the env map's alpha. */
        float envMapSpecular[4] = {0, 0, 0, 0};
        /** @brief PbrEffect factors: metallic, roughness, unused, unused. */
        float pbrFactors[4] = {1, 1, 0, 0};
        /**
         * @brief glTF `normalTexture.scale`, `occlusionTexture.strength`, then padding.
         *
         * plan_gltf.md GLTF-224/GLTF-225. Both are core glTF 2.0 material inputs, not extensions:
         * a renderer that ignores them draws an authored material with different semantics.
         */
        float pbrScales[4] = {1, 1, 0, 0};
        /**
         * @brief Dielectric normal-incidence reflectance (rgb) and grazing reflectance (a).
         *
         * `KHR_materials_ior` and the factor-only half of `KHR_materials_specular`, already
         * clamped and weighted on the CPU. Core glTF's default is 0.04 with a grazing weight of 1.
         */
        float pbrDielectricFresnel[4] = {0.04f, 0.04f, 0.04f, 1.0f};
        /**
         * @brief Pre-clamp dielectric F0 (rgb) and the authored specular factor (a).
         *
         * `specularColorTexture` multiplies BEFORE the specification's per-channel clamp, so a
         * shader handed the already-clamped value above cannot reproduce the extension.
         */
        float pbrSpecularInputs[4] = {0.04f, 0.04f, 0.04f, 1.0f};
        /**
         * @brief Two affine rows per core PBR map: base colour, normal, metallic-roughness,
         *        emissive, occlusion. `KHR_texture_transform`, identity by default.
         */
        float pbrTextureTransform[10][4] = {
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0},
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0},
            {1,0,0,0}, {0,1,0,0}};
        /** @brief The same two rows for the specular strength map, then the specular colour map. */
        float pbrSpecularTextureTransform[4][4] = {
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0}};
        /** @brief World-space, pre-normalized directions of the three directional lights. */
        float lightDirection[3][4] = {{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}};
        /** @brief Diffuse colours of the three directional lights; zero when a light is disabled. */
        float lightDiffuse[3][4] = {{1,1,1,0}, {0,0,0,0}, {0,0,0,0}};
        /** @brief Specular colours of the three directional lights; zero when disabled. */
        float lightSpecular[3][4] = {{0,0,0,0}, {0,0,0,0}, {0,0,0,0}};
        /** @brief Rows of CNA's fixed ColorMatrixEffect, applied on the 2D path only. */
        float colorMatrix[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}};
        /** @brief Constant added after @ref colorMatrix. */
        float colorOffset[4] = {0, 0, 0, 0};
        /**
         * @brief x = @ref EffectFeature bitmask, y = weights per vertex, z = bone count,
         *        w = the seven-bit PBR texture-coordinate-set mask.
         *
         * plan_gltf.md GLTF-182/GLTF-183: bit i of `w` selects `TEXCOORD_1` for PBR texture slot i
         * -- base colour, normal, metallic-roughness, emissive, occlusion, specular strength,
         * specular colour -- and a clear bit selects `TEXCOORD_0`.
         */
        std::int32_t flags[4] = {0, 4, 0, 0};
    };

    /** @brief Maximum bones a SkinnedEffect draw can carry, matching XNA's own limit. */
    inline constexpr int kMaxBones = 72;

    /** @brief The `CnaBones` std140 uniform block, mirrored for the CPU side. CNAEXT. */
    struct IglBoneUniforms
    {
        /** @brief Column-major bone matrices; entries past `flags[2]` are never read. */
        float bones[kMaxBones * 16] = {};
    };

    /** @brief A generated vertex/fragment source pair. CNAEXT. */
    struct IglShaderSources
    {
        /** @brief Vertex shader source, ready for `igl::ShaderStagesCreator`. */
        std::string vertex;
        /** @brief Fragment shader source, ready for `igl::ShaderStagesCreator`. */
        std::string fragment;
    };

    /**
     * @brief Generates the uber-effect shader pair for one vertex layout.
     *
     * Only the attributes present in @p attributeMask are declared, because a Vulkan pipeline
     * rejects a shader input with no matching vertex-input attribute. Everything else the stock
     * effects vary -- lighting, fog, texturing, skinning, PBR -- is a uniform, not a variant.
     *
     * @param attributeMask        Bitwise OR of `VertexAttributeBit(VertexAttributeSlot::…)`.
     * @param vulkan               True to emit Vulkan GLSL (explicit descriptor sets), false to
     *                             emit desktop OpenGL GLSL.
     * @param colorAttachmentCount How many `out vec4` slots the fragment stage declares; a Vulkan
     *                             pipeline requires this to match the render pass, so it is part of
     *                             the variant rather than a fixed four.
     * @return The generated sources.
     */
    [[nodiscard]] IglShaderSources BuildEffectShaderSources(std::uint32_t attributeMask,
                                                            bool vulkan,
                                                            int colorAttachmentCount);

    /**
     * @brief Prepares a game-authored `ShaderEffect` source pair for the selected backend.
     *
     * CNA's `ShaderEffect` hands renderers GLSL written against the OpenGL family's own conventions.
     * This adds the version directive and, on Vulkan, the descriptor-set decorations IGL's glslang
     * front end requires; it deliberately does not rewrite the body, so a shader this renderer
     * cannot express fails with a real compiler diagnostic rather than being silently mangled.
     *
     * @param vertexSource   Game-supplied vertex shader source.
     * @param fragmentSource Game-supplied fragment shader source.
     * @param vulkan         True when targeting the Vulkan backend.
     * @return The adapted sources.
     */
    [[nodiscard]] IglShaderSources AdaptCustomShaderSources(const std::string& vertexSource,
                                                            const std::string& fragmentSource,
                                                            bool vulkan);

    /**
     * @brief Returns the GLSL name of the sampler bound to @p unit.
     *
     * The OpenGL backend resolves sampler uniforms by name (`fragmentUnitSamplerMap`), so the
     * generated shader's names are part of this renderer's internal contract rather than cosmetic.
     *
     * @param unit A @ref TextureUnit value.
     * @return The sampler's declared name, or an empty string for an unknown unit.
     */
    [[nodiscard]] const char* GetSamplerUniformName(std::uint32_t unit);

    /**
     * @brief Returns the GLSL block name bound at @p binding.
     *
     * @param binding A @ref UniformBufferBinding value.
     * @return The block's declared name, or an empty string for an unknown binding.
     */
    [[nodiscard]] const char* GetUniformBlockName(std::uint32_t binding);
}
