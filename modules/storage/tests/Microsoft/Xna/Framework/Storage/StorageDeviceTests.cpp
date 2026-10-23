// SPDX-License-Identifier: MS-PL
//
// StorageDevice::DeleteContainer(titleName) built its target path via
// fs::path(storageRoot) / titleName with no containment check -- an absolute titleName (or one
// escaping via "..") resolved outside the storage root and was recursively deleted. No prior test
// coverage existed for DeleteContainer at all.

#include <gtest/gtest.h>
#include <string>
#include <stdexcept>
#include <exception>
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "SharpRuntime/Storage/StoragePaths.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IO/IOException.hpp"
#include "System/IO/DirectoryNotFoundException.hpp"
#include "System/UnauthorizedAccessException.hpp"

using Microsoft::Xna::Framework::Storage::StorageDevice;

namespace
{
    namespace fs = std::filesystem;

    // Isolates every test in this file to its own real, disposable storage root, matching the
    // established convention in GamerServicesGamerTests.cpp's GamerServicesStoreGuard -- avoids
    // colliding with (or accidentally deleting) any other test's or the real user's storage state.
    class StorageDeviceDeleteContainerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            StorageDevice::SetAppNameEXT("CnaTestsContent002StorageDevice");
            root_ = StorageDevice::GetStorageRootEXT();
            ASSERT_FALSE(root_.empty());
            fs::create_directories(root_);
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(root_, ec);
            StorageDevice::SetAppNameEXT("");
        }

        // Marks that the real storage root itself (and everything under it) is still intact --
        // used to prove a rejected DeleteContainer call touched nothing.
        void CreateMarker(const std::string& relativePath)
        {
            fs::path p = fs::path(root_) / relativePath;
            fs::create_directories(p.parent_path());
            std::ofstream(p) << "marker";
        }

        bool MarkerExists(const std::string& relativePath) const
        {
            std::error_code ec;
            return fs::exists(fs::path(root_) / relativePath, ec);
        }

        std::unique_ptr<Microsoft::Xna::Framework::Storage::StorageContainer> OpenContainer(
            const std::string& displayName)
        {
            auto operation = device_->BeginOpenContainer(displayName, nullptr, nullptr);
            return device_->EndOpenContainer(operation.get());
        }

        std::string root_;
        // StorageDevice's constructor is private; the real acquisition path is
        // BeginShowSelector()/EndShowSelector(), matching the fake-async XNA 4.0 pattern.
        std::unique_ptr<StorageDevice> device_ =
            StorageDevice::EndShowSelector(StorageDevice::BeginShowSelector(nullptr, nullptr).get());
    };
}

// Genuine XNA (StorageResidualPathsOracle.cs): Path.Combine(root, "") is the root, so an empty
// name removes the player folder there -- never the root itself.
TEST_F(StorageDeviceDeleteContainerTest, EmptyTitleNameDeletesOnlyTheRootPlayerFolder)
{
    CreateMarker("AllPlayers/save.dat");
    CreateMarker("sentinel.txt");
    EXPECT_NO_THROW(device_->DeleteContainer(""));
    EXPECT_FALSE(MarkerExists("AllPlayers"));
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "an empty name must not delete the storage root";
}

TEST_F(StorageDeviceDeleteContainerTest, AppNameAlsoScopesSharpRuntimeIsolatedStorage)
{
    EXPECT_EQ(
        SharpRuntime::Storage::StoragePaths::GetIsolatedStorageRoot(),
        fs::path(root_) / ".cna_isolated_storage");
}

TEST_F(StorageDeviceDeleteContainerTest, AbsoluteTitleNameThrowsAndDeletesNothing)
{
    CreateMarker("sentinel.txt");
    EXPECT_THROW(device_->DeleteContainer("/etc"), System::ArgumentException);
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "storage root must be untouched by a rejected call";
}

