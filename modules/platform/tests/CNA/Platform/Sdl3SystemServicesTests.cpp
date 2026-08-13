// SPDX-License-Identifier: MS-PL
//
// PLAT-43/44 and the system-information half of PLAT-107, against real SDL3.
//
// Most of these need no video subsystem, so they run everywhere. The display tests do, and skip
// when a container cannot provide one.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3ServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
    }

    std::unique_ptr<IPlatform> platform_;
};

// --- services are present, because SDL3 advertises them ---------------------------------------------

TEST_F(Sdl3ServiceTest, ServicesTheCapabilitySetClaimsAreActuallyReturned)
{
    // The contract says a service is null exactly when its capability is false. A platform that
    // advertised clipboard support and then returned null would send a caller down a null
    // dereference, so the two must agree.
    const PlatformCapabilities capabilities = platform_->GetCapabilities();

    EXPECT_EQ(platform_->GetClipboard() != nullptr, capabilities.clipboard);
    EXPECT_EQ(platform_->GetDisplays() != nullptr, capabilities.multipleDisplays);
    EXPECT_NE(platform_->GetFileSystem(), nullptr) << "every platform can resolve paths";
    EXPECT_NE(platform_->GetSystemInfo(), nullptr) << "system info is always answerable";
}

// --- filesystem (PLAT-44) -----------------------------------------------------------------------------

TEST_F(Sdl3ServiceTest, BasePathIsNonEmptyAndExists)
{
    const std::string base = platform_->GetFileSystem()->GetBasePath();
    ASSERT_FALSE(base.empty());
    EXPECT_TRUE(std::filesystem::exists(base)) << base;
}

TEST_F(Sdl3ServiceTest, PreferencesPathIsCreatedAndWritable)
{
    // Backs StorageDevice, so it must exist after the call rather than merely being named.
    const std::string prefs =
        platform_->GetFileSystem()->GetPreferencesPath("openeggbert-test", "cna-platform-test");
    ASSERT_FALSE(prefs.empty());
    EXPECT_TRUE(std::filesystem::exists(prefs)) << prefs;

    const std::filesystem::path probe = std::filesystem::path(prefs) / "writable.probe";
    {
        std::ofstream out(probe);
        ASSERT_TRUE(out.good());
        out << "x";
    }
    EXPECT_TRUE(std::filesystem::exists(probe));
    std::filesystem::remove(probe);
}

TEST_F(Sdl3ServiceTest, LoadFileReadsContentsExactly)
{
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() / "cna_platform_loadfile.bin";
    // The length is explicit: the const char* constructor would stop at the embedded NUL, leaving
    // a 3-character string whose buffer this then wrote 7 bytes out of.
    const std::string contents("abc\0def", 7);
    {
        std::ofstream out(temporary, std::ios::binary);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::vector<std::uint8_t> data;
    ASSERT_TRUE(platform_->GetFileSystem()->TryLoadFile(temporary.string(), data));
    ASSERT_EQ(data.size(), 7u);
    EXPECT_EQ(data[3], 0u) << "an embedded NUL must survive; this is a byte loader";
    EXPECT_EQ(data[6], static_cast<std::uint8_t>('f'));

    std::filesystem::remove(temporary);
}

TEST_F(Sdl3ServiceTest, MissingFileReturnsFalseRatherThanThrowing)
{
    // A missing file is an ordinary outcome a caller branches on -- PLAT-21's throw-vs-status
    // split. Throwing here would turn normal control flow into exceptions.
    std::vector<std::uint8_t> data{1, 2, 3};
    EXPECT_FALSE(platform_->GetFileSystem()->TryLoadFile("/no/such/file/anywhere.bin", data));
    EXPECT_EQ(data.size(), 3u) << "the output must be untouched on failure";
}

TEST_F(Sdl3ServiceTest, CreateDirectoryMakesNestedDirectories)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cna_platform_mkdir_probe";
    std::filesystem::remove_all(root);

    const std::filesystem::path nested = root / "a" / "b";
    platform_->GetFileSystem()->CreateDirectory(nested.string());
    EXPECT_TRUE(std::filesystem::is_directory(nested));

    // Creating an existing directory must be idempotent, not an error: callers create their save
    // directory on every launch.
    EXPECT_NO_THROW(platform_->GetFileSystem()->CreateDirectory(nested.string()));

    std::filesystem::remove_all(root);
}

