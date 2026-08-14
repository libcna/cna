// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_H
#define CNA_C_INPUT_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width XNA keyboard-key identity in the inclusive bitfield range 0 through 255. */
typedef uint32_t CNA_Key;

/** @brief Identifies the None key. */
#define CNA_KEY_NONE UINT32_C(0)
/** @brief Identifies the Back key. */
#define CNA_KEY_BACK UINT32_C(8)
/** @brief Identifies the Tab key. */
#define CNA_KEY_TAB UINT32_C(9)
/** @brief Identifies the Enter key. */
#define CNA_KEY_ENTER UINT32_C(13)
/** @brief Identifies the Pause key. */
#define CNA_KEY_PAUSE UINT32_C(19)
/** @brief Identifies the CapsLock key. */
#define CNA_KEY_CAPS_LOCK UINT32_C(20)
/** @brief Identifies the Kana key. */
#define CNA_KEY_KANA UINT32_C(21)
/** @brief Identifies the Kanji key. */
#define CNA_KEY_KANJI UINT32_C(25)
/** @brief Identifies the Escape key. */
#define CNA_KEY_ESCAPE UINT32_C(27)
/** @brief Identifies the ImeConvert key. */
#define CNA_KEY_IME_CONVERT UINT32_C(28)
/** @brief Identifies the ImeNoConvert key. */
#define CNA_KEY_IME_NO_CONVERT UINT32_C(29)
/** @brief Identifies the Space key. */
#define CNA_KEY_SPACE UINT32_C(32)
/** @brief Identifies the PageUp key. */
#define CNA_KEY_PAGE_UP UINT32_C(33)
/** @brief Identifies the PageDown key. */
#define CNA_KEY_PAGE_DOWN UINT32_C(34)
/** @brief Identifies the End key. */
#define CNA_KEY_END UINT32_C(35)
/** @brief Identifies the Home key. */
#define CNA_KEY_HOME UINT32_C(36)
/** @brief Identifies the Left key. */
#define CNA_KEY_LEFT UINT32_C(37)
/** @brief Identifies the Up key. */
#define CNA_KEY_UP UINT32_C(38)
/** @brief Identifies the Right key. */
#define CNA_KEY_RIGHT UINT32_C(39)
/** @brief Identifies the Down key. */
#define CNA_KEY_DOWN UINT32_C(40)
/** @brief Identifies the Select key. */
#define CNA_KEY_SELECT UINT32_C(41)
/** @brief Identifies the Print key. */
#define CNA_KEY_PRINT UINT32_C(42)
/** @brief Identifies the Execute key. */
#define CNA_KEY_EXECUTE UINT32_C(43)
/** @brief Identifies the PrintScreen key. */
#define CNA_KEY_PRINT_SCREEN UINT32_C(44)
/** @brief Identifies the Insert key. */
#define CNA_KEY_INSERT UINT32_C(45)
/** @brief Identifies the Delete key. */
#define CNA_KEY_DELETE UINT32_C(46)
/** @brief Identifies the Help key. */
#define CNA_KEY_HELP UINT32_C(47)
/** @brief Identifies the D0 key. */
#define CNA_KEY_D0 UINT32_C(48)
/** @brief Identifies the D1 key. */
#define CNA_KEY_D1 UINT32_C(49)
/** @brief Identifies the D2 key. */
#define CNA_KEY_D2 UINT32_C(50)
/** @brief Identifies the D3 key. */
#define CNA_KEY_D3 UINT32_C(51)
/** @brief Identifies the D4 key. */
#define CNA_KEY_D4 UINT32_C(52)
/** @brief Identifies the D5 key. */
#define CNA_KEY_D5 UINT32_C(53)
/** @brief Identifies the D6 key. */
#define CNA_KEY_D6 UINT32_C(54)
/** @brief Identifies the D7 key. */
#define CNA_KEY_D7 UINT32_C(55)
/** @brief Identifies the D8 key. */
#define CNA_KEY_D8 UINT32_C(56)
/** @brief Identifies the D9 key. */
#define CNA_KEY_D9 UINT32_C(57)
/** @brief Identifies the A key. */
#define CNA_KEY_A UINT32_C(65)
/** @brief Identifies the B key. */
#define CNA_KEY_B UINT32_C(66)
/** @brief Identifies the C key. */
#define CNA_KEY_C UINT32_C(67)
/** @brief Identifies the D key. */
#define CNA_KEY_D UINT32_C(68)
/** @brief Identifies the E key. */
#define CNA_KEY_E UINT32_C(69)
/** @brief Identifies the F key. */
#define CNA_KEY_F UINT32_C(70)
/** @brief Identifies the G key. */
#define CNA_KEY_G UINT32_C(71)
/** @brief Identifies the H key. */
#define CNA_KEY_H UINT32_C(72)
/** @brief Identifies the I key. */
#define CNA_KEY_I UINT32_C(73)
/** @brief Identifies the J key. */
#define CNA_KEY_J UINT32_C(74)
/** @brief Identifies the K key. */
#define CNA_KEY_K UINT32_C(75)
/** @brief Identifies the L key. */
#define CNA_KEY_L UINT32_C(76)
/** @brief Identifies the M key. */
#define CNA_KEY_M UINT32_C(77)
/** @brief Identifies the N key. */
#define CNA_KEY_N UINT32_C(78)
/** @brief Identifies the O key. */
#define CNA_KEY_O UINT32_C(79)
/** @brief Identifies the P key. */
#define CNA_KEY_P UINT32_C(80)
/** @brief Identifies the Q key. */
#define CNA_KEY_Q UINT32_C(81)
/** @brief Identifies the R key. */
#define CNA_KEY_R UINT32_C(82)
/** @brief Identifies the S key. */
#define CNA_KEY_S UINT32_C(83)
/** @brief Identifies the T key. */
#define CNA_KEY_T UINT32_C(84)
/** @brief Identifies the U key. */
#define CNA_KEY_U UINT32_C(85)
/** @brief Identifies the V key. */
#define CNA_KEY_V UINT32_C(86)
/** @brief Identifies the W key. */
#define CNA_KEY_W UINT32_C(87)
/** @brief Identifies the X key. */
#define CNA_KEY_X UINT32_C(88)
/** @brief Identifies the Y key. */
#define CNA_KEY_Y UINT32_C(89)
/** @brief Identifies the Z key. */
#define CNA_KEY_Z UINT32_C(90)
/** @brief Identifies the LeftWindows key. */
#define CNA_KEY_LEFT_WINDOWS UINT32_C(91)
/** @brief Identifies the RightWindows key. */
#define CNA_KEY_RIGHT_WINDOWS UINT32_C(92)
/** @brief Identifies the Apps key. */
#define CNA_KEY_APPS UINT32_C(93)
/** @brief Identifies the Sleep key. */
#define CNA_KEY_SLEEP UINT32_C(95)
/** @brief Identifies the NumPad0 key. */
#define CNA_KEY_NUM_PAD0 UINT32_C(96)
/** @brief Identifies the NumPad1 key. */
#define CNA_KEY_NUM_PAD1 UINT32_C(97)
/** @brief Identifies the NumPad2 key. */
#define CNA_KEY_NUM_PAD2 UINT32_C(98)
/** @brief Identifies the NumPad3 key. */
#define CNA_KEY_NUM_PAD3 UINT32_C(99)
/** @brief Identifies the NumPad4 key. */
#define CNA_KEY_NUM_PAD4 UINT32_C(100)
/** @brief Identifies the NumPad5 key. */
#define CNA_KEY_NUM_PAD5 UINT32_C(101)
/** @brief Identifies the NumPad6 key. */
#define CNA_KEY_NUM_PAD6 UINT32_C(102)
/** @brief Identifies the NumPad7 key. */
#define CNA_KEY_NUM_PAD7 UINT32_C(103)
/** @brief Identifies the NumPad8 key. */
#define CNA_KEY_NUM_PAD8 UINT32_C(104)
/** @brief Identifies the NumPad9 key. */
#define CNA_KEY_NUM_PAD9 UINT32_C(105)
/** @brief Identifies the Multiply key. */
#define CNA_KEY_MULTIPLY UINT32_C(106)
/** @brief Identifies the Add key. */
#define CNA_KEY_ADD UINT32_C(107)
/** @brief Identifies the Separator key. */
#define CNA_KEY_SEPARATOR UINT32_C(108)
/** @brief Identifies the Subtract key. */
#define CNA_KEY_SUBTRACT UINT32_C(109)
/** @brief Identifies the Decimal key. */
#define CNA_KEY_DECIMAL UINT32_C(110)
/** @brief Identifies the Divide key. */
#define CNA_KEY_DIVIDE UINT32_C(111)
/** @brief Identifies the F1 key. */
#define CNA_KEY_F1 UINT32_C(112)
/** @brief Identifies the F2 key. */
#define CNA_KEY_F2 UINT32_C(113)
/** @brief Identifies the F3 key. */
#define CNA_KEY_F3 UINT32_C(114)
/** @brief Identifies the F4 key. */
#define CNA_KEY_F4 UINT32_C(115)
/** @brief Identifies the F5 key. */
#define CNA_KEY_F5 UINT32_C(116)
/** @brief Identifies the F6 key. */
#define CNA_KEY_F6 UINT32_C(117)
/** @brief Identifies the F7 key. */
#define CNA_KEY_F7 UINT32_C(118)
/** @brief Identifies the F8 key. */
#define CNA_KEY_F8 UINT32_C(119)
/** @brief Identifies the F9 key. */
#define CNA_KEY_F9 UINT32_C(120)
/** @brief Identifies the F10 key. */
#define CNA_KEY_F10 UINT32_C(121)
/** @brief Identifies the F11 key. */
#define CNA_KEY_F11 UINT32_C(122)
/** @brief Identifies the F12 key. */
#define CNA_KEY_F12 UINT32_C(123)
/** @brief Identifies the F13 key. */
#define CNA_KEY_F13 UINT32_C(124)
/** @brief Identifies the F14 key. */
#define CNA_KEY_F14 UINT32_C(125)
/** @brief Identifies the F15 key. */
#define CNA_KEY_F15 UINT32_C(126)
/** @brief Identifies the F16 key. */
#define CNA_KEY_F16 UINT32_C(127)
/** @brief Identifies the F17 key. */
#define CNA_KEY_F17 UINT32_C(128)
/** @brief Identifies the F18 key. */
#define CNA_KEY_F18 UINT32_C(129)
/** @brief Identifies the F19 key. */
#define CNA_KEY_F19 UINT32_C(130)
/** @brief Identifies the F20 key. */
#define CNA_KEY_F20 UINT32_C(131)
/** @brief Identifies the F21 key. */
#define CNA_KEY_F21 UINT32_C(132)
/** @brief Identifies the F22 key. */
#define CNA_KEY_F22 UINT32_C(133)
/** @brief Identifies the F23 key. */
#define CNA_KEY_F23 UINT32_C(134)
/** @brief Identifies the F24 key. */
#define CNA_KEY_F24 UINT32_C(135)
/** @brief Identifies the NumLock key. */
#define CNA_KEY_NUM_LOCK UINT32_C(144)
/** @brief Identifies the Scroll key. */
#define CNA_KEY_SCROLL UINT32_C(145)
/** @brief Identifies the LeftShift key. */
#define CNA_KEY_LEFT_SHIFT UINT32_C(160)
/** @brief Identifies the RightShift key. */
#define CNA_KEY_RIGHT_SHIFT UINT32_C(161)
/** @brief Identifies the LeftControl key. */
#define CNA_KEY_LEFT_CONTROL UINT32_C(162)
/** @brief Identifies the RightControl key. */
#define CNA_KEY_RIGHT_CONTROL UINT32_C(163)
/** @brief Identifies the LeftAlt key. */
#define CNA_KEY_LEFT_ALT UINT32_C(164)
/** @brief Identifies the RightAlt key. */
#define CNA_KEY_RIGHT_ALT UINT32_C(165)
/** @brief Identifies the BrowserBack key. */
#define CNA_KEY_BROWSER_BACK UINT32_C(166)
/** @brief Identifies the BrowserForward key. */
#define CNA_KEY_BROWSER_FORWARD UINT32_C(167)
/** @brief Identifies the BrowserRefresh key. */
#define CNA_KEY_BROWSER_REFRESH UINT32_C(168)
/** @brief Identifies the BrowserStop key. */
#define CNA_KEY_BROWSER_STOP UINT32_C(169)
/** @brief Identifies the BrowserSearch key. */
#define CNA_KEY_BROWSER_SEARCH UINT32_C(170)
/** @brief Identifies the BrowserFavorites key. */
#define CNA_KEY_BROWSER_FAVORITES UINT32_C(171)
/** @brief Identifies the BrowserHome key. */
#define CNA_KEY_BROWSER_HOME UINT32_C(172)
/** @brief Identifies the VolumeMute key. */
#define CNA_KEY_VOLUME_MUTE UINT32_C(173)
/** @brief Identifies the VolumeDown key. */
#define CNA_KEY_VOLUME_DOWN UINT32_C(174)
/** @brief Identifies the VolumeUp key. */
#define CNA_KEY_VOLUME_UP UINT32_C(175)
/** @brief Identifies the MediaNextTrack key. */
#define CNA_KEY_MEDIA_NEXT_TRACK UINT32_C(176)
/** @brief Identifies the MediaPreviousTrack key. */
#define CNA_KEY_MEDIA_PREVIOUS_TRACK UINT32_C(177)
/** @brief Identifies the MediaStop key. */
#define CNA_KEY_MEDIA_STOP UINT32_C(178)
/** @brief Identifies the MediaPlayPause key. */
#define CNA_KEY_MEDIA_PLAY_PAUSE UINT32_C(179)
/** @brief Identifies the LaunchMail key. */
#define CNA_KEY_LAUNCH_MAIL UINT32_C(180)
/** @brief Identifies the SelectMedia key. */
#define CNA_KEY_SELECT_MEDIA UINT32_C(181)
/** @brief Identifies the LaunchApplication1 key. */
#define CNA_KEY_LAUNCH_APPLICATION1 UINT32_C(182)
/** @brief Identifies the LaunchApplication2 key. */
#define CNA_KEY_LAUNCH_APPLICATION2 UINT32_C(183)
/** @brief Identifies the OemSemicolon key. */
#define CNA_KEY_OEM_SEMICOLON UINT32_C(186)
/** @brief Identifies the OemPlus key. */
#define CNA_KEY_OEM_PLUS UINT32_C(187)
/** @brief Identifies the OemComma key. */
#define CNA_KEY_OEM_COMMA UINT32_C(188)
/** @brief Identifies the OemMinus key. */
#define CNA_KEY_OEM_MINUS UINT32_C(189)
/** @brief Identifies the OemPeriod key. */
#define CNA_KEY_OEM_PERIOD UINT32_C(190)
/** @brief Identifies the OemQuestion key. */
#define CNA_KEY_OEM_QUESTION UINT32_C(191)
/** @brief Identifies the OemTilde key. */
#define CNA_KEY_OEM_TILDE UINT32_C(192)
/** @brief Identifies the ChatPadGreen key. */
#define CNA_KEY_CHAT_PAD_GREEN UINT32_C(202)
/** @brief Identifies the ChatPadOrange key. */
#define CNA_KEY_CHAT_PAD_ORANGE UINT32_C(203)
/** @brief Identifies the OemOpenBrackets key. */
#define CNA_KEY_OEM_OPEN_BRACKETS UINT32_C(219)
/** @brief Identifies the OemPipe key. */
#define CNA_KEY_OEM_PIPE UINT32_C(220)
/** @brief Identifies the OemCloseBrackets key. */
#define CNA_KEY_OEM_CLOSE_BRACKETS UINT32_C(221)
/** @brief Identifies the OemQuotes key. */
#define CNA_KEY_OEM_QUOTES UINT32_C(222)
/** @brief Identifies the Oem8 key. */
#define CNA_KEY_OEM8 UINT32_C(223)
/** @brief Identifies the OemBackslash key. */
#define CNA_KEY_OEM_BACKSLASH UINT32_C(226)
/** @brief Identifies the ProcessKey key. */
#define CNA_KEY_PROCESS_KEY UINT32_C(229)
/** @brief Identifies the OemCopy key. */
#define CNA_KEY_OEM_COPY UINT32_C(242)
/** @brief Identifies the OemAuto key. */
#define CNA_KEY_OEM_AUTO UINT32_C(243)
/** @brief Identifies the OemEnlW key. */
#define CNA_KEY_OEM_ENL_W UINT32_C(244)
/** @brief Identifies the Attn key. */
#define CNA_KEY_ATTN UINT32_C(246)
/** @brief Identifies the Crsel key. */
#define CNA_KEY_CRSEL UINT32_C(247)
/** @brief Identifies the Exsel key. */
#define CNA_KEY_EXSEL UINT32_C(248)
/** @brief Identifies the EraseEof key. */
#define CNA_KEY_ERASE_EOF UINT32_C(249)
/** @brief Identifies the Play key. */
#define CNA_KEY_PLAY UINT32_C(250)
/** @brief Identifies the Zoom key. */
#define CNA_KEY_ZOOM UINT32_C(251)
/** @brief Identifies the Pa1 key. */
#define CNA_KEY_PA1 UINT32_C(253)
/** @brief Identifies the OemClear key. */
#define CNA_KEY_OEM_CLEAR UINT32_C(254)

