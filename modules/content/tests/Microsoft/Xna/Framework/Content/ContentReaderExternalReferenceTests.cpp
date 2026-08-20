// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-35: unit tests for ContentReader::ReadExternalReference<T>() -- relative-path
// resolution against a real ContentManager/.xnb round trip, the empty-reference default(T) case,
// and the content-root-escape hardening this task adds beyond FNA's own behavior.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

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
            : dir_(std::filesystem::temp_directory_path() /
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
}

TEST_F(ContentReaderExternalReferenceTest, ResolvesSiblingPathAndLoadsThroughContentManager)
{
    ScratchContentRoot root;
    std::error_code ec;
    std::filesystem::copy_file(
        "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb",
        root.path() / "textures" / "white-1.xnb", ec);
    ASSERT_FALSE(ec);

    ContentManager cm(nullptr, root.path().string());
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

TEST_F(ContentReaderExternalReferenceTest, EmptyReferenceReturnsNullopt)
{
    ContentManager cm; // never dereferenced: the empty-reference path returns before touching it

    std::string storage;
    auto stream = MakeReferenceStream("", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    std::optional<Texture2D> texture = reader.ReadExternalReference<Texture2D>();

    EXPECT_FALSE(texture.has_value());
}

TEST_F(ContentReaderExternalReferenceTest, PathEscapingContentRootThrowsContentLoadException)
{
    ContentManager cm;

    std::string storage;
    auto stream = MakeReferenceStream("../../outside", storage);
    // Top-level asset name (no directory component) -- ".." from here has nowhere real to go.
    ContentReader reader(&cm, stream.get(), "white-1", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
}

// REMED-CONTENT-002: this method's own doc comment already promised an absolute reference is
// "rejected outright," but only the "..''-escaping case was actually enforced -- an absolute
// reference passed straight through unchanged, since fs::path::operator/ silently discards the
// base directory for an absolute right-hand operand. Neither BuildAssetPath() nor anything else
// downstream re-contained it either.
TEST_F(ContentReaderExternalReferenceTest, AbsolutePathReferenceThrowsContentLoadException)
{
    ContentManager cm;

    std::string storage;
    auto stream = MakeReferenceStream("/etc/passwd", storage);
    ContentReader reader(&cm, stream.get(), "effects/myeffect", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
}

TEST_F(ContentReaderExternalReferenceTest, NoOwningContentManagerThrowsContentLoadException)
{
    std::string storage;
    auto stream = MakeReferenceStream("textures/white-1", storage);
    ContentReader reader(nullptr, stream.get(), "effects/myeffect", 5, 'w');

    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
}