// --- system information -------------------------------------------------------------------------------

TEST_F(Sdl3ServiceTest, PlatformNameIsReported)
{
    EXPECT_FALSE(platform_->GetSystemInfo()->GetPlatformName().empty());
}

TEST_F(Sdl3ServiceTest, CoreCountAndMemoryArePositive)
{
    EXPECT_GT(platform_->GetSystemInfo()->GetLogicalCoreCount(), 0);
    EXPECT_GT(platform_->GetSystemInfo()->GetSystemMemoryMegabytes(), 0);
}

TEST_F(Sdl3ServiceTest, PowerInfoKeepsUnknownDistinguishableFromEmpty)
{
    // A container usually reports no battery. What matters is that "unknown" stays -1 rather
    // than collapsing to 0, which would read as a battery about to die.
    const PowerInfo info = platform_->GetSystemInfo()->GetPowerInfo();
    EXPECT_TRUE(info.percent == -1 || (info.percent >= 0 && info.percent <= 100)) << info.percent;
    EXPECT_TRUE(info.secondsRemaining == -1 || info.secondsRemaining >= 0);
}

TEST_F(Sdl3ServiceTest, PreferredLocalesAreWellFormedWhenPresent)
{
    // A container may report none; the assertion is on shape, not on presence.
    for (const PlatformLocale& locale : platform_->GetSystemInfo()->GetPreferredLocales())
    {
        // Structured rather than a formatted tag: the language is what identifies a locale, and
        // a reported entry with an empty one would be meaningless.
        EXPECT_FALSE(locale.language.empty());
        EXPECT_EQ(locale.language.find('-'), std::string::npos)
            << "language and country stay separate; this is not a BCP 47 tag";
    }
}

// --- displays (PLAT-43) ---------------------------------------------------------------------------------

class Sdl3DisplayTest : public Sdl3ServiceTest
{
protected:
    void SetUp() override
    {
        Sdl3ServiceTest::SetUp();
        if (IsSkipped())
        {
            return;
        }
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no video subsystem available here: " << error.what();
        }
        acquired_ = true;
    }

    void TearDown() override
    {
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
    }

    bool acquired_ = false;
};

TEST_F(Sdl3DisplayTest, EnumeratedDisplaysAreWellFormed)
{
    for (const DisplayInfo& display : platform_->GetDisplays()->GetDisplays())
    {
        EXPECT_NE(display.id, 0u);
        EXPECT_GT(display.width, 0);
        EXPECT_GT(display.height, 0);
        // Never zero: a zero content scale divides to infinity in any layout computation.
        EXPECT_GT(display.contentScale, 0.0f);
    }
}

TEST_F(Sdl3DisplayTest, DisplayForAWindowMatchesAnEnumeratedDisplay)
{
    WindowDescription description;
    description.title = "display probe";
    description.visible = false;
    std::unique_ptr<IPlatformWindow> window;
    try
    {
        window = platform_->CreateWindow(description);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "window creation unavailable here: " << error.what();
    }

    DisplayInfo found;
    if (!platform_->GetDisplays()->TryGetDisplayForWindow(*window, found))
    {
        GTEST_SKIP() << "this video driver reports no display for a window";
    }

    const std::vector<DisplayInfo> all = platform_->GetDisplays()->GetDisplays();
    EXPECT_TRUE(std::any_of(all.begin(), all.end(),
                            [&](const DisplayInfo& candidate) { return candidate.id == found.id; }))
        << "a window's display must be one of the enumerated displays";
}

