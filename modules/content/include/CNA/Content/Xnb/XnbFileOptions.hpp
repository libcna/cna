// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

namespace CNA::Content::Xnb
{
    /**
     * @brief Target platform identifiers a produced `.xnb` container may declare
     *        (plans/plan_xnapipeline.md `XNAP-003`).
     *
     * The enumerator values are the literal platform bytes stored at offset 3 of the container.
     * The set is deliberately narrower than the 16 identifiers CNA's *reader* accepts: a writer
     * should only claim a platform whose per-platform encoding rules it actually implements, and
     * every identifier here shares the little-endian, desktop-oriented rules this writer emits.
     * Xbox 360 (`'x'`) is excluded on purpose, because its `SoundEffect` format block is
     * big-endian and CNA has no way to validate the result.
     */
    enum class XnbTargetPlatform : char
    {
        /** @brief Microsoft Windows (`'w'`), the XNA 4.0 default and this writer's default. */
        Windows = 'w',

        /** @brief Windows Phone 7 (`'m'`). */
        WindowsPhone = 'm',

        /** @brief MonoGame/FNA desktop OpenGL (`'d'`). */
        DesktopGL = 'd',

        /** @brief MonoGame macOS (`'X'`). */
        MacOSX = 'X',

        /** @brief MonoGame Linux (`'l'`), a deprecated identifier FNA still accepts. */
        Linux = 'l',

        /** @brief MonoGame iOS (`'i'`). */
        iOS = 'i',

        /** @brief MonoGame Android (`'a'`). */
        Android = 'a',
    };

    /** @brief Graphics profile declared by the container's flags byte. */
    enum class XnbGraphicsProfile
    {
        /** @brief Reach: flag bit `0x01` clear. */
        Reach,

        /** @brief HiDef: flag bit `0x01` set. */
        HiDef,
    };

    /** @brief Payload compression declared by the container's flags byte. */
    enum class XnbWriteCompression
    {
        /** @brief No compression; the body follows the 10-byte header verbatim. */
        None,
    };

    /** @brief Container version this writer emits by default. */
    inline constexpr int XnbDefaultFormatVersion = 5;

    /** @brief Bytes occupied by the fixed, uncompressed container header. */
    inline constexpr std::size_t XnbHeaderSize = 10u;

    /** @brief Byte offset of the container's total-size field, patched once the body is complete. */
    inline constexpr std::size_t XnbTotalSizeOffset = 6u;

    /** @brief Complete, explicit description of the container a build should produce. */
    struct XnbFileOptions
    {
        /** @brief Platform identifier byte written at offset 3. */
        XnbTargetPlatform platform = XnbTargetPlatform::Windows;

        /** @brief Container version; 4 and 5 are the only values XNA 4.0 readers accept. */
        int version = XnbDefaultFormatVersion;

        /** @brief Graphics profile encoded in flag bit `0x01`. */
        XnbGraphicsProfile profile = XnbGraphicsProfile::Reach;

        /** @brief Payload compression encoded in flag bit `0x80`. */
        XnbWriteCompression compression = XnbWriteCompression::None;

        /** @brief Compares every declared container field. */
        bool operator==(const XnbFileOptions&) const = default;
    };

    /**
     * @brief Returns the platform identifier byte for @p platform.
     *
     * @param platform The target platform.
     * @return The literal byte stored at container offset 3.
     */
    [[nodiscard]] char XnbPlatformByte(XnbTargetPlatform platform) noexcept;

    /**
     * @brief Parses a stable lowercase platform name used by configuration and the command line.
     *
     * Accepted spellings are `windows`, `windowsphone`, `desktopgl`, `macosx`, `linux`, `ios` and
     * `android`.
     *
     * @param name Case-sensitive stable spelling.
     * @return The matching platform.
     * @throws XnbWriteException when @p name is not one of the accepted spellings.
     */
    [[nodiscard]] XnbTargetPlatform ParseXnbTargetPlatform(const std::string& name);

    /**
     * @brief Returns the stable lowercase spelling of @p platform.
     *
     * @param platform The target platform.
     * @return A process-lifetime string literal.
     */
    [[nodiscard]] const char* XnbTargetPlatformName(XnbTargetPlatform platform) noexcept;

    /**
     * @brief Validates a complete container description before any byte is produced.
     *
     * @param options The description to validate.
     * @throws XnbWriteException for an unsupported version, platform or compression selection.
     */
    void ValidateXnbFileOptions(const XnbFileOptions& options);
}
