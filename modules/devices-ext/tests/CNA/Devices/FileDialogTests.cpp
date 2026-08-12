// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "CNA/Devices/Detail/IFileDialogBackend.hpp"
#include "CNA/Devices/FileDialog.hpp"

using CNA::Devices::FileDialog;
using CNA::Devices::FileDialogFilter;
using CNA::Devices::Detail::FileDialogResultCallback;
using CNA::Devices::Detail::IFileDialogBackend;

namespace
{
    // Task DEVICES-CNA-008: the real backend (Detail::PlatformFileDialogBackend) launches
    // a genuine, interactive native OS dialog -- calling it from an automated test was
    // tried during this class's own development and left orphaned `zenity` processes
    // running on the development machine's real desktop session, waiting forever for
    // a human that would never arrive. Every test below uses this fake instead, never
    // the real backend, mirroring VibrateControllerTests.cpp's established
    // FakeVibrateBackend/ScopedFakeVibrateBackend pattern.
    class FakeFileDialogBackend final : public IFileDialogBackend
    {
    public:
        int ShowOpenFileCallCount = 0;
        std::vector<FileDialogFilter> LastOpenFileFilters;
        std::string LastOpenFileDefaultLocation;
        bool LastOpenFileAllowMultiple = false;

        int ShowSaveFileCallCount = 0;
        std::vector<FileDialogFilter> LastSaveFileFilters;
        std::string LastSaveFileDefaultLocation;

        int ShowOpenFolderCallCount = 0;
        std::string LastOpenFolderDefaultLocation;
        bool LastOpenFolderAllowMultiple = false;

        // The result this fake immediately invokes the callback with, instead of ever
        // showing a real dialog -- simulates a real dialog's asynchronous-but-
        // eventually-fires-once contract without any real OS interaction.
        std::vector<std::string> SimulatedResult;

        void ShowOpenFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation,
            bool allowMultiple) override
        {
            ++ShowOpenFileCallCount;
            LastOpenFileFilters = filters;
            LastOpenFileDefaultLocation = defaultLocation;
            LastOpenFileAllowMultiple = allowMultiple;
            if (onResult)
            {
                onResult(SimulatedResult);
            }
        }

        void ShowSaveFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation) override
        {
            ++ShowSaveFileCallCount;
            LastSaveFileFilters = filters;
            LastSaveFileDefaultLocation = defaultLocation;
            if (onResult)
            {
                onResult(SimulatedResult);
            }
        }

        void ShowOpenFolder(
            FileDialogResultCallback onResult,
            const std::string& defaultLocation,
            bool allowMultiple) override
        {
            ++ShowOpenFolderCallCount;
            LastOpenFolderDefaultLocation = defaultLocation;
            LastOpenFolderAllowMultiple = allowMultiple;
            if (onResult)
            {
                onResult(SimulatedResult);
            }
        }
    };

    // RAII: installs the fake in the constructor, restores the real
    // Detail::PlatformFileDialogBackend in the destructor -- FileDialog's backend is
    // process-wide static state, so every test must restore it, or a fake would leak
    // into unrelated tests run afterward in the same process.
    class ScopedFakeFileDialogBackend
    {
    public:
        ScopedFakeFileDialogBackend()
        {
            auto fake = std::make_unique<FakeFileDialogBackend>();
            fake_ = fake.get();
            FileDialog::SetBackendForTesting(std::move(fake));
        }

        ~ScopedFakeFileDialogBackend()
        {
            FileDialog::SetBackendForTesting(nullptr);
        }

        ScopedFakeFileDialogBackend(const ScopedFakeFileDialogBackend&) = delete;
        ScopedFakeFileDialogBackend& operator=(const ScopedFakeFileDialogBackend&) = delete;

        FakeFileDialogBackend* operator->() const
        {
            return fake_;
        }

    private:
        FakeFileDialogBackend* fake_;
    };
} // namespace

TEST(FileDialogTests, GetIsSupportedPropertyDoesNotCrash)
{
    // Real, platform-dependent value (true on this desktop Linux container) --
    // exercises the actual CNA::getCurrentPlatform()-based logic, not the fake
    // backend (getIsSupportedProperty() does not go through the backend at all).
    EXPECT_NO_THROW({ (void)FileDialog::getIsSupportedProperty(); });
}

TEST(FileDialogTests, ShowOpenFileForwardsParametersToBackendAndInvokesCallback)
{
    ScopedFakeFileDialogBackend fake;
    fake->SimulatedResult = {"/tmp/chosen-file.txt"};

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

    EXPECT_EQ(fake->ShowOpenFileCallCount, 1);
    EXPECT_EQ(fake->LastOpenFileFilters.size(), 1u);
    EXPECT_EQ(fake->LastOpenFileDefaultLocation, "/tmp");
    EXPECT_TRUE(fake->LastOpenFileAllowMultiple);
    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, fake->SimulatedResult);
}