TEST_F(Sdl3DisplayTest, SafeAreaForAWindowIsReportedInClientCoordinates)
{
    WindowDescription description;
    description.title = "safe-area probe";
    description.width = 160;
    description.height = 90;
    description.visible = false;
    std::unique_ptr<IPlatformWindow> window;
    try
    {
        window = platform_->CreateWindow(description);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "window creation unavailable here: " << error.what();
    }

    WindowBounds safeArea;
    if (!platform_->GetDisplays()->TryGetSafeAreaForWindow(*window, safeArea))
    {
        GTEST_SKIP() << "this video driver reports no safe area for a window";
    }

    EXPECT_GE(safeArea.x, 0);
    EXPECT_GE(safeArea.y, 0);
    EXPECT_GT(safeArea.width, 0);
    EXPECT_GT(safeArea.height, 0);
    EXPECT_LE(safeArea.x + safeArea.width, description.width);
    EXPECT_LE(safeArea.y + safeArea.height, description.height);
}

TEST_F(Sdl3DisplayTest, UnknownDisplayIdYieldsNoModesRatherThanThrowing)
{
    EXPECT_TRUE(platform_->GetDisplays()->GetDisplayModes(0xDEADBEEF).empty());
}

TEST_F(Sdl3DisplayTest, CurrentModeMatchesAnEnumeratedDisplay)
{
    const std::vector<DisplayInfo> displays = platform_->GetDisplays()->GetDisplays();
    if (displays.empty())
    {
        GTEST_SKIP() << "this video driver reports no displays";
    }

    DisplayMode current;
    ASSERT_TRUE(platform_->GetDisplays()->TryGetCurrentDisplayMode(displays.front().id, current));
    EXPECT_GT(current.width, 0);
    EXPECT_GT(current.height, 0);
    EXPECT_GE(current.refreshRate, 0.0f);
}

TEST_F(Sdl3DisplayTest, UnknownDisplayLeavesCurrentModeOutputUntouched)
{
    DisplayMode current{123, 456, 78.0f};
    EXPECT_FALSE(platform_->GetDisplays()->TryGetCurrentDisplayMode(0xDEADBEEF, current));
    EXPECT_EQ(current.width, 123);
    EXPECT_EQ(current.height, 456);
    EXPECT_FLOAT_EQ(current.refreshRate, 78.0f);
}

// --- dialogs (PLAT-103) -----------------------------------------------------------------------

TEST_F(Sdl3ServiceTest, TheDialogServiceIsPresentBecauseMessageBoxIsSupported)
{
    EXPECT_NE(platform_->GetDialogs(), nullptr);
    EXPECT_TRUE(platform_->GetCapabilities().messageBox);
}

TEST_F(Sdl3ServiceTest, FileDialogsAreSupportedAndRejectAMissingCallback)
{
    // The dialogs are asynchronous: the call returns immediately and the answer arrives later, so
    // a call with nowhere to deliver it would show a dialog whose result is discarded. That is
    // worse than refusing, because by then the user has already been interrupted.
    IPlatformDialogs* dialogs = platform_->GetDialogs();
    ASSERT_NE(dialogs, nullptr);
    ASSERT_TRUE(platform_->GetCapabilities().nativeFileDialog);

    const std::vector<FileDialogFilter> filters{{"Images", "png;jpg"}};

    EXPECT_THROW(dialogs->ShowOpenFileDialog(nullptr, filters, "", false, nullptr),
                 PlatformException);
    EXPECT_THROW(dialogs->ShowSaveFileDialog(nullptr, filters, "", nullptr), PlatformException);
    EXPECT_THROW(dialogs->ShowOpenFolderDialog(nullptr, "", false, nullptr), PlatformException);
}

TEST_F(Sdl3ServiceTest, AMessageBoxWithNoButtonsIsRejectedRatherThanShown)
{
    // A dialog with nothing to click cannot be dismissed. Rejecting before showing keeps that
    // from becoming a hung process on a machine nobody is watching.
    IPlatformDialogs* dialogs = platform_->GetDialogs();
    ASSERT_NE(dialogs, nullptr);

    EXPECT_THROW(
        (void)dialogs->ShowMessageBoxWithButtons(MessageBoxSeverity::Information, "t", "m", {},
                                                 nullptr),
        PlatformException);
}

} // namespace
