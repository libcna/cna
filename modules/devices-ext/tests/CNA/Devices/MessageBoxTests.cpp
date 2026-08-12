// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "CNA/Devices/Detail/IMessageBoxBackend.hpp"
#include "CNA/Devices/MessageBox.hpp"

using CNA::Devices::MessageBox;
using CNA::Devices::MessageBoxType;
using CNA::Devices::Detail::IMessageBoxBackend;

namespace
{
    // Task DEVICES-CNA-011: the real backend (Detail::PlatformMessageBoxBackend) pops a
    // genuine, interactive, modal native OS dialog and blocks the calling thread
    // until a human responds -- the same class of incident already hit once during
    // FileDialog's own development (see FileDialogTests.cpp's own doc comment).
    // Every test below uses this fake instead, never the real backend.
    class FakeMessageBoxBackend final : public IMessageBoxBackend
    {
    public:
        int ShowSimpleCallCount = 0;
        MessageBoxType LastShowSimpleType = MessageBoxType::Information;
        std::string LastShowSimpleTitle;
        std::string LastShowSimpleMessage;

        int ShowCallCount = 0;
        MessageBoxType LastShowType = MessageBoxType::Information;
        std::string LastShowTitle;
        std::string LastShowMessage;
        std::vector<std::string> LastShowButtonLabels;

        // The result Show() immediately returns, instead of ever showing a real
        // dialog -- simulates "the user clicked this button" without any real OS
        // interaction.
        int SimulatedButtonIndex = 0;

        void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message) override
        {
            ++ShowSimpleCallCount;
            LastShowSimpleType = type;
            LastShowSimpleTitle = title;
            LastShowSimpleMessage = message;
        }

        int Show(
            MessageBoxType type,
            const std::string& title,
            const std::string& message,
            const std::vector<std::string>& buttonLabels) override
        {
            ++ShowCallCount;
            LastShowType = type;
            LastShowTitle = title;
            LastShowMessage = message;
            LastShowButtonLabels = buttonLabels;
            return SimulatedButtonIndex;
        }
    };

    // RAII: installs the fake in the constructor, restores the real
    // Detail::PlatformMessageBoxBackend in the destructor -- MessageBox's backend is
    // process-wide static state, so every test must restore it, or a fake would leak
    // into unrelated tests run afterward in the same process.
    class ScopedFakeMessageBoxBackend
    {
    public:
        ScopedFakeMessageBoxBackend()
        {
            auto fake = std::make_unique<FakeMessageBoxBackend>();
            fake_ = fake.get();
            MessageBox::SetBackendForTesting(std::move(fake));
        }

        ~ScopedFakeMessageBoxBackend()
        {
            MessageBox::SetBackendForTesting(nullptr);
        }

        ScopedFakeMessageBoxBackend(const ScopedFakeMessageBoxBackend&) = delete;
        ScopedFakeMessageBoxBackend& operator=(const ScopedFakeMessageBoxBackend&) = delete;

        FakeMessageBoxBackend* operator->() const
        {
            return fake_;
        }

    private:
        FakeMessageBoxBackend* fake_;
    };
} // namespace

TEST(MessageBoxTests, GetIsSupportedPropertyIsAlwaysTrue)
{
    EXPECT_TRUE(MessageBox::getIsSupportedProperty());
}

TEST(MessageBoxTests, ShowSimpleForwardsParametersToBackend)
{
    ScopedFakeMessageBoxBackend fake;

    MessageBox::ShowSimple(MessageBoxType::Warning, "Careful", "Something needs attention.");

    EXPECT_EQ(fake->ShowSimpleCallCount, 1);
    EXPECT_EQ(fake->LastShowSimpleType, MessageBoxType::Warning);
    EXPECT_EQ(fake->LastShowSimpleTitle, "Careful");
    EXPECT_EQ(fake->LastShowSimpleMessage, "Something needs attention.");
}

TEST(MessageBoxTests, ShowForwardsParametersToBackendAndReturnsClickedIndex)
{
    ScopedFakeMessageBoxBackend fake;
    fake->SimulatedButtonIndex = 1;

    const std::vector<std::string> buttons = {"Cancel", "OK"};
    const int clicked = MessageBox::Show(MessageBoxType::Error, "Failure", "Something went wrong.", buttons);

    EXPECT_EQ(fake->ShowCallCount, 1);
    EXPECT_EQ(fake->LastShowType, MessageBoxType::Error);
    EXPECT_EQ(fake->LastShowTitle, "Failure");
    EXPECT_EQ(fake->LastShowMessage, "Something went wrong.");
    EXPECT_EQ(fake->LastShowButtonLabels, buttons);
    EXPECT_EQ(clicked, 1);
}

TEST(MessageBoxTests, SetBackendForTestingNullRestoresDefaultBackendBehavior)
{
    {
        ScopedFakeMessageBoxBackend fake;
        MessageBox::ShowSimple(MessageBoxType::Information, "Title", "Message");
        EXPECT_EQ(fake->ShowSimpleCallCount, 1);
    }
    // After the scope above, the real Detail::PlatformMessageBoxBackend is restored.
    // getIsSupportedProperty() does not depend on the backend, so this only proves
    // SetBackendForTesting(nullptr) does not crash -- the real backend's dialog-
    // showing behavior itself is deliberately never exercised in this test file.
    EXPECT_NO_THROW({ (void)MessageBox::getIsSupportedProperty(); });
}

// Task REMED-DEVICES-001: MessageBox's GetBackend() (see MessageBox.cpp, internal
// linkage) used to return a raw pointer after already releasing BackendMutex(),
// so a concurrent SetBackendForTesting() could reassign -- and destroy -- the
// backend object while another thread was still calling through the just-returned
// pointer (a genuine use-after-free window). GetBackend() now returns a
// shared_ptr copy taken under the lock, which keeps the pointee alive for the
// full duration of any in-flight call regardless of a concurrent swap. See
// FileDialogTests.cpp's identical test for the full rationale on why this is a
// stress test rather than a deterministic single-shot reproduction.
TEST(MessageBoxTests, ConcurrentSetBackendForTestingDoesNotRaceWithLiveCalls)
{
    constexpr int kIterations = 2000;
    std::atomic<int> callCount{0};

    // Install a fake before racing -- the caller thread below must never reach
    // the real Detail::PlatformMessageBoxBackend, which would pop a real, modal,
    // interactive dialog and block forever (see this file's own top-of-file
    // comment on why).
    MessageBox::SetBackendForTesting(std::make_unique<FakeMessageBoxBackend>());

    std::thread caller(
        [&]
        {
            for (int i = 0; i < kIterations; ++i)
            {
                MessageBox::ShowSimple(MessageBoxType::Information, "Title", "Message");
                ++callCount;
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
                MessageBox::SetBackendForTesting(std::make_unique<FakeMessageBoxBackend>());
            }
        });

    caller.join();
    swapper.join();

    MessageBox::SetBackendForTesting(nullptr);

    // A UAF would show up as a crash (under a sanitizer or even a plain build,
    // given enough iterations) well before this assertion is reached.
    EXPECT_EQ(callCount.load(), kIterations);
}

#endif // CNA_DEVICES
