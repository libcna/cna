// SPDX-License-Identifier: MS-PL
// Task 433: ModelMeshCollection unit tests.
//
// ModelMeshCollection's storage is private and only Model can populate it, so these tests build a
// real populated collection via Model's own 3-arg constructor (GraphicsDevice* is unused by that
// constructor -- confirmed by reading Model.cpp -- so nullptr is safe here, matching the
// established ModelMeshTests.cpp convention of never touching a real GraphicsDevice).

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

TEST(ModelMeshCollectionTest, DefaultConstructorCountIsZero)
{
    ModelMeshCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(ModelMeshCollectionTest, IndexOutOfRangeThrows)
{
    ModelMeshCollection col;
    EXPECT_THROW({ [[maybe_unused]] auto* m = col[0]; }, std::out_of_range);
}

TEST(ModelMeshCollectionTest, NameLookupNotFoundThrows)
{
    ModelMeshCollection col;
    EXPECT_THROW({ [[maybe_unused]] auto* m = col[std::string("Body")]; }, std::out_of_range);
}

namespace
{
    struct PopulatedMeshes
    {
        ModelMesh body { nullptr, "Body", {} };
        ModelMesh head { nullptr, "Head", {} };
        Model model { nullptr, {}, { &body, &head } };
    };
}

TEST(ModelMeshCollectionTest, CountReflectsPopulatedSize)
{
    PopulatedMeshes fixture;
    EXPECT_EQ(fixture.model.getMeshesProperty().getCountProperty(), 2);
}

TEST(ModelMeshCollectionTest, IndexByIntReturnsCorrectMesh)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    EXPECT_EQ(col[0], &fixture.body);
    EXPECT_EQ(col[1], &fixture.head);
}

TEST(ModelMeshCollectionTest, IndexByNameReturnsCorrectMesh)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    EXPECT_EQ(col["Body"], &fixture.body);
    EXPECT_EQ(col["Head"], &fixture.head);
}

TEST(ModelMeshCollectionTest, IndexByNamePopulatedNotFoundThrows)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    EXPECT_THROW({ [[maybe_unused]] auto* m = col["Tail"]; }, std::out_of_range);
}

TEST(ModelMeshCollectionTest, TryGetValueFindsExisting)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    ModelMesh* found = nullptr;
    EXPECT_TRUE(col.TryGetValue("Head", found));
    EXPECT_EQ(found, &fixture.head);
}

TEST(ModelMeshCollectionTest, TryGetValueFailsForMissing)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    ModelMesh* found = nullptr;
    EXPECT_FALSE(col.TryGetValue("Tail", found));
    EXPECT_EQ(found, nullptr);
}

TEST(ModelMeshCollectionTest, ContainsFindsExisting)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    EXPECT_TRUE(col.Contains(&fixture.body));
}

TEST(ModelMeshCollectionTest, ContainsFailsForAbsentMesh)
{
    PopulatedMeshes fixture;
    ModelMesh unrelated(nullptr, "Unrelated", {});
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();
    EXPECT_FALSE(col.Contains(&unrelated));
}

TEST(ModelMeshCollectionTest, SupportsRangeBasedForLoop)
{
    PopulatedMeshes fixture;
    const ModelMeshCollection& col = fixture.model.getMeshesProperty();

    std::vector<ModelMesh*> visited;
    for (ModelMesh* mesh : col)
        visited.push_back(mesh);

    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], &fixture.body);
    EXPECT_EQ(visited[1], &fixture.head);
}
