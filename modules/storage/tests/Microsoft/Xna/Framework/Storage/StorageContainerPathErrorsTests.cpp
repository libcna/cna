// SPDX-License-Identifier: MS-PL
// Genuine public-call evidence: cna-demos StoragePathErrorsOracle.cs.
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <typeinfo>
#include <type_traits>
#include <vector>
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IO/DirectoryNotFoundException.hpp"
#include "System/IO/IOException.hpp"

using namespace Microsoft::Xna::Framework::Storage;
namespace fs = std::filesystem;
namespace
{
template<class Exception, class Action>
void Exact(Action action, const char* parameter = nullptr)
{
    try { action(); ADD_FAILURE() << "expected " << typeid(Exception).name(); }
    catch (const Exception& e)
    {
        EXPECT_TRUE(typeid(e) == typeid(Exception));
        if constexpr (std::is_base_of_v<System::ArgumentException, Exception>)
            if (parameter) EXPECT_EQ(e.getParamNameProperty(), parameter);
    }
    catch (const std::exception& e) { ADD_FAILURE() << "wrong exception: " << typeid(e).name() << ": " << e.what(); }
}

class StorageContainerPathErrorsTest : public ::testing::Test
{
protected:
    fs::path fixture, root;
    bool owns = false;
    std::unique_ptr<StorageDevice> device;
    std::unique_ptr<StorageContainer> container;
    void SetUp() override
    {
        std::random_device entropy;
        const std::string name = "CnaStorageErrors-" + std::to_string(entropy()) + "-" + std::to_string(entropy());
        fixture = CNA::Internal::PathFromUtf8(StorageDevice::GetStorageRootEXT()) / name;
        ASSERT_TRUE(fs::create_directory(fixture));
        owns = true;
        auto selected = StorageDevice::BeginShowSelector(nullptr, nullptr);
        device = StorageDevice::EndShowSelector(selected.get());
        auto result = device->BeginOpenContainer(name, nullptr, nullptr);
        container = device->EndOpenContainer(result.get());
        root = fixture / "AllPlayers";
        ASSERT_TRUE(fs::create_directory(root / "child"));
        std::ofstream seed(root / "seed.txt"); seed << "seed";
        ASSERT_TRUE(seed.good());
    }
    void TearDown() override
    {
        container.reset(); device.reset();
        if (owns) { std::error_code ignored; fs::remove_all(fixture, ignored); }
    }
};
}

// RV-STORAGE review #7: Framework 4's Path.CheckInvalidPathChars, reached before any filesystem
// call (ValidateArguments -> GetFullPath; a search's Path.InternalCombine), refuses a control
// character or " < > | with ArgumentException("Illegal characters in path."). An embedded NUL used
// to reach the native call, which acted on the prefix: DeleteDirectory deleted "child".
TEST_F(StorageContainerPathErrorsTest, IllegalPathCharactersRefuseBeforeTheFilesystemIsTouched)
{
    const std::string nul("child\0ignored", 13);
    ASSERT_EQ(nul.size(), 13u);
    const auto illegal = [](auto action)
    {
        try { action(); ADD_FAILURE() << "an illegal path character was accepted"; }
        catch (const System::ArgumentException& e)
        {
            EXPECT_TRUE(typeid(e) == typeid(System::ArgumentException));
            EXPECT_EQ(std::string(e.what()).rfind("Illegal characters in path.", 0), 0u) << e.what();
        }
    };
    illegal([&] { container->DeleteDirectory(nul); });
    EXPECT_TRUE(fs::is_directory(root / "child")) << "the NUL-truncated path deleted another directory";
    illegal([&] { (void)container->DirectoryExists(nul); });
    illegal([&] { (void)container->FileExists(std::string("seed.txt\0x", 10)); });
    illegal([&] { (void)container->CreateFile("save|1.dat"); });
    EXPECT_FALSE(fs::exists(root / "save|1.dat"));
    illegal([&] { container->CreateDirectory("a<b"); });
    illegal([&] { (void)container->OpenFile("q\"uote.dat", System::IO::FileMode::OpenOrCreate); });
    illegal([&] { (void)container->GetFileNames("*|.txt"); });
    illegal([&] { (void)container->GetDirectoryNames("child/\x01*"); });
}

// RV-STORAGE review #3 and rv-fix5 #2: Framework 4's search iterator normalizes the pattern before
// anything else -- trims trailing whitespace (Path.TrimEndChars), maps "." to "*", refuses ".." at
// the end or before a separator, finds nothing for an empty pattern, searches "*" after a trailing
// separator -- and only then checks the combined path's characters. A leading "./" is the container.
TEST_F(StorageContainerPathErrorsTest, SearchPatternsAreNormalizedAsFramework4DoesBeforeTheyAreChecked)
{
    { std::ofstream nested(root / "child" / "nested.dat"); nested << "n"; }
    using Names = std::vector<std::string>;
    EXPECT_EQ(container->GetFileNames("\t"), Names{}) << "a pattern trimmed to nothing finds nothing";
    EXPECT_EQ(container->GetDirectoryNames("\n"), Names{});
    EXPECT_EQ(container->GetFileNames("*.txt\t "), Names{"seed.txt"});
    EXPECT_EQ(container->GetFileNames("."), Names{"seed.txt"});
    EXPECT_EQ(container->GetDirectoryNames("."), Names{"child"});
    EXPECT_EQ(container->GetFileNames("./*.txt"), Names{"seed.txt"});
    EXPECT_EQ(container->GetFileNames("child/"), Names{"child/nested.dat"});
    EXPECT_EQ(container->GetFileNames("a..b"), Names{});
    for (const char* refused : {"..", "*..", "../*", "child/..\\x"})
        Exact<System::ArgumentException>([&] { (void)container->GetFileNames(refused); });
}

