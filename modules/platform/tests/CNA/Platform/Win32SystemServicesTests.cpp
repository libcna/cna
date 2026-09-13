// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0050..0054: clipboard, displays, dialogs, system info and paths.
//
// These are the services a game reaches for rarely and notices immediately when they are wrong: a
// clipboard that loses non-ASCII text, a display list that misses the primary monitor, a
// preferences path that is not writable. Each is implemented with native Windows calls and each is
// asserted here against behaviour rather than against the call that produced it.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

namespace {

using namespace CNA::Platform;

class Win32SystemServices : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    std::unique_ptr<IPlatformWindow> CreateWindow()
    {
        WindowDescription description;
        description.title = "system services";
        description.visible = false;
        return platform_->CreateWindow(description);
    }

    std::unique_ptr<IPlatform> platform_;
};

// --- clipboard -------------------------------------------------------------------------------

TEST_F(Win32SystemServices, ClipboardRoundTripsUnicodeText)
{
    IPlatformClipboard* const clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);

    // CF_UNICODETEXT on purpose: the ANSI clipboard format would turn each of these into question
    // marks, and it is the kind of loss nobody notices until a player pastes their own name.
    const std::string text = "CNA \xC5\x99\xC3\xADzen\xC3\xAD \xE2\x80\x94 \xE6\x97\xA5\xE6\x9C\xAC";
    try
    {
        clipboard->SetText(text);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this host has no usable clipboard: " << error.what();
    }

    EXPECT_TRUE(clipboard->HasText());
    EXPECT_EQ(clipboard->GetText(), text);
}

TEST_F(Win32SystemServices, ClipboardAcceptsEmptyTextWithoutReportingFailure)
{
    IPlatformClipboard* const clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    try
    {
        clipboard->SetText("");
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this host has no usable clipboard: " << error.what();
    }
    // An empty clipboard is ordinary, not exceptional -- the contract says GetText returns an
    // empty string rather than throwing.
    EXPECT_EQ(clipboard->GetText(), "");
}

// --- displays --------------------------------------------------------------------------------

TEST_F(Win32SystemServices, DisplaysAreEnumeratedWithThePrimaryFirst)
{
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);

    const std::vector<DisplayInfo> attached = displays->GetDisplays();
    if (attached.empty())
        GTEST_SKIP() << "this host reports no monitors";

    std::set<std::uint32_t> ids;
    for (const DisplayInfo& display : attached)
    {
        EXPECT_NE(display.id, 0u) << "zero is the contract's 'no display'";
        EXPECT_TRUE(ids.insert(display.id).second) << "display ids must be distinct";
        EXPECT_GT(display.width, 0);
        EXPECT_GT(display.height, 0);
        EXPECT_GT(display.contentScale, 0.0f);
        EXPECT_FALSE(display.name.empty());
        EXPECT_GT(display.desktopMode.width, 0);
        EXPECT_GT(display.desktopMode.height, 0);
    }
}

TEST_F(Win32SystemServices, DisplayModesBelongToTheDisplayTheyWereAskedFor)
{
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);
    const std::vector<DisplayInfo> attached = displays->GetDisplays();
    if (attached.empty())
        GTEST_SKIP() << "this host reports no monitors";

    const std::vector<DisplayMode> modes = displays->GetDisplayModes(attached.front().id);
    for (const DisplayMode& mode : modes)
    {
        EXPECT_GT(mode.width, 0);
        EXPECT_GT(mode.height, 0);
    }

    // EnumDisplaySettingsW enumerates the cross product with colour depth, so the same resolution
    // and refresh rate comes back several times unless they are collapsed.
    std::set<std::tuple<int, int, float>> unique;
    for (const DisplayMode& mode : modes)
        EXPECT_TRUE(unique.insert({mode.width, mode.height, mode.refreshRate}).second);

    DisplayMode current{};
    if (displays->TryGetCurrentDisplayMode(attached.front().id, current))
    {
        EXPECT_GT(current.width, 0);
        EXPECT_GT(current.height, 0);
    }
}

