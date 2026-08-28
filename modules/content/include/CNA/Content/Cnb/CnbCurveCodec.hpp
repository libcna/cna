// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"

namespace CNA::Content::Cnb
{
    /** @brief Chunk identifiers defined by the `Curve` asset schema (plans/plan_cnb.md `CNBF-040`). */
    namespace CnbCurveChunk
    {
        /** @brief `CRVH` -- loop behaviour and the key count. Mandatory, exactly one. */
        inline constexpr CnbChunkId Header = MakeChunkId('C', 'R', 'V', 'H');

        /** @brief `CRVK` -- the flat array of curve keys. Mandatory, exactly one. */
        inline constexpr CnbChunkId Keys = MakeChunkId('C', 'R', 'V', 'K');
    }

    /** @brief Highest `Curve` schema version this build understands. */
    inline constexpr std::uint32_t CnbCurveSchemaVersion = 1u;

    /** @brief Bytes one serialized curve key occupies in the `CRVK` chunk. */
    inline constexpr std::uint32_t CnbCurveKeyStride = 20u;

    /**
     * @brief Encodes a `Curve` as a complete `.cnb` byte image.
     *
     * The result is a compiled representation, not a transcription of a `.cnj` document: the keys
     * become one flat, fixed-stride array that decodes in a single pass with no per-key lookups.
     *
     * @param curve       The curve to encode.
     * @param contentName Logical content name recorded in the debug `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the curve cannot be
     *         represented (for example, more keys than the format can count).
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeCurveToCnb(
        const Microsoft::Xna::Framework::Curve& curve, const std::string& contentName = {});

    /**
     * @brief Decodes a `Curve` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded curve.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `Curve`, uses an unsupported schema version, is missing a mandatory chunk, has a
     *         chunk whose length disagrees with its declared counts, or carries an out-of-range
     *         enumerator.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Curve DecodeCurveFromCnb(const CnbDocument& document);
}