TEST_F(StorageDeviceDeleteContainerTest, EscapingTitleNameThrowsAndDeletesNothing)
{
    // The classic reproduction from the finding: enough ".." segments to climb above the real
    // storage root (a SDL pref-path directory is always several levels deep), landing on some
    // other, unrelated real directory this test must never actually touch.
    CreateMarker("sentinel.txt");
    EXPECT_THROW(device_->DeleteContainer("../../../../../../../../tmp"), System::ArgumentException);
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "storage root must be untouched by a rejected call";
}

TEST_F(StorageDeviceDeleteContainerTest, DotTitleNameDeletesOnlyTheRootPlayerFolder)
{
    CreateMarker("AllPlayers/save.dat");
    CreateMarker("MyGame/AllPlayers/save1.dat");
    CreateMarker("sentinel.txt");
    EXPECT_NO_THROW(device_->DeleteContainer("."));
    EXPECT_FALSE(MarkerExists("AllPlayers"));
    EXPECT_TRUE(MarkerExists("MyGame/AllPlayers/save1.dat"));
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "'.' must not delete the entire storage root";
}

TEST_F(StorageDeviceDeleteContainerTest, SimpleTitleNameDeletesOnlyThatContainer)
{
    CreateMarker("MyGame/AllPlayers/save1.dat");
    CreateMarker("sentinel.txt"); // sibling that must survive

    EXPECT_NO_THROW(device_->DeleteContainer("MyGame"));

    EXPECT_FALSE(MarkerExists("MyGame/AllPlayers/save1.dat"));
    EXPECT_TRUE(MarkerExists("sentinel.txt"));
}

TEST_F(StorageDeviceDeleteContainerTest, ContainerOpenRejectsPathsOutsideStorageRoot)
{
    auto parentEscape = device_->BeginOpenContainer("../outside", nullptr, nullptr);
    EXPECT_THROW(
        { [[maybe_unused]] auto rejected = device_->EndOpenContainer(parentEscape.get()); },
        System::ArgumentException);

    auto absoluteEscape = device_->BeginOpenContainer("/tmp/cna-storage-escape", nullptr, nullptr);
    EXPECT_THROW(
        { [[maybe_unused]] auto rejected = device_->EndOpenContainer(absoluteEscape.get()); },
        System::ArgumentException);
}

TEST_F(StorageDeviceDeleteContainerTest, ContainerOperationsRejectLexicalEscapes)
{
    auto container = OpenContainer("ContainedGame");

    EXPECT_THROW(container->CreateDirectory("../outside"), System::ArgumentException);
    EXPECT_THROW(
        { [[maybe_unused]] const bool rejected = container->DirectoryExists("../outside"); },
        System::ArgumentException);
    EXPECT_THROW(container->DeleteDirectory("../outside"), System::ArgumentException);
    EXPECT_THROW(
        { [[maybe_unused]] auto rejected = container->CreateFile("../outside.bin"); },
        System::ArgumentException);
    EXPECT_THROW(
        { [[maybe_unused]] const bool rejected = container->FileExists("../outside.bin"); },
        System::ArgumentException);
    EXPECT_THROW(container->DeleteFile("../outside.bin"), System::ArgumentException);
    EXPECT_THROW(
        { [[maybe_unused]] auto rejected =
            container->OpenFile("../outside.bin", System::IO::FileMode::Open); },
        System::ArgumentException);

    EXPECT_FALSE(MarkerExists("ContainedGame/outside.bin"));
}

TEST_F(StorageDeviceDeleteContainerTest, ContainerOperationsRejectSymlinkEscapes)
{
    auto container = OpenContainer("ContainedGame");
    const fs::path containerRoot = fs::path(root_) / "ContainedGame" / "AllPlayers";
    const fs::path outsideRoot = fs::path(root_) / "outside";
    fs::create_directories(outsideRoot);

    std::error_code error;
    fs::create_directory_symlink(outsideRoot, containerRoot / "escape", error);
    if (error)
    {
        GTEST_SKIP() << "Directory symlinks are unavailable: " << error.message();
    }

    EXPECT_THROW(
        { [[maybe_unused]] auto rejected = container->CreateFile("escape/outside.bin"); },
        System::ArgumentException);
    EXPECT_THROW(
        { [[maybe_unused]] const bool rejected = container->FileExists("escape/outside.bin"); },
        System::ArgumentException);
    EXPECT_FALSE(fs::exists(outsideRoot / "outside.bin"));
}

