// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <string>
#include <vector>

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Internal::Input::SdlInputBridge;
using CNA::Platform::KeyCode;
using CNA::Platform::KeyEvent;
using CNA::Platform::PlatformEvent;
using CNA::Platform::TextEditingEvent;
using CNA::Platform::TextInputEvent;
using Microsoft::Xna::Framework::Input::charcs;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::TextInputEXT;

namespace
{
    PlatformEvent keyEvent(const bool down, const Keys key, const bool repeat = false)
    {
        KeyEvent event;
        event.keycode = static_cast<KeyCode>(static_cast<std::uint16_t>(key));
        event.pressed = down;
        event.repeat = repeat;
        return event;
    }

    PlatformEvent textInputEvent(const char* text)
    {
        return TextInputEvent{0, text};
    }

    PlatformEvent textEditingEvent(const char* text, const int start, const int length)
    {
        return TextEditingEvent{0, text, start, length};
    }

    // Exercises the platform-independent text-input state machine (plans/plan_input.md Tasks 704-706).
    class PlatformInputBridgeTextInputTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reset(); }
        void TearDown() override { Reset(); }

        // Bridge suppress/control-down flags + finger-id map + scancode override are file-local
        // statics; SdlInputBridge::ResetForTests() (task 874) clears them centrally. Also reset the
        // keyboard state these tests touch so control_key_held() etc. start clean.
        static void Reset()
        {
            TextInputEXT::TextInput = nullptr;
            TextInputEXT::TextEditing = nullptr;
            SdlInputBridge::ResetForTests();
            for (const Keys k : {Keys::Back, Keys::Enter, Keys::Tab, Keys::Delete,
                                 Keys::Home, Keys::End, Keys::V,
                                 Keys::LeftControl, Keys::RightControl})
            {
                InputManager::SetKeyState(k, false);
            }
        }
    };
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventForwardsAsciiAsCodeUnits)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("abc"));

    EXPECT_EQ(captured, u"abc");
}

// P7-003(b): an empty TextInputEvent has nothing to decode, so it delivers zero calls.
TEST_F(PlatformInputBridgeTextInputTest, EmptyTextInputEventDeliversNoCodeUnits)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent(""));

    EXPECT_TRUE(captured.empty());
}

// P7-003(d): multiple TextInput subscribers fire in REGISTRATION order (multicast delegate semantics).
TEST_F(PlatformInputBridgeTextInputTest, TextInputSubscribersFireInRegistrationOrder)
{
    std::vector<int> order;
    TextInputEXT::TextInput += [&order](charcs) { order.push_back(1); };
    TextInputEXT::TextInput += [&order](charcs) { order.push_back(2); };
    TextInputEXT::TextInput += [&order](charcs) { order.push_back(3); };

    PlatformInputBridge::ProcessEvent(textInputEvent("a")); // one code unit -> one dispatch round

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesTwoByteUtf8ToSingleCodeUnit)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "é" is U+00E9 -> UTF-8 0xC3 0xA9. The bridge decodes to UTF-16, so it arrives as ONE
    // code unit 0x00E9 (matching FNA's Encoding.UTF8.GetChars), not two raw bytes.
    PlatformInputBridge::ProcessEvent(textInputEvent("\xC3\xA9"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{0x00E9});
}

// DEC-08: malformed UTF-8 is replaced with U+FFFD (matching FNA's Encoding.UTF8), not dropped.
// (These byte sequences cannot actually come from SDL, which emits valid UTF-8, but the decoder is
// defensive and must match FNA.) String literals are split so a hex escape does not eat the next
// character (e.g. "\xFF" "b", since 'b' is a hex digit).
TEST_F(PlatformInputBridgeTextInputTest, InvalidLeadByteBecomesReplacementCharAndPreservesSurroundingText)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("a\xFF" "b"));
    EXPECT_EQ(captured, u"a\uFFFDb");
}

TEST_F(PlatformInputBridgeTextInputTest, TruncatedMultiByteSequenceBecomesReplacementChar)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("x\xC3"));
    EXPECT_EQ(captured, u"x\uFFFD");
}

TEST_F(PlatformInputBridgeTextInputTest, BadContinuationEmitsReplacementCharThenResyncsToValidText)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("\xE0" "A"));
    EXPECT_EQ(captured, u"\uFFFDA");
}

TEST_F(PlatformInputBridgeTextInputTest, OverlongEncodingBecomesReplacementChar)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("\xC0\x80"));
    EXPECT_EQ(captured, u"\uFFFD");
}

TEST_F(PlatformInputBridgeTextInputTest, SurrogateCodePointEncodedInUtf8BecomesReplacementChar)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("\xED\xA0\x80"));
    EXPECT_EQ(captured, u"\uFFFD");
}

