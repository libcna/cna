// SPDX-License-Identifier: MS-PL
//
// PLAT-90: FileDialog is driven through the platform's dialog service, not an injected backend.
//
// A test must never reach a real file dialog. It launches an interactive native window -- a real
// `zenity` process on Linux -- that waits for a human forever; that happened once during this
// class's own development and left orphaned processes on a real desktop session. The protection
// used to be a `SetBackendForTesting` hook on the public class. It is now a platform whose dialog
// service records instead of showing, which is one seam serving every surface rather than a
// test-only hook on shipped API.

#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "CNA/Devices/FileDialog.hpp"
#include "CNA/Platform/CannedDialogs.hpp"

using CNA::Devices::FileDialog;
using CNA::Devices::FileDialogFilter;
using CNA::Platform::Testing::CannedDialogPlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;

namespace
{
    class FileDialogTest : public ::testing::Test
    {
    protected:
        CannedDialogPlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(FileDialogTest, GetIsSupportedPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)FileDialog::getIsSupportedProperty(); });
}

TEST_F(FileDialogTest, ShowOpenFileForwardsParametersAndInvokesCallback)
{
    platform.Canned().chosenPaths = {"/tmp/chosen-file.txt"};

    const std::vector<FileDialogFilter> filters = {{"Text files", "txt"}};
    bool invoked = false;
    std::vector<std::string> received;

    FileDialog::ShowOpenFile(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        },
        filters, "/tmp", true);

    ASSERT_EQ(platform.Canned().fileDialogs.size(), 1u);
    const auto& call = platform.Canned().fileDialogs.front();
    EXPECT_EQ(call.kind, "open");
    ASSERT_EQ(call.filters.size(), 1u);
    EXPECT_EQ(call.filters.front().name, "Text files");
    EXPECT_EQ(call.filters.front().patterns, "txt");
    EXPECT_EQ(call.defaultLocation, "/tmp");
    EXPECT_TRUE(call.allowMultiple);

    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, platform.Canned().chosenPaths);
}

TEST_F(FileDialogTest, ShowSaveFileForwardsParametersAndInvokesCallback)
{
    platform.Canned().chosenPaths = {"/tmp/save-target.txt"};

    bool invoked = false;
    std::vector<std::string> received;

    FileDialog::ShowSaveFile(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        },
        {}, "/tmp");

    ASSERT_EQ(platform.Canned().fileDialogs.size(), 1u);
    EXPECT_EQ(platform.Canned().fileDialogs.front().kind, "save");
    EXPECT_EQ(platform.Canned().fileDialogs.front().defaultLocation, "/tmp");
    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, platform.Canned().chosenPaths);
}

TEST_F(FileDialogTest, ShowOpenFolderForwardsParametersAndInvokesCallback)
{
    platform.Canned().chosenPaths = {"/tmp/chosen-folder"};

    bool invoked = false;
    std::vector<std::string> received;

    FileDialog::ShowOpenFolder(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        },
        "/tmp", false);

    ASSERT_EQ(platform.Canned().fileDialogs.size(), 1u);
    EXPECT_EQ(platform.Canned().fileDialogs.front().kind, "folder");
    EXPECT_EQ(platform.Canned().fileDialogs.front().defaultLocation, "/tmp");
    EXPECT_FALSE(platform.Canned().fileDialogs.front().allowMultiple);
    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, platform.Canned().chosenPaths);
}

TEST_F(FileDialogTest, EmptyResultMeansCanceledOrError)
{
    // chosenPaths defaults to empty -- which represents both "user cancelled" and "an error
    // occurred", a distinction this wrapper deliberately does not make.
    std::vector<std::string> received = {"sentinel-should-be-overwritten"};
    FileDialog::ShowOpenFile([&](const std::vector<std::string>& files) { received = files; });

    EXPECT_TRUE(received.empty());
}

namespace
{
    /// A platform that reports no file dialogs at all, which is what HEADLESS does.
    class DialoglessPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::PlatformCapabilities GetCapabilities() const override
        {
            CNA::Platform::PlatformCapabilities capabilities =
                PlatformTestDecorator::GetCapabilities();
            capabilities.nativeFileDialog = false;
            return capabilities;
        }
    };
}

TEST(FileDialogWithoutAServiceTest, TheCallbackStillFiresSoACallerIsNotLeftWaiting)
{
    // The branch that did not exist before the migration. A caller has already registered its
    // continuation by the time it could learn there is no dialog, so dropping the callback would
    // be a hang rather than a refusal -- and a hang in a UI flow reads as a frozen game.
    DialoglessPlatform platform;
    const ScopedCurrentPlatform installed(platform);

    bool invoked = false;
    std::vector<std::string> received = {"sentinel"};
    FileDialog::ShowOpenFile(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        });

    EXPECT_TRUE(invoked);
    EXPECT_TRUE(received.empty());

    EXPECT_NO_THROW(FileDialog::ShowSaveFile([&](const std::vector<std::string>&) {}));
    EXPECT_NO_THROW(FileDialog::ShowOpenFolder([&](const std::vector<std::string>&) {}));
}