// weakly_canonical cannot follow a symlink whose target does not exist yet, so such a link used to
// pass the containment check; opening it for writing then created its target outside.
TEST_F(StorageDeviceDeleteContainerTest, ContainerOperationsRejectDanglingSymlinkEscapes)
{
    auto container = OpenContainer("ContainedGame");
    const fs::path containerRoot = fs::path(root_) / "ContainedGame" / "AllPlayers";
    const fs::path outsideRoot = fs::path(root_) / "outside";
    fs::create_directories(outsideRoot);

    std::error_code error;
    fs::create_symlink(outsideRoot / "created.bin", containerRoot / "dangling", error);
    if (error)
    {
        GTEST_SKIP() << "Symlinks are unavailable: " << error.message();
    }
    fs::create_symlink("../../outside/relative.bin", containerRoot / "relative", error);
    ASSERT_FALSE(error) << error.message();
    // A chain: the first link exists and names a second that dangles outside.
    fs::create_symlink(outsideRoot / "chained.bin", containerRoot / "second", error);
    ASSERT_FALSE(error) << error.message();
    fs::create_symlink("second", containerRoot / "chain", error);
    ASSERT_FALSE(error) << error.message();
    // ".." after a linked directory is resolved by the kernel from the link's target.
    fs::create_directories(fs::path(root_) / "deep" / "inner");
    fs::create_directory_symlink(fs::path(root_) / "deep" / "inner", containerRoot / "jump", error);
    ASSERT_FALSE(error) << error.message();
    fs::create_symlink("jump/../escaped.bin", containerRoot / "through", error);
    ASSERT_FALSE(error) << error.message();
    // A dangling link that stays inside the container is an ordinary new file.
    fs::create_symlink("inside.bin", containerRoot / "local", error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_NO_THROW({ [[maybe_unused]] auto accepted = container->CreateFile("local"); });
    EXPECT_TRUE(fs::is_regular_file(containerRoot / "inside.bin"));

    for (const char* link : {"dangling", "relative", "chain", "through"})
    {
        SCOPED_TRACE(link);
        EXPECT_THROW({ [[maybe_unused]] auto rejected = container->CreateFile(link); },
                     System::ArgumentException);
        for (const auto mode : {System::IO::FileMode::Create, System::IO::FileMode::OpenOrCreate,
                                System::IO::FileMode::Append, System::IO::FileMode::CreateNew})
        {
            EXPECT_THROW({ [[maybe_unused]] auto rejected = container->OpenFile(link, mode); },
                         System::ArgumentException);
        }
    }
    EXPECT_FALSE(fs::exists(outsideRoot / "created.bin"));
    EXPECT_FALSE(fs::exists(outsideRoot / "relative.bin"));
    EXPECT_FALSE(fs::exists(outsideRoot / "chained.bin"));
    EXPECT_FALSE(fs::exists(fs::path(root_) / "deep" / "escaped.bin"));
}

TEST_F(StorageDeviceDeleteContainerTest, ContainerAllowsNormalizedPathsThatRemainContained)
{
    auto container = OpenContainer("ContainedGame");
    container->CreateDirectory("saves/../profiles");
    EXPECT_TRUE(container->DirectoryExists("profiles"));

    auto stream = container->CreateFile("profiles/../save.bin");
    stream.reset();
    EXPECT_TRUE(container->FileExists("save.bin"));
}

// Museum B11. Genuine XNA4 public-call oracle: StoragePathErrorsOracle.cs, 29/29.
// Keep these committed regressions independent of inherited uncommitted test files.
TEST_F(StorageDeviceDeleteContainerTest, EmptyContainerPathsMatchXnaExceptionTypeAndParameter)
{
    using namespace System::IO;
    auto container = OpenContainer("Arguments");
    const std::vector<std::pair<std::function<void()>, const char*>> operations = {
        {[&] { container->CreateDirectory(""); }, "directory"},
        {[&] { (void)container->DirectoryExists(""); }, "directory"},
        {[&] { container->DeleteDirectory(""); }, "directory"},
        {[&] { (void)container->CreateFile(""); }, "file"},
        {[&] { (void)container->FileExists(""); }, "file"},
        {[&] { container->DeleteFile(""); }, "file"},
        {[&] { (void)container->OpenFile("", static_cast<FileMode>(-1)); }, "file"},
        {[&] { (void)container->OpenFile("", static_cast<FileMode>(-1),
                                       static_cast<FileAccess>(-1)); }, "file"},
        {[&] { (void)container->OpenFile("", static_cast<FileMode>(-1),
                                       static_cast<FileAccess>(-1), static_cast<FileShare>(-1)); }, "file"}
    };
    for (std::size_t index = 0; index < operations.size(); ++index)
    {
        SCOPED_TRACE(index);
        try
        {
            operations[index].first();
            ADD_FAILURE() << "expected ArgumentNullException";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_TRUE(typeid(error) == typeid(System::ArgumentNullException));
            EXPECT_EQ(error.getParamNameProperty(), operations[index].second);
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << "wrong exception: " << error.what();
        }
    }
}

TEST_F(StorageDeviceDeleteContainerTest, EmptyContainerPatternsReturnNoMatchesWithoutAccessingTheRoot)
{
    auto container = OpenContainer("Patterns");
    container->CreateDirectory("child");
    { auto file = container->CreateFile("seed.txt"); }
    EXPECT_EQ(container->GetFileNames(), std::vector<std::string>{"seed.txt"});
    EXPECT_EQ(container->GetDirectoryNames(), std::vector<std::string>{"child"});
    EXPECT_TRUE(container->GetFileNames("").empty());
    EXPECT_TRUE(container->GetDirectoryNames("").empty());
    fs::rename(fs::path(root_) / "Patterns" / "AllPlayers", fs::path(root_) / "moved");
    EXPECT_TRUE(container->GetFileNames("").empty());
    EXPECT_TRUE(container->GetDirectoryNames("").empty());
}

TEST_F(StorageDeviceDeleteContainerTest, DisposalStillPrecedesEmptyContainerPatternsAndPaths)
{
    auto container = OpenContainer("DisposedArguments");
    container->Dispose();
    EXPECT_THROW(container->CreateDirectory(""), System::ObjectDisposedException);
    EXPECT_THROW((void)container->CreateFile(""), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetFileNames(""), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetDirectoryNames(""), System::ObjectDisposedException);
}

TEST_F(StorageDeviceDeleteContainerTest, NonemptyDirectoryDeletionHasAnIoExceptionAndPreservesFiles)
{
    auto container = OpenContainer("DeletionErrors");
    CreateMarker("DeletionErrors/AllPlayers/child/keep.txt");
    try
    {
        container->DeleteDirectory("child");
        ADD_FAILURE() << "expected IOException";
    }
    catch (const System::IO::IOException& error)
    {
        EXPECT_TRUE(typeid(error) == typeid(System::IO::IOException));
    }
    EXPECT_TRUE(MarkerExists("DeletionErrors/AllPlayers/child/keep.txt"));
}

TEST_F(StorageDeviceDeleteContainerTest, MissingDirectoryDeletionHasTheDirectoryNotFoundType)
{
    auto container = OpenContainer("MissingDirectory");
    EXPECT_THROW(container->DeleteDirectory("missing"), System::IO::DirectoryNotFoundException);
    EXPECT_THROW(container->DeleteDirectory("missing/child"), System::IO::DirectoryNotFoundException);
}

TEST_F(StorageDeviceDeleteContainerTest, DirectoryDeletionNeverTreatsARegularFileAsADirectory)
{
    auto container = OpenContainer("FileIsNotDirectory");
    CreateMarker("FileIsNotDirectory/AllPlayers/keep.txt");
    EXPECT_THROW(container->DeleteDirectory("keep.txt"), System::IO::IOException);
    EXPECT_TRUE(MarkerExists("FileIsNotDirectory/AllPlayers/keep.txt"));
}

TEST_F(StorageDeviceDeleteContainerTest, EmptyUnicodeDirectoryDeletionStillWorks)
{
    auto container = OpenContainer("UnicodeDeletion");
    const std::string name = "empty-\xc3\xa9-\xe6\x97\xa5";
    container->CreateDirectory(name);
    ASSERT_TRUE(container->DirectoryExists(name));
    container->DeleteDirectory(name);
    EXPECT_FALSE(container->DirectoryExists(name));
}

// CNA-DEMOS-ISSUE-152. Framework4 StorageContractsOracle pins the facade's exclusive
// defaults. The explicit sharing matrix uses the owner-approved native .NET platform split.
#if !defined(__EMSCRIPTEN__) // These checks require native OS sharing, not a browser virtual FS.
TEST_F(StorageDeviceDeleteContainerTest, FileSharingIsForwardedToTheNativeStream)
{
    using namespace System::IO;
    auto container = OpenContainer("FileSharing");
    { auto created = container->CreateFile("seed.bin"); created->WriteByte(42); }
    int refused = 0;
    for (auto firstAccess : {FileAccess::Read, FileAccess::Write, FileAccess::ReadWrite})
    for (auto firstShare : {FileShare::None, FileShare::Read, FileShare::Write, FileShare::ReadWrite})
    for (auto nextAccess : {FileAccess::Read, FileAccess::Write, FileAccess::ReadWrite})
    for (auto nextShare : {FileShare::None, FileShare::Read, FileShare::Write, FileShare::ReadWrite})
    {
        SCOPED_TRACE(::testing::Message() << static_cast<int>(firstAccess) << '/'
            << static_cast<int>(firstShare) << " -> " << static_cast<int>(nextAccess)
            << '/' << static_cast<int>(nextShare));
        auto first = container->OpenFile("seed.bin", FileMode::Open, firstAccess, firstShare);
        bool denied = false;
        try { auto next = container->OpenFile("seed.bin", FileMode::Open, nextAccess, nextShare); }
        catch (const IOException&) { denied = true; }
#if defined(_WIN32)
        const bool expected = (static_cast<int>(nextAccess) & ~static_cast<int>(firstShare)) != 0 ||
                              (static_cast<int>(firstAccess) & ~static_cast<int>(nextShare)) != 0;
#else
        const bool expected = firstShare == FileShare::None || nextShare == FileShare::None;
#endif
        EXPECT_EQ(denied, expected);
        if (denied) ++refused;
    }
#if defined(_WIN32)
    EXPECT_EQ(refused, 119);
#else
    EXPECT_EQ(refused, 63);
#endif
}

TEST_F(StorageDeviceDeleteContainerTest, ShortFileOverloadsAndCreateFileAreExclusive)
{
    using namespace System::IO;
    auto container = OpenContainer("ExclusiveDefaults");
    {
        auto created = container->CreateFile("seed.bin");
        EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read, FileShare::ReadWrite), IOException);
        created->WriteByte(42);
    }
    {
        auto first = container->OpenFile("seed.bin", FileMode::Open);
        EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read, FileShare::ReadWrite), IOException);
    }
    {
        auto first = container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read);
        EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read, FileShare::ReadWrite), IOException);
    }
}

