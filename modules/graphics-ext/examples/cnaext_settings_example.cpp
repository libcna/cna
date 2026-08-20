// SPDX-License-Identifier: MS-PL
// CNAEXT Example — compile-time demonstration of CNA extended graphics API.
//
// Build with: cmake -DCNA_CNAEXT=ON -DCNA_GRAPHICS_RENDERER=OPENGLES3 ..
//
// This example does not draw anything.  It exercises the CNAEXT settings
// API so the compiler verifies that all declarations compile correctly.

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include <cstdio>
#include <cassert>

using namespace CNA::Graphics;

static void testRenderPipelineSettings()
{
    RenderPipelineSettings s;

    // defaults
    assert(!s.isHDREnabled());
    assert(s.getExposure()         == 1.0f);
    assert(s.getGamma()            == 2.2f);
    assert(s.getTonemappingMode()  == TonemappingMode::None);
    assert(!s.isBloomEnabled());
    assert(s.getBloomIntensity()   == 1.0f);
    assert(!s.isSSAOEnabled());
    assert(s.getRenderQuality()    == RenderQuality::Medium);
    assert(s.getShadowQuality()    == ShadowQuality::Disabled);
    assert(!s.isShadowsEnabled());

    // round-trips
    s.setHDREnabled(true);
    s.setExposure(2.5f);
    s.setGamma(2.0f);
    s.setTonemappingMode(TonemappingMode::Aces);
    s.setBloomEnabled(true);
    s.setBloomIntensity(0.8f);
    s.setSSAOEnabled(true);
    s.setRenderQuality(RenderQuality::High);
    s.setShadowQuality(ShadowQuality::High);
    s.setShadowsEnabled(true);

    assert(s.isHDREnabled());
    assert(s.getExposure()         == 2.5f);
    assert(s.getGamma()            == 2.0f);
    assert(s.getTonemappingMode()  == TonemappingMode::Aces);
    assert(s.isBloomEnabled());
    assert(s.getBloomIntensity()   == 0.8f);
    assert(s.isSSAOEnabled());
    assert(s.getRenderQuality()    == RenderQuality::High);
    assert(s.getShadowQuality()    == ShadowQuality::High);
    assert(s.isShadowsEnabled());

    std::puts("[PASS] RenderPipelineSettings");
}

static void testPbrMaterial()
{
    PbrMaterial mat;

    // defaults
    assert(mat.getAlbedoTexture()            == nullptr);
    assert(mat.getNormalTexture()            == nullptr);
    assert(mat.getMetallicRoughnessTexture() == nullptr);
    assert(mat.getAmbientOcclusionTexture()  == nullptr);
    assert(mat.getEmissiveTexture()          == nullptr);
    assert(mat.getSpecularTexture()          == nullptr);
    assert(mat.getSpecularColorTexture()     == nullptr);
    // plans/plan_modern.md MOD-1301: glTF's own default material, which is also PbrEffect's default --
    // metallic 1 and roughness 1, not the 0/0.5 this bag used before Phase 13.
    assert(mat.getMetallicFactor()           == 1.0f);
    assert(mat.getRoughnessFactor()          == 1.0f);
    assert(mat.getNormalScale()              == 1.0f);
    assert(mat.getOcclusionStrength()        == 1.0f);
    assert(mat.getIor()                      == 1.5f);
    assert(mat.getSpecularFactor()           == 1.0f);
    assert(mat.getAlphaMode() == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque);
    assert(!mat.isDoubleSided());
    assert(mat.getAlphaCutoff()              == 0.5f);
    assert(mat.getTextureCoordinateSet(CNA::Graphics::PbrTextureSlot::Emissive) == 0);

    // round-trips
    mat.setMetallicFactor(1.0f);
    mat.setRoughnessFactor(0.25f);
    mat.setNormalScale(0.8f);
    mat.setOcclusionStrength(0.6f);
    mat.setAlphaMode(Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend);
    mat.setDoubleSided(true);
    mat.setAlphaCutoff(0.3f);
    mat.setEmissiveFactor(Microsoft::Xna::Framework::Vector3(2.0f, 0.0f, 0.0f));

    assert(mat.getMetallicFactor()    == 1.0f);
    assert(mat.getRoughnessFactor()   == 0.25f);
    assert(mat.getNormalScale()       == 0.8f);
    assert(mat.getOcclusionStrength() == 0.6f);
    assert(mat.getAlphaMode() == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend);
    assert(mat.isDoubleSided());
    assert(mat.getAlphaCutoff()       == 0.3f);
    assert(mat.getEmissiveFactor().X  == 2.0f);
    // MOD-1311: the value semantics Phase 13 added.
    assert(mat == mat);
    assert(!(mat != mat));
    assert(mat != PbrMaterial{});
    assert(mat.GetHashCode() == mat.GetHashCode());
    assert(!mat.ToString().empty());

    std::puts("[PASS] PbrMaterial");
}

int main()
{
    std::puts("=== CNAEXT settings example ===");
    testRenderPipelineSettings();
    testPbrMaterial();
    std::puts("=== All PASS ===");
    return 0;
}

#else // CNA_CNAEXT

#include <cstdio>
int main()
{
    std::puts("CNAEXT is disabled (compile with -DCNA_CNAEXT=ON to enable).");
    return 0;
}

#endif // CNA_CNAEXT
