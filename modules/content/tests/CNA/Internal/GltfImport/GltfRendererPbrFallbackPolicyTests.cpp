// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-163/184/373/374/379/394: repository-wide renderer contracts are tested from the
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
            || extension == ".sc" || extension == ".metal" || extension == ".wgsl";
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

    // These fragments name the actual null branch (or the preselected default for Wicked) for
    // each semantic. Whitespace is ignored, but the map/fallback pairing is not: changing a normal
    // slot to white, or any other slot to the flat-normal texture, fails this table.
    constexpr std::array<RendererAudit, 16> kAudits{{
        {"bgfx",
         "params.pbrNormalMap, defaultFlatNormalTexture3D_",
         "params.pbrMetallicRoughnessMap, defaultWhiteTexture3D_",
         "params.pbrEmissiveMap, defaultWhiteTexture3D_",
         "params.pbrOcclusionMap, defaultWhiteTexture3D_"},
        {"diligent",
         "params != nullptr ? params->pbrNormalMap : nullptr, flatNormalTextureView_",
         "params != nullptr ? params->pbrMetallicRoughnessMap : nullptr, fallbackTextureView_",
         "params != nullptr ? params->pbrEmissiveMap : nullptr, fallbackTextureView_",
         "params != nullptr ? params->pbrOcclusionMap : nullptr, fallbackTextureView_"},
        {"directx11",
         "params.pbrNormalMap ? GetSrvForTextureEXT(params.pbrNormalMap) : GetOrCreateDefaultFlatNormalSrvEXT()",
         "params.pbrMetallicRoughnessMap ? GetSrvForTextureEXT(params.pbrMetallicRoughnessMap) : GetOrCreateDefaultWhiteSrvEXT()",
         "params.pbrEmissiveMap ? GetSrvForTextureEXT(params.pbrEmissiveMap) : GetOrCreateDefaultWhiteSrvEXT()",
         "params.pbrOcclusionMap ? GetSrvForTextureEXT(params.pbrOcclusionMap) : GetOrCreateDefaultWhiteSrvEXT()"},
        {"directx12",
         "params.pbrNormalMap ? params.pbrNormalMap : GetOrCreateDefaultFlatNormalTextureEXT()",
         "params.pbrMetallicRoughnessMap ? params.pbrMetallicRoughnessMap : GetOrCreateDefaultWhiteTextureEXT()",
         "params.pbrEmissiveMap ? params.pbrEmissiveMap : GetOrCreateDefaultWhiteTextureEXT()",
         "params.pbrOcclusionMap ? params.pbrOcclusionMap : GetOrCreateDefaultWhiteTextureEXT()"},
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
        // plans/plan_igl.md: IGL compiles a shader variant per feature set, so an absent map is not
        // sampled at all rather than sampled from a neutral texture. Its dispositions are therefore
        // the feature-flag guards that decide which variant is built -- see
        // IglShaderLibrary.cpp's `cnaHas(CNA_NORMAL_MAP)` and friends for the consuming half, which
        // `EveryPbrMapReachesTheShaderBindingIntendedByItsRenderer` audits separately.
        // MERGE (origin/next, IGL-65..IGL-69): this renderer used to be the one pure
        // ShaderFeatureVariant entry -- it bound nothing for an absent map and let the shader's
        // feature bit skip the sample. Vulkan requires every declared descriptor to have a
        // resource behind it, so it now binds a semantically neutral texture as well, which is
        // what puts it in the NeutralTexture half. Both mechanisms are live and neither is
        // redundant: the bind satisfies the descriptor, the feature bit still means the sample
        // never happens. The evidence below names the null branch, because that is what this
        // field is for; `cnaHas(CNA_NORMAL_MAP)` and friends are pinned by the shader-side tests.
        {"igl",
         "bindUnitNeutral(TextureUnit::NormalMap, textureOf(params.pbrNormalMap), NeutralTextureKind::FlatNormal2D)",
         "bindUnit(TextureUnit::MetallicRoughnessMap, textureOf(params.pbrMetallicRoughnessMap), false)",
         "bindUnit(TextureUnit::EmissiveMap, textureOf(params.pbrEmissiveMap), false)",
         "bindUnit(TextureUnit::OcclusionMap, textureOf(params.pbrOcclusionMap), false)",
         PbrFallbackStrategy::NeutralTexture},
        {"llgl",
         "params->pbrNormalMap, defaultFlatNormalPbrTexture_",
         "params->pbrMetallicRoughnessMap, defaultWhitePbrTexture_",
         "params->pbrEmissiveMap, defaultWhitePbrTexture_",
         "params->pbrOcclusionMap, defaultWhitePbrTexture_"},
        {"magnum",
         "params.pbrNormalMap, *defaultFlatNormalTexture_",
         "params.pbrMetallicRoughnessMap, *defaultWhiteTexture_",
         "params.pbrEmissiveMap, *defaultWhiteTexture_",
         "params.pbrOcclusionMap, *defaultWhiteTexture_"},
        {"metal",
         "params->pbrNormalMap, MetalStockTextureSlot::PbrNormal",
         "params->pbrMetallicRoughnessMap, MetalStockTextureSlot::PbrMetallicRoughness",
         "params->pbrEmissiveMap, MetalStockTextureSlot::PbrEmissive",
         "params->pbrOcclusionMap, MetalStockTextureSlot::PbrOcclusion"},
        {"opengl2",
         "params->pbrNormalMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture2D_)",
         "params->pbrMetallicRoughnessMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_)",
         "params->pbrEmissiveMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_)",
         "params->pbrOcclusionMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_)"},
        {"opengl4",
         "params.pbrNormalMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture_)",
         "params.pbrMetallicRoughnessMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_)",
         "params.pbrEmissiveMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_)",
         "params.pbrOcclusionMap->BindGL(); else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_)"},
        {"sdl-gpu",
         "command.normalMap ? command.normalMap.texture : defaultFlatNormalTexture_->Texture()",
         "command.metallicRoughnessMap ? command.metallicRoughnessMap.texture : defaultWhiteTexture_->Texture()",
         "command.emissiveMap ? command.emissiveMap.texture : defaultWhiteTexture_->Texture()",
         "command.occlusionMap ? command.occlusionMap.texture : defaultWhiteTexture_->Texture()"},
        {"vulkan",
         "vNorm = vsNorm ? vsNorm->GetVkImageView() : defaultFlatNormalView_",
         "vMR = vsMR ? vsMR->GetVkImageView() : defaultWhiteView_",
         "vEmis = vsEmis ? vsEmis->GetVkImageView() : defaultWhiteView_",
         "vOcc = vsOcc ? vsOcc->GetVkImageView() : defaultWhiteView_"},
        {"webgpu",
         "params.pbrNormalMap != nullptr ? ResolveSamplable(params.pbrNormalMap) : pbrDefaultFlatNormalTexture_->Sampled()",
         "params.pbrMetallicRoughnessMap != nullptr ? ResolveSamplable(params.pbrMetallicRoughnessMap) : pbrDefaultWhiteTexture_->Sampled()",
         "params.pbrEmissiveMap != nullptr ? ResolveSamplable(params.pbrEmissiveMap) : pbrDefaultWhiteTexture_->Sampled()",
         "params.pbrOcclusionMap != nullptr ? ResolveSamplable(params.pbrOcclusionMap) : pbrDefaultWhiteTexture_->Sampled()"},
        {"wicked",
         "const wig::Texture* normalMap = &flatNormalTexture_",
         "const wig::Texture* metallicRoughnessMap = &whiteTexture_",
         "const wig::Texture* emissiveMap = &whiteTexture_",
         "const wig::Texture* occlusionMap = &whiteTexture_"},
    }};

    struct RendererSlotAudit
    {
        const char* name;
        const char* abi;
        // Together these fragments form the CPU-field -> native binding -> shader-declaration
        // chain. Keeping the fragments renderer-specific is intentional: a WebGPU bind-group
        // binding and a Wicked HLSL resource register are not interchangeable kinds of "unit".
        std::array<const char*, 18> evidence;
    };

    constexpr std::array<RendererSlotAudit, 15> kSlotAudits{{
        {"bgfx", "stages 0 through 6",
         {{R"(texColor3DSampler_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler))",
           R"(normalMapSampler_ = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler))",
           R"(metallicRoughnessSampler_ = bgfx::createUniform("s_texMetallicRoughness", bgfx::UniformType::Sampler))",
           R"(emissiveMapSampler_ = bgfx::createUniform("s_texEmissive", bgfx::UniformType::Sampler))",
           R"(occlusionMapSampler_ = bgfx::createUniform("s_texOcclusion", bgfx::UniformType::Sampler))",
           R"(specularMapSampler_ = bgfx::createUniform("s_texSpecular", bgfx::UniformType::Sampler))",
           R"(specularColorMapSampler_ = bgfx::createUniform("s_texSpecularColor", bgfx::UniformType::Sampler))",
           R"(BindSamplerSlot(1, normalMapSampler_, params.pbrNormalMap, defaultFlatNormalTexture3D_);
              BindSamplerSlot(2, metallicRoughnessSampler_, params.pbrMetallicRoughnessMap, defaultWhiteTexture3D_);
              BindSamplerSlot(3, emissiveMapSampler_, params.pbrEmissiveMap, defaultWhiteTexture3D_);
              BindSamplerSlot(4, occlusionMapSampler_, params.pbrOcclusionMap, defaultWhiteTexture3D_);
              BindSamplerSlot(5, specularMapSampler_, params.pbrSpecularMap, defaultWhiteTexture3D_);
              BindSamplerSlot(6, specularColorMapSampler_, params.pbrSpecularColorMap, defaultWhiteTexture3D_);
              BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);)",
           R"(SAMPLER2D(s_texColor, 0);
              SAMPLER2D(s_texNormal, 1);
              SAMPLER2D(s_texMetallicRoughness, 2);
              SAMPLER2D(s_texEmissive, 3);
              SAMPLER2D(s_texOcclusion, 4);
              SAMPLER2D(s_texSpecular, 5);
              SAMPLER2D(s_texSpecularColor, 6);)"}}},
        {"diligent", "shader-resource names; sampler-state slots 0 through 6",
         {{R"(texture = params->texture0)",
           R"(cached.textureVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_Texture"))",
           R"(pipeline.textureVariable->Set(view))",
           R"(cached.normalMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_NormalMap"))",
           R"(cached.metallicRoughnessVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_MetallicRoughnessMap"))",
           R"(cached.emissiveMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_EmissiveMap"))",
           R"(cached.occlusionMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_OcclusionMap"))",
           R"(cached.specularMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_SpecularMap"))",
           R"(cached.specularColorMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_SpecularColorMap"))",
           R"(BindPbrMap(pipeline.normalMapVariable, params != nullptr ? params->pbrNormalMap : nullptr,
                         flatNormalTextureView_, 1))",
           R"(BindPbrMap(pipeline.metallicRoughnessVariable,
                         params != nullptr ? params->pbrMetallicRoughnessMap : nullptr,
                         fallbackTextureView_, 2))",
           R"(BindPbrMap(pipeline.emissiveMapVariable,
                         params != nullptr ? params->pbrEmissiveMap : nullptr, fallbackTextureView_, 3))",
           R"(BindPbrMap(pipeline.occlusionMapVariable,
                         params != nullptr ? params->pbrOcclusionMap : nullptr, fallbackTextureView_, 4))",
           R"(BindPbrMap(pipeline.specularMapVariable,
                         params != nullptr ? params->pbrSpecularMap : nullptr, fallbackTextureView_, 5))",
           R"(BindPbrMap(pipeline.specularColorMapVariable,
                         params != nullptr ? params->pbrSpecularColorMap : nullptr,
                         fallbackTextureView_, 6))",
           R"(Texture2D g_Texture;
              SamplerState g_Texture_sampler;
              Texture2D g_NormalMap;
              SamplerState g_NormalMap_sampler;
              Texture2D g_MetallicRoughnessMap;
              SamplerState g_MetallicRoughnessMap_sampler;
              Texture2D g_EmissiveMap;
              SamplerState g_EmissiveMap_sampler;
              Texture2D g_OcclusionMap;
              SamplerState g_OcclusionMap_sampler;
              Texture2D g_SpecularMap;
              SamplerState g_SpecularMap_sampler;
              Texture2D g_SpecularColorMap;
              SamplerState g_SpecularColorMap_sampler;)"}}},
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
        {"directx11", "SRV/sampler registers t0/s0 through t6/s6",
         {{R"(srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                        : GetOrCreateDefaultWhiteSrvEXT();
              srvs[1] = params.pbrNormalMap ? GetSrvForTextureEXT(params.pbrNormalMap) : GetOrCreateDefaultFlatNormalSrvEXT();
              srvs[2] = params.pbrMetallicRoughnessMap ? GetSrvForTextureEXT(params.pbrMetallicRoughnessMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[3] = params.pbrEmissiveMap ? GetSrvForTextureEXT(params.pbrEmissiveMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[4] = params.pbrOcclusionMap ? GetSrvForTextureEXT(params.pbrOcclusionMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[5] = params.pbrSpecularMap ? GetSrvForTextureEXT(params.pbrSpecularMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[6] = params.pbrSpecularColorMap ? GetSrvForTextureEXT(params.pbrSpecularColorMap) : GetOrCreateDefaultWhiteSrvEXT();)",
           R"(context_->PSSetShaderResources(0, 7, srvs))",
           R"(Texture2D uTexture : register(t0);
              SamplerState uTextureSampler : register(s0);
              Texture2D uNormalMap : register(t1);
              SamplerState uNormalMapSampler : register(s1);
              Texture2D uMetallicRoughnessMap : register(t2);
              SamplerState uMetallicRoughnessSampler : register(s2);
              Texture2D uEmissiveMap : register(t3);
              SamplerState uEmissiveMapSampler : register(s3);
              Texture2D uOcclusionMap : register(t4);
              SamplerState uOcclusionMapSampler : register(s4);
              Texture2D uSpecularMap : register(t5);
              SamplerState uSpecularMapSampler : register(s5);
              Texture2D uSpecularColorMap : register(t6);
              SamplerState uSpecularColorMapSampler : register(s6);)"}}},
        {"directx12", "separate descriptor tables for t0/s0 through t6/s6",
         {{R"(srvTextures[0] = params.texture0;
              srvTextures[1] = params.pbrNormalMap ? params.pbrNormalMap : GetOrCreateDefaultFlatNormalTextureEXT();
              srvTextures[2] = params.pbrMetallicRoughnessMap ? params.pbrMetallicRoughnessMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[3] = params.pbrEmissiveMap ? params.pbrEmissiveMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[4] = params.pbrOcclusionMap ? params.pbrOcclusionMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[5] = params.pbrSpecularMap ? params.pbrSpecularMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[6] = params.pbrSpecularColorMap ? params.pbrSpecularColorMap : GetOrCreateDefaultWhiteTextureEXT();)",
           R"(range.BaseShaderRegister = static_cast<UINT>(t))",
           R"(cmdList->SetGraphicsRootDescriptorTable(numCbvs + i, srvHandles[i]))",
           R"(cmdList->SetGraphicsRootDescriptorTable(numCbvs + numSrvs + i, GetSamplerGpuHandleEXT(i)))",
           R"(Texture2D uTexture : register(t0);
              SamplerState uTextureSampler : register(s0);
              Texture2D uNormalMap : register(t1);
              SamplerState uNormalMapSampler : register(s1);
              Texture2D uMetallicRoughnessMap : register(t2);
              SamplerState uMetallicRoughnessSampler : register(s2);
              Texture2D uEmissiveMap : register(t3);
              SamplerState uEmissiveMapSampler : register(s3);
              Texture2D uOcclusionMap : register(t4);
              SamplerState uOcclusionMapSampler : register(s4);
              Texture2D uSpecularMap : register(t5);
              SamplerState uSpecularMapSampler : register(s5);
              Texture2D uSpecularColorMap : register(t6);
              SamplerState uSpecularColorMapSampler : register(s6);)"}}},
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
        {"llgl", "pipeline bindings 2,4,6,8,10 (paired samplers 3,5,7,9,11)",
         {{R"(LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                      LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2})",
           R"(LLGL::BindingDescriptor{"normalMap", LLGL::ResourceType::Texture,
                                      LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 4})",
           R"(LLGL::BindingDescriptor{"metallicRoughnessMap", LLGL::ResourceType::Texture,
                                      LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 6})",
           R"(LLGL::BindingDescriptor{"emissiveMap", LLGL::ResourceType::Texture,
                                      LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 8})",
           R"(LLGL::BindingDescriptor{"occlusionMap", LLGL::ResourceType::Texture,
                                      LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 10})",
           R"(commands_->SetResource(1, *command.texture))",
           R"(commands_->SetResource(3, *command.pbrNormalTexture))",
           R"(commands_->SetResource(5, *command.pbrMetallicRoughnessTexture))",
           R"(commands_->SetResource(7, *command.pbrEmissiveTexture))",
           R"(commands_->SetResource(9, *command.pbrOcclusionTexture))",
           R"(layout(binding = 2) uniform texture2D colorMap;)" ,
           R"(layout(binding = 4) uniform texture2D normalMap;)" ,
           R"(layout(binding = 6) uniform texture2D metallicRoughnessMap;)" ,
           R"(layout(binding = 8) uniform texture2D emissiveMap;)" ,
           R"(layout(binding = 10) uniform texture2D occlusionMap;)"}}},
        {"magnum", "GL texture units 0 through 6",
         {{R"(constexpr int kPbrNormalMapSlot = 1;
              constexpr int kPbrMetallicRoughnessMapSlot = 2;
              constexpr int kPbrEmissiveMapSlot = 3;
              constexpr int kPbrOcclusionMapSlot = 4;
              constexpr int kPbrSpecularMapSlot = 5;
              constexpr int kPbrSpecularColorMapSlot = 6;)",
           R"(BindTextureToSlot(0, params.texture0);
              program.SetInt(program.LocationOf("uTexture"), 0);)",
           R"(BindPbrMap(program, "uNormalMap", kPbrNormalMapSlot, params.pbrNormalMap)",
           R"(BindPbrMap(program, "uMetallicRoughnessMap", kPbrMetallicRoughnessMapSlot)",
           R"(params.pbrMetallicRoughnessMap, *defaultWhiteTexture_)",
           R"(BindPbrMap(program, "uEmissiveMap", kPbrEmissiveMapSlot, params.pbrEmissiveMap)",
           R"(BindPbrMap(program, "uOcclusionMap", kPbrOcclusionMapSlot, params.pbrOcclusionMap)",
           R"(BindPbrMap(program, "uSpecularMap", kPbrSpecularMapSlot, params.pbrSpecularMap)",
           R"(BindPbrMap(program, "uSpecularColorMap", kPbrSpecularColorMapSlot)",
           R"(params.pbrSpecularColorMap, *defaultWhiteTexture_, specularColorFlip)",
           R"(uniform sampler2D uTexture;)" ,
           R"(uniform sampler2D uNormalMap;)" ,
           R"(uniform sampler2D uMetallicRoughnessMap;)" ,
           R"(uniform sampler2D uEmissiveMap;)" ,
           R"(uniform sampler2D uOcclusionMap;)" ,
           R"(uniform sampler2D uSpecularMap;)" ,
           R"(uniform sampler2D uSpecularColorMap;)"}}},
        {"metal", "fragment texture/sampler indices 0,1,2,3,4",
         {{R"(texture0=resolveMetal2DTextureBinding(p,params->texture0,MetalStockTextureSlot::PbrBaseColor))",
           R"(normalMap=resolveMetal2DTextureBinding(p,params->pbrNormalMap,MetalStockTextureSlot::PbrNormal))",
           R"(metallicRoughnessMap=resolveMetal2DTextureBinding(p,params->pbrMetallicRoughnessMap,MetalStockTextureSlot::PbrMetallicRoughness))",
           R"(emissiveMap=resolveMetal2DTextureBinding(p,params->pbrEmissiveMap,MetalStockTextureSlot::PbrEmissive))",
           R"(occlusionMap=resolveMetal2DTextureBinding(p,params->pbrOcclusionMap,MetalStockTextureSlot::PbrOcclusion))",
           R"([p.encoder setFragmentTexture:texture0 atIndex:0])",
           R"([p.encoder setFragmentTexture:normalMap atIndex:1])",
           R"([p.encoder setFragmentTexture:metallicRoughnessMap atIndex:2])",
           R"([p.encoder setFragmentTexture:emissiveMap atIndex:3])",
           R"([p.encoder setFragmentTexture:occlusionMap atIndex:4])",
           R"(texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
              texture2d<float> normalMap [[texture(1)]], sampler normalSmp [[sampler(1)]],
              texture2d<float> mrMap [[texture(2)]], sampler mrSmp [[sampler(2)]],
              texture2d<float> emissiveMap [[texture(3)]], sampler emissiveSmp [[sampler(3)]],
              texture2d<float> occlusionMap [[texture(4)]], sampler occlusionSmp [[sampler(4)]])"}}},
        {"opengl2", "GL texture units 0,1,2,3,4",
         {{R"(glActiveTexture(GL_TEXTURE0);
              if (params->texture0) params->texture0->BindGL())",
           R"(glUniform1i(glGetUniformLocation(program, "uTex"), 0))",
           R"(glActiveTexture(GL_TEXTURE1);
              if (params->pbrNormalMap) params->pbrNormalMap->BindGL())",
           R"(glUniform1i(glGetUniformLocation(program, "uNormalMap"), 1))",
           R"(glActiveTexture(GL_TEXTURE2);
              if (params->pbrMetallicRoughnessMap) params->pbrMetallicRoughnessMap->BindGL())",
           R"(glUniform1i(glGetUniformLocation(program, "uMetallicRoughnessMap"), 2))",
           R"(glActiveTexture(GL_TEXTURE3);
              if (params->pbrEmissiveMap) params->pbrEmissiveMap->BindGL())",
           R"(glUniform1i(glGetUniformLocation(program, "uEmissiveMap"), 3))",
           R"(glActiveTexture(GL_TEXTURE4);
              if (params->pbrOcclusionMap) params->pbrOcclusionMap->BindGL())",
           R"(glUniform1i(glGetUniformLocation(program, "uOcclusionMap"), 4))",
           R"(uniform sampler2D uTex;)",
           R"(uniform sampler2D uNormalMap;)",
           R"(uniform sampler2D uMetallicRoughnessMap;)",
           R"(uniform sampler2D uEmissiveMap;)",
           R"(uniform sampler2D uOcclusionMap;)"}}},
        {"opengl4", "GL texture units 0,1,2,3,4",
         {{R"(gl4_glActiveTexture(GL_TEXTURE0);
              params.texture0->BindGL())",
           R"(gl4_glActiveTexture(GL_TEXTURE1);
              if (params.pbrNormalMap) params.pbrNormalMap->BindGL())",
           R"(gl4_glActiveTexture(GL_TEXTURE2);
              if (params.pbrMetallicRoughnessMap) params.pbrMetallicRoughnessMap->BindGL())",
           R"(gl4_glActiveTexture(GL_TEXTURE3);
              if (params.pbrEmissiveMap) params.pbrEmissiveMap->BindGL())",
           R"(gl4_glActiveTexture(GL_TEXTURE4);
              if (params.pbrOcclusionMap) params.pbrOcclusionMap->BindGL())",
           R"(gl4_glUniform1i(texLoc, 0))",
           R"(gl4_glUniform1i(normalMapLoc, 1))",
           R"(gl4_glUniform1i(mrLoc, 2))",
           R"(gl4_glUniform1i(emissiveMapLoc, 3))",
           R"(gl4_glUniform1i(occlusionMapLoc, 4))",
           R"(uniform sampler2D uTexture;
              uniform sampler2D uNormalMap;
              uniform sampler2D uMetallicRoughnessMap;
              uniform sampler2D uEmissiveMap;
              uniform sampler2D uOcclusionMap;)"}}},
        {"sdl-gpu", "fragment sampler bindings 0 through 6",
         {{R"(samplerBindings[0].texture = command.texture.texture)",
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
        {"vulkan", "descriptor set 0 bindings 0,1,2,3,4",
         {{R"(VkImageView views[7] = { baseColor, normalMap, metallicRoughness, emissive, occlusion,
                                      specular, specularColor })",
           R"(writes[i].dstBinding = i)",
           R"(GetOrCreatePbrDescSet(
                currentFrame_, vBase, vNorm, vMR, vEmis, vOcc, vSpec, vSpecColor,
                                    PbrSlotSamplersRawEXT().s))",
           R"(layout(set = 0, binding = 0) uniform sampler2D uTexture;
              layout(set = 0, binding = 1) uniform sampler2D uNormalMap;
              layout(set = 0, binding = 2) uniform sampler2D uMetallicRoughnessMap;
              layout(set = 0, binding = 3) uniform sampler2D uEmissiveMap;
              layout(set = 0, binding = 4) uniform sampler2D uOcclusionMap;)"}}},
        {"webgpu", "bind group 1: sampler 0, textures 1,2,3,4,5",
         {{R"(texEntries[1].binding = 1;
              texEntries[1].textureView = command.baseColorTexture.View();
              texEntries[2].binding = 2;
              texEntries[2].textureView = command.normalMap.View();
              texEntries[3].binding = 3;
              texEntries[3].textureView = command.metallicRoughnessMap.View();
              texEntries[4].binding = 4;
              texEntries[4].textureView = command.emissiveMap.View();
              texEntries[5].binding = 5;
              texEntries[5].textureView = command.occlusionMap.View();)",
           R"(@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
              @group(1) @binding(2) var normalTex: texture_2d<f32>;
              @group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
              @group(1) @binding(4) var emissiveTex: texture_2d<f32>;
              @group(1) @binding(5) var occlusionTex: texture_2d<f32>;)",
           R"(command.normalMap = params.pbrNormalMap != nullptr)",
           R"(command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr)",
           R"(command.emissiveMap = params.pbrEmissiveMap != nullptr)",
           R"(command.occlusionMap = params.pbrOcclusionMap != nullptr)"}}},
        {"wicked", "HLSL resources t0,t3,t4,t5,t6",
         {{R"(device_->BindResource(resolveTexture(texture0), 0, cmd))",
           R"(device_->BindResource(normalMap, 3, cmd))",
           R"(device_->BindResource(metallicRoughnessMap, 4, cmd))",
           R"(device_->BindResource(emissiveMap, 5, cmd))",
           R"(device_->BindResource(occlusionMap, 6, cmd))",
           R"(Texture2D<float4> texture0 : register(t0))",
           R"(Texture2D<float4> normalMap : register(t3))",
           R"(Texture2D<float4> metallicRoughnessMap : register(t4))",
           R"(Texture2D<float4> emissiveMap : register(t5))",
           R"(Texture2D<float4> occlusionMap : register(t6))"}}},
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

    constexpr std::array<RendererAlphaAudit, 15> kAlphaAudits{{
        {"bgfx", {{
            R"(bgfx::setUniform(alphaTestUnif_, params.alphaTest))",
            R"(BindPbrTextures(params); SubmitViewProgram(pbr3DProgram_);)",
            R"(float at = (u_alphaTest.y > 0.0))",
            R"(if (at < 0.0) discard;)"}}},
        {"diligent", {{
            R"(constants.alphaTest[component] = params->alphaTest[component])",
            R"(psOut.Color = FinishPixel(float4(ambient + Lo + emissive, alpha), psIn.FogKeep))",
            R"(float weight = passesAlphaTest ? g_AlphaTest.z : g_AlphaTest.w)",
            R"(if (weight < 0.0) discard;)"}}},
        {"directx9", {{
            R"(TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "AlphaTest", params.alphaTest))",
            R"(float4 AlphaTest : register(c11))",
            R"(float alphaTestResult = (AlphaTest.y > 0.0))",
            R"(clip(alphaTestResult))"}}},
        {"directx11", {{
            R"(const bool needsPbr = params.pbr; const bool needsAlphaTest = !needsPbr)",
            R"(perDraw.AlphaTest[0] = params.alphaTest[0])",
            R"(bool passesAlphaTest = (AlphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? AlphaTest.z : AlphaTest.w) < 0.0) discard;)"}}},
        {"directx12", {{
            R"(const bool needsPbr = params.pbr; const bool needsAlphaTest = !needsPbr)",
            R"(perDraw.AlphaTest[0] = params.alphaTest[0])",
            R"(bool passesAlphaTest = (AlphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? AlphaTest.z : AlphaTest.w) < 0.0) discard;)"}}},
        {"easygl", {{
            R"(p.prog.set_uniform(p.loc_alphatest, params.alphaTest[0], params.alphaTest[1], params.alphaTest[2], params.alphaTest[3]))",
            R"("uniform vec4 uAlphaTest;\n")",
            R"(float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w))",
            R"("    if(_at<0.0)discard;\n")"}}},
        {"llgl", {{
            R"(uniforms[84] = params.alphaTest[0])",
            R"(vec4 alphaTest;)",
            R"(bool passesAlphaTest = (alphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? alphaTest.z : alphaTest.w) < 0.0) discard;)"}}},
        {"magnum", {{
            R"(program.SetVector4(program.LocationOf("uAlphaTest"), Mg::Vector4{params.alphaTest[0], params.alphaTest[1], params.alphaTest[2], params.alphaTest[3]}))",
            R"(source += "    fragColor = vec4(ambient + reflected + emissive, alpha);\n"; source += kAlphaTestFragmentTerm;)",
            R"(float cnaAlphaTest = (uAlphaTest.y > 0.0))",
            R"(if (cnaAlphaTest < 0.0) discard;)"}}},
        {"metal", {{
            R"(std::memcpy(pu.alphaTest, params.alphaTest, sizeof(pu.alphaTest)))",
            R"(float4 c = float4(ambient + Lo + emissive, alpha))",
            R"(cna_alpha_test_fails(c.a, pu.alphaTest))",
            R"(discard_fragment())"}}},
        {"opengl2", {{
            R"(if (params) std::memcpy(alphaTest, params->alphaTest, sizeof(alphaTest)))",
            R"(glUniform4fv(glGetUniformLocation(program, "uAlphaTest"), 1, alphaTest))",
            R"(gl_FragData[0]=vec4(ambient+Lo+emissive,alpha))",
            R"(if(_at<0.0)discard)"}}},
        {"opengl4", {{
            R"(OpenGL4RawProgram& prog = skinnedPbr ? pbrSkinned3DProgram_ : pbr3DProgram_)",
            R"(gl4_glUniform4f(alphaTestLoc, params.alphaTest[0], params.alphaTest[1], params.alphaTest[2], params.alphaTest[3]))",
            R"(bool passesAlphaTest = (uAlphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? uAlphaTest.z : uAlphaTest.w) < 0.0) discard;)"}}},
        {"sdl-gpu", {{
            R"(out[4] = p.alphaTest[0])",
            R"(const bool needsAlphaTest = !needsPbr)",
            R"(bool passesAlphaTest = (pbrp.alphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? pbrp.alphaTest.z : pbrp.alphaTest.w) < 0.0) discard;)"}}},
        {"vulkan", {{
            R"(out[48] = p.alphaTest[0])",
            R"(const bool needsAlphaTest = !needsPbr)",
            R"(bool passesAlphaTest = (pbr.alphaTest.y > 0.0))",
            R"(if ((passesAlphaTest ? pbr.alphaTest.z : pbr.alphaTest.w) < 0.0) discard;)"}}},
        {"webgpu", {{
            R"(out[4] = p.alphaTest[0])",
            R"(const bool needsAlphaTest = !params.pbr)",
            R"(let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest))",
            R"(if (alphaWeight < 0.0) { discard;)"}}},
        {"wicked", {{
            R"(std::copy_n(params->alphaTest, 4, constants.alphaTest))",
            R"(const float selected = (cb.alphaTest.y > 0.0f))",
            R"((alpha < cb.alphaTest.x) ? cb.alphaTest.z : cb.alphaTest.w)",
            R"(clip(selected))"}}},
    }};

    struct RendererPbrScalarAudit
    {
        const char* name;
        // Evidence covers both CPU-side values and both glTF shader equations: normalTexture.scale
        // affects tangent-space X/Y only, while occlusionTexture.strength interpolates from 1.
        std::array<const char*, 4> evidence;
    };

    constexpr std::array<RendererPbrScalarAudit, 15> kPbrScalarAudits{{
        {"bgfx", {{
            R"(float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                     params.pbrNormalScale, params.pbrOcclusionStrength })",
            R"(uniform vec4 u_metallicRoughnessFactor;)",
            R"(sampledNormal.xy *= u_metallicRoughnessFactor.z;)",
            R"(occlusion = 1.0 + u_metallicRoughnessFactor.w * (occlusion - 1.0);)"}}},
        {"diligent", {{
            R"(params.pbrNormalScale, params.pbrOcclusionStrength,
                params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
                params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f)",
            R"(float4 g_PbrMapScales;)",
            R"(sampledNormal.xy *= g_PbrMapScales.x;)",
            R"(occlusion = 1.0 + g_PbrMapScales.y * (occlusion - 1.0);)"}}},
        {"directx9", {{
            R"(params.pbrNormalScale, params.pbrOcclusionStrength})",
            R"(float4 MetallicRoughnessFactor : register(c3))",
            R"(sampledNormal.xy *= MetallicRoughnessFactor.z;)",
            R"(occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);)"}}},
        {"directx11", {{
            R"(perDraw.PbrMapScales[0] = params.pbrNormalScale;)",
            R"(perDraw.PbrMapScales[1] = params.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= PbrMapScales.x;)",
            R"(occlusion = 1.0 + PbrMapScales.y * (occlusion - 1.0);)"}}},
        {"directx12", {{
            R"(perDraw.PbrMapScales[0] = params.pbrNormalScale;)",
            R"(perDraw.PbrMapScales[1] = params.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= PbrMapScales.x;)",
            R"(occlusion = 1.0 + PbrMapScales.y * (occlusion - 1.0);)"}}},
        {"easygl", {{
            R"(p.prog.set_uniform(p.loc_pbr_normalscale, params.pbrNormalScale))",
            R"(p.prog.set_uniform(p.loc_pbr_occlstrength, params.pbrOcclusionStrength))",
            R"("    sampledNormal.xy*=uNormalScale;\n")",
            R"("    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n")"}}},
        {"llgl", {{
            R"(uniforms[46] = params.pbrNormalScale;)",
            R"(uniforms[47] = params.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= roughnessWeightsPad.z;)",
            R"(float occlusion = 1.0 + roughnessWeightsPad.w * (occlusionSample - 1.0);)"}}},
        {"magnum", {{
            R"(program.SetFloat(program.LocationOf("uNormalScale"), params.pbrNormalScale))",
            R"(program.SetFloat(program.LocationOf("uOcclusionStrength"), params.pbrOcclusionStrength))",
            R"(source += "    sampledNormal.xy *= uNormalScale;\n";)",
            R"(source += "    float occlusion = 1.0 + uOcclusionStrength * (occlusionSample - 1.0);\n";)"}}},
        {"metal", {{
            R"(pu.pbrFactors[2]=params.pbrNormalScale; pu.pbrFactors[3]=params.pbrOcclusionStrength;)",
            R"(float4 pbrFactors;)",
            R"(sampledNormal.xy *= pu.pbrFactors.z;)",
            R"(float occlusion = 1.0 + pu.pbrFactors.w * (occlusionSample - 1.0);)"}}},
        {"opengl2", {{
            R"(glUniform1f(glGetUniformLocation(program, "uNormalScale"), params->pbrNormalScale))",
            R"(glUniform1f(glGetUniformLocation(program, "uOcclusionStrength"), params->pbrOcclusionStrength))",
            R"("sampledNormal.xy*=uNormalScale;")",
            R"("float occlusion=1.0+uOcclusionStrength*(occlusionSample-1.0);")"}}},
        {"opengl4", {{
            R"(gl4_glUniform1f(normalScaleLoc, params.pbrNormalScale))",
            R"(gl4_glUniform1f(occlusionStrengthLoc, params.pbrOcclusionStrength))",
            R"(sampledNormal.xy *= uNormalScale;)",
            R"(float occlusion = 1.0 + uOcclusionStrength * (occlusionSample - 1.0);)"}}},
        {"sdl-gpu", {{
            R"(out[2] = p.pbrNormalScale;)",
            R"(out[3] = p.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= pbrp.normalScale;)",
            R"(float occlusion = 1.0 + pbrp.occlusionStrength * (occlusionSample - 1.0);)"}}},
        {"vulkan", {{
            R"(out[52] = p.pbrNormalScale;)",
            R"(out[53] = p.pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= pbr.pbrMapScales.x;)",
            R"(float occlusion = 1.0 + pbr.pbrMapScales.y * (occlusionSample - 1.0);)"}}},
        {"webgpu", {{
            R"(out[2] = p.pbrNormalScale;)",
            R"(out[3] = p.pbrOcclusionStrength;)",
            R"(sampledNormal.x *= pf.metallicRoughness.z; sampledNormal.y *= pf.metallicRoughness.z;)",
            R"(let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);)"}}},
        {"wicked", {{
            R"(constants.pbrFactors[2] = params->pbrNormalScale;)",
            R"(constants.pbrFactors[3] = params->pbrOcclusionStrength;)",
            R"(sampledNormal.xy *= cb.pbrFactors.z;)",
            R"(const float occlusion = 1.0f + cb.pbrFactors.w * (occlusionSample - 1.0f);)"}}},
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
    constexpr std::array<RendererPbrTextureTransformAudit, 15> kPbrTextureTransformAudits{{
        {"bgfx",
         "bgfx::setUniform(pbrTextureTransformUnif_, params.pbrTextureTransformRows, 10)",
         "u_pbrTextureTransform[slot * 2 + 1].xyz",
         {{"texture2D(s_texColor, rtFlipUV(pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 0), 0), u_rtFlipV.x))",
           "texture2D(s_texNormal, rtFlipUV(pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 1), 1), u_rtFlipV.y))",
           "texture2D(s_texMetallicRoughness, rtFlipUV(pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 2), 2), u_rtFlipV.z))",
           "texture2D(s_texEmissive, rtFlipUV(pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 3), 3), u_rtFlipV.w))",
           "texture2D(s_texOcclusion, pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 4), 4))"}}, 1},
        {"diligent",
         "std::memcpy(values + 20, params.pbrTextureTransformRows",
         "g_PbrTextureTransformRows[slot * 2 + 1].xyz",
         {{"g_Texture.Sample(g_Texture_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 0), 0))",
           "g_NormalMap.Sample(g_NormalMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 1), 1))",
           "g_MetallicRoughnessMap.Sample(g_MetallicRoughnessMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 2), 2))",
           "g_EmissiveMap.Sample(g_EmissiveMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 3), 3))",
           "g_OcclusionMap.Sample(g_OcclusionMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 4), 4))"}}, 1},
        {"directx9",
         "TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, \"TextureTransformRows\", &params.pbrTextureTransformRows[0][0])",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"tex2D(Texture, CnaPbrTransformUv(pin.UV, 0))",
           "tex2D(NormalMap, CnaPbrTransformUv(pin.UV, 1))",
           "tex2D(MetallicRoughnessMap, CnaPbrTransformUv(pin.UV, 2))",
           "tex2D(EmissiveMap, CnaPbrTransformUv(pin.UV, 3))",
           "tex2D(OcclusionMap, CnaPbrTransformUv(pin.UV, 4))"}}, 2},
        {"directx11",
         "std::memcpy(perDraw.TextureTransformRows, params.pbrTextureTransformRows",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"uTexture.Sample(uTextureSampler, CnaPbrTransformUv(CNA_PBR_UV(0), 0))",
           "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(CNA_PBR_UV(1), 1))",
           "uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, CnaPbrTransformUv(CNA_PBR_UV(2), 2))",
           "uEmissiveMap.Sample(uEmissiveMapSampler, CnaPbrTransformUv(CNA_PBR_UV(3), 3))",
           "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(CNA_PBR_UV(4), 4))"}}, 2},
        {"directx12",
         "std::memcpy(perDraw.TextureTransformRows, params.pbrTextureTransformRows",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"uTexture.Sample(uTextureSampler, CnaPbrTransformUv(CNA_PBR_UV(0), 0))",
           "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(CNA_PBR_UV(1), 1))",
           "uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, CnaPbrTransformUv(CNA_PBR_UV(2), 2))",
           "uEmissiveMap.Sample(uEmissiveMapSampler, CnaPbrTransformUv(CNA_PBR_UV(3), 3))",
           "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(CNA_PBR_UV(4), 4))"}}, 2},
        {"easygl",
         "const float* values = params.pbrTextureTransformRows[row]",
         "uTextureTransformRows[slot*2+1].xyz",
         {{"texture(uTexture,cnaSampleUV(cnaPbrTransformUV(\" + baseUv + \",0),uRtFlipV.x))",
           "texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(\" + normalUv + \",1),uRtFlipV.y))",
           "texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrTransformUV(\" + mrUv + \",2),uRtFlipV.z))",
           "texture(uEmissiveMap,cnaSampleUV(cnaPbrTransformUV(\" + emissiveUv + \",3),uRtFlipV.w))",
           "texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(\" + occlusionUv + \",4),uRtFlipVHi.x))"}}, 2},
        {"llgl",
         "uniforms[92 + row * 4 + component] = params.pbrTextureTransformRows[row][component]",
         "textureTransformRows[slot * 2 + 1].xyz",
         {{"texture(sampler2D(colorMap, samplerState), cnaPbrTransformUV(cnaPbrUv(0), 0))",
           "texture(sampler2D(normalMap, normalMapSampler), cnaPbrTransformUV(cnaPbrUv(1), 1))",
           "texture(sampler2D(metallicRoughnessMap, metallicRoughnessMapSampler), cnaPbrTransformUV(cnaPbrUv(2), 2))",
           "texture(sampler2D(emissiveMap, emissiveMapSampler), cnaPbrTransformUV(cnaPbrUv(3), 3))",
           "texture(sampler2D(occlusionMap, occlusionMapSampler), cnaPbrTransformUV(cnaPbrUv(4), 4))"}}, 1},
        {"magnum",
         "const float* values = params.pbrTextureTransformRows[row]",
         "uTextureTransformRows[slot * 2 + 1].xyz",
         {{"texture(uTexture, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 0), uRtFlipV.x))",
           "texture(uNormalMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 1), uRtFlipV.y))",
           "texture(uMetallicRoughnessMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 2), uRtFlipV.z))",
           "texture(uEmissiveMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 3), uRtFlipV.w))",
           "texture(uOcclusionMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 4), uRtFlipVHi.x))"}}, 1},
        {"metal",
         "std::memcpy(pu.textureTransformRows, params.pbrTextureTransformRows",
         "pu.textureTransformRows[slot * 2 + 1].xyz",
         {{"tex.sample(smp, cna_pbr_transform_uv(in.uv, 0, pu))",
           "normalMap.sample(normalSmp, cna_pbr_transform_uv(in.uv, 1, pu))",
           "mrMap.sample(mrSmp, cna_pbr_transform_uv(in.uv, 2, pu))",
           "emissiveMap.sample(emissiveSmp, cna_pbr_transform_uv(in.uv, 3, pu))",
           "occlusionMap.sample(occlusionSmp, cna_pbr_transform_uv(in.uv, 4, pu))"}}, 1},
        {"opengl2",
         "&params->pbrTextureTransformRows[0][0]",
         "uTextureTransformRows[slot*2+1].xyz",
         {{"texture2D(uTex,cnaPbrTransformUV(vTex,0))",
           "texture2D(uNormalMap,cnaPbrTransformUV(vTex,1))",
           "texture2D(uMetallicRoughnessMap,cnaPbrTransformUV(vTex,2))",
           "texture2D(uEmissiveMap,cnaPbrTransformUV(vTex,3))",
           "texture2D(uOcclusionMap,cnaPbrTransformUV(vTex,4))"}}, 1},
        {"opengl4",
         "const float* values = params.pbrTextureTransformRows[row]",
         "uTextureTransformRows[slot * 2 + 1].xyz",
         {{"texture(uTexture, cnaPbrTransformUV(vUV, 0))",
           "texture(uNormalMap, cnaPbrTransformUV(vUV, 1))",
           "texture(uMetallicRoughnessMap, cnaPbrTransformUV(vUV, 2))",
           "texture(uEmissiveMap, cnaPbrTransformUV(vUV, 3))",
           "texture(uOcclusionMap, cnaPbrTransformUV(vUV, 4))"}}, 1},
        {"sdl-gpu",
         "p.pbrTextureTransformRows[row][component]",
         "pbrp.textureTransformRows[slot * 2 + 1].xyz",
         {{"texture(uTexture, cnaPbrTransformUV(fragUV, 0))",
           "texture(uNormalMap, cnaPbrTransformUV(fragUV, 1))",
           "texture(uMetallicRoughnessMap, cnaPbrTransformUV(fragUV, 2))",
           "texture(uEmissiveMap, cnaPbrTransformUV(fragUV, 3))",
           "texture(uOcclusionMap, cnaPbrTransformUV(fragUV, 4))"}}, 1},
        {"vulkan",
         "out[64 + row * 4 + component] = p.pbrTextureTransformRows[row][component]",
         "pbr.textureTransformRows[slot * 2 + 1].xyz",
         {{"texture(uTexture, CnaPbrTransformUV(CNA_PBR_UV(0), 0))",
           "texture(uNormalMap, CnaPbrTransformUV(CNA_PBR_UV(1), 1))",
           "texture(uMetallicRoughnessMap, CnaPbrTransformUV(CNA_PBR_UV(2), 2))",
           "texture(uEmissiveMap, CnaPbrTransformUV(CNA_PBR_UV(3), 3))",
           "texture(uOcclusionMap, CnaPbrTransformUV(CNA_PBR_UV(4), 4))"}}, 2},
        {"webgpu",
         "p.pbrTextureTransformRows[row][component]",
         "pf.textureTransformRows[slot * 2u + 1u].xyz",
         {{"textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u))",
           "textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u))",
           "textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u))",
           "textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u))",
           "textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u))"}}, 2},
        {"wicked",
         "std::memcpy(constants.pbrTextureTransformRows, params->pbrTextureTransformRows",
         "cb.pbrTextureTransformRows[slot * 2 + 1].xyz",
         {{"texture0.Sample(sampler0, CnaPbrTransformUv(input.uv, 0))",
           "normalMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 1))",
           "metallicRoughnessMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 2))",
           "emissiveMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 3))",
           "occlusionMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 4))"}}, 1},
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
    // The copy count distinguishes separately stored rigid/skinned fragment programs and LLGL's
    // GL/Vulkan plus generated-GL sources from backends that share one fragment program.
    constexpr std::array<RendererPbrFresnelAudit, 15> kPbrFresnelAudits{{
        {"bgfx",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3_splat(specularWeight), vec3_splat(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"diligent",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 1},
        {"directx9",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"directx11",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"directx12",
         "float3 F0 = lerp(dielectricF0, albedo, metallic)",
         "float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"easygl",
         "vec3 F0=mix(dielectricF0,albedo,metallic)",
         "vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic)",
         "vec3 F=F0+(F90-F0)*", 2},
        {"llgl",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 3},
        {"magnum",
         "vec3 f0 = mix(dielectricF0, albedo, metallic)",
         "vec3 f90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 fresnel = f0 + (f90 - f0) *", 1},
        {"metal",
         "float3 F0 = mix(pu.dielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = mix(float3(pu.dielectricFresnel.w), float3(1.0), metallic)",
         "float3 F = F0 + (F90-F0) *", 1},
        {"opengl2",
         "vec3 F0=mix(dielectricF0,albedo,metallic)",
         "vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic)",
         "vec3 F=F0+(F90-F0)*", 1},
        {"opengl4",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"sdl-gpu",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"vulkan",
         "vec3 F0 = mix(dielectricF0, albedo, metallic)",
         "vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 2},
        {"webgpu",
         "let f0 = mix(dielectricF0, albedo, metallic)",
         "let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic)",
         "let f = f0 + (f90 - f0) *", 2},
        {"wicked",
         "const float3 F0 = lerp(cb.pbrDielectricFresnel.xyz, albedo, metallic)",
         "const float3 F90 = lerp(float3(cb.pbrDielectricFresnel.w, cb.pbrDielectricFresnel.w, cb.pbrDielectricFresnel.w), float3(1.0f, 1.0f, 1.0f), metallic)",
         "const float3 F = F0 + (F90 - F0) *", 1},
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
    constexpr std::array<RendererPbrColorSpaceAudit, 15> kPbrColorSpaceAudits{{
        {"bgfx",
         "mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), u_srgb.x)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), u_srgb.y)",
         "mix(result.rgb, cnaLinearToSrgb(result.rgb), u_srgb.z)", 1},
        {"diligent",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), g_PbrMapScales.z)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), g_PbrMapScales.w)",
         "lerp(color.rgb, CnaLinearToSrgb(color.rgb), g_FogColor.w)", 1},
        {"directx9",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), AmbientColor.w)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), EmissiveColor.w)",
         "lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w)", 2},
        {"directx11",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), PbrMapScales.z)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), PbrMapScales.w)",
         "lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w)", 2},
        {"directx12",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), PbrMapScales.z)",
         "lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), PbrMapScales.w)",
         "lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w)", 2},
        {"easygl",
         "mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x)",
         "mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y)",
         "mix(FragColor.rgb,cnaLinearToSrgb(FragColor.rgb),uSrgb.z)", 2},
        {"llgl",
         "mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), ambientColorPad.w)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), eyePositionWorldPad.w)",
         "mix(rgb, cnaLinearToSrgb(rgb), light0DirPad.w)", 1},
        {"magnum",
         "mix(baseColor.rgb, cnaSrgbToLinear(baseColor.rgb), uSrgb.x)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), uSrgb.y)",
         "mix(fragColor.rgb, cnaLinearToSrgb(fragColor.rgb), uSrgb.z)", 1},
        {"metal",
         "mix(baseColorTex.rgb, cna_srgb_to_linear(baseColorTex.rgb), pu.srgbFlags.x)",
         "mix(emissiveSample, cna_srgb_to_linear(emissiveSample), pu.srgbFlags.y)",
         "mix(c.rgb, cna_linear_to_srgb(c.rgb), pu.srgbFlags.z)", 1},
        {"opengl2",
         "mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),vec3(uSrgb.x))",
         "mix(emissiveSample,cnaSrgbToLinear(emissiveSample),vec3(uSrgb.y))",
         "mix(gl_FragData[0].rgb,cnaLinearToSrgb(gl_FragData[0].rgb),vec3(uSrgb.z))", 1},
        {"opengl4",
         "mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), uSrgb.x)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), uSrgb.y)",
         "mix(rgb, cnaLinearToSrgb(rgb), uSrgb.z)", 1},
        {"sdl-gpu",
         "mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), pbrp.srgbFlags.x)",
         "mix(emissiveSample, cnaSrgbToLinear(emissiveSample), pbrp.srgbFlags.y)",
         "mix(outColor.rgb, cnaLinearToSrgb(outColor.rgb), pbrp.srgbFlags.z)", 1},
        {"vulkan",
         "mix(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), pbr.srgbFlags.x)",
         "mix(emissiveSample, CnaSrgbToLinear(emissiveSample), pbr.srgbFlags.y)",
         "mix(outColor.rgb, CnaLinearToSrgb(outColor.rgb), pbr.srgbFlags.z)", 2},
        {"webgpu",
         "select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5)",
         "select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5)",
         "select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5)", 2},
        {"wicked",
         "lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), cb.pbrSrgb.x)",
         "lerp(emissiveRaw, CnaSrgbToLinear(emissiveRaw), cb.pbrSrgb.y)",
         "lerp(rgb, CnaLinearToSrgb(rgb), cb.pbrSrgb.z)", 1},
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
    // separately stored rigid/skinned fragments (and LLGL's GL/Vulkan sources plus its embedded
    // generated GL copy) from those whose one fragment program is shared by both vertex paths.
    // EasyGL constructs legacy and dual-UV programs from the same two rigid/skinned builders, so
    // its evidence deliberately includes the selected-UV variable rather than pretending the
    // final shader still contains a hard-coded vUV expression.
    constexpr std::array<RendererPbrChannelAudit, 15> kPbrChannelAudits{{
        {"bgfx",
         "texture2D(s_texNormal, rtFlipUV(pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 1), 1), u_rtFlipV.y)).rgb * 2.0 - 1.0",
         "mr.g * u_metallicRoughnessFactor.y",
         "mr.b * u_metallicRoughnessFactor.x",
         "texture2D(s_texOcclusion, pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 4), 4)).r", 1},
        {"diligent",
         "g_NormalMap.Sample(g_NormalMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 1), 1)).rgb * 2.0 - 1.0",
         "mr.g * g_PbrEmissiveRoughness.w",
         "mr.b * g_PbrAmbientMetallic.w",
         "g_OcclusionMap.Sample(g_OcclusionMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 4), 4)).r", 1},
        {"directx9",
         "tex2D(NormalMap, CnaPbrTransformUv(pin.UV, 1)).rgb * 2.0 - 1.0",
         "mr.g * MetallicRoughnessFactor.y",
         "mr.b * MetallicRoughnessFactor.x",
         "tex2D(OcclusionMap, CnaPbrTransformUv(pin.UV, 4)).r", 2},
        {"directx11",
         "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(CNA_PBR_UV(1), 1)).rgb * 2.0 - 1.0",
         "mr.g * EmissiveRoughness.w",
         "mr.b * AmbientMetallic.w",
         "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(CNA_PBR_UV(4), 4)).r", 2},
        {"directx12",
         "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(CNA_PBR_UV(1), 1)).rgb * 2.0 - 1.0",
         "mr.g * EmissiveRoughness.w",
         "mr.b * AmbientMetallic.w",
         "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(CNA_PBR_UV(4), 4)).r", 2},
        {"easygl",
         "texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(\" + normalUv + \",1),uRtFlipV.y)).rgb*2.0-1.0",
         "mr.g*uRoughnessFactor",
         "mr.b*uMetallicFactor",
         "texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(\" + occlusionUv + \",4),uRtFlipVHi.x)).r", 2},
        {"llgl",
         "cnaPbrTransformUV(cnaPbrUv(1), 1)).rgb * 2.0 - 1.0",
         "mr.g * roughnessWeightsPad.x",
         "mr.b * emissiveMetallic.w",
         "cnaPbrTransformUV(cnaPbrUv(4), 4)).r;", 3},
        {"magnum",
         "texture(uNormalMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 1), uRtFlipV.y)).rgb * 2.0 - 1.0",
         "metallicRoughness.g * uRoughnessFactor",
         "metallicRoughness.b * uMetallicFactor",
         "texture(uOcclusionMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 4), uRtFlipVHi.x)).r", 1},
        {"metal",
         "normalMap.sample(normalSmp, cna_pbr_transform_uv(in.uv, 1, pu)).rgb*2.0 - 1.0",
         "mr.g * pu.pbrFactors.y",
         "mr.b * pu.pbrFactors.x",
         "occlusionMap.sample(occlusionSmp, cna_pbr_transform_uv(in.uv, 4, pu)).r", 1},
        {"opengl2",
         "texture2D(uNormalMap,cnaPbrTransformUV(vTex,1)).rgb*2.0-1.0",
         "mr.g*uRoughnessFactor",
         "mr.b*uMetallicFactor",
         "texture2D(uOcclusionMap,cnaPbrTransformUV(vTex,4)).r", 1},
        {"opengl4",
         "texture(uNormalMap, cnaPbrTransformUV(vUV, 1)).rgb * 2.0 - 1.0",
         "mr.g * uRoughnessFactor",
         "mr.b * uMetallicFactor",
         "texture(uOcclusionMap, cnaPbrTransformUV(vUV, 4)).r", 1},
        {"sdl-gpu",
         "texture(uNormalMap, cnaPbrTransformUV(fragUV, 1)).rgb * 2.0 - 1.0",
         "mr.g * pbrp.roughnessFactor",
         "mr.b * pbrp.metallicFactor",
         "texture(uOcclusionMap, cnaPbrTransformUV(fragUV, 4)).r", 1},
        {"vulkan",
         "texture(uNormalMap, CnaPbrTransformUV(CNA_PBR_UV(1), 1)).rgb * 2.0 - 1.0",
         "mr.g * pbr.emissive_roughness.w",
         "mr.b * pbr.eyePos_metallic.w",
         "texture(uOcclusionMap, CnaPbrTransformUV(CNA_PBR_UV(4), 4)).r", 2},
        {"webgpu",
         "textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0",
         "mr.g * pf.metallicRoughness.y",
         "mr.b * pf.metallicRoughness.x",
         "textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r", 2},
        {"wicked",
         "normalMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 1)).rgb * 2.0f - 1.0f",
         "mr.g * cb.pbrFactors.y",
         "mr.b * cb.pbrFactors.x",
         "occlusionMap.Sample(sampler0, CnaPbrTransformUv(input.uv, 4)).r", 1},
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
    // fragments honest; LLGL additionally carries native-GL, Vulkan-style GLSL and its generated
    // native-GL header.
    constexpr std::array<RendererPbrMaterialFactorAudit, 15> kPbrMaterialFactorAudits{{
        {"bgfx",
         "vec3 albedo = baseColor * u_diffuseColor.rgb",
         "float alpha = baseColorTex.a * u_diffuseColor.a",
         "vec3 emissive = u_emissiveColor.xyz * emissiveSample", 1},
        {"diligent",
         "float3 albedo = baseColor * g_DiffuseColor.rgb",
         "float alpha = baseColorTex.a * g_DiffuseColor.a",
         "float3 emissive = g_PbrEmissiveRoughness.xyz * emissiveSample", 1},
        {"directx9",
         "float3 albedo = baseColor * DiffuseColor.rgb",
         "float alpha = baseColorTex.a * DiffuseColor.a",
         "float3 emissive = EmissiveColor.xyz * emissiveSample", 2},
        {"directx11",
         "float3 albedo = baseColor * DiffuseColor.rgb",
         "float alpha = baseColorTex.a * DiffuseColor.a",
         "float3 emissive = EmissiveRoughness.xyz * emissiveSample", 2},
        {"directx12",
         "float3 albedo = baseColor * DiffuseColor.rgb",
         "float alpha = baseColorTex.a * DiffuseColor.a",
         "float3 emissive = EmissiveRoughness.xyz * emissiveSample", 2},
        {"easygl",
         "vec3 albedo=baseRGB*uDiffuseColor.rgb",
         "float alpha=baseColorTex.a*uDiffuseColor.a",
         "vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y)", 2},
        {"llgl",
         "vec3 albedo = baseColor * diffuseColor.rgb",
         "float alpha = baseColorTex.a * diffuseColor.a",
         "vec3 emissive = emissiveMetallic.xyz * emissiveSample", 3},
        {"magnum",
         "vec3 albedo = baseLinear * uDiffuseColor.rgb",
         "float alpha = baseColor.a * uDiffuseColor.a",
         "vec3 emissive = uEmissiveColor * emissiveSample", 1},
        {"metal",
         "float3 albedo = baseColor * pu.diffuseColor.rgb",
         "float alpha = baseColorTex.a * pu.diffuseColor.a",
         "float3 emissive = pu.emissiveColor.xyz * emissiveSample", 1},
        {"opengl2",
         "vec3 albedo=baseColor*uDiffuse.rgb",
         "float alpha=baseColorTex.a*uDiffuse.a",
         "vec3 emissive=uEmissiveColor*emissiveSample", 1},
        {"opengl4",
         "vec3 albedo = baseColor * uDiffuseColor.rgb",
         "float alpha = baseColorTex.a * uDiffuseColor.a",
         "vec3 emissive = uEmissiveColor * emissiveSample", 1},
        {"sdl-gpu",
         "vec3 albedo = baseColor * pc.diffuseColor.rgb",
         "float alpha = baseColorTex.a * pc.diffuseColor.a",
         "vec3 emissive = lp.emissiveColor_pad.xyz * emissiveSample", 1},
        {"vulkan",
         "vec3 albedo = baseColor * pc.diffuseColor.rgb",
         "float alpha = baseColorTex.a * pc.diffuseColor.a",
         "vec3 emissive = pbr.emissive_roughness.xyz * emissiveSample", 2},
        {"webgpu",
         "let albedo = baseColor * u.diffuseColor.rgb",
         "let alpha = baseColorSample.a * u.diffuseColor.a",
         "let emissive = lp.emissiveColor.xyz * emissiveLinear", 2},
        {"wicked",
         "const float3 albedo = baseColor * cb.diffuse.rgb",
         "const float alpha = baseColorTex.a * cb.diffuse.a",
         "const float3 emissive = cb.emissive.rgb * emissiveSample", 1},
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
    constexpr std::array<RendererPbrTransformAudit, 15> kPbrTransformAudits{{
        {"bgfx",
         "const Matrix wvp = world * view * projection",
         "bgfx::setUniform(wvpUniform_, wvp_col)",
         "bgfx::setUniform(world3DUnif_, params.worldColMajor)",
         "gl_Position = mul(u_wvp, vec4(a_position, 1.0))",
         "gl_Position = mul(u_wvp, skinnedPos)"},
        {"diligent",
         "MatrixToFloats(world * view * projection, constants.worldViewProj)",
         "UploadConstants(constants)",
         "MatrixToFloats(world, constants.world)",
         "psIn.Pos = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj)",
         "psIn.Pos = mul(skinnedPos, g_WorldViewProj)"},
        {"directx9",
         "world * view * projection",
         "UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, \"WorldViewProj\", world * view * projection)",
         "UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, \"World\", world)",
         "vout.Position = mul(float4(vin.Position, 1.0), WorldViewProj)",
         "vout.Position = mul(float4(skinnedPos, 1.0), WorldViewProj)"},
        {"directx11",
         "const Matrix wvp = world * view * projection",
         "D3DCommon::D3DPbrPerDrawConstants perDraw{}; wvp.ToColumnMajor(perDraw.Mvp)",
         "world.ToColumnMajor(perDraw.World)",
         "output.Position = mul(float4(input.Position, 1.0), Mvp)",
         "output.Position = mul(skinnedPos, Mvp)"},
        {"directx12",
         "const Matrix wvp = world * view * projection",
         "D3DPbrPerDrawConstants perDraw{}; wvp.ToColumnMajor(perDraw.Mvp)",
         "world.ToColumnMajor(perDraw.World)",
         "output.Position = mul(float4(input.Position, 1.0), Mvp)",
         "output.Position = mul(skinnedPos, Mvp)"},
        {"easygl",
         "const Matrix wvp = world * view * projection",
         "p.prog.set_uniform_matrix4(p.loc_wvp, wvp_col)",
         "p.prog.set_uniform_matrix4(p.loc_world, params.worldColMajor)",
         "gl_Position=uWVP*cnaPos",
         "gl_Position=uWVP*cnaPos"},
        {"llgl",
         "const Matrix combined = world * view * projection",
         "FillPbrUniforms(pbrUniforms, matrix, *params)",
         "std::memcpy(uniforms + 16, params.worldColMajor, sizeof(float) * 16)",
         "gl_Position = mvpMatrix * vec4(position, 1.0)",
         "gl_Position = mvpMatrix * skinnedPos"},
        {"magnum",
         "const Matrix worldViewProjection = world * view * projection",
         "program.SetMatrix4(program.LocationOf(\"uWVP\"), columnMajor)",
         "program.SetMatrix4(program.LocationOf(\"uWorld\"), params.worldColMajor)",
         "gl_Position = uWVP * cnaPosition",
         "gl_Position = uWVP * cnaPosition"},
        {"metal",
         "Mat4 wvp=transpose(multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr)))",
         "fillPbrUniforms(t, pu, wvp, *params)",
         "std::memcpy(t.world, params.worldColMajor, sizeof(t.world))",
         "o.position = t.wvp * float4(in.position, 1.0)",
         "o.position = t.wvp * skinnedPos"},
        {"opengl2",
         "ComputeColumnMajorWVP(world, view, projection, wvp)",
         "glUniformMatrix4fv(glGetUniformLocation(program, \"uWVP\"), 1, GL_FALSE, wvp)",
         "glUniformMatrix4fv(glGetUniformLocation(program, \"uWorld\"), 1, GL_FALSE, worldColMajor)",
         "gl_Position=uWVP*vec4(aPosition,1.0)",
         "gl_Position=uWVP*skinnedPos"},
        {"opengl4",
         "const Matrix wvp = world * view * projection",
         "setM4(\"uWorldViewProj\", wvpCol)",
         "setM4(\"uWorld\", worldCol)",
         "gl_Position = uWorldViewProj * vec4(aPos, 1.0)",
         "gl_Position = uWorldViewProj * skinnedPos"},
        {"sdl-gpu",
         "const Matrix wvp = world * view * projection",
         "FillExtUniforms(command.uniforms, wvp, params)",
         "for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi]",
         "gl_Position = pc.mvp * vec4(inPos, 1.0)",
         "gl_Position = pc.mvp * skinnedPos"},
        {"vulkan",
         "const Matrix wvp = world * view * projection",
         "FillExtPushConst(d.pushConst, wvp, params)",
         "for (int wi = 0; wi < 16; ++wi) out[16 + wi] = p.worldColMajor[wi]",
         "gl_Position = pc.mvp * vec4(aPos, 1.0)",
         "gl_Position = pc.mvp * skinnedPos"},
        {"webgpu",
         "const Matrix wvp = world * view * projection",
         "FillExtUniforms(command.uniforms, wvp, params)",
         "for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi]",
         "output.position = u.mvp * vec4f(input.position, 1.0)",
         "output.position = u.mvp * skinnedPos"},
        {"wicked",
         "instanced ? view * projection : world * view * projection",
         "WriteMatrixColumns(instanced ? view * projection : world * view * projection, constants.mvp)",
         "WriteMatrixColumns(world, constants.world)",
         "o.position = TransformPosition(position)",
         "o.position = TransformPosition(skinnedPosition)"},
    }};

    struct RendererPbrCullAudit
    {
        const char* name;
        std::array<const char*, 5> evidence;
    };

    // GLTF-231/232/379: doubleSided deliberately stays application-owned RasterizerState. This
    // inventory locks the renderer half of that boundary: CullMode::None reaches native no-cull
    // state, and the PBR rigid/skinned route consumes that same dynamic state or pipeline key.
    constexpr std::array<RendererPbrCullAudit, 16> kPbrCullAudits{{
        {"bgfx", {{
            "default: cullFlags_ = 0; break",
            "kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_",
            "SubmitViewProgram(pbr3DProgram_)",
            "SubmitViewProgram(pbrSkinned3DProgram_)",
            nullptr}}},
        {"diligent", {{
            "state_.raster = PackBytes(cullMode, fillMode, 0, 0)",
            "cullMode == 0 ? Dg::CULL_MODE_NONE",
            "PipelineKey key = state_",
            "case 48: variant = ShaderVariant::Pbr3D; break",
            "case 68: variant = ShaderVariant::SkinnedPbr3D; break"}}},
        {"directx9", {{
            "case CullMode::None: return D3DCULL_NONE",
            "SetRenderStateCheckedEXT(D3DRS_CULLMODE, CullModeToD3D9(cullMode)",
            "void DirectX9Renderer::DrawPbrEffectEXT(",
            "const bool skinned = params.skinned",
            "device_->DrawIndexedPrimitive(ToD3D9Topology(primitive)"}}},
        {"directx11", {{
            "case CullMode::None: return D3D11_CULL_NONE",
            "desc.CullMode = D3DCommon::CullModeToD3D11(cullMode)",
            "context_->RSSetState(state.Get())",
            "variant = params.skinned",
            "D3DCommon::D3DShaderVariant::PbrSkinned3dDualUv"}}},
        {"directx12", {{
            "currentCullMode_ = cullMode",
            "case CullMode::None: return D3D11_CULL_NONE",
            "psoDesc.cullMode = currentCullMode_",
            "variant = params.skinned",
            "rs.CullMode = static_cast<D3D12_CULL_MODE>(CullModeToD3D11(desc.cullMode))"}}},
        {"easygl", {{
            "if (cullMode == 0) { device.set_cull_face_enabled(false); }",
            "device.set_cull_face(cullMode == 1 ? ::easygl::CullFace::Back : ::easygl::CullFace::Front)",
            "if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned",
            "if (params.pbr) return StockProgramShape::Pbr",
            "Prog3D& p = SelectProgram(layoutStride, params)"}}},
        // plans/plan_igl.md: IGL bakes the cull mode into its pipeline key, so the caller's
        // RasterizerState reaches the draw through the pipeline cache rather than through a
        // per-draw state call -- and a PBR draw is a feature-flag variant of the same shader, so
        // one key carries both.
        {"igl", {{
            "case 2:  return igl::CullMode::Back;",
            "default: return igl::CullMode::Disabled;",
            "key.cullMode = static_cast<std::uint8_t>(ToIglCullMode(cullMode_));",
            "desc.cullMode = static_cast<igl::CullMode>(key.cullMode);",
            "flags |= EffectFeature::Pbr;"}}},
        {"llgl", {{
            "case XnaCullMode::None: return LLGL::CullMode::Disabled",
            "cullMode_ = cullMode",
            "key = key * 4u + static_cast<std::uint64_t>(cullMode_ & 0x3)",
            "pipelineDesc.rasterizer.cullMode = MapCullMode(cullMode_)",
            "pipelineDesc.debugName = pbrSkinned ? \"CNA.PbrSkinned3D\" : pbr ? \"CNA.Pbr3D\""}}},
        {"magnum", {{
            "Renderer::setFeature(Renderer::Feature::FaceCulling, false)",
            "Renderer::setFaceCullingMode(cullMode == 1 ? Renderer::PolygonFacing::Back",
            "selector.pbr = params.pbr",
            "programOut = MagnumStockProgram::PbrSkinned",
            "programOut = MagnumStockProgram::Pbr"}}},
        {"metal", {{
            "case K::None: default: return MTLCullModeNone",
            "impl_->cull=metalCullMode(c)",
            "[p.encoder setCullMode:p.cull]",
            "case PipelineKind::Pbr48: vs=@\"cna_v3d_pbr\"; fs=@\"cna_f3d_pbr\"; stride=48; break",
            "case PipelineKind::SkinnedPbr68: vs=@\"cna_v3d_skinned_pbr\"; fs=@\"cna_f3d_pbr\"; stride=68; break"}}},
        {"opengl2", {{
            "if (cullMode == 0) { glDisable(GL_CULL_FACE); }",
            "glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT)",
            "const bool pbrSkinned = params && params->pbr && params->skinned && "
            "(vb->stride == 68 || vb->stride == 76 || vb->stride == 80)",
            "const bool pbr = params && params->pbr && !params->skinned && "
            "(vb->stride == 48 || vb->stride == 60)",
            "const GLuint program = pbrSkinned ? pbrSkinnedProgram_ : pbr ? pbrProgram_"}}},
        {"opengl4", {{
            "if (cullMode == 0) { glDisable(GL_CULL_FACE); }",
            "glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT)",
            "if (params.pbr && (strideInBytes == 48 || strideInBytes == 60 || "
            "strideInBytes == 68 || strideInBytes == 76 || strideInBytes == 80))",
            "OpenGL4RawProgram& prog = skinnedPbr ? pbrSkinned3DProgram_ : pbr3DProgram_",
            "if (skinnedPbr) EnsurePbrSkinned3DProgram(); else EnsurePbr3DProgram()"}}},
        {"sdl-gpu", {{
            "default: return SDL_GPU_CULLMODE_NONE",
            "cullMode_ = cullMode",
            "command.renderState = CaptureRenderState()",
            "auto& cache = skinned ? (colored ? pbrSkinnedColorPipelines_ : pbrSkinnedPipelines_)",
            "FillRasterizerState(pipelineInfo.rasterizer_state, renderState, pipelineInfo.primitive_type)"}}},
        {"vulkan", {{
            "cullMode_ = cullMode",
            "d.cullMode = cullMode_",
            "VkPipeline VulkanRenderer::GetOrCreatePipelinePbr3D(",
            "VkPipeline VulkanRenderer::GetOrCreatePipelinePbrSkinned3D(",
            nullptr}}},
        {"webgpu", {{
            "default: return WGPUCullMode_None",
            "cullMode_ = cullMode",
            "command.cullMode = cullMode_",
            "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D(",
            "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D("}}},
        {"wicked", {{
            "default: return wig::CullMode::NONE",
            "state_.cullMode = cullMode",
            "WickedPipelineKey key = state_",
            "entry.rasterizer.cull_mode = ToWickedCull(key.cullMode)",
            "if (key.pbr != 0) { desc.vs = &pbrVertexShaders_[PbrShaderIndex(key)]"}}},
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
    constexpr std::array<RendererPbrSkinningAudit, 15> kPbrSkinningAudits{{
        {"bgfx",
         "bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount))",
         "bgfx::setUniform(weightsPerVertex3DUnif_, weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinMat += u_bones[int(a_indices.y)] * a_weight.y",
         "if (weightsPerVertex >= 4.0) skinMat += u_bones[int(a_indices.z)] * a_weight.z"},
        {"diligent",
         "UploadBoneTransforms(*params)",
         "constants.flags[3] = static_cast<float>(params->weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skin += g_Bones[indices.y] * weights.y",
         "if (weightsPerVertex >= 4.0) skin += g_Bones[indices.z] * weights.z"},
        {"directx9",
         "UploadBonesVS(device_.Get(), vsRegs, vsCount, params)",
         "0.0f, 0.0f, 0.0f, static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinning += Bones[vin.BoneIndices.y] * vin.BoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinning += Bones[vin.BoneIndices.z] * vin.BoneWeights.z"},
        {"directx11",
         "std::memcpy(bones.Bones, params.boneTransforms",
         "lights.EyePosWeights[3] = params.skinned ? static_cast<float>(params.weightsPerVertex) : 0.0f",
         "if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z"},
        {"directx12",
         "std::memcpy(bones.Bones, params.boneTransforms",
         "lights.EyePosWeights[3] = params.skinned ? static_cast<float>(params.weightsPerVertex) : 0.0f",
         "if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z"},
        {"easygl",
         "glUniformMatrix4fv(::metagl::UniformLocation{p.loc_bones}, params.boneCount, 0, params.boneTransforms)",
         "p.prog.set_uniform(p.loc_weightsPerVertex, params.weightsPerVertex)",
         "if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y",
         "if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z"},
        {"llgl",
         "std::memcpy(bones, params.boneTransforms",
         "uniforms[45] = static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z"},
        {"magnum",
         "program.SetMatrix4Array(program.LocationOf(\"uBones\"), params.boneTransforms",
         "program.SetInt(program.LocationOf(\"uWeightsPerVertex\")",
         "if (uWeightsPerVertex >= 2) skin += uBones[aBoneIndices.y] * aBoneWeights.y",
         "if (uWeightsPerVertex >= 4)"},
        {"metal",
         "newBufferWithBytes:params->boneTransforms length:sizeof(float)*72*16",
         "t.skinParams[0]=(float)params.weightsPerVertex",
         "if (weightsPerVertex >= 2) skinMat += bones[in.boneIndices.y] * in.boneWeights.y",
         "if (weightsPerVertex >= 4) skinMat += bones[in.boneIndices.z] * in.boneWeights.z"},
        {"opengl2",
         "glUniformMatrix4fv(glGetUniformLocation(program, \"uBones[0]\"), params->boneCount",
         "glUniform1i(glGetUniformLocation(program, \"uWeightsPerVertex\"), params->weightsPerVertex)",
         "if(uWeightsPerVertex>=2) skinMat+=uBones[i1]*aBoneWeight.y",
         "if(uWeightsPerVertex>=4) skinMat+=uBones[i2]*aBoneWeight.z"},
        {"opengl4",
         "gl4_glUniformMatrix4fv(bonesLoc, params.boneCount, GL_FALSE, params.boneTransforms)",
         "gl4_glUniform1i(weightsLoc, params.weightsPerVertex)",
         "if (uWeightsPerVertex >= 2) skinMat += uBones[aBoneIndices.y] * aBoneWeights.y",
         "if (uWeightsPerVertex >= 4)"},
        {"sdl-gpu",
         "out[i] = p.boneTransforms[i]",
         "command.lightUniforms[39] = static_cast<float>(params.weightsPerVertex)",
         "if (weightsPerVertex >= 2.0) skinMat += bb.bones[inBoneIndices.y] * inBoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += bb.bones[inBoneIndices.z] * inBoneWeights.z"},
        {"vulkan",
         "d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16)",
         "FillPbrUboData(d.pbrUboData, params, static_cast<float>(params.weightsPerVertex))",
         "if (weightsPerVertex >= 2.0) skinMat += bb.bones[aBoneIndices.y] * aBoneWeights.y",
         "if (weightsPerVertex >= 4.0) skinMat += bb.bones[aBoneIndices.z] * aBoneWeights.z"},
        {"webgpu",
         "out[4 + i] = p.boneTransforms[i]",
         "out[0] = static_cast<float>(p.weightsPerVertex)",
         "if (sk.weightsPerVertex.x >= 2.0)",
         "if (sk.weightsPerVertex.x >= 4.0)"},
        {"wicked",
         "std::copy_n(params->boneTransforms, static_cast<std::size_t>(boneCount) * 16",
         "boneConstants.skinParams[0] = static_cast<float>(params->weightsPerVertex)",
         "if (weightsPerVertex >= 2.0f)",
         "if (weightsPerVertex >= 4.0f)"},
    }};

    std::string RendererSlotText(const std::filesystem::path& renderers, const char* name)
    {
        std::string source = RendererText(renderers / name);
        if (std::string(name) == "directx11" || std::string(name) == "directx12")
            source += RendererText(renderers / "common" / "d3d");
        return source;
    }
}

TEST(GltfRendererPbrFallbackPolicy, InventoryCoversEveryRendererThatConsumesPbrMaps)
{
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers)) << "cannot find " << renderers;

    std::set<std::string> observed;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(renderers))
    {
        if (!entry.is_directory()) { continue; }
        const std::filesystem::path src = entry.path() / "src";
        if (!std::filesystem::is_directory(src)) { continue; }
        bool consumesPbrMaps = false;
        for (const std::filesystem::directory_entry& source :
             std::filesystem::recursive_directory_iterator(src))
        {
            if (!source.is_regular_file() || !IsPolicySource(source.path())) { continue; }
            if (ReadFile(source.path()).find("pbrNormalMap") != std::string::npos)
            {
                consumesPbrMaps = true;
                break;
            }
        }
        if (consumesPbrMaps) { observed.insert(entry.path().filename().string()); }
    }

    std::set<std::string> expected;
    for (const RendererAudit& audit : kAudits) { expected.insert(audit.name); }
    EXPECT_EQ(expected, observed)
        << "a PBR renderer was added/removed without updating GLTF-374's fallback contract";
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
        else
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

    // Metal routes all four fields through named slots; this policy is what turns PbrNormal into
    // flat-normal and every other PBR slot into white. Its own exhaustive unit test checks every
    // enum value, while this assertion prevents the source-level audit above from accepting a
    // slot table whose default changed underneath it.
    const std::string metal = RendererText(renderers / "metal");
    EXPECT_NE(std::string::npos, metal.find(Normalize(
        "if (slot == MetalStockTextureSlot::PbrNormal) return MetalNeutralTextureKind::FlatNormal2D")));
    EXPECT_NE(std::string::npos, metal.find(Normalize(
        "return MetalNeutralTextureKind::White2D")));
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
    constexpr std::array<ShaderPair, 3> pairs{{
        {"directx9", "directx9/src/shaders/cna/Pbr3D.hlsl",
         "directx9/src/shaders/cna/PbrSkinned3D.hlsl",
         R"(sampler2D Texture : register(s0);
            sampler2D NormalMap : register(s1);
            sampler2D MetallicRoughnessMap : register(s2);
            sampler2D EmissiveMap : register(s3);
            sampler2D OcclusionMap : register(s4);
            sampler2D SpecularMap : register(s5);
            sampler2D SpecularColorMap : register(s6);)"},
        {"directx11/directx12", "common/d3d/src/shaders/pbr3d.frag.hlsl",
         "common/d3d/src/shaders/pbr_skinned3d.frag.hlsl",
         R"(Texture2D uTexture : register(t0);
            SamplerState uTextureSampler : register(s0);
            Texture2D uNormalMap : register(t1);
            SamplerState uNormalMapSampler : register(s1);
            Texture2D uMetallicRoughnessMap : register(t2);
            SamplerState uMetallicRoughnessSampler : register(s2);
            Texture2D uEmissiveMap : register(t3);
            SamplerState uEmissiveMapSampler : register(s3);
            Texture2D uOcclusionMap : register(t4);
            SamplerState uOcclusionMapSampler : register(s4);
            Texture2D uSpecularMap : register(t5);
            SamplerState uSpecularMapSampler : register(s5);
            Texture2D uSpecularColorMap : register(t6);
            SamplerState uSpecularColorMapSampler : register(s6);)"},
        {"vulkan", "vulkan/src/shaders/pbr3d.frag.glsl",
         "vulkan/src/shaders/pbr3d_skinned.frag.glsl",
         R"(layout(set = 0, binding = 0) uniform sampler2D uTexture;
            layout(set = 0, binding = 1) uniform sampler2D uNormalMap;
            layout(set = 0, binding = 2) uniform sampler2D uMetallicRoughnessMap;
            layout(set = 0, binding = 3) uniform sampler2D uEmissiveMap;
            layout(set = 0, binding = 4) uniform sampler2D uOcclusionMap;)"},
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
    // KHR_materials_specular's unclamped F0, specular factor and two transform rows landed.
    const std::string webgpu = RendererSlotText(renderers, "webgpu");
    const std::string pbrFactorsSize = Normalize(
        "uboEntries[2].buffer.minBindingSize = 76 * sizeof(float)");
    std::size_t pbrFactorsSizeCount = 0;
    for (std::size_t at = webgpu.find(pbrFactorsSize); at != std::string::npos;
         at = webgpu.find(pbrFactorsSize, at + pbrFactorsSize.size()))
        ++pbrFactorsSizeCount;
    EXPECT_EQ(2u, pbrFactorsSizeCount)
        << "both WebGPU PBR pipeline layouts must expose the complete 304-byte factors block";
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

    // LLGL carries both Vulkan-style separate texture/sampler GLSL and a GL combined-sampler
    // variant. The table above checks the former; require every map/slot pairing in the latter too.
    const std::string llgl = RendererSlotText(renderers, "llgl");
    for (const char* evidence : {
             "texture(colorMap, cnaPbrTransformUV(cnaPbrUv(0), 0))",
             "texture(normalMap, cnaPbrTransformUV(cnaPbrUv(1), 1))",
             "texture(metallicRoughnessMap, cnaPbrTransformUV(cnaPbrUv(2), 2))",
             "texture(emissiveMap, cnaPbrTransformUV(cnaPbrUv(3), 3))",
             "texture(occlusionMap, cnaPbrTransformUV(cnaPbrUv(4), 4))"})
    {
        EXPECT_NE(std::string::npos, llgl.find(Normalize(evidence)))
            << "LLGL's combined-sampler shader is missing: " << evidence;
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

// plans/plan_gltf.md GLTF-476. The inventory below this one partitions the PBR renderers by whether they
// sample KHR_materials_specular's two maps, and it labelled `igl` "factor-only" -- which was never
// checked against anything. It was false: `igl` consumed 6 of the 20 PBR draw parameters, and the
// 14 it dropped included four CORE glTF 2.0 material inputs (normalTexture.scale,
// occlusionTexture.strength, the sRGB encoding of base colour and emissive, and KHR_texture_transform
// with its per-slot TEXCOORD selection). It did not refuse those materials; it drew them with the
// shader's own defaults substituted, which is the forbidden third state and the exact thing the
// campaign's partition tests exist to make impossible.
//
// So this test asks the question the label assumed the answer to. Every parameter here is one that
// EVERY PBR renderer must consume -- the specular-texture six are deliberately excluded, because
// `metal` and `wicked` genuinely are factor-only and that is a stated boundary, not a defect.
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
    // multiplies it. Seven renderers legitimately read only the second. Requiring the first by name
    // would fail them for being more complete, so the condition is "one of the two".
    constexpr std::array<const char*, 2> grazingEndpoint{{"pbrDielectricF90", "pbrSpecularFactor"}};
    // The same sixteen the specular partition below covers, so a renderer cannot be visible to one
    // audit and invisible to the other.
    constexpr std::array<const char*, 16> pbrRenderers{{
        "bgfx", "diligent", "directx9", "directx11", "directx12", "easygl", "igl", "llgl",
        "magnum", "metal", "opengl2", "opengl4", "sdl-gpu", "vulkan", "webgpu", "wicked",
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
    // half that carries the value -- naming the three renderers that sample neither map is what
    // makes finishing one of them a deliberate edit here rather than a silent drift.
    //
    // Both directions are asserted. A renderer moved into `sampling` without the bindings fails,
    // and so does one that grows them while still listed as factor-only, which is the direction a
    // half-finished backend would otherwise take unnoticed.
    constexpr std::array<const char*, 14> sampling{{
        "bgfx", "diligent", "directx9", "directx11", "directx12", "easygl", "igl",
        "llgl", "magnum", "opengl2", "opengl4", "sdl-gpu", "vulkan", "webgpu",
    }};
    // Factor-only is not a capability decision -- it is unfinished work. `webgpu` left this set on
    // 2026-08-18 (`GLTF-344`): its PBR uniform block grew KHR_materials_specular's own inputs -- the
    // UNCLAMPED dielectric F0, the specular factor and two affine transform rows per map -- and its
    // two WGSL shaders sample both maps at bindings 6 and 7. `igl` left it on the same day
    // (`GLTF-476`), and the reason is worth keeping: the label above used to say it sampled "exactly
    // the four core PBR maps", which nothing had checked. It was reading 6 of the 20 PBR draw
    // parameters -- it had no specular inputs, but it also had no normal scale, no occlusion
    // strength, no sRGB decode and no texture transforms, so calling it factor-only overstated it in
    // one direction while the count understated the gap in the other. It now samples both maps at
    // units 7 and 8 with per-slot TEXCOORD selection. `metal` cannot be compiled anywhere this
    // repository runs; `wicked` needs WickedEngine shader work. Both genuinely ARE factor-only:
    // each reads 14 of the 20, missing exactly the six specular-texture inputs.
    constexpr std::array<const char*, 2> factorOnly{{"metal", "wicked"}};

    std::set<std::string> expected;
    for (const char* name : sampling) { expected.insert(name); }
    for (const char* name : factorOnly) { expected.insert(name); }
    ASSERT_EQ(16u, expected.size()) << "the two sets must be disjoint";

    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Same discriminator as InventoryCoversEveryRendererThatConsumesPbrMaps, so a new PBR renderer
    // cannot appear to one audit and not the other.
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

TEST(GltfRendererPbrFallbackPolicy, OpenGLRenderersSampleBothKhrMaterialsSpecularTextures)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    const std::string gl2 = RendererSlotText(renderers, "opengl2");
    const std::string gl4 = RendererSlotText(renderers, "opengl4");
    ASSERT_FALSE(gl2.empty());
    ASSERT_FALSE(gl4.empty());

    for (const auto& [source, evidence] : {
             std::pair<const std::string*, const char*>{
                 &gl2,
                 "if (params->pbrSpecularMap) params->pbrSpecularMap->BindGL(); "
                 "else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_)"},
             std::pair<const std::string*, const char*>{
                 &gl2,
                 "if (params->pbrSpecularColorMap) params->pbrSpecularColorMap->BindGL(); "
                 "else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_)"},
             std::pair<const std::string*, const char*>{
                 &gl4,
                 "if (params.pbrSpecularMap) params.pbrSpecularMap->BindGL(); "
                 "else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_)"},
             std::pair<const std::string*, const char*>{
                 &gl4,
                 "if (params.pbrSpecularColorMap) params.pbrSpecularColorMap->BindGL(); "
                 "else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_)"}})
    {
        EXPECT_NE(std::string::npos, source->find(Normalize(evidence)))
            << "missing OpenGL specular identity fallback: " << evidence;
    }

    for (const std::string* source : {&gl2, &gl4})
    {
        for (const char* evidence : {
                 "pbrDielectricF0Unclamped[0]",
                 "pbrSpecularFactor",
                 "pbrSpecularTextureTransformRows",
                 "pbrSpecularColorTextureIsSrgb",
                 "uSpecularMap",
                 "uSpecularColorMap",
                 "uSpecularTextureTransformRows"})
        {
            EXPECT_NE(std::string::npos, source->find(evidence))
                << "missing OpenGL specular state: " << evidence;
        }
        EXPECT_NE(std::string::npos, source->find(Normalize(
            "min(uSpecularFresnelInputs.xyz * specularColorTex, vec3(1.0)) * specularWeight")));
        EXPECT_NE(std::string::npos, source->find(Normalize(
            "mix(vec3(specularWeight), vec3(1.0), metallic)")));
    }
}

TEST(GltfRendererPbrFallbackPolicy, ModernDirectXRenderersSampleBothKhrMaterialsSpecularTextures)
{
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    const std::string dx11 = RendererSlotText(renderers, "directx11");
    const std::string dx12 = RendererSlotText(renderers, "directx12");
    ASSERT_FALSE(dx11.empty());
    ASSERT_FALSE(dx12.empty());

    for (const std::string* source : {&dx11, &dx12})
    {
        for (const char* evidence : {
                 "pbrDielectricF0Unclamped[0]",
                 "pbrSpecularFactor",
                 "pbrSpecularColorTextureIsSrgb",
                 "pbrSpecularTextureTransformRows",
                 "pbrTextureCoordinateSetMask & 0x7fu",
                 "uSpecularMap : register(t5)",
                 "uSpecularColorMap : register(t6)",
                 "CnaPbrSpecularTransformUv(CNA_PBR_UV(5), 0)",
                 "CnaPbrSpecularTransformUv(CNA_PBR_UV(6), 1)"})
        {
            EXPECT_NE(std::string::npos, source->find(Normalize(evidence)))
                << "missing DirectX specular state: " << evidence;
        }
        EXPECT_GE(CountOccurrences(*source, Normalize(
            "min(SpecularFresnelInputs.xyz * specularColorTex, 1.0) * specularWeight")), 2u);
        EXPECT_GE(CountOccurrences(*source, Normalize(
            "lerp(float3(specularWeight, specularWeight, specularWeight), "
            "float3(1.0, 1.0, 1.0), metallic)")), 2u);
    }

    for (const char* evidence : {
             "ID3D11ShaderResourceView* srvs[7]",
             "context_->PSSetShaderResources(0, 7, srvs)",
             "params.pbrSpecularMap ? GetSrvForTextureEXT(params.pbrSpecularMap) : GetOrCreateDefaultWhiteSrvEXT()",
             "params.pbrSpecularColorMap ? GetSrvForTextureEXT(params.pbrSpecularColorMap) : GetOrCreateDefaultWhiteSrvEXT()"})
    {
        EXPECT_NE(std::string::npos, dx11.find(Normalize(evidence)))
            << "missing DirectX 11 specular binding evidence: " << evidence;
    }

    for (const char* evidence : {
             "numSrvs = 7",
             "const ITextureRenderer* srvTextures[7]",
             "params.pbrSpecularMap ? params.pbrSpecularMap : GetOrCreateDefaultWhiteTextureEXT()",
             "params.pbrSpecularColorMap ? params.pbrSpecularColorMap : GetOrCreateDefaultWhiteTextureEXT()",
             "(stride == 60) ? D3DShaderVariant::Pbr3dDualUv",
             "(stride == 76) ? D3DShaderVariant::PbrSkinned3dDualUv",
             "case 60: count = static_cast<UINT>(std::size(kStride60D3D12))",
             "case 76: count = static_cast<UINT>(std::size(kStride76D3D12))"})
    {
        EXPECT_NE(std::string::npos, dx12.find(Normalize(evidence)))
            << "missing DirectX 12 specular/dual-UV binding evidence: " << evidence;
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
             // register, and the byte count is the only part of an opaque blob a source audit can
             // read -- so it is updated deliberately here, alongside the register table below,
             // rather than loosened into "some bytecode exists".
             "kPbr3DPSBytecode[5688]",
             "kPbrSkinned3DPSBytecode[5688]"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing DirectX 9 specular binding evidence: " << evidence;
    }

    for (const char* evidence : {
             "sampler2D SpecularMap : register(s5)",
             "sampler2D SpecularColorMap : register(s6)",
             "tex2D(SpecularMap, CnaPbrSpecularTransformUv(pin.UV, 0)).a",
             "tex2D(SpecularColorMap, CnaPbrSpecularTransformUv(pin.UV, 1)).rgb",
             "lerp(specularColorTex, CnaSrgbToLinear(specularColorTex), SpecularMapFlags.x)",
             "min(SpecularFresnelInputs.xyz * specularColorTex, 1.0) * specularWeight",
             "lerp(float3(specularWeight, specularWeight, specularWeight), "
             "float3(1.0, 1.0, 1.0), metallic)"})
    {
        EXPECT_EQ(2u, CountOccurrences(source, Normalize(evidence)))
            << "both DirectX 9 PBR shaders must contain: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, MagnumSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "magnum");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "constexpr int kPbrSpecularMapSlot = 5",
             "constexpr int kPbrSpecularColorMapSlot = 6",
             "params.pbrSpecularMap, *defaultWhiteTexture_, specularFlip",
             "params.pbrSpecularColorMap, *defaultWhiteTexture_, specularColorFlip",
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularColorTextureIsSrgb",
             "params.pbrSpecularTextureTransformRows[row]",
             "uniform sampler2D uSpecularMap",
             "uniform sampler2D uSpecularColorMap",
             "texture(uSpecularMap, cnaSampleUV(cnaPbrSpecularTransformUV(vTexCoord, 0), uSpecularMapFlags.y)).a",
             "texture(uSpecularColorMap, cnaSampleUV(cnaPbrSpecularTransformUV(vTexCoord, 1), uSpecularMapFlags.z)).rgb",
             "mix(specularColorTex, cnaSrgbToLinear(specularColorTex), uSpecularMapFlags.x)",
             "min(uSpecularFresnelInputs.xyz * specularColorTex, vec3(1.0)) * specularWeight",
             "mix(vec3(specularWeight), vec3(1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing Magnum specular binding evidence: " << evidence;
    }
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
             "fsInfo.num_samplers = 7",
             "layout(set = 2, binding = 5) uniform sampler2D uSpecularMap",
             "layout(set = 2, binding = 6) uniform sampler2D uSpecularColorMap",
             "texture(uSpecularMap, cnaPbrSpecularTransformUV(fragUV, 0)).a",
             "uSpecularColorMap, cnaPbrSpecularTransformUV(fragUV, 1)).rgb",
             "mix(specularColorTex, cnaSrgbToLinear(specularColorTex), pbrp.srgbFlags.w)",
             "pbrp.specularFresnelInputs.xyz * specularColorTex, vec3(1.0)) * specularWeight",
             "mix(vec3(specularWeight), vec3(1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing SDL-GPU specular binding evidence: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, VulkanSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "vulkan");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "float pbrUboData[124]",
             "out[60] = p.pbrDielectricF0Unclamped[0]",
             "out[63] = p.pbrSpecularFactor",
             "p.pbrSpecularColorTextureIsSrgb ? 1.f : 0.f",
             "p.pbrSpecularTextureTransformRows[row][component]",
             "p.pbrTextureCoordinateSetMask & 0x7fu",
             "params.pbrSpecularMap",
             "params.pbrSpecularColorMap",
             "VkImageView views[7] = { baseColor, normalMap, metallicRoughness, emissive, occlusion, specular, specularColor }",
             "slotSamplers_[4], slotSamplers_[5], slotSamplers_[6]",
             "layout(set = 0, binding = 6) uniform sampler2D uSpecularMap",
             "layout(set = 0, binding = 7) uniform sampler2D uSpecularColorMap",
             "layout(set = 0, binding = 7) uniform sampler2D uSpecularMap",
             "layout(set = 0, binding = 8) uniform sampler2D uSpecularColorMap",
             "texture(uSpecularMap, CnaPbrSpecularTransformUV(CNA_PBR_UV(5), 0)).a",
             "uSpecularColorMap, CnaPbrSpecularTransformUV(CNA_PBR_UV(6), 1)).rgb",
             "mix(specularColorTex, CnaSrgbToLinear(specularColorTex), pbr.srgbFlags.w)",
             "pbr.specularFresnelInputs.xyz * specularColorTex, vec3(1.0)) * specularWeight",
             "mix(vec3(specularWeight), vec3(1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing Vulkan specular binding evidence: " << evidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, BgfxSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "bgfx");
    ASSERT_FALSE(source.empty());

    for (const char* evidence : {
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f",
             "params.pbrTextureCoordinateSetMask & 0x7fu",
             "params.pbrSpecularTextureTransformRows, 4",
             "else if (stride == 60)",
             "else if (stride == 76)",
             "layout.add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)",
             "specularMapSampler_ = bgfx::createUniform(\"s_texSpecular\"",
             "specularColorMapSampler_ = bgfx::createUniform(\"s_texSpecularColor\"",
             "BindSamplerSlot(5, specularMapSampler_, params.pbrSpecularMap, defaultWhiteTexture3D_)",
             "BindSamplerSlot(6, specularColorMapSampler_, params.pbrSpecularColorMap, defaultWhiteTexture3D_)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing Bgfx specular/dual-UV state: " << evidence;
    }

    EXPECT_GE(CountOccurrences(source, Normalize("v_texcoord1 = a_texcoord1")), 2u);
    for (const char* shaderEvidence : {
             "SAMPLER2D(s_texSpecular, 5)",
             "SAMPLER2D(s_texSpecularColor, 6)",
             "pbrSpecularTransformUV(pbrUV(v_texcoord0, v_texcoord1, 5), 0)).a",
             "pbrSpecularTransformUV(pbrUV(v_texcoord0, v_texcoord1, 6), 1)).rgb",
             "mix(specularColorTex, cnaSrgbToLinear(specularColorTex), u_srgb.w)",
             "min(u_dielectricFresnel.xyz * specularColorTex, vec3_splat(1.0)) * specularWeight",
             "mix(vec3_splat(specularWeight), vec3_splat(1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(shaderEvidence)))
            << "missing Bgfx specular shader evidence: " << shaderEvidence;
    }
}

TEST(GltfRendererPbrFallbackPolicy, DiligentSamplesBothKhrMaterialsSpecularTextures)
{
    const std::string source = RendererSlotText(
        RepositoryRoot() / "modules" / "renderers", "diligent");
    ASSERT_FALSE(source.empty());

    // CPU transport, public vertex-layout routing and dynamic shader-resource binding all need
    // independent evidence: a correct shader alone cannot prove that the authored maps reach it.
    for (const char* evidence : {
             "pbrDesc.Size = 76 * sizeof(float)",
             "params.pbrDielectricF0Unclamped[0]",
             "params.pbrSpecularFactor",
             "params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f",
             "params.pbrTextureCoordinateSetMask & 0x7fu",
             "std::memcpy(values + 60, params.pbrSpecularTextureTransformRows",
             "case 60: variant = ShaderVariant::PbrDualUv3D",
             "case 76: variant = ShaderVariant::SkinnedPbrDualUv3D",
             "Dg::LayoutElement{4, 0, 2, Dg::VT_FLOAT32, Dg::False, 48, 60}",
             "Dg::LayoutElement{6, 0, 2, Dg::VT_FLOAT32, Dg::False, 68, 76}",
             "usesDualPbrUv ? \"float2 UV1 : TEX_COORD1;\" : \"\"",
             "Dg::ShaderResourceVariableDesc variables[9]",
             "g_SpecularMap", "g_SpecularColorMap",
             "params != nullptr ? params->pbrSpecularMap : nullptr, fallbackTextureView_, 5",
             "params != nullptr ? params->pbrSpecularColorMap : nullptr, fallbackTextureView_, 6"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(evidence)))
            << "missing Diligent specular/dual-UV state: " << evidence;
    }

    for (const char* shaderEvidence : {
             "Texture2D g_SpecularMap",
             "Texture2D g_SpecularColorMap",
             "CnaPbrSpecularTransformUv(CnaPbrUv(psIn, 5), 0)).a",
             "CnaPbrSpecularTransformUv(CnaPbrUv(psIn, 6), 1)).rgb",
             "lerp(specularColorTex, CnaSrgbToLinear(specularColorTex), g_PbrSpecularState.x)",
             "min(g_PbrDielectricFresnel.xyz * specularColorTex, float3(1.0, 1.0, 1.0)) * specularWeight",
             "lerp(float3(specularWeight, specularWeight, specularWeight), float3(1.0, 1.0, 1.0), metallic)"})
    {
        EXPECT_NE(std::string::npos, source.find(Normalize(shaderEvidence)))
            << "missing Diligent specular shader evidence: " << shaderEvidence;
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

    // GLTF-385's production-viewer retake exposed that Vulkan had advertised PBR support while
    // hard-coding stride 48/68 and sampling every map from UV0. Keep the entire repaired chain
    // together: real draw stride -> distinct pipeline/cache variants -> appended UV1 attributes
    // -> transported per-map selector -> all seven shader samples. The shader source occurs twice
    // because rigid and skinned PBR intentionally share the same contract.
    const std::string vulkan = RendererSlotText(renderers, "vulkan");
    for (const char* evidence : {
             "GetOrCreatePipelinePbr3D(draw.stride, draw.topology",
             "GetOrCreatePipelinePbrSkinned3D(draw.stride, draw.topology",
             "stride != 48 && stride != 60",
             "stride != 68 && stride != 76",
             "attrs[4] = { 4, 0, VK_FORMAT_R32G32_SFLOAT, 48 }",
             "attrs[6] = { 6, 0, VK_FORMAT_R32G32_SFLOAT, 68 }",
             "out[120] = static_cast<float>(p.pbrTextureCoordinateSetMask & 0x7fu)"})
    {
        EXPECT_NE(std::string::npos, vulkan.find(Normalize(evidence)))
            << "Vulkan dual-UV PBR path is missing: " << evidence;
    }
    for (std::size_t slot = 0; slot < 7; ++slot)
    {
        const std::string sample = "CNA_PBR_UV(" + std::to_string(slot) + ")";
        EXPECT_EQ(2u, CountOccurrences(vulkan, Normalize(sample)))
            << "Vulkan rigid/skinned shaders do not select the authored UV set for map slot "
            << slot;
    }
    EXPECT_EQ(2u, CountOccurrences(vulkan, Normalize(
        "int mask = int(pbr.textureCoordinateSets.x + 0.5)")))
        << "both Vulkan dual-UV PBR fragment variants must decode the transported selector";

    // GLTF-386 applies the same complete contract to both modern DirectX renderers. Their shared
    // HLSL keeps distinct DXBC variants so each PSO/input layout matches its authored stride.
    const std::string directx11 = RendererSlotText(renderers, "directx11");
    for (const char* evidence : {
             "stride != 48 && stride != 60",
             "stride != 68 && stride != 76",
             "D3DShaderVariant::Pbr3dDualUv",
             "D3DShaderVariant::PbrSkinned3dDualUv",
             R"({ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 })",
             R"({ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 68, D3D11_INPUT_PER_VERTEX_DATA, 0 })",
             "static_cast<float>(params.pbrTextureCoordinateSetMask & 0x7fu)"})
    {
        EXPECT_NE(std::string::npos, directx11.find(Normalize(evidence)))
            << "DirectX11 dual-UV PBR path is missing: " << evidence;
    }
    for (std::size_t slot = 0; slot < 7; ++slot)
    {
        const std::string sample = "CNA_PBR_UV(" + std::to_string(slot) + ")";
        EXPECT_EQ(2u, CountOccurrences(directx11, Normalize(sample)))
            << "DirectX11 rigid/skinned shaders do not select the authored UV set for map slot "
            << slot;
    }
    EXPECT_EQ(2u, CountOccurrences(directx11, Normalize(
        "int mask = int(TextureCoordinateSets.x + 0.5)")))
        << "both DirectX11 dual-UV PBR fragment variants must decode the transported selector";

    const std::string directx12 = RendererSlotText(renderers, "directx12");
    for (const char* evidence : {
             "stride != 48 && stride != 60",
             "stride != 68 && stride != 76",
             "D3DShaderVariant::Pbr3dDualUv",
             "D3DShaderVariant::PbrSkinned3dDualUv",
             R"({ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 })",
             R"({ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 68, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 })",
             "static_cast<float>(params.pbrTextureCoordinateSetMask & 0x7fu)"})
    {
        EXPECT_NE(std::string::npos, directx12.find(Normalize(evidence)))
            << "DirectX12 dual-UV PBR path is missing: " << evidence;
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

    // The two WebGPU programs are inline in one translation unit and share common expression
    // spellings with other stock shaders. Scope both PBR owners explicitly so a non-PBR WGSL
    // occurrence cannot keep this test green after either path drifts.
    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "src" / "WebGPURenderer.cpp"));
    for (const auto& markers : {
             std::pair{"void WebGPURenderer::CreatePbrResources()",
                       "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D"},
             std::pair{"void WebGPURenderer::CreateSkinnedPbrResources()",
                       "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D"}})
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
        "if (renderer_) renderer_->ApplyRasterizerState("
        "(int)value.getCullModeProperty(), (int)value.getFillModeProperty(),"
        "value.getScissorTestEnableProperty(), value.getDepthBiasProperty(),"
        "value.getSlopeScaleDepthBiasProperty()); rasterizerState_ = value;")));

    // Vulkan and WebGPU each build two immutable PBR pipelines. Scope their owners independently:
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

    const std::string vulkan = Normalize(ReadFile(
        renderers / "vulkan" / "src" / "VulkanRenderer.cpp"));
    expectScoped(vulkan,
                 "VkPipeline VulkanRenderer::GetOrCreatePipelinePbr3D(",
                 "void VulkanRenderer::EnsurePbrSkinnedResources()",
                 {"MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode,",
                  "VkCullModeFlags vkCull = VK_CULL_MODE_NONE",
                  "rs.cullMode = vkCull"});
    expectScoped(vulkan,
                 "VkPipeline VulkanRenderer::GetOrCreatePipelinePbrSkinned3D(",
                 "VkPipeline VulkanRenderer::GetOrCreatePipelineInstanced3D(",
                 {"MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode,",
                  "VkCullModeFlags vkCull = VK_CULL_MODE_NONE",
                  "rs.cullMode = vkCull"});

    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "src" / "WebGPURenderer.cpp"));
    expectScoped(webgpu,
                 "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D(",
                 "void WebGPURenderer::QueuePbrDraw(",
                 {"Make3DPipelineKey(topology, stripIndexFormat, depthTest, depthWrite, depthFunc,"
                  "blend, blendParams, cullMode, wireframe,",
                  "pipeline.primitive.cullMode = ToWGPUCullMode(cullMode)"});
    expectScoped(webgpu,
                 "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D(",
                 "void WebGPURenderer::QueueSkinnedPbrDraw(",
                 {"Make3DPipelineKey(topology, stripIndexFormat, depthTest, depthWrite, depthFunc,"
                  "blend, blendParams, cullMode, wireframe,",
                  "pipeline.primitive.cullMode = ToWGPUCullMode(cullMode)"});
}

TEST(GltfRendererPbrFallbackPolicy, EverySkinnedPbrShaderInverseTransposesTheJointMatrix)
{
    struct Audit
    {
        const char* name;
        const char* evidence;
    };
    constexpr std::array<Audit, 15> audits{{
        {"bgfx", "cnaSkinNormal(skinDirectionMat, a_normal)"},
        {"diligent", "CnaSkinNormal(skinNormalMat, vsIn.Normal)"},
        {"directx9", "CnaSkinNormal(skinNormalMat, vin.Normal)"},
        {"directx11", "CnaSkinNormal(skinNormalMat, input.Normal)"},
        {"directx12", "CnaSkinNormal(skinNormalMat, input.Normal)"},
        {"easygl", "cnaSkinNormal(skinDirectionMat,aNormal)"},
        {"llgl", "cnaSkinNormal(skinNormalMat, normal)"},
        {"magnum", "cnaSkinNormal(mat3(skin), aNormal)"},
        {"metal", "normalMat * boneNormal"},
        {"opengl2", "uNormalMatrix*cnaSkinNormal(skinMat3,aNormal)"},
        {"opengl4", "cnaSkinNormal(mat3(skinMat), aNormal)"},
        {"sdl-gpu", "cnaSkinNormal(skinNormalMat, inNormal)"},
        {"vulkan", "cnaSkinNormal(skinNormalMat, aNormal)"},
        {"webgpu", "normalMatrix * pbrSkinNormal(skinMat3, input.normal)"},
        {"wicked", "ApplySkinNormal(normal, blendWeights, blendIndices)"},
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
    const std::string webgpu = Normalize(ReadFile(
        renderers / "webgpu" / "src" / "WebGPURenderer.cpp"));
    const std::string beginMarker = Normalize(
        "void WebGPURenderer::CreateSkinnedPbrResources()");
    const std::string endMarker = Normalize(
        "WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D");
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
    static_assert(kPbrSkinningAudits.size() == 15);
}

TEST(GltfRendererPbrFallbackPolicy, EveryPbrShaderComposesDirectionDeterminantsIntoTangentHandedness)
{
    struct Audit
    {
        const char* name;
        const char* rigid;
        const char* skinned;
    };
    constexpr std::array<Audit, 15> audits{{
        {"bgfx",
         "a_tangent.w * cnaDirectionHandedness(worldDirectionMat)",
         "* cnaDirectionHandedness(skinDirectionMat)"},
        {"diligent",
         "vsIn.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        {"directx9",
         "vin.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        {"directx11",
         "input.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        {"directx12",
         "input.Tangent.w * CnaDirectionHandedness(worldDirectionMat)",
         "* CnaDirectionHandedness(skinNormalMat)"},
        {"easygl",
         "aTangent.w*cnaDirectionHandedness(worldDirectionMat)*instanceHandedness",
         "*cnaDirectionHandedness(skinDirectionMat)"},
        {"llgl",
         "tangent.w * cnaDirectionHandedness(mat3(worldMatrix))",
         "* cnaDirectionHandedness(skinNormalMat)"},
        {"magnum",
         "aTangent.w*cnaDirectionHandedness(cnaWorldDirection)*cnaInstanceSign",
         "*cnaDirectionHandedness(mat3(skin))"},
        {"metal",
         "in.tangent.w * cna_direction_handedness(world3)",
         "* cna_direction_handedness(skinMat3)"},
        {"opengl2",
         "aTangent.w*cnaDirectionHandedness(world3)",
         "*cnaDirectionHandedness(skinMat3)"},
        {"opengl4",
         "aTangent.w * cnaDirectionHandedness(mat3(uWorld))",
         "* cnaDirectionHandedness(mat3(skinMat))"},
        {"sdl-gpu",
         "inTangent.w * cnaDirectionHandedness(mat3(lp.world))",
         "* cnaDirectionHandedness(skinNormalMat)"},
        {"vulkan",
         "aTangent.w * cnaDirectionHandedness(mat3(pbr.world))",
         "* cnaDirectionHandedness(skinNormalMat)"},
        {"webgpu",
         "input.tangent.w * directionHandedness(worldMat3)",
         "* pbrDirectionHandedness(skinMat3)"},
        {"wicked",
         "tangent.w * WorldDirectionHandedness()",
         "* SkinDirectionHandedness(blendWeights, blendIndices)"},
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

TEST(GltfRendererPbrFallbackPolicy, DirectX11SkinnedEffectUsesOpaqueWhiteForMissingTexture)
{
    // GLTF-386: skin-unlit has no base-color texture, while SkinnedEffect deliberately keeps
    // TextureEnabled=true. An unbound Direct3D 11 SRV samples transparent black and therefore
    // erases this otherwise valid glTF draw. Keep the SkinnedEffect-specific null branch tied to
    // the same opaque-white fallback used by the other full renderers.
    const std::string directx11 = RendererText(
        RepositoryRoot() / "modules" / "renderers" / "directx11");
    EXPECT_EQ(2u, CountOccurrences(directx11, Normalize(R"(
        srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                  : GetOrCreateDefaultWhiteSrvEXT();
    )"))) << "both the PBR and plain-skinned bindings require opaque-white texture0 fallbacks";
}

// --- plans/plan_gltf.md GLTF-462/GLTF-465: the stride-60 record, per renderer ---------------------------

TEST(GltfRendererPbrFallbackPolicy, EveryPbrRendererEitherBindsTheStride60RecordOrIsNamedAsNotYet)
{
    // Stride 60 is the rigid PBR record: Position, Normal, Tangent, TEXCOORD_0, TEXCOORD_1 and --
    // since GLTF-462 -- a packed COLOR_0 in the four bytes GLTF-182 had reserved purely to keep the
    // stride distinct from 56. It has existed since GLTF-182, and this audit is what found that most
    // PBR renderers never learned it: OPENGL2 fell through to a `stride >= 32` catch-all that reads
    // TEXCOORD at offset 24 -- inside the tangent -- so a dual-UV PBR mesh textured itself from
    // tangent bytes in silence, and OPENGL4/MAGNUM/LLGL/DIRECTX9 degraded to position-only, no
    // attributes at all, or an outright refusal.
    //
    // GLTF-462 made that reachable for ordinary content (every rigid vertex-coloured
    // metallic-roughness primitive lands here now), so the disposition is a partition rather than a
    // hope: a renderer either binds the record or is named as not yet doing so.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Binds the record through its own stride table.
    constexpr std::array<const char*, 14> strideTable{{
        "bgfx", "diligent", "directx9", "directx11", "directx12", "easygl",
        "llgl", "magnum", "opengl2", "opengl4", "software", "vulkan", "sdl-gpu", "webgpu",
    }};
    // Builds its vertex input from the public VertexDeclaration instead of from a stride table, so
    // the canonical layout reaches it -- colour element included -- with no per-stride row at all.
    // This is the abstraction the others' stride tables are a restatement of.
    constexpr std::array<const char*, 1> declarationDriven{{"igl"}};
    // No stride-60 row and no declaration path: the record degrades visibly (no attributes, or a
    // refusal) rather than being mis-bound. GLTF-465 owns closing these, and each needs pipeline or
    // shader-descriptor work rather than a table entry.
    constexpr std::array<const char*, 2> notYet{{"metal", "wicked"}};

    std::set<std::string> classified;
    for (const char* name : strideTable) { classified.insert(name); }
    for (const char* name : declarationDriven) { classified.insert(name); }
    for (const char* name : notYet) { classified.insert(name); }
    ASSERT_EQ(17u, classified.size()) << "the three dispositions must be disjoint";

    // Discovered exactly as the PBR-map inventory discovers its own set, plus SOFTWARE, which
    // rasterises the record on the CPU without binding `pbrNormalMap` at all.
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
    // plans/plan_gltf.md GLTF-463: stride 80 is the whole stride-76 skinned PBR record with a packed
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
    // Binds the record. EasyGL serves five GL profiles; SOFTWARE rasterises it on the CPU; the two
    // D3D families share one input-element table and one HLSL pair, which is why one row each of
    // shared code covers both.
    constexpr std::array<Stride80Audit, 14> binds{{
        {"webgpu", "attributes[6].offset = 76;"},
        {"magnum", "MakeAttribute(6, 76, 4, true,  4)"},
        // GLTF-465: D3DDECLTYPE_D3DCOLOR is D3D9's own normalized four-byte colour element, read
        // into a float4 COLOR register -- exactly what the importer packs at offset 76.
        {"directx9", "{0, 76, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0},"},
        {"diligent", "Dg::LayoutElement{7, 0, 4, Dg::VT_UINT8, Dg::True, 76, 80},"},
        {"bgfx", "layout.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true);"},
        {"llgl", "addAttribute(\"color\", LLGL::Format::RGBA8UNorm, 1, 76);"},
        {"sdl-gpu", "attrs[slot].offset = skinned ? 76 : 56;"},
        {"easygl", "case 80:"},
        {"software", "if (stride == 80) UnpackColorBytes(raw.At(76), out.r, out.g, out.b, out.a);"},
        {"opengl2", "colorOffset = (stride == 60) ? 56u : 76u;"},
        {"opengl4", "case 80:"},
        {"vulkan", "attrs[7] = { 7, 0, VK_FORMAT_R8G8B8A8_UNORM, 76 }; // aColor"},
        {"directx11", "case 80: count = static_cast<UINT>(std::size(kStride80)); return kStride80;"},
        {"directx12", "case 80: count = static_cast<UINT>(std::size(kStride80D3D12)); return kStride80D3D12;"},
    }};
    // Declaration-driven: IGL builds its vertex input and generates its shader from the public
    // VertexDeclaration, so stride 80 needs no row and no shader variant of its own.
    constexpr std::array<const char*, 1> declarationDriven{{"igl"}};
    // Never sees stride 80: its skinned PBR path accepts only the strides it has layouts for, so an
    // 80-byte record refuses rather than being mis-read. GLTF-465 records what each would need.
    constexpr std::array<const char*, 2> refuses{{
        "metal", "wicked",
    }};

    std::set<std::string> classified;
    for (const Stride80Audit& audit : binds) { classified.insert(audit.name); }
    for (const char* name : declarationDriven) { classified.insert(name); }
    for (const char* name : refuses) { classified.insert(name); }
    ASSERT_EQ(17u, classified.size()) << "the three dispositions must be disjoint";
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
    // EasyGL serves five GL profiles (OPENGLES2/3, OPENGL33, WEBGL1/2), so this is more than one
    // renderer identity; SOFTWARE is the CPU rasteriser, where the same product is evaluated per
    // fragment on the host.
    // IGL is here for the same architectural reason it needs no stride row: its shader library is
    // GENERATED per feature set, and it declares `aColor` exactly when the vertex declaration carries
    // a Color -- which stride 60 now does. Its vertex stage multiplies the attribute into the colour
    // that becomes `vColor`, and its fragment stage feeds that straight to the PBR BRDF, so the
    // product is baseColorFactor x COLOR_0 x baseColorTexture with no per-renderer work at all. That
    // is the abstraction paying for itself, and it is why this row was checked rather than assumed:
    // the first draft of this audit listed `igl` as not-yet on the strength of it having no stride
    // table, which is exactly backwards.
    // DIRECTX11 and DIRECTX12 share one HLSL pair and one constant-buffer struct, so their single
    // shared multiply serves both identities -- the same "improve what is shared" shape as EasyGL's.
    constexpr std::array<VertexColourPbrAudit, 15> implemented{{
        // WebGPU expands one marked WGSL source into a bare and a colour-carrying module, because
        // WGSL rejects a vertex input with no matching attribute (GLTF-465).
        {"webgpu", "let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"easygl", "vec3 albedo=baseRGB*uDiffuseColor.rgb*cnaVertexColor.rgb;"},
        // DirectX 9 compiles its HLSL offline into vs_3_0/ps_3_0 bytecode, so the .hlsl IS the
        // source; the two colour-carrying vertex programs are separate entry points because a
        // vs_3_0 input with no stream behind it reads undefined (GLTF-465).
        {"directx9", "albedo *= pin.Color.rgb;"},
        // Magnum generates its PBR GLSL at runtime, so its evidence is the generated source itself.
        {"magnum", "vec3 albedo = baseLinear * uDiffuseColor.rgb * cnaVertexColor.rgb;"},
        // Diligent expands a per-variant HLSL template, so its product is the substituted string.
        {"diligent", "albedo *= psIn.Color.rgb;"},
        // bgfx compiles .sc sources offline into the four backend bytecodes; the .sc IS the source.
        {"bgfx", "vec3 albedo = baseColor * u_diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"llgl", "vec3 albedo = baseColor * diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"sdl-gpu", "vec3 albedo = baseColor * pc.diffuseColor.rgb * cnaVertexColor.rgb;"},
        {"igl", "if (cnaHas(CNA_VERTEX_COLOR_ENABLED)) color *= aColor;"},
        {"software", "if (stride == 60) UnpackColorBytes(raw.At(56), out.r, out.g, out.b, out.a);"},
        {"opengl2", "albedo = baseColor * uDiffuse.rgb * cnaVertexColor.rgb;"},
        {"opengl4", "vec3 albedo = baseColor * uDiffuseColor.rgb * cnaVertexColor.rgb;"},
        {"vulkan", "albedo *= vColor.rgb;"},
        {"directx11", "albedo *= input.Color.rgb;"},
        {"directx12", "albedo *= input.Color.rgb;"},
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
    constexpr std::array<VertexColourPbrAudit, 14> alphaProduct{{
        {"webgpu", "let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;"},
        {"easygl", "alpha=baseColorTex.a*uDiffuseColor.a*cnaVertexColor.a;"},
        {"directx9", "alpha  *= pin.Color.a;"},
        {"magnum", "float alpha = baseColor.a * uDiffuseColor.a * cnaVertexColor.a;"},
        {"diligent", "alpha  *= psIn.Color.a;"},
        {"bgfx", "float alpha = baseColorTex.a * u_diffuseColor.a * cnaVertexColor.a;"},
        {"llgl", "float alpha = baseColorTex.a * diffuseColor.a * cnaVertexColor.a;"},
        {"sdl-gpu", "float alpha = baseColorTex.a * pc.diffuseColor.a * cnaVertexColor.a;"},
        // SOFTWARE has no separate PBR fragment program: the interpolated vertex colour IS the
        // start of the product, alpha included, and the base colour factor multiplies into it.
        {"software", "float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;"},
        {"opengl2", "alpha = baseColorTex.a * uDiffuse.a * cnaVertexColor.a;"},
        {"opengl4", "float alpha = baseColorTex.a * uDiffuseColor.a * cnaVertexColor.a;"},
        {"vulkan", "alpha *= vColor.a;"},
        {"directx11", "alpha *= input.Color.a;"},
        {"directx12", "alpha *= input.Color.a;"},
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
    constexpr std::array<VertexColourPbrAudit, 15> gate{{
        {"webgpu", "select(vec4f(1.0), input.color, u.light0DiffuseVertexColor.w > 0.5)"},
        {"easygl", "vec4 cnaVertexColor=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);"},
        {"directx9", "if (VertexColorFlags.x > 0.5)"},
        {"magnum", "vec4 cnaVertexColor = (uVertexColorEnabled > 0.5) ? vColor : vec4(1.0);"},
        {"diligent", "if (g_Flags.y > 0.5)"},
        {"bgfx", "vec4 cnaVertexColor = u_vertexColorEnabled3D.x > 0.5 ? v_vertexColor0 : vec4(1.0, 1.0, 1.0, 1.0);"},
        {"llgl", "vec4 cnaVertexColor = (specularState.z > 0.5) ? vColor : vec4(1.0);"},
        {"sdl-gpu", "vec4 cnaVertexColor = (pc.vertexColorEnabled > 0.5) ? fragColor0 : vec4(1.0);"},
        {"igl", "if (cnaHas(CNA_VERTEX_COLOR_ENABLED)) color *= aColor;"},
        {"software", "params.vertexColorEnabled"},
        {"opengl2", "vec4 cnaVertexColor=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);"},
        {"opengl4", "vec4 cnaVertexColor = uVertexColorEnabled > 0.5 ? vColor : vec4(1.0);"},
        {"vulkan", "if (pc.vertexColorEnabled > 0.5)"},
        {"directx11", "if (VertexColorFlags.x > 0.5)"},
        {"directx12", "if (VertexColorFlags.x > 0.5)"},
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
    constexpr std::array<VertexColourPbrAudit, 11> upload{{
        {"easygl", "p.loc_vertexcolor"},
        // Magnum asks the LAYOUT as well as the effect: one program serves strides 48 and 60, and
        // only the latter supplies the attribute, so raising the flag on stride 48 would multiply
        // base colour by GL's generic default (0,0,0,1) -- black, not merely uncoloured.
        {"magnum", "const bool colourAttributeSupplied = !params.pbr || strideInBytes == 60 || strideInBytes == 80;"},
        {"diligent", "constants.flags[1] = params->vertexColorEnabled ? 1.0f : 0.0f;"},
        {"bgfx", "const float vcePbr[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };"},
        {"llgl", "uniforms[134] = params.vertexColorEnabled ? 1.0f : 0.0f;"},
        // SDL_GPU's PC block already carried the flag -- its own comment called it "unused".
        {"sdl-gpu", "float vertexColorEnabled; // plans/plan_gltf.md GLTF-465: gates the COLOR_0 product below"},
        {"opengl2", "if (lit || skinned || pbr || pbrSkinned)"},
        {"opengl4", "gl4_glUniform1f(vertexColorLoc, params.vertexColorEnabled ? 1.0f : 0.0f);"},
        {"vulkan", "pc[31] = p.vertexColorEnabled ? 1.f : 0.f;"},
        {"directx11", "perDraw.VertexColorFlags[0] = params.vertexColorEnabled ? 1.0f : 0.0f;"},
        {"directx12", "perDraw.VertexColorFlags[0] = params.vertexColorEnabled ? 1.0f : 0.0f;"},
    }};
    for (const VertexColourPbrAudit& audit : upload)
    {
        SCOPED_TRACE(audit.name);
        const std::string source = RendererSlotText(renderers, audit.name);
        ASSERT_FALSE(source.empty());
        EXPECT_NE(std::string::npos, source.find(Normalize(audit.evidence)))
            << "the effect's vertex-colour switch never reaches this renderer's PBR draw";
    }

    // IGL's product has to reach the BRDF, not merely exist: the colour is multiplied in the VERTEX
    // stage, so what proves it is PBR-relevant is that the same value is what `cnaShadePbr` receives.
    const std::string igl = RendererSlotText(renderers, "igl");
    EXPECT_NE(std::string::npos, igl.find(Normalize("vec4 color = vColor;")))
        << "the fragment stage does not start from the interpolated vertex colour";
    EXPECT_NE(std::string::npos, igl.find(Normalize("cnaShadePbr(color.rgb, normal, eyeVector,")))
        << "the colour product never reaches the PBR BRDF";

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
    constexpr std::array<OpenVertexColourRenderer, 2> notYet{{
        {"metal", "no stride-60/80 layout at all; Metal cannot be built or run on this host"},
        {"wicked", "no stride-60/80 layout at all; needs WickedEngine shader work"},
    }};
    std::set<std::string> classified;
    for (const VertexColourPbrAudit& audit : implemented) { classified.insert(audit.name); }
    for (const OpenVertexColourRenderer& open : notYet) { classified.insert(open.name); }
    ASSERT_EQ(17u, classified.size()) << "the two dispositions must be disjoint";
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
    // plans/plan_gltf.md GLTF-465, and the reason this test exists rather than another prose row: the
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
    // precondition for the unqualified milestone name, so it is machine-checked over all seventeen
    // rather than tracked in a table somebody has to remember to update.
    //
    // "Seventeen PBR renderers" is sixteen full ones plus `software`, and the difference is worth
    // stating where the count is asserted rather than only in docs/software-renderer.md. SOFTWARE is
    // a CPU rasteriser with no metallic-roughness BRDF at all: it consumes the base-colour map's UV
    // selection and transform and nothing else of the twenty PBR draw parameters, and it evaluates
    // no lights, so a PbrEffect draw comes out as vertexColor * diffuseColor * texture0. That is a
    // deliberate, documented reduction that predates this campaign -- the plan calls it a "reduced
    // CPU cross-check" and reserves "full PBR renderer" for the other sixteen -- and it is why the
    // sixteen-renderer sets in the tests around this one exclude it. COLOR_0 specifically it does
    // evaluate, which is what this partition asks. Do not read the count as sixteen-plus-one
    // equivalent implementations (plans/plan_gltf.md GLTF-476).
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers));

    // Evaluates the product -- the same set VertexColourReachesTheBaseColourProduct... verifies in
    // detail (RGB, alpha, the enable gate and the uniform upload, per renderer).
    constexpr std::array<const char*, 15> applies{{
        "easygl", "igl", "software", "opengl2", "opengl4", "vulkan", "directx11", "directx12",
        "magnum", "diligent", "bgfx", "llgl", "sdl-gpu", "directx9", "webgpu",
    }};
    // Refuses the draw through the shared guard. Two shapes of renderer are here for two different
    // reasons, and both end at the same behaviour: none of the five has an implemented product, and
    // each already failed such a draw somewhere downstream --
    // directx9/metal/sdl-gpu/webgpu/wicked already failed such a draw somewhere downstream, but as a
    // stride/layout mismatch that never mentioned the missing semantic, so for them it is the same
    // refusal given for the right reason and at the same place as everyone else's.
    constexpr std::array<const char*, 2> refuses{{
        "metal", "wicked",
    }};

    std::set<std::string> classified;
    for (const char* name : applies) { classified.insert(name); }
    for (const char* name : refuses) { classified.insert(name); }
    ASSERT_EQ(17u, classified.size()) << "the two dispositions must be disjoint";
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
    // plans/plan_gltf.md GLTF-465, and the hole the two tests above cannot see. They ask whether a
    // renderer DECLARES the colour -- a layout row at offset 56/76, a shader that multiplies
    // `cnaVertexColor` into albedo. Neither asks whether a stride-60 or stride-80 draw ever REACHES
    // that shader, and in a renderer whose PBR route is chosen from an explicit stride list those
    // are different questions: the layout can be complete while the route that selects it still
    // enumerates only the two uncoloured strides, so the whole implementation is unreachable.
    //
    // That is not hypothetical. It has now happened three times in this renderer set:
    //
    //   - OPENGL2 selected its PBR program for stride 48 only, so a stride-60 draw fell through to
    //     the Blinn-Phong `lit` branch (fixed with GLTF-465's own OpenGL2 row, see the comment at
    //     the predicate below);
    //   - SDL_GPU shipped the stride-60/80 pipelines, shaders and attributes and left
    //     `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` gating on `stride == 48`/`68`, so every such
    //     draw fell past every branch into the stride-16 coloured path and was refused there;
    //   - DILIGENT chose `SkinnedPbrColor3D` for stride 80 in one switch and then threw
    //     "needs a skinned PBR vertex layout (stride 68 or 76)" nine lines later.
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
    const std::array<RouteGate, 19> gates{{
        {"webgpu", "the rigid PBR stride check", "if (pbrStride != 48 && pbrStride != 60)"},
        {"webgpu", "the skinned PBR stride check",
         "if (skinnedPbrStride != 68 && skinnedPbrStride != 80)"},
        {"directx9", "the rigid PBR stride check",
         "if (!skinned && stride != 48 && stride != 60)"},
        {"directx9", "the skinned PBR stride check",
         "if (skinned && stride != 68 && stride != 80)"},
        {"sdl-gpu", "the draw-entry dispatch (both routes)",
         "if (needsPbr &&"
         "    ((params.skinned && (stride == 68 || stride == 80)) ||"
         "     (!params.skinned && (stride == 48 || stride == 60))))"},
        {"sdl-gpu", "QueuePbrDraw's own acceptance",
         "const bool acceptable = skinned ? (stride == 68u || stride == 80u)"
         "                                : (stride == 48u || stride == 60u);"},
        {"diligent", "the rigid PBR stride check",
         "if (params != nullptr && params->pbr && !params->skinned && stride != 48 && stride != 60)"},
        {"diligent", "the skinned PBR stride check",
         "if (params != nullptr && params->pbr && params->skinned && stride != 68 &&"
         "    stride != 76 && stride != 80)"},
        {"vulkan", "the rigid PBR pipeline's stride check", "if (stride != 48 && stride != 60)"},
        {"vulkan", "the skinned PBR pipeline's stride check",
         "if (stride != 68 && stride != 76 && stride != 80)"},
        {"directx11", "the rigid PBR stride check",
         "if (needsPbr && !params.skinned && stride != 48 && stride != 60)"},
        {"directx11", "the skinned PBR stride check",
         "if (needsPbr && params.skinned && stride != 68 && stride != 76 && stride != 80)"},
        {"directx12", "the rigid PBR stride check",
         "if (needsPbr && !params.skinned && stride != 48 && stride != 60)"},
        {"directx12", "the skinned PBR stride check",
         "if (needsPbr && params.skinned && stride != 68 && stride != 76 && stride != 80)"},
        {"magnum", "SelectStockProgram's rigid PBR arm",
         "if (selector.strideInBytes != 48 && selector.strideInBytes != 60)"},
        {"magnum", "SelectStockProgram's skinned PBR arm",
         "if (selector.strideInBytes != 68 && selector.strideInBytes != 80)"},
        {"opengl2", "the rigid PBR route selector",
         "(vb->stride == 48 || vb->stride == 60)"},
        {"opengl2", "the skinned PBR route selector",
         "(vb->stride == 68 || vb->stride == 76 || vb->stride == 80)"},
        {"opengl4", "the PBR route selector",
         "if (params.pbr && (strideInBytes == 48 || strideInBytes == 60 ||"
         "                   strideInBytes == 68 || strideInBytes == 76 || strideInBytes == 80))"},
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
    // selected from `params.pbr` (and the layout separately from the stride), or -- IGL -- from the
    // public VertexDeclaration, so there is no second list that can fall out of step with the first.
    // Stated as evidence rather than as an absence, so that a renderer which GROWS a stride gate
    // stops matching and has to be classified above.
    struct UngatedRoute
    {
        const char* renderer;
        const char* evidence;
    };
    const std::array<UngatedRoute, 4> ungated{{
        {"easygl", "if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned;"},
        {"bgfx", "else if (params.pbr && params.skinned && bgfx::isValid(pbrSkinned3DProgram_))"},
        {"igl", "if (params.pbr)"},
        {"software", "if (stride == 48 || stride == 60 || stride == 68 || stride == 76 || stride == 80)"},
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

    // LLGL selects its PBR shader variant from the vertex attributes the caller declared rather
    // than from a stride, which is why it has no row in either table -- but it must still HAVE the
    // colour-carrying variants, or "attribute-driven" would just mean the colour is dropped.
    const std::string llgl = RendererSlotText(renderers, "llgl");
    ASSERT_FALSE(llgl.empty());
    EXPECT_NE(std::string::npos, llgl.find(Normalize("hasVertexColour ? Shaders::kPbr3dSkinnedDualUvColorVertGlsl")))
        << "LLGL's skinned PBR variant no longer branches on the declared colour attribute";

    // And the two dispositions together are still the whole implementing set, so a renderer cannot
    // be added to `applies` above and quietly skip this test.
    std::set<std::string> covered;
    for (const RouteGate& gate : gates) { covered.insert(gate.renderer); }
    for (const UngatedRoute& route : ungated) { covered.insert(route.renderer); }
    covered.insert("llgl");
    const std::set<std::string> applies{
        "easygl", "igl", "software", "opengl2", "opengl4", "vulkan", "directx11", "directx12",
        "magnum", "diligent", "bgfx", "llgl", "sdl-gpu", "directx9", "webgpu"};
    EXPECT_EQ(applies, covered)
        << "a renderer listed as applying COLOR_0 has no route-reachability disposition";
}

TEST(GltfRendererIndexWidthPolicy, InventoryClassifiesEveryRenderer)
{
    // A provider has a local CreateIndexBuffer32 implementation. The two explicit rejecters also
    // implement the method locally because their renderer-specific diagnostic is useful. The
    // remaining 2D/no-3D backends deliberately inherit the shared, unconditionally throwing
    // default. Keeping the three sets disjoint makes a new renderer an audit failure, not an
    // accidental 16-bit fallback.
    constexpr std::array<const char*, 33> providers{{
        "bgfx", "diligent", "directx10", "directx11", "directx12", "directx2",
        "directx3", "directx5", "directx6", "directx7", "directx8", "directx9",
        "easygl", "fna3d", "glide", "headless", "igl", "llgl", "magnum", "metal",
        "opengl1", "opengl2", "opengl4", "opengles1", "portablegl", "sdl-gpu",
        "software", "sokol", "stub", "tinygl", "vulkan", "webgpu", "wicked",
    }};
    constexpr std::array<const char*, 2> explicitRejecters{{"gdi", "skia"}};
    // PIXIJS overrides CreateIndexBuffer16 locally to name itself in the refusal but does NOT
    // override CreateIndexBuffer32, so its 32-bit path is the shared throwing default -- which is
    // what puts it here rather than among the explicit rejecters (plans/plan_pixijs.md).
    constexpr std::array<const char*, 11> inheritedRejecters{{
        "blend2d", "canvas", "direct2d", "directx1", "freedirect", "html-dom",
        "nanovg", "openvg", "pixijs", "sdl-renderer", "svg-dom",
    }};

    std::set<std::string> expected;
    for (const char* name : providers) { expected.insert(name); }
    for (const char* name : explicitRejecters) { expected.insert(name); }
    for (const char* name : inheritedRejecters) { expected.insert(name); }
    ASSERT_EQ(46u, expected.size()) << "the policy sets must be disjoint";

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
    constexpr std::array<const char*, 33> providers{{
        "bgfx", "diligent", "directx10", "directx11", "directx12", "directx2",
        "directx3", "directx5", "directx6", "directx7", "directx8", "directx9",
        "easygl", "fna3d", "glide", "headless", "igl", "llgl", "magnum", "metal",
        "opengl1", "opengl2", "opengl4", "opengles1", "portablegl", "sdl-gpu",
        "software", "sokol", "stub", "tinygl", "vulkan", "webgpu", "wicked",
    }};
    // PIXIJS overrides CreateIndexBuffer16 locally to name itself in the refusal but does NOT
    // override CreateIndexBuffer32, so its 32-bit path is the shared throwing default -- which is
    // what puts it here rather than among the explicit rejecters (plans/plan_pixijs.md).
    constexpr std::array<const char*, 11> inheritedRejecters{{
        "blend2d", "canvas", "direct2d", "directx1", "freedirect", "html-dom",
        "nanovg", "openvg", "pixijs", "sdl-renderer", "svg-dom",
    }};

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

    const std::string gles1 = RendererText(renderers / "opengles1");
    EXPECT_NE(std::string::npos, gles1.find(Normalize(
        "if (!elementIndexUintSupported_) return IGraphicsRenderer::CreateIndexBuffer32(index_capacity);")))
        << "OpenGL ES 1 must reject uint32 indices when GL_OES_element_index_uint is absent";
    EXPECT_NE(std::string::npos, RendererText(renderers / "gdi").find(Normalize(
        "ThrowUnsupportedFeature(\"32-bit index buffers\")")));
    EXPECT_NE(std::string::npos, RendererText(renderers / "skia").find(Normalize(
        "ThrowSkiaUnsupported3D(\"CreateIndexBuffer32\")")));
}

TEST(GltfRendererPointTopologyPolicy, Direct3DBackendsMapPointsOrRejectBeforeSubmission)
{
    // GLTF-394 closes the last known silent POINTS reinterpretations. D3D9 duplicates its native
    // mapper in five independently compiled draw implementations, so checking only the ordinary
    // path would leave PBR (the glTF path), stock, skinned-colour or instanced draws behind.
    const std::filesystem::path renderers =
        RepositoryRoot() / "modules" / "renderers";
    const std::string d3d9 = RendererText(renderers / "directx9");
    EXPECT_EQ(5u, CountOccurrences(
                      d3d9,
                      "casePrimitiveType::PointListEXT:returnD3DPT_POINTLIST;"));

    const std::string d3d10 = RendererText(renderers / "directx10");
    EXPECT_NE(std::string::npos, d3d10.find(
        "casePrimitiveType::PointListEXT:returnprimitiveCount;"));
    EXPECT_NE(std::string::npos, d3d10.find(
        "casePrimitiveType::PointListEXT:returnD3D10_PRIMITIVE_TOPOLOGY_POINTLIST;"));

    const std::string d3d11 = RendererText(renderers / "directx11");
    EXPECT_NE(std::string::npos, d3d11.find(
        "casePrimitiveType::PointListEXT:returnprimitiveCount;"));
    EXPECT_NE(std::string::npos, d3d11.find(
        "casePrimitiveType::PointListEXT:returnD3D11_PRIMITIVE_TOPOLOGY_POINTLIST;"));

    const std::string pointSuite = Normalize(ReadFile(
        RepositoryRoot() / "modules" / "graphics" / "tests" / "Microsoft" / "Xna" /
        "Framework" / "Graphics" / "PointListPrimitiveTests.cpp"));
    for (const std::string_view renderer : {"DIRECTX9", "DIRECTX10", "DIRECTX11"})
    {
        EXPECT_NE(std::string::npos, pointSuite.find(
            "defined(CNA_RENDERER_" + std::string(renderer) + ")"))
            << renderer << " must remain in the shared point framebuffer suite";
    }

    // D3D12's current PSO cache fixes PrimitiveTopologyType to TRIANGLE. Mapping IA topology to
    // POINTLIST/LINELIST/LINESTRIP would therefore trade an approximation for a validation error.
    // Its honest contract is a named refusal, reached by all four ordinary/instanced native paths.
    const std::string d3d12 = RendererText(renderers / "directx12");
    for (const std::string_view topology : {"LineList", "LineStrip", "PointListEXT"})
    {
        EXPECT_NE(std::string::npos, d3d12.find(
            "casePrimitiveType::" + std::string(topology) + ":"));
        EXPECT_NE(std::string::npos, d3d12.find(
            "DirectX12rendererdoesnotsupportPrimitiveType::" +
            std::string(topology) + ":"));
    }
    EXPECT_EQ(4u, CountOccurrences(d3d12, "ToD3D12Topology(primitive)"));
    EXPECT_EQ(std::string::npos, d3d12.find(
        "casePrimitiveType::PointListEXT:returnD3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;"));
    EXPECT_EQ(std::string::npos, d3d12.find(
        "casePrimitiveType::LineList:returnD3D_PRIMITIVE_TOPOLOGY_LINELIST;"));
    EXPECT_EQ(std::string::npos, d3d12.find(
        "casePrimitiveType::LineStrip:returnD3D_PRIMITIVE_TOPOLOGY_LINESTRIP;"));
}