TEST_F(StorageDeviceDeleteContainerTest, DeniedSharingDoesNotTruncateStoredData)
{
    using namespace System::IO;
    auto container = OpenContainer("ShareBeforeTruncate");
    { auto seed = container->CreateFile("seed.bin"); seed->WriteByte(42); }
    auto owner = container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read, FileShare::None);
    EXPECT_THROW((void)container->CreateFile("seed.bin"), IOException);
    EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Truncate, FileAccess::Write, FileShare::ReadWrite), IOException);
    EXPECT_EQ(owner->getLengthProperty(), 1);
    EXPECT_EQ(owner->ReadByte(), 42);
}
#endif

TEST_F(StorageDeviceDeleteContainerTest, TwoArgumentAppendUsesWriteOnlyAndPreservesThePrefix)
{
    using namespace System::IO;
    auto container = OpenContainer("AppendDefaultAccess");
    { auto seed = container->CreateFile("seed.bin"); seed->WriteByte(42); }
    auto append = container->OpenFile("seed.bin", FileMode::Append);
    EXPECT_TRUE(append->getCanWriteProperty());
    EXPECT_FALSE(append->getCanReadProperty());
    EXPECT_EQ(append->getPositionProperty(), 1);
    append->WriteByte(43);
    append->Close();
    auto read = container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read);
    EXPECT_EQ(read->getLengthProperty(), 2);
    EXPECT_EQ(read->ReadByte(), 42);
    EXPECT_EQ(read->ReadByte(), 43);
}

