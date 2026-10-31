// SPDX-License-Identifier: MS-PL
//
// GLTF-163/184/373/374/379/394: repository-wide renderer contracts are tested from the
// renderer sources even when the current host cannot compile or execute a particular backend.
// This covers 32-bit index-factory ownership as well as PBR texture bindings, packed-channel
// semantics and neutral fallbacks.

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "GltfFixtureCorpus.hpp"

namespace
{
    std::filesystem::path RepositoryRoot()
    {
        const std::filesystem::path corpus = CnaTest::GltfOracle::CorpusDirectory();
        if (corpus.empty()) { return {}; }
        return corpus.parent_path().parent_path().parent_path();
    }

    bool IsPolicySource(const std::filesystem::path& path)
    {
        const std::string extension = path.extension().string();
        return extension == ".cpp" || extension == ".mm" || extension == ".hpp"
            || extension == ".h" || extension == ".glsl" || extension == ".hlsl"
            || extension == ".sc" || extension == ".wgsl";
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    std::string Normalize(std::string text)
    {
        text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), text.end());
        return text;
    }

    std::size_t CountOccurrences(const std::string& source, const std::string& fragment)
    {
        std::size_t count = 0;
        for (std::size_t at = source.find(fragment); at != std::string::npos;
             at = source.find(fragment, at + fragment.size()))
            ++count;
        return count;
    }

    std::string RendererText(const std::filesystem::path& renderer)
    {
        std::string result;
        for (const char* subtree : {"src", "include"})
        {
            const std::filesystem::path root = renderer / subtree;
            if (!std::filesystem::is_directory(root)) { continue; }
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator(root))
            {
                if (!entry.is_regular_file() || !IsPolicySource(entry.path())) { continue; }
                result += ReadFile(entry.path());
                result.push_back('\n');
            }
        }
        return Normalize(std::move(result));
    }

    // HEADLESS names every texture field only to reject a resource created by another renderer;
    // it never samples a map or substitutes a semantic fallback. Keep that third disposition
    // explicit instead of teaching the PBR inventories that ownership validation is rendering.
    constexpr std::array<const char*, 1> kPbrMapValidationOnlyRenderers{{"headless"}};

    std::set<std::string> RendererNamesMentioningPbrMaps(
        const std::filesystem::path& renderers)
    {
        std::set<std::string> observed;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(renderers))
        {
            if (!entry.is_directory()) { continue; }
            const std::filesystem::path src = entry.path() / "src";
            if (!std::filesystem::is_directory(src)) { continue; }
            for (const std::filesystem::directory_entry& source :
                 std::filesystem::recursive_directory_iterator(src))
            {
                if (!source.is_regular_file() || !IsPolicySource(source.path())) { continue; }
                if (ReadFile(source.path()).find("pbrNormalMap") != std::string::npos)
                {
                    observed.insert(entry.path().filename().string());
                    break;
                }
            }
        }
        return observed;
    }

    std::set<std::string> RendererNamesConsumingPbrMaps(
        const std::filesystem::path& renderers)
    {
        std::set<std::string> observed = RendererNamesMentioningPbrMaps(renderers);
        for (const char* name : kPbrMapValidationOnlyRenderers) { observed.erase(name); }
        return observed;
    }

    /// How a renderer answers "this PBR map is absent".
    ///
    /// Two strategies, both correct, and the distinction is why this is a field rather than an
    /// assumption. A renderer that compiles one PBR shader binds a SEMANTICALLY NEUTRAL texture in
    /// the empty slot -- flat-normal `(128,128,255,255)` for the normal map, opaque white for every
    /// other -- so the sample happens and changes nothing. A renderer that compiles a shader VARIANT
    /// per feature set instead does not sample at all, which is the same result without the fetch.
    /// The audit table below states which each renderer does, so a renderer cannot be admitted to
    /// the inventory without saying how it answers the question.
    enum class PbrFallbackStrategy
    {
        NeutralTexture,
        ShaderFeatureVariant,
        CpuSemanticDefault,
    };

    struct RendererAudit
    {
        const char* name;
        const char* normal;
        const char* metallicRoughness;
        const char* emissive;
        const char* occlusion;
        PbrFallbackStrategy strategy = PbrFallbackStrategy::NeutralTexture;
    };

    // These fragments name the actual null branch (or the preselected default) for each semantic. Whitespace is ignored, but the map/fallback pairing is not: changing a normal
    // slot to white, or any other slot to the flat-normal texture, fails this table.
    constexpr std::array<RendererAudit, 6> kAudits{{
        {"directx9",
         "params.pbrNormalMap, ResolveD3D9TextureEXT(GetOrCreateDefaultFlatNormalTextureEXT())",
         "params.pbrMetallicRoughnessMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT())",
         "params.pbrEmissiveMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT())",
         "params.pbrOcclusionMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT())"},
        {"easygl",
         "params.pbrNormalMap->BindGL(1); else default_flat_normal_texture_.active_bind",
         "params.pbrMetallicRoughnessMap->BindGL(2); else default_white_texture_.active_bind",
         "params.pbrEmissiveMap->BindGL(3); else default_white_texture_.active_bind",
         "params.pbrOcclusionMap->BindGL(4); else default_white_texture_.active_bind"},
        {"fna3d",
         "params.pbrNormalMap != nullptr ? params.pbrNormalMap : flatNormal",
         "params.pbrMetallicRoughnessMap != nullptr ? params.pbrMetallicRoughnessMap : white",
         "params.pbrEmissiveMap != nullptr ? params.pbrEmissiveMap : white",
         "params.pbrOcclusionMap != nullptr ? params.pbrOcclusionMap : white"},
        {"sdl-gpu",
         "command.normalMap ? command.normalMap.texture : defaultFlatNormalTexture_->Texture()",
         "command.metallicRoughnessMap ? command.metallicRoughnessMap.texture : defaultWhiteTexture_->Texture()",
         "command.emissiveMap ? command.emissiveMap.texture : defaultWhiteTexture_->Texture()",
         "command.occlusionMap ? command.occlusionMap.texture : defaultWhiteTexture_->Texture()"},
        {"software",
         "float normalR = 0.5f, normalG = 0.5f, normalB = 1.0f, normalA = 1.0f; samplePbrMap(0, 1",
         "float mrR = 1.0f, mrG = 1.0f, mrB = 1.0f, mrA = 1.0f; samplePbrMap(1, 2",
         "float emissiveR = 1.0f, emissiveG = 1.0f; float emissiveB = 1.0f, emissiveA = 1.0f; samplePbrMap(2, 3",
         "float occlusionR = 1.0f, occlusionG = 1.0f; float occlusionB = 1.0f, occlusionA = 1.0f; samplePbrMap(3, 4",
         PbrFallbackStrategy::CpuSemanticDefault},
        {"webgpu",
         "params.pbrNormalMap != nullptr ? ResolveSamplable(params.pbrNormalMap) : pbrDefaultFlatNormalTexture_->Sampled()",
         "params.pbrMetallicRoughnessMap != nullptr ? ResolveSamplable(params.pbrMetallicRoughnessMap) : pbrDefaultWhiteTexture_->Sampled()",
         "params.pbrEmissiveMap != nullptr ? ResolveSamplable(params.pbrEmissiveMap) : pbrDefaultWhiteTexture_->Sampled()",
         "params.pbrOcclusionMap != nullptr ? ResolveSamplable(params.pbrOcclusionMap) : pbrDefaultWhiteTexture_->Sampled()"},
    }};

    struct RendererSlotAudit
    {
        const char* name;
        const char* abi;
        // Together these fragments form the CPU-field -> native binding -> shader-declaration
        // chain. Keeping the fragments renderer-specific is intentional: a WebGPU bind-group
        // binding and a Direct3D HLSL resource register are not interchangeable kinds of "unit".
        std::array<const char*, 18> evidence;
    };

    constexpr std::array<RendererSlotAudit, 6> kSlotAudits{{
        {"directx9", "sampler registers s0 through s6",
         {{R"(BindPbrSampler(device_.Get(), 0, params.texture0, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 1, params.pbrNormalMap, ResolveD3D9TextureEXT(GetOrCreateDefaultFlatNormalTextureEXT()));
              BindPbrSampler(device_.Get(), 2, params.pbrMetallicRoughnessMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 3, params.pbrEmissiveMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 4, params.pbrOcclusionMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 5, params.pbrSpecularMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 6, params.pbrSpecularColorMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));)",
           R"(sampler2D Texture : register(s0);
              sampler2D NormalMap : register(s1);
              sampler2D MetallicRoughnessMap : register(s2);
              sampler2D EmissiveMap : register(s3);
              sampler2D OcclusionMap : register(s4);
              sampler2D SpecularMap : register(s5);
              sampler2D SpecularColorMap : register(s6);)"}}},
        {"easygl", "GL texture units 0,1,2,3,4",
         {{R"(p.prog.set_uniform(p.loc_texture, 0))",
           R"(params.texture0->BindGL(0))",
           R"(p.prog.set_uniform(p.loc_pbr_normalmap, 1))",
           R"(params.pbrNormalMap->BindGL(1))",
           R"(p.prog.set_uniform(p.loc_pbr_mr, 2))",
           R"(params.pbrMetallicRoughnessMap->BindGL(2))",
           R"(p.prog.set_uniform(p.loc_pbr_emissivemap, 3))",
           R"(params.pbrEmissiveMap->BindGL(3))",
           R"(p.prog.set_uniform(p.loc_pbr_occlusionmap, 4))",
           R"(params.pbrOcclusionMap->BindGL(4))",
           R"(uniform sampler2D uTexture;)",
           R"(uniform sampler2D uNormalMap;)",
           R"(uniform sampler2D uMetallicRoughnessMap;)",
           R"(uniform sampler2D uEmissiveMap;)",
           R"(uniform sampler2D uOcclusionMap;)"}}},
        {"fna3d", "applied Effect Framework sampler registers s0 through s6",
         {{R"(bindPbrTexture("Texture", params.texture0 != nullptr ? params.texture0 : white);
              bindPbrTexture("NormalMap", params.pbrNormalMap != nullptr ? params.pbrNormalMap : flatNormal);
              bindPbrTexture("MetallicRoughnessMap", params.pbrMetallicRoughnessMap != nullptr ? params.pbrMetallicRoughnessMap : white);
              bindPbrTexture("EmissiveMap", params.pbrEmissiveMap != nullptr ? params.pbrEmissiveMap : white);
              bindPbrTexture("OcclusionMap", params.pbrOcclusionMap != nullptr ? params.pbrOcclusionMap : white);
              bindPbrTexture("SpecularMap", params.pbrSpecularMap != nullptr ? params.pbrSpecularMap : white);
              bindPbrTexture("SpecularColorMap", params.pbrSpecularColorMap != nullptr ? params.pbrSpecularColorMap : white);)",
           R"(const int slot = effect.FindPixelSamplerRegister(samplerName);)",
           R"(sampler2D Texture : register(s0);
              sampler2D NormalMap : register(s1);
              sampler2D MetallicRoughnessMap : register(s2);
              sampler2D EmissiveMap : register(s3);
              sampler2D OcclusionMap : register(s4);
              sampler2D SpecularMap : register(s5);
              sampler2D SpecularColorMap : register(s6);)"}}},
        {"sdl-gpu", "fragment sampler bindings 0 through 6",
         {{R"(samplerBindings[0].texture = command.texture ? command.texture.texture
                                                    : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[1].texture = command.normalMap ? command.normalMap.texture : defaultFlatNormalTexture_->Texture())",
           R"(samplerBindings[2].texture = command.metallicRoughnessMap ? command.metallicRoughnessMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[3].texture = command.emissiveMap ? command.emissiveMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[4].texture = command.occlusionMap ? command.occlusionMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[5].texture = command.specularMap
                  ? command.specularMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[6].texture = command.specularColorMap
                  ? command.specularColorMap.texture : defaultWhiteTexture_->Texture())",
           R"(SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 7))",
           R"(layout(set = 2, binding = 0) uniform sampler2D uTexture;
              layout(set = 2, binding = 1) uniform sampler2D uNormalMap;
              layout(set = 2, binding = 2) uniform sampler2D uMetallicRoughnessMap;
              layout(set = 2, binding = 3) uniform sampler2D uEmissiveMap;
              layout(set = 2, binding = 4) uniform sampler2D uOcclusionMap;
              layout(set = 2, binding = 5) uniform sampler2D uSpecularMap;
              layout(set = 2, binding = 6) uniform sampler2D uSpecularColorMap;)"}}},
        {"software", "CPU colour-surface array entries 0 through 5",
         {{R"(const std::array<const SoftwareColorSurface*, 6> pbrTextures = {
                dynamic_cast<const SoftwareColorSurface*>(params.pbrNormalMap),
                dynamic_cast<const SoftwareColorSurface*>(params.pbrMetallicRoughnessMap),
                dynamic_cast<const SoftwareColorSurface*>(params.pbrEmissiveMap),
                dynamic_cast<const SoftwareColorSurface*>(params.pbrOcclusionMap),
                dynamic_cast<const SoftwareColorSurface*>(params.pbrSpecularMap),
                dynamic_cast<const SoftwareColorSurface*>(params.pbrSpecularColorMap)})",
           R"(samplePbrMap(0, 1, normalR, normalG, normalB, normalA))",
           R"(samplePbrMap(1, 2, mrR, mrG, mrB, mrA))",
           R"(samplePbrMap(2, 3, emissiveR, emissiveG, emissiveB, emissiveA))",
           R"(samplePbrMap(3, 4, occlusionR, occlusionG, occlusionB, occlusionA))",
           R"(samplePbrMap(4, 5, specularMapR, specularMapG, specularMapB, specularMapA))",
           R"(samplePbrMap(5, 6, specularColorR, specularColorG, specularColorB, specularColorA))"}}},
        {"webgpu", "bind group 1: sampler 0, textures 1,2,3,4,5",
         {{R"(AcquirePbrTextureBindingEXT(
            sampler, command.baseColorTexture, command.normalMap, command.metallicRoughnessMap,
            command.emissiveMap, command.occlusionMap, command.specularMap,
            command.specularColorMap,)",
           R"(const std::array<const WebGPUSampledTextureEXT*, 7> maps{
            &baseColor, &normal, &metallicRoughness, &emissive, &occlusion, &specular,
            &specularColor};)",
           R"(entries[i + 1].binding = static_cast<std::uint32_t>(i + 1);
            entries[i + 1].textureView = maps[i]->View();)",
           R"(@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
              @group(1) @binding(2) var normalTex: texture_2d<f32>;
              @group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
              @group(1) @binding(4) var emissiveTex: texture_2d<f32>;
              @group(1) @binding(5) var occlusionTex: texture_2d<f32>;)",
           R"(command.normalMap = params.pbrNormalMap != nullptr)",
           R"(command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr)",
           R"(command.emissiveMap = params.pbrEmissiveMap != nullptr)",
           R"(command.occlusionMap = params.pbrOcclusionMap != nullptr)"}}},
    }};

    struct RendererAlphaAudit
    {
        const char* name;
        // A renderer passes only when the PBR draw uploads the four-component XNA alpha-test
        // encoding and the PBR fragment path itself consumes it. Merely having a separate
        // AlphaTestEffect shader is deliberately insufficient: glTF MASK materials must retain
        // their PBR vertex layout, material maps and BRDF while rejecting uncovered fragments.
        std::array<const char*, 4> evidence;
    };

    constexpr std::array<RendererAlphaAudit, 6> kAlphaAudits{{
        {"directx9", {{
            R"(TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "AlphaTest", params.alphaTest))",
            R"(float4 AlphaTest : register(c11))",
            R"(float alphaTestResult = (AlphaTest.y > 0.0))",
            R"(clip(alphaTestResult))"}}},
        {"easygl", {{
            R"(p.prog.set_uniform(p.loc_alphatest, params.alphaTest[0], params.alphaTest[1], params.alphaTest[2], params.alphaTest[3]))",
            R"("uniform vec4 uAlphaTest;\n")",
            R"(float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w))",
            R"("    if(_at<0.0)discard;\n")"}}},
        {"fna3d", {{
            R"(effect.SetFloats("AlphaTest", params.alphaTest, 4))",
            R"(float4 AlphaTest : register(c11))",
            R"(float alphaTestResult = (AlphaTest.y > 0.0))",
            R"(clip(alphaTestResult))"}}},
        {"sdl-gpu", {{
            R"(out[4] = p.alphaTest[0])",
            R"(const bool needsAlphaTest = !params.pbr && (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f))",
            R"(bool passesAlphaTest = (pbrp.alphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? pbrp.alphaTest.z : pbrp.alphaTest.w) < 0.0) discard;)"}}},
        {"software", {{
            R"(const bool comparison = params.alphaTest[1] > 0.0f)",
            R"(const float clipWeight = comparison ? params.alphaTest[2] : params.alphaTest[3])",
            R"(if (!AlphaTestPasses(ctx.params, a)) return)",
            R"(if (ctx.params.pbr))"}}},
        {"webgpu", {{
            R"(out[4] = p.alphaTest[0])",
            R"(const bool needsAlphaTest = !params.pbr)",
            R"(let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest))",
            R"(if (alphaWeight < 0.0) { discard;)"}}},
    }};

    struct RendererPbrScalarAudit
    {
        const char* name;
        // Evidence covers both CPU-side values and both glTF shader equations: normalTexture.scale
        // affects tangent-space X/Y only, while occlusionTexture.strength interpolates from 1.
        std::array<const char*, 4> evidence;
    };

    constexpr std::array<RendererPbrScalarAudit, 6> kPbrScalarAudits{{
        {"directx9", {{
            R"(params.pbrNormalScale, params.pbrOcclusionStrength})",
            R"(float4 MetallicRoughnessFactor : register(c3))",
            R"(sampledNormal.xy *= MetallicRoughnessFactor.z;)",
            R"(occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);)"}}},
        {"easygl", {{
            R"(p.prog.set_uniform(p.loc_pbr_normalscale, params.pbrNormalScale))",
            R"(p.prog.set_uniform(p.loc_pbr_occlstrength, params.pbrOcclusionStrength))",
            R"("    sampledNormal.xy*=uNormalScale;\n")",
            R"("    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n")"}}},
        {"fna3d", {{
            R"(params.pbrNormalScale, params.pbrOcclusionStrength)",
            R"(float4 MetallicRoughnessFactor : register(c3))",
            R"(sampledNormal.xy *= MetallicRoughnessFactor.z;)",
            R"(occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);)"}}},
        {"sdl-gpu", {{
            R"(out[2] = p.pbrNormalScale;)",
            R"(out[3] = p.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= pbrp.normalScale;)",
            R"(float occlusion = 1.0 + pbrp.occlusionStrength * (occlusionSample - 1.0);)"}}},
        {"software", {{
            R"((normalR * 2.0f - 1.0f) * ctx.params.pbrNormalScale)",
            R"((normalG * 2.0f - 1.0f) * ctx.params.pbrNormalScale)",
            R"(const float occlusion = 1.0f + ctx.params.pbrOcclusionStrength *)",
            R"((occlusionR - 1.0f))"}}},
        {"webgpu", {{
            R"(out[2] = p.pbrNormalScale;)",
            R"(out[3] = p.pbrOcclusionStrength;)",
            R"(sampledNormal.x *= pf.metallicRoughness.z; sampledNormal.y *= pf.metallicRoughness.z;)",
            R"(let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);)"}}},
    }};

    struct RendererPbrTextureTransformAudit
    {
        const char* name;
        const char* cpuUpload;
        const char* secondAffineRow;
        // Base color, normal, metallic-roughness, emissive and occlusion, in public draw-field
        // order. Each fragment names both the map and its fixed transform slot.
        std::array<const char*, 5> samples;
        std::size_t shaderCopies;
    };

    // GLTF-184: KHR_texture_transform is transported as two affine rows for each of the five PBR
    // maps. A renderer passes only when it uploads the public draw field, evaluates both rows, and
    // applies slots 0/1/2/3/4 to the corresponding samples. The minimum copy count keeps separately
    // stored rigid/skinned shader variants in lockstep without depending on generated-header copies.
    constexpr std::array<RendererPbrTextureTransformAudit, 6> kPbrTextureTransformAudits{{
        {"directx9",
         "TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, \"TextureTransformRows\", &params.pbrTextureTransformRows[0][0])",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"tex2D(Texture, CnaPbrTransformUv(CnaPbrUv(pin, 0), 0))",
           "tex2D(NormalMap, CnaPbrTransformUv(CnaPbrUv(pin, 1), 1))",
           "tex2D(MetallicRoughnessMap, CnaPbrTransformUv(CnaPbrUv(pin, 2), 2))",
           "tex2D(EmissiveMap, CnaPbrTransformUv(CnaPbrUv(pin, 3), 3))",
           "tex2D(OcclusionMap, CnaPbrTransformUv(CnaPbrUv(pin, 4), 4))"}}, 2},
        {"easygl",
         "const float* values = params.pbrTextureTransformRows[row]",
         "uTextureTransformRows[slot*2+1].xyz",
         {{"texture(uTexture,cnaSampleUV(cnaPbrTransformUV(\" + baseUv + \",0),uRtFlipV.x))",
           "texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(\" + normalUv + \",1),uRtFlipV.y))",
           "texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrTransformUV(\" + mrUv + \",2),uRtFlipV.z))",
           "texture(uEmissiveMap,cnaSampleUV(cnaPbrTransformUV(\" + emissiveUv + \",3),uRtFlipV.w))",
           "texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(\" + occlusionUv + \",4),uRtFlipVHi.x))"}}, 2},
        {"fna3d",
         "effect.SetFloats(\"TextureTransformRows\", &params.pbrTextureTransformRows[0][0], 40)",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"tex2D(Texture, CnaPbrTransformUv(CnaPbrUv(pin, 0), 0))",
           "tex2D(NormalMap, CnaPbrTransformUv(CnaPbrUv(pin, 1), 1))",
           "tex2D(MetallicRoughnessMap, CnaPbrTransformUv(CnaPbrUv(pin, 2), 2))",
           "tex2D(EmissiveMap, CnaPbrTransformUv(CnaPbrUv(pin, 3), 3))",
           "tex2D(OcclusionMap, CnaPbrTransformUv(CnaPbrUv(pin, 4), 4))"}}, 2},
        // STREETS-0006: the LOD biases moved into PbrParams (SMG-0032).
        {"sdl-gpu",
         "p.pbrTextureTransformRows[row][component]",
         "pbrp.textureTransformRows[slot * 2 + 1].xyz",
         {{"texture(uTexture, cnaPbrTransformUV(fragUV, 0), pbrp.lodBias0To3.x)",
           "texture(uNormalMap, cnaPbrTransformUV(fragUV, 1), pbrp.lodBias0To3.y)",
           "texture(uMetallicRoughnessMap, cnaPbrTransformUV(fragUV, 2), pbrp.lodBias0To3.z)",
           "texture(uEmissiveMap, cnaPbrTransformUV(fragUV, 3), pbrp.lodBias0To3.w)",
           "texture(uOcclusionMap, cnaPbrTransformUV(fragUV, 4), pbrp.lodBias4To7.x)"}}, 1},
        {"software",
         "const float (*rows)[4] = textureSlot < 5 ? &params.pbrTextureTransformRows[textureSlot * 2]",
         "transformedV = u * rows[1][0] + v * rows[1][1] + rows[1][2]",
         {{"TransformPbrUv(ctx.params, 0, u, v, u1, v1, sampleU, sampleV)",
           "samplePbrMap(0, 1, normalR, normalG, normalB, normalA)",
           "samplePbrMap(1, 2, mrR, mrG, mrB, mrA)",
           "samplePbrMap(2, 3, emissiveR, emissiveG, emissiveB, emissiveA)",
           "samplePbrMap(3, 4, occlusionR, occlusionG, occlusionB, occlusionA)"}}, 1},
        {"webgpu",
         "p.pbrTextureTransformRows[row][component]",
         "pf.textureTransformRows[slot * 2u + 1u].xyz",
         {{"textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u))",
           "textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u))",
           "textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u))",
           "textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u))",
           "textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u))"}}, 2},
    }};

    struct RendererPbrFresnelAudit
    {
        const char* name;
        const char* dielectricF0;
        const char* dielectricF90;
        const char* schlickEndpoints;
        std::size_t shaderCopies;
    };

    // GLTF-343/344: both public draw fields must reach every PBR shader, and Schlick must use the
    // transported grazing endpoint instead of silently rebuilding core glTF's constant F90=1.
    // The copy count distinguishes separately stored rigid/skinned fragment programs from backends
    // that share one fragment program.
    constexpr std::array<RendererPbrFresnelAudit, 6> kPbrFresnelAudits{{
        {"directx9",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"easygl",
         "vec3 F0=mix(dielectricF0,albedo,metallic)",
         "vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic)",
         "vec3 F=F0+(F90-F0)*", 2},
        {"fna3d",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"sdl-gpu",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"software",
         "f0[channel] = dielectricF0[channel] * (1.0f - metallic) + albedo[channel] * metallic",
         "f90[channel] = specularWeight * (1.0f - metallic) + metallic",
         "const float fresnel = f0[channel] + (f90[channel] - f0[channel]) * fresnelWeight", 1},
        {"webgpu",
         "let f0 = mix(dielectricF0, albedo, metallic)",
         "let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic)",
         "let f = f0 + (f90 - f0) *", 2},
    }};

    struct RendererPbrColorSpaceAudit
    {
        const char* name;
        const char* baseDecode;
        const char* emissiveDecode;
        const char* outputEncode;
        std::size_t shaderCopies;
    };

    // A shared fragment program needs one copy. Backends that compile distinct rigid and skinned
    // fragment sources need two, so one correct variant cannot hide a stale sibling in aggregate
    // source text. CPU evidence is checked independently below for all three public draw flags.
    constexpr std::array<RendererPbrColorSpaceAudit, 6> kPbrColorSpaceAudits{{
        {"directx9",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), AmbientColor.w)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), EmissiveColor.w)",
         "lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w)", 2},
        {"easygl",
         "mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x)",
         "mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y)",
         "mix(FragColor.rgb,cnaLinearToSrgb(FragColor.rgb),uSrgb.z)", 2},
        {"fna3d",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), AmbientColor.w)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), EmissiveColor.w)",
         "lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w)", 2},
        {"sdl-gpu",
         "mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), pbrp.srgbFlags.x)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), pbrp.srgbFlags.y)",
         "mix(outColor.rgb, cnaLinearToSrgb(outColor.rgb), pbrp.srgbFlags.z)", 1},
        {"software",
         "ctx.params.pbrBaseColorTextureIsSrgb",
         "ctx.params.pbrEmissiveTextureIsSrgb",
         "ctx.params.pbrEncodeOutputToSrgb", 1},
        {"webgpu",
         "select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5)",
         "select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5)",
         "select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5)", 2},
    }};

    struct RendererPbrChannelAudit
    {
        const char* name;
        const char* normalRgbRemap;
        const char* roughnessGreen;
        const char* metallicBlue;
        const char* occlusionRed;
        std::size_t shaderCopies;
    };

    // glTF packs roughness into G and metallic into B; occlusion is R and a tangent-space normal
    // consumes all RGB before the [0,1] -> [-1,1] remap. The count distinguishes backends with
    // separately stored rigid/skinned fragments from those whose one fragment program is shared by
    // both vertex paths.
    // EasyGL constructs legacy and dual-UV programs from the same two rigid/skinned builders, so
    // its evidence deliberately includes the selected-UV variable rather than pretending the
    // final shader still contains a hard-coded vUV expression.
    constexpr std::array<RendererPbrChannelAudit, 6> kPbrChannelAudits{{
        {"directx9",
         "tex2D(NormalMap, CnaPbrTransformUv(CnaPbrUv(pin, 1), 1)).rgb * 2.0 - 1.0",
         "mr.g * MetallicRoughnessFactor.y",
         "mr.b * MetallicRoughnessFactor.x",
         "tex2D(OcclusionMap, CnaPbrTransformUv(CnaPbrUv(pin, 4), 4)).r", 2},
        {"easygl",
         "texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(\" + normalUv + \",1),uRtFlipV.y)).rgb*2.0-1.0",
         "mr.g*uRoughnessFactor",
         "mr.b*uMetallicFactor",
         "texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(\" + occlusionUv + \",4),uRtFlipVHi.x)).r", 2},
        {"fna3d",
         "tex2D(NormalMap, CnaPbrTransformUv(CnaPbrUv(pin, 1), 1)).rgb * 2.0 - 1.0",
         "mr.g * MetallicRoughnessFactor.y",
         "mr.b * MetallicRoughnessFactor.x",
         "tex2D(OcclusionMap, CnaPbrTransformUv(CnaPbrUv(pin, 4), 4)).r", 2},
        {"sdl-gpu",
         "texture(uNormalMap, cnaPbrTransformUV(fragUV, 1), pbrp.lodBias0To3.y).rgb * 2.0 - 1.0",
         "mr.g * pbrp.roughnessFactor",
         "mr.b * pbrp.metallicFactor",
         "texture(uOcclusionMap, cnaPbrTransformUV(fragUV, 4), pbrp.lodBias4To7.x).r", 1},
        {"software",
         "const float tangentNormalX = (normalR * 2.0f - 1.0f)",
         "mrG * ctx.params.pbrRoughnessFactor",
         "mrB * ctx.params.pbrMetallicFactor",
         "(occlusionR - 1.0f)", 1},
        {"webgpu",
         "textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0",
         "mr.g * pf.metallicRoughness.y",
         "mr.b * pf.metallicRoughness.x",
         "textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r", 2},
    }};

    struct RendererPbrMaterialFactorAudit
    {
        const char* name;
        const char* baseRgbFactor;
        const char* baseAlphaFactor;
        const char* emissiveFactor;
        std::size_t shaderCopies;
    };

    // GLTF-216/218/220/221/223/379: base-colour RGB and alpha are independent linear factors,
    // emissive is a separate additive factor, and the MR factors are already locked beside their
    // G/B channel reads in kPbrChannelAudits. Copy counts keep separately stored rigid/skinned
    // fragments honest.
    constexpr std::array<RendererPbrMaterialFactorAudit, 6> kPbrMaterialFactorAudits{{
        {"directx9",
         "float3 albedo = baseColor * DiffuseColor.rgb",
         "float alpha = baseColorTex.a * DiffuseColor.a",
         "float3 emissive = EmissiveColor.xyz * emissiveSample", 2},
        {"easygl",
         "vec3 albedo=baseRGB*uDiffuseColor.rgb",
         "float alpha=baseColorTex.a*uDiffuseColor.a",
         "vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y)", 2},
        {"fna3d",
         "float3 albedo = baseColor * DiffuseColor.rgb",
         "float alpha = baseColorTex.a * DiffuseColor.a",
         "float3 emissive = EmissiveColor.xyz * emissiveSample", 2},
        {"sdl-gpu",
         "vec3 albedo = baseColor * pc.diffuseColor.rgb",
         "float alpha = baseColorTex.a * pc.diffuseColor.a",
         "vec3 emissive = lp.emissiveColor_pad.xyz * emissiveSample", 1},
        {"software",
         "r * ctx.params.diffuseColor[0]",
         "a *= ctx.params.diffuseColor[3]",
         "ctx.params.emissiveColor[0] * emissiveR", 1},
        {"webgpu",
         "let albedo = baseColor * u.diffuseColor.rgb",
         "let alpha = baseColorSample.a * u.diffuseColor.a",
         "let emissive = lp.emissiveColor.xyz * emissiveLinear", 2},
    }};

    struct RendererPbrTransformAudit
    {
        const char* name;
        const char* mvpCompose;
        const char* mvpUpload;
        const char* worldUpload;
        const char* rigidClipPosition;
        const char* skinnedClipPosition;
    };

    // GLTF-266/366/379: L6 locks the values at the effect boundary; this table locks the next
    // renderer-specific hop. Every backend must compose XNA's row-vector World*View*Projection,
    // upload it to the PBR carrier, retain World independently for world-space shading, and use
    // the combined matrix for both rigid and post-skin positions. Whitespace is deliberately
    // ignored, but each native carrier/expression remains backend-specific.
    constexpr std::array<RendererPbrTransformAudit, 6> kPbrTransformAudits{{
        {"directx9",
         "world * view * projection",
         "UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, \"WorldViewProj\", world * view * projection)",
         "UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, \"World\", world)",
         "vout.Position = mul(float4(vin.Position, 1.0), WorldViewProj)",
         "vout.Position = mul(float4(skinnedPos, 1.0), WorldViewProj)"},
        {"easygl",
         "const Matrix wvp = world * view * projection",
         "p.prog.set_uniform_matrix4(p.loc_wvp, wvp_col)",
         "p.prog.set_uniform_matrix4(p.loc_world, params.worldColMajor)",
         "gl_Position=uWVP*cnaPos",
         "gl_Position=uWVP*cnaPos"},
        {"fna3d",
         "const Matrix worldView = Matrix::Multiply(world, view)",
         "effect.SetMatrix(\"WorldViewProj\", worldViewProj)",
         "effect.SetMatrix(\"World\", world)",
         "vout.Position = mul(float4(vin.Position, 1.0), WorldViewProj)",
         "vout.Position = mul(float4(skinnedPos, 1.0), WorldViewProj)"},
        // STREETS-0001: SDLGPU compiles its PBR vertex shaders twice
        // from one source, the per-instance transform spelled CNA_INSTANCE_POSITION() (VULKAN-232's
        // scheme); same evidence, current spelling.
        {"sdl-gpu",
         "const Matrix wvp = ApplyXnaPixelCenter(world * view * projection)",
         "FillExtUniforms(command.uniforms, wvp, params)",
         "for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi]",
         "gl_Position = pc.mvp * CNA_INSTANCE_POSITION(vec4(inPos, 1.0))",
         "gl_Position = pc.mvp * skinnedPos"},
        {"software",
         "const Matrix combined = world * view * projection",
         "const Vector4 clip = Vector4::Transform(position, combined)",
         "ApplyAffineColumnMajor(params.worldColMajor, position, 1.0f)",
         "const Vector4 clip = Vector4::Transform(position, combined)",
         "position = ApplyAffineColumnMajor(blended, position, 1.0f)"},
        {"webgpu",
         "const Matrix wvp = world * view * projection",
         "FillExtUniforms(command.uniforms, wvp, params)",
         "for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi]",
         "output.position = u.mvp * vec4f(input.position, 1.0)",
         "output.position = u.mvp * skinnedPos"},
    }};

    struct RendererPbrCullAudit
    {
        const char* name;
        std::array<const char*, 5> evidence;
    };

    // GLTF-231/232/379: doubleSided deliberately stays application-owned RasterizerState. This
    // inventory locks the renderer half of that boundary: CullMode::None reaches native no-cull
    // state, and the PBR rigid/skinned route consumes that same dynamic state or pipeline key.
    constexpr std::array<RendererPbrCullAudit, 6> kPbrCullAudits{{
        {"directx9", {{
            "case CullMode::None: return D3DCULL_NONE",
            "SetRenderStateCheckedEXT(D3DRS_CULLMODE, CullModeToD3D9(cullMode)",
            "void DirectX9Renderer::DrawPbrEffectEXT(",
            "const bool skinned = params.skinned",
            "device_->DrawIndexedPrimitive(ToD3D9Topology(primitive)"}}},
        {"easygl", {{
            "if (cullMode == 0) { device.set_cull_face_enabled(false); }",
            "device.set_cull_face(cullMode == 1 ? ::easygl::CullFace::Back : ::easygl::CullFace::Front)",
            "if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned",
            "if (params.pbr) return StockProgramShape::Pbr",
            // The declaration is validated before program selection after the Software merge.
            // What this row asserts is that the caller's `params` still reach program selection.
            "Prog3D& p = SelectProgram(layoutStride, params);"}}},
        {"fna3d", {{
            "rasterizerState_.cullMode = ToFna3dCullMode(cullMode)",
            "FNA3D_ApplyRasterizerState(device_, &rasterizerState_)",
            "if (params.pbr) return params.skinned ? StockEffectKind::SkinnedPbr : StockEffectKind::Pbr",
            "const StockEffectKind kind = SelectStockEffect(params, vertexColorDeclared)",
            "FNA3D_DrawIndexedPrimitives("}}},
        {"sdl-gpu", {{
            "default: return SDL_GPU_CULLMODE_NONE",
            "cullMode_ = cullMode",
            "command.renderState = CaptureRenderState()",
            "auto& cache = skinned ? (colored ? pbrSkinnedColorPipelines_ : pbrSkinnedPipelines_)",
            "FillRasterizerState(pipelineInfo.rasterizer_state, renderState, pipelineInfo.primitive_type, depthStencilFormat)"}}},
        {"software", {{
            "bool ShouldCullTriangle(float area, int cullMode)",
            "if (ShouldCullTriangle(area, cullMode))",
            "void RasterizeTriangleShaded(",
            "int cullMode, float depthBias, float slopeScaleDepthBias",
            "const GpuDrawParams& params"}}},
        {"webgpu", {{
            "default: return WGPUCullMode_None",
            "cullMode_ = cullMode",
            "command.cullMode = cullMode_",
            "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D(",
            "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D("}}},
    }};

    struct RendererPbrSkinningAudit
    {
        const char* name;
        const char* paletteUpload;
        const char* weightCountUpload;
        const char* twoWeightGate;
        const char* fourWeightGate;
    };

    // GLTF-258/263/379: the inverse-transpose audit below proves each PBR vertex path uses its
    // blended joint matrix for directions. This adjacent inventory proves how that matrix is
    // formed: the real 72-entry palette reaches the backend, and only the first requested 1/2/4
    // influence pairs contribute. The CPU-upload fragments are PBR-specific where a backend has
    // multiple stock skinning paths; the shader gates are paired with the PBR evidence below.
    constexpr std::array<RendererPbrSkinningAudit, 6> kPbrSkinningAudits{{
        {"directx9",
         "UploadBonesVS(device_.Get(), vsRegs, vsCount, params)",
         "0.0f, 0.0f, 0.0f, static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinning += Bones[vin.BoneIndices.y] * vin.BoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinning += Bones[vin.BoneIndices.z] * vin.BoneWeights.z"},
        {"easygl",
         "glUniformMatrix4fv(::metagl::UniformLocation{p.loc_bones}, params.boneCount, 0, params.boneTransforms)",
         "p.prog.set_uniform(p.loc_weightsPerVertex, params.weightsPerVertex)",
         // FX-127 made BLENDINDICES readable as Vector4 as well as Byte4, which put an int() cast
         // around the index in EasyGL's own skinning GLSL. The rule is unchanged.
         "if(uWeightsPerVertex>=2) skinMat+=uBones[int(aBoneIndices.y)]*aBoneWeights.y",
         "if(uWeightsPerVertex>=4) skinMat+=uBones[int(aBoneIndices.z)]*aBoneWeights.z"},
        {"fna3d",
         "effect.SetMatrix4x3Array(\"Bones\", params.boneTransforms, params.boneCount)",
         "static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinning += Bones[vin.BoneIndices.y] * vin.BoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinning += Bones[vin.BoneIndices.z] * vin.BoneWeights.z"},
        {"sdl-gpu",
         "out[i] = p.boneTransforms[i]",
         "command.lightUniforms[39] = static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinMat += loadBone(inBoneIndices.y) * inBoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += loadBone(inBoneIndices.z) * inBoneWeights.z"},
        {"software",
         "const float* bone = &params.boneTransforms[static_cast<std::size_t>(boneIndex) * 16u]",
         "const int n = std::clamp(params.weightsPerVertex, 1, 4)",
         "for (int k = 0; k < n; ++k)",
         "for (int k = 0; k < n; ++k)"},
        {"webgpu",
         "out[4 + i] = p.boneTransforms[i]",
         "out[0] = static_cast<float>(p.weightsPerVertex)",
         "if (sk.weightsPerVertex.x >= 2.0)",
         "if (sk.weightsPerVertex.x >= 4.0)"},
    }};

    std::string RendererSlotText(const std::filesystem::path& renderers, const char* name)
    {
        std::string source = RendererText(renderers / name);
        // FNA3D's compiled wrappers deliberately include the DirectX 9 renderer's proven HLSL
        // instead of copying the BRDF. Audit its renderer-specific CPU upload together with those
        // two shared sources, which are the exact inputs used to produce the committed .fxb files.
        if (std::string_view(name) == "fna3d")
        {
            const std::filesystem::path shaders =
                renderers / "directx9" / "src" / "shaders" / "cna";
            source += Normalize(ReadFile(shaders / "Pbr3D.hlsl"));
            source += Normalize(ReadFile(shaders / "PbrSkinned3D.hlsl"));
            source += Normalize(ReadFile(
                renderers.parent_path() / "graphics" / "include" / "CNA" / "Internal" /
                "Graphics" / "VertexDeclarationFidelity.hpp"));
        }
        return source;
    }
}

