// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_KEYBOARD_H
#define CNA_C_INPUT_KEYBOARD_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of whether a key is up or down. */
typedef uint32_t CNA_KeyState;

/** @brief The key is not pressed. */
#define CNA_KEY_STATE_UP UINT32_C(0)
/** @brief The key is pressed. */
#define CNA_KEY_STATE_DOWN UINT32_C(1)

/**
 * @brief Fixed-width bit set of the modifier keys currently held.
 *
 * These are real flags — unlike `CNA_GamePadButtonFlags`, whose canonical enumeration only looks
 * like one — so they combine and mask with C's own `|`, `&` and `~`.
 */
typedef uint32_t CNA_KeyModifiers;

/** @brief No modifier is held. */
#define CNA_KEY_MODIFIER_NONE UINT32_C(0x00000000)
/** @brief A Shift key is held. */
#define CNA_KEY_MODIFIER_SHIFT UINT32_C(0x00000001)
/** @brief A Control key is held. */
#define CNA_KEY_MODIFIER_CTRL UINT32_C(0x00000002)
/** @brief An Alt key is held. */
#define CNA_KEY_MODIFIER_ALT UINT32_C(0x00000004)
/** @brief A GUI key — Windows, Command or Super — is held. */
#define CNA_KEY_MODIFIER_GUI UINT32_C(0x00000008)
/** @brief Caps Lock is active. */
#define CNA_KEY_MODIFIER_CAPS UINT32_C(0x00000010)
/** @brief Num Lock is active. */
#define CNA_KEY_MODIFIER_NUM UINT32_C(0x00000020)
/** @brief Scroll Lock is active. */
#define CNA_KEY_MODIFIER_SCROLL UINT32_C(0x00000040)
/** @brief The AltGr mode key is held. */
#define CNA_KEY_MODIFIER_MODE UINT32_C(0x00000080)
/** @brief Mask of every currently defined modifier bit. */
#define CNA_KEY_MODIFIER_ALL UINT32_C(0x000000FF)

/**
 * @brief Initializes an empty keyboard snapshot.
 *
 * @param out_state Receives the versioned structure with no key pressed.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This reproduces the canonical default constructor. It touches no runtime state and may run on
 * any thread.
 */
CNA_C_API CNA_Result cna_keyboard_state_init(CNA_KeyboardState* out_state);

/**
 * @brief Initializes a keyboard snapshot from an explicit set of pressed keys.
 *
 * @param keys Array of `CNA_KEY_*` identities, or null only when @p count is zero.
 * @param count Number of identities.
 * @param out_state Receives the versioned structure.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid array, a null
 *         output or a key outside the 256-slot range. The output is unchanged on failure.
 *
 * This maps both canonical set-taking constructors: a duplicate key contributes once, exactly as a
 * set would. **Documented deviation:** the canonical constructors silently drop a key outside the
 * 256-slot bit field; C refuses instead, so a caller can never lose a key without being told, and
 * the refusal matches every other keyboard route in this API.
 */
CNA_C_API CNA_Result cna_keyboard_state_init_from_keys(
    const CNA_Key* keys,
    uint64_t count,
    CNA_KeyboardState* out_state);

/**
 * @brief Reads whether one key is up or down in a snapshot.
 *
 * @param state Snapshot to query.
 * @param key One `CNA_KEY_*` identity.
 * @param out_key_state Receives `CNA_KEY_STATE_DOWN` or `CNA_KEY_STATE_UP`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot, a null
 *         output or a key outside the 256-slot range.
 *
 * This maps the canonical indexer. `cna_keyboard_state_is_key_down` answers the same question as a
 * boolean; both exist because the canonical API does.
 */
CNA_C_API CNA_Result cna_keyboard_state_get_key_state(
    const CNA_KeyboardState* state,
    CNA_Key key,
    CNA_KeyState* out_key_state);

