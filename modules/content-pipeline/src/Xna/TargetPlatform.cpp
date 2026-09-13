// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"

#include <algorithm>
#include <cctype>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    const char* TargetPlatformName(TargetPlatform platform) noexcept
    {
        switch (platform)
        {
            case TargetPlatform::Windows: return "Windows";
            case TargetPlatform::Xbox360: return "Xbox360";
            case TargetPlatform::WindowsPhone: return "WindowsPhone";
        }
        return "Windows";
    }

    std::optional<TargetPlatform> TryParseTargetPlatform(const std::string& name)
    {
        std::string folded;
        folded.reserve(name.size());
        for (const char c : name)
        {
            if (c == ' ' || c == '\t') { continue; }
            folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (folded == "windows") { return TargetPlatform::Windows; }
        if (folded == "xbox360") { return TargetPlatform::Xbox360; }
        if (folded == "windowsphone") { return TargetPlatform::WindowsPhone; }
        return std::nullopt;
    }
}
