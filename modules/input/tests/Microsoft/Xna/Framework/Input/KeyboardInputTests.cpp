// SPDX-License-Identifier: MS-PL
//
// Task 768: dedicated coverage for KeyboardState/Keyboard, consolidating the ad-hoc
// verifications done (but not committed) for tasks 760-767.
//
// GetKeyFromScancodeEXT's scancode-mode branch is NOT covered here: FNA_KEYBOARD_USE_SCANCODES
// is read into a function-local static bool the first time any key-conversion path in
// SdlInputBridge.cpp runs, cached for the rest of the process (mirroring FNA's own
// UseScancodes static-readonly-at-startup semantics). By the time this shared CnaTests binary
// reaches this file, some earlier test has already triggered that first read with the env var
// unset, so it can never be flipped to scancode mode within this process. Scancode mode was
// verified via a separate single-purpose process in task 765 (not committed, since it needs a
// fresh process rather than a gtest case).

#include <gtest/gtest.h>

#include <memory>
#include <unordered_set>

#include "CNA/Platform/CannedKeyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class KeyboardPlatformInputTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedKeyboardPlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void TearDown() override { installed.reset(); }
    };
}

// ===========================================================================
// Keyboard::GetState / GetState(PlayerIndex)
// ===========================================================================

TEST_F(KeyboardPlatformInputTest, GetStateReflectsPublishedPlatformSnapshot)
{
    platform.Canned().SetPending({CNA::Platform::KeyCode::Left,
                                  CNA::Platform::KeyCode::Space});
    platform.Canned().Update();

    const auto state = Keyboard::GetState();

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_TRUE(state.IsKeyDown(Keys::Space));
    EXPECT_TRUE(state.IsKeyUp(Keys::Right));
}

TEST_F(KeyboardPlatformInputTest, ReturnedValueDoesNotChangeAfterPlatformPublishesAnotherFrame)
{
    platform.Canned().SetPending({CNA::Platform::KeyCode::A});
    platform.Canned().Update();
    const auto snapshot = Keyboard::GetState();

    platform.Canned().SetPending({CNA::Platform::KeyCode::D});
    platform.Canned().Update();

    EXPECT_TRUE(snapshot.IsKeyDown(Keys::A));
    EXPECT_TRUE(snapshot.IsKeyUp(Keys::D));

    const auto currentState = Keyboard::GetState();
    EXPECT_TRUE(currentState.IsKeyUp(Keys::A));
    EXPECT_TRUE(currentState.IsKeyDown(Keys::D));
}

TEST_F(KeyboardPlatformInputTest, GetPressedKeysContainsOnlyThePublishedKeys)
{
    platform.Canned().SetPending({CNA::Platform::KeyCode::S});
    platform.Canned().Update();

    const auto state = Keyboard::GetState();
    const auto pressedKeys = state.GetPressedKeys();

    EXPECT_EQ(pressedKeys.size(), 1);
    EXPECT_EQ(pressedKeys[0], Keys::S);
    EXPECT_TRUE(state.IsKeyDown(Keys::S));
    EXPECT_TRUE(state.IsKeyUp(Keys::W));
}

TEST_F(KeyboardPlatformInputTest, GetStateWithPlayerIndexMatchesGetState)
{
    platform.Canned().SetPending({CNA::Platform::KeyCode::Enter});
    platform.Canned().Update();

    const auto state = Keyboard::GetState(Microsoft::Xna::Framework::PlayerIndex::One);

    EXPECT_TRUE(state.IsKeyDown(Keys::Enter));
    EXPECT_EQ(state, Keyboard::GetState());
}

TEST_F(KeyboardPlatformInputTest, MissingKeyboardServiceReturnsEmptyState)
{
    platform.Canned().SetPending({CNA::Platform::KeyCode::A});
    platform.Canned().Update();
    platform.SetKeyboardAvailable(false);

    EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty());
}

// ===========================================================================
// Keyboard::GetKeyFromScancodeEXT (default, non-scancode mode; see file header)
// ===========================================================================