TEST(GltfRendererPbrFallbackPolicy, InventoryCoversEveryRendererThatConsumesPbrMaps)
{
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers)) << "cannot find " << renderers;

    const std::set<std::string> observed = RendererNamesConsumingPbrMaps(renderers);

    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    EXPECT_EQ(expected, observed)
        << "a PBR renderer was added/removed without updating GLTF-374's fallback contract";
}

TEST(GltfRendererPbrFallbackPolicy, HeadlessPbrMapReferencesValidateOwnershipOnly)
{
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    const std::set<std::string> mentions = RendererNamesMentioningPbrMaps(renderers);
    ASSERT_EQ(1u, kPbrMapValidationOnlyRenderers.size());
    EXPECT_TRUE(mentions.contains(kPbrMapValidationOnlyRenderers[0]));

    const std::string source = RendererText(renderers / kPbrMapValidationOnlyRenderers[0]);
    ASSERT_FALSE(source.empty());
    EXPECT_NE(std::string::npos, source.find(Normalize("DrawResourcesBelongToHeadless(params)")));
    EXPECT_NE(std::string::npos, source.find(Normalize("IsHeadlessTextureResource(texture)")));
    EXPECT_EQ(std::string::npos, source.find("pbrNormalScale"))
        << "HEADLESS started consuming PBR material state; move it into the renderer audits";
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrMapHasItsSemanticNeutralFallback)
{
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    for (const RendererAudit& audit : kAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererText(renderers / audit.name);
        ASSERT_FALSE(source.empty());

        if (audit.strategy == PbrFallbackStrategy::NeutralTexture)
        {
            // RGBA8 cannot encode zero exactly after rgb*2-1; 128 is the conventional closest
            // value. It yields an almost exact (0,0,1), unlike white's (1,1,1), which tilts the
            // normal by 54.7 degrees and visibly changes every lit pixel.
            EXPECT_NE(std::string::npos, source.find("{128,128,255,255}"))
                << "no canonical flat-normal texel";
            EXPECT_NE(std::string::npos, source.find("{255,255,255,255}"))
                << "no canonical white texel";
        }
        else if (audit.strategy == PbrFallbackStrategy::ShaderFeatureVariant)
        {
            // A feature-variant renderer must NOT carry those texels, because a neutral texture it
            // never binds would be dead weight that later reads as a fallback nobody uses -- and it
            // must show the guard the variant is selected by.
            EXPECT_EQ(std::string::npos, source.find("{128,128,255,255}"))
                << "a shader-feature-variant renderer should not need a neutral flat-normal texel; "
                   "if it grew one, its strategy in the audit table is wrong";
            EXPECT_NE(std::string::npos, source.find(Normalize("cnaHas(CNA_NORMAL_MAP)")))
                << "no shader-side feature guard for the normal map";
        }

        for (const char* evidence :
             {audit.normal, audit.metallicRoughness, audit.emissive, audit.occlusion})
        {
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing map/fallback pairing: " << evidence;
        }
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrMapReachesTheShaderBindingIntendedByItsRenderer)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererSlotAudit& audit : kSlotAudits)
    {
        SCOPED_TRACE(std::string(audit.name) + " (" + audit.abi + ")");
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence : audit.evidence)
        {
            if (evidence == nullptr) { continue; }
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing CPU-to-shader slot evidence: " << evidence;
        }
    }
}