namespace
{
    class RejectingDialogPlatform final : public CNA::Platform::Testing::PlatformTestDecorator,
                                          private CNA::Platform::IPlatformDialogs
    {
    public:
        CNA::Platform::FileDialogCallback pending;
        bool completeBeforeReject = false;
        bool reject = true;

        CNA::Platform::IPlatformDialogs* GetDialogs() override { return this; }

        CNA::Platform::PlatformCapabilities GetCapabilities() const override
        {
            auto capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.nativeFileDialog = true;
            return capabilities;
        }

    private:
        void ShowMessageBox(CNA::Platform::MessageBoxSeverity, const std::string&,
                            const std::string&, CNA::Platform::IPlatformWindow*) override {}
        int ShowMessageBoxWithButtons(CNA::Platform::MessageBoxSeverity, const std::string&,
                                      const std::string&, const std::vector<std::string>&,
                                      CNA::Platform::IPlatformWindow*) override { return -1; }

        void ShowOpenFileDialog(CNA::Platform::FileDialogCallback callback,
                                const std::vector<CNA::Platform::FileDialogFilter>&,
                                const std::string&, bool, CNA::Platform::IPlatformWindow*) override
        {
            Request(std::move(callback));
        }
        void ShowSaveFileDialog(CNA::Platform::FileDialogCallback callback,
                                const std::vector<CNA::Platform::FileDialogFilter>&,
                                const std::string&, CNA::Platform::IPlatformWindow*) override
        {
            Request(std::move(callback));
        }
        void ShowOpenFolderDialog(CNA::Platform::FileDialogCallback callback,
                                  const std::string&, bool, CNA::Platform::IPlatformWindow*) override
        {
            Request(std::move(callback));
        }
        void Request(CNA::Platform::FileDialogCallback callback)
        {
            pending = std::move(callback);
            if (completeBeforeReject && pending) { pending({"chosen"}); }
            if (reject) { throw CNA::Platform::PlatformException("file-dialog", "scripted rejection"); }
        }
    };

    void ShowDialog(int route, FileDialog::ResultCallback callback)
    {
        if (route == 0) { FileDialog::ShowOpenFile(std::move(callback)); }
        else if (route == 1) { FileDialog::ShowSaveFile(std::move(callback)); }
        else { FileDialog::ShowOpenFolder(std::move(callback)); }
    }
}

TEST(FileDialogProviderFailureTest, RejectionCompletesEveryRouteOnceWithAnEmptyResult)
{
    for (int route = 0; route < 3; ++route)
    {
        SCOPED_TRACE(route);
        RejectingDialogPlatform platform;
        const ScopedCurrentPlatform installed(platform);
        int calls = 0;
        std::vector<std::string> received = {"sentinel"};
        EXPECT_NO_THROW(ShowDialog(route, [&](const auto& paths) { ++calls; received = paths; }));
        EXPECT_EQ(calls, 1);
        EXPECT_TRUE(received.empty());
        ASSERT_TRUE(platform.pending);
        platform.pending({"late"});
        EXPECT_EQ(calls, 1);
        EXPECT_TRUE(received.empty());
    }
}

TEST(FileDialogProviderFailureTest, CompletionBeforeRejectionIsNotRepeatedOrOverwritten)
{
    for (int route = 0; route < 3; ++route)
    {
        SCOPED_TRACE(route);
        RejectingDialogPlatform platform;
        platform.completeBeforeReject = true;
        const ScopedCurrentPlatform installed(platform);
        int calls = 0;
        std::vector<std::string> received;
        EXPECT_NO_THROW(ShowDialog(route, [&](const auto& paths) { ++calls; received = paths; }));
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(received, std::vector<std::string>{"chosen"});
    }
}

TEST(FileDialogProviderFailureTest, DeferredCompletionSurvivesTheWrapperAndIgnoresDuplicates)
{
    for (int route = 0; route < 3; ++route)
    {
        SCOPED_TRACE(route);
        RejectingDialogPlatform platform;
        platform.reject = false;
        const ScopedCurrentPlatform installed(platform);
        int calls = 0;
        std::vector<std::string> received;
        ShowDialog(route, [&](const auto& paths) { ++calls; received = paths; });
        EXPECT_EQ(calls, 0);
        ASSERT_TRUE(platform.pending);
        std::jthread completion([callback = platform.pending]
        {
            callback({"chosen"});
            callback({"duplicate"});
        });
        completion.join();
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(received, std::vector<std::string>{"chosen"});
    }
}

TEST(FileDialogProviderFailureTest, EmptyCallbackRemainsEmptyAtTheProvider)
{
    for (int route = 0; route < 3; ++route)
    {
        RejectingDialogPlatform platform;
        const ScopedCurrentPlatform installed(platform);
        EXPECT_NO_THROW(ShowDialog(route, {}));
        EXPECT_FALSE(platform.pending);
    }
}

// The concurrency test that used to live here raced a caller thread against
// SetBackendForTesting, pinning the dialog mutex-lifetime fix: a swap could destroy the backend while a call
// was still running through it. It is deleted rather than ported, because with no swappable
// backend the race has no parts left to have -- there is nothing to swap and nothing to destroy.
//
// Porting it was tried and is the wrong shape: two threads calling through the recording service
// race on the recording itself, so the test would have been measuring test scaffolding, and
// making that scaffolding thread-safe would have been work done purely to keep a test of it
// alive. The one remaining instance of the pattern is `GetCurrentPlatform()`, which returns a
// reference the caller uses after the lock is released -- and that is governed by a stated
// ownership rule (the platform must outlive every use of it) rather than by a swap.

#endif // CNA_DEVICES