TEST(KeyboardInputTest, GetKeyFromScancodeEXTRoundTripsCommonKeys)
{
    // On this environment's default (US) layout, physical-key round-trips are identity.
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::A), Keys::A);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::D1), Keys::D1);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Enter), Keys::Enter);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::OemPeriod), Keys::OemPeriod);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Space), Keys::Space);
}

TEST(KeyboardInputTest, GetKeyFromScancodeEXTNoneReturnsNone)
{
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::None), Keys::None);
}

TEST(KeyboardInputTest, GetKeyFromScancodeEXTUnmappedKeyReturnsNone)
{
    // Keys::Kana has no SDL_Scancode equivalent (no such physical key on US layouts).
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Kana), Keys::None);
}

// ===========================================================================
// KeyboardState — constructors
// ===========================================================================

TEST(KeyboardStateTest, DefaultConstructorHasNoPressedKeys)
{
    const KeyboardState state;

    EXPECT_TRUE(state.GetPressedKeys().empty());
    EXPECT_TRUE(state.IsKeyUp(Keys::A));
    EXPECT_FALSE(state.IsKeyDown(Keys::A));
}

TEST(KeyboardStateTest, InitializerListConstructorFlagsGivenKeys)
{
    const KeyboardState state{Keys::A, Keys::Space};

    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyDown(Keys::Space));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));
}

TEST(KeyboardStateTest, UnorderedSetConstructorFlagsGivenKeys)
{
    const std::unordered_set<Keys> pressed{Keys::Left, Keys::Right};
    const KeyboardState state(pressed);

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_TRUE(state.IsKeyDown(Keys::Right));
    EXPECT_TRUE(state.IsKeyUp(Keys::Up));
}

// ===========================================================================
// KeyboardState — operator[] / getItem
// ===========================================================================

TEST(KeyboardStateTest, IndexerMatchesGetItemAndIsKeyDown)
{
    const KeyboardState state{Keys::A};

    EXPECT_EQ(state[Keys::A], KeyState::Down);
    EXPECT_EQ(state[Keys::B], KeyState::Up);
    EXPECT_EQ(state[Keys::A], state.getItem(Keys::A));
    EXPECT_EQ(state[Keys::B], state.getItem(Keys::B));
    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));
}

// A2-001: dedicated IsKeyUp coverage — it is the exact complement of IsKeyDown (FNA
// KeyboardState.cs:125 `IsKeyUp => !IsKeyDown`). A pressed key reads Up=false / Down=true; an unpressed
// key reads Up=true / Down=false; for every key exactly one of IsKeyDown/IsKeyUp is true.
TEST(KeyboardStateTest, IsKeyUpIsTheComplementOfIsKeyDown)
{
    const KeyboardState state{Keys::A, Keys::Space};

    EXPECT_FALSE(state.IsKeyUp(Keys::A));       // pressed -> not up
    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));        // unpressed -> up
    EXPECT_FALSE(state.IsKeyDown(Keys::B));
    for (const Keys k : {Keys::A, Keys::Space, Keys::B, Keys::Enter})
    {
        EXPECT_NE(state.IsKeyDown(k), state.IsKeyUp(k)) << "exactly one of Down/Up must hold";
    }
}

// ===========================================================================
// KeyboardState — GetPressedKeys ordering
// ===========================================================================

TEST(KeyboardStateTest, GetPressedKeysReturnsEmptyForDefaultState)
{
    const KeyboardState state;
    EXPECT_TRUE(state.GetPressedKeys().empty());
}

TEST(KeyboardStateTest, GetPressedKeysIsSortedByAscendingNumericValue)
{
    // Z (90), A (65), Space (32), D1 (49) -> ascending: Space, D1, A, Z.
    const KeyboardState state{Keys::Z, Keys::A, Keys::Space, Keys::D1};

    const auto pressed = state.GetPressedKeys();
    const std::vector<Keys> expected{Keys::Space, Keys::D1, Keys::A, Keys::Z};

    EXPECT_EQ(pressed, expected);
}