TEST(GltfRendererPbrFallbackPolicy, RigidAndSkinnedShaderVariantsKeepMatchingPbrBindings)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    struct ShaderPair
    {
        const char* name;
        const char* rigid;
        const char* skinned;
        const char* declarations;
    };
    constexpr std::array<ShaderPair, 1> pairs{{
        {"directx9", "directx9/src/shaders/cna/Pbr3D.hlsl",
         "directx9/src/shaders/cna/PbrSkinned3D.hlsl",
         R"(sampler2D Texture : register(s0);
            sampler2D NormalMap : register(s1);
            sampler2D MetallicRoughnessMap : register(s2);
            sampler2D EmissiveMap : register(s3);
            sampler2D OcclusionMap : register(s4);
            sampler2D SpecularMap : register(s5);
            sampler2D SpecularColorMap : register(s6);)"},
    }};

    for (const ShaderPair& pair : pairs)
    {
        SCOPED_TRACE(pair.name);
        const std::string expected = Normalize(pair.declarations);
        for (const char* relative : {pair.rigid, pair.skinned})
        {
            const std::filesystem::path path = renderers / relative;
            ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path;
            EXPECT_NE(std::string::npos, Normalize(ReadFile(path)).find(expected)) << path;
        }
    }

    // These two backends keep both programs inline in one C++ translation unit rather than in
    // separate files. Require two copies of a complete declaration block so one variant cannot
    // quietly drift while the other keeps the aggregate source search green.
    const auto countCopies = [](const std::string& source, const std::string& fragment) {
        std::size_t count = 0;
        for (std::size_t at = source.find(fragment); at != std::string::npos;
             at = source.find(fragment, at + fragment.size()))
            ++count;
        return count;
    };
    const std::string easy = RendererSlotText(renderers, "easygl");
    EXPECT_GE(countCopies(easy, Normalize(R"("uniform sampler2D uTexture;\n")")), 2u);
    for (const char* declaration :
         {R"("uniform sampler2D uNormalMap;\n")",
          R"("uniform sampler2D uMetallicRoughnessMap;\n")",
          R"("uniform sampler2D uEmissiveMap;\n")",
          R"("uniform sampler2D uOcclusionMap;\n")"})
        EXPECT_EQ(2u, countCopies(easy, Normalize(declaration))) << declaration;
    const std::string webgpu = RendererSlotText(renderers, "webgpu");
    EXPECT_GE(countCopies(webgpu, Normalize(
        "@group(1) @binding(1) var baseColorTex: texture_2d<f32>; "
        "@group(1) @binding(2) var normalTex: texture_2d<f32>; "
        "@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>; "
        "@group(1) @binding(4) var emissiveTex: texture_2d<f32>; "
        "@group(1) @binding(5) var occlusionTex: texture_2d<f32>;")), 2u);
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderConsumesTheAlphaCoverageVector)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererAlphaAudit& audit : kAlphaAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence : audit.evidence)
        {
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing PBR alpha-coverage evidence: " << evidence;
        }
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderConsumesNormalScaleAndOcclusionStrength)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrScalarAudit& audit : kPbrScalarAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence : audit.evidence)
        {
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing PBR map-scalar evidence: " << evidence;
        }
    }

    // Rigid and skinned WebGPU pipelines share one PbrFactors ABI, and its declared minimum has to
    // match the shader's own struct exactly -- wgpu rejects the pipeline by name when it does not
    // ("Buffer structure size 304 ... greater than the given min_binding_size, which is 256", which
    // is how GLTF-344's own first attempt at this was caught). It grew 56 -> 76 floats when
    // KHR_materials_specular's unclamped F0, specular factor and two transform rows landed, and
    // 76 -> 80 for the glTF doubleSided sign.
    const std::string webgpu = RendererSlotText(renderers, "webgpu");
    const std::string pbrFactorsSize = Normalize(
        "uboEntries[2].buffer.minBindingSize = 80 * sizeof(float)");
    std::size_t pbrFactorsSizeCount = 0;
    for (std::size_t at = webgpu.find(pbrFactorsSize); at != std::string::npos;
         at = webgpu.find(pbrFactorsSize, at + pbrFactorsSize.size()))
        ++pbrFactorsSizeCount;
    EXPECT_EQ(2u, pbrFactorsSizeCount)
        << "both WebGPU PBR pipeline layouts must expose the complete 320-byte factors block";
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderConsumesAllFiveTextureTransforms)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrTextureTransformAudit& audit : kPbrTextureTransformAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());

        EXPECT_NE(std::string::npos, source.find(Normalize(audit.cpuUpload)))
            << "the renderer does not upload pbrTextureTransformRows: " << audit.cpuUpload;
        EXPECT_GE(CountOccurrences(source, Normalize(audit.secondAffineRow)), audit.shaderCopies)
            << "the shader does not evaluate the transform's second affine row";

        for (std::size_t slot = 0; slot < audit.samples.size(); ++slot)
        {
            EXPECT_GE(CountOccurrences(source, Normalize(audit.samples[slot])),
                      audit.shaderCopies)
                << "PBR map at transform slot " << slot
                << " is not sampled with its own affine transform: " << audit.samples[slot];
        }
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderHonorsColorSpaceDeclarations)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrColorSpaceAudit& audit : kPbrColorSpaceAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());

        for (const char* flag : {"pbrBaseColorTextureIsSrgb",
                                 "pbrEmissiveTextureIsSrgb",
                                 "pbrEncodeOutputToSrgb"})
        {
            EXPECT_NE(std::string::npos, source.find(flag))
                << "the renderer does not consume draw flag " << flag;
        }

        for (const char* evidence :
             {audit.baseDecode, audit.emissiveDecode, audit.outputEncode})
        {
            const std::string normalized = Normalize(evidence);
            EXPECT_GE(CountOccurrences(source, normalized), audit.shaderCopies)
                << "missing PBR colour-transfer evidence: " << evidence;
        }
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderHonorsTransportedFresnelEndpoints)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrFresnelAudit& audit : kPbrFresnelAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());

        EXPECT_NE(std::string::npos, source.find("pbrDielectricF0"))
            << "the renderer does not upload the transported dielectric F0";
        EXPECT_TRUE(source.find("pbrDielectricF90") != std::string::npos
                    || source.find("pbrSpecularFactor") != std::string::npos)
            << "the renderer uploads neither the factor-only F90 nor the textured specular "
               "weight from which F90 is evaluated";

        for (const char* evidence :
             {audit.dielectricF0, audit.dielectricF90, audit.schlickEndpoints})
        {
            EXPECT_GE(CountOccurrences(source, Normalize(evidence)), audit.shaderCopies)
                << "missing rigid/skinned PBR Fresnel evidence: " << evidence;
        }
    }
}

