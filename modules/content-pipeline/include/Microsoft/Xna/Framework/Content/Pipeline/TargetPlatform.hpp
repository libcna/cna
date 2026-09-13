// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Identifies the platform a piece of content is being built for.
     *
     * The numbering is the one the XNA Game Studio 4.0 assembly declares
     * (tests/reference/xna40/content-pipeline-api.json).
     */
    enum class TargetPlatform
    {
        /** @brief Windows (x86 desktop). */
        Windows = 0,

        /** @brief Xbox 360. */
        Xbox360 = 1,

        /** @brief Windows Phone 7. */
        WindowsPhone = 2,
    };

    /**
     * @brief Returns the XNA spelling of a target platform (`Windows`, `Xbox360`, `WindowsPhone`).
     *
     * @param platform The platform to name.
     * @return A process-lifetime string literal.
     */
    CNAEXT [[nodiscard]] const char* TargetPlatformName(TargetPlatform platform) noexcept;

    /**
     * @brief Parses the XNA spellings of a target platform, including the two MSBuild property
     *        spellings `Xbox 360` and `Windows Phone` the content targets normalize.
     *
     * @param name Spelling to parse, matched case-insensitively.
     * @return The platform, or no value when the spelling names none.
     */
    CNAEXT [[nodiscard]] std::optional<TargetPlatform> TryParseTargetPlatform(const std::string& name);
}
