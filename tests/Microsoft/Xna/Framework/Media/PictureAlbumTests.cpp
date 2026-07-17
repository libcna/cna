// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"

using Microsoft::Xna::Framework::Media::PictureAlbum;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    PictureAlbum* FindChildAlbum(Microsoft::Xna::Framework::Media::PictureAlbumCollection* albums,
                                  const std::string& name)
    {
        for (PictureAlbum* a : *albums)
        {
            if (a->getNameProperty() == name) return a;
        }
        return nullptr;
    }
}

// plan_media.md MEDIA-67: real tree node, incl. the root's self-referencing Parent==nullptr case.
TEST_F(MediaLibraryTestFixture, RootPictureAlbumParentIsNull)
{
    ASSERT_NE(library->getRootPictureAlbumProperty(), nullptr);
    EXPECT_EQ(library->getRootPictureAlbumProperty()->getParentProperty(), nullptr);
}

TEST_F(MediaLibraryTestFixture, RootHasTwoChildAlbums)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getAlbumsProperty()->getCountProperty(), 2); // Vacation, Family
}

// plan_media.md MEDIA-67: walking Parent from any leaf reaches the root in exactly as many steps
// as the fixture's real directory depth -- Vacation/Day 2 is 2 levels deep.
TEST_F(MediaLibraryTestFixture, MultiLevelParentWalkReachesRootInExpectedSteps)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    PictureAlbum* vacation = FindChildAlbum(root->getAlbumsProperty(), "Vacation");
    ASSERT_NE(vacation, nullptr);
    ASSERT_EQ(vacation->getAlbumsProperty()->getCountProperty(), 1);
    PictureAlbum* day2 = (*vacation->getAlbumsProperty())[0];
    ASSERT_NE(day2, nullptr);
    EXPECT_EQ(day2->getNameProperty(), "Day 2");

    int steps = 0;
    PictureAlbum* walker = day2;
    while (walker->getParentProperty() != nullptr)
    {
        walker = walker->getParentProperty();
        ++steps;
    }
    EXPECT_EQ(steps, 2); // Day 2 -> Vacation -> root
    EXPECT_EQ(walker, root);
}

TEST_F(MediaLibraryTestFixture, VacationAlbumHasOnePictureAndFamilyHasOne)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    PictureAlbum* vacation = FindChildAlbum(root->getAlbumsProperty(), "Vacation");
    PictureAlbum* family = FindChildAlbum(root->getAlbumsProperty(), "Family");
    ASSERT_NE(vacation, nullptr);
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(vacation->getPicturesProperty()->getCountProperty(), 1); // beach.jpg
    EXPECT_EQ(family->getPicturesProperty()->getCountProperty(), 1);   // portrait.png
}

TEST_F(MediaLibraryTestFixture, PictureAlbumEqualitySetForEqualAndUnequalAlbums)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    PictureAlbum* vacation = FindChildAlbum(root->getAlbumsProperty(), "Vacation");
    PictureAlbum* family = FindChildAlbum(root->getAlbumsProperty(), "Family");
    ASSERT_NE(vacation, nullptr);
    ASSERT_NE(family, nullptr);

    EXPECT_TRUE(vacation->Equals(vacation));
    EXPECT_FALSE(vacation->Equals(family));
    EXPECT_TRUE(*vacation == *vacation);
    EXPECT_TRUE(*vacation != *family);
}

TEST_F(MediaLibraryTestFixture, PictureAlbumGetTypeNameIsFullyQualified)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->GetTypeName(), "Microsoft.Xna.Framework.Media.PictureAlbum");
}
