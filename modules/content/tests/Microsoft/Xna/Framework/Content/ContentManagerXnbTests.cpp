// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-17B/XNB-17C/XNB-17D/XNB-17E/XNB-17F: ContentManager's .xnb integration --
// resolution order (.xnb wins first), path/cache-identity reuse, ContentLoadException
// propagation, and Unload() behavior, proven end-to-end through ContentManager::Load<T>()
// using only a test-only reader (a real Texture2DReader is Phase C/XNB-23).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unordered_map>

#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class NullServiceProvider final : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info&) const override
        {
            return nullptr;
        }
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_xnb_content_manager_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
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

    struct TestValue
    {
        int32_t value = 0;
    };

    class PackagedAssetFileSystem final : public CNA::Platform::IPlatformFileSystem
    {
    public:
        std::unordered_map<std::string, std::vector<std::uint8_t>> assets;

        [[nodiscard]] std::string GetBasePath() const override { return {}; }
        [[nodiscard]] std::string GetPreferencesPath(
            const std::string&, const std::string&) const override { return {}; }
        [[nodiscard]] std::string GetUserFolder(CNA::Platform::UserFolder) const override
        {
            return {};
        }
        [[nodiscard]] bool TryLoadFile(
            const std::string& path, std::vector<std::uint8_t>& data) const override
        {
            const auto asset = assets.find(path);
            if (asset == assets.end())
            {
                return false;
            }
            data = asset->second;
            return true;
        }
        void CreateDirectory(const std::string&) override {}
    };

    class PackagedAssetPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformFileSystem* GetFileSystem() override
        {
            return &fileSystem;
        }

        PackagedAssetFileSystem fileSystem;
    };

    class TestOnlyValueReader : public ContentTypeReader<TestValue>
    {
    public:
        TestOnlyValueReader() : ContentTypeReader<TestValue>("CNA.Test.TestValue") {}

    protected:
        TestValue Read(ContentReader& input, std::optional<TestValue> existingInstance) override
        {
            TestValue result = existingInstance.value_or(TestValue{});
            result.value = input.ReadInt32();
            return result;
        }
    };

    // Builds a full, valid, minimal-header-plus-body .xnb byte sequence: 10-byte container
    // header (uncompressed, version 5, Windows platform) + a one-entry type-reader table for
    // "CNA.Test.TestValueReader" + zero shared resources + a root object with the given int32.
    std::vector<std::uint8_t> BuildTestXnbFile(int32_t value)
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1); // one type-reader table entry
        bodyWriter.Write(std::string("CNA.Test.TestValueReader"));
        bodyWriter.Write((int32_t)0); // reader version
        bodyWriter.Write7BitEncodedInt(0); // zero shared resources
        bodyWriter.Write7BitEncodedInt(1); // root object -> table entry 1
        bodyWriter.Write(value);
        bodyWriter.Flush();
        auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write((uint8_t)'X'); fileWriter.Write((uint8_t)'N'); fileWriter.Write((uint8_t)'B');
        fileWriter.Write((uint8_t)'w'); // platform: Windows
        fileWriter.Write((uint8_t)5);   // version
        fileWriter.Write((uint8_t)0);   // flags: uncompressed
        fileWriter.Write((int32_t)(10 + (int32_t)bodyBytes.size())); // total length
        fileWriter.Write(bodyBytes.data(), 0, (int32_t)bodyBytes.size());
        fileWriter.Flush();

        auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    class ContentManagerXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.TestValueReader", [] { return std::make_unique<TestOnlyValueReader>(); });
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST(ContentManagerConstructorTest, ServiceProviderConstructorUsesXnaEmptyRootDirectory)
{
    NullServiceProvider services;
    ContentManager content(&services);

    EXPECT_TRUE(content.getRootDirectoryProperty().empty());
}

TEST(ContentManagerConstructorTest, CnaDefaultConstructorKeepsItsContentRootExtensionDefault)
{
    ContentManager content;

    EXPECT_EQ(content.getRootDirectoryProperty(), "Content");
}

TEST_F(ContentManagerXnbTest, LoadFindsAndDeserializesARealXnbFile)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(123));

    ContentManager cm(nullptr, root.path().string());
    const TestValue result = cm.Load<TestValue>("fixture");

    EXPECT_EQ(result.value, 123);
}

TEST_F(ContentManagerXnbTest, LoadReadsXnbFromPlatformPackagedAssets)
{
    PackagedAssetPlatform platform;
    platform.fileSystem.assets.emplace("PackagedContent/fixture.xnb", BuildTestXnbFile(789));
    const CNA::Platform::Testing::ScopedCurrentPlatform current(platform);

    ContentManager cm(nullptr, "PackagedContent");
    const TestValue result = cm.Load<TestValue>("fixture");

    EXPECT_EQ(result.value, 789);
}

