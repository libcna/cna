// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <string>

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::TextInputEXT;

namespace
{
    SDL_Event keyEvent(const bool down, const SDL_Keycode key, const bool repeat = false)
    {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = key;
        e.key.repeat = repeat;
        return e;
    }

    SDL_Event textInputEvent(const char* text)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.text = text;
        return e;
    }

    SDL_Event textEditingEvent(const char* text, const int start, const int length)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_TEXT_EDITING;
        e.edit.text = text;
        e.edit.start = start;
        e.edit.length = length;
        return e;
    }

    // Exercises SdlInputBridge::ProcessEvent text-input handling (plan_input.md Tasks 704-706)
    // by feeding synthetic SDL events. The key/text paths never touch a window, so no SDL_Init
    // is required.
    class SdlInputBridgeTextInputTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reset(); }
        void TearDown() override { Reset(); }

        // Bridge suppress/control-down flags are file-local statics; clear them by releasing
        // the keys that set them, and reset the keyboard state these tests touch.
        static void Reset()
        {
            TextInputEXT::TextInput = nullptr;
            TextInputEXT::TextEditing = nullptr;
            SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_V));
            SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_LCTRL));
            SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_RCTRL));
            for (const Keys k : {Keys::Back, Keys::Enter, Keys::Tab, Keys::Delete,
                                 Keys::Home, Keys::End, Keys::V,
                                 Keys::LeftControl, Keys::RightControl})
            {
                InputManager::SetKeyState(k, false);
            }
        }
    };
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventForwardsAsciiBytes)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    SdlInputBridge::ProcessEvent(textInputEvent("abc"));

    EXPECT_EQ(captured, "abc");
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventForwardsUtf8BytesInOrder)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    // "é" is U+00E9 -> UTF-8 0xC3 0xA9. Forwarding the bytes in order rebuilds the string.
    SdlInputBridge::ProcessEvent(textInputEvent("\xC3\xA9"));

    EXPECT_EQ(captured, std::string("\xC3\xA9"));
}

TEST_F(SdlInputBridgeTextInputTest, ControlKeysSynthesizeTextInputCharacters)
{
    struct Case { SDL_Keycode key; char expected; };
    const Case cases[] = {
        {SDLK_HOME,      static_cast<char>(2)},
        {SDLK_END,       static_cast<char>(3)},
        {SDLK_BACKSPACE, static_cast<char>(8)},
        {SDLK_TAB,       static_cast<char>(9)},
        {SDLK_RETURN,    static_cast<char>(13)},
        {SDLK_DELETE,    static_cast<char>(127)},
    };

    for (const Case& c : cases)
    {
        std::string captured;
        TextInputEXT::TextInput = [&captured](char ch) { captured += ch; };

        SdlInputBridge::ProcessEvent(keyEvent(true, c.key));
        SdlInputBridge::ProcessEvent(keyEvent(false, c.key));

        ASSERT_EQ(captured.size(), 1u) << "keycode " << c.key;
        EXPECT_EQ(captured[0], c.expected) << "keycode " << c.key;
    }
}

TEST_F(SdlInputBridgeTextInputTest, KeyRepeatReemitsControlCharacter)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_BACKSPACE, /*repeat=*/false));
    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_BACKSPACE, /*repeat=*/true));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_BACKSPACE));

    // First press + repeat both emit the control char (FNA re-emits on repeat).
    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], static_cast<char>(8));
    EXPECT_EQ(captured[1], static_cast<char>(8));
}

TEST_F(SdlInputBridgeTextInputTest, CtrlVEmitsPasteCharAndSuppressesLiteralText)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_LCTRL));
    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_V));
    // SDL also delivers the literal 'v' as TEXT_INPUT; it must be suppressed.
    SdlInputBridge::ProcessEvent(textInputEvent("v"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], static_cast<char>(22));

    // After releasing the keys, suppression clears and text flows again.
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_V));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_LCTRL));

    captured.clear();
    SdlInputBridge::ProcessEvent(textInputEvent("x"));
    EXPECT_EQ(captured, "x");
}

TEST_F(SdlInputBridgeTextInputTest, PlainVWithoutCtrlIsNotSuppressed)
{
    std::string captured;
    TextInputEXT::TextInput = [&captured](char c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_V)); // no Ctrl held -> no paste, no suppress
    SdlInputBridge::ProcessEvent(textInputEvent("v"));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_V));

    EXPECT_EQ(captured, "v");
}

TEST_F(SdlInputBridgeTextInputTest, TextEditingEventForwardsTextStartLength)
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

    SdlInputBridge::ProcessEvent(textEditingEvent("draft", 1, 2));

    EXPECT_EQ(text, "draft");
    EXPECT_EQ(start, 1);
    EXPECT_EQ(length, 2);
}

TEST_F(SdlInputBridgeTextInputTest, TextEditingEmptyCompositionForwardsZeroes)
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

    // Empty composition -> empty string with start/length forced to 0 (FNA passes null).
    SdlInputBridge::ProcessEvent(textEditingEvent("", 5, 5));

    EXPECT_TRUE(called);
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(start, 0);
    EXPECT_EQ(length, 0);
}
