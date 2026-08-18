// SPDX-License-Identifier: MS-PL
// MOD-1: CNAEXT.hpp is the master include of the CNA::Graphics extension layer. This translation
// unit deliberately includes *only* that header (plus gtest) — anything it fails to pull in shows
// up here as a compile error rather than as a surprise in consumer code. It also pins the second
// half of the contract: with CNA_CNAEXT undefined the header declares nothing at all, which is why
// every test below sits inside the guard and this file compiles to an empty TU in a default build.

#include <gtest/gtest.h>

#include "CNA/Graphics/CNAEXT.hpp"

#ifdef CNA_CNAEXT

#include <type_traits>

namespace {

using CNA::Graphics::AsciiQuantizeMode;
using CNA::Graphics::CRTMaskType;
using CNA::Graphics::DepthEffectMode;
using CNA::Graphics::DitherMode;
using CNA::Graphics::PbrMaterial;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;

TEST(CnaExtMasterIncludeTest, ConfigurationTypesAreVisible)
{
    RenderPipelineSettings settings;

    EXPECT_FALSE(settings.isHDREnabled());
    EXPECT_EQ(settings.getTonemappingMode(), TonemappingMode::None);
    EXPECT_EQ(settings.getRenderQuality(), RenderQuality::Medium);
    EXPECT_EQ(settings.getShadowQuality(), ShadowQuality::Disabled);
}

TEST(CnaExtMasterIncludeTest, MaterialTypeIsVisible)
{
    static_assert(std::is_default_constructible_v<PbrMaterial>,
                  "PbrMaterial must remain default-constructible through the master include");

    const PbrMaterial material;

    EXPECT_GE(material.getMetallicFactor(), 0.0f);
}

TEST(CnaExtMasterIncludeTest, PostProcessEnumerationsAreVisible)
{
    EXPECT_EQ(static_cast<int>(DepthEffectMode::Color16Bit), 0);
    EXPECT_EQ(static_cast<int>(DitherMode::None), 0);
    EXPECT_EQ(static_cast<int>(CRTMaskType::None), 0);
    EXPECT_EQ(static_cast<int>(AsciiQuantizeMode::BlackWhite), 0);
}

TEST(CnaExtMasterIncludeTest, EffectTypesAreVisible)
{
    // The three post-process effects are class types, not forward declarations: sizeof() only
    // compiles against a complete type, which is exactly what a master include must deliver.
    EXPECT_GT(sizeof(CNA::Graphics::DepthEffect), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::CRTEffect), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::AsciiPostProcessEffect), 0u);
    // Shadows, both shapes -- the master include is the one place a consumer should have to name.
    EXPECT_GT(sizeof(CNA::Graphics::ShadowMap), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::CascadedShadowMap), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::CubeShadowMap), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::SpotShadowMap), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::PointLightEXT), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::SpotLightEXT), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::Skybox), 0u);
}

} // namespace

#endif // CNA_CNAEXT
