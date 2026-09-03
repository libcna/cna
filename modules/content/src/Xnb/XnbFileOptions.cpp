// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"

#include <array>
#include <utility>

#include "CNA/Internal/Xnb/XnbByteWriter.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        struct PlatformRecord
        {
            XnbTargetPlatform platform;
            char byte;
            const char* name;
            bool xna40;
        };

        constexpr std::array<PlatformRecord, 8> kPlatforms{{
            {XnbTargetPlatform::Windows, 'w', "windows", true},
            {XnbTargetPlatform::WindowsPhone, 'm', "windowsphone", true},
            {XnbTargetPlatform::Xbox360, 'x', "xbox360", true},
            {XnbTargetPlatform::DesktopGL, 'd', "desktopgl", false},
            {XnbTargetPlatform::Linux, 'l', "linux", false},
            {XnbTargetPlatform::iOS, 'i', "ios", false},
            {XnbTargetPlatform::Android, 'a', "android", false},
            {XnbTargetPlatform::WindowsGL, 'g', "windowsgl", false},
        }};

        [[nodiscard]] const PlatformRecord& Record(const XnbTargetPlatform platform) noexcept
        {
            for (const PlatformRecord& record : kPlatforms)
            {
                if (record.platform == platform) { return record; }
            }
            return kPlatforms.front();
        }
    }

    char XnbPlatformByte(const XnbTargetPlatform platform) noexcept
    {
        return Record(platform).byte;
    }

    bool IsXna40TargetPlatform(const XnbTargetPlatform platform) noexcept
    {
        return Record(platform).xna40;
    }

    const char* XnbTargetPlatformName(const XnbTargetPlatform platform) noexcept
    {
        return Record(platform).name;
    }

    bool TryParseXnbTargetPlatform(const std::string& name, XnbTargetPlatform& platform)
    {
        for (const PlatformRecord& record : kPlatforms)
        {
            if (name == record.name)
            {
                platform = record.platform;
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> XnbTargetPlatformNames()
    {
        std::vector<std::string> names;
        names.reserve(kPlatforms.size());
        for (const PlatformRecord& record : kPlatforms) { names.emplace_back(record.name); }
        return names;
    }

    std::uint8_t XnbContainerVersionByte(const XnbContainerVersion version) noexcept
    {
        return version == XnbContainerVersion::Legacy4 ? std::uint8_t{4} : std::uint8_t{5};
    }

    std::uint8_t XnbHeaderFlagsByte(const XnbGraphicsProfile graphicsProfile,
                                    const XnbOutputCompression compression) noexcept
    {
        std::uint8_t flags = 0u;
        if (graphicsProfile == XnbGraphicsProfile::HiDef) { flags |= 0x01u; }
        if (compression == XnbOutputCompression::Lzx) { flags |= 0x80u; }
        else if (compression == XnbOutputCompression::Lz4) { flags |= 0x40u; }
        return flags;
    }

    void ValidateXnbFileOptions(const XnbFileOptions& options)
    {
        if (options.limits.maxFileSize <= 0 || options.limits.maxPayloadSize <= 0 ||
            options.limits.maxStringBytes <= 0 || options.limits.maxTypeWriterCount <= 0 ||
            options.limits.maxSharedResourceCount < 0 ||
            options.limits.maxCollectionElementCount < 0 ||
            options.limits.maxObjectNestingDepth <= 0)
        {
            throw XnbWriteException("XNB: every XnbWriteLimits ceiling must be positive.");
        }
        if (options.version == XnbContainerVersion::Legacy4 &&
            options.compression != XnbOutputCompression::None)
        {
            throw XnbWriteException(
                "XNB: container version 4 output is offered only uncompressed; CNA has no "
                "evidence for a compressed pre-XNA-4.0 container layout.");
        }
        if (options.compression == XnbOutputCompression::Lz4 &&
            IsXna40TargetPlatform(options.platform))
        {
            throw XnbWriteException(
                std::string("XNB: LZ4 compression (flag 0x40) is an extended-ecosystem scheme "
                            "Microsoft XNA 4.0 never produced, so it cannot be combined with the "
                            "XNA 4.0 target platform '") +
                XnbTargetPlatformName(options.platform) +
                "'. Use no compression, or LZX, for an XNA 4.0 target.");
        }
    }
}
