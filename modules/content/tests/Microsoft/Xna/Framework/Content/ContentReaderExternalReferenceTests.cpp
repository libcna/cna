// SPDX-License-Identifier: MS-PL
//
// XNB-35: unit tests for ContentReader::ReadExternalReference<T>() -- relative-path
// resolution against a real ContentManager/.xnb round trip, the empty-reference default(T) case,
// and the content-root-escape hardening this task adds beyond FNA's own behavior.

#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <algorithm>
#include <any>
#include "CNA/Internal/PathUtf8.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(CNA::TestSupport::FixtureRoot() /
                   ("cna_external_ref_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_ / "textures");
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    // Encodes a reference string the same way ContentReader::ReadString() expects to decode it
    // (.NET 7-bit-length-prefixed UTF8), so the test drives ReadExternalReference<T>() through a
    // raw MemoryStream exactly as a real reader (e.g. a future BasicEffectReader) would.
    std::unique_ptr<System::IO::MemoryStream> MakeReferenceStream(const std::string& reference, std::string& storage)
    {
        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(reference);
        writer.Flush();
        storage = std::string(reinterpret_cast<const char*>(ms.ToArray().data()), ms.ToArray().size());
        return std::make_unique<System::IO::MemoryStream>(
            reinterpret_cast<const uint8_t*>(storage.data()), static_cast<int32_t>(storage.size()));
    }

    class ContentReaderExternalReferenceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };

    struct ExternalPayload
    {
        int32_t value = 0;
    };

    class ExternalPayloadReader final
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<ExternalPayload>
    {
    public:
        ExternalPayloadReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<ExternalPayload>(
                  "CNA.Test.ExternalPayload") {}

    protected:
        ExternalPayload Read(ContentReader& input, std::optional<ExternalPayload>) override
        {
            return ExternalPayload{input.ReadInt32()};
        }
    };

    // An uncompressed XNB (10-byte header, then type table, shared count, root) whose root is an
    // ExternalPayload holding 4321.
    const std::vector<uint8_t>& PayloadXnb()
    {
        static const std::vector<uint8_t> bytes = []
        {
            System::IO::MemoryStream body;
            System::IO::BinaryWriter writer(&body, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("CNA.Test.ExternalPayloadReader"));
            writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(0);
            writer.Write7BitEncodedInt(1);
            writer.Write(int32_t{4321});
            writer.Flush();
            const auto payload = body.ToArray();
            std::vector<uint8_t> file{'X', 'N', 'B', 'w', 5, 0};
            const auto total = static_cast<uint32_t>(payload.size() + 10);
            for (int shift = 0; shift < 32; shift += 8)
                file.push_back(static_cast<uint8_t>(total >> shift));
            file.insert(file.end(), payload.begin(), payload.end());
            return file;
        }();
        return bytes;
    }

    // Serves PayloadXnb() for every name and records the names it was asked to open, so a test
    // can tell a refused reference (nothing opened) from one that merely failed to load.
    class RecordingContentManager final : public ContentManager
    {
    public:
        RecordingContentManager() { RouteLoadsThroughOpenStreamEXT(true); }
        std::vector<std::string> opened;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            opened.push_back(assetName);
            const auto& bytes = PayloadXnb();
            return std::make_unique<System::IO::MemoryStream>(
                bytes.data(), static_cast<int32_t>(bytes.size()), false);
        }
    };

    std::string WithForwardSlashes(std::string name)
    {
        std::replace(name.begin(), name.end(), '\\', '/');
        return name;
    }
}

TEST_F(ContentReaderExternalReferenceTest, ResolvesSiblingPathAndLoadsThroughContentManager)
{
    ScratchContentRoot root;
    std::error_code ec;
    std::filesystem::copy_file(
        "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb",
        root.path() / "textures" / "white-1.xnb", ec);
    ASSERT_FALSE(ec);

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));
    cm.setGraphicsDevice(gd);

    // Simulates a reader whose own asset name is "effects/myeffect" referencing "../textures/white-1"
    // -- resolves to "textures/white-1", a sibling of "effects/", not of the content root itself.
    std::string storage;
    auto stream = MakeReferenceStream("../textures/white-1", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    std::optional<Texture2D> texture = reader.ReadExternalReference<Texture2D>();

    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->getWidthProperty(), 1);
    EXPECT_EQ(texture->getHeightProperty(), 1);
}

TEST_F(ContentReaderExternalReferenceTest, TypeErasedReferenceLoadsLz4CompressedAsset)
{
    ScratchContentRoot root;
    std::error_code ec;
    std::filesystem::copy_file(
        "tests/assets/xnb/monogame/windows/lz4/white-1.xnb",
        root.path() / "textures" / "white-1.xnb", ec);
    ASSERT_FALSE(ec);

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));
    cm.setGraphicsDevice(gd);
    std::string storage;
    auto stream = MakeReferenceStream("../textures/white-1", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    const std::any asset = reader.ReadExternalReference();
    const auto* texture = std::any_cast<Texture2D>(&asset);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidthProperty(), 1);
    EXPECT_EQ(texture->getHeightProperty(), 1);
}

