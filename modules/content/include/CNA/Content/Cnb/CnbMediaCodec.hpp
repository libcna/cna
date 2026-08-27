// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Chunk identifiers defined by the `Song` and `Video` asset schemas
     *        (plans/plan_cnb.md `CNBF-103B`).
     */
    namespace CnbMediaChunk
    {
        /** @brief `SNGH` -- a `Song`'s duration, flags and display name. Mandatory, exactly one. */
        inline constexpr CnbChunkId SongHeader = MakeChunkId('S', 'N', 'G', 'H');

        /** @brief `VIDH` -- a `Video`'s duration, frame size, rate and soundtrack type. Mandatory, exactly one. */
        inline constexpr CnbChunkId VideoHeader = MakeChunkId('V', 'I', 'D', 'H');
    }

    /** @brief Highest `Song` and `Video` schema version this build understands. */
    inline constexpr std::uint32_t CnbMediaSchemaVersion = 1u;

    /** @brief Bytes the fixed part of the `SNGH` chunk occupies, before the display name. */
    inline constexpr std::uint32_t CnbSongHeaderFixedStride = 8u;

    /** @brief Bytes the `VIDH` chunk occupies. */
    inline constexpr std::uint32_t CnbVideoHeaderStride = 24u;

    /** @brief Highest frame dimension a `Video` may declare, in pixels. */
    inline constexpr std::uint32_t CnbMaxVideoDimension = 65536u;

    /**
     * @brief The decoded contents of a `Song` `.cnb`.
     *
     * A `Song` `.cnb` is **metadata plus a reference**, never an embedded audio blob. A song can
     * be hundreds of megabytes and wants streaming, seeking and buffering; embedding it would
     * force the whole thing through the container's chunk machinery and into memory to play the
     * first second of it. So the file tells the runtime *what* to stream and *how*, and the media
     * itself stays beside it — recorded as the file's single `XREF` entry, which is exactly what
     * that table is for and what makes the dependency visible to `cna_tool_cnb_info --refs`.
     */
    struct CnbSongData
    {
        /** @brief Logical name of the media file to stream, from the file's `XREF` table. */
        std::string streamReference;

        /** @brief Display name of the song; may be empty. */
        std::string name;

        /** @brief Duration in milliseconds; 0 when the compiler could not determine it. */
        std::uint32_t durationMs = 0u;
    };

    /**
     * @brief The decoded contents of a `Video` `.cnb`.
     *
     * Metadata plus a streaming reference, for the same reasons as CnbSongData.
     */
    struct CnbVideoData
    {
        /** @brief Logical name of the media file to stream, from the file's `XREF` table. */
        std::string streamReference;

        /** @brief Duration in milliseconds; 0 when the compiler could not determine it. */
        std::uint32_t durationMs = 0u;

        /** @brief Frame width in pixels; 1…65536. */
        std::uint32_t width = 1u;

        /** @brief Frame height in pixels; 1…65536. */
        std::uint32_t height = 1u;

        /** @brief Frame rate; must be finite and greater than zero. */
        float framesPerSecond = 30.0f;

        /** @brief `VideoSoundtrackType` value: 0 Music, 1 Dialog, 2 MusicAndDialog. */
        std::uint32_t soundtrackType = 0u;
    };

    /**
     * @brief Encodes a `Song` as a complete `.cnb` byte image.
     *
     * @param data        The song to encode. `streamReference` must not be empty.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if `streamReference` is
     *         empty or is not a valid relative logical name.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeSongToCnb(const CnbSongData& data,
                                                            const std::string& contentName = {});

    /**
     * @brief Encodes a `Video` as a complete `.cnb` byte image.
     *
     * @param data        The video to encode. `streamReference` must not be empty.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if `streamReference` is
     *         empty, a dimension is out of range, the frame rate is not positive and finite, or
     *         the soundtrack type is not a `VideoSoundtrackType` value.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeVideoToCnb(const CnbVideoData& data,
                                                             const std::string& contentName = {});

    /**
     * @brief Decodes a `Song` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded song description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `Song`, uses an unsupported schema version, is missing a mandatory chunk, or does
     *         not name exactly one external reference.
     */
    [[nodiscard]] CnbSongData DecodeSongFromCnb(const CnbDocument& document);

    /**
     * @brief Decodes a `Video` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded video description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on the same conditions as
     *         DecodeSongFromCnb(), plus an out-of-range dimension, frame rate or soundtrack type.
     */
    [[nodiscard]] CnbVideoData DecodeVideoFromCnb(const CnbDocument& document);
}