// GLTF-476. The inventory below this one partitions the PBR renderers by whether they
// sample KHR_materials_specular's two maps, and it once labelled a renderer "factor-only" -- which was
// never checked against anything. It was false: that renderer consumed 6 of the 20 PBR draw
// parameters, and the 14 it dropped included four CORE glTF 2.0 material inputs (normalTexture.scale,
// occlusionTexture.strength, the sRGB encoding of base colour and emissive, and KHR_texture_transform
// with its per-slot TEXCOORD selection). It did not refuse those materials; it drew them with the
// shader's own defaults substituted, which is the forbidden third state and the exact thing the
// campaign's partition tests exist to make impossible.
//
// So this test asks the question the label assumed the answer to. Every parameter here is one that
// EVERY PBR renderer must consume -- the specular-texture six are deliberately excluded: they are
// audited by the specular-texture partition below.
//
// A missing NAME here is not proof of a wrong picture on its own, and this test does not claim
// otherwise: it is a cheap necessary condition. What makes it worth having is that the condition
// was already violated, by one renderer, for fourteen parameters at once, and nothing said so.
TEST(GltfRendererPbrFallbackPolicy, EveryPbrRendererConsumesEveryUniversalPbrDrawParameter)
{
    constexpr std::array<const char*, 13> universal{{
        "pbrBaseColorTextureIsSrgb", "pbrDielectricF0", "pbrEmissiveMap",
        "pbrEmissiveTextureIsSrgb", "pbrEncodeOutputToSrgb", "pbrMetallicFactor",
        "pbrMetallicRoughnessMap", "pbrNormalMap", "pbrNormalScale", "pbrOcclusionMap",
        "pbrOcclusionStrength", "pbrRoughnessFactor", "pbrTextureTransformRows",
    }};
    // The grazing endpoint is the one input with two correct spellings. `pbrDielectricF90` is the
    // already-weighted value; `pbrSpecularFactor` is the authored strength the weight comes from,
    // and a renderer that samples the strength MAP must start from the latter because the map
    // multiplies it. Several renderers legitimately read only the second. Requiring the first by name
    // would fail them for being more complete, so the condition is "one of the two".
    constexpr std::array<const char*, 2> grazingEndpoint{{"pbrDielectricF90", "pbrSpecularFactor"}};
    // The same set the specular partition below covers, so a renderer cannot be visible to one
    // audit and invisible to the other.
    constexpr std::array<const char*, 6> pbrRenderers{{
        "directx9", "easygl", "fna3d", "sdl-gpu", "software", "webgpu",
    }};

    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    for (const char* name : pbrRenderers)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty()) << "no policy source found for this renderer";
        for (const char* parameter : universal)
        {
            EXPECT_NE(std::string::npos, source.find(parameter))
                << name << " never mentions GpuDrawParams::" << parameter
                << ". A renderer that does not read a material input does not refuse the material "
                   "either -- it draws it with a substituted default, which is the one outcome the "
                   "two-state partition forbids.";
        }
        const bool hasGrazingEndpoint =
            source.find(grazingEndpoint[0]) != std::string::npos ||
            source.find(grazingEndpoint[1]) != std::string::npos;
        EXPECT_TRUE(hasGrazingEndpoint)
            << name << " mentions neither GpuDrawParams::" << grazingEndpoint[0] << " nor ::"
            << grazingEndpoint[1] << ", so KHR_materials_specular's grazing weight reaches its "
               "shader in no form at all.";
    }
}

