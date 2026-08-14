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
            || extension == ".h";
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
