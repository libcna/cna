// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0169: file dialogs and URL opening through the desktop portal.
//
// No test reaches the session bus of the desktop it runs on: a file chooser opened there would open
// on that desktop. This binary gives itself no session bus as it loads (below), ctest and the X11
// test launcher say the same, and every test that wants a portal starts a private dbus-daemon --
// with no service it could start, so no real portal can appear on it -- and plays the portal on it
// itself.

#include <gtest/gtest.h>

#include "../../../src/Freedesktop/DesktopPortal.hpp"
#include "../../../src/X11/X11MessageBox.hpp"
#include "../../../src/X11/X11Headers.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "System/Environment.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(CNA_PLATFORM_HAVE_DBUS)
#include "FreedesktopFakePortal.hpp"
#include "FreedesktopPrivateSessionBus.hpp"

#include <unistd.h>
#endif

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11;
using namespace CNA::Platform::Freedesktop;
#if defined(CNA_PLATFORM_HAVE_DBUS)
using CNA::Platform::Freedesktop::Testing::FakePortal;
using CNA::Platform::Freedesktop::Testing::PortalAnswer;
using CNA::Platform::Freedesktop::Testing::PortalCall;
using CNA::Platform::Freedesktop::Testing::PrivateBus;
#endif

// Before any test runs, and whatever started the binary: no session bus.
const bool kNoDesktopSessionBus = [] {
    System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS",
                                                std::string("unix:path=/nonexistent/cna-test-no-session-bus"));
    return true;
}();

TEST(X11PortalRequest, NoTestOfThisBinaryReachesTheDesktopsSessionBus)
{
    // Whatever environment the binary was started in -- a developer's shell has their desktop's
    // bus in it -- a test sees none.
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    ASSERT_NE(address, nullptr);
    EXPECT_STREQ(address, "unix:path=/nonexistent/cna-test-no-session-bus");
#if defined(CNA_PLATFORM_HAVE_DBUS)
    EXPECT_EQ(DesktopPortal::Connect(), nullptr);
#endif
}

// --- what goes on the bus, worked out without one ---------------------------------------------

TEST(X11PortalRequest, AnExtensionBecomesACaseInsensitiveGlob)
{
    EXPECT_EQ(PortalGlob("png"), "*.[pP][nN][gG]");
    EXPECT_EQ(PortalGlob("tar.gz"), "*.[tT][aA][rR].[gG][zZ]");
    EXPECT_EQ(PortalGlob("mp3"), "*.[mM][pP]3");
    EXPECT_EQ(PortalGlob("*"), "*") << "everything stays everything";
}

TEST(X11PortalRequest, FiltersSplitTheirPatternsAndDropTheEmpty)
{
    const std::vector<PortalFilter> filters =
        ToPortalFilters({{"Images", "png;jpg"}, {"Everything", "*"}, {"Nothing", ";; "}, {"Spaced", " txt ; *.md"}});
    ASSERT_EQ(filters.size(), 3u) << "a filter with no pattern is not offered";
    EXPECT_EQ(filters[0].name, "Images");
    ASSERT_EQ(filters[0].patterns.size(), 2u);
    EXPECT_EQ(filters[0].patterns[0].kind, 0u);
    EXPECT_EQ(filters[0].patterns[0].pattern, "*.[pP][nN][gG]");
    EXPECT_EQ(filters[0].patterns[1].pattern, "*.[jJ][pP][gG]");
    EXPECT_EQ(filters[1].patterns[0].pattern, "*");
    ASSERT_EQ(filters[2].patterns.size(), 2u);
    EXPECT_EQ(filters[2].patterns[0].pattern, "*.[tT][xX][tT]");
    EXPECT_EQ(filters[2].patterns[1].pattern, "*.[mM][dD]") << "a pattern written as *.md is the same extension";
}

