// SPDX-License-Identifier: MS-PL
// NOXNA Example — compile-time demonstration of CNA extended graphics API.
//
// Build with: cmake -DCNA_NOXNA=ON -DCNA_GRAPHICS_BACKEND=OPENGLES ..
//
// This example does not draw anything.  It exercises the NOXNA settings
// API so the compiler verifies that all declarations compile correctly.

#ifdef CNA_NOXNA

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
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
    assert(mat.getMetallicFactor()           == 0.0f);
    assert(mat.getRoughnessFactor()          == 0.5f);
    assert(mat.getNormalScale()              == 1.0f);
    assert(mat.getOcclusionStrength()        == 1.0f);
    assert(!mat.isAlphaBlendEnabled());
    assert(mat.getAlphaCutoff()              == 0.5f);

    // round-trips
    mat.setMetallicFactor(1.0f);
    mat.setRoughnessFactor(0.25f);
    mat.setNormalScale(0.8f);
    mat.setOcclusionStrength(0.6f);
    mat.setAlphaBlendEnabled(true);
    mat.setAlphaCutoff(0.3f);

    assert(mat.getMetallicFactor()    == 1.0f);
    assert(mat.getRoughnessFactor()   == 0.25f);
    assert(mat.getNormalScale()       == 0.8f);
    assert(mat.getOcclusionStrength() == 0.6f);
    assert(mat.isAlphaBlendEnabled());
    assert(mat.getAlphaCutoff()       == 0.3f);

    std::puts("[PASS] PbrMaterial");
}

int main()
{
    std::puts("=== NOXNA settings example ===");
    testRenderPipelineSettings();
    testPbrMaterial();
    std::puts("=== All PASS ===");
    return 0;
}

#else // CNA_NOXNA

#include <cstdio>
int main()
{
    std::puts("NOXNA is disabled (compile with -DCNA_NOXNA=ON to enable).");
    return 0;
}

#endif // CNA_NOXNA