TEST_F(StorageDeviceDeleteContainerTest, DeleteFilePreservesDirectoriesAndTheirContents)
{
    auto container = OpenContainer("FileDeletion");
    container->CreateDirectory("directory");
    EXPECT_THROW(container->DeleteFile("directory"), System::UnauthorizedAccessException);
    EXPECT_TRUE(container->DirectoryExists("directory"));
    // Recreate only if the defective implementation deleted the first fixture.
    // The preservation assertion above must still fail in that case.
    container->CreateDirectory("directory");
    auto marker = container->CreateFile("directory/sentinel");
    marker->WriteByte(42);
    marker->Close();
    EXPECT_THROW(container->DeleteFile("directory"), System::UnauthorizedAccessException);
    EXPECT_TRUE(container->FileExists("directory/sentinel"));
}

TEST_F(StorageDeviceDeleteContainerTest, DeleteFileDistinguishesMissingParentAndMissingLeaf)
{
    auto container = OpenContainer("FileDeletion");
    EXPECT_THROW(container->DeleteFile("absent/file"), System::IO::DirectoryNotFoundException);
    EXPECT_NO_THROW(container->DeleteFile("absent-file"));
    auto file = container->CreateFile("real-file");
    file->WriteByte(42);
    file->Close();
    EXPECT_NO_THROW(container->DeleteFile("real-file"));
    EXPECT_FALSE(container->FileExists("real-file"));
}