TEST(X11PortalRequest, AFileUriNamesALocalPathPercentDecoded)
{
    EXPECT_EQ(PathFromFileUri("file:///home/user/a%20b.png"), "/home/user/a b.png");
    EXPECT_EQ(PathFromFileUri("file://localhost/tmp/x"), "/tmp/x");
    EXPECT_EQ(PathFromFileUri("FILE:///tmp/%C5%BE"), "/tmp/\xC5\xBE");
    EXPECT_EQ(PathFromFileUri("file:///tmp/x?query#fragment"), "/tmp/x");
    EXPECT_EQ(PathFromFileUri("file://elsewhere/tmp/x"), std::nullopt) << "another machine's file";
    EXPECT_EQ(PathFromFileUri("https://example.com/"), std::nullopt);
    EXPECT_EQ(PathFromFileUri("file:///tmp/%00"), std::nullopt) << "an encoded NUL names no path";
    EXPECT_EQ(PathFromFileUri("file:///tmp/%zz"), std::nullopt);
    EXPECT_EQ(PathFromFileUri("file:///tmp/%2"), std::nullopt);
}

TEST(X11PortalRequest, TheRequestPathIsBuiltFromTheSenderAndTheToken)
{
    EXPECT_EQ(PortalRequestPath(":1.42", "cna7"), "/org/freedesktop/portal/desktop/request/1_42/cna7");
    EXPECT_EQ(PortalParentWindow(0x1a00003), "x11:1a00003");
    EXPECT_EQ(PortalParentWindow(0), "") << "no parent";
}

TEST(X11PortalRequest, ASaveLocationIsAFolderAFileOrASuggestedName)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("cna-portal-save-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "existing.txt") << "x";

    PortalSaveLocation folder = SplitSaveLocation(directory.string());
    EXPECT_EQ(folder.currentFolder, directory.string());
    EXPECT_TRUE(folder.currentName.empty());

    PortalSaveLocation existing = SplitSaveLocation((directory / "existing.txt").string());
    EXPECT_EQ(existing.currentFile, (directory / "existing.txt").string());
    EXPECT_EQ(existing.currentName, "existing.txt");

    PortalSaveLocation suggested = SplitSaveLocation((directory / "new.sav").string());
    EXPECT_EQ(suggested.currentFolder, directory.string());
    EXPECT_EQ(suggested.currentName, "new.sav");
    EXPECT_TRUE(suggested.currentFile.empty());

    PortalSaveLocation bare = SplitSaveLocation("slot1.sav");
    EXPECT_TRUE(bare.currentFolder.empty());
    EXPECT_EQ(bare.currentName, "slot1.sav");

    EXPECT_TRUE(SplitSaveLocation("").currentName.empty());
    std::filesystem::remove_all(directory);
}

#if defined(CNA_PLATFORM_HAVE_DBUS)

// --- a private bus and a portal played on it --------------------------------------------------
//
// The portal itself is FreedesktopFakePortal.hpp, shared with the Wayland backend's portal test
// (plans/plan_wayland.md WAYLAND-0091): the same portal, asked the same questions, so the two
// backends' answers are comparable.

/// A private bus, the portal on it, and the session bus variable pointing there while it lasts.
class X11DesktopPortalBus : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!GetDBus().loaded)
        {
            GTEST_SKIP() << "libdbus is not installed";
        }
        bus_ = std::make_unique<PrivateBus>();
        if (bus_->Address().empty())
        {
            GTEST_SKIP() << "dbus-daemon could not be started (is it installed?)";
        }
        saved_ = System::Environment::GetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", bus_->Address());
    }

    void TearDown() override
    {
        client_.reset();
        portal_.reset();
        bus_.reset();
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", saved_);
    }

    void StartPortal()
    {
        portal_ = std::make_unique<FakePortal>(bus_->Address());
        ASSERT_TRUE(portal_->Ready());
        client_ = DesktopPortal::Connect();
        ASSERT_NE(client_, nullptr) << "the portal on the bus was not found";
    }

    /// Pumps until a callback has run; returns what it received.
    std::optional<std::vector<std::string>> PumpForAnswer(const std::shared_ptr<std::optional<std::vector<std::string>>>& got,
                                                          const std::chrono::milliseconds budget = std::chrono::milliseconds(5000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!got->has_value() && std::chrono::steady_clock::now() < deadline)
        {
            client_->Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return *got;
    }

    static FileDialogCallback Capture(const std::shared_ptr<std::optional<std::vector<std::string>>>& got,
                                      const std::shared_ptr<int>& count)
    {
        return [got, count](const std::vector<std::string>& paths) {
            *got = paths;
            ++*count;
        };
    }

    std::unique_ptr<PrivateBus> bus_;
    std::unique_ptr<FakePortal> portal_;
    std::unique_ptr<DesktopPortal> client_;
    std::optional<std::string> saved_;
};

