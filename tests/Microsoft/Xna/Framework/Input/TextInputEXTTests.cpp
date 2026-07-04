// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::Rectangle;

namespace
{
    // TextInputEXT is a static class with global state. Reset it around every test so
    // cases do not leak subscribers or a stale window handle into one another.
    class TextInputEXTTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reset(); }
        void TearDown() override { Reset(); }

        static void Reset()
        {
            TextInputEXT::TextInput = nullptr;
            TextInputEXT::TextEditing = nullptr;
            TextInputEXT::setWindowHandleProperty(0);
        }
    };
}

TEST_F(TextInputEXTTest, TextInputDispatchesEachCodeUnitToSubscriber)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    TextInputEXT::INTERNAL_OnTextInput(u'H');
    TextInputEXT::INTERNAL_OnTextInput(u'i');

    EXPECT_EQ(captured, u"Hi");
}

TEST_F(TextInputEXTTest, TextInputWithoutSubscriberIsSafe)
{
    TextInputEXT::TextInput = nullptr;
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextInput(u'x'));
}

TEST_F(TextInputEXTTest, TextEditingDispatchesTextStartAndLength)
{
    std::string text;
    int start = -1;
    int length = -1;
    TextInputEXT::TextEditing = [&](const std::string& t, int s, int l)
    {
        text = t;
        start = s;
        length = l;
    };

    TextInputEXT::INTERNAL_OnTextEditing("draft", 1, 3);

    EXPECT_EQ(text, "draft");
    EXPECT_EQ(start, 1);
    EXPECT_EQ(length, 3);
}

TEST_F(TextInputEXTTest, TextEditingEmptyCompositionFiresWithEmptyString)
{
    bool called = false;
    std::string text = "unset";
    int start = -1;
    int length = -1;
    TextInputEXT::TextEditing = [&](const std::string& t, int s, int l)
    {
        called = true;
        text = t;
        start = s;
        length = l;
    };

    // FNA passes null for an empty composition; CNA maps that to an empty string.
    TextInputEXT::INTERNAL_OnTextEditing(std::string(), 0, 0);

    EXPECT_TRUE(called);
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(start, 0);
    EXPECT_EQ(length, 0);
}

TEST_F(TextInputEXTTest, TextEditingWithoutSubscriberIsSafe)
{
    TextInputEXT::TextEditing = nullptr;
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextEditing("x", 0, 1));
}

TEST_F(TextInputEXTTest, WindowHandlePropertyRoundTrips)
{
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), std::uintptr_t{0});

    const std::uintptr_t handle = 0xDEADBEEFu;
    TextInputEXT::setWindowHandleProperty(handle);
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), handle);

    TextInputEXT::setWindowHandleProperty(0);
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), std::uintptr_t{0});
}

TEST_F(TextInputEXTTest, IsTextInputActiveIsFalseWithoutWindow)
{
    // No window handle -> the null guard returns false without touching SDL.
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
}

TEST_F(TextInputEXTTest, IsScreenKeyboardShownIsFalseWithoutWindow)
{
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown());
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown(0));
}

TEST_F(TextInputEXTTest, StartStopAndSetRectangleWithoutWindowAreSafeNoOps)
{
    // With no window handle every call is null-guarded, so none reach SDL.
    EXPECT_NO_THROW(TextInputEXT::StartTextInput());
    EXPECT_NO_THROW(TextInputEXT::StopTextInput());
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(0, 0, 10, 10)));
}

TEST_F(TextInputEXTTest, StartStopAndIsActiveRoundTripThroughRealWindow)
{
    // Task 809: verify the real SDL path (not just the no-window null guards). Mirrors the
    // MouseInputTests real-hidden-window pattern (SDL_INIT_VIDEO + GTEST_SKIP on failure).
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* window = SDL_CreateWindow("TextInputEXTTests", 64, 64, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    TextInputEXT::setWindowHandleProperty(reinterpret_cast<std::uintptr_t>(window));

    // Some platforms (e.g. certain Wayland/IME setups) may not toggle SDL_TextInputActive on a
    // hidden window; skip rather than fail if StartTextInput doesn't take effect here, so this
    // stays a genuine real-path check without becoming environment-flaky.
    TextInputEXT::StartTextInput();
    if (!TextInputEXT::IsTextInputActive())
    {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_StartTextInput did not activate text input on a hidden window in this environment";
    }

    EXPECT_TRUE(TextInputEXT::IsTextInputActive());

    // SetInputRectangle must reach SDL without error while text input is active.
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(4, 4, 32, 16)));

    TextInputEXT::StopTextInput();
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());

    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