TEST_F(StorageDeviceDeleteContainerTest, CreateDirectoryRefusesFilesAndPreservesTheirContents)
{
    auto container = OpenContainer("DirectoryCreation");
    auto file = container->CreateFile("seed.bin");
    file->WriteByte(42);
    file->Close();
    EXPECT_THROW(container->CreateDirectory("seed.bin"), System::IO::IOException);
    EXPECT_THROW(container->CreateDirectory("seed.bin/child"), System::IO::IOException);
    auto read = container->OpenFile("seed.bin", System::IO::FileMode::Open);
    EXPECT_EQ(read->ReadByte(), 42);
    EXPECT_EQ(read->getLengthProperty(), 1);
    EXPECT_NO_THROW(container->CreateDirectory("parent/child"));
    EXPECT_NO_THROW(container->CreateDirectory("parent/child"));
    EXPECT_TRUE(container->DirectoryExists("parent/child"));
}

TEST_F(StorageDeviceDeleteContainerTest, EnumerationOfVanishedRootUsesDirectoryNotFoundException)
{
    auto container = OpenContainer("VanishedDirectory");
    device_->DeleteContainer("VanishedDirectory");
    EXPECT_THROW((void)container->GetFileNames(), System::IO::DirectoryNotFoundException);
    EXPECT_THROW((void)container->GetFileNames("*"), System::IO::DirectoryNotFoundException);
    EXPECT_THROW((void)container->GetDirectoryNames(), System::IO::DirectoryNotFoundException);
    EXPECT_THROW((void)container->GetDirectoryNames("*"), System::IO::DirectoryNotFoundException);
    // Genuine XNA returns an empty result before accessing storage for an empty pattern.
    EXPECT_TRUE(container->GetFileNames("").empty());
    EXPECT_TRUE(container->GetDirectoryNames("").empty());
}

