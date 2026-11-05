// SPDX-License-Identifier: MS-PL
#include "PictureContractFixture.hpp"
#include <typeinfo>
#include "System/AppContext.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/IOException.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::Tq086PictureTest;

namespace
{
    // A legal short-reading source with a caller-controlled initial cursor/advertised length.
    // No seeking is supplied: SavePicture must consume the caller's current position.
    class PictureSource final : public System::IO::Stream
    {
    public:
        std::vector<uint8_t> bytes;
        std::size_t position = 0;
        int advertisedLength = 0;
        int calls = 0;
        bool closed = false;
        bool failSecondRead = false;

        explicit PictureSource(std::vector<uint8_t> input) : bytes(std::move(input)),
            advertisedLength(static_cast<int>(bytes.size())) {}

        SharpRuntime::intcs Read(uint8_t buffer[], SharpRuntime::intcs offset,
                                 SharpRuntime::intcs count) override
        {
            ++calls;
            if (failSecondRead && calls == 2) throw System::IO::IOException("source read failed");
            const auto n = std::min<std::size_t>({bytes.size() - position,
                static_cast<std::size_t>(count), 7});
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(position), n, buffer + offset);
            position += n;
            return static_cast<SharpRuntime::intcs>(n);
        }
        void Close() override { closed = true; }
        SharpRuntime::intcs getLengthProperty() const override { return advertisedLength; }
    };

    struct FrameworkMessages
    {
        bool previous = false;
        FrameworkMessages()
        {
            System::AppContext::TryGetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", previous);
            System::AppContext::SetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", true);
        }
        ~FrameworkMessages()
        {
            System::AppContext::SetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", previous);
        }
    };
}

TEST_F(Tq086PictureTest, SavedCollectionsKeepIdentityAndAppendEveryPictureInOrder)
{
    Load();
    auto* all = library->getPicturesProperty();
    auto* saved = library->getSavedPicturesProperty();
    auto* rootAlbum = library->getRootPictureAlbumProperty();
    ASSERT_NE(rootAlbum, nullptr);
    auto* children = rootAlbum->getAlbumsProperty();
    EXPECT_EQ(all->getCountProperty(), 0);
    EXPECT_EQ(saved->getCountProperty(), 0);
    EXPECT_EQ(children->getCountProperty(), 0);
    EXPECT_FALSE(std::filesystem::exists(pictures / "Saved Pictures"));
    const auto bytes = Bmp();
    auto* first = library->SavePicture("z-last-alphabetically", bytes);
    ASSERT_NE(first, nullptr);
    auto* album = first->getAlbumProperty();
    ASSERT_NE(album, nullptr);
    auto* inAlbum = album->getPicturesProperty();
    const auto before = std::chrono::system_clock::now();
    auto* second = library->SavePicture("a-first-alphabetically", bytes);
    const auto after = std::chrono::system_clock::now();
    ASSERT_NE(second, nullptr);
    // CNA feature contract: existing read-only views grow in call order, including the album.
    EXPECT_EQ(library->getPicturesProperty(), all);
    EXPECT_EQ(library->getSavedPicturesProperty(), saved);
    EXPECT_EQ(rootAlbum->getAlbumsProperty(), children);
    EXPECT_EQ(children->getCountProperty(), 1);
    EXPECT_EQ((*children)[0], album);
    EXPECT_EQ(album->getNameProperty(), "Saved Pictures");
    EXPECT_EQ(album->getParentProperty(), rootAlbum);
    EXPECT_EQ(second->getAlbumProperty(), album);
    EXPECT_EQ(second->getNameProperty(), "a-first-alphabetically");
    EXPECT_GE(second->getDateProperty(), before);
    EXPECT_LE(second->getDateProperty(), after);
    for (auto* collection : {all, saved, inAlbum})
    {
        ASSERT_EQ(collection->getCountProperty(), 2);
        EXPECT_EQ((*collection)[0], first);
        EXPECT_EQ((*collection)[1], second);
        EXPECT_EQ((std::vector<Picture*>(collection->begin(), collection->end())),
                  (std::vector<Picture*>{first, second}));
    }
    EXPECT_EQ(ReadOwned(first->GetImage()), bytes);
    EXPECT_EQ(ReadOwned(second->GetImage()), bytes);
    EXPECT_EQ(rootAlbum->getPicturesProperty()->getCountProperty(), 0);
    EXPECT_EQ(library->GetPictureFromToken(second->getTokenEXT()), second);
    MediaLibrary reopened;
    // On rescan the filesystem's alphabetical order applies, rather than former append order.
    EXPECT_EQ(Names(*reopened.getSavedPicturesProperty()),
              (std::vector<std::string>{"a-first-alphabetically", "z-last-alphabetically"}));
}

