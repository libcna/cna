// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Math
{
    // XNA's .NET Framework Single formatting prints either zero sign as "0".
    // Canonicalize only the stream operand, never the stored mathematical value.
    [[nodiscard]] inline float DisplayFloat(float value) noexcept
    {
        return value == 0.0f ? 0.0f : value;
    }
}
