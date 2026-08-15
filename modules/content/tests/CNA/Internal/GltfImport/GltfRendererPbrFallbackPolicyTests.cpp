// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-163/184/373/374/379/394: repository-wide renderer contracts are tested from the
// renderer sources even when the current host cannot compile or execute a particular backend.
// This covers 32-bit index-factory ownership as well as PBR texture bindings, packed-channel
// semantics and neutral fallbacks.

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

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

    struct RendererAudit
    {
        const char* name;
        const char* normal;
        const char* metallicRoughness;
        const char* emissive;
        const char* occlusion;
    };

    // These fragments name the actual null branch (or the preselected default for Wicked) for
    // each semantic. Whitespace is ignored, but the map/fallback pairing is not: changing a normal
    // slot to white, or any other slot to the flat-normal texture, fails this table.
    constexpr std::array<RendererAudit, 15> kAudits{{
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
        std::array<const char*, 16> evidence;
    };

    constexpr std::array<RendererSlotAudit, 15> kSlotAudits{{
        {"bgfx", "stages 0,1,2,3,4",
         {{R"(texColor3DSampler_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler))",
           R"(normalMapSampler_ = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler))",
           R"(metallicRoughnessSampler_ = bgfx::createUniform("s_texMetallicRoughness", bgfx::UniformType::Sampler))",
           R"(emissiveMapSampler_ = bgfx::createUniform("s_texEmissive", bgfx::UniformType::Sampler))",
           R"(occlusionMapSampler_ = bgfx::createUniform("s_texOcclusion", bgfx::UniformType::Sampler))",
           R"(BindSamplerSlot(1, normalMapSampler_, params.pbrNormalMap, defaultFlatNormalTexture3D_);
              BindSamplerSlot(2, metallicRoughnessSampler_, params.pbrMetallicRoughnessMap, defaultWhiteTexture3D_);
              BindSamplerSlot(3, emissiveMapSampler_, params.pbrEmissiveMap, defaultWhiteTexture3D_);
              BindSamplerSlot(4, occlusionMapSampler_, params.pbrOcclusionMap, defaultWhiteTexture3D_);
              BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);)",
           R"(SAMPLER2D(s_texColor, 0);
              SAMPLER2D(s_texNormal, 1);
              SAMPLER2D(s_texMetallicRoughness, 2);
              SAMPLER2D(s_texEmissive, 3);
              SAMPLER2D(s_texOcclusion, 4);)"}}},
        {"diligent", "shader-resource names; sampler-state slots 0,1,2,3,4",
         {{R"(texture = params->texture0)",
           R"(cached.textureVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_Texture"))",
           R"(pipeline.textureVariable->Set(view))",
           R"(cached.normalMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_NormalMap"))",
           R"(cached.metallicRoughnessVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_MetallicRoughnessMap"))",
           R"(cached.emissiveMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_EmissiveMap"))",
           R"(cached.occlusionMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_OcclusionMap"))",
           R"(BindPbrMap(pipeline.normalMapVariable, params != nullptr ? params->pbrNormalMap : nullptr,
                         flatNormalTextureView_, 1))",
           R"(BindPbrMap(pipeline.metallicRoughnessVariable,
                         params != nullptr ? params->pbrMetallicRoughnessMap : nullptr,
                         fallbackTextureView_, 2))",
           R"(BindPbrMap(pipeline.emissiveMapVariable,
                         params != nullptr ? params->pbrEmissiveMap : nullptr, fallbackTextureView_, 3))",
           R"(BindPbrMap(pipeline.occlusionMapVariable,
                         params != nullptr ? params->pbrOcclusionMap : nullptr, fallbackTextureView_, 4))",
           R"(Texture2D g_Texture;
              SamplerState g_Texture_sampler;
              Texture2D g_NormalMap;
              SamplerState g_NormalMap_sampler;
              Texture2D g_MetallicRoughnessMap;
              SamplerState g_MetallicRoughnessMap_sampler;
              Texture2D g_EmissiveMap;
              SamplerState g_EmissiveMap_sampler;
              Texture2D g_OcclusionMap;
              SamplerState g_OcclusionMap_sampler;)"}}},
        {"directx9", "sampler registers s0,s1,s2,s3,s4",
         {{R"(BindPbrSampler(device_.Get(), 0, params.texture0, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 1, params.pbrNormalMap, ResolveD3D9TextureEXT(GetOrCreateDefaultFlatNormalTextureEXT()));
              BindPbrSampler(device_.Get(), 2, params.pbrMetallicRoughnessMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 3, params.pbrEmissiveMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
              BindPbrSampler(device_.Get(), 4, params.pbrOcclusionMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));)",
           R"(sampler2D Texture : register(s0);
              sampler2D NormalMap : register(s1);
              sampler2D MetallicRoughnessMap : register(s2);
              sampler2D EmissiveMap : register(s3);
              sampler2D OcclusionMap : register(s4);)"}}},
        {"directx11", "SRV/sampler registers t0/s0 through t4/s4",
         {{R"(srvs[0] = GetSrvForTextureEXT(params.texture0);
              srvs[1] = params.pbrNormalMap ? GetSrvForTextureEXT(params.pbrNormalMap) : GetOrCreateDefaultFlatNormalSrvEXT();
              srvs[2] = params.pbrMetallicRoughnessMap ? GetSrvForTextureEXT(params.pbrMetallicRoughnessMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[3] = params.pbrEmissiveMap ? GetSrvForTextureEXT(params.pbrEmissiveMap) : GetOrCreateDefaultWhiteSrvEXT();
              srvs[4] = params.pbrOcclusionMap ? GetSrvForTextureEXT(params.pbrOcclusionMap) : GetOrCreateDefaultWhiteSrvEXT();)",
           R"(context_->PSSetShaderResources(0, 5, srvs))",
           R"(Texture2D uTexture : register(t0);
              SamplerState uTextureSampler : register(s0);
              Texture2D uNormalMap : register(t1);
              SamplerState uNormalMapSampler : register(s1);
              Texture2D uMetallicRoughnessMap : register(t2);
              SamplerState uMetallicRoughnessSampler : register(s2);
              Texture2D uEmissiveMap : register(t3);
              SamplerState uEmissiveMapSampler : register(s3);
              Texture2D uOcclusionMap : register(t4);
              SamplerState uOcclusionMapSampler : register(s4);)"}}},
        {"directx12", "separate descriptor tables for t0/s0 through t4/s4",
         {{R"(srvTextures[0] = params.texture0;
              srvTextures[1] = params.pbrNormalMap ? params.pbrNormalMap : GetOrCreateDefaultFlatNormalTextureEXT();
              srvTextures[2] = params.pbrMetallicRoughnessMap ? params.pbrMetallicRoughnessMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[3] = params.pbrEmissiveMap ? params.pbrEmissiveMap : GetOrCreateDefaultWhiteTextureEXT();
              srvTextures[4] = params.pbrOcclusionMap ? params.pbrOcclusionMap : GetOrCreateDefaultWhiteTextureEXT();)",
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
              SamplerState uOcclusionMapSampler : register(s4);)"}}},
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
        {"magnum", "GL texture units 0,1,2,3,4",
         {{R"(constexpr int kPbrNormalMapSlot = 1;
              constexpr int kPbrMetallicRoughnessMapSlot = 2;
              constexpr int kPbrEmissiveMapSlot = 3;
              constexpr int kPbrOcclusionMapSlot = 4;)",
           R"(BindTextureToSlot(0, params.texture0);
              program.SetInt(program.LocationOf("uTexture"), 0);)",
           R"(BindPbrMap(program, "uNormalMap", kPbrNormalMapSlot, params.pbrNormalMap)",
           R"(BindPbrMap(program, "uMetallicRoughnessMap", kPbrMetallicRoughnessMapSlot)",
           R"(params.pbrMetallicRoughnessMap, *defaultWhiteTexture_)",
           R"(BindPbrMap(program, "uEmissiveMap", kPbrEmissiveMapSlot, params.pbrEmissiveMap)",
           R"(BindPbrMap(program, "uOcclusionMap", kPbrOcclusionMapSlot, params.pbrOcclusionMap)",
           R"(uniform sampler2D uTexture;)" ,
           R"(uniform sampler2D uNormalMap;)" ,
           R"(uniform sampler2D uMetallicRoughnessMap;)" ,
           R"(uniform sampler2D uEmissiveMap;)" ,
           R"(uniform sampler2D uOcclusionMap;)"}}},
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
        {"sdl-gpu", "fragment sampler bindings 0,1,2,3,4",
         {{R"(samplerBindings[0].texture = command.texture.texture)",
           R"(samplerBindings[1].texture = command.normalMap ? command.normalMap.texture : defaultFlatNormalTexture_->Texture())",
           R"(samplerBindings[2].texture = command.metallicRoughnessMap ? command.metallicRoughnessMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[3].texture = command.emissiveMap ? command.emissiveMap.texture : defaultWhiteTexture_->Texture())",
           R"(samplerBindings[4].texture = command.occlusionMap ? command.occlusionMap.texture : defaultWhiteTexture_->Texture())",
           R"(SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 5))",
           R"(layout(set = 2, binding = 0) uniform sampler2D uTexture;
              layout(set = 2, binding = 1) uniform sampler2D uNormalMap;
              layout(set = 2, binding = 2) uniform sampler2D uMetallicRoughnessMap;
              layout(set = 2, binding = 3) uniform sampler2D uEmissiveMap;
              layout(set = 2, binding = 4) uniform sampler2D uOcclusionMap;)"}}},
        {"vulkan", "descriptor set 0 bindings 0,1,2,3,4",
         {{R"(VkImageView views[5] = { baseColor, normalMap, metallicRoughness, emissive, occlusion })",
           R"(writes[i].dstBinding = i)",
           R"(GetOrCreatePbrDescSet(currentFrame_, vBase, vNorm, vMR, vEmis, vOcc,
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
            R"(OpenGL4RawProgram& prog = (strideInBytes == 68) ? pbrSkinned3DProgram_ : pbr3DProgram_)",
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
         {{"texture2D(s_texColor, rtFlipUV(pbrTransformUV(v_texcoord0, 0), u_rtFlipV.x))",
           "texture2D(s_texNormal, rtFlipUV(pbrTransformUV(v_texcoord0, 1), u_rtFlipV.y))",
           "texture2D(s_texMetallicRoughness, rtFlipUV(pbrTransformUV(v_texcoord0, 2), u_rtFlipV.z))",
           "texture2D(s_texEmissive, rtFlipUV(pbrTransformUV(v_texcoord0, 3), u_rtFlipV.w))",
           "texture2D(s_texOcclusion, pbrTransformUV(v_texcoord0, 4))"}}, 1},
        {"diligent",
         "std::memcpy(values + 16, params.pbrTextureTransformRows",
         "g_PbrTextureTransformRows[slot * 2 + 1].xyz",
         {{"g_Texture.Sample(g_Texture_sampler, CnaPbrTransformUv(psIn.UV, 0))",
           "g_NormalMap.Sample(g_NormalMap_sampler, CnaPbrTransformUv(psIn.UV, 1))",
           "g_MetallicRoughnessMap.Sample(g_MetallicRoughnessMap_sampler, CnaPbrTransformUv(psIn.UV, 2))",
           "g_EmissiveMap.Sample(g_EmissiveMap_sampler, CnaPbrTransformUv(psIn.UV, 3))",
           "g_OcclusionMap.Sample(g_OcclusionMap_sampler, CnaPbrTransformUv(psIn.UV, 4))"}}, 1},
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
         {{"uTexture.Sample(uTextureSampler, CnaPbrTransformUv(input.UV, 0))",
           "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(input.UV, 1))",
           "uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, CnaPbrTransformUv(input.UV, 2))",
           "uEmissiveMap.Sample(uEmissiveMapSampler, CnaPbrTransformUv(input.UV, 3))",
           "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(input.UV, 4))"}}, 2},
        {"directx12",
         "std::memcpy(perDraw.TextureTransformRows, params.pbrTextureTransformRows",
         "TextureTransformRows[slot * 2 + 1].xyz",
         {{"uTexture.Sample(uTextureSampler, CnaPbrTransformUv(input.UV, 0))",
           "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(input.UV, 1))",
           "uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, CnaPbrTransformUv(input.UV, 2))",
           "uEmissiveMap.Sample(uEmissiveMapSampler, CnaPbrTransformUv(input.UV, 3))",
           "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(input.UV, 4))"}}, 2},
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
         {{"texture(sampler2D(colorMap, samplerState), cnaPbrTransformUV(vTexCoord, 0))",
           "texture(sampler2D(normalMap, normalMapSampler), cnaPbrTransformUV(vTexCoord, 1))",
           "texture(sampler2D(metallicRoughnessMap, metallicRoughnessMapSampler), cnaPbrTransformUV(vTexCoord, 2))",
           "texture(sampler2D(emissiveMap, emissiveMapSampler), cnaPbrTransformUV(vTexCoord, 3))",
           "texture(sampler2D(occlusionMap, occlusionMapSampler), cnaPbrTransformUV(vTexCoord, 4))"}}, 1},
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
         "vec3 F0 = mix(u_dielectricFresnel.xyz, albedo, metallic)",
         "vec3 F90 = mix(vec3_splat(u_dielectricFresnel.w), vec3_splat(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"diligent",
         "float3 F0 = lerp(g_PbrDielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = lerp(float3(g_PbrDielectricFresnel.w, g_PbrDielectricFresnel.w, g_PbrDielectricFresnel.w), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 1},
        {"directx9",
         "float3 F0 = lerp(DielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = lerp(float3(DielectricFresnel.w, DielectricFresnel.w, DielectricFresnel.w), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"directx11",
         "float3 F0 = lerp(DielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = lerp(float3(DielectricFresnel.w, DielectricFresnel.w, DielectricFresnel.w), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"directx12",
         "float3 F0 = lerp(DielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = lerp(float3(DielectricFresnel.w, DielectricFresnel.w, DielectricFresnel.w), float3(1.0, 1.0, 1.0), metallic)",
         "float3 F = F0 + (F90 - F0) *", 2},
        {"easygl",
         "vec3 F0=mix(uDielectricFresnel.xyz,albedo,metallic)",
         "vec3 F90=mix(vec3(uDielectricFresnel.w),vec3(1.0),metallic)",
         "vec3 F=F0+(F90-F0)*", 2},
        {"llgl",
         "vec3 F0 = mix(dielectricFresnel.xyz, albedo, metallic)",
         "vec3 F90 = mix(vec3(dielectricFresnel.w), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 3},
        {"magnum",
         "vec3 f0 = mix(uDielectricFresnel.xyz, albedo, metallic)",
         "vec3 f90 = mix(vec3(uDielectricFresnel.w), vec3(1.0), metallic)",
         "vec3 fresnel = f0 + (f90 - f0) *", 1},
        {"metal",
         "float3 F0 = mix(pu.dielectricFresnel.xyz, albedo, metallic)",
         "float3 F90 = mix(float3(pu.dielectricFresnel.w), float3(1.0), metallic)",
         "float3 F = F0 + (F90-F0) *", 1},
        {"opengl2",
         "vec3 F0=mix(uDielectricFresnel.xyz,albedo,metallic)",
         "vec3 F90=mix(vec3(uDielectricFresnel.w),vec3(1.0),metallic)",
         "vec3 F=F0+(F90-F0)*", 1},
        {"opengl4",
         "vec3 F0 = mix(uDielectricFresnel.xyz, albedo, metallic)",
         "vec3 F90 = mix(vec3(uDielectricFresnel.w), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"sdl-gpu",
         "vec3 F0 = mix(pbrp.dielectricFresnel.xyz, albedo, metallic)",
         "vec3 F90 = mix(vec3(pbrp.dielectricFresnel.w), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 1},
        {"vulkan",
         "vec3 F0 = mix(pbr.dielectricFresnel.xyz, albedo, metallic)",
         "vec3 F90 = mix(vec3(pbr.dielectricFresnel.w), vec3(1.0), metallic)",
         "vec3 F = F0 + (F90 - F0) *", 2},
        {"webgpu",
         "let f0 = mix(pf.dielectricFresnel.xyz, albedo, metallic)",
         "let f90 = mix(vec3f(pf.dielectricFresnel.w), vec3f(1.0), metallic)",
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
         "texture2D(s_texNormal, rtFlipUV(pbrTransformUV(v_texcoord0, 1), u_rtFlipV.y)).rgb * 2.0 - 1.0",
         "mr.g * u_metallicRoughnessFactor.y",
         "mr.b * u_metallicRoughnessFactor.x",
         "texture2D(s_texOcclusion, pbrTransformUV(v_texcoord0, 4)).r", 1},
        {"diligent",
         "g_NormalMap.Sample(g_NormalMap_sampler, CnaPbrTransformUv(psIn.UV, 1)).rgb * 2.0 - 1.0",
         "mr.g * g_PbrEmissiveRoughness.w",
         "mr.b * g_PbrAmbientMetallic.w",
         "g_OcclusionMap.Sample(g_OcclusionMap_sampler, CnaPbrTransformUv(psIn.UV, 4)).r", 1},
        {"directx9",
         "tex2D(NormalMap, CnaPbrTransformUv(pin.UV, 1)).rgb * 2.0 - 1.0",
         "mr.g * MetallicRoughnessFactor.y",
         "mr.b * MetallicRoughnessFactor.x",
         "tex2D(OcclusionMap, CnaPbrTransformUv(pin.UV, 4)).r", 2},
        {"directx11",
         "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(input.UV, 1)).rgb * 2.0 - 1.0",
         "mr.g * EmissiveRoughness.w",
         "mr.b * AmbientMetallic.w",
         "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(input.UV, 4)).r", 2},
        {"directx12",
         "uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(input.UV, 1)).rgb * 2.0 - 1.0",
         "mr.g * EmissiveRoughness.w",
         "mr.b * AmbientMetallic.w",
         "uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(input.UV, 4)).r", 2},
        {"easygl",
         "texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(\" + normalUv + \",1),uRtFlipV.y)).rgb*2.0-1.0",
         "mr.g*uRoughnessFactor",
         "mr.b*uMetallicFactor",
         "texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(\" + occlusionUv + \",4),uRtFlipVHi.x)).r", 2},
        {"llgl",
         "cnaPbrTransformUV(vTexCoord, 1)).rgb * 2.0 - 1.0",
         "mr.g * roughnessWeightsPad.x",
         "mr.b * emissiveMetallic.w",
         "cnaPbrTransformUV(vTexCoord, 4)).r;", 3},
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

        // RGBA8 cannot encode zero exactly after rgb*2-1; 128 is the conventional closest value.
        // It yields an almost exact (0,0,1), unlike white's (1,1,1), which tilts the normal by
        // 54.7 degrees and visibly changes every lit pixel.
        EXPECT_NE(std::string::npos, source.find("{128,128,255,255}"))
            << "no canonical flat-normal texel";
        EXPECT_NE(std::string::npos, source.find("{255,255,255,255}"))
            << "no canonical white texel";

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

TEST(GltfRendererPbrFallbackPolicy, RigidAndSkinnedShaderVariantsKeepTheSameFiveBindings)
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
            sampler2D OcclusionMap : register(s4);)"},
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
            SamplerState uOcclusionMapSampler : register(s4);)"},
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

    // Rigid and skinned WebGPU pipelines share the same 14-vec4 PbrFactors ABI. The old 16-byte
    // minimum accepted the first vector but caused Dawn to reject both pipelines as alpha coverage,
    // colour transfer, Fresnel endpoints and ten texture-transform rows expanded it to 224 bytes.
    const std::string webgpu = RendererSlotText(renderers, "webgpu");
    const std::string pbrFactorsSize = Normalize(
        "uboEntries[2].buffer.minBindingSize = 56 * sizeof(float)");
    std::size_t pbrFactorsSizeCount = 0;
    for (std::size_t at = webgpu.find(pbrFactorsSize); at != std::string::npos;
         at = webgpu.find(pbrFactorsSize, at + pbrFactorsSize.size()))
        ++pbrFactorsSizeCount;
    EXPECT_EQ(2u, pbrFactorsSizeCount)
        << "both WebGPU PBR pipeline layouts must expose the complete 224-byte factors block";
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
             "texture(colorMap, cnaPbrTransformUV(vTexCoord, 0))",
             "texture(normalMap, cnaPbrTransformUV(vTexCoord, 1))",
             "texture(metallicRoughnessMap, cnaPbrTransformUV(vTexCoord, 2))",
             "texture(emissiveMap, cnaPbrTransformUV(vTexCoord, 3))",
             "texture(occlusionMap, cnaPbrTransformUV(vTexCoord, 4))"})
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
        EXPECT_NE(std::string::npos, source.find("pbrDielectricF90"))
            << "the renderer does not upload the transported dielectric F90";

        for (const char* evidence :
             {audit.dielectricF0, audit.dielectricF90, audit.schlickEndpoints})
        {
            EXPECT_GE(CountOccurrences(source, Normalize(evidence)), audit.shaderCopies)
                << "missing rigid/skinned PBR Fresnel evidence: " << evidence;
        }
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
    // -> transported per-map selector -> all five shader samples. The shader source occurs twice
    // because rigid and skinned PBR intentionally share the same contract.
    const std::string vulkan = RendererSlotText(renderers, "vulkan");
    for (const char* evidence : {
             "GetOrCreatePipelinePbr3D(draw.stride, draw.topology",
             "GetOrCreatePipelinePbrSkinned3D(draw.stride, draw.topology",
             "stride != 48 && stride != 60",
             "stride != 68 && stride != 76",
             "attrs[4] = { 4, 0, VK_FORMAT_R32G32_SFLOAT, 48 }",
             "attrs[6] = { 6, 0, VK_FORMAT_R32G32_SFLOAT, 68 }",
             "out[104] = static_cast<float>(p.pbrTextureCoordinateSetMask & 0x1fu)"})
    {
        EXPECT_NE(std::string::npos, vulkan.find(Normalize(evidence)))
            << "Vulkan dual-UV PBR path is missing: " << evidence;
    }
    for (std::size_t slot = 0; slot < 5; ++slot)
    {
        const std::string sample = "CNA_PBR_UV(" + std::to_string(slot) + ")";
        EXPECT_EQ(2u, CountOccurrences(vulkan, Normalize(sample)))
            << "Vulkan rigid/skinned shaders do not select the authored UV set for map slot "
            << slot;
    }
    EXPECT_EQ(2u, CountOccurrences(vulkan, Normalize(
        "int mask = int(pbr.textureCoordinateSets.x + 0.5)")))
        << "both Vulkan dual-UV PBR fragment variants must decode the transported selector";
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

TEST(GltfRendererIndexWidthPolicy, InventoryClassifiesEveryRenderer)
{
    // A provider has a local CreateIndexBuffer32 implementation. The two explicit rejecters also
    // implement the method locally because their renderer-specific diagnostic is useful. The
    // remaining 2D/no-3D backends deliberately inherit the shared, unconditionally throwing
    // default. Keeping the three sets disjoint makes a new renderer an audit failure, not an
    // accidental 16-bit fallback.
    constexpr std::array<const char*, 31> providers{{
        "bgfx", "diligent", "directx10", "directx11", "directx12", "directx2",
        "directx3", "directx5", "directx6", "directx7", "directx8", "directx9",
        "easygl", "fna3d", "glide", "headless", "llgl", "magnum", "metal",
        "opengl1", "opengl2", "opengl4", "opengles1", "portablegl", "sdl-gpu",
        "software", "sokol", "stub", "vulkan", "webgpu", "wicked",
    }};
    constexpr std::array<const char*, 2> explicitRejecters{{"gdi", "skia"}};
    constexpr std::array<const char*, 9> inheritedRejecters{{
        "blend2d", "canvas", "direct2d", "directx1", "freedirect", "html-dom",
        "openvg", "sdl-renderer", "svg-dom",
    }};

    std::set<std::string> expected;
    for (const char* name : providers) { expected.insert(name); }
    for (const char* name : explicitRejecters) { expected.insert(name); }
    for (const char* name : inheritedRejecters) { expected.insert(name); }
    ASSERT_EQ(42u, expected.size()) << "the policy sets must be disjoint";

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
    constexpr std::array<const char*, 31> providers{{
        "bgfx", "diligent", "directx10", "directx11", "directx12", "directx2",
        "directx3", "directx5", "directx6", "directx7", "directx8", "directx9",
        "easygl", "fna3d", "glide", "headless", "llgl", "magnum", "metal",
        "opengl1", "opengl2", "opengl4", "opengles1", "portablegl", "sdl-gpu",
        "software", "sokol", "stub", "vulkan", "webgpu", "wicked",
    }};
    constexpr std::array<const char*, 9> inheritedRejecters{{
        "blend2d", "canvas", "direct2d", "directx1", "freedirect", "html-dom",
        "openvg", "sdl-renderer", "svg-dom",
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
