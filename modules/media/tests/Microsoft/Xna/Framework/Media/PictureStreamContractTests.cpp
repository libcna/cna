// SPDX-License-Identifier: MS-PL
#include "PictureContractFixture.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "System/IO/FileNotFoundException.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::Tq086PictureTest;

TEST_F(Tq086PictureTest, MetadataUsesBmpDimensionsNameAndControlledFileDate)
{
    Write(pictures / "dated.bmp", Bmp());
    // CNA filesystem mapping, not XNA capture-date parity. A known historical mtime catches
    // a hardcoded nonzero date or now(). Allow one second for clock conversion / FS granularity.
    const auto expected = std::chrono::system_clock::time_point{std::chrono::seconds{946684800}};
    const auto fileTime = std::filesystem::file_time_type::clock::now()
        + std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
            expected - std::chrono::system_clock::now());
    std::filesystem::last_write_time(pictures / "dated.bmp", fileTime);
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    const auto* picture = (*library->getPicturesProperty())[0];
    EXPECT_EQ(picture->getNameProperty(), "dated");
    EXPECT_EQ(picture->getWidthProperty(), 3);
    EXPECT_EQ(picture->getHeightProperty(), 2);
    EXPECT_GE(picture->getDateProperty(), expected - std::chrono::seconds{1});
    EXPECT_LE(picture->getDateProperty(), expected + std::chrono::seconds{1});
    EXPECT_EQ(picture->getAlbumProperty(), library->getRootPictureAlbumProperty());
}

TEST_F(Tq086PictureTest, ImageAndSmallThumbnailCallsHaveIndependentOwnedStreams)
{
    const auto expected = Bmp(); // 54 + ceil(3*3/4)*4*2 = 78 bytes, literal BMP oracle.
    Write(pictures / "small.bmp", expected);
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    auto* picture = (*library->getPicturesProperty())[0];
    // Public CNA ownership contract: every returned stream is caller-owned. No Refresh
    // byte-stream parity is claimed (Picture.GetImageStream IL_0000..0003 returns null).
    for (bool thumbnail : {false, true})
    {
        SCOPED_TRACE(thumbnail);
        auto get = [&] { return thumbnail ? picture->GetThumbnail() : picture->GetImage(); };
        std::unique_ptr<System::IO::Stream> first(get()), second(get());
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(first.get(), second.get());
        EXPECT_EQ(first->getLengthProperty(), 78);
        uint8_t header[2]{};
        ASSERT_EQ(first->Read(header, 0, 2), 2);
        EXPECT_EQ(header[0], 'B'); EXPECT_EQ(header[1], 'M');
        EXPECT_EQ(Read(second.get()), expected);
        EXPECT_EQ(Read(first.get()), (std::vector<uint8_t>(expected.begin() + 2, expected.end())));
        first->Close();
        first.reset();
        EXPECT_EQ(ReadOwned(get()), expected);
    }
}

TEST_F(Tq086PictureTest, OpenStreamsAndPictureViewsSurviveLibraryDispose)
{
    const auto expected = Bmp();
    Write(pictures / "small.bmp", expected);
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    auto* picture = (*library->getPicturesProperty())[0];
    auto* album = picture->getAlbumProperty();
    std::unique_ptr<System::IO::Stream> image(picture->GetImage()), thumbnail(picture->GetThumbnail());
    library->Dispose();
    // Refresh MediaLibrary.Dispose(bool) IL_0000..000f changes only its own flag.
    EXPECT_FALSE(picture->getIsDisposedProperty());
    EXPECT_FALSE(album->getIsDisposedProperty());
    EXPECT_EQ(ReadOwned(picture->GetImage()), expected);
    EXPECT_EQ(ReadOwned(picture->GetThumbnail()), expected);
    picture->Dispose();
    // CNA's streams own their data/file handles, not their Picture or MediaLibrary.
    library.reset();
    EXPECT_EQ(Read(image.get()), expected);
    EXPECT_EQ(Read(thumbnail.get()), expected);
}