TEST_F(Tq086PictureTest, PreexistingSavedAlbumIsReusedWithoutDuplicatingOrRecursingMembership)
{
    Write(pictures / "Saved Pictures" / "existing.bmp", Bmp());
    Write(pictures / "Saved Pictures" / "Nested" / "nested.bmp", Bmp());
    Load();
    auto* rootAlbum = library->getRootPictureAlbumProperty();
    ASSERT_NE(rootAlbum, nullptr);
    ASSERT_EQ(rootAlbum->getAlbumsProperty()->getCountProperty(), 1);
    auto* album = (*rootAlbum->getAlbumsProperty())[0];
    auto* saved = library->getSavedPicturesProperty();
    ASSERT_EQ(saved->getCountProperty(), 1); // Direct members only; nested belongs to Nested.
    auto* existing = (*saved)[0];
    EXPECT_EQ(existing->getNameProperty(), "existing");
    EXPECT_EQ(existing->getAlbumProperty(), album);
    auto* added = library->SavePicture("added", Bmp());
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->getAlbumProperty(), album);
    EXPECT_EQ(rootAlbum->getAlbumsProperty()->getCountProperty(), 1);
    ASSERT_EQ(saved->getCountProperty(), 2);
    EXPECT_EQ((*saved)[0], existing);
    EXPECT_EQ((*saved)[1], added);
    EXPECT_EQ(album->getPicturesProperty()->getCountProperty(), 2);
    EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 3);
}

TEST_F(Tq086PictureTest, StreamSaveStartsAtCurrentCursorAndLeavesSourceOpen)
{
    const auto expected = Bmp();
    std::vector<uint8_t> input{0xAA, 0xBB, 0xCC};
    input.insert(input.end(), expected.begin(), expected.end());
    PictureSource source(input);
    source.position = 3;
    Load();
    auto* picture = library->SavePicture("cursor", &source);
    ASSERT_NE(picture, nullptr);
    // Stream.Read contract: total Length is not remaining length. Bytes before the current
    // position must not be read again, and the short EOF must not cause zero padding.
    EXPECT_EQ(ReadOwned(picture->GetImage()), expected);
    EXPECT_EQ(source.position, input.size());
    EXPECT_FALSE(source.closed);
    uint8_t byte{};
    EXPECT_EQ(source.Read(&byte, 0, 1), 0);
}

TEST_F(Tq086PictureTest, EarlyEofSavesOnlyActualBytesAndDoesNotCloseSource)
{
    const auto expected = Bmp();
    PictureSource source(expected);
    source.advertisedLength += 20;
    Load();
    auto* picture = library->SavePicture("short", &source);
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(ReadOwned(picture->GetImage()), expected);
    // 78 bytes in chunks <=7: 12 nonempty reads plus the EOF read.
    EXPECT_EQ(source.calls, 13);
    EXPECT_FALSE(source.closed);
    EXPECT_EQ(picture->getWidthProperty(), 3);
    EXPECT_EQ(picture->getHeightProperty(), 2);
}

