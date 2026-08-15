// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/HapticDirection.hpp"
#include "CNA/Input/HapticEffectType.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — describes one force-feedback effect, for any `HapticEffectTypeEXT` family.
     *
     * This is CNA's platform-neutral haptic-effect record, flattened into one struct rather than
     * six near-identical tagged-union variants: `type`
     * selects the family, and only the fields documented for that family are meaningful — the rest
     * are ignored when the effect is built. Strength/level limits are validated or clamped by
     * the selected platform implementation when the effect is created.
     *
     * Field applicability by `type`:
     * - **Constant**: `direction`, `length`, `delay`, `button`, `interval`, `level`, envelope.
     * - **Sine/Square/Triangle/SawtoothUp/SawtoothDown**: `direction`, `length`, `delay`, `button`,
     *   `interval`, `period`, `magnitude`, `offset`, `phase`, envelope.
     * - **Ramp**: `direction`, `length`, `delay`, `button`, `interval`, `rampStart`, `rampEnd`, envelope.
     * - **Spring/Damper/Inertia/Friction**: `length`, `delay`, `button`, `interval`, the six per-axis
     *   condition arrays. No envelope; `direction` is present but SDL's condition internals handle
     *   direction per-axis instead of using it.
     * - **LeftRight**: `length`, `largeMagnitude`, `smallMagnitude`. Nothing else applies.
     * - **Custom**: `direction`, `length`, `delay`, `button`, `interval`, `customChannels`,
     *   `customPeriod`, `customData` (size must be `customChannels * samples`), envelope.
     */
    CNAEXT struct HapticEffectEXT
    {
        /** @brief A `length` value meaning "play forever". */
        static constexpr std::uint32_t InfiniteLengthEXT = 4294967295u;

        /** @brief Which effect family this describes. */
        HapticEffectTypeEXT type = HapticEffectTypeEXT::Constant;

        /** @brief Direction the force comes from (Constant/periodic/Ramp/Custom). */
        HapticDirectionEXT direction;

        /** @brief Duration of the effect in milliseconds, or `InfiniteLengthEXT`. */
        std::uint32_t length = 0;
        /** @brief Delay before starting the effect, in milliseconds. */
        std::uint16_t delay = 0;

        /** @brief 1-based button index that triggers the effect, or 0 for none. */
        std::uint16_t button = 0;
        /** @brief Minimum time between button-triggered replays, in milliseconds. */
        std::uint16_t interval = 0;

        /** @brief Strength of a Constant effect. */
        std::int16_t level = 0;

        /** @brief Wave period of a periodic effect, in milliseconds. */
        std::uint16_t period = 0;
        /** @brief Peak value of a periodic effect; negative adds 180 degrees of phase shift. */
        std::int16_t magnitude = 0;
        /** @brief Mean (DC offset) value of a periodic effect's wave. */
        std::int16_t offset = 0;
        /** @brief Positive phase shift of a periodic effect, in hundredths of a degree. */
        std::uint16_t phase = 0;

        /** @brief Starting strength of a Ramp effect. */
        std::int16_t rampStart = 0;
        /** @brief Ending strength of a Ramp effect. */
        std::int16_t rampEnd = 0;

        /** @brief Per-axis (X, Y, Z) saturation level on the positive side, for condition effects. */
        std::array<std::uint16_t, 3> rightSaturation{};
        /** @brief Per-axis (X, Y, Z) saturation level on the negative side, for condition effects. */
        std::array<std::uint16_t, 3> leftSaturation{};
        /** @brief Per-axis (X, Y, Z) force growth rate towards the positive side, for condition effects. */
        std::array<std::int16_t, 3> rightCoefficient{};
        /** @brief Per-axis (X, Y, Z) force growth rate towards the negative side, for condition effects. */
        std::array<std::int16_t, 3> leftCoefficient{};
        /** @brief Per-axis (X, Y, Z) dead-zone size, for condition effects. */
        std::array<std::uint16_t, 3> deadband{};
        /** @brief Per-axis (X, Y, Z) dead-zone center position, for condition effects. */
        std::array<std::int16_t, 3> center{};

        /** @brief Low-frequency (large) motor strength, for a LeftRight effect. */
        std::uint16_t largeMagnitude = 0;
        /** @brief High-frequency (small) motor strength, for a LeftRight effect. */
        std::uint16_t smallMagnitude = 0;

        /** @brief Number of axes the Custom waveform drives (minimum 1); 1 rotates using `direction`. */
        std::uint8_t customChannels = 0;
        /** @brief Sample period of the Custom waveform, in milliseconds. */
        std::uint16_t customPeriod = 0;
        /** @brief Raw Custom waveform samples; size must equal `customChannels` times the sample count. */
        std::vector<std::uint16_t> customData;

        /** @brief Duration of the attack ramp-in, in milliseconds (0 with `attackLevel` = no envelope). */
        std::uint16_t attackLength = 0;
        /** @brief Effect level at the start of the attack. */
        std::uint16_t attackLevel = 0;
        /** @brief Duration of the fade ramp-out, in milliseconds. */
        std::uint16_t fadeLength = 0;
        /** @brief Effect level at the end of the fade. */
        std::uint16_t fadeLevel = 0;
    };

    /**
     * @brief Compares two haptic effect descriptors for equality (every field).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const HapticEffectEXT& left, const HapticEffectEXT& right)
    {
        return left.type == right.type
            && left.direction == right.direction
            && left.length == right.length
            && left.delay == right.delay
            && left.button == right.button
            && left.interval == right.interval
            && left.level == right.level
            && left.period == right.period
            && left.magnitude == right.magnitude
            && left.offset == right.offset
            && left.phase == right.phase
            && left.rampStart == right.rampStart
            && left.rampEnd == right.rampEnd
            && left.rightSaturation == right.rightSaturation
            && left.leftSaturation == right.leftSaturation
            && left.rightCoefficient == right.rightCoefficient
            && left.leftCoefficient == right.leftCoefficient
            && left.deadband == right.deadband
            && left.center == right.center
            && left.largeMagnitude == right.largeMagnitude
            && left.smallMagnitude == right.smallMagnitude
            && left.customChannels == right.customChannels
            && left.customPeriod == right.customPeriod
            && left.customData == right.customData
            && left.attackLength == right.attackLength
            && left.attackLevel == right.attackLevel
            && left.fadeLength == right.fadeLength
            && left.fadeLevel == right.fadeLevel;
    }

    /**
     * @brief Compares two haptic effect descriptors for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if any field differs.
     */
    [[nodiscard]] inline bool operator!=(const HapticEffectEXT& left, const HapticEffectEXT& right)
    {
        return !(left == right);
    }
}