TEST_F(X11DesktopPortalBus, WithoutAPortalOnTheBusThereIsNone)
{
    EXPECT_EQ(DesktopPortal::Connect(), nullptr) << "a bus with nothing on it";
    System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS",
                                                std::string("unix:path=/nonexistent/cna-no-bus"));
    EXPECT_EQ(DesktopPortal::Connect(), nullptr) << "no bus at all";
}

TEST_F(X11DesktopPortalBus, AnOpenDialogCarriesItsOptionsAndItsAnswerArrivesFromPump)
{
    StartPortal();
    portal_->Answer({0, {"file:///tmp/a%20b.png", "file:///tmp/c.jpg", "https://example.com/not-a-file"}});
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(got, count),
                            {{"Images", "png;jpg"}}, "/tmp", true, PortalParentWindow(0x1a00003));
    EXPECT_FALSE(got->has_value()) << "never inside the call";
    const std::optional<std::vector<std::string>> paths = PumpForAnswer(got);
    ASSERT_TRUE(paths.has_value()) << "no answer came";
    EXPECT_EQ(*paths, (std::vector<std::string>{"/tmp/a b.png", "/tmp/c.jpg"})) << "only local files are paths";
    for (int pass = 0; pass < 20; ++pass) { client_->Pump(); }
    EXPECT_EQ(*count, 1) << "exactly once";

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 1u);
    const PortalCall& call = calls[0];
    EXPECT_EQ(call.method, "OpenFile");
    EXPECT_EQ(call.parent, "x11:1a00003");
    EXPECT_EQ(call.title, "Open File");
    EXPECT_EQ(call.sender, client_->GetUniqueName());
    EXPECT_EQ(call.strings.at("handle_token").rfind("cna", 0), 0u);
    EXPECT_TRUE(call.flags.at("modal"));
    EXPECT_TRUE(call.flags.at("multiple"));
    EXPECT_EQ(call.flags.count("directory"), 0u);
    EXPECT_EQ(call.paths.at("current_folder"), "/tmp");
    ASSERT_EQ(call.filters.size(), 1u);
    EXPECT_EQ(call.filters[0].name, "Images");
    ASSERT_EQ(call.filters[0].patterns.size(), 2u);
    EXPECT_EQ(call.filters[0].patterns[0].pattern, "*.[pP][nN][gG]");
}

TEST_F(X11DesktopPortalBus, ACancelledOrFailedDialogAnswersNothingOnce)
{
    StartPortal();
    portal_->Answer({1, {"file:///tmp/ignored"}});
    auto cancelled = std::make_shared<std::optional<std::vector<std::string>>>();
    auto cancelledCount = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(cancelled, cancelledCount), {}, "", false, "");
    ASSERT_TRUE(PumpForAnswer(cancelled).has_value());
    EXPECT_TRUE((*cancelled)->empty()) << "cancelled: no paths, whatever came with it";

    PortalAnswer failure;
    failure.error = true;
    portal_->Answer(failure);
    auto failed = std::make_shared<std::optional<std::vector<std::string>>>();
    auto failedCount = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(failed, failedCount), {}, "", false, "");
    ASSERT_TRUE(PumpForAnswer(failed).has_value()) << "an error is an answer too";
    EXPECT_TRUE((*failed)->empty());
    for (int pass = 0; pass < 20; ++pass) { client_->Pump(); }
    EXPECT_EQ(*cancelledCount, 1);
    EXPECT_EQ(*failedCount, 1);
    EXPECT_FALSE(portal_->Calls()[0].flags.at("modal")) << "no parent, not modal";
    EXPECT_EQ(portal_->Calls()[0].flags.count("multiple"), 0u);
}

