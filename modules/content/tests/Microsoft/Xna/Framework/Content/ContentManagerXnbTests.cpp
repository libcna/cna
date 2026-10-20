// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-17B/XNB-17C/XNB-17D/XNB-17E/XNB-17F: ContentManager's .xnb integration --
// resolution order (.xnb wins first), path/cache-identity reuse, ContentLoadException
// propagation, and Unload() behavior, proven end-to-end through ContentManager::Load<T>()
// using only a test-only reader (a real Texture2DReader is Phase C/XNB-23).

#include <algorithm>
#include <cctype>
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
#include "System/IO/IOException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

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
        [[nodiscard]] bool TryLoadFileIgnoringCase(
            const std::string& path, std::vector<std::uint8_t>& data) const override
        {
            std::string wanted = path;
            std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

            const std::vector<std::uint8_t>* match = nullptr;
            for (const auto& [candidatePath, candidateBytes] : assets)
            {
                std::string candidate = candidatePath;
                std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (candidate != wanted)
                {
                    continue;
                }
                if (match != nullptr)
                {
                    return false;
                }
                match = &candidateBytes;
            }
            if (match == nullptr)
            {
                return false;
            }
            data = *match;
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

    std::any ReadUntypedFixtureReference(ContentManager& manager)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(std::string("fixture"));
        writer.Flush();
        stream.setPositionProperty(0);
        ContentReader reader(&manager, &stream, "parent", 5, 'w');
        return reader.ReadExternalReference();
    }
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

TEST_F(ContentManagerXnbTest, LoadMatchesPackagedXnbPathCaseLikeWindows)
{
    PackagedAssetPlatform platform;
    platform.fileSystem.assets.emplace(
        "PackagedContent/Textures/ingame.xnb", BuildTestXnbFile(790));
    const CNA::Platform::Testing::ScopedCurrentPlatform current(platform);

    ContentManager cm(nullptr, "PackagedContent");
    const TestValue result = cm.Load<TestValue>("textures\\Ingame");

    EXPECT_EQ(result.value, 790);
}

TEST_F(ContentManagerXnbTest, AmbiguousPackagedPathCaseIsRejected)
{
    PackagedAssetPlatform platform;
    platform.fileSystem.assets.emplace(
        "PackagedContent/Textures/ingame.xnb", BuildTestXnbFile(790));
    platform.fileSystem.assets.emplace(
        "PackagedContent/Textures/InGame.xnb", BuildTestXnbFile(791));
    const CNA::Platform::Testing::ScopedCurrentPlatform current(platform);

    ContentManager cm(nullptr, "PackagedContent");
    EXPECT_THROW((void)cm.Load<TestValue>("textures\\INGAME"), ContentLoadException);
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
    ASSERT_EQ(cm.Load<TestValue>("fixture").value, 1);
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(2));
    cm.Unload();
    const TestValue result = cm.Load<TestValue>("fixture");

    EXPECT_EQ(result.value, 2);
}

TEST_F(ContentManagerXnbTest, LoadedAssetsRefuseBothRootSettersUntilUnload)
{
    ScratchContentRoot firstRoot;
    ScratchContentRoot secondRoot;
    WriteBytes(firstRoot.path() / "fixture.xnb", BuildTestXnbFile(11));
    WriteBytes(secondRoot.path() / "fixture.xnb", BuildTestXnbFile(22));
    for (const bool moveArgument : {false, true})
    {
        SCOPED_TRACE(moveArgument);
        const std::string original = firstRoot.path().string();
        const std::string replacement = secondRoot.path().string();
        ContentManager cm(nullptr, original);
        ASSERT_EQ(cm.Load<TestValue>("fixture").value, 11);
        if (moveArgument)
        {
            EXPECT_THROW(cm.setRootDirectoryProperty(std::string(original)),
                         System::InvalidOperationException);
            EXPECT_THROW(cm.setRootDirectoryProperty(std::string(replacement)),
                         System::InvalidOperationException);
        }
        else
        {
            EXPECT_THROW(cm.setRootDirectoryProperty(original), System::InvalidOperationException);
            EXPECT_THROW(cm.setRootDirectoryProperty(replacement), System::InvalidOperationException);
        }
        EXPECT_EQ(cm.getRootDirectoryProperty(), original);
        EXPECT_EQ(cm.Load<TestValue>("fixture").value, 11);
        cm.Unload();
        if (moveArgument) cm.setRootDirectoryProperty(std::string(replacement));
        else cm.setRootDirectoryProperty(replacement);
        EXPECT_EQ(cm.getRootDirectoryProperty(), replacement);
        EXPECT_EQ(cm.Load<TestValue>("fixture").value, 22);
    }
}

TEST_F(ContentManagerXnbTest, FailedLoadDoesNotPreventChangingRoot)
{
    ScratchContentRoot missingRoot;
    ScratchContentRoot validRoot;
    WriteBytes(validRoot.path() / "fixture.xnb", BuildTestXnbFile(33));
    ContentManager cm(nullptr, missingRoot.path().string());
    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
    EXPECT_NO_THROW(cm.setRootDirectoryProperty(validRoot.path().string()));
    EXPECT_EQ(cm.Load<TestValue>("fixture").value, 33);
}

