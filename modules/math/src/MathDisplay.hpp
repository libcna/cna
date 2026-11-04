// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include "System/Single.hpp"
#include "System/Globalization/CultureInfo.hpp"

namespace CNA::Internal::Math
{
    // XNA's .NET Framework Single formatting prints either zero sign as "0".
    // Canonicalize only the stream operand, never the stored mathematical value.
    [[nodiscard]] inline float DisplayFloat(float value) noexcept
    {
        return value == 0.0f ? 0.0f : value;
    }
    // XNA Refresh Matrix/Vector2/3/4/Quaternion.ToString IL_0000 uses CurrentCulture.
    // Framework 4 Single default is G7; sharp-runtime's modern default is shortest.
    [[nodiscard]] inline std::string FormatXnaSingle(float value)
    {
        auto text = System::Single::ToString(DisplayFloat(value), "G7");
        // The existing explicit-precision formatter emits a lowercase exponent.
        if (const auto exponent = text.find('e'); exponent != std::string::npos)
            text[exponent] = 'E';
        return System::detail::NumberFormatText::LocalizeFormatted(
            text, &System::Globalization::CultureInfo::getCurrentCultureProperty());
    }
}
