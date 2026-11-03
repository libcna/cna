// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "System/TimeSpan.hpp"

namespace CNA::Internal::Audio
{
    // XNA's internal AudioFormat for 16-bit PCM. SoundEffect, DynamicSoundEffectInstance and
    // Microphone each validate their own arguments as XNA does, then share this arithmetic.

    /// AudioFormat.DurationFromSize: whole frames, milliseconds computed in float, then .NET
    /// Framework's TimeSpan.FromMilliseconds, which rounds to the nearest whole millisecond (away
    /// from zero on a tie). FNA truncates instead (cna-killer KF-7).
    inline System::TimeSpan Pcm16DurationFromSize(const std::int32_t sizeInBytes,
                                                  const std::int32_t sampleRate,
                                                  const std::int32_t channels)
    {
        const std::int32_t frames = sizeInBytes / (2 * channels);
        const float milliseconds =
            static_cast<float>(frames) * 1000.0f / static_cast<float>(sampleRate);
        const double value = static_cast<double>(milliseconds);
        const auto whole = static_cast<std::int64_t>(value + (value >= 0.0 ? 0.5 : -0.5));
        return System::TimeSpan::FromTicks(whole * System::TimeSpan::TicksPerMillisecond);
    }

    /// AudioFormat.SizeFromDuration: (int)(ms * (rate / 1000f)) frames, plus frames % channels,
    /// times the block alignment -- never a partial frame (cna-killer KF-8). Empty where XNA's
    /// checked conversion or arithmetic (conv.ovf.i4, add.ovf, mul.ovf) throws OverflowException,
    /// which every public caller turns into ArgumentOutOfRangeException("duration").
    inline std::optional<std::int32_t> Pcm16SizeFromDuration(const System::TimeSpan& duration,
                                                             const std::int32_t sampleRate,
                                                             const std::int32_t channels)
    {
        constexpr auto lowest = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
        constexpr auto highest = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
        const double frames = duration.getTotalMillisecondsProperty() *
                              static_cast<double>(static_cast<float>(sampleRate) / 1000.0f);
        if (!(frames > -2147483649.0 && frames < 2147483648.0))
            return std::nullopt;
        const auto whole = static_cast<std::int64_t>(frames);
        const std::int64_t aligned = whole + whole % channels;
        if (aligned < lowest || aligned > highest)
            return std::nullopt;
        const std::int64_t bytes = aligned * (2 * static_cast<std::int64_t>(channels));
        if (bytes < lowest || bytes > highest)
            return std::nullopt;
        return static_cast<std::int32_t>(bytes);
    }
}