TEST_F(X11DesktopPortalBus, SaveAndFolderDialogsSayWhatTheyAre)
{
    StartPortal();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("cna-portal-dialogs-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    portal_->Answer({0, {"file://" + (directory / "new.sav").string()}});
    auto saved = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Save, Capture(saved, count), {{"Saves", "sav"}},
                            (directory / "new.sav").string(), true, "");
    ASSERT_TRUE(PumpForAnswer(saved).has_value());
    EXPECT_EQ(**saved, std::vector<std::string>{(directory / "new.sav").string()});

    portal_->Answer({0, {"file:///tmp", "file:///var"}});
    auto folders = std::make_shared<std::optional<std::vector<std::string>>>();
    client_->ShowFileDialog(DesktopPortal::FileRequest::OpenFolder, Capture(folders, count), {{"Ignored", "x"}},
                            directory.string(), true, "");
    ASSERT_TRUE(PumpForAnswer(folders).has_value());
    EXPECT_EQ(**folders, (std::vector<std::string>{"/tmp", "/var"}));

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].method, "SaveFile");
    EXPECT_EQ(calls[0].title, "Save File");
    EXPECT_EQ(calls[0].paths.at("current_folder"), directory.string());
    EXPECT_EQ(calls[0].strings.at("current_name"), "new.sav");
    EXPECT_EQ(calls[0].flags.count("multiple"), 0u) << "a save dialog saves one file";
    ASSERT_EQ(calls[0].filters.size(), 1u);
    EXPECT_EQ(calls[1].method, "OpenFile");
    EXPECT_EQ(calls[1].title, "Open Folder");
    EXPECT_TRUE(calls[1].flags.at("directory"));
    EXPECT_TRUE(calls[1].flags.at("multiple"));
    EXPECT_TRUE(calls[1].filters.empty()) << "a folder has no file type";
    EXPECT_EQ(calls[1].paths.at("current_folder"), directory.string());
    std::filesystem::remove_all(directory);
}

TEST_F(X11DesktopPortalBus, AnOlderPortalsOwnRequestPathIsFollowed)
{
    StartPortal();
    PortalAnswer answer;
    answer.uris = {"file:///tmp/older"};
    answer.ownPath = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, "");
    ASSERT_TRUE(PumpForAnswer(got).has_value()) << "the answer on the portal's own path was missed";
    EXPECT_EQ(**got, std::vector<std::string>{"/tmp/older"});
}

TEST_F(X11DesktopPortalBus, AnAnswerLongAfterTheQuestionStillArrives)
{
    StartPortal();
    PortalAnswer answer;
    answer.silent = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, "");
    // The user takes their time: frames go by.
    EXPECT_FALSE(PumpForAnswer(got, std::chrono::milliseconds(300)).has_value());
    portal_->RespondLater(0, {"file:///tmp/eventually"});
    ASSERT_TRUE(PumpForAnswer(got).has_value());
    EXPECT_EQ(**got, std::vector<std::string>{"/tmp/eventually"});
}

TEST_F(X11DesktopPortalBus, ACallbackMayAskForAnotherDialog)
{
    StartPortal();
    portal_->Answer({0, {"file:///tmp/first"}});
    auto second = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    auto first = std::make_shared<std::optional<std::vector<std::string>>>();
    DesktopPortal* client = client_.get();
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open,
                            [first, second, count, client](const std::vector<std::string>& paths) {
                                *first = paths;
                                client->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(second, count), {},
                                                       "", false, "");
                            },
                            {}, "", false, "");
    ASSERT_TRUE(PumpForAnswer(first).has_value());
    ASSERT_TRUE(PumpForAnswer(second).has_value()) << "the second dialog, asked for from the first's callback";
    EXPECT_EQ(portal_->Calls().size(), 2u);
}

TEST_F(X11DesktopPortalBus, ABusThatGoesAwayAnswersEveryOpenDialog)
{
    StartPortal();
    PortalAnswer answer;
    answer.silent = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, "");
    EXPECT_FALSE(PumpForAnswer(got, std::chrono::milliseconds(200)).has_value());
    portal_.reset();
    bus_->Stop();
    ASSERT_TRUE(PumpForAnswer(got).has_value()) << "a dialog whose answer can never come is answered empty";
    EXPECT_TRUE((*got)->empty());
    EXPECT_EQ(*count, 1);
}