TEST_F(ContentReaderExternalReferenceTest, EmptyReferenceReturnsNullopt)
{
    // XNA ReadExternalReference<T> IL_0007-IL_0018: an empty string is default(T); the manager
    // is never asked for anything.
    RecordingContentManager cm;

    std::string storage;
    auto stream = MakeReferenceStream("", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    std::optional<Texture2D> texture = reader.ReadExternalReference<Texture2D>();

    EXPECT_FALSE(texture.has_value());
    EXPECT_TRUE(cm.opened.empty());
    EXPECT_EQ(stream->getPositionProperty(), 1); // the zero length prefix, nothing more
}

TEST_F(ContentReaderExternalReferenceTest, PathEscapingContentRootThrowsContentLoadException)
{
    // TQ-004: a manager that would serve ANY name, so the refusal cannot be a missing file.
    RecordingContentManager cm;

    std::string storage;
    auto stream = MakeReferenceStream("../../outside", storage);
    // Top-level asset name (no directory component) -- ".." from here has nowhere real to go.
    ContentReader reader(&cm, stream.get(), "white-1", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
    EXPECT_TRUE(cm.opened.empty());
}

// This method's own doc comment already promised an absolute reference is
// "rejected outright," but only the "..''-escaping case was actually enforced -- an absolute
// reference passed straight through unchanged, since fs::path::operator/ silently discards the
// base directory for an absolute right-hand operand. Neither BuildAssetPath() nor anything else
// downstream re-contained it either.
TEST_F(ContentReaderExternalReferenceTest, AbsolutePathReferenceThrowsContentLoadException)
{
    // TQ-004: as above, the refusal must come before any open, not from a missing file.
    RecordingContentManager cm;

    std::string storage;
    auto stream = MakeReferenceStream("/etc/passwd", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
    EXPECT_TRUE(cm.opened.empty());
}

TEST_F(ContentReaderExternalReferenceTest, NoOwningContentManagerThrowsContentLoadException)
{
    std::string storage;
    auto stream = MakeReferenceStream("textures/white-1", storage);
    ContentReader reader(nullptr, stream.get(), "effects/myeffect", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
}

// TQ-004: XNA ReadExternalReference<T> loads GetCleanPath(GetPathToReference(reference))
// (IL_001b-IL_002f). GetPathToReference combines the reference with the directory part of the
// reader's own asset name (everything before its last '\' or '/'); GetCleanPath then folds "." and
// "x/.." segments. Expected names are those segments, compared separator-agnostically.
TEST_F(ContentReaderExternalReferenceTest, ReferenceNamesResolveAgainstTheReadersOwnDirectoryLikeXna)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.ExternalPayloadReader", [] { return std::make_unique<ExternalPayloadReader>(); });
    struct Case { const char* assetName; const char* reference; const char* expected; };
    for (const Case& c : {
             Case{"effects/myeffect", "../textures/white-1", "textures/white-1"},
             Case{"effects/myeffect", "..\\textures\\white-1", "textures/white-1"},
             Case{"effects/myeffect", "white-1", "effects/white-1"},
             Case{"effects/myeffect", "./sub/../white-1", "effects/white-1"},
             Case{"effects\\nested\\myeffect", "../white-1", "effects/white-1"},
             Case{"myeffect", "textures/white-1", "textures/white-1"}})
    {
        SCOPED_TRACE(std::string(c.assetName) + " + " + c.reference);
        for (const bool typeErased : {false, true})
        {
            RecordingContentManager cm;
            std::string storage;
            auto stream = MakeReferenceStream(c.reference, storage);
            ContentReader reader(&cm, stream.get(), c.assetName, 5, 'w');

            int32_t value = 0;
            if (typeErased)
            {
                const std::any asset = reader.ReadExternalReference();
                const auto* payload = std::any_cast<ExternalPayload>(&asset);
                ASSERT_NE(payload, nullptr);
                value = payload->value;
            }
            else
            {
                const auto payload = reader.ReadExternalReference<ExternalPayload>();
                ASSERT_TRUE(payload.has_value());
                value = payload->value;
            }
            EXPECT_EQ(value, 4321);
            ASSERT_EQ(cm.opened.size(), 1u);
            EXPECT_EQ(WithForwardSlashes(cm.opened.front()), c.expected);
        }
    }
}

// TQ-004: the hardening the header documents (no FNA/XNA equivalent): a rooted, drive-lettered or
// content-root-escaping reference is refused outright -- before the manager opens anything, which
// a manager that serves every name makes observable.
TEST_F(ContentReaderExternalReferenceTest, EscapingReferencesAreRefusedBeforeAnythingIsOpened)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.ExternalPayloadReader", [] { return std::make_unique<ExternalPayloadReader>(); });
    struct Case { const char* assetName; const char* reference; };
    for (const Case& c : {
             Case{"white-1", "../outside"}, Case{"white-1", "..\\..\\outside"},
             Case{"white-1", "a/../../outside"}, Case{"effects/myeffect", "../../outside"},
             Case{"effects/myeffect", "/etc/passwd"}, Case{"effects/myeffect", "\\\\server\\share\\x"},
             Case{"effects/myeffect", "C:/Windows/x"}, Case{"effects/myeffect", "c:x"}})
    {
        SCOPED_TRACE(std::string(c.assetName) + " + " + c.reference);
        for (const bool typeErased : {false, true})
        {
            RecordingContentManager cm;
            std::string storage;
            auto stream = MakeReferenceStream(c.reference, storage);
            ContentReader reader(&cm, stream.get(), c.assetName, 5, 'w');
            if (typeErased)
                EXPECT_THROW((void)reader.ReadExternalReference(), ContentLoadException);
            else
                EXPECT_THROW((void)reader.ReadExternalReference<ExternalPayload>(), ContentLoadException);
            EXPECT_TRUE(cm.opened.empty());
        }
    }
}
