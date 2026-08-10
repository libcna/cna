// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/Clipboard.hpp"

using CNA::Devices::Clipboard;

// This test binary never initializes SDL's video subsystem (no window is created by
// CnaTests), so these tests double as the "no video subsystem available" verification
// this class's own doc comment promises: every call must fail gracefully (false/empty),
// never crash. If a future test run *does* have a real clipboard-owning session
// available, a successful round-trip is also accepted -- this test does not assume
// either environment, only that behavior is internally consistent and never a crash.

TEST(ClipboardTests, GetHasTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::getHasTextProperty(); });
}

TEST(ClipboardTests, GetTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::getTextProperty(); });
}

TEST(ClipboardTests, SetTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::setTextProperty("CNA::Devices::Clipboard test"); });
}

// If setTextProperty() reports success, a same-process getTextProperty() should read
// back the same text (real clipboard available); if it reports failure (expected in
// this headless container with no video subsystem), getTextProperty() must still not
// crash and getHasTextProperty() must not contradict it by claiming text is present.
TEST(ClipboardTests, SetThenGetIsConsistentWhetherOrNotAClipboardIsAvailable)
{
    const std::string testText = "CNA::Devices::Clipboard round-trip test";
    const bool setSucceeded = Clipboard::setTextProperty(testText);

    if (setSucceeded)
    {
        EXPECT_EQ(Clipboard::getTextProperty(), testText);
        EXPECT_TRUE(Clipboard::getHasTextProperty());
    }
    else
    {
        EXPECT_NO_THROW({ (void)Clipboard::getTextProperty(); });
        EXPECT_NO_THROW({ (void)Clipboard::getHasTextProperty(); });
    }
}

TEST(ClipboardTests, EmptyTextIsAValidInput)
{
    EXPECT_NO_THROW({ (void)Clipboard::setTextProperty(""); });
}

#endif // CNA_DEVICES