TEST_F(StorageContainerPathErrorsTest, EmptyDirectoryArgumentsPreserveExactExceptionAndParameter)
{
    Exact<System::ArgumentNullException>([&] { container->CreateDirectory(""); }, "directory");
    Exact<System::ArgumentNullException>([&] { (void)container->DirectoryExists(""); }, "directory");
    Exact<System::ArgumentNullException>([&] { container->DeleteDirectory(""); }, "directory");
    EXPECT_TRUE(fs::is_directory(root));
}

TEST_F(StorageContainerPathErrorsTest, EmptyFileArgumentsPrecedeInvalidOpenOptions)
{
    using namespace System::IO;
    Exact<System::ArgumentNullException>([&] { (void)container->CreateFile(""); }, "file");
    Exact<System::ArgumentNullException>([&] { (void)container->FileExists(""); }, "file");
    Exact<System::ArgumentNullException>([&] { container->DeleteFile(""); }, "file");
    Exact<System::ArgumentNullException>([&] { (void)container->OpenFile("", static_cast<FileMode>(-1)); }, "file");
    Exact<System::ArgumentNullException>([&] { (void)container->OpenFile("", static_cast<FileMode>(-1), static_cast<FileAccess>(-1)); }, "file");
    Exact<System::ArgumentNullException>([&] { (void)container->OpenFile("", static_cast<FileMode>(-1), static_cast<FileAccess>(-1), static_cast<FileShare>(-1)); }, "file");
    EXPECT_EQ(fs::file_size(root / "seed.txt"), 4U);
}

TEST_F(StorageContainerPathErrorsTest, EmptySearchPatternsReturnNoMatchesWithoutFilesystemAccess)
{
    EXPECT_TRUE(container->GetFileNames("").empty());
    EXPECT_TRUE(container->GetDirectoryNames("").empty());
    EXPECT_EQ(container->GetFileNames(), std::vector<std::string>{"seed.txt"});
    EXPECT_EQ(container->GetDirectoryNames(), std::vector<std::string>{"child"});
    fs::rename(root, fixture / "moved");
    EXPECT_TRUE(container->GetFileNames("").empty());
    EXPECT_TRUE(container->GetDirectoryNames("").empty());
}

TEST_F(StorageContainerPathErrorsTest, DisposedStateStillPrecedesEmptyArgumentsAndPatterns)
{
    using namespace System::IO;
    container->Dispose();
    Exact<System::ObjectDisposedException>([&] { container->CreateDirectory(""); });
    Exact<System::ObjectDisposedException>([&] { (void)container->DirectoryExists(""); });
    Exact<System::ObjectDisposedException>([&] { container->DeleteDirectory(""); });
    Exact<System::ObjectDisposedException>([&] { (void)container->CreateFile(""); });
    Exact<System::ObjectDisposedException>([&] { (void)container->FileExists(""); });
    Exact<System::ObjectDisposedException>([&] { container->DeleteFile(""); });
    Exact<System::ObjectDisposedException>([&] { (void)container->OpenFile("", FileMode::Open); });
    Exact<System::ObjectDisposedException>([&] { (void)container->OpenFile("", FileMode::Open, FileAccess::Read); });
    Exact<System::ObjectDisposedException>([&] { (void)container->OpenFile("", FileMode::Open, FileAccess::Read, FileShare::Read); });
    Exact<System::ObjectDisposedException>([&] { (void)container->GetFileNames(""); });
    Exact<System::ObjectDisposedException>([&] { (void)container->GetDirectoryNames(""); });
}

TEST_F(StorageContainerPathErrorsTest, NonEmptyDirectoryRefusesWithIOExceptionAndPreservesContents)
{
    { std::ofstream seed(root / "child" / "kept.txt"); seed << "keep"; ASSERT_TRUE(seed.good()); }
    Exact<System::IO::IOException>([&] { container->DeleteDirectory("child"); });
    std::ifstream seed(root / "child" / "kept.txt");
    std::string text; seed >> text;
    EXPECT_EQ(text, "keep");
}

TEST_F(StorageContainerPathErrorsTest, MissingDirectoryUsesDirectoryNotFoundException)
{
    Exact<System::IO::DirectoryNotFoundException>([&] { container->DeleteDirectory("missing"); });
}

TEST_F(StorageContainerPathErrorsTest, EmptyUnicodeDirectoryCanBeDeleted)
{
    const std::string name = "empty-\xc3\xa9-\xe6\x97\xa5";
    const fs::path path = root / CNA::Internal::PathFromUtf8(name);
    ASSERT_TRUE(fs::create_directory(path));
    container->DeleteDirectory(name);
    EXPECT_FALSE(fs::exists(path));
}
