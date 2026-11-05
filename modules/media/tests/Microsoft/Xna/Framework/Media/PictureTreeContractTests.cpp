// SPDX-License-Identifier: MS-PL
#include "PictureContractFixture.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::Tq086PictureTest;

TEST_F(Tq086PictureTest, ConstEnumerationAndIndexerKeepTheLiteralTreeOrder)
{
    // CNA's deterministic filesystem mapping: sorted child albums, child pictures before
    // this album's own pictures. Expectations follow this literal tree, not a CNA dump.
    Write(pictures / "B" / "b.bmp", Bmp());
    Write(pictures / "z.bmp", Bmp());
    Write(pictures / "A" / "a.bmp", Bmp());
    Load();
    auto* rootAlbum = library->getRootPictureAlbumProperty();
    ASSERT_NE(rootAlbum, nullptr);
    const auto& albums = *rootAlbum->getAlbumsProperty();
    EXPECT_EQ(Names(albums), (std::vector<std::string>{"A", "B"}));
    ASSERT_EQ(albums.getCountProperty(), 2);
    EXPECT_EQ(albums[0]->getNameProperty(), "A");
    EXPECT_EQ(albums[1]->getNameProperty(), "B");
    const auto& all = *library->getPicturesProperty();
    EXPECT_EQ(Names(all), (std::vector<std::string>{"a", "b", "z"}));
    ASSERT_EQ(all.getCountProperty(), 3);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(all[i]->getNameProperty(), (std::vector<std::string>{"a", "b", "z"})[i]);
    ASSERT_EQ(rootAlbum->getPicturesProperty()->getCountProperty(), 1);
    EXPECT_EQ((*rootAlbum->getPicturesProperty())[0], all[2]);
    for (int i = 0; i < 2; ++i)
    {
        EXPECT_EQ(albums[i]->getParentProperty(), rootAlbum);
        ASSERT_EQ(albums[i]->getPicturesProperty()->getCountProperty(), 1);
        EXPECT_EQ((*albums[i]->getPicturesProperty())[0], all[i]);
        EXPECT_EQ(all[i]->getAlbumProperty(), albums[i]);
    }
}

TEST_F(Tq086PictureTest, SameNamedPicturesAndAlbumsHaveDifferentIdentityAndEqualNameHashes)
{
    Write(pictures / "A" / "Twin" / "same.bmp", Bmp());
    Write(pictures / "B" / "Twin" / "same.bmp", Bmp(4, 1));
    Load();
    auto* all = library->getPicturesProperty();
    ASSERT_EQ(all->getCountProperty(), 2);
    auto* first = (*all)[0]; auto* second = (*all)[1];
    // CNA catalog identity is path-based; Refresh's native MediaItem_IsEqual key is opaque.
    // Distinct objects with the same display name must not collapse into one identity.
    EXPECT_FALSE(first->Equals(second));
    EXPECT_FALSE(first->Equals(static_cast<const System::Object*>(second)));
    EXPECT_FALSE(*first == *second); EXPECT_TRUE(*first != *second);
    auto* a = first->getAlbumProperty(); auto* b = second->getAlbumProperty();
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
    EXPECT_FALSE(a->Equals(b));
    EXPECT_FALSE(a->Equals(static_cast<const System::Object*>(b)));
    EXPECT_FALSE(*a == *b); EXPECT_TRUE(*a != *b);
    EXPECT_EQ(first->ToString(), "same"); EXPECT_EQ(second->ToString(), "same");
    EXPECT_EQ(a->ToString(), "Twin"); EXPECT_EQ(b->ToString(), "Twin");
    // Refresh Picture/PictureAlbum.GetHashCode IL_000f..001b hashes Name only.
    // Compare collisions, without requiring any platform's numerical string-hash bits.
    EXPECT_EQ(first->GetHashCode(), second->GetHashCode());
    EXPECT_EQ(a->GetHashCode(), b->GetHashCode());
}

TEST_F(Tq086PictureTest, EmptyViewsAreIndependentAndMissingRootStaysNull)
{
    Load();
    MediaLibrary peer;
    auto* all = library->getPicturesProperty();
    auto* saved = library->getSavedPicturesProperty();
    auto* rootAlbum = library->getRootPictureAlbumProperty();
    ASSERT_NE(rootAlbum, nullptr);
    EXPECT_EQ(rootAlbum->getNameProperty(), "Pictures");
    EXPECT_EQ(rootAlbum->getParentProperty(), nullptr);
    ASSERT_NE(all, saved);
    ASSERT_NE(all, peer.getPicturesProperty());
    EXPECT_EQ(all->getCountProperty(), 0);
    EXPECT_EQ(saved->getCountProperty(), 0);
    EXPECT_EQ(all->begin(), all->end());
    EXPECT_EQ(rootAlbum->getAlbumsProperty()->begin(), rootAlbum->getAlbumsProperty()->end());
    // Deliberate CNA divergence from Refresh MediaLibrary::.ctor(source) IL_0037..0053:
    // per-library views are independent; disposing one empty view cannot poison its neighbors.
    all->Dispose();
    EXPECT_FALSE(saved->getIsDisposedProperty());
    EXPECT_FALSE(peer.getPicturesProperty()->getIsDisposedProperty());
    EXPECT_EQ(saved->getCountProperty(), 0);
    EXPECT_EQ(peer.getPicturesProperty()->getCountProperty(), 0);
    CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride((root / "absent").string());
    MediaLibrary missing;
    EXPECT_EQ(missing.getRootPictureAlbumProperty(), nullptr);
    EXPECT_EQ(missing.getPicturesProperty()->getCountProperty(), 0);
    EXPECT_EQ(missing.getSavedPicturesProperty()->getCountProperty(), 0);
}
