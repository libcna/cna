// SPDX-License-Identifier: MS-PL
// Task 883: EffectMaterial::Clone() — the last of the 8 concrete Effect
// subclasses (6 XNA stock effects + EffectMaterial + the CNAEXT ShaderEffect
// extension) to gain a Clone() override once Effect::Clone() became a pure
// virtual base-class contract.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(EffectMaterialTest, CloneReturnsIndependentEffectMaterial)
{
    GraphicsDevice gd;
    BasicEffect source(gd);
    EffectMaterial material(source);

    std::unique_ptr<Effect> cloned(material.Clone());
    auto* clone = dynamic_cast<EffectMaterial*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&material));
    EXPECT_EQ(clone->GetTypeName(), "Microsoft.Xna.Framework.Graphics.EffectMaterial");
}


namespace
{
    // The same committed fixture EffectTests uses: a real compiled XNA Effect Framework
    // binary, so a clone has something with parameters to carry across.
    std::vector<SharpRuntime::bytecs> LoadCompiledEffectFixture()
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../renderers/fna3d/effects/BasicEffect.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    // Effect's clone constructor is protected, as in XNA; this exposes it for the tests
    // the way EffectMaterial itself uses it.
    class CompiledSourceEffect : public Effect
    {
    public:
        CompiledSourceEffect(GraphicsDevice& device,
                             const std::vector<SharpRuntime::bytecs>& effectCode)
            : Effect(device, effectCode) {}
        Effect* Clone() override { return nullptr; }
    };
}

// SAMPLE-028: EffectMaterial used to construct through Effect(GraphicsDevice), which
// throws away everything about the source -- a material cloned from a compiled effect had
// ZERO parameters, so a game setting effect.Parameters["X"] got a null back. XNA's
// EffectMaterial is `: base(cloneSource)`, and that constructor did not exist in CNA.

TEST(EffectMaterialTest, CarriesTheSourceEffectsParametersAcross)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);
    ASSERT_GT(source.getParametersProperty().getCountProperty(), 0);

    EffectMaterial material(source);
    EXPECT_EQ(material.getParametersProperty().getCountProperty(),
              source.getParametersProperty().getCountProperty());
    for (int i = 0; i < source.getParametersProperty().getCountProperty(); ++i)
    {
        EXPECT_EQ(material.getParametersProperty()[i].getNameProperty(),
                  source.getParametersProperty()[i].getNameProperty());
    }
}

TEST(EffectMaterialTest, ParametersAreReachableByName)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);
    const std::string firstName = source.getParametersProperty()[0].getNameProperty();

    EffectMaterial material(source);
    // The lookup that returned nullptr before this fix, which a game then dereferenced.
    EXPECT_NE(material.getParametersProperty()[firstName], nullptr);
}

TEST(EffectMaterialTest, TechniquesAreClonedTooNotLeftEmpty)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);

    EffectMaterial material(source);
    EXPECT_EQ(material.getTechniquesProperty().getCountProperty(),
              source.getTechniquesProperty().getCountProperty());
    EXPECT_NE(material.getCurrentTechniqueProperty(), nullptr);
}

TEST(EffectMaterialTest, CloningANonCompiledSourceGivesTheBareDefaultTechnique)
{
    // A stock effect has no compiled runtime for the renderer to clone, so the material
    // gets the same single "Default" technique a bare Effect has -- not an empty one.
    GraphicsDevice gd;
    BasicEffect source(gd);
    EffectMaterial material(source);

    ASSERT_EQ(material.getTechniquesProperty().getCountProperty(), 1);
    EXPECT_EQ(material.getTechniquesProperty()[0].getNameProperty(), "Default");
    EXPECT_NE(material.getCurrentTechniqueProperty(), nullptr);
}

TEST(EffectMaterialTest, TheCloneIsIndependentOfItsSource)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);
    EffectMaterial a(source);
    EffectMaterial b(source);

    EXPECT_NE(&a.getParametersProperty()[0], &source.getParametersProperty()[0]);
    EXPECT_NE(&a.getParametersProperty()[0], &b.getParametersProperty()[0]);
}