TEST(GltfRendererPbrFallbackPolicy, SpecularTextureInventoryClassifiesEveryPbrRenderer)
{
    // GLTF-344 is `KHR_materials_specular`'s partial boundary, and prose is the wrong place to
    // keep it: "the other renderers remain" was written when four remained and was still being
    // read after eleven were done. The boundary is a partition instead, and it is the *unfinished*
    // half that carries the value -- naming the renderers that sample neither map is what makes
    // finishing one of them a deliberate edit here rather than a silent drift.
    //
    // Both directions are asserted. A renderer moved into `sampling` without the bindings fails,
    // and so does one that grows them while still listed as factor-only, which is the direction a
    // half-finished backend would otherwise take unnoticed.
    constexpr std::array<const char*, 6> sampling{{
        "directx9", "easygl", "fna3d", "sdl-gpu", "software", "webgpu",
    }};
    // Factor-only is not a capability decision -- it is unfinished work. `webgpu` left this set on
    // 2026-08-18 (`GLTF-344`): its PBR uniform block grew KHR_materials_specular's own inputs -- the
    // UNCLAMPED dielectric F0, the specular factor and two affine transform rows per map -- and its
    // two WGSL shaders sample both maps at bindings 6 and 7.
    constexpr std::array<const char*, 0> factorOnly{};

    std::set<std::string> expected;
    for (const char* name : sampling) { expected.insert(name); }
    for (const char* name : factorOnly) { expected.insert(name); }
    ASSERT_EQ(sampling.size() + factorOnly.size(), expected.size()) << "the two sets must be disjoint";

    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Same discriminator as InventoryCoversEveryRendererThatConsumesPbrMaps, so a new PBR renderer
    // cannot appear to one audit and not the other. HEADLESS's ownership-only validation is a
    // separately pinned disposition above, not a renderer that samples these maps.
    const std::set<std::string> observed = RendererNamesConsumingPbrMaps(renderers);
    EXPECT_EQ(expected, observed)
        << "a PBR renderer was added or removed without a GLTF-344 specular-texture disposition";

    for (const char* name : sampling)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        EXPECT_NE(std::string::npos, source.find("pbrSpecularMap"))
            << "listed as sampling, but the scalar specular map is never bound";
        EXPECT_NE(std::string::npos, source.find("pbrSpecularColorMap"))
            << "listed as sampling, but the specular colour map is never bound";
    }
    for (const char* name : factorOnly)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        EXPECT_EQ(std::string::npos, source.find("pbrSpecularMap"))
            << "this renderer now binds a specular map -- move it to `sampling` and give it the "
               "per-map UV selector, rather than leaving the boundary saying it has neither";
        EXPECT_EQ(std::string::npos, source.find("pbrSpecularColorMap"))
            << "this renderer now binds a specular colour map -- move it to `sampling`";
    }
}

TEST(GltfRendererPbrFallbackPolicy, EasyGLSamplesBothKhrMaterialsSpecularTextures)
{
    // GLTF-344 lands backend-by-backend. This focused contract prevents the first completed
    // renderer from regressing; the repository-wide partition is
    // SpecularTextureInventoryClassifiesEveryPbrRenderer above.
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "easygl");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrSpecularMap->BindGL(5)",
             "params.pbrSpecularColorMap->BindGL(6)",
             "default_white_texture_.active_bind(::easygl::TextureUnit::Texture5",
             "default_white_texture_.active_bind(::easygl::TextureUnit::Texture6",
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularTextureTransformRows[row]",
             "params.pbrSpecularColorTextureIsSrgb",
             "(mask & (std::uint32_t{1} << 5))",
             "(mask & (std::uint32_t{1} << 6))"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing EasyGL specular binding evidence: " << evidence;
    }

    for (const char* shaderEvidence : {
             "texture(uSpecularMap,cnaSampleUV(cnaPbrSpecularTransformUV(",
             "texture(uSpecularColorMap,cnaSampleUV(cnaPbrSpecularTransformUV(",
             "specularColorTex=mix(specularColorTex,cnaSrgbToLinear(specularColorTex),uSrgb.w)",
             "min(uSpecularFresnelInputs.xyz*specularColorTex,vec3(1.0))*specularWeight",
             "vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic)"})
    {
        EXPECT_GE(CountOccurrences(source, Normalize(shaderEvidence)), 2u)
            << "both rigid and skinned EasyGL PBR shaders must contain: " << shaderEvidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, DirectX9SamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "directx9");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "BindPbrSampler(device_.Get(), 5, params.pbrSpecularMap",
             "BindPbrSampler(device_.Get(), 6, params.pbrSpecularColorMap",
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularColorTextureIsSrgb",
             "params.pbrSpecularTextureTransformRows[0][0]",
             "SpecularFresnelInputs", "'p', 24, 1",
             "SpecularMapFlags", "'p', 25, 1",
             "SpecularTextureTransformRows", "'p', 26, 4",
             // The pixel bytecode's exact LENGTH, which is what says the committed blob was
             // regenerated from the HLSL beside it rather than left behind by an edit. It moved from
             // 5588 to 5688 when GLTF-465 added the COLOR_0 product and its VertexColorFlags
             // register, to 6472 when the PBR pixel shaders gained punctual lights and shadow
             // sampling, and to 7200 with the degenerate-basis guard and glTF doubleSided; the byte
             // count is the only part of an opaque blob a source audit can read -- so it is updated
             // deliberately here, alongside the register table below, rather than loosened into
             // "some bytecode exists".
             "kPbr3DPSBytecode[7200]",
             "kPbrSkinned3DPSBytecode[7200]"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing DirectX 9 specular binding evidence: " << evidence;
    }

    for (const char* evidence : {
             "sampler2D SpecularMap : register(s5)",
             "sampler2D SpecularColorMap : register(s6)",
             "tex2D(SpecularMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 5), 0)).a",
             "tex2D(SpecularColorMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 6), 1)).rgb",
             "lerp(specularColorTex, CnaSrgbToLinear(specularColorTex), SpecularMapFlags.x)",
             "min(SpecularFresnelInputs.xyz * specularColorTex, 1.0) * specularWeight",
             "lerp(float3(specularWeight, specularWeight, specularWeight), "
             "float3(1.0, 1.0, 1.0), metallic)"})
    {
        EXPECT_EQ(2u, CountOccurrences(source, Normalize(evidence)))
            << "both DirectX 9 PBR shaders must contain: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, Fna3dSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "fna3d");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularColorTextureIsSrgb",
             "params.pbrSpecularTextureTransformRows[0][0]",
             "bindPbrTexture(\"SpecularMap\", params.pbrSpecularMap",
             "bindPbrTexture(\"SpecularColorMap\", params.pbrSpecularColorMap",
             "effect.FindPixelSamplerRegister(samplerName)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing FNA3D specular binding evidence: " << evidence;
    }

    for (const char* evidence : {
             "sampler2D SpecularMap : register(s5)",
             "sampler2D SpecularColorMap : register(s6)",
             "tex2D(SpecularMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 5), 0)).a",
             "tex2D(SpecularColorMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 6), 1)).rgb",
             "lerp(specularColorTex, CnaSrgbToLinear(specularColorTex), SpecularMapFlags.x)",
             "min(SpecularFresnelInputs.xyz * specularColorTex, 1.0) * specularWeight"})
    {
        EXPECT_EQ(2u, CountOccurrences(source, Normalize(evidence)))
            << "both FNA3D PBR source shaders must contain: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, Fna3dSamplesSplitSumImageBasedLighting)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "fna3d");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.iblEnabled && params.iblIrradiance != nullptr",
             "effect.SetFloats(\"IblParams\", iblParams, 4)",
             "bindPbrSampled(\"IblIrradiance\", irradiance)",
             "bindPbrSampled(\"IblSpecular\", specular)",
             "bindPbrTexture(\"IblBrdfLut\", useIbl ? params.iblBrdfLut : nullptr)",
             "SupportsImageBasedLightingEXT() const override"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing FNA3D IBL upload/binding evidence: " << evidence;
    }

    for (const char* evidence : {
             "samplerCUBE IblIrradiance : register(s10)",
             "samplerCUBE IblSpecular : register(s11)",
             "sampler2D IblBrdfLut : register(s12)",
             "texCUBElod(IblIrradiance, float4(N, 0.0)).rgb",
             "texCUBElod(IblSpecular, float4(R, lod)).rgb",
             "tex2Dlod(IblBrdfLut, float4(NdotV, roughness, 0.0, 0.0)).rg",
             "CnaIblAmbient(finalNormal, V, albedo, F0, roughness, metallic, occlusion)"})
    {
        EXPECT_EQ(2u, CountOccurrences(source, Normalize(evidence)))
            << "both FNA3D PBR source shaders must contain: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, Fna3dFlipsDoubleSidedPbrTangentBasis)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "fna3d");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrDoubleSided",
             "effect.SetFloats(\"PbrDoubleSidedParams\", doubleSidedParams, 4)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing FNA3D double-sided upload/compile evidence: " << evidence;
    }
    EXPECT_EQ(2u, CountOccurrences(
                      source,
                      Normalize("face * PbrDoubleSidedParams.x < 0.0")))
        << "both FNA3D PBR shaders must reverse the tangent basis from VFACE";
}

// DIRECTX9 compiles the same two PBR sources into embedded vs_3_0/ps_3_0 bytecode; Direct3D's VFACE
// is positive on its own clockwise front face, glTF's back face (DirectX9_Pbr_DoubleSided measures it
// under DXVK), the sign every FNA3D driver reports too.
TEST(GltfRendererPbrFallbackPolicy, DirectX9FlipsDoubleSidedPbrTangentBasis)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "directx9");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrDoubleSided ? (Determinant3x3(world) < 0.0f ? 1.0f : -1.0f) : 0.0f",
             "\"PbrDoubleSidedParams\", doubleSidedParams)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing DirectX 9 double-sided upload evidence: " << evidence;
    }
    EXPECT_EQ(8u, CountOccurrences(source, Normalize("{\"PbrDoubleSidedParams\", 'p', 77, 1}")))
        << "every rigid and skinned PBR pixel shader's register table must name c77";
    EXPECT_EQ(2u, CountOccurrences(source, Normalize("face * PbrDoubleSidedParams.x < 0.0")))
        << "both DirectX 9 PBR shaders must reverse the tangent basis from VFACE";
}

TEST(GltfRendererPbrFallbackPolicy, Fna3dSelectsIndependentPbrTextureCoordinates)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "fna3d");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrTextureCoordinateSetMask",
             "effect.SetFloats(\"PbrTextureCoordinateSets\", textureCoordinateSets, 4)",
             "effect.SetFloats(\"PbrTextureCoordinateSets2\", textureCoordinateSets2, 4)",
             "const bool useDualUv = params.pbrTextureCoordinateSetMask != 0",
             "VSPbr3DDualUv",
             "VSPbrSkinned3DDualUv"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing FNA3D UV1 upload/pass evidence: " << evidence;
    }
    EXPECT_EQ(2u, CountOccurrences(source, Normalize("float2 CnaPbrUv(PSInput pin, int slot)")))
        << "both FNA3D PBR shaders must select UV0/UV1 independently";
    EXPECT_EQ(2u, CountOccurrences(
                      source,
                      Normalize("PbrTextureCoordinateSets2.z")))
        << "both FNA3D PBR shaders must carry all seven material-map selectors";
}

