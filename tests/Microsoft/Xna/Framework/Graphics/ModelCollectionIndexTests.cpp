// SPDX-License-Identifier: MS-PL
// REMED-GFX-040: public Model-family collection index regressions.

#include <cstdint>

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelBoneCollection;
using Microsoft::Xna::Framework::Graphics::ModelEffectCollection;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshCollection;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::ModelMeshPartCollection;
using System::ArgumentOutOfRangeException;

namespace
{
    template<typename TCollection>
    void ExpectIndexArgumentOutOfRange(const TCollection& collection, int index)
    {
        try
        {
            (void) collection[index];
            FAIL() << "Expected index " << index << " to be rejected";
        }
        catch (const ArgumentOutOfRangeException& exception)
        {
            EXPECT_EQ(exception.getParamNameProperty(), "index");
        }
        catch (...)
        {
            FAIL() << "Expected System::ArgumentOutOfRangeException for index " << index;
        }
    }

    Effect* FakeEffect(int id)
    {
        // ModelEffectCollection stores and compares these identity-only pointers without
        // dereferencing them, matching the existing ModelMeshPart tests.
        return reinterpret_cast<Effect*>(static_cast<std::uintptr_t>(id));
    }

    struct PopulatedModelCollections
    {
        ModelBone root { 0, "Root" };
        ModelBone firstBone { 1, "FirstBone" };
        ModelBone lastBone { 2, "LastBone" };
        ModelMeshPart firstPart;
        ModelMeshPart lastPart;
        ModelMesh firstMesh { nullptr, "FirstMesh", { &firstPart, &lastPart } };
        ModelMesh lastMesh { nullptr, "LastMesh", {} };
        Model model {
            nullptr,
            { &root, &firstBone, &lastBone },
            { &firstMesh, &lastMesh },
            { &firstBone, &lastBone },
            0
        };
        Effect* firstEffect = FakeEffect(1);
        Effect* lastEffect = FakeEffect(2);

        PopulatedModelCollections()
        {
            root.AddChild(&firstBone);
            root.AddChild(&lastBone);
            firstPart.setEffectProperty(firstEffect);
            lastPart.setEffectProperty(lastEffect);
        }
    };
}

TEST(ModelCollectionIndexTest, EmptyCollectionsRejectNegativeCountAndAboveCountIndices)
{
    const ModelBoneCollection bones;
    const ModelMeshCollection meshes;
    const ModelMesh emptyMesh(nullptr, {});
    const ModelMeshPartCollection& parts = emptyMesh.getMeshPartsProperty();
    const ModelEffectCollection& effects = emptyMesh.getEffectsProperty();

    const auto expectEmptyBoundaries = [](const auto& collection)
    {
        ASSERT_EQ(collection.getCountProperty(), 0);
        ExpectIndexArgumentOutOfRange(collection, -1);
        ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty());
        ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty() + 4);
    };

    expectEmptyBoundaries(bones);
    expectEmptyBoundaries(meshes);
    expectEmptyBoundaries(parts);
    expectEmptyBoundaries(effects);
}

TEST(ModelCollectionIndexTest, BoneCollectionRejectsNegativeCountAndAboveCountIndices)
{
    PopulatedModelCollections fixture;
    const ModelBoneCollection& collection = fixture.root.getChildrenProperty();

    ExpectIndexArgumentOutOfRange(collection, -1);
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty());
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty() + 7);
}

TEST(ModelCollectionIndexTest, MeshCollectionRejectsNegativeCountAndAboveCountIndices)
{
    PopulatedModelCollections fixture;
    const ModelMeshCollection& collection = fixture.model.getMeshesProperty();

    ExpectIndexArgumentOutOfRange(collection, -1);
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty());
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty() + 7);
}

TEST(ModelCollectionIndexTest, MeshPartCollectionRejectsNegativeCountAndAboveCountIndices)
{
    PopulatedModelCollections fixture;
    const ModelMeshPartCollection& collection = fixture.firstMesh.getMeshPartsProperty();

    ExpectIndexArgumentOutOfRange(collection, -1);
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty());
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty() + 7);
}

TEST(ModelCollectionIndexTest, EffectCollectionRejectsNegativeCountAndAboveCountIndices)
{
    PopulatedModelCollections fixture;
    const ModelEffectCollection& collection = fixture.firstMesh.getEffectsProperty();

    ExpectIndexArgumentOutOfRange(collection, -1);
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty());
    ExpectIndexArgumentOutOfRange(collection, collection.getCountProperty() + 7);
}

TEST(ModelCollectionIndexTest, ValidFirstAndLastAccessPreservesIdentityOrderCountsAndOwners)
{
    PopulatedModelCollections fixture;

    const ModelBoneCollection& bones = fixture.root.getChildrenProperty();
    ASSERT_EQ(bones.getCountProperty(), 2);
    EXPECT_EQ(bones[0], &fixture.firstBone);
    EXPECT_EQ(bones[bones.getCountProperty() - 1], &fixture.lastBone);
    EXPECT_EQ(fixture.firstBone.getParentProperty(), &fixture.root);
    EXPECT_EQ(fixture.lastBone.getParentProperty(), &fixture.root);

    const ModelMeshCollection& meshes = fixture.model.getMeshesProperty();
    ASSERT_EQ(meshes.getCountProperty(), 2);
    EXPECT_EQ(meshes[0], &fixture.firstMesh);
    EXPECT_EQ(meshes[meshes.getCountProperty() - 1], &fixture.lastMesh);
    EXPECT_EQ(fixture.firstMesh.getParentBoneProperty(), &fixture.firstBone);
    EXPECT_EQ(fixture.lastMesh.getParentBoneProperty(), &fixture.lastBone);

    const ModelMeshPartCollection& parts = fixture.firstMesh.getMeshPartsProperty();
    ASSERT_EQ(parts.getCountProperty(), 2);
    EXPECT_EQ(parts[0], &fixture.firstPart);
    EXPECT_EQ(parts[parts.getCountProperty() - 1], &fixture.lastPart);

    const ModelEffectCollection& effects = fixture.firstMesh.getEffectsProperty();
    ASSERT_EQ(effects.getCountProperty(), 2);
    EXPECT_EQ(effects[0], fixture.firstEffect);
    EXPECT_EQ(effects[effects.getCountProperty() - 1], fixture.lastEffect);
    EXPECT_EQ(fixture.firstPart.getEffectProperty(), fixture.firstEffect);
    EXPECT_EQ(fixture.lastPart.getEffectProperty(), fixture.lastEffect);
}