/**
 * @brief Compares two keyboard snapshots for equality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_equals Receives `CNA_TRUE` when exactly the same keys are pressed.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_keyboard_state_equals(
    const CNA_KeyboardState* left,
    const CNA_KeyboardState* right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two keyboard snapshots for inequality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_not_equals Receives `CNA_TRUE` when the snapshots differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_keyboard_state_not_equals(
    const CNA_KeyboardState* left,
    const CNA_KeyboardState* right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a keyboard snapshot.
 *
 * @param state Snapshot to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 *
 * The canonical hash treats the pressed keys as eight 32-bit words and exclusive-ors them
 * together, so two snapshots whose pressed keys differ by exactly 32 slots can hash alike.
 */
CNA_C_API CNA_Result cna_keyboard_state_get_hash_code(
    const CNA_KeyboardState* state,
    int32_t* out_hash);

/**
 * @brief Reports the byte length of a keyboard snapshot's text, without a terminator.
 *
 * @param state Snapshot to describe.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_keyboard_state_get_string_size(
    const CNA_KeyboardState* state,
    uint64_t* out_bytes);

/**
 * @brief Copies a keyboard snapshot's text without a terminator.
 *
 * @param state Snapshot to describe.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or
 *         `CNA_RESULT_INVALID_ARGUMENT`. No partial value is written.
 *
 * The canonical type does not override its string conversion, so the text is the fixed
 * fully-qualified type name and never reflects which keys are pressed.
 */
CNA_C_API CNA_Result cna_keyboard_state_copy_string(
    const CNA_KeyboardState* state,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Captures a keyboard snapshot for one player slot.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_state Caller-provided versioned structure to receive the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * CNA has one keyboard, so every slot reports the same snapshot `cna_keyboard_get_state`
 * produces; the overload exists because the canonical API has it.
 */
CNA_C_API CNA_Result cna_keyboard_get_state_for_player(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_KeyboardState* out_state);

/**
 * @brief Translates a physical scancode into the key it currently produces.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param scancode Physical `CNA_KEY_*` identity.
 * @param out_key Receives the key the active layout maps it to.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_key_from_scancode_ext(
    CNA_Handle game,
    CNA_Key scancode,
    CNA_Key* out_key);

/**
 * @brief Reads which modifier keys are currently held.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_modifiers Receives zero or more `CNA_KEY_MODIFIER_*` bits.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_mod_state_ext(
    CNA_Handle game,
    CNA_KeyModifiers* out_modifiers);

/**
 * @brief Reports the length of a scancode's name, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key Physical `CNA_KEY_*` identity.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_scancode_name_size_ext(
    CNA_Handle game,
    CNA_Key key,
    uint64_t* out_bytes);

/**
 * @brief Copies a scancode's name without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key Physical `CNA_KEY_*` identity.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 *
 * A scancode with no name reports zero bytes, which is an ordinary answer rather than a failure.
 */
CNA_C_API CNA_Result cna_keyboard_copy_scancode_name_ext(
    CNA_Handle game,
    CNA_Key key,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Translates a scancode name back into its identity.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param name UTF-8 scancode name.
 * @param out_key Receives the identity, or the canonical none value when the name is unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_scancode_from_name_ext(
    CNA_Handle game,
    CNA_StringView name,
    CNA_Key* out_key);

/**
 * @brief Reports the length of a key's name, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key One `CNA_KEY_*` identity.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_key_name_size_ext(
    CNA_Handle game,
    CNA_Key key,
    uint64_t* out_bytes);

/**
 * @brief Copies a key's name without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key One `CNA_KEY_*` identity.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 *
 * A key with no name reports zero bytes. The name follows the active keyboard layout, so it is not
 * a stable identifier — use the `CNA_KEY_*` identity for that.
 */
CNA_C_API CNA_Result cna_keyboard_copy_key_name_ext(
    CNA_Handle game,
    CNA_Key key,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Translates a key name back into its identity.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param name UTF-8 key name.
 * @param out_key Receives the identity, or the canonical none value when the name is unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_keyboard_get_key_from_name_ext(
    CNA_Handle game,
    CNA_StringView name,
    CNA_Key* out_key);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_KEYBOARD_H