TEST_F(ContentManagerXnbTest, UncachedReadAssetDoesNotPreventChangingRoot)
{
    class ExposedContentManager final : public ContentManager
    {
    public:
        using ContentManager::ContentManager;
        using ContentManager::ReadAsset;
    };
    ScratchContentRoot firstRoot;
    ScratchContentRoot secondRoot;
    WriteBytes(firstRoot.path() / "fixture.xnb", BuildTestXnbFile(44));
    WriteBytes(secondRoot.path() / "fixture.xnb", BuildTestXnbFile(55));
    ExposedContentManager cm(nullptr, firstRoot.path().string());
    ASSERT_EQ(cm.ReadAsset<TestValue>("fixture", {}).value, 44);
    const std::string replacement = secondRoot.path().string();
    EXPECT_NO_THROW(cm.setRootDirectoryProperty(replacement));
    EXPECT_EQ(cm.ReadAsset<TestValue>("fixture", {}).value, 55);
}

TEST_F(ContentManagerXnbTest, MalformedXnbHeaderThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", {'X', 'N', 'X', 'w', 5, 0, 0, 0, 0, 0}); // bad magic

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, EveryTruncatedHeaderIsWrappedAtTheContentManagerBoundary)
{
    class ExposedContentManager final : public ContentManager
    {
    public:
        using ContentManager::ContentManager;
        using ContentManager::ReadAsset;
    };
    ScratchContentRoot root;
    const auto complete = BuildTestXnbFile(66);
    for (std::size_t length = 0; length < 10; ++length)
    {
        SCOPED_TRACE(length);
        WriteBytes(root.path() / "fixture.xnb", {complete.begin(), complete.begin() + length});
        ExposedContentManager cm(nullptr, root.path().string());
        EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
        EXPECT_THROW((void)cm.ReadAsset<TestValue>("fixture", {}), ContentLoadException);
    }
}

TEST_F(ContentManagerXnbTest, TruncatedObjectBodyIsWrappedWithoutCachingAPartialAsset)
{
    ScratchContentRoot root;
    auto bytes = BuildTestXnbFile(77);
    bytes.pop_back();
    const std::int32_t length = static_cast<std::int32_t>(bytes.size());
    for (std::size_t i = 0; i < 4; ++i)
        bytes[6 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(length) >> (i * 8));
    WriteBytes(root.path() / "fixture.xnb", bytes);
    ContentManager cm(nullptr, root.path().string());
    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(88));
    EXPECT_EQ(cm.Load<TestValue>("fixture").value, 88);
}

TEST_F(ContentManagerXnbTest, UntypedExternalReferencesWrapEveryTruncatedHeader)
{
    ScratchContentRoot root;
    const auto complete = BuildTestXnbFile(66);
    for (std::size_t length = 0; length < 10; ++length)
    {
        SCOPED_TRACE(length);
        WriteBytes(root.path() / "fixture.xnb", {complete.begin(), complete.begin() + length});
        ContentManager cm(nullptr, root.path().string());
        EXPECT_THROW((void)ReadUntypedFixtureReference(cm), ContentLoadException);
    }
}

TEST_F(ContentManagerXnbTest, UntypedCompressedHeaderRefusesBeforeReadingBeyondTheFile)
{
    ScratchContentRoot root;
    for (std::size_t length = 10; length < 14; ++length)
    {
        SCOPED_TRACE(length);
        std::vector<std::uint8_t> bytes{'X', 'N', 'B', 'w', 5, 0x80,
                                      static_cast<std::uint8_t>(length), 0, 0, 0};
        bytes.resize(length);
        WriteBytes(root.path() / "fixture.xnb", bytes);
        ContentManager cm(nullptr, root.path().string());
        EXPECT_THROW((void)ReadUntypedFixtureReference(cm), ContentLoadException);
    }
}

TEST_F(ContentManagerXnbTest, ReaderIoExceptionsAreWrappedButUserExceptionsKeepTheirType)
{
    class ThrowingValueReader final : public ContentTypeReader<TestValue>
    {
    public:
        explicit ThrowingValueReader(bool io) : ContentTypeReader("CNA.Test.TestValue"), io_(io) {}
    protected:
        TestValue Read(ContentReader&, std::optional<TestValue>) override
        {
            if (io_) throw System::IO::IOException("reader IO sentinel");
            throw System::InvalidOperationException("reader user sentinel");
        }
    private:
        bool io_;
    };
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", BuildTestXnbFile(99));
    for (const bool io : {false, true})
    {
        SCOPED_TRACE(io);
        ContentTypeReaderManager::ClearTypeCreators();
        ContentTypeReaderManager::AddTypeCreator("CNA.Test.TestValueReader",
            [io] { return std::make_unique<ThrowingValueReader>(io); });
        ContentManager cm(nullptr, root.path().string());
        if (io)
            EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
        else
            EXPECT_THROW((void)cm.Load<TestValue>("fixture"), System::InvalidOperationException);
    }
}

TEST_F(ContentManagerXnbTest, TruncatedLz4ContainerThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb",
               {'X', 'N', 'B', 'w', 5, 0x40, 10, 0, 0, 0});

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
}

TEST_F(ContentManagerXnbTest, BothCompressionBitsSetThrowsContentLoadException)
{
    // plans/plan_xnb.md XNB-27: neither a real LZX nor a real Lz4 signal -- rejected outright rather
    // than silently picking one.
    ScratchContentRoot root;
    WriteBytes(root.path() / "fixture.xnb", {'X', 'N', 'B', 'w', 5, 0xC0, 0, 0, 0, 0});

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
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

    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
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

    EXPECT_THROW((void)cm.Load<TestValue>("fixture"), ContentLoadException);
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
