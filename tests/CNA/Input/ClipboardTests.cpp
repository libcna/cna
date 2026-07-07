// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/Clipboard.hpp"

#include <SDL3/SDL.h>

#include <string>

using CNA::Input::Clipboard;

// SDL's clipboard requires the video subsystem. On a headless box run under Xvfb + SDL_VIDEODRIVER=x11;
// skip if video is unavailable (mirrors the real-window MouseCursor / TextInputEXT tests). Within one
// process SDL owns the selection and returns its own cached text, so a set->get round-trip is reliable
// without a system clipboard manager.
namespace
{
    class CnaInputClipboardTest : public ::testing::Test
    {
    protected:
        bool videoUp_ = false;
        std::string saved_;

        void SetUp() override
        {
            videoUp_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
            if (!videoUp_)
            {
                GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed (no display): " << SDL_GetError();
            }
            saved_ = Clipboard::GetTextEXT();
        }

        void TearDown() override
        {
            if (videoUp_)
            {
                Clipboard::SetTextEXT(saved_); // restore whatever was there
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }
        }
    };
}

TEST_F(CnaInputClipboardTest, SetTextThenGetTextRoundTripsIncludingUtf8)
{
    const std::string text = "cna clipboard \xC3\xA9\xE2\x82\xAC"; // ASCII + U+00E9 + U+20AC
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