TEST(GltfRendererPbrFallbackPolicy, SdlGpuSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "sdl-gpu");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "std::array<float, 72> pbrParams",
             "out[12] = p.pbrDielectricF0Unclamped[0]",
             "out[15] = p.pbrSpecularFactor",
             "p.pbrSpecularColorTextureIsSrgb",
             "p.pbrSpecularTextureTransformRows[row][component]",
             "params.pbrSpecularMap, \"PbrEffect.SpecularMapEXT\"",
             "params.pbrSpecularColorMap, \"PbrEffect.SpecularColorMapEXT\"",
             "command.specularSampler = samplerSlots_[5]",
             "command.specularColorSampler = samplerSlots_[6]",
             // STREETS-0006: the two maps' own binding slots, rather
             // than the module's total sampler count, which SMG-0032 and STREETS-0002 each grew.
             "samplerBindings[5].sampler = specularSampler",
             "samplerBindings[6].sampler = specularColorSampler",
             "layout(set = 2, binding = 5) uniform sampler2D uSpecularMap",
             "layout(set = 2, binding = 6) uniform sampler2D uSpecularColorMap",
             "texture(uSpecularMap, cnaPbrSpecularTransformUV(fragUV, 0), pbrp.lodBias4To7.y).a",
             "uSpecularColorMap, cnaPbrSpecularTransformUV(fragUV, 1), pbrp.lodBias4To7.z).rgb",
             "mix(specularColorTex, cnaSrgbToLinear(specularColorTex), pbrp.srgbFlags.w)",
             "pbrp.specularFresnelInputs.xyz * specularColorTex, vec3(1.0)) * specularWeight",
             "mix(vec3(specularWeight), vec3(1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing SDL-GPU specular binding evidence: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderUsesTheGltfPackedTextureChannels)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrChannelAudit& audit : kPbrChannelAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());

        for (const char* evidence :
             {audit.normalRgbRemap, audit.roughnessGreen,
              audit.metallicBlue, audit.occlusionRed})
        {
            EXPECT_EQ(audit.shaderCopies, CountOccurrences(source, Normalize(evidence)))
                << "wrong or missing packed-channel evidence: " << evidence;
        }
    }

    // Lock EasyGL's slot-to-selector mapping in both rigid and skinned builders as well as the
    // generic packed-channel math above: exchanging normal and MR selectors would retain the
    // correct .rgb/.g/.b expressions while sampling the wrong authored coordinate stream.
    const std::string easy = RendererSlotText(renderers, "easygl");
    for (const char* selector : {
             "baseUv = dualUv ? \"cnaPbrUV(uTextureCoordinateSets.x)\" : \"vUV\"",
             "normalUv = dualUv ? \"cnaPbrUV(uTextureCoordinateSets.y)\" : \"vUV\"",
             "mrUv = dualUv ? \"cnaPbrUV(uTextureCoordinateSets.z)\" : \"vUV\"",
             "emissiveUv = dualUv ? \"cnaPbrUV(uTextureCoordinateSets.w)\" : \"vUV\"",
             "dualUv ? \"cnaPbrUV(uOcclusionTextureCoordinateSet)\" : \"vUV\""})
    {
        EXPECT_EQ(2u, CountOccurrences(easy, Normalize(selector)))
            << "EasyGL rigid/skinned PBR builders must agree on selector: " << selector;
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderConsumesTheCoreMaterialFactors)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    static_assert(kPbrMaterialFactorAudits.size() == kPbrChannelAudits.size());
    for (std::size_t index = 0; index < kPbrMaterialFactorAudits.size(); ++index)
    {
        const RendererPbrMaterialFactorAudit& material = kPbrMaterialFactorAudits[index];
        const RendererPbrChannelAudit& channels = kPbrChannelAudits[index];
        SCOPED_TRACE(material.name);
        ASSERT_STREQ(material.name, channels.name)
            << "the material-factor and packed-channel inventories must stay in the same order";
        const std::string source = RendererSlotText(renderers, material.name);
        ASSERT_FALSE(source.empty());

        for (const char* evidence :
             {material.baseRgbFactor, material.baseAlphaFactor, material.emissiveFactor})
        {
            EXPECT_GE(CountOccurrences(source, Normalize(evidence)), material.shaderCopies)
                << "missing rigid/skinned PBR material-factor evidence: " << evidence;
        }
        EXPECT_GE(CountOccurrences(source, Normalize(channels.roughnessGreen)),
                  channels.shaderCopies)
            << "roughness factor is not applied to the glTF G channel";
        EXPECT_GE(CountOccurrences(source, Normalize(channels.metallicBlue)),
                  channels.shaderCopies)
            << "metallic factor is not applied to the glTF B channel";
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrVertexPathConsumesWorldViewProjection)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrTransformAudit& audit : kPbrTransformAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence :
             {audit.mvpCompose, audit.mvpUpload, audit.worldUpload,
              audit.rigidClipPosition, audit.skinnedClipPosition})
        {
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing PBR transform evidence: " << evidence;
        }
    }

    // The WebGPU programs share common expression spellings with other stock shaders, so scope
    // both PBR owners explicitly: a non-PBR WGSL occurrence must not keep this green after either
    // path drifts. WEBGPU-28 moved every WGSL literal out of WebGPURenderer.cpp into
    // webgpu_shaders.hpp, and the C++ function markers this used to scope by stayed behind -- so
    // the ASSERTs still passed, the region they framed no longer held any shader, and the
    // assertion read as a policy violation for a policy that was never broken. Scope by the
    // shader constants instead, which is where the WGSL now is and what it is named by.
    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "include" / "CNA" / "Internal" / "Renderers" / "WebGPU" /
        "webgpu_shaders.hpp"));
    for (const auto& markers : {
             std::pair{"inline constexpr char kPbr[] = R\"WGSL(",
                       "inline constexpr char kSkinned[] = R\"WGSL("},
             std::pair{"inline constexpr char kSkinnedPbr[] = R\"WGSL(",
                       "/// Directly-compilable WGSL sources"}})
    {
        const std::size_t begin = webgpu.find(Normalize(markers.first));
        const std::size_t end = webgpu.find(Normalize(markers.second), begin);
        ASSERT_NE(std::string::npos, begin);
        ASSERT_NE(std::string::npos, end);
        const std::string pbrPath = webgpu.substr(begin, end - begin);
        EXPECT_NE(std::string::npos, pbrPath.find("output.position=u.mvp*"));
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrRendererHonorsCallerOwnedCullState)
{
    const std::filesystem::path repository = RepositoryRoot();
    const std::filesystem::path renderers = repository / "modules" / "renderers";
    std::set<std::string> cullInventory;
    for (const RendererPbrCullAudit& audit : kPbrCullAudits)
    {
        SCOPED_TRACE(audit.name);
        EXPECT_TRUE(cullInventory.emplace(audit.name).second) << "duplicate cull audit row";
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence : audit.evidence)
        {
            if (evidence == nullptr) { continue; }
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing caller-owned PBR cull-state evidence: " << evidence;
        }
    }
    std::set<std::string> pbrInventory;
    for (const RendererAudit& audit : kAudits) { pbrInventory.emplace(audit.name); }
    EXPECT_EQ(pbrInventory, cullInventory)
        << "every renderer discovered by the PBR fallback inventory needs a cull-state row too";

    // RasterizerState is a device/application contract rather than an Effect side effect. Lock
    // the single public forwarding point as well as the native endpoints above: doubleSided's
    // consumer selects CullNone, then this call must carry that exact enum to the active renderer.
    const std::string graphicsDevice = Normalize(ReadFile(
        repository / "modules" / "graphics" / "src" / "Xna" / "GraphicsDevice.cpp"));
    EXPECT_NE(std::string::npos, graphicsDevice.find(Normalize(
        "renderer_->ApplyRasterizerState("
        "CNA::Internal::Renderers::NormalizeXnaCullModeOrdinal("
        "static_cast<int>(value.getCullModeProperty())),")));

    // WebGPU builds two immutable PBR pipelines. Scope their owners independently:
    // an ordinary 3D pipeline with correct culling must not hide a rigid or skinned PBR hardcode.
    const auto expectScoped = [](const std::string& source, const char* beginMarker,
                                 const char* endMarker,
                                 std::initializer_list<const char*> evidence)
    {
        const std::size_t begin = source.find(Normalize(beginMarker));
        const std::size_t end = source.find(Normalize(endMarker), begin);
        ASSERT_NE(std::string::npos, begin);
        ASSERT_NE(std::string::npos, end);
        const std::string owner = source.substr(begin, end - begin);
        for (const char* fragment : evidence)
        {
            EXPECT_NE(std::string::npos, owner.find(Normalize(fragment))) << fragment;
        }
    };

    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "src" / "WebGPURenderer.cpp"));
    expectScoped(webgpu,
                 "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D(",
                 "void WebGPURenderer::QueuePbrDraw(",
                 {"Make3DPipelineKey(topology, stripIndexFormat, depthTest, depthWrite, depthFunc,"
                  "blend, blendParams, cullMode, wireframe,",
                  "desc.cullMode = cullMode"});
    expectScoped(webgpu,
                 "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D(",
                 "void WebGPURenderer::QueueSkinnedPbrDraw(",
                 {"Make3DPipelineKey(topology, stripIndexFormat, depthTest, depthWrite, depthFunc,"
                  "blend, blendParams, cullMode, wireframe,",
                  "desc.cullMode = cullMode"});
    // The two scoped rows above prove the PBR paths FORWARD the caller's cull mode -- into the
    // pipeline key and into the descriptor. Applying it moved into the shared builder, which no
    // per-path scope can see, so that half is asserted on the file: scoping it would have made
    // this test unsatisfiable, and dropping it would have left forwarding-to-nowhere green.
    EXPECT_NE(std::string::npos, webgpu.find(Normalize(
        "pipeline.primitive.cullMode = ToWGPUCullMode(d.cullMode)")))
        << "WebGPU's shared 3D pipeline builder must apply the descriptor's cull mode";
}

TEST(GltfRendererPbrFallbackPolicy, EverySkinnedPbrShaderInverseTransposesTheJointMatrix)
{
    struct Audit
    {
        const char* name;
        const char* evidence;
    };
    constexpr std::array<Audit, 6> audits{{
        {"directx9", "CnaSkinNormal(skinNormalMat, vin.Normal)"},
        {"easygl", "cnaSkinNormal(skinDirectionMat,aNormal)"},
        {"fna3d", "CnaSkinNormal(skinNormalMat, vin.Normal)"},
        {"sdl-gpu", "cnaSkinNormal(skinNormalMat, inNormal)"},
        {"software", "normal = params.pbr ? TransformWorldNormal(blended, normal)"},
        {"webgpu", "normalMatrix * pbrSkinNormal(skinMat3, input.normal)"},
    }};

    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const Audit& audit : audits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "missing skinned-PBR inverse-transpose evidence: " << audit.evidence;
    }

    // WebGPU also has a stock SkinnedEffect WGSL program with an intentionally identical helper.
    // Scope this assertion to CreateSkinnedPbrResources so that fixing only the stock program
    // cannot satisfy the PBR audit (the first implementation of this gate made exactly that
    // mistake).
    // WEBGPU-28 moved the WGSL into webgpu_shaders.hpp; scope by the shader constant it lives in.
    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "include" / "CNA" / "Internal" / "Renderers" / "WebGPU" /
        "webgpu_shaders.hpp"));
    const std::string beginMarker = Normalize("inline constexpr char kSkinnedPbr[] = R\"WGSL(");
    const std::string endMarker = Normalize("/// Directly-compilable WGSL sources");
    const std::size_t begin = webgpu.find(beginMarker);
    const std::size_t end = webgpu.find(endMarker, begin);
    ASSERT_NE(std::string::npos, begin);
    ASSERT_NE(std::string::npos, end);
    const std::string skinnedPbr = webgpu.substr(begin, end - begin);
    EXPECT_NE(std::string::npos, skinnedPbr.find(Normalize(
        "normalMatrix * pbrSkinNormal(skinMat3, input.normal)")))
        << "WebGPU's actual SkinnedPbrEffect WGSL must inverse-transpose the joint matrix";
}

TEST(GltfRendererPbrFallbackPolicy, EverySkinnedPbrShaderConsumesThePaletteAndInfluenceCount)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const RendererPbrSkinningAudit& audit : kPbrSkinningAudits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        for (const char* evidence :
             {audit.paletteUpload, audit.weightCountUpload,
              audit.twoWeightGate, audit.fourWeightGate})
        {
            EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
                << "missing skinned-PBR palette/influence evidence: " << evidence;
        }
    }

    // These aggregate-source gates are paired with the PBR-specific inverse-transpose inventory
    // above. Keep its inventory in exact lockstep, so a new backend cannot satisfy the generic
    // weight gates without also proving that its actual PBR path consumes the resulting matrix.
    static_assert(kPbrSkinningAudits.size() == 6);
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderComposesDirectionDeterminantsIntoTangentHandedness)
{
    struct Audit
    {
        const char* name;
        const char* rigid;
        const char* skinned;
    };
    constexpr std::array<Audit, 6> audits{{
        {"directx9",
         "vin.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        {"easygl",
         "aTangent.w*cnaDirectionHandedness(worldDirectionMat)*instanceHandedness",
         "*cnaDirectionHandedness(skinDirectionMat)"},
        {"fna3d",
         "vin.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        // STREETS-0001: the instance matrix is folded into World, so the
        // determinant covers a mirroring instance too (VULKAN-232's reasoning).
        {"sdl-gpu",
         "inTangent.w * cnaDirectionHandedness(mat3(CNA_INSTANCE_WORLD(lp.world)))",
         "* cnaDirectionHandedness(skinNormalMat)"},
        {"software",
         "tangentSign * AffineHandedness(params.worldColMajor)",
         "tangentSign *= AffineHandedness(blended)"},
        {"webgpu",
         "input.tangent.w * directionHandedness(worldMat3)",
         "* pbrDirectionHandedness(skinMat3)"},
    }};

    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    for (const Audit& audit : audits)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.rigid)))
            << "missing rigid-PBR transform-handedness evidence: " << audit.rigid;
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.skinned)))
            << "missing skinned-PBR transform-handedness evidence: " << audit.skinned;
    }
}

// --- GLTF-462/GLTF-465: the stride-60 record, per renderer ---------------------------

