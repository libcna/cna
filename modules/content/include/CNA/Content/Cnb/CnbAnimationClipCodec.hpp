// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Chunk identifiers defined by the `AnimationClip` asset schema
     *        (plans/plan_cnb.md `CNBF-050`).
     */
    namespace CnbAnimationClipChunk
    {
        /** @brief `ACLH` -- duration, target space and the two array counts. Mandatory, one. */
        inline constexpr CnbChunkId Header = MakeChunkId('A', 'C', 'L', 'H');

        /** @brief `ACLT` -- the track table; each row names a range of the key array. Mandatory, one. */
        inline constexpr CnbChunkId Tracks = MakeChunkId('A', 'C', 'L', 'T');

        /** @brief `ACLK` -- one flat array holding every keyframe of every track. Mandatory, one. */
        inline constexpr CnbChunkId Keys = MakeChunkId('A', 'C', 'L', 'K');
    }

    /** @brief Highest `AnimationClip` schema version this build understands. */
    inline constexpr std::uint32_t CnbAnimationClipSchemaVersion = 1u;

    /** @brief Bytes one serialized track row occupies in the `ACLT` chunk. */
    inline constexpr std::uint32_t CnbAnimationTrackStride = 12u;

    /** @brief Bytes one serialized keyframe occupies in the `ACLK` chunk. */
    inline constexpr std::uint32_t CnbAnimationKeyStride = 48u;

    /**
     * @brief Encodes an `AnimationClipEXT` as a complete `.cnb` byte image.
     *
     * Unlike the `.cnj` representation, where each track carries its own nested key array, the
     * compiled form puts every keyframe of the whole clip into one flat, fixed-stride array and
     * gives each track a `(firstKey, keyCount)` range into it. One contiguous read reaches every
     * keyframe, which is the property that makes this a compiled format rather than a re-encoded
     * document.
     *
     * @param clip        The clip to encode.
     * @param contentName Logical content name recorded in the debug `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the clip cannot be
     *         represented.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeAnimationClipToCnb(
        const Microsoft::Xna::Framework::Graphics::AnimationClipEXT& clip,
        const std::string& contentName = {});

    /**
     * @brief Decodes an `AnimationClipEXT` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded clip.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not an
     *         `AnimationClip`, uses an unsupported schema version, is missing a mandatory chunk,
     *         declares counts its chunk lengths do not match, names a key range outside the key
     *         array, or carries a duration or key time that cannot be a `System::TimeSpan`.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::AnimationClipEXT DecodeAnimationClipFromCnb(
        const CnbDocument& document);
}
