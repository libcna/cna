// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <string>

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::charcs;
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

TEST_F(SdlInputBridgeTextInputTest, TextInputEventForwardsAsciiAsCodeUnits)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    SdlInputBridge::ProcessEvent(textInputEvent("abc"));

    EXPECT_EQ(captured, u"abc");
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesTwoByteUtf8ToSingleCodeUnit)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "é" is U+00E9 -> UTF-8 0xC3 0xA9. The bridge decodes to UTF-16, so it arrives as ONE
    // code unit 0x00E9 (matching FNA's Encoding.UTF8.GetChars), not two raw bytes.
    SdlInputBridge::ProcessEvent(textInputEvent("\xC3\xA9"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{0x00E9});
}

TEST_F(SdlInputBridgeTextInputTest, ControlKeysSynthesizeTextInputCharacters)
{
    struct Case { SDL_Keycode key; charcs expected; };
    const Case cases[] = {
        {SDLK_HOME,      charcs{2}},
        {SDLK_END,       charcs{3}},
        {SDLK_BACKSPACE, charcs{8}},
        {SDLK_TAB,       charcs{9}},
        {SDLK_RETURN,    charcs{13}},
        {SDLK_DELETE,    charcs{127}},
    };

    for (const Case& c : cases)
    {
        std::u16string captured;
        TextInputEXT::TextInput = [&captured](charcs ch) { captured += ch; };

        SdlInputBridge::ProcessEvent(keyEvent(true, c.key));
        SdlInputBridge::ProcessEvent(keyEvent(false, c.key));

        ASSERT_EQ(captured.size(), 1u) << "keycode " << c.key;
        EXPECT_EQ(captured[0], c.expected) << "keycode " << c.key;
    }
}

TEST_F(SdlInputBridgeTextInputTest, KeyRepeatReemitsControlCharacter)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_BACKSPACE, /*repeat=*/false));
    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_BACKSPACE, /*repeat=*/true));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_BACKSPACE));

    // First press + repeat both emit the control char (FNA re-emits on repeat).
    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{8});
    EXPECT_EQ(captured[1], charcs{8});
}

TEST_F(SdlInputBridgeTextInputTest, CtrlVEmitsPasteCharAndSuppressesLiteralText)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_LCTRL));
    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_V));
    // SDL also delivers the literal 'v' as TEXT_INPUT; it must be suppressed.
    SdlInputBridge::ProcessEvent(textInputEvent("v"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{22});

    // After releasing the keys, suppression clears and text flows again.
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_V));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_LCTRL));

    captured.clear();
    SdlInputBridge::ProcessEvent(textInputEvent("x"));
    EXPECT_EQ(captured, u"x");
}

TEST_F(SdlInputBridgeTextInputTest, PlainVWithoutCtrlIsNotSuppressed)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    SdlInputBridge::ProcessEvent(keyEvent(true, SDLK_V)); // no Ctrl held -> no paste, no suppress
    SdlInputBridge::ProcessEvent(textInputEvent("v"));
    SdlInputBridge::ProcessEvent(keyEvent(false, SDLK_V));

    EXPECT_EQ(captured, u"v");
}

// --- Task 807: Unicode decoding of TEXT_INPUT (UTF-8 -> UTF-16 code units) ---
// CNA's chosen semantics (task 806): TextInput fires once per UTF-16 code unit, matching FNA's
// Action<char>. A BMP code point is one call; an astral code point (> U+FFFF) is two calls
// (a high then a low surrogate), exactly like FNA's C# char stream.

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesThreeByteUtf8ToSingleCodeUnit)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "€" is U+20AC -> UTF-8 E2 82 AC -> one BMP UTF-16 code unit 0x20AC.
    SdlInputBridge::ProcessEvent(textInputEvent("\xE2\x82\xAC"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{0x20AC});
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesAstralEmojiToSurrogatePair)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "😀" is U+1F600 -> UTF-8 F0 9F 98 80 -> UTF-16 surrogate pair D83D DE00 (two calls),
    // matching FNA's C# char stream for astral code points.
    SdlInputBridge::ProcessEvent(textInputEvent("\xF0\x9F\x98\x80"));

    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{0xD83D}); // high surrogate
    EXPECT_EQ(captured[1], charcs{0xDE00}); // low surrogate
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesCombiningCharactersAsSeparateCodeUnits)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "e" + combining acute accent U+0301 (UTF-8 65 CC 81): a base letter plus a separate
    // combining mark — two distinct code units, not a single precomposed character.
    SdlInputBridge::ProcessEvent(textInputEvent("e\xCC\x81"));

    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{0x0065}); // 'e'
    EXPECT_EQ(captured[1], charcs{0x0301}); // combining acute accent
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesCzechDiacritics)
{
    // Task 852 (headless part): Czech diacritics are 2-byte UTF-8 (U+00xx / U+01xx) — the decode
    // must yield one BMP code unit each. "žluťoučký": ž U+017E, ť U+0165, č U+010D, ý U+00FD.
    // (Real IME/keyboard typing of these is a separate, human-gated check — see
    // docs/input-manual-verification-results.md.)
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    SdlInputBridge::ProcessEvent(textInputEvent("\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD"));

    const std::u16string expected = {0x017E, u'l', u'u', 0x0165, u'o', u'u', 0x010D, u'k', 0x00FD};
    EXPECT_EQ(captured, expected);
}

TEST_F(SdlInputBridgeTextInputTest, TextInputEventDecodesMixedWidthStringInOrder)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "aé€😀": 1-byte 'a', 2-byte é, 3-byte €, 4-byte emoji (surrogate pair) — 5 code units total.
    SdlInputBridge::ProcessEvent(textInputEvent("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));

    const std::u16string expected = {0x0061, 0x00E9, 0x20AC, 0xD83D, 0xDE00};
    EXPECT_EQ(captured, expected);
}

TEST_F(SdlInputBridgeTextInputTest, TextEditingForwardsMultiByteUtf8CompositionUnchanged)
{
    // IME composition (TextEditing) keeps FNA's Action<string,int,int> shape; CNA models the
    // C# string as a UTF-8 std::string (a documented, separate deviation from the per-code-unit
    // TextInput event). A multi-byte draft passes through byte-for-byte.
    std::string text;
    TextInputEXT::TextEditing = [&](const std::string& t, int, int) { text = t; };

    SdlInputBridge::ProcessEvent(textEditingEvent("caf\xC3\xA9", 0, 4)); // "café"

    EXPECT_EQ(text, std::string("caf\xC3\xA9"));
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
