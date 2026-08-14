// SPDX-License-Identifier: MS-PL

#include "CNA/C/input.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <bit>
#include <cstdint>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::KeyboardState;
using Microsoft::Xna::Framework::Input::Keys;

constexpr uint32_t StructureVersion = UINT32_C(1);
constexpr CNA_Key KeyboardSlotCount = UINT32_C(256);

[[nodiscard]] CNA_Result ValidateKeyboardState(
    const CNA_KeyboardState* const state) noexcept
{
    if (state == nullptr || state->struct_size < sizeof(CNA_KeyboardState) ||
        state->struct_version != StructureVersion) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The keyboard-state snapshot structure is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool IsKeyDown(
    const CNA_KeyboardState& state,
    const CNA_Key key) noexcept
{
    const uint32_t wordIndex = key >> 6U;
    const uint32_t bitIndex = key & UINT32_C(63);
    return (state.pressed_key_words[wordIndex] & (UINT64_C(1) << bitIndex)) != 0U;
}

[[nodiscard]] uint32_t CountPressedKeys(const CNA_KeyboardState& state) noexcept
{
    uint32_t count = 0U;
    for (const uint64_t word : state.pressed_key_words) {
        count += static_cast<uint32_t>(std::popcount(word));
    }
    return count;
}

} // namespace

CNA_Result cna_keyboard_get_state(
    const CNA_Handle gameHandle,
    CNA_KeyboardState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr || outState->struct_size < sizeof(CNA_KeyboardState) ||
            outState->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard-state output structure is invalid.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        CNA_KeyboardState snapshot = {
            sizeof(CNA_KeyboardState),
            StructureVersion,
            {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)}
        };
        const KeyboardState nativeState = Keyboard::GetState();
        for (const Keys key : nativeState.GetPressedKeys()) {
            const int value = static_cast<int>(key);
            if (value >= 0 && value < static_cast<int>(KeyboardSlotCount)) {
                const auto keyValue = static_cast<uint32_t>(value);
                snapshot.pressed_key_words[keyValue >> 6U] |=
                    UINT64_C(1) << (keyValue & UINT32_C(63));
            }
        }
        *outState = snapshot;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_is_key_down(
    const CNA_KeyboardState* const state,
    const CNA_Key key,
    CNA_Bool* const outIsDown)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsDown == nullptr || key >= KeyboardSlotCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard key-down query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDown = IsKeyDown(*state, key) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_is_key_up(
    const CNA_KeyboardState* const state,
    const CNA_Key key,
    CNA_Bool* const outIsUp)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsUp == nullptr || key >= KeyboardSlotCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard key-up query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsUp = IsKeyDown(*state, key) ? CNA_FALSE : CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_get_pressed_key_count(
    const CNA_KeyboardState* const state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pressed-key count output is null.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = CountPressedKeys(*state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_copy_pressed_keys(
    const CNA_KeyboardState* const state,
    CNA_Key* const destination,
    const uint64_t capacity,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pressed-key output buffer is invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint32_t requiredCount = CountPressedKeys(*state);
        *outCount = requiredCount;
        if (capacity < requiredCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The pressed-key output buffer is too small.");
        }

        uint32_t destinationIndex = 0U;
        for (CNA_Key key = 0U; key < KeyboardSlotCount; ++key) {
            if (IsKeyDown(*state, key)) {
                destination[destinationIndex] = key;
                ++destinationIndex;
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}