TEST_F(Tq086PictureTest, SourceReadFailurePropagatesWithoutPublishingOrClosing)
{
    PictureSource source(Bmp());
    source.failSecondRead = true;
    Load();
    try { (void)library->SavePicture("broken-source", &source); FAIL() << "Expected IOException"; }
    catch (const System::IO::IOException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::IO::IOException));
        EXPECT_EQ(error.getMessageProperty(), "source read failed");
    }
    EXPECT_FALSE(source.closed);
    EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 0);
    EXPECT_EQ(library->getSavedPicturesProperty()->getCountProperty(), 0);
    EXPECT_FALSE(std::filesystem::exists(pictures / "Saved Pictures"));
}

TEST_F(Tq086PictureTest, NullSourceHasExactCnaDiagnosticWithoutFilesystemSideEffects)
{
    FrameworkMessages messages;
    Load();
    // CNA's established argument refusal + SharpRuntime's Framework diagnostic contract.
    // Refresh SavePicture(Stream) IL_001e..0023 always throws NotSupportedException instead.
    try { (void)library->SavePicture("", static_cast<System::IO::Stream*>(nullptr)); FAIL(); }
    catch (const System::ArgumentNullException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::ArgumentNullException));
        EXPECT_EQ(error.getParamNameProperty(), "source");
        EXPECT_EQ(error.getMessageProperty(), "Value cannot be null.\nParameter name: source");
    }
    EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 0);
    EXPECT_EQ(library->getSavedPicturesProperty()->getCountProperty(), 0);
    EXPECT_FALSE(std::filesystem::exists(pictures / "Saved Pictures"));
}

TEST_F(Tq086PictureTest, DestinationFailureHasExactDiagnosticForBothOverloads)
{
    // A regular file cannot be a parent directory: deterministic failure even as root.
    const std::vector<uint8_t> marker{0x53};
    Write(pictures / "Saved Pictures", marker);
    Load();
    const auto bytes = Bmp();
    PictureSource source(bytes);
    auto check = [&](auto operation)
    {
        try { operation(); FAIL() << "Expected IOException"; }
        catch (const System::IO::IOException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::IO::IOException));
            EXPECT_EQ(error.getMessageProperty(), "Failed to save picture 'blocked'.");
        }
        EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 0);
        EXPECT_EQ(library->getSavedPicturesProperty()->getCountProperty(), 0);
        EXPECT_EQ(library->getRootPictureAlbumProperty()->getAlbumsProperty()->getCountProperty(), 0);
        EXPECT_EQ(FileBytes(pictures / "Saved Pictures"), marker);
    };
    // CNA storage feature contract; no Windows XNA writable destination exists in this profile.
    check([&] { (void)library->SavePicture("blocked", bytes); });
    check([&] { (void)library->SavePicture("blocked", &source); });
    EXPECT_FALSE(source.closed);
}

TEST_F(Tq086PictureTest, EmptyNameAndBufferFollowDocumentedCnaFallbackPolicy)
{
    Load();
    // Explicit CNA feature policy, NOT inferred XNA validation. Empty name uses a safe filename;
    // opaque/empty bytes are stored unchanged, with dimensions 0 when no image can be decoded.
    auto* picture = library->SavePicture("", std::vector<uint8_t>{});
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->getNameProperty(), "");
    EXPECT_EQ(picture->ToString(), "");
    EXPECT_EQ(picture->getWidthProperty(), 0);
    EXPECT_EQ(picture->getHeightProperty(), 0);
    EXPECT_TRUE(std::filesystem::is_regular_file(pictures / "Saved Pictures" / "picture.png"));
    EXPECT_EQ(std::filesystem::file_size(pictures / "Saved Pictures" / "picture.png"), 0u);
    EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 1);
    EXPECT_EQ(library->getSavedPicturesProperty()->getCountProperty(), 1);
    EXPECT_EQ(ReadOwned(picture->GetImage()), (std::vector<uint8_t>{}));
}