TEST_F(ContentManagerXnbTest, LoadAcceptsWindowsSeparatorsAndXnaCaseInsensitiveAssetNames)
{
    ScratchContentRoot root;
    std::filesystem::create_directories(root.path() / "Textures");
    WriteBytes(root.path() / "Textures" / "background.xnb", BuildTestXnbFile(456));

    ContentManager cm(nullptr, root.path().string());
    const TestValue result = cm.Load<TestValue>("textures\\Background");

    EXPECT_EQ(result.value, 456);
}

TEST_F(ContentManagerXnbTest, LoadCachesXnbAssetsLikeAnyOtherAsset)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(1));

    ContentManager cm(nullptr, root.path().string());
    const TestValue first = cm.Load<TestValue>("fixture");
    // Overwrite the file on disk -- a cached second Load<T>() call must not re-read it.
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(999));
    const TestValue second = cm.Load<TestValue>("fixture");

    EXPECT_EQ(first.value, 1);
    EXPECT_EQ(second.value, 1);
}

TEST_F(ContentManagerXnbTest, UnloadClearsXnbCachedAssets)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(1));

    ContentManager cm(nullptr, root.path().string());
    cm.Load<TestValue>("fixture");
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(2));
    cm.Unload();
    const TestValue result = cm.Load<TestValue>("fixture");

    EXPECT_EQ(result.value, 2);
}

TEST_F(ContentManagerXnbTest, MalformedXnbHeaderThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", {'X', 'N', 'X', 'w', 5, 0, 0, 0, 0, 0}); // bad magic

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, TruncatedLz4ContainerThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb",
               {'X', 'N', 'B', 'w', 5, 0x40, 10, 0, 0, 0});

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, BothCompressionBitsSetThrowsContentLoadException)
{
    // plans/plan_xnb.md XNB-27: neither a real LZX nor a real Lz4 signal -- rejected outright rather
    // than silently picking one.
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", {'X', 'N', 'B', 'w', 5, 0xC0, 0, 0, 0, 0});

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, UnregisteredXnbReaderNameThrowsContentLoadException)
{
    ScratchContentRoot root;
    ContentTypeReaderManager::ClearTypeCreators(); // no readers registered at all

    System::IO::MemoryStream bodyMs;
    System::IO::BinaryWriter bodyWriter(&bodyMs, true);
    bodyWriter.Write7BitEncodedInt(1);
    bodyWriter.Write(std::string("CNA.Test.TestValueReader"));
    bodyWriter.Write((int32_t)0);
    bodyWriter.Write7BitEncodedInt(0);
    bodyWriter.Write7BitEncodedInt(1);
    bodyWriter.Write((int32_t)1);
    bodyWriter.Flush();
    auto bodyBytes = bodyMs.ToArray();

    std::vector<std::uint8_t> fileBytes{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
    const int32_t totalLength = 10 + (int32_t)bodyBytes.size();
    std::memcpy(fileBytes.data() + 6, &totalLength, 4);
    fileBytes.insert(fileBytes.end(), bodyBytes.begin(), bodyBytes.end());

    WriteBytes(root.path() / "fixture.xnb", fileBytes);

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<TestValue>("fixture"), ContentLoadException);
}

// plans/plan_xnb.md XNB-43/47: found via a whole-container fuzz test that mutated a real .xnb's own
// totalLength header field independently of the file's actual on-disk size -- confirmed as a real
// heap-buffer-overflow under -DCNA_SANITIZE=address,undefined (the Lzx branch's compressedSize =
// totalLength - 14 was used to size a read straight from the just-read file buffer, with nothing
// cross-checking totalLength against how many bytes that buffer actually holds).
TEST_F(ContentManagerXnbTest, TotalLengthLargerThanActualFileSizeThrowsContentLoadException)
{
    ScratchContentRoot root;
    // A real, valid 10-byte uncompressed header, except totalLength (bytes 6-9) claims 100000
    // bytes while the file on disk is only the 10-byte header itself.
    std::vector<std::uint8_t> fileBytes{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
    const int32_t bogusTotalLength = 100000;
    std::memcpy(fileBytes.data() + 6, &bogusTotalLength, 4);
    WriteBytes(root.path() / "fixture.xnb", fileBytes);

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, XnbWinsOverCnjAndNativeExtensionForTheSameName)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(42));
    // A .cnj sidecar with the same logical name that would resolve to a totally different value
    // if it were consulted -- proves .xnb is checked and wins before .cnj ever gets a look.
    WriteFile(root.path() / "fixture.cnj", R"({"cnjVersion": 1, "type": "TestValue"})");

    ContentManager cm(nullptr, root.path().string());
    const TestValue result = cm.Load<TestValue>("fixture");

    EXPECT_EQ(result.value, 42);
}
