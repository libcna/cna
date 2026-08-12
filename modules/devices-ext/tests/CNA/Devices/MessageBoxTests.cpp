// SPDX-License-Identifier: MS-PL
//
// PLAT-90: MessageBox is driven through the platform's dialog service, not an injected backend.
//
// A real message box blocks the calling thread until a human closes it, so no test may ever reach
// one. That protection used to be a `SetBackendForTesting` hook on the public class; it is now a
// platform whose dialog service records instead of showing.

#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "CNA/Devices/MessageBox.hpp"
#include "CNA/Platform/CannedDialogs.hpp"

using CNA::Devices::MessageBox;
using CNA::Devices::MessageBoxType;
using CNA::Platform::MessageBoxSeverity;
using CNA::Platform::Testing::CannedDialogPlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;

namespace
{
    class MessageBoxTest : public ::testing::Test
    {
    protected:
        CannedDialogPlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(MessageBoxTest, GetIsSupportedPropertyReportsTheCapability)
{
    // Previously an unconditional true. It now answers from the platform, so a host that cannot
    // show a dialog says so instead of letting a caller believe one appeared.
    EXPECT_EQ(MessageBox::getIsSupportedProperty(),
              platform.GetCapabilities().messageBox);
}

TEST_F(MessageBoxTest, ShowSimpleForwardsItsParameters)
{
    MessageBox::ShowSimple(MessageBoxType::Warning, "Careful", "Something needs attention.");

    ASSERT_EQ(platform.Canned().messageBoxes.size(), 1u);
    const auto& call = platform.Canned().messageBoxes.front();
    EXPECT_EQ(call.severity, MessageBoxSeverity::Warning);
    EXPECT_EQ(call.title, "Careful");
    EXPECT_EQ(call.message, "Something needs attention.");
    EXPECT_TRUE(call.buttons.empty()) << "the single-button form must not invent buttons";
}

TEST_F(MessageBoxTest, ShowForwardsItsParametersAndReturnsTheClickedIndex)
{
    platform.Canned().chosenButton = 1;

    const std::vector<std::string> buttons = {"Cancel", "OK"};
    const int clicked =
        MessageBox::Show(MessageBoxType::Error, "Failure", "Something went wrong.", buttons);

    ASSERT_EQ(platform.Canned().messageBoxes.size(), 1u);
    const auto& call = platform.Canned().messageBoxes.front();
    EXPECT_EQ(call.severity, MessageBoxSeverity::Error);
    EXPECT_EQ(call.title, "Failure");
    EXPECT_EQ(call.message, "Something went wrong.");
    EXPECT_EQ(call.buttons, buttons);
    EXPECT_EQ(clicked, 1);
}

TEST_F(MessageBoxTest, EverySeverityIsForwardedDistinctly)
{
    // Collapsing two severities would show an error as an information dialog, which is exactly
    // the sort of thing nobody notices until a user reports the wrong icon.
    MessageBox::ShowSimple(MessageBoxType::Information, "i", "m");
    MessageBox::ShowSimple(MessageBoxType::Warning, "w", "m");
    MessageBox::ShowSimple(MessageBoxType::Error, "e", "m");

    ASSERT_EQ(platform.Canned().messageBoxes.size(), 3u);
    EXPECT_EQ(platform.Canned().messageBoxes[0].severity, MessageBoxSeverity::Information);
    EXPECT_EQ(platform.Canned().messageBoxes[1].severity, MessageBoxSeverity::Warning);
    EXPECT_EQ(platform.Canned().messageBoxes[2].severity, MessageBoxSeverity::Error);
}

TEST_F(MessageBoxTest, ADismissedDialogReportsNoChoice)
{
    platform.Canned().chosenButton = -1;
    EXPECT_EQ(MessageBox::Show(MessageBoxType::Information, "t", "m", {"OK"}), -1);
}

namespace
{
    /// A platform with no message box at all, which is what HEADLESS reports.
    class MessageBoxlessPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::PlatformCapabilities GetCapabilities() const override
        {
            CNA::Platform::PlatformCapabilities capabilities =
                PlatformTestDecorator::GetCapabilities();
            capabilities.messageBox = false;
            return capabilities;
        }
    };
}

TEST(MessageBoxWithoutAServiceTest, ReportsUnsupportedAndDoesNothingRatherThanFailing)
{
    // The branch that did not exist before the migration. This is usually the last thing a game
    // does on its way out, so it must not take the process down on a host with no one to show it
    // to -- and Show's -1 is already this API's "no choice was made".
    MessageBoxlessPlatform platform;
    const ScopedCurrentPlatform installed(platform);

    EXPECT_FALSE(MessageBox::getIsSupportedProperty());
    EXPECT_NO_THROW(MessageBox::ShowSimple(MessageBoxType::Error, "t", "m"));
    EXPECT_EQ(MessageBox::Show(MessageBoxType::Error, "t", "m", {"OK"}), -1);
}

// The concurrency test that used to live here raced a caller thread against
// SetBackendForTesting, pinning REMED-DEVICES-001: a swap could destroy the backend while a call
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
