// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbCurveCodec.hpp"

#include <limits>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        // The enumerators are frozen by the file format: their numeric values are what a .cnb
        // stores, so they are spelled out here rather than derived from the enum, and a value
        // outside the range is a corrupt file rather than something to cast blindly.
        constexpr std::uint32_t kMaxCurveLoopType = 4u;   // Constant..Linear
        constexpr std::uint32_t kMaxCurveContinuity = 1u; // Smooth..Step

        CurveLoopType DecodeLoopType(std::uint32_t value, CnbByteReader& reader, const char* which)
        {
            if (value > kMaxCurveLoopType)
            {
                reader.Fail(std::string(which) + " is " + std::to_string(value) +
                            ", which is not a CurveLoopType value (0-4).");
            }
            return static_cast<CurveLoopType>(value);
        }
    }

    Curve DecodeCurveFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Curve, CnbCurveSchemaVersion);
        const CnbChunkId known[] = {CnbCurveChunk::Header, CnbCurveChunk::Keys};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header = document.OpenChunk(document.RequireSingle(CnbCurveChunk::Header));
        Curve curve;
        curve.setPreLoopProperty(DecodeLoopType(header.ReadU32(), header, "preLoop"));
        curve.setPostLoopProperty(DecodeLoopType(header.ReadU32(), header, "postLoop"));
        const std::uint32_t keyCount = header.ReadU32();
        if (keyCount > document.Limits().maxArrayElementCount)
        {
            header.Fail("declares " + std::to_string(keyCount) +
                        " curve keys, above the configured limit.");
        }
        header.RequireExhausted();

        CnbByteReader keys = document.OpenChunk(document.RequireSingle(CnbCurveChunk::Keys));
        // The key chunk's length has to agree exactly with the header's count -- a shorter chunk
        // would truncate the curve and a longer one would mean the two chunks disagree about what
        // the file holds. Either way it is a corrupt file, not something to interpret.
        const std::uint64_t expected =
            static_cast<std::uint64_t>(keyCount) * CnbCurveKeyStride;
        if (keys.Size() != expected)
        {
            keys.Fail("holds " + std::to_string(keys.Size()) + " byte(s) but the header declares " +
                      std::to_string(keyCount) + " key(s), which need " +
                      std::to_string(expected) + " byte(s).");
        }

        for (std::uint32_t i = 0; i < keyCount; ++i)
        {
            const float position = keys.ReadF32();
            const float value = keys.ReadF32();
            const float tangentIn = keys.ReadF32();
            const float tangentOut = keys.ReadF32();
            const std::uint32_t continuity = keys.ReadU32();
            if (continuity > kMaxCurveContinuity)
            {
                keys.Fail("key " + std::to_string(i) + " has continuity " +
                          std::to_string(continuity) +
                          ", which is not a CurveContinuity value (0-1).");
            }
            curve.getKeysProperty().Add(CurveKey(position, value, tangentIn, tangentOut,
                                                 static_cast<CurveContinuity>(continuity)));
        }
        keys.RequireExhausted();

        return curve;
    }
}