TEST(GltfRendererPbrFallbackPolicy, EveryPbrRendererEitherBindsTheStride60RecordOrIsNamedAsNotYet)
{
    // Stride 60 is the rigid PBR record: Position, Normal, Tangent, TEXCOORD_0, TEXCOORD_1 and --
    // since GLTF-462 -- a packed COLOR_0 in the four bytes GLTF-182 had reserved purely to keep the
    // stride distinct from 56. It has existed since GLTF-182, and this audit is what found that most
    // PBR renderers never learned it: one fell through to a `stride >= 32` catch-all that reads
    // TEXCOORD at offset 24 -- inside the tangent -- so a dual-UV PBR mesh textured itself from
    // tangent bytes in silence, and DIRECTX9 degraded to position-only, no attributes at all, or an
    // outright refusal.
    //
    // GLTF-462 made that reachable for ordinary content (every rigid vertex-coloured
    // metallic-roughness primitive lands here now), so the disposition is a partition rather than a
    // hope: a renderer either binds the record or is named as not yet doing so.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Binds the record through a stride table or a declaration-normalized shader variant.
    constexpr std::array<const char*, 5> strideTable{{
        "directx9", "easygl", "fna3d", "software", "sdl-gpu",
    }};
    constexpr std::array<const char*, 1> normalized{{"webgpu"}};
    // No stride-60 row and no declaration path: the record degrades visibly (no attributes, or a
    // refusal) rather than being mis-bound. GLTF-465 owns closing these, and each needs pipeline or
    // shader-descriptor work rather than a table entry.
    constexpr std::array<const char*, 0> notYet{};

    std::set<std::string> classified;
    for (const char* name : strideTable) { classified.insert(name); }
    for (const char* name : normalized) { classified.insert(name); }
    for (const char* name : notYet) { classified.insert(name); }
    ASSERT_EQ(strideTable.size() + normalized.size() + notYet.size(), classified.size())
        << "the dispositions must be disjoint";

    // Discovered exactly as the PBR-map inventory discovers its own set.
    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    expected.insert("software");
    EXPECT_EQ(expected, classified)
        << "a PBR renderer was added or removed without a GLTF-462 stride-60 disposition";

    for (const char* name : strideTable)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty());
        EXPECT_TRUE(source.find(Normalize("case 60:")) != std::string::npos ||
                    source.find(Normalize("stride == 60")) != std::string::npos)
            << "listed as binding the stride-60 record, but no stride-60 row exists";
    }
    for (const char* name : normalized)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        EXPECT_NE(std::string::npos, source.find(Normalize("NormalizeRigidPbrStreamEXT(")));
        EXPECT_NE(std::string::npos, source.find(Normalize("command.colored = (pbrStride == 60);")));
        EXPECT_NE(std::string::npos, source.find(Normalize("GetOrCreatePipelinePbr3D(command.colored,")));
    }
    for (const char* name : notYet)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty());
        EXPECT_EQ(std::string::npos, source.find(Normalize("case 60:")))
            << "this renderer grew a stride-60 row; move it out of the not-yet list";
        EXPECT_EQ(std::string::npos, source.find(Normalize("stride == 60")))
            << "this renderer grew a stride-60 row; move it out of the not-yet list";
    }
}

TEST(GltfRendererPbrFallbackPolicy, EverySkinnedPbrRendererEitherBindsTheStride80RecordOrRefusesIt)
{
    // GLTF-463: stride 80 is the whole stride-76 skinned PBR record with a packed
    // COLOR_0 appended, and it is where a SKINNED vertex-coloured metallic-roughness primitive now
    // imports to -- so it is ordinary content, not a corner case. Unlike stride 60 there is no
    // pre-existing row to grow a meaning: a renderer either declares the layout and its shader's
    // colour input, or it never sees the stride at all and refuses the draw by its own established
    // unsupported-stride path. What must not exist is a third state -- accepting the stride and
    // reading the record with a layout that does not describe it.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    struct Stride80Audit
    {
        const char* name;
        const char* evidence;
    };
    // Binds the record. EasyGL serves five GL profiles; SOFTWARE rasterises it on the CPU;
    // DirectX 9 has a fixed input-element table, while FNA3D forwards the caller's declaration and
    // executes the same shared HLSL through its compiled Effect Framework wrapper.
    constexpr std::array<Stride80Audit, 6> binds{{
        {"webgpu", "attributes[6].offset = 76;"},
        // GLTF-465: D3DDECLTYPE_D3DCOLOR is D3D9's own normalized four-byte colour element, read
        // into a float4 COLOR register -- exactly what the importer packs at offset 76.
        {"directx9", "{0, 76, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0},"},
        // FNA3D converts the caller's complete public declaration element-by-element; the
        // canonical stride-80 declaration appended by RendererSlotText pins COLOR0 at byte 76.
        {"fna3d", "converted.offset = element.getOffsetProperty();"},
        {"sdl-gpu", "attrs[slot].offset = skinned ? 76 : 56;"},
        {"easygl", "case 80:"},
        {"software", "if (stride == 80) UnpackColorBytes(raw.At(76), out.r, out.g, out.b, out.a);"},
    }};
    // Never sees stride 80: its skinned PBR path accepts only the strides it has layouts for, so an
    // 80-byte record refuses rather than being mis-read. GLTF-465 records what each would need.
    constexpr std::array<const char*, 0> refuses{};

    std::set<std::string> classified;
    for (const Stride80Audit& audit : binds) { classified.insert(audit.name); }
    for (const char* name : refuses) { classified.insert(name); }
    ASSERT_EQ(binds.size() + refuses.size(), classified.size())
        << "the two dispositions must be disjoint";
    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    expected.insert("software");
    EXPECT_EQ(expected, classified)
        << "a PBR renderer was added or removed without a GLTF-463 stride-80 disposition";

    for (const Stride80Audit& audit : binds)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "listed as binding the stride-80 record, but its layout row is not there";
        // The colour lives at offset 76 in that record; a row that binds the stride without reaching
        // that offset would be the mis-read this test exists to forbid.
        EXPECT_NE(std::string::npos, source.find(Normalize("76")))
            << "the stride-80 row never mentions the colour's offset";
    }
    for (const char* name : refuses)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty());
        EXPECT_EQ(std::string::npos, source.find(Normalize("case 80:")))
            << "this renderer grew a stride-80 row; move it out of the refusing list";
        EXPECT_EQ(std::string::npos, source.find(Normalize("stride == 80")))
            << "this renderer grew a stride-80 row; move it out of the refusing list";
    }
}

TEST(GltfRendererPbrFallbackPolicy, VertexColourReachesTheBaseColourProductOnlyWhereItIsImplemented)
{
    // §3.7.2.1: "if a primitive specifies a vertex color using the attribute semantic property
    // COLOR_0, then this value acts as an additional linear multiplier to base color". GLTF-462
    // makes the importer and the shared material representation carry that -- PbrEffect's
    // VertexColorEnabledEXT reaches every renderer through GpuDrawParams::vertexColorEnabled -- but
    // a renderer has to actually multiply by the attribute, and this states which do.
    //
    // The residue is safe rather than merely unfinished, and that is the property worth pinning: an
    // uncoloured primitive fills the slot with OPAQUE WHITE, the multiplier's identity, so a
    // renderer that ignores the slot draws exactly what it drew before GLTF-462 and one that starts
    // reading it cannot darken anything. What a non-implementing renderer loses is the colour
    // itself, not correctness of everything else -- which is why this is a named per-renderer gap
    // (GLTF-465) and not a reason to keep the whole material model off vertex-coloured content.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";

    struct VertexColourPbrAudit
    {
        const char* name;
        const char* evidence;
    };
    // EasyGL serves three GL profiles (OPENGLES3, OPENGL33, WEBGL2), so this is more than one
    // renderer identity; SOFTWARE is the CPU rasteriser, where the same product is evaluated per
    // fragment on the host.
    constexpr std::array<VertexColourPbrAudit, 6> implemented{{
        // WebGPU expands one marked WGSL source into a bare and a colour-carrying module, because
        // WGSL rejects a vertex input with no matching attribute (GLTF-465).
        {"webgpu", "let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"easygl", "vec3 albedo=baseRGB*uDiffuseColor.rgb*cnaVertexColor.rgb;"},
        // DirectX 9 compiles its HLSL offline into vs_3_0/ps_3_0 bytecode, so the .hlsl IS the
        // source; the two colour-carrying vertex programs are separate entry points because a
        // vs_3_0 input with no stream behind it reads undefined (GLTF-465).
        {"directx9", "albedo *= pin.Color.rgb;"},
        {"fna3d", "albedo *= pin.Color.rgb;"},
        {"sdl-gpu", "vec3 albedo = baseColor * pc.diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"software", "if (stride == 60) UnpackColorBytes(raw.At(56), out.r, out.g, out.b, out.a);"},
    }};
    for (const VertexColourPbrAudit& audit : implemented)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "listed as multiplying COLOR_0 into base colour, but the product is not there";
    }

    // The multiply is only half of §3.9.2: the same factor applies to the base colour's ALPHA, which
    // is what a BLEND-mode vertex-coloured primitive's transparency comes from. A renderer that
    // multiplied only the RGB would look right on an opaque asset and be wrong on a transparent one.
    constexpr std::array<VertexColourPbrAudit, 6> alphaProduct{{
        {"webgpu", "let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;"},
        {"easygl", "alpha=baseColorTex.a*uDiffuseColor.a*cnaVertexColor.a;"},
        {"directx9", "alpha  *= pin.Color.a;"},
        {"fna3d", "alpha  *= pin.Color.a;"},
        {"sdl-gpu", "float alpha = baseColorTex.a * pc.diffuseColor.a * cnaVertexColor.a;"},
        // SOFTWARE's CPU fragment path starts with the interpolated vertex colour, alpha included,
        // and multiplies the base-colour texture and factor into it before evaluating the BRDF.
        {"software", "float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;"},
    }};
    for (const VertexColourPbrAudit& audit : alphaProduct)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "COLOR_0 multiplies base colour but not its alpha";
    }

    // Every implementing renderer must also ASK whether the effect enabled the colour. The stride-60
    // and stride-80 records always carry a colour slot, so a shader that multiplied unconditionally
    // would be relying on the opaque-white fill rather than on what the effect requested -- and would
    // silently ignore an application that set VertexColorEnabledEXT to false on coloured geometry.
    constexpr std::array<VertexColourPbrAudit, 6> gate{{
        {"webgpu", "select(vec4f(1.0), input.color, u.light0DiffuseVertexColor.w > 0.5)"},
        {"easygl", "vec4 cnaVertexColor=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);"},
        {"directx9", "if (VertexColorFlags.x > 0.5)"},
        {"fna3d", "if (VertexColorFlags.x > 0.5)"},
        {"sdl-gpu", "vec4 cnaVertexColor = (pc.vertexColorEnabled > 0.5) ? fragColor0 : vec4(1.0);"},
        {"software", "params.vertexColorEnabled"},
    }};
    for (const VertexColourPbrAudit& audit : gate)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "the colour is multiplied in without asking whether the effect enabled it";
    }

    // The gate is worthless if nothing ever uploads it, and a uniform/constant that no draw writes is
    // exactly the shape of bug this whole audit keeps finding. Each implementing renderer must carry
    // GpuDrawParams::vertexColorEnabled to its own PBR draw.
    constexpr std::array<VertexColourPbrAudit, 3> upload{{
        {"easygl", "p.loc_vertexcolor"},
        {"fna3d", "useVertexColor ? 1.0f : 0.0f"},
        // SDLGPU's PC block already carried the flag -- its own comment called it "unused".
        {"sdl-gpu", "float vertexColorEnabled; // GLTF-465: gates the COLOR_0 product below"},
    }};
    for (const VertexColourPbrAudit& audit : upload)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "the effect's vertex-colour switch never reaches this renderer's PBR draw";
    }

    // EasyGL's binding has to exist as well as its product, in BOTH families: stride 60 carries the
    // colour at 56 and stride 80 at 76, and a shader reading an unbound attribute takes stale VAO
    // state rather than the record's bytes.
    const std::string easygl = RendererSlotText(renderers, "easygl");
    EXPECT_NE(std::string::npos, easygl.find(Normalize(
        "vao.set_attribute_pointer(5, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)56);")))
        << "the stride-60 colour slot is never bound, so the shader reads stale VAO state";
    EXPECT_NE(std::string::npos, easygl.find(Normalize(
        "vao.set_attribute_pointer(7, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)76);")))
        << "the stride-80 colour slot is never bound, so the skinned shader reads stale VAO state";

    // And the whole of the residue, named, with the reason each one is still open recorded next to
    // it. Anything here multiplies by the identity instead: the colour is dropped, nothing else is.
    struct OpenVertexColourRenderer
    {
        const char* name;
        const char* reason;
    };
    constexpr std::array<OpenVertexColourRenderer, 0> notYet{};
    std::set<std::string> classified;
    for (const VertexColourPbrAudit& audit : implemented) { classified.insert(audit.name); }
    for (const OpenVertexColourRenderer& open : notYet) { classified.insert(open.name); }
    ASSERT_EQ(implemented.size() + notYet.size(), classified.size())
        << "the two dispositions must be disjoint";
    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    expected.insert("software");
    EXPECT_EQ(expected, classified)
        << "a PBR renderer was added or removed without a GLTF-462 vertex-colour disposition";

    // A renderer named as not-yet must not silently have grown the product: that is how an inventory
    // rots into fiction. The multiply is spelled differently per renderer, so what is checked here is
    // the one thing all of them would need -- reading the effect's own switch inside a PBR shader.
    for (const OpenVertexColourRenderer& open : notYet)
    {
        SCOPED_TRACE(open.name);
        ASSERT_NE(nullptr, open.reason);
        const std::string source = RendererSlotText(renderers, open.name);
        ASSERT_FALSE(source.empty());
        EXPECT_EQ(std::string::npos, source.find(Normalize("cnaVertexColor")))
            << "this renderer grew the glTF colour product; move it into the implemented list";
    }
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrRendererEitherAppliesVertexColourOrRefusesTheDrawExplicitly)
{
    // GLTF-465, and the reason this test exists rather than another prose row: the
    // project owner rejected `GLTF CORE 2.0 CORRECT` on 2026-08-18 with an argument that decides the
    // shape of the whole task. A renderer that ACCEPTS a valid glTF asset carrying COLOR_0 on a
    // metallic-roughness material and then substitutes the opaque-white identity renders a visibly
    // wrong surface and reports success. A renderer that REFUSES the combination is limited backend
    // coverage instead -- a different thing entirely, and acceptable.
    //
    // So there are exactly two acceptable states and one forbidden one:
    //
    //   1. evaluates §3.9.2's product (RGB and alpha), or
    //   2. calls RequireVertexColourPbrSupportEXT, the shared refusal, on its PBR draw path,
    //
    // and never "accepts the asset and draws it with different core semantics". This partition is the
    // precondition for the unqualified milestone name, so it is machine-checked over every PBR
    // renderer rather than tracked in a table somebody has to remember to update.
    //
    // The PBR renderer set includes SOFTWARE's CPU fragment implementation as well as the native
    // shader backends. It evaluates the same core material product, so it belongs in this exact
    // partition rather than in a special base-colour-only exception.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Evaluates the product -- the same set VertexColourReachesTheBaseColourProduct... verifies in
    // detail (RGB, alpha, the enable gate and the uniform upload, per renderer).
    constexpr std::array<const char*, 6> applies{{
        "easygl", "software", "sdl-gpu", "directx9", "fna3d", "webgpu",
    }};
    // Refuses the draw through the shared guard; the guard stays the one way a renderer that
    // cannot evaluate COLOR_0 may decline it.
    constexpr std::array<const char*, 0> refuses{};

    std::set<std::string> classified;
    for (const char* name : applies) { classified.insert(name); }
    for (const char* name : refuses) { classified.insert(name); }
    ASSERT_EQ(applies.size() + refuses.size(), classified.size())
        << "the two dispositions must be disjoint";
    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    expected.insert("software");
    EXPECT_EQ(expected, classified)
        << "a PBR renderer was added or removed without a GLTF-465 disposition";

    for (const char* name : refuses)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize("RequireVertexColourPbrSupportEXT(")))
            << "this renderer neither evaluates COLOR_0 nor refuses the draw, which is the one state "
               "that is a defect rather than a limitation";
    }
    for (const char* name : applies)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(renderers, name);
        ASSERT_FALSE(source.empty());
        EXPECT_EQ(std::string::npos, source.find(Normalize("RequireVertexColourPbrSupportEXT(")))
            << "this renderer implements the product, so refusing the draw as well would reject "
               "content it renders correctly";
    }

    // The guard itself must stay a single shared implementation rather than nine copies drifting
    // apart -- that is what makes "refuses" one auditable behaviour instead of nine.
    const std::filesystem::path guard = RepositoryRoot() / "modules" / "graphics" / "include" /
        "CNA" / "Internal" / "Renderers" / "Common" / "VertexColourPbrSupport.hpp";
    ASSERT_TRUE(std::filesystem::is_regular_file(guard)) << guard;
    const std::string guardText = Normalize(ReadFile(guard));
    EXPECT_NE(std::string::npos, guardText.find(Normalize(
        "return params.pbr && params.vertexColorEnabled && (strideInBytes == 60 || strideInBytes == 80);")))
        << "the refusal predicate changed; it must fire exactly for an enabled COLOR_0 on the two "
           "colour-carrying PBR strides, or it starts refusing content that renders correctly";
}

