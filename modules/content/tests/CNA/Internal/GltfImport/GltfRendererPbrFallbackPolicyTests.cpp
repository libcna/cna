// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-374: every PBR-capable renderer must bind the same neutral textures when a
// material map is absent. This is a repository-wide renderer contract, so it is tested from the
// renderer sources even when the current host cannot compile or execute a particular backend.

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

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