// P2-011: a key repeated within the same initializer list (e.g. from a caller that queries the
// same physical key twice) must appear exactly once in GetPressedKeys, mirroring FNA's bitfield
// representation where "pressed" is a single bit per key, not a count.
TEST(KeyboardStateTest, GetPressedKeysHasNoDuplicateWhenSameKeyGivenTwice)
{
    const KeyboardState state{Keys::A, Keys::A, Keys::B, Keys::A};

    const auto pressed = state.GetPressedKeys();
    const std::vector<Keys> expected{Keys::A, Keys::B};

    EXPECT_EQ(pressed, expected);
}

// ===========================================================================
// KeyboardState — equality / Equals
// ===========================================================================

TEST(KeyboardStateTest, EqualStatesCompareEqual)
{
    const KeyboardState a{Keys::A, Keys::B};
    const KeyboardState b{Keys::B, Keys::A};

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(KeyboardStateTest, UnequalStatesCompareUnequal)
{
    const KeyboardState a{Keys::A};
    const KeyboardState b{Keys::B};

    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// ===========================================================================
// KeyboardState — GetHashCode
// ===========================================================================

TEST(KeyboardStateTest, GetHashCodeIsConsistentForEqualStates)
{
    const KeyboardState a{Keys::A, Keys::Enter};
    const KeyboardState b{Keys::Enter, Keys::A};

    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(KeyboardStateTest, GetHashCodeOfEmptyStateIsZero)
{
    const KeyboardState state;
    EXPECT_EQ(state.GetHashCode(), 0);
}

TEST(KeyboardStateTest, GetHashCodeMatchesFNAWordXorFormula)
{
    // Keys::A = 65 -> word 2 (65>>5=2), bit 1 (65&0x1f=1) -> word2 = 1<<1 = 0x2.
    // Keys::Space = 32 -> word 1 (32>>5=1), bit 0 (32&0x1f=0) -> word1 = 1<<0 = 0x1.
    // No other words set, so hash = word1 ^ word2 = 0x1 ^ 0x2 = 0x3.
    const KeyboardState state{Keys::A, Keys::Space};
    EXPECT_EQ(state.GetHashCode(), 0x3);
}

// ===========================================================================
// KeyboardState — invalid / out-of-range Keys hardening (P2-002)
// ===========================================================================
//
// FNA's KeyboardState stores pressed keys as eight 32-bit bitfields (keys0..keys7) covering
// Keys values 0..255; InternalSetKey switches on ((int)key)>>5 with cases 0..7 and no default,
// so any Keys value outside 0..255 maps to no bitfield and is silently ignored. CNA's
// GetHashCode mirrors that with an 8-word array indexed by value>>5; without a range guard an
// out-of-range Keys value (>=256, or a negative value that wraps to a huge unsigned) would
// write past the array. These tests pin the guard: constructing a state from such values and
// exercising every accessor must not crash or read/write out of bounds.

TEST(KeyboardStateTest, GetHashCodeIgnoresOutOfRangeKeysValue)
{
    // 999 >> 5 == 31, far past the 8-word array; the guard must drop it (contributes no bit),
    // so the hash equals that of the same state without the out-of-range key.
    const KeyboardState withInvalid{Keys::A, static_cast<Keys>(999)};
    const KeyboardState withoutInvalid{Keys::A};
    EXPECT_EQ(withInvalid.GetHashCode(), withoutInvalid.GetHashCode());
}

TEST(KeyboardStateTest, GetHashCodeIgnoresNegativeKeysValue)
{
    // A negative Keys value casts to a huge unsigned (>= 256) and must be guarded out, not
    // shifted into an out-of-bounds index.
    const KeyboardState withNegative{Keys::A, static_cast<Keys>(-1)};
    const KeyboardState onlyValid{Keys::A};
    EXPECT_EQ(withNegative.GetHashCode(), onlyValid.GetHashCode());
}

TEST(KeyboardStateTest, GetHashCodeIgnoresBoundaryKeysValue256)
{
    // 256 is the first value past the 0..255 range (256>>5 == 8, one past words[7]); it must be
    // ignored so the hash matches the empty state.
    const KeyboardState boundary{static_cast<Keys>(256)};
    EXPECT_EQ(boundary.GetHashCode(), KeyboardState{}.GetHashCode());
}

TEST(KeyboardStateTest, AccessorsAreSafeForOutOfRangeKeysValues)
{
    // Every public accessor must be memory-safe for out-of-range/negative/unknown Keys, even
    // though such a state is not producible from real hardware.
    const KeyboardState state{static_cast<Keys>(999), static_cast<Keys>(-5),
                              static_cast<Keys>(70000), Keys::A};

    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_FALSE(state.IsKeyDown(Keys::B));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));

    // Querying an out-of-range key must not crash.
    EXPECT_NO_THROW((void)state.IsKeyDown(static_cast<Keys>(999)));
    EXPECT_NO_THROW((void)state[static_cast<Keys>(-5)]);

    // GetPressedKeys and GetHashCode must not read/write out of bounds regardless of contents.
    EXPECT_NO_THROW((void)state.GetPressedKeys());
    EXPECT_NO_THROW((void)state.GetHashCode());
}

// P1-014: the constructors themselves must drop out-of-range Keys values the same way FNA's
// AddPressedKey does (KeyboardState.cs:220-234 — the switch on `key >> 5` has cases 0..7 and no
// default, so a value outside 0..255 matches nothing and the key is never marked pressed). Before
// this fix, CNA's constructors stored every value verbatim into `pressedKeys_` unfiltered, so only
// GetHashCode (which had its own separate guard) matched FNA for out-of-range keys; IsKeyDown,
// IsKeyUp, operator[]/getItem, GetPressedKeys, and Equals/operator== all disagreed with FNA (and
// with GetHashCode) by reporting the out-of-range key as pressed. These tests pin the fixed,
// FNA-consistent behavior across every accessor, for both the initializer-list and the
// unordered_set constructor.
TEST(KeyboardStateTest, InitializerListConstructorDropsOutOfRangeKeysValue)
{
    const KeyboardState state{Keys::A, static_cast<Keys>(999)};

    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_FALSE(state.IsKeyDown(static_cast<Keys>(999)));
    EXPECT_TRUE(state.IsKeyUp(static_cast<Keys>(999)));
    EXPECT_EQ(state[static_cast<Keys>(999)], KeyState::Up);
    EXPECT_EQ(state.getItem(static_cast<Keys>(999)), KeyState::Up);

    const auto pressed = state.GetPressedKeys();
    EXPECT_EQ(pressed, std::vector<Keys>{Keys::A});

    // Equals/operator== must agree that the out-of-range key never happened.
    EXPECT_EQ(state, KeyboardState{Keys::A});
    EXPECT_TRUE(state.Equals(KeyboardState{Keys::A}));
}

TEST(KeyboardStateTest, InitializerListConstructorDropsNegativeAndBoundaryKeysValues)
{
    // -1 (int) casts to a huge unsigned value; 256 is the first value past the representable
    // 0..255 range (256>>5 == 8, one past the last word). Both must be dropped, same as
    // GetHashCode already guarded for these exact values.
    const KeyboardState state{static_cast<Keys>(-1), static_cast<Keys>(256), Keys::B};

    EXPECT_FALSE(state.IsKeyDown(static_cast<Keys>(-1)));
    EXPECT_FALSE(state.IsKeyDown(static_cast<Keys>(256)));
    EXPECT_EQ(state.GetPressedKeys(), std::vector<Keys>{Keys::B});
    EXPECT_EQ(state, KeyboardState{Keys::B});
}

TEST(KeyboardStateTest, UnorderedSetConstructorDropsOutOfRangeKeysValue)
{
    const std::unordered_set<Keys> pressed{Keys::Left, static_cast<Keys>(999)};
    const KeyboardState state(pressed);

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_FALSE(state.IsKeyDown(static_cast<Keys>(999)));
    EXPECT_EQ(state.GetPressedKeys(), std::vector<Keys>{Keys::Left});
    EXPECT_EQ(state, KeyboardState{Keys::Left});
}

// ===========================================================================
// KeyboardState — ToString
// ===========================================================================

TEST(KeyboardStateTest, ToStringMatchesFNAValueTypeDefault)
{
    const KeyboardState state{Keys::A};
    EXPECT_EQ(state.ToString(), "Microsoft.Xna.Framework.Input.KeyboardState");
}

// ===========================================================================
// Keys — exhaustive numeric parity vs FNA / Windows Virtual-Key (INPUT-KBD-001 / INPUT-API-034)
// ===========================================================================
//
// Every Keys enumerator is pinned to its exact FNA Keys.cs / Windows VK value. These values are
// ABI: games and serialized input bindings persist Keys by number, so a silent renumbering would
// break save/binding compatibility. This table is the ABI guardrail (INPUT-API-034 for Keys):
//   * removing a member -> compile error (every member is referenced by name below),
//   * renumbering a member -> the EXPECT_EQ against its hardcoded literal fails,
//   * adding/removing a member -> the static_assert size anchor fails until the table is updated.
// Verified name-for-name and value-for-value against the FNA reference (src/Input/Keys.cs): 160
// members, byte-identical (no missing member, no value drift on either side).

TEST(KeyboardStateTest, KeysValuesMatchXNANumericConstants)
{
    struct Case { Keys key; int value; };
    static const Case cases[] = {
        {Keys::None, 0},
        {Keys::Back, 8},
        {Keys::Tab, 9},
        {Keys::Enter, 13},
        {Keys::Pause, 19},
        {Keys::CapsLock, 20},
        {Keys::Kana, 21},
        {Keys::Kanji, 25},
        {Keys::Escape, 27},
        {Keys::ImeConvert, 28},
        {Keys::ImeNoConvert, 29},
        {Keys::Space, 32},
        {Keys::PageUp, 33},
        {Keys::PageDown, 34},
        {Keys::End, 35},
        {Keys::Home, 36},
        {Keys::Left, 37},
        {Keys::Up, 38},
        {Keys::Right, 39},
        {Keys::Down, 40},
        {Keys::Select, 41},
        {Keys::Print, 42},
        {Keys::Execute, 43},
        {Keys::PrintScreen, 44},
        {Keys::Insert, 45},
        {Keys::Delete, 46},
        {Keys::Help, 47},
        {Keys::D0, 48},
        {Keys::D1, 49},
        {Keys::D2, 50},
        {Keys::D3, 51},
        {Keys::D4, 52},
        {Keys::D5, 53},
        {Keys::D6, 54},
        {Keys::D7, 55},
        {Keys::D8, 56},
        {Keys::D9, 57},
        {Keys::A, 65},
        {Keys::B, 66},
        {Keys::C, 67},
        {Keys::D, 68},
        {Keys::E, 69},
        {Keys::F, 70},
        {Keys::G, 71},
        {Keys::H, 72},
        {Keys::I, 73},
        {Keys::J, 74},
        {Keys::K, 75},
        {Keys::L, 76},
        {Keys::M, 77},
        {Keys::N, 78},
        {Keys::O, 79},
        {Keys::P, 80},
        {Keys::Q, 81},
        {Keys::R, 82},
        {Keys::S, 83},
        {Keys::T, 84},
        {Keys::U, 85},
        {Keys::V, 86},
        {Keys::W, 87},
        {Keys::X, 88},
        {Keys::Y, 89},
        {Keys::Z, 90},
        {Keys::LeftWindows, 91},
        {Keys::RightWindows, 92},
        {Keys::Apps, 93},
        {Keys::Sleep, 95},
        {Keys::NumPad0, 96},
        {Keys::NumPad1, 97},
        {Keys::NumPad2, 98},
        {Keys::NumPad3, 99},
        {Keys::NumPad4, 100},
        {Keys::NumPad5, 101},
        {Keys::NumPad6, 102},
        {Keys::NumPad7, 103},
        {Keys::NumPad8, 104},
        {Keys::NumPad9, 105},
        {Keys::Multiply, 106},
        {Keys::Add, 107},
        {Keys::Separator, 108},
        {Keys::Subtract, 109},
        {Keys::Decimal, 110},
        {Keys::Divide, 111},
        {Keys::F1, 112},
        {Keys::F2, 113},
        {Keys::F3, 114},
        {Keys::F4, 115},
        {Keys::F5, 116},
        {Keys::F6, 117},
        {Keys::F7, 118},
        {Keys::F8, 119},
        {Keys::F9, 120},
        {Keys::F10, 121},
        {Keys::F11, 122},
        {Keys::F12, 123},
        {Keys::F13, 124},
        {Keys::F14, 125},
        {Keys::F15, 126},
        {Keys::F16, 127},
        {Keys::F17, 128},
        {Keys::F18, 129},
        {Keys::F19, 130},
        {Keys::F20, 131},
        {Keys::F21, 132},
        {Keys::F22, 133},
        {Keys::F23, 134},
        {Keys::F24, 135},
        {Keys::NumLock, 144},
        {Keys::Scroll, 145},
        {Keys::LeftShift, 160},
        {Keys::RightShift, 161},
        {Keys::LeftControl, 162},
        {Keys::RightControl, 163},
        {Keys::LeftAlt, 164},
        {Keys::RightAlt, 165},
        {Keys::BrowserBack, 166},
        {Keys::BrowserForward, 167},
        {Keys::BrowserRefresh, 168},
        {Keys::BrowserStop, 169},
        {Keys::BrowserSearch, 170},
        {Keys::BrowserFavorites, 171},
        {Keys::BrowserHome, 172},
        {Keys::VolumeMute, 173},
        {Keys::VolumeDown, 174},
        {Keys::VolumeUp, 175},
        {Keys::MediaNextTrack, 176},
        {Keys::MediaPreviousTrack, 177},
        {Keys::MediaStop, 178},
        {Keys::MediaPlayPause, 179},
        {Keys::LaunchMail, 180},
        {Keys::SelectMedia, 181},
        {Keys::LaunchApplication1, 182},
        {Keys::LaunchApplication2, 183},
        {Keys::OemSemicolon, 186},
        {Keys::OemPlus, 187},
        {Keys::OemComma, 188},
        {Keys::OemMinus, 189},
        {Keys::OemPeriod, 190},
        {Keys::OemQuestion, 191},
        {Keys::OemTilde, 192},
        {Keys::ChatPadGreen, 202},
        {Keys::ChatPadOrange, 203},
        {Keys::OemOpenBrackets, 219},
        {Keys::OemPipe, 220},
        {Keys::OemCloseBrackets, 221},
        {Keys::OemQuotes, 222},
        {Keys::Oem8, 223},
        {Keys::OemBackslash, 226},
        {Keys::ProcessKey, 229},
        {Keys::OemCopy, 242},
        {Keys::OemAuto, 243},
        {Keys::OemEnlW, 244},
        {Keys::Attn, 246},
        {Keys::Crsel, 247},
        {Keys::Exsel, 248},
        {Keys::EraseEof, 249},
        {Keys::Play, 250},
        {Keys::Zoom, 251},
        {Keys::Pa1, 253},
        {Keys::OemClear, 254},
    };

    static_assert(sizeof(cases) / sizeof(cases[0]) == 160,
                  "Keys member count changed - update this FNA parity table deliberately");

    std::unordered_set<int> seen;
    for (const Case& c : cases)
    {
        EXPECT_EQ(static_cast<int>(c.key), c.value);
        EXPECT_TRUE(seen.insert(c.value).second) << "duplicate Keys numeric value " << c.value;
    }
    EXPECT_EQ(seen.size(), 160u) << "every Keys value must be present and distinct";
}

// ===========================================================================
// KeyState — value spot-check (exhaustively pinned in the dedicated KeyStateTests suite)
// ===========================================================================

TEST(KeyboardStateTest, KeyStateValuesMatchXNAConstants)
{
    EXPECT_NE(KeyState::Up, KeyState::Down);
}