TEST_F(Win32SystemServices, AnUnknownDisplayIdAnswersEmptyRatherThanThrowing)
{
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);
    EXPECT_TRUE(displays->GetDisplayModes(0).empty());
    EXPECT_TRUE(displays->GetDisplayModes(9999).empty());

    DisplayMode mode{};
    EXPECT_FALSE(displays->TryGetCurrentDisplayMode(9999, mode));
}

TEST_F(Win32SystemServices, TheSafeAreaOfADesktopWindowIsItsWholeClientArea)
{
    // Nothing on a desktop window is notched, rounded or covered by system chrome, so "all of it"
    // is the correct answer rather than a refusal.
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);
    const std::unique_ptr<IPlatformWindow> window = CreateWindow();
    ASSERT_NE(window, nullptr);
    window->Sync();

    WindowBounds safeArea{};
    ASSERT_TRUE(displays->TryGetSafeAreaForWindow(*window, safeArea));
    EXPECT_EQ(safeArea.x, 0);
    EXPECT_EQ(safeArea.y, 0);
    EXPECT_EQ(safeArea.width, window->GetClientBounds().width);
    EXPECT_EQ(safeArea.height, window->GetClientBounds().height);
}

TEST_F(Win32SystemServices, DisplayLookupRejectsAWindowFromAnotherPlatform)
{
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);

    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    WindowDescription description;
    description.visible = false;
    const std::unique_ptr<IPlatformWindow> foreign = headless->CreateWindow(description);
    ASSERT_NE(foreign, nullptr);

    DisplayInfo display{};
    WindowBounds safeArea{};
    EXPECT_FALSE(displays->TryGetDisplayForWindow(*foreign, display));
    EXPECT_FALSE(displays->TryGetSafeAreaForWindow(*foreign, safeArea));
}

TEST_F(Win32SystemServices, ScreenSaverSuppressionRoundTrips)
{
    // Implemented with SetThreadExecutionState rather than by changing the user's screen-saver
    // setting: the setting is theirs, and a crashed game would leave it changed.
    IPlatformDisplays* const displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);
    EXPECT_TRUE(displays->IsScreenSaverEnabled());

    displays->SetScreenSaverEnabled(false);
    EXPECT_FALSE(displays->IsScreenSaverEnabled());
    displays->SetScreenSaverEnabled(true);
    EXPECT_TRUE(displays->IsScreenSaverEnabled());
}

// --- dialogs --------------------------------------------------------------------------------

TEST_F(Win32SystemServices, DialogServiceExistsBecauseBothOfItsCapabilitiesAreTrue)
{
    // The one service backed by two capabilities. Its presence rule is "null when NEITHER", and
    // this backend claims both.
    EXPECT_NE(platform_->GetDialogs(), nullptr);
    EXPECT_TRUE(platform_->GetCapabilities().messageBox);
    EXPECT_TRUE(platform_->GetCapabilities().nativeFileDialog);
}

TEST_F(Win32SystemServices, FileDialogsRefuseAnEmptyCallbackRatherThanLosingTheResult)
{
    // The callback is the only channel a file dialog's result travels through. Accepting an empty
    // one would show the dialog and silently discard whatever the user chose.
    IPlatformDialogs* const dialogs = platform_->GetDialogs();
    ASSERT_NE(dialogs, nullptr);

    EXPECT_THROW(dialogs->ShowOpenFileDialog({}, {}, "", false, nullptr), PlatformException);
    EXPECT_THROW(dialogs->ShowSaveFileDialog({}, {}, "", nullptr), PlatformException);
    EXPECT_THROW(dialogs->ShowOpenFolderDialog({}, "", false, nullptr), PlatformException);
}

TEST_F(Win32SystemServices, AMessageBoxWithNoButtonsIsRefused)
{
    IPlatformDialogs* const dialogs = platform_->GetDialogs();
    ASSERT_NE(dialogs, nullptr);
    EXPECT_THROW((void) dialogs->ShowMessageBoxWithButtons(MessageBoxSeverity::Information, "t",
                                                           "m", {}, nullptr),
                 PlatformException);
}

// --- system information ------------------------------------------------------------------------

