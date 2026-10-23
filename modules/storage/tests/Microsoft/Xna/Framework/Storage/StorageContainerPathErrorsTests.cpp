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
