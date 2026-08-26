// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
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
     * @brief Reads a `f64` seconds value and rejects anything a `System::TimeSpan` cannot hold.
     *
     * `System::TimeSpan::FromSeconds` throws `System::ArgumentException`/`OverflowException` for a
     * NaN or out-of-range value. Those are perfectly good exceptions, but they are not what the
     * content subsystem's callers catch, and a malformed file must surface as a
     * `ContentLoadException` naming the file. This checks the range before the value can reach
     * `TimeSpan` at all.
     *
     * @param reader Cursor positioned at the `f64`.
     * @param what   Noun used in the exception message, e.g. `"the clip duration"`.
     * @return The value, guaranteed convertible with `System::TimeSpan::FromSeconds`.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the value is not finite
     *         or is out of `TimeSpan`'s range.
     */
    [[nodiscard]] double ReadCnbSeconds(CnbByteReader& reader, const char* what);

    /**
     * @brief Writes one keyframe in the canonical 48-byte `.cnb` layout.
     *
     * Shared by the standalone `AnimationClip` schema and by a `Model`'s embedded clips, so both
     * store keyframes identically -- there is exactly one keyframe encoding in CNB.
     *
     * @param writer Destination.
     * @param key    The keyframe to write.
     */
    void WriteCnbKeyframe(CnbByteWriter& writer,
                          const Microsoft::Xna::Framework::Graphics::KeyframeEXT& key);

    /**
     * @brief Reads one keyframe written by WriteCnbKeyframe().
     *
     * @param reader Cursor positioned at the keyframe.
     * @return The decoded keyframe.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation or a time
     *         value no `System::TimeSpan` can hold.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::KeyframeEXT ReadCnbKeyframe(
        CnbByteReader& reader);

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