/**
 * @brief Immutable 256-key snapshot captured from CNA's current keyboard state.
 */
typedef struct CNA_KeyboardState {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Four words; key N is bit N modulo 64 of word N divided by 64. */
    uint64_t pressed_key_words[4];
} CNA_KeyboardState;

/**
 * @brief Captures a fresh keyboard state for the active game at this call site.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_state Caller-provided versioned structure to receive the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The call delegates to CNA's current `Keyboard::GetState`; it is not cached once per update or
 * draw frame, so two calls in one callback are two independently timed snapshots. It must run on
 * the game's creation/graphics thread. The returned POD has no handle lifetime and may be copied.
 */
CNA_C_API CNA_Result cna_keyboard_get_state(
    CNA_Handle game,
    CNA_KeyboardState* out_state);

/**
 * @brief Tests whether one key slot is down in an immutable keyboard snapshot.
 *
 * @param state Snapshot to query.
 * @param key Numeric key slot in the inclusive range 0 through 255.
 * @param out_is_down Receives `CNA_TRUE` when the key is down, otherwise `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 *
 * This pure POD query touches no CNA runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_keyboard_state_is_key_down(
    const CNA_KeyboardState* state,
    CNA_Key key,
    CNA_Bool* out_is_down);

/**
 * @brief Tests whether one key slot is up in an immutable keyboard snapshot.
 *
 * @param state Snapshot to query.
 * @param key Numeric key slot in the inclusive range 0 through 255.
 * @param out_is_up Receives `CNA_TRUE` when the key is up, otherwise `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 *
 * This pure POD query touches no CNA runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_keyboard_state_is_key_up(
    const CNA_KeyboardState* state,
    CNA_Key key,
    CNA_Bool* out_is_up);

/**
 * @brief Counts pressed keys in an immutable keyboard snapshot.
 *
 * @param state Snapshot to query.
 * @param out_count Receives a value in the inclusive range 0 through 256.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 *
 * This pure POD query touches no CNA runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_keyboard_state_get_pressed_key_count(
    const CNA_KeyboardState* state,
    uint32_t* out_count);

/**
 * @brief Copies pressed key slots in ascending numeric order.
 *
 * @param state Snapshot to query.
 * @param destination Caller-owned key array, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination measured in keys.
 * @param out_count Receives the exact number of pressed keys.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or
 * `CNA_RESULT_INVALID_ARGUMENT`. No partial key array is written.
 *
 * This pure POD query touches no CNA runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_keyboard_state_copy_pressed_keys(
    const CNA_KeyboardState* state,
    CNA_Key* destination,
    uint64_t capacity,
    uint32_t* out_count);

#ifdef __cplusplus
}
#endif

#endif