TEST_F(StorageDeviceDeleteContainerTest, EnumerationPreservesUnicodeNamesAndReturnsOnlyLeafNames)
{
    const std::string unicode = "\xe4\xb8\x96\xe7\x95\x8c";
    auto container = OpenContainer("Unicode-" + unicode);
    const auto directory = "directory-" + unicode;
    const auto file = "file-" + unicode + ".bin";
    container->CreateDirectory(directory);
    container->CreateFile(file)->Close();
    container->CreateFile(directory + "/nested.bin")->Close();
    EXPECT_EQ(container->GetFileNames(), std::vector<std::string>{file});
    EXPECT_EQ(container->GetFileNames("*.bin"), std::vector<std::string>{file});
    EXPECT_TRUE(container->GetFileNames("no-match*").empty());
    EXPECT_EQ(container->GetDirectoryNames(), std::vector<std::string>{directory});
    EXPECT_EQ(container->GetDirectoryNames("directory-*"), std::vector<std::string>{directory});
    EXPECT_TRUE(container->GetDirectoryNames("no-match*").empty());
}

// ---------------------------------------------------------------------------
// XNA-MISSING-014: StorageDeviceNotConnectedException's serialization constructor.
//
// XNA's body is `base(info, context)` and nothing else -- the type adds no state of its own -- so
// what has to survive the round trip is the exception base state
// (xna4-decomp/.../Microsoft.Xna.Framework.Storage/StorageDeviceNotConnectedException.cs).
// ---------------------------------------------------------------------------

namespace
{
    /// The constructor is protected, as .NET's ISerializable pattern has it, so a derived type is
    /// the only way to reach it -- which is also the accessibility this asserts.
    struct TestableStorageDeviceNotConnectedException
        : Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException
    {
        TestableStorageDeviceNotConnectedException(
            const System::Runtime::Serialization::SerializationInfo& info,
            const System::Runtime::Serialization::StreamingContext& context)
            : StorageDeviceNotConnectedException(info, context)
        {
        }
    };
}

TEST(StorageDeviceNotConnectedExceptionSerializationTest, TheBaseStateSurvivesARoundTrip)
{
    using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;

    const StorageDeviceNotConnectedException original("the card was removed");
    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    original.GetObjectData(info, context);

    const TestableStorageDeviceNotConnectedException restored(info, context);
    EXPECT_EQ(restored.getMessageProperty(), "the card was removed");
    EXPECT_EQ(restored.getInnerExceptionProperty(), nullptr);
}

TEST(StorageDeviceNotConnectedExceptionSerializationTest, TheInnerCauseTravelsWithIt)
{
    using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;

    std::exception_ptr cause;
    try
    {
        throw std::runtime_error("the mount point vanished");
    }
    catch (...)
    {
        cause = std::current_exception();
    }

    const StorageDeviceNotConnectedException original("device lost", cause);
    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    original.GetObjectData(info, context);

    const TestableStorageDeviceNotConnectedException restored(info, context);
    EXPECT_EQ(restored.getMessageProperty(), "device lost");
    ASSERT_NE(restored.getInnerExceptionProperty(), nullptr);
    try
    {
        std::rethrow_exception(restored.getInnerExceptionProperty());
        FAIL() << "the restored cause must rethrow";
    }
    catch (const std::runtime_error& inner)
    {
        EXPECT_EQ(std::string(inner.what()), "the mount point vanished");
    }
}

TEST(StorageDeviceNotConnectedExceptionSerializationTest, AnEmptyStoreStillConstructs)
{
    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    const TestableStorageDeviceNotConnectedException restored(info, context);
    EXPECT_TRUE(restored.getMessageProperty().empty());
    EXPECT_EQ(restored.getInnerExceptionProperty(), nullptr);
}

TEST(StorageDeviceNotConnectedExceptionSerializationTest, TheRestoredExceptionIsStillItsOwnType)
{
    using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;

    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    StorageDeviceNotConnectedException("typed").GetObjectData(info, context);

    const TestableStorageDeviceNotConnectedException restored(info, context);
    EXPECT_NE(dynamic_cast<const StorageDeviceNotConnectedException*>(&restored), nullptr);
    EXPECT_NE(dynamic_cast<const System::Runtime::InteropServices::ExternalException*>(&restored),
              nullptr);
    EXPECT_THROW({ throw restored; }, StorageDeviceNotConnectedException);
}
