// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbFileOptions.hpp"

#include <array>
#include <string_view>
#include <utility>

#include "CNA/Content/Xnb/XnbWriteLimits.hpp"

namespace CNA::Content::Xnb
{
    namespace
    {
        constexpr std::array<std::pair<std::string_view, XnbTargetPlatform>, 7> kPlatformNames{{
            {"windows", XnbTargetPlatform::Windows},
            {"windowsphone", XnbTargetPlatform::WindowsPhone},
            {"desktopgl", XnbTargetPlatform::DesktopGL},
            {"macosx", XnbTargetPlatform::MacOSX},
            {"linux", XnbTargetPlatform::Linux},
            {"ios", XnbTargetPlatform::iOS},
            {"android", XnbTargetPlatform::Android},
        }};
    }

    char XnbPlatformByte(const XnbTargetPlatform platform) noexcept
    {
        return static_cast<char>(platform);
    }

    const char* XnbTargetPlatformName(const XnbTargetPlatform platform) noexcept
    {
        switch (platform)
        {
            case XnbTargetPlatform::Windows: return "windows";
            case XnbTargetPlatform::WindowsPhone: return "windowsphone";
            case XnbTargetPlatform::DesktopGL: return "desktopgl";
            case XnbTargetPlatform::MacOSX: return "macosx";
            case XnbTargetPlatform::Linux: return "linux";
            case XnbTargetPlatform::iOS: return "ios";
            case XnbTargetPlatform::Android: return "android";
        }
        return "windows";
    }

    XnbTargetPlatform ParseXnbTargetPlatform(const std::string& name)
    {
        for (const auto& [spelling, platform] : kPlatformNames)
        {
            if (spelling == name) { return platform; }
        }

        std::string accepted;
        for (const auto& [spelling, platform] : kPlatformNames)
        {
            (void)platform;
            if (!accepted.empty()) { accepted += ", "; }
            accepted.append(spelling);
        }
        throw XnbWriteException(
            "'" + name + "' is not a supported .xnb target platform; expected one of " + accepted +
            ".");
    }

    void ValidateXnbFileOptions(const XnbFileOptions& options)
    {
        if (options.version != 4 && options.version != 5)
        {
            throw XnbWriteException(
                "XNB container version " + std::to_string(options.version) +
                " is not writable; only 4 and 5 are valid XNA 4.0 versions.");
        }

        bool known = false;
        for (const auto& [spelling, platform] : kPlatformNames)
        {
            (void)spelling;
            if (platform == options.platform) { known = true; break; }
        }
        if (!known)
        {
            throw XnbWriteException(
                "XNB target platform byte '" + std::string(1, XnbPlatformByte(options.platform)) +
                "' is not one this writer knows how to encode for.");
        }

        if (options.compression != XnbWriteCompression::None)
        {
            throw XnbWriteException(
                "This build produces uncompressed .xnb files only; compressed output is "
                "plans/plan_xnapipeline.md XNAP-023 and is not implemented.");
        }
    }
}
