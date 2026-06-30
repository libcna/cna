// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

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

TEST_F(TextInputEXTTest, TextInputDispatchesEachCharToSubscriber)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    TextInputEXT::INTERNAL_OnTextInput('H');
    TextInputEXT::INTERNAL_OnTextInput('i');

    EXPECT_EQ(captured, "Hi");
}

TEST_F(TextInputEXTTest, TextInputWithoutSubscriberIsSafe)
{
    TextInputEXT::TextInput = nullptr;
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextInput('x'));
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
