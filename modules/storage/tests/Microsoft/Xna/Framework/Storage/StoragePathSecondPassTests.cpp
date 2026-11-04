// SPDX-License-Identifier: MS-PL
#include "StorageSecondPassFixture.hpp"
#include <functional>
#include <vector>
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace Tq042
{
TEST_F(StorageSecondPassTest, EscapeErrorsNameEachPublicArgumentBeforeOpenOptions)
{
    using namespace System::IO;
    // XNA StorageContainer.ValidateArguments IL_0000-0038; each public route
    // supplies "file"/"directory" at IL_0002. Invalid open enums are reached later.
    // Absolute paths below the root are a deliberate CNA restriction; only outside paths here.
    for (const std::string& path : {std::string("../../outside"), CNA::Internal::PathToUtf8(scratch / "outside")})
    {
        SCOPED_TRACE(path);
        Exact<System::ArgumentException>([&] { container->CreateDirectory(path); }, "directory");
        Exact<System::ArgumentException>([&] { container->DeleteDirectory(path); }, "directory");
        Exact<System::ArgumentException>([&] { (void)container->DirectoryExists(path); }, "directory");
        Exact<System::ArgumentException>([&] { (void)container->CreateFile(path); }, "file");
        Exact<System::ArgumentException>([&] { container->DeleteFile(path); }, "file");
        Exact<System::ArgumentException>([&] { (void)container->FileExists(path); }, "file");
        Exact<System::ArgumentException>([&] { (void)container->OpenFile(path, static_cast<FileMode>(-1)); }, "file");
        Exact<System::ArgumentException>([&] { (void)container->OpenFile(path, static_cast<FileMode>(-1), static_cast<FileAccess>(-1)); }, "file");
        Exact<System::ArgumentException>([&] { (void)container->OpenFile(path, static_cast<FileMode>(-1), static_cast<FileAccess>(-1), static_cast<FileShare>(-1)); }, "file");
    }
    SeedUnchanged();
    EXPECT_FALSE(fs::exists(scratch / "outside"));
    EXPECT_FALSE(fs::exists(app / "outside"));
}

TEST_F(StorageSecondPassTest, EveryDisposedRouteNamesTheObjectBeforeInvalidInput)
{
    using namespace System::IO;
    // XNA VerifyNotDisposed IL_0008-0018 uses GetType().Name, not an empty name.
    std::vector<std::function<void()>> calls{
        [&] { (void)container->getDisplayNameProperty(); },
        [&] { (void)container->getStorageDeviceProperty(); },
        [&] { container->CreateDirectory(""); }, [&] { container->DeleteDirectory(""); },
        [&] { (void)container->DirectoryExists(""); },
        [&] { (void)container->CreateFile(""); }, [&] { container->DeleteFile(""); },
        [&] { (void)container->FileExists(""); },
        [&] { (void)container->GetDirectoryNames(); }, [&] { (void)container->GetDirectoryNames("../*"); },
        [&] { (void)container->GetFileNames(); }, [&] { (void)container->GetFileNames("../*"); },
        [&] { (void)container->OpenFile("", static_cast<FileMode>(-1)); },
        [&] { (void)container->OpenFile("", static_cast<FileMode>(-1), static_cast<FileAccess>(-1)); },
        [&] { (void)container->OpenFile("", static_cast<FileMode>(-1), static_cast<FileAccess>(-1), static_cast<FileShare>(-1)); }
    };
    container->Dispose();
    for (std::size_t i = 0; i < calls.size(); ++i)
    {
        SCOPED_TRACE(i);
        Exact<System::ObjectDisposedException>(calls[i]);
    }
    SeedUnchanged();
}

TEST_F(StorageSecondPassTest, TrailingDirectorySeparatorsKeepDirectoryIdentity)
{
    // XNA CreateDirectory/DirectoryExists/DeleteDirectory IL_0007 validate then
    // delegate to System.IO. Both Windows separators denote the existing child.
    container->CreateDirectory("child/");
    EXPECT_TRUE(container->DirectoryExists("child\\"));
    EXPECT_FALSE(container->FileExists("child/"));
    container->DeleteDirectory("child\\");
    EXPECT_FALSE(fs::exists(root / "child"));
    SeedUnchanged();
}
}
#endif