TEST_F(Tq086PictureTest, OversizedPictureThumbnailUsesPngAndPreservesPixels)
{
    const auto expected = Bmp(256, 128);
    Write(pictures / "large.bmp", expected);
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    auto* picture = (*library->getPicturesProperty())[0];
    EXPECT_EQ(picture->getWidthProperty(), 256);
    EXPECT_EQ(picture->getHeightProperty(), 128);
    EXPECT_EQ(ReadOwned(picture->GetImage()), expected);
    std::unique_ptr<System::IO::Stream> first(picture->GetThumbnail()), second(picture->GetThumbnail());
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    ASSERT_NE(first.get(), second.get());
    uint8_t signature[8]{};
    ASSERT_EQ(first->Read(signature, 0, 8), 8);
    // PNG signature is a format literal, independent of CNA's encoder.
    EXPECT_EQ((std::vector<uint8_t>(signature, signature + 8)),
              (std::vector<uint8_t>{137, 80, 78, 71, 13, 10, 26, 10}));
    const auto png = Read(second.get());
    ASSERT_GE(png.size(), 24u);
    // IHDR starts at byte 12; width/height are big endian at 16/20.
    EXPECT_EQ((std::vector<uint8_t>(png.begin() + 12, png.begin() + 24)),
              (std::vector<uint8_t>{'I', 'H', 'D', 'R', 0, 0, 0, 128, 0, 0, 0, 64}));
    // CNA thumbnail policy: scale 128/256=1/2, so 256x128 -> 128x64, no tolerance.
    // Decoding is only transport; expected RGB is the literal BMP's solid colour.
    const auto decoded = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(png.data(), png.size());
    ASSERT_EQ(decoded.pixels.size(), 128u * 64u * 4u);
    for (std::size_t i = 0; i < decoded.pixels.size(); i += 4)
    {
        ASSERT_EQ(decoded.pixels[i], 10);
        ASSERT_EQ(decoded.pixels[i + 1], 20);
        ASSERT_EQ(decoded.pixels[i + 2], 30);
        ASSERT_EQ(decoded.pixels[i + 3], 255);
    }
    first.reset(); // Releasing one generated stream must leave the Picture reusable.
    std::unique_ptr<System::IO::Stream> survivor(picture->GetThumbnail());
    library.reset();
    const auto again = Read(survivor.get());
    ASSERT_GE(again.size(), 24u);
    EXPECT_EQ((std::vector<uint8_t>(again.begin() + 16, again.begin() + 24)),
              (std::vector<uint8_t>{0, 0, 0, 128, 0, 0, 0, 64}));
}

TEST_F(Tq086PictureTest, RemovedImageReportsFileFailureForBothStreamRoutes)
{
    Write(pictures / "removed.bmp", Bmp());
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    auto* picture = (*library->getPicturesProperty())[0];
    std::filesystem::remove(pictures / "removed.bmp");
    // FileStream's documented missing-file contract; do not accept a null success result.
    EXPECT_THROW({ auto owner = std::unique_ptr<System::IO::Stream>(picture->GetImage()); },
                 System::IO::FileNotFoundException);
    EXPECT_THROW({ auto owner = std::unique_ptr<System::IO::Stream>(picture->GetThumbnail()); },
                 System::IO::FileNotFoundException);
}

TEST_F(Tq086PictureTest, ReadOnlyPicturesReturnReadOnlyReadableStreams)
{
    const auto expected = Bmp();
    Write(pictures / "readonly.bmp", expected);
    struct RestoreWritePermission
    {
        std::filesystem::path path;
        ~RestoreWritePermission()
        {
            std::error_code ignored;
            std::filesystem::permissions(path, std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::add, ignored);
        }
    } restore{pictures / "readonly.bmp"};
    std::filesystem::permissions(pictures / "readonly.bmp",
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
        std::filesystem::perms::others_read, std::filesystem::perm_options::replace);
    Load();
    ASSERT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    auto* picture = (*library->getPicturesProperty())[0];
    // CNA filesystem reader contract: browsing an existing picture needs read permission only.
    // Explicit read access also permits concurrent opens under Windows FileShare.Read.
    for (bool thumbnail : {false, true})
    {
        SCOPED_TRACE(thumbnail);
        std::unique_ptr<System::IO::Stream> stream(thumbnail ? picture->GetThumbnail() : picture->GetImage());
        ASSERT_NE(stream, nullptr);
        EXPECT_TRUE(stream->getCanReadProperty());
        EXPECT_FALSE(stream->getCanWriteProperty());
        EXPECT_EQ(Read(stream.get()), expected);
    }
}
