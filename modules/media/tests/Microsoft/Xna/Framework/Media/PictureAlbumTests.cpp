// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

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

// plans/plan_media.md MEDIA-67: real tree node, incl. the root's self-referencing Parent==nullptr case.
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

// plans/plan_media.md MEDIA-67: walking Parent from any leaf reaches the root in exactly as many steps
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

// plans/plan_media.md MEDIA-234: cover the System.Object override separately from
// Equals(PictureAlbum).
TEST_F(MediaLibraryTestFixture, PictureAlbumObjectEqualsChecksRuntimeTypeAndValue)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    PictureAlbum* vacation = FindChildAlbum(root->getAlbumsProperty(), "Vacation");
    PictureAlbum* family = FindChildAlbum(root->getAlbumsProperty(), "Family");
    ASSERT_NE(vacation, nullptr);
    ASSERT_NE(family, nullptr);

    const System::Object* equalAlbum = vacation;
    const System::Object* unequalAlbum = family;
    const System::Object* wrongType = library.get();
    const System::Object* nullObject = nullptr;
    EXPECT_TRUE(vacation->Equals(equalAlbum));
    EXPECT_FALSE(vacation->Equals(unequalAlbum));
    EXPECT_FALSE(vacation->Equals(wrongType));
    EXPECT_FALSE(vacation->Equals(nullObject));
}

TEST_F(MediaLibraryTestFixture, PictureAlbumGetTypeNameIsFullyQualified)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->GetTypeName(), "Microsoft.Xna.Framework.Media.PictureAlbum");
}

// plans/plan_media.md MEDIA-106: IsDisposed, not exercised anywhere else in this file.
TEST_F(MediaLibraryTestFixture, PictureAlbumDisposeFlipsIsDisposed)
{
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    PictureAlbum* vacation = FindChildAlbum(root->getAlbumsProperty(), "Vacation");
    ASSERT_NE(vacation, nullptr);
    ASSERT_FALSE(vacation->getIsDisposedProperty());

    vacation->Dispose();

    EXPECT_TRUE(vacation->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-107: PictureAlbumCollection's own explicit out-of-range indexer test and
// Dispose()/IsDisposed -- neither was exercised anywhere else in this file (only in-bounds access
// via FindChildAlbum, and only implicitly, never Dispose()).
TEST_F(MediaLibraryTestFixture, PictureAlbumCollectionIndexerThrowsOutOfRange)
{
    auto* albums = library->getRootPictureAlbumProperty()->getAlbumsProperty();
    EXPECT_THROW((void)(*albums)[-1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)(*albums)[static_cast<SharpRuntime::intcs>(albums->getCountProperty())],
                 System::ArgumentOutOfRangeException);
}

TEST_F(MediaLibraryTestFixture, PictureAlbumCollectionDisposeFlipsIsDisposed)
{
    auto* albums = library->getRootPictureAlbumProperty()->getAlbumsProperty();
    ASSERT_FALSE(albums->getIsDisposedProperty());

    albums->Dispose();

    EXPECT_TRUE(albums->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-121 (found by external code review): PictureAlbumCollection's own
// GetTypeName().
TEST_F(MediaLibraryTestFixture, PictureAlbumCollectionGetTypeNameIsFullyQualified)
{
    auto* albums = library->getRootPictureAlbumProperty()->getAlbumsProperty();
    EXPECT_EQ(albums->GetTypeName(), "Microsoft.Xna.Framework.Media.PictureAlbumCollection");
}