TEST_F(PlatformInputBridgeTextInputTest, ControlKeysSynthesizeTextInputCharacters)
{
    struct Case { Keys key; charcs expected; };
    const Case cases[] = {
        {Keys::Home,   charcs{2}},
        {Keys::End,    charcs{3}},
        {Keys::Back,   charcs{8}},
        {Keys::Tab,    charcs{9}},
        {Keys::Enter,  charcs{13}},
        {Keys::Delete, charcs{127}},
    };

    for (const Case& c : cases)
    {
        std::u16string captured;
        TextInputEXT::TextInput = [&captured](charcs ch) { captured += ch; };

        PlatformInputBridge::ProcessEvent(keyEvent(true, c.key));
        PlatformInputBridge::ProcessEvent(keyEvent(false, c.key));

        ASSERT_EQ(captured.size(), 1u) << "key " << static_cast<int>(c.key);
        EXPECT_EQ(captured[0], c.expected) << "key " << static_cast<int>(c.key);
    }
}

// P7-006(b): a repeated (non-control) TEXT_INPUT stream is delivered per event — text input is decoded
// independently per SDL event and is NOT de-duplicated, so two identical events yield two code units.
TEST_F(PlatformInputBridgeTextInputTest, RepeatedTextInputEventsAreEachDelivered)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(textInputEvent("a"));
    PlatformInputBridge::ProcessEvent(textInputEvent("a"));

    EXPECT_EQ(captured, u"aa");
}

TEST_F(PlatformInputBridgeTextInputTest, KeyRepeatReemitsControlCharacter)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::Back, /*repeat=*/false));
    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::Back, /*repeat=*/true));
    PlatformInputBridge::ProcessEvent(keyEvent(false, Keys::Back));

    // First press + repeat both emit the control char (FNA re-emits on repeat).
    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{8});
    EXPECT_EQ(captured[1], charcs{8});
}

TEST_F(PlatformInputBridgeTextInputTest, CtrlVEmitsPasteCharAndSuppressesLiteralText)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::LeftControl));
    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::V));
    PlatformInputBridge::ProcessEvent(textInputEvent("v"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{22});

    // After releasing the keys, suppression clears and text flows again.
    PlatformInputBridge::ProcessEvent(keyEvent(false, Keys::V));
    PlatformInputBridge::ProcessEvent(keyEvent(false, Keys::LeftControl));

    captured.clear();
    PlatformInputBridge::ProcessEvent(textInputEvent("x"));
    EXPECT_EQ(captured, u"x");
}

// P8-008(a): SdlInputBridge::ResetForTests must clear the text-input suppression flag in isolation. Enter a
// Ctrl+V paste (which turns suppression ON to swallow the literal echo), reset WITHOUT releasing the keys,
// then confirm a following TEXT_INPUT flows normally — proving reset cleared the flag rather than leaving it
// stuck for the next test.
TEST_F(PlatformInputBridgeTextInputTest, ResetForTestsClearsTextInputSuppressionFlag)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::LeftControl));
    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::V));
    PlatformInputBridge::ProcessEvent(textInputEvent("v"));
    captured.clear();

    SdlInputBridge::ResetForTests(); // must clear g_textInputSuppress (and the control-down flags)

    // ResetForTests does not touch TextInputEXT's callback, so the subscriber above is still registered.
    PlatformInputBridge::ProcessEvent(textInputEvent("x"));
    EXPECT_EQ(captured, u"x") << "reset must clear the paste-suppression flag so text flows again";
}

TEST_F(PlatformInputBridgeTextInputTest, CtrlVSuppressionDoesNotStickWhenCtrlReleasedWithoutVKeyUp)
{
    // Task 875: the Ctrl+V paste-echo suppression must not get stuck if the V key-up is missing or
    // out of order. Suppression is intentionally scoped to the Ctrl+V key lifecycle and clears on
    // EITHER a V key-up OR a Ctrl key-up (SdlInputBridge.cpp handle_text_input_key_up), so releasing
    // Ctrl alone re-enables text input — later unrelated TEXT_INPUT is never swallowed indefinitely.
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::LeftControl));
    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::V));
    PlatformInputBridge::ProcessEvent(textInputEvent("v"));
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{22});                     // only the paste control char

    // Release Ctrl WITHOUT ever releasing V. Suppression must clear anyway.
    PlatformInputBridge::ProcessEvent(keyEvent(false, Keys::LeftControl));

    captured.clear();
    PlatformInputBridge::ProcessEvent(textInputEvent("x"));
    EXPECT_EQ(captured, u"x");
}

TEST_F(PlatformInputBridgeTextInputTest, PlainVWithoutCtrlIsNotSuppressed)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(keyEvent(true, Keys::V));
    PlatformInputBridge::ProcessEvent(textInputEvent("v"));
    PlatformInputBridge::ProcessEvent(keyEvent(false, Keys::V));

    EXPECT_EQ(captured, u"v");
}