TEST(GltfRendererPbrFallbackPolicy, EveryStrideGatedPbrRouteAdmitsBothColourCarryingStrides)
{
    // GLTF-465, and the hole the two tests above cannot see. They ask whether a
    // renderer DECLARES the colour -- a layout row at offset 56/76, a shader that multiplies
    // `cnaVertexColor` into albedo. Neither asks whether a stride-60 or stride-80 draw ever REACHES
    // that shader, and in a renderer whose PBR route is chosen from an explicit stride list those
    // are different questions: the layout can be complete while the route that selects it still
    // enumerates only the two uncoloured strides, so the whole implementation is unreachable.
    //
    // That is not hypothetical. It has now happened three times in this renderer set:
    //
    //   - an OpenGL renderer selected its PBR program for stride 48 only, so a stride-60 draw fell
    //     through to its Blinn-Phong `lit` branch;
    //   - SDLGPU shipped the stride-60/80 pipelines, shaders and attributes and left
    //     `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` gating on `stride == 48`/`68`, so every such
    //     draw fell past every branch into the stride-16 coloured path and was refused there;
    //   - another renderer chose its skinned colour PBR program for stride 80 in one switch and then
    //     threw "needs a skinned PBR vertex layout (stride 68 or 76)" nine lines later.
    //
    // All three passed every layout- and shader-text audit in this file while doing it. So the
    // predicate itself is pinned here: for each renderer whose PBR route is stride-gated, the gate
    // must name the colour-carrying stride alongside its bare twin.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    struct RouteGate
    {
        const char* renderer;
        const char* what;
        const char* predicate;
    };
    // Each row is the predicate that decides whether a PBR draw of that stride reaches the PBR
    // shader at all -- not the layout it would then be read with.
    const std::array<RouteGate, 5> gates{{
        {"webgpu", "the skinned PBR stride check",
         "if (skinnedPbrStride != 68 && skinnedPbrStride != 80)"},
        {"directx9", "the rigid PBR stride check",
         "if (!skinned && stride != 48 && stride != 60 && stride != 68 && stride != 80)"},
        {"directx9", "the skinned PBR stride check",
         "if (skinned && stride != 68 && stride != 80)"},
        {"sdl-gpu", "the remaining stride-derived draw-entry dispatch",
         "if (params.pbr &&"
         "    ((params.skinned && (stride == 68 || stride == 80)) ||"
         "     (!params.skinned && (stride == 48 || stride == 60))))"},
        {"sdl-gpu", "QueuePbrDraw's own acceptance",
         "const bool acceptable = skinned ? (stride == 68u || stride == 80u)"
         "                                : (stride == 48u || stride == 60u);"},
    }};

    for (const RouteGate& gate : gates)
    {
        SCOPED_TRACE(std::string(gate.renderer) + ": " + gate.what);
        const std::string source = RendererSlotText(renderers, gate.renderer);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(gate.predicate)))
            << "this renderer's PBR route is chosen from a stride list, and the list no longer "
               "matches the one pinned here. If the colour-carrying stride was dropped from it, a "
               "vertex-coloured metallic-roughness primitive can no longer reach the shader that "
               "was written for it -- which every other test in this file would still call correct.";
    }

    // The rest of the implementing set does not gate on a stride list at all: their PBR route is
    // selected from `params.pbr` (and the layout separately from the stride), so there is no second
    // list that can fall out of step with the first.
    // Stated as evidence rather than as an absence, so that a renderer which GROWS a stride gate
    // stops matching and has to be classified above.
    struct UngatedRoute
    {
        const char* renderer;
        const char* evidence;
    };
    const std::array<UngatedRoute, 5> ungated{{
        {"webgpu", "if (params.pbr && !params.skinned) { RequireSingleStreamRouteEXT(params, route);"
                   " QueuePbrDraw(vb, ib, world, view, projection, primitive, primitiveCount, params,"
                   " instanceCount, instanceStream); return; }"},
        {"sdl-gpu", "if (params.pbr && !params.skinned) { if (streams.count != 1)"
                    " throw System::NotSupportedException("
                    "\"CNA SDLGPU: PbrEffect requires one per-vertex stream\");"
                    " QueuePbrDraw(vb, ib, world, view, projection, primitive, primitiveCount, params);"
                    " return; }"},
        {"easygl", "if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned;"},
        {"fna3d", "if (params.pbr) return params.skinned ? StockEffectKind::SkinnedPbr : StockEffectKind::Pbr;"},
        {"software", "const bool tangentLayout = stride == 48 || stride == 60 || stride == 68 || stride == 76 || stride == 80"},
    }};
    for (const UngatedRoute& route : ungated)
    {
        SCOPED_TRACE(route.renderer);
        const std::string source = RendererSlotText(renderers, route.renderer);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(route.evidence)))
            << "this renderer's PBR route selection changed; re-check whether it now depends on a "
               "stride list, and if so pin that list above";
    }

    // And the two dispositions together are still the whole implementing set, so a renderer cannot
    // be added to `applies` above and quietly skip this test.
    std::set<std::string> covered;
    for (const RouteGate& gate : gates) { covered.insert(gate.renderer); }
    for (const UngatedRoute& route : ungated) { covered.insert(route.renderer); }
    const std::set<std::string> applies{
        "easygl", "software", "sdl-gpu", "directx9", "fna3d", "webgpu"};
    EXPECT_EQ(applies, covered)
        << "a renderer listed as applying COLOR_0 has no route-reachability disposition";
}

TEST(GltfRendererPbrFallbackPolicy, DeclarationDrivenRigidPbrPreservesTheColourRecord)
{
    const auto root = RepositoryRoot();
    const std::string helper = Normalize(ReadFile(root / "modules/graphics/include/CNA/Internal/Graphics/PbrVertexStream.hpp"));
    ASSERT_FALSE(helper.empty());
    EXPECT_NE(std::string::npos, helper.find(Normalize("const std::size_t targetStride = color || uv1 ? 60 : 48;")));
    EXPECT_NE(std::string::npos, helper.find(Normalize("copy(color, 56, 4);")));
    EXPECT_NE(std::string::npos, helper.find(Normalize("stride = targetStride;")));
    for (const char* name : {"webgpu", "sdl-gpu"})
    {
        SCOPED_TRACE(name);
        const std::string source = RendererSlotText(root / "modules/renderers", name);
        const auto queue = source.find("::QueuePbrDraw(");
        ASSERT_NE(std::string::npos, queue);
        const auto normalize = source.find("NormalizeRigidPbrStreamEXT(", queue);
        const auto colored = source.find("command.colored=", queue);
        ASSERT_NE(std::string::npos, normalize);
        ASSERT_NE(std::string::npos, colored);
        EXPECT_LT(normalize, colored) << "select the shader from the normalized record, not the input stride";
        EXPECT_NE(std::string::npos, source.find(Normalize(
            std::string("const auto& shadow = normalized.empty() ? ") +
            (std::string_view(name) == "webgpu" ? "webgpuVb" : "sdlGpuVb") +
            ".ShadowData() : normalized;"), colored));
    }
}

TEST(GltfRendererIndexWidthPolicy, InventoryClassifiesEveryRenderer)
{
    // A provider has a local CreateIndexBuffer32 implementation. The 2D/no-3D backends
    // deliberately inherit the shared, unconditionally throwing default. Keeping the two sets
    // disjoint makes a new renderer an audit failure, not an accidental 16-bit fallback.
    constexpr std::array<const char*, 8> providers{{
        "directx9", "easygl", "fna3d", "headless", "sdl-gpu",
        "software", "stub", "webgpu",
    }};
    constexpr std::array<const char*, 0> inheritedRejecters{};

    std::set<std::string> expected;
    for (const char* name : providers) { expected.insert(name); }
    for (const char* name : inheritedRejecters) { expected.insert(name); }
    ASSERT_EQ(providers.size() + inheritedRejecters.size(), expected.size())
        << "the policy sets must be disjoint";

    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));
    std::set<std::string> observed;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(renderers))
    {
        if (!entry.is_directory()) { continue; }
        const std::string source = RendererText(entry.path());
        if (source.find("CreateIndexBuffer16") != std::string::npos)
            observed.insert(entry.path().filename().string());
    }
    EXPECT_EQ(expected, observed)
        << "a renderer was added/removed without a GLTF-163 index-width disposition";
}

TEST(GltfRendererIndexWidthPolicy, ProvidersOptInAndUnsupportedRenderersCannotFallBackToSixteenBits)
{
    constexpr std::array<const char*, 8> providers{{
        "directx9", "easygl", "fna3d", "headless", "sdl-gpu",
        "software", "stub", "webgpu",
    }};
    constexpr std::array<const char*, 0> inheritedRejecters{};

    const std::filesystem::path root = RepositoryRoot();
    const std::filesystem::path renderers = root / "modules" / "renderers";
    for (const char* name : providers)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererText(renderers / name);
        EXPECT_NE(std::string::npos, source.find("::CreateIndexBuffer32("))
            << "32-bit support must be an explicit renderer implementation";
    }
    for (const char* name : inheritedRejecters)
    {
        SCOPED_TRACE(name);
        const std::string source = RendererText(renderers / name);
        EXPECT_EQ(std::string::npos, source.find("::CreateIndexBuffer32("))
            << "this no-3D renderer should inherit the shared explicit rejection";
    }

    const std::string common = Normalize(ReadFile(
        root / "modules" / "graphics" / "include" / "CNA" / "Internal" /
        "Renderers" / "Common" / "IGraphicsRenderer.hpp"));
    EXPECT_NE(std::string::npos, common.find(Normalize(
        "IGraphicsRenderer::CreateIndexBuffer32: 32-bit index buffers are not supported by this renderer")));
    EXPECT_NE(std::string::npos, common.find(Normalize(
        "virtual std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int /*index_capacity*/)")));
}

TEST(GltfRendererPointTopologyPolicy, Direct3DBackendsMapPointsOrRejectBeforeSubmission)
{
    // GLTF-394 closes the last known silent POINTS reinterpretations. D3D9 duplicates its native
    // mapper in seven independently compiled draw implementations, so checking only the ordinary
    // path would leave PBR (the glTF path), stock, skinned-colour, instanced, compiled-effect or
    // punctual-light draws behind. The sixth is D3D9CompiledEffect.cpp, added by
    // FX-070; the seventh is D3D9BasicPunctualDraw.cpp.
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    const std::string d3d9 = RendererText(renderers / "directx9");
    EXPECT_EQ(7u, CountOccurrences(
                      d3d9,
                      "casePrimitiveType::PointListEXT:returnD3DPT_POINTLIST;"));

    const std::string pointSuite = Normalize(ReadFile(
        RepositoryRoot() / "modules" / "graphics" / "tests" / "Microsoft" / "Xna" /
        "Framework" / "Graphics" / "PointListPrimitiveTests.cpp"));
    EXPECT_NE(std::string::npos, pointSuite.find("defined(CNA_RENDERER_DIRECTX9)"))
        << "DIRECTX9 must remain in the shared point framebuffer suite";
}
