// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbWriteLimits.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Target platform identifiers CNA's `.xnb` writer can emit
     *        (plans/plan_xnapipeline.md `XNAP-12`, §2.2).
     *
     * Only @ref Windows, @ref WindowsPhone and @ref Xbox360 were ever produced by Microsoft's own
     * XNA 4.0 Content Pipeline. Every other member is an **extended XNB ecosystem** identifier
     * introduced by later implementations; a file carrying one is not an XNA 4.0 asset, however
     * well-formed it is, and must never be described as one.
     */
    enum class XnbTargetPlatform
    {
        /** @brief `'w'` -- Windows. A Microsoft XNA 4.0 target. */
        Windows,
        /** @brief `'m'` -- Windows Phone 7. A Microsoft XNA 4.0 target. */
        WindowsPhone,
        /** @brief `'x'` -- Xbox 360. A Microsoft XNA 4.0 target. */
        Xbox360,
        /** @brief `'d'` -- DesktopGL. Extended XNB ecosystem. */
        DesktopGL,
        /** @brief `'l'` -- legacy Linux. Extended XNB ecosystem. */
        Linux,
        /** @brief `'i'` -- iOS. Extended XNB ecosystem. */
        iOS,
        /** @brief `'a'` -- Android. Extended XNB ecosystem. */
        Android,
        /** @brief `'g'` -- legacy Windows OpenGL. Extended XNB ecosystem. */
        WindowsGL,
    };

    /**
     * @brief The container version CNA writes.
     *
     * Version 5 is the XNA 4.0-era container. Version 4 is *earlier* XNB and is offered only for
     * legacy compatibility; it cannot express every `SurfaceFormat` version 5 can.
     */
    enum class XnbContainerVersion
    {
        /** @brief Container version 4 -- pre-XNA-4.0 legacy compatibility. */
        Legacy4,
        /** @brief Container version 5 -- the XNA 4.0-era container. CNA's default. */
        Xna40 = 1,
    };

    /** @brief The graphics profile recorded in the header's `0x01` flag bit. */
    enum class XnbGraphicsProfile
    {
        /**
         * @brief Reach -- flag bit clear. CNA's default, because a Reach asset is loadable by both
         *        Reach and HiDef games while the converse is not true.
         */
        Reach,
        /** @brief HiDef -- flag bit set. */
        HiDef,
    };

    /** @brief The payload compression CNA writes into the header's flags byte. */
    enum class XnbOutputCompression
    {
        /** @brief No compression. Always available and always readable. */
        None,
        /** @brief Flag `0x80` -- LZX, the scheme Microsoft XNA 4.0 itself produced. */
        Lzx,
        /** @brief Flag `0x40` -- one raw LZ4 block. Extended XNB ecosystem only. */
        Lz4,
    };

    /** @brief How reader type names are spelled in the emitted type-reader table. */
    enum class XnbReaderNameStyle
    {
        /**
         * @brief The assembly-qualification style Microsoft XNA 4.0 itself wrote: the reader type
         *        is qualified only when it does not live in `Microsoft.Xna.Framework`, and every
         *        generic argument is fully assembly-qualified. Maximally XNA-4.0-compatible and
         *        the writer default.
         */
        Xna40,
        /**
         * @brief Bare `Microsoft.Xna.Framework.Content.*Reader` names with no assembly
         *        qualification anywhere. CNA normalizes assembly qualification away before
         *        registry lookup, so these load in CNA; a genuine Microsoft XNA 4.0 runtime is
         *        **not** known to accept them.
         */
        Portable,
    };

    /**
     * @brief Complete container-level configuration for one emitted `.xnb` file.
     *
     * Every field has a default chosen for maximum XNA 4.0 compatibility: Windows, container
     * version 5, Reach profile, no compression, XNA-4.0 reader-name spelling.
     */
    struct XnbFileOptions
    {
        /** @brief Target platform byte written into the header. */
        XnbTargetPlatform platform = XnbTargetPlatform::Windows;

        /** @brief Container version written into the header. */
        XnbContainerVersion version = XnbContainerVersion::Xna40;

        /** @brief Graphics profile recorded in the header's `0x01` flag bit. */
        XnbGraphicsProfile graphicsProfile = XnbGraphicsProfile::Reach;

        /** @brief Payload compression recorded in the header's flags byte. */
        XnbOutputCompression compression = XnbOutputCompression::None;

        /** @brief Reader type-name spelling used in the type-reader table. */
        XnbReaderNameStyle readerNameStyle = XnbReaderNameStyle::Xna40;

        /** @brief Output ceilings applied to every length-driven write. */
        XnbWriteLimits limits{};

        /** @brief Compares every container-level option, limits included. */
        bool operator==(const XnbFileOptions& other) const = default;
    };

    /**
     * @brief Returns the single header byte for a target platform.
     *
     * @param platform The configured target platform.
     * @return The platform identifier byte written at offset 3.
     */
    [[nodiscard]] char XnbPlatformByte(XnbTargetPlatform platform) noexcept;

    /**
     * @brief Returns whether Microsoft's own XNA 4.0 Content Pipeline ever produced this platform.
     *
     * @param platform The configured target platform.
     * @return True only for Windows, Windows Phone and Xbox 360.
     */
    [[nodiscard]] bool IsXna40TargetPlatform(XnbTargetPlatform platform) noexcept;

    /**
     * @brief Returns the stable lowercase CLI/configuration spelling of a target platform.
     *
     * @param platform The configured target platform.
     * @return A process-lifetime string literal such as `"windows"` or `"desktopgl"`.
     */
    [[nodiscard]] const char* XnbTargetPlatformName(XnbTargetPlatform platform) noexcept;

    /**
     * @brief Parses the stable lowercase spelling of a target platform.
     *
     * @param name Configuration or command-line spelling.
     * @param platform Receives the parsed platform when parsing succeeds.
     * @return True when @p name named a supported platform.
     */
    [[nodiscard]] bool TryParseXnbTargetPlatform(const std::string& name,
                                                 XnbTargetPlatform& platform);

    /**
     * @brief Returns every supported target-platform spelling in declaration order.
     *
     * @return Stable names suitable for a diagnostic listing.
     */
    [[nodiscard]] std::vector<std::string> XnbTargetPlatformNames();

    /**
     * @brief Returns the numeric container version byte.
     *
     * @param version The configured container version.
     * @return `4` or `5`.
     */
    [[nodiscard]] std::uint8_t XnbContainerVersionByte(XnbContainerVersion version) noexcept;

    /**
     * @brief Composes the header flags byte from the profile and compression selections.
     *
     * @param graphicsProfile The configured graphics profile.
     * @param compression The configured payload compression.
     * @return The byte written at offset 5.
     */
    [[nodiscard]] std::uint8_t XnbHeaderFlagsByte(XnbGraphicsProfile graphicsProfile,
                                                  XnbOutputCompression compression) noexcept;

    /**
     * @brief Validates a complete option set before any content is written.
     *
     * @param options The options to validate.
     * @throws XnbWriteException for an unsupported combination, naming the reason.
     */
    void ValidateXnbFileOptions(const XnbFileOptions& options);
}