// --- Task 807: Unicode decoding of TEXT_INPUT (UTF-8 -> UTF-16 code units) ---
// CNA's chosen semantics (task 806): TextInput fires once per UTF-16 code unit, matching FNA's
// Action<char>. A BMP code point is one call; an astral code point (> U+FFFF) is two calls
// (a high then a low surrogate), exactly like FNA's C# char stream.

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesThreeByteUtf8ToSingleCodeUnit)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "€" is U+20AC -> UTF-8 E2 82 AC -> one BMP UTF-16 code unit 0x20AC.
    PlatformInputBridge::ProcessEvent(textInputEvent("\xE2\x82\xAC"));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], charcs{0x20AC});
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesAstralEmojiToSurrogatePair)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "😀" is U+1F600 -> UTF-8 F0 9F 98 80 -> UTF-16 surrogate pair D83D DE00 (two calls),
    // matching FNA's C# char stream for astral code points.
    PlatformInputBridge::ProcessEvent(textInputEvent("\xF0\x9F\x98\x80"));

    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{0xD83D}); // high surrogate
    EXPECT_EQ(captured[1], charcs{0xDE00}); // low surrogate
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesCombiningCharactersAsSeparateCodeUnits)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "e" + combining acute accent U+0301 (UTF-8 65 CC 81): a base letter plus a separate
    // combining mark — two distinct code units, not a single precomposed character.
    PlatformInputBridge::ProcessEvent(textInputEvent("e\xCC\x81"));

    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], charcs{0x0065}); // 'e'
    EXPECT_EQ(captured[1], charcs{0x0301}); // combining acute accent
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesCzechDiacritics)
{
    // Task 852 (headless part): Czech diacritics are 2-byte UTF-8 (U+00xx / U+01xx) — the decode
    // must yield one BMP code unit each. "žluťoučký": ž U+017E, ť U+0165, č U+010D, ý U+00FD.
    // (Real IME/keyboard typing of these is a separate, human-gated check — see
    // docs/input-manual-verification-results.md.)
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    PlatformInputBridge::ProcessEvent(
        textInputEvent("\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD"));

    const std::u16string expected = {0x017E, u'l', u'u', 0x0165, u'o', u'u', 0x010D, u'k', 0x00FD};
    EXPECT_EQ(captured, expected);
}

TEST_F(PlatformInputBridgeTextInputTest, TextInputEventDecodesMixedWidthStringInOrder)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    // "aé€😀": 1-byte 'a', 2-byte é, 3-byte €, 4-byte emoji (surrogate pair) — 5 code units total.
    PlatformInputBridge::ProcessEvent(
        textInputEvent("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));

    const std::u16string expected = {0x0061, 0x00E9, 0x20AC, 0xD83D, 0xDE00};
    EXPECT_EQ(captured, expected);
}

TEST_F(PlatformInputBridgeTextInputTest, TextEditingForwardsMultiByteUtf8CompositionUnchanged)
{
    // IME composition (TextEditing) keeps FNA's Action<string,int,int> shape; CNA models the
    // C# string as a UTF-8 std::string (a documented, separate deviation from the per-code-unit
    // TextInput event). A multi-byte draft passes through byte-for-byte.
    std::string text;
    TextInputEXT::TextEditing = [&](const std::string& t, int, int) { text = t; };

    PlatformInputBridge::ProcessEvent(textEditingEvent("caf\xC3\xA9", 0, 4));

    EXPECT_EQ(text, std::string("caf\xC3\xA9"));
}

TEST_F(PlatformInputBridgeTextInputTest, TextEditingEventForwardsTextStartLength)
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

    PlatformInputBridge::ProcessEvent(textEditingEvent("draft", 1, 2));

    EXPECT_EQ(text, "draft");
    EXPECT_EQ(start, 1);
    EXPECT_EQ(length, 2);
}

// P7-007(d): TextEditing start/length are SDL's raw BYTE offsets into the UTF-8 composition string, passed
// through unchanged (NOT converted to UTF-16 code-unit indices — documented in TextInputEXT.hpp
// INPUT-TEXT-016). Composition "éxy" = bytes C3 A9 'x' 'y'; byte offset 2 points at 'x', whose UTF-16 index
// would be 1. CNA must report the byte offset (2), which discriminates byte- from UTF-16-semantics.
TEST_F(PlatformInputBridgeTextInputTest, TextEditingStartLengthAreRawByteOffsetsNotUtf16Indices)
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

    PlatformInputBridge::ProcessEvent(textEditingEvent("\xC3\xA9xy", 2, 1));

    EXPECT_EQ(text, std::string("\xC3\xA9xy")); // UTF-8 bytes preserved unchanged
    EXPECT_EQ(start, 2);   // byte offset (the UTF-16 index of 'x' would be 1) -> byte, not UTF-16, semantics
    EXPECT_EQ(length, 1);
}

TEST_F(PlatformInputBridgeTextInputTest, TextEditingEmptyCompositionForwardsZeroes)
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
    PlatformInputBridge::ProcessEvent(textEditingEvent("", 5, 5));

    EXPECT_TRUE(called);
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(start, 0);
    EXPECT_EQ(length, 0);
}