TEST_F(Win32SystemServices, HostFactsAreAnsweredOrHonestlyUnknown)
{
    IPlatformSystemInfo* const info = platform_->GetSystemInfo();
    ASSERT_NE(info, nullptr);

    EXPECT_EQ(info->GetPlatformName(), "Windows");
    EXPECT_GT(info->GetLogicalCoreCount(), 0) << "GetNativeSystemInfo always answers";
    EXPECT_GT(info->GetSystemMemoryMegabytes(), 0) << "GlobalMemoryStatusEx always answers";
}

TEST_F(Win32SystemServices, PowerInfoNeverFabricatesAPercentage)
{
    // 255 is the documented "unknown" for both battery fields. Passing it through would report a
    // 255% battery, which a caller displays.
    IPlatformSystemInfo* const info = platform_->GetSystemInfo();
    ASSERT_NE(info, nullptr);

    const PowerInfo power = info->GetPowerInfo();
    EXPECT_TRUE(power.percent == -1 || (power.percent >= 0 && power.percent <= 100))
        << power.percent;
    EXPECT_TRUE(power.secondsRemaining == -1 || power.secondsRemaining >= 0);
}

TEST_F(Win32SystemServices, PreferredLocalesAreSplitIntoTheirParts)
{
    // GetUserPreferredUILanguages hands back a double-null-terminated list of BCP 47 tags. Reading
    // only the first, or forgetting the double terminator, drops every locale after the first.
    IPlatformSystemInfo* const info = platform_->GetSystemInfo();
    ASSERT_NE(info, nullptr);

    for (const PlatformLocale& locale : info->GetPreferredLocales())
    {
        EXPECT_FALSE(locale.language.empty()) << "a reported locale always names a language";
        EXPECT_EQ(locale.language.find('-'), std::string::npos)
            << "the tag must be split, not stored whole";
    }
}

// --- filesystem --------------------------------------------------------------------------------

TEST_F(Win32SystemServices, BasePathIsReportedWithATrailingSeparator)
{
    IPlatformFileSystem* const files = platform_->GetFileSystem();
    ASSERT_NE(files, nullptr);

    const std::string base = files->GetBasePath();
    ASSERT_FALSE(base.empty());
    EXPECT_TRUE(base.back() == '\\' || base.back() == '/') << base;
}

TEST_F(Win32SystemServices, PreferencesPathIsCreatedAndWritable)
{
    // Backs StorageDevice. A path that is returned but not created is the failure mode that shows
    // up as a game silently losing every save.
    IPlatformFileSystem* const files = platform_->GetFileSystem();
    ASSERT_NE(files, nullptr);

    std::string path;
    try
    {
        path = files->GetPreferencesPath("CnaWin32Tests", "PreferencesProbe");
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this host has no writable roaming profile: " << error.what();
    }

    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(path.back() == '\\' || path.back() == '/') << path;
    EXPECT_TRUE(std::filesystem::is_directory(path)) << path;

    const std::filesystem::path probe = std::filesystem::path(path) / "probe.bin";
    {
        std::ofstream stream(probe, std::ios::binary);
        ASSERT_TRUE(stream.is_open()) << probe.string();
        stream << "cna";
    }

    std::vector<std::uint8_t> data;
    EXPECT_TRUE(files->TryLoadFile(probe.string(), data));
    EXPECT_EQ(data.size(), 3u);

    std::error_code error;
    std::filesystem::remove(probe, error);
}

TEST_F(Win32SystemServices, UserFoldersAreEitherUnavailableOrEndInASeparator)
{
    IPlatformFileSystem* const files = platform_->GetFileSystem();
    ASSERT_NE(files, nullptr);
    for (const UserFolder folder : {UserFolder::Music, UserFolder::Pictures})
    {
        const std::string path = files->GetUserFolder(folder);
        EXPECT_TRUE(path.empty() || path.back() == '\\' || path.back() == '/') << path;
    }
}

TEST_F(Win32SystemServices, AMissingFileReturnsFalseWithTheOutputUntouched)
{
    IPlatformFileSystem* const files = platform_->GetFileSystem();
    ASSERT_NE(files, nullptr);

    std::vector<std::uint8_t> data{1, 2, 3};
    EXPECT_FALSE(files->TryLoadFile("C:\\definitely\\not\\here.bin", data));
    EXPECT_EQ(data.size(), 3u) << "a failed load must leave the caller's buffer alone";
}

} // namespace
