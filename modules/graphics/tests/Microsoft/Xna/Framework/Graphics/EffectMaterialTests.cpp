// SPDX-License-Identifier: MS-PL
// EffectMaterial deliberately has no Clone override in XNA: calling the
// inherited Effect.Clone returns an independent base Effect.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "CNA/TestSupport/TestPaths.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

TEST(EffectMaterialTest, InheritedCloneReturnsIndependentBaseEffect)
{
    GraphicsDevice gd;
    BasicEffect source(gd);
    EffectMaterial material(source);

    std::unique_ptr<Effect> cloned(material.Clone());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(dynamic_cast<EffectMaterial*>(cloned.get()), nullptr);
    EXPECT_NE(cloned.get(), static_cast<Effect*>(&material));
    EXPECT_EQ(cloned->GetTypeName(), "Microsoft.Xna.Framework.Graphics.Effect");
}


namespace
{
    // The same committed fixture EffectTests uses: a real compiled XNA Effect Framework
    // binary, so a clone has something with parameters to carry across.
    std::vector<SharpRuntime::bytecs> LoadCompiledEffectFixture()
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectDirectory() / "BasicEffect.fxb";
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

TEST(EffectMaterialTest, InheritedCloneCarriesIndependentCompiledState)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);
    EffectMaterial material(source);

    std::unique_ptr<Effect> clone(material.Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(dynamic_cast<EffectMaterial*>(clone.get()), nullptr);
    EXPECT_EQ(clone->getParametersProperty().getCountProperty(),
              material.getParametersProperty().getCountProperty());
    EXPECT_EQ(clone->getTechniquesProperty().getCountProperty(),
              material.getTechniquesProperty().getCountProperty());
    EXPECT_NE(clone->getParametersProperty()[0], material.getParametersProperty()[0]);
    EXPECT_NE(clone->getCurrentTechniqueProperty(), material.getCurrentTechniqueProperty());
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
        EXPECT_EQ(material.getParametersProperty()[i]->getNameProperty(),
                  source.getParametersProperty()[i]->getNameProperty());
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
    const std::string firstName = source.getParametersProperty()[0]->getNameProperty();

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

// TQ-035 replaces "CloningANonCompiledSourceGivesTheBareDefaultTechnique", whose expectations
// (one "Default" technique, no parameters) described CNA rather than XNA. XNA's EffectMaterial is
// `: base(cloneSource)`, and Effect::.ctor(Effect) IL_0048-0085 clones the native effect -- its
// techniques, passes, parameters and their current values -- before IL_00b8 reflects the copy
// and IL_00be-00cb select Techniques[0]. A stock effect is an Effect like any other there, so the
// material keeps everything. BasicEffect's SpecularColor, SpecularPower and FogColor setters write
// their parameter at once (set_SpecularColor/set_SpecularPower/set_FogColor IL_0000-000c each);
// World and ShaderIndex are written raw, as a game could through Parameters.
TEST(EffectMaterialTest, CloningAStockEffectKeepsItsTechniquesParametersAndValues)
{
    GraphicsDevice gd;
    BasicEffect source(gd);
    source.setSpecularColorProperty(Vector3(0.25f, 0.5f, 0.75f));
    source.setSpecularPowerProperty(7.0f);
    source.setFogColorProperty(Vector3(0.125f, 0.25f, 0.375f));
    source.getParametersProperty()["World"]->SetValue(Matrix::CreateTranslation(1.0f, 2.0f, 3.0f));
    source.getParametersProperty()["ShaderIndex"]->SetValue(5);

    EffectMaterial material(source);

    const auto& techniques = material.getTechniquesProperty();
    ASSERT_EQ(techniques.getCountProperty(), source.getTechniquesProperty().getCountProperty());
    for (int t = 0; t < techniques.getCountProperty(); ++t)
    {
        const auto* expected = source.getTechniquesProperty()[t];
        EXPECT_EQ(techniques[t]->getNameProperty(), expected->getNameProperty());
        ASSERT_EQ(techniques[t]->getPassesProperty().getCountProperty(),
                  expected->getPassesProperty().getCountProperty());
        for (int p = 0; p < techniques[t]->getPassesProperty().getCountProperty(); ++p)
            EXPECT_EQ(techniques[t]->getPassesProperty()[p]->getNameProperty(),
                      expected->getPassesProperty()[p]->getNameProperty());
    }
    EXPECT_EQ(techniques[0]->getNameProperty(), "BasicEffect");
    EXPECT_EQ(material.getCurrentTechniqueProperty(), techniques[0]);

    const auto& parameters = material.getParametersProperty();
    ASSERT_EQ(parameters.getCountProperty(), source.getParametersProperty().getCountProperty());
    for (int i = 0; i < parameters.getCountProperty(); ++i)
    {
        const auto* expected = source.getParametersProperty()[i];
        EXPECT_EQ(parameters[i]->getNameProperty(), expected->getNameProperty());
        EXPECT_EQ(parameters[i]->getParameterClassProperty(),
                  expected->getParameterClassProperty());
        EXPECT_EQ(parameters[i]->getParameterTypeProperty(),
                  expected->getParameterTypeProperty());
        EXPECT_EQ(parameters[i]->getRowCountProperty(), expected->getRowCountProperty());
        EXPECT_EQ(parameters[i]->getColumnCountProperty(), expected->getColumnCountProperty());
        EXPECT_NE(parameters[i], expected) << "the material must own its parameters";
    }

    EXPECT_EQ(parameters["SpecularColor"]->GetValueVector3(), Vector3(0.25f, 0.5f, 0.75f));
    EXPECT_FLOAT_EQ(parameters["SpecularPower"]->GetValueSingle(), 7.0f);
    EXPECT_EQ(parameters["FogColor"]->GetValueVector3(), Vector3(0.125f, 0.25f, 0.375f));
    EXPECT_EQ(parameters["World"]->GetValueMatrix(), Matrix::CreateTranslation(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(parameters["ShaderIndex"]->GetValueInt32(), 5);

    // Independent storage: writing the material leaves the source alone, and the reverse.
    material.getParametersProperty()["SpecularPower"]->SetValue(9.0f);
    source.setFogColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(source.getSpecularPowerProperty(), 7.0f);
    EXPECT_EQ(parameters["FogColor"]->GetValueVector3(), Vector3(0.125f, 0.25f, 0.375f));
}

// Effect.Clone IL_0000-0006 is `new Effect(this)`, so the base-Effect clone of such a material
// keeps the same graph and values.
TEST(EffectMaterialTest, TheInheritedCloneOfAStockMaterialKeepsItsValues)
{
    GraphicsDevice gd;
    BasicEffect source(gd);
    source.setSpecularPowerProperty(7.0f);
    EffectMaterial material(source);

    std::unique_ptr<Effect> clone(material.Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetTypeName(), "Microsoft.Xna.Framework.Graphics.Effect");
    ASSERT_EQ(clone->getTechniquesProperty().getCountProperty(), 1);
    EXPECT_EQ(clone->getTechniquesProperty()[0]->getNameProperty(), "BasicEffect");
    ASSERT_EQ(clone->getParametersProperty().getCountProperty(),
              source.getParametersProperty().getCountProperty());
    EXPECT_FLOAT_EQ(clone->getParametersProperty()["SpecularPower"]->GetValueSingle(), 7.0f);
}

// SkinnedEffect.SetBoneTransforms writes the Bones array at once (FNA SkinnedEffect.cs, and the
// clone's IL_00d0-016f re-copy numeric arrays explicitly). The material's bone matrices equal the
// source's and live in storage of their own. Both sides are read through the same raw parameter
// getter: SkinnedEffect.GetBoneTransforms additionally sets M44 = 1 on each 4x3 bone.
TEST(EffectMaterialTest, CloningASkinnedEffectGivesTheBoneArrayStorageOfItsOwn)
{
    GraphicsDevice gd;
    SkinnedEffect source(gd);
    source.SetBoneTransforms({Matrix::CreateTranslation(1.0f, 2.0f, 3.0f),
                              Matrix::CreateScale(2.0f)});
    auto* sourceBonesParameter = source.getParametersProperty()["Bones"];
    ASSERT_NE(sourceBonesParameter, nullptr);
    const std::vector<Matrix> sourceBones = sourceBonesParameter->GetValueMatrixArray(2);

    EffectMaterial material(source);
    auto* bones = material.getParametersProperty()["Bones"];
    ASSERT_NE(bones, nullptr);
    EXPECT_EQ(bones->getElementsProperty().getCountProperty(),
              sourceBonesParameter->getElementsProperty().getCountProperty());
    const std::vector<Matrix> materialBones = bones->GetValueMatrixArray(2);
    ASSERT_EQ(materialBones.size(), 2u);
    EXPECT_EQ(materialBones[0], sourceBones[0]);
    EXPECT_EQ(materialBones[1], sourceBones[1]);
    // CreateTranslation(1, 2, 3) keeps its translation in row 4 of a 4x3 bone; CreateScale(2) its
    // diagonal.
    EXPECT_FLOAT_EQ(materialBones[0].M41, 1.0f);
    EXPECT_FLOAT_EQ(materialBones[0].M42, 2.0f);
    EXPECT_FLOAT_EQ(materialBones[0].M43, 3.0f);
    EXPECT_FLOAT_EQ(materialBones[1].M11, 2.0f);
    EXPECT_FLOAT_EQ(materialBones[1].M33, 2.0f);

    const Matrix identity = Matrix::getIdentityProperty();
    bones->SetValue(std::vector<Matrix>{identity, identity});
    EXPECT_EQ(sourceBonesParameter->GetValueMatrixArray(2)[0], sourceBones[0]);
    EXPECT_EQ(bones->getElementsProperty()[0]->GetValueMatrix(),
              bones->GetValueMatrixArray(1)[0])
        << "the element views read the material's own store";
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

    EXPECT_NE(a.getParametersProperty()[0], source.getParametersProperty()[0]);
    EXPECT_NE(a.getParametersProperty()[0], b.getParametersProperty()[0]);
}

// SAMPLE-032: EffectParameter stores a raw Texture*, so a material whose parameters name
// textures has to own them. The XNB reader loads them into a value table it then discards,
// which left every textured material dereferencing freed memory at its first draw -- a segfault
// inside dynamic_cast, reached from Effect::SyncCompiledParameters. Ownership is asserted here
// rather than inferred from a read through a pointer that may merely happen to still be
// readable.
TEST(EffectMaterialTest, RetainedParameterTexturesOutliveTheCallersHandle)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "renderer does not support compiled effects";

    const auto bytes = LoadCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    CompiledSourceEffect source(gd, bytes);
    EffectMaterial material(source);
    EXPECT_EQ(material.GetRetainedParameterTextureCountEXT(), 0u);

    Microsoft::Xna::Framework::Graphics::Texture* raw = nullptr;
    std::weak_ptr<Microsoft::Xna::Framework::Graphics::Texture> observer;
    {
        auto texture = std::make_shared<Microsoft::Xna::Framework::Graphics::Texture2D>(gd, 2, 2);
        raw = texture.get();
        observer = std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::Texture>(texture);
        material.RetainParameterTextureEXT(texture);
    }

    EXPECT_EQ(material.GetRetainedParameterTextureCountEXT(), 1u);
    EXPECT_FALSE(observer.expired())
        << "the material must keep the texture alive after the loader's handle is gone";
    EXPECT_EQ(observer.lock().get(), raw);

    // A null retains nothing rather than growing the list with an unusable entry.
    material.RetainParameterTextureEXT(nullptr);
    EXPECT_EQ(material.GetRetainedParameterTextureCountEXT(), 1u);
}