TEST_F(X11DesktopPortalBus, OpenUriHandsOverAUrlOrAnOpenFile)
{
    StartPortal();
    EXPECT_TRUE(client_->OpenUri("https://example.com/page?x=1", PortalParentWindow(0x2c00001)));
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("cna-portal-open-" + std::to_string(::getpid()) + ".txt");
    std::ofstream(file) << "hello";
    EXPECT_TRUE(client_->OpenUri("file://" + file.string(), ""));
    EXPECT_FALSE(client_->OpenUri("file:///nonexistent/cna/file", "")) << "a file that is not there";
    EXPECT_FALSE(client_->OpenUri("no scheme at all", ""));
    EXPECT_FALSE(client_->OpenUri(":empty-scheme", ""));

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u) << "only the two that were URLs reached the portal";
    EXPECT_EQ(calls[0].interface, "org.freedesktop.portal.OpenURI");
    EXPECT_EQ(calls[0].method, "OpenURI");
    EXPECT_EQ(calls[0].uri, "https://example.com/page?x=1");
    EXPECT_EQ(calls[0].parent, "x11:2c00001");
    EXPECT_EQ(calls[1].method, "OpenFile");
    EXPECT_EQ(calls[1].fileTarget, file.string()) << "the descriptor the portal received is the file's";
    std::filesystem::remove(file);
}

// --- through the platform, on the launcher's server --------------------------------------------

class X11DesktopPortalLive : public X11DesktopPortalBus
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "creates windows; needs tools/platform/x11_test_server.sh";
        }
        X11DesktopPortalBus::SetUp();
    }

    void TearDown() override
    {
        window_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        X11DesktopPortalBus::TearDown();
    }

    void OpenPlatform()
    {
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
};

TEST_F(X11DesktopPortalLive, WithoutAPortalFileDialogsRefuseNamingTheirCapability)
{
    OpenPlatform();
    EXPECT_FALSE(platform_->GetCapabilities().nativeFileDialog);
    EXPECT_TRUE(platform_->GetCapabilities().messageBox);
    try
    {
        platform_->GetDialogs()->ShowOpenFileDialog([](const std::vector<std::string>&) {}, {}, "", false, nullptr);
        FAIL() << "no portal, yet a file dialog was shown";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::NativeFileDialog);
    }
    EXPECT_FALSE(platform_->GetSystemInfo()->OpenUrl("https://example.com/"));
}

TEST_F(X11DesktopPortalLive, ThePlatformsDialogsAndOpenUrlGoThroughThePortal)
{
    portal_ = std::make_unique<FakePortal>(bus_->Address());
    ASSERT_TRUE(portal_->Ready());
    OpenPlatform();
    ASSERT_TRUE(platform_->GetCapabilities().nativeFileDialog);
    WindowDescription description;
    description.title = "CNA portal parent";
    description.width = 320;
    description.height = 200;
    window_ = platform_->CreateWindow(description);

    portal_->Answer({0, {"file:///tmp/chosen.png"}});
    std::optional<std::vector<std::string>> got;
    platform_->GetDialogs()->ShowOpenFileDialog(
        [&got](const std::vector<std::string>& paths) { got = paths; }, {{"Images", "png"}}, "", false, window_.get());
    EXPECT_FALSE(got.has_value()) << "never inside the call";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::vector<PlatformEvent> events;
    while (!got.has_value() && std::chrono::steady_clock::now() < deadline)
    {
        platform_->PollEvents(events);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(got.has_value()) << "PollEvents never delivered the answer";
    EXPECT_EQ(*got, std::vector<std::string>{"/tmp/chosen.png"});

    EXPECT_TRUE(platform_->GetSystemInfo()->OpenUrl("https://example.com/"));
    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u);
    char expectedParent[32] = {};
    std::snprintf(expectedParent, sizeof(expectedParent), "x11:%lx",
                  static_cast<unsigned long>(window_->GetNativeHandle().windowId));
    EXPECT_EQ(calls[0].parent, expectedParent) << "the dialog belongs to the game's window";
    EXPECT_TRUE(calls[0].flags.at("modal"));
    EXPECT_EQ(calls[1].uri, "https://example.com/");
}

#endif // CNA_PLATFORM_HAVE_DBUS

} // namespace
