// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Devices/Detail/IFileDialogBackend.hpp"
#include "CNA/Devices/FileDialog.hpp"

using CNA::Devices::FileDialog;
using CNA::Devices::FileDialogFilter;
using CNA::Devices::Detail::FileDialogResultCallback;
using CNA::Devices::Detail::IFileDialogBackend;

namespace
{
    // Task DEVICES-CNA-008: the real backend (Detail::SdlFileDialogBackend) launches
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
    // Detail::SdlFileDialogBackend in the destructor -- FileDialog's backend is
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
    // After the scope above, the real Detail::SdlFileDialogBackend is restored.
    // getIsSupportedProperty() does not depend on the backend, so this only proves
    // SetBackendForTesting(nullptr) does not crash -- the real backend's dialog-
    // launching behavior itself is deliberately never exercised in this test file.
    EXPECT_NO_THROW({ (void)FileDialog::getIsSupportedProperty(); });
}

#endif // CNA_DEVICES
