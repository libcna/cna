// SPDX-License-Identifier: MS-PL
//
// PLAT-88: CNA::Input::Clipboard reads the platform, not SDL.

#include <gtest/gtest.h>

#include "CNA/Input/Clipboard.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <memory>
#include <string>

using CNA::Input::Clipboard;
using CNA::Platform::GetCurrentPlatform;
using CNA::Platform::PlatformSubsystem;

// A clipboard needs the video subsystem, which needs a display. On a headless box these run under
// Xvfb; without one they skip, matching the real-window MouseCursor and TextInputEXT tests. Within
// one process the platform owns the selection and returns its own cached text, so a set-then-get
// round-trip is reliable with no system clipboard manager present.
namespace
{
    class CnaInputClipboardTest : public ::testing::Test
    {
    protected:
        bool videoUp_ = false;
        std::string saved_;

        void SetUp() override
        {
            try
            {
                GetCurrentPlatform().AcquireSubsystem(PlatformSubsystem::Video);
                videoUp_ = true;
            }
            catch (const std::exception& error)
            {
                GTEST_SKIP() << "no video subsystem (no display): " << error.what();
            }

            // Video being up is necessary but not sufficient: HEADLESS has a video subsystem and
            // no clipboard at all. The round-trip's real precondition is a clipboard service, and
            // its absence is covered by its own test below rather than by a failure here.
            if (GetCurrentPlatform().GetClipboard() == nullptr)
            {
                GTEST_SKIP() << "this platform reports no clipboard";
            }
            saved_ = Clipboard::GetTextEXT();
        }

        void TearDown() override
        {
            if (videoUp_)
            {
                Clipboard::SetTextEXT(saved_);  // restore whatever was there
                GetCurrentPlatform().ReleaseSubsystem(PlatformSubsystem::Video);
            }
        }
    };
}

TEST_F(CnaInputClipboardTest, SetTextThenGetTextRoundTripsIncludingUtf8)
{
    const std::string text = "cna clipboard \xC3\xA9\xE2\x82\xAC";  // ASCII + U+00E9 + U+20AC
    Clipboard::SetTextEXT(text);

    EXPECT_EQ(Clipboard::GetTextEXT(), text);
    EXPECT_TRUE(Clipboard::HasTextEXT());
}

TEST_F(CnaInputClipboardTest, EmptyTextLeavesNoText)
{
    Clipboard::SetTextEXT("");

    EXPECT_EQ(Clipboard::GetTextEXT(), std::string());
    EXPECT_FALSE(Clipboard::HasTextEXT());
}

namespace
{
    /// A platform with no clipboard at all, which is what the Clipboard capability being false
    /// means. HEADLESS is one; a terminal will be another.
    class ClipboardlessPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override { return nullptr; }
    };
}

TEST(CnaInputClipboardWithoutAServiceTest, ReportsEmptyAndIgnoresWritesRatherThanFailing)
{
    // This branch did not exist before the migration -- SDL's clipboard functions were always
    // callable. A game implementing a text field calls all three unconditionally, so each has to
    // answer rather than throw, and the answer has to be the same one an empty clipboard gives.
    ClipboardlessPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    EXPECT_NO_THROW(Clipboard::SetTextEXT("ignored"));
    EXPECT_EQ(Clipboard::GetTextEXT(), std::string());
    EXPECT_FALSE(Clipboard::HasTextEXT());
}