TEST(FileDialogTests, ShowSaveFileForwardsParametersToBackendAndInvokesCallback)
{
    ScopedFakeFileDialogBackend fake;
    fake->SimulatedResult = {"/tmp/save-target.txt"};

    bool invoked = false;
    std::vector<std::string> received;

    FileDialog::ShowSaveFile(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        },
        {}, "/tmp");

    EXPECT_EQ(fake->ShowSaveFileCallCount, 1);
    EXPECT_EQ(fake->LastSaveFileDefaultLocation, "/tmp");
    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, fake->SimulatedResult);
}

TEST(FileDialogTests, ShowOpenFolderForwardsParametersToBackendAndInvokesCallback)
{
    ScopedFakeFileDialogBackend fake;
    fake->SimulatedResult = {"/tmp/chosen-folder"};

    bool invoked = false;
    std::vector<std::string> received;

    FileDialog::ShowOpenFolder(
        [&](const std::vector<std::string>& files)
        {
            invoked = true;
            received = files;
        },
        "/tmp", false);

    EXPECT_EQ(fake->ShowOpenFolderCallCount, 1);
    EXPECT_EQ(fake->LastOpenFolderDefaultLocation, "/tmp");
    EXPECT_FALSE(fake->LastOpenFolderAllowMultiple);
    ASSERT_TRUE(invoked);
    EXPECT_EQ(received, fake->SimulatedResult);
}

TEST(FileDialogTests, EmptyResultMeansCanceledOrError)
{
    ScopedFakeFileDialogBackend fake;
    // SimulatedResult defaults to empty -- represents both "user canceled" and
    // "an error occurred", which this simplified wrapper does not distinguish (see
    // FileDialog's own doc comment).

    std::vector<std::string> received = {"sentinel-should-be-overwritten"};
    FileDialog::ShowOpenFile([&](const std::vector<std::string>& files) { received = files; });

    EXPECT_TRUE(received.empty());
}

TEST(FileDialogTests, SetBackendForTestingNullRestoresDefaultBackendBehavior)
{
    {
        ScopedFakeFileDialogBackend fake;
        FileDialog::ShowOpenFile([](const std::vector<std::string>&) {});
        EXPECT_EQ(fake->ShowOpenFileCallCount, 1);
    }
    // After the scope above, the real Detail::PlatformFileDialogBackend is restored.
    // getIsSupportedProperty() does not depend on the backend, so this only proves
    // SetBackendForTesting(nullptr) does not crash -- the real backend's dialog-
    // launching behavior itself is deliberately never exercised in this test file.
    EXPECT_NO_THROW({ (void)FileDialog::getIsSupportedProperty(); });
}

// Task REMED-DEVICES-001: FileDialog's GetBackend() (see FileDialog.cpp, internal
// linkage) used to return a raw pointer after already releasing BackendMutex(),
// so a concurrent SetBackendForTesting() could reassign -- and destroy -- the
// backend object while another thread was still calling through the just-returned
// pointer (a genuine use-after-free window). GetBackend() now returns a
// shared_ptr copy taken under the lock, which keeps the pointee alive for the
// full duration of any in-flight call regardless of a concurrent swap.
//
// This does not deterministically reproduce the pre-fix bug on every run -- the
// original race window was a handful of machine instructions -- but racing both
// operations for many iterations reliably trips ThreadSanitizer/AddressSanitizer
// against the pre-fix code (see the devices-tsan/devices-asan CMake presets).
// Against the fix, it must complete cleanly under a plain build and under both
// sanitizers.
TEST(FileDialogTests, ConcurrentSetBackendForTestingDoesNotRaceWithLiveCalls)
{
    constexpr int kIterations = 2000;
    std::atomic<int> callCount{0};

    // Install a fake before racing -- the caller thread below must never reach
    // the real Detail::PlatformFileDialogBackend, which would launch a real
    // interactive dialog (see this file's own top-of-file comment on why).
    FileDialog::SetBackendForTesting(std::make_unique<FakeFileDialogBackend>());

    std::thread caller(
        [&]
        {
            for (int i = 0; i < kIterations; ++i)
            {
                FileDialog::ShowOpenFile([&](const std::vector<std::string>&) { ++callCount; });
            }
        });

    std::thread swapper(
        [&]
        {
            for (int i = 0; i < kIterations; ++i)
            {
                // Never pass nullptr here -- that would install the real
                // backend mid-race, which the caller thread above must never
                // touch.
                FileDialog::SetBackendForTesting(std::make_unique<FakeFileDialogBackend>());
            }
        });

    caller.join();
    swapper.join();

    FileDialog::SetBackendForTesting(nullptr);

    // Every call must have run its callback exactly once, regardless of which
    // backend instance happened to be installed at the time -- a UAF would show
    // up here as a crash (under a sanitizer or even a plain build, given enough
    // iterations) well before this assertion is reached.
    EXPECT_EQ(callCount.load(), kIterations);
}

#endif // CNA_DEVICES
