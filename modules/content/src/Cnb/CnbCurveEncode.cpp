// SPDX-License-Identifier: MS-PL
// EncodeCurveToCnb(): the writing half of the .cnb curve codec.
//
// Link boundary: apart from the decoder (CnbCurveCodec.cpp), which every program that loads such a .cnb asset
// links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"

#include <limits>
#include <string>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    std::vector<std::uint8_t> EncodeCurveToCnb(const Curve& curve, const std::string& contentName)
    {
        const auto& keys = curve.getKeysProperty();
        const int count = keys.getCountProperty();
        if (count < 0 || static_cast<std::uint64_t>(count) >
                             static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw ContentLoadException("CNB Curve: key count is not representable.");
        }

        CnbByteWriter header;
        header.WriteU32(static_cast<std::uint32_t>(curve.getPreLoopProperty()));
        header.WriteU32(static_cast<std::uint32_t>(curve.getPostLoopProperty()));
        header.WriteU32(static_cast<std::uint32_t>(count));

        CnbByteWriter keyBytes;
        for (int i = 0; i < count; ++i)
        {
            const CurveKey& key = keys[i];
            keyBytes.WriteF32(key.getPositionProperty());
            keyBytes.WriteF32(key.getValueProperty());
            keyBytes.WriteF32(key.getTangentInProperty());
            keyBytes.WriteF32(key.getTangentOutProperty());
            keyBytes.WriteU32(static_cast<std::uint32_t>(key.getContinuityProperty()));
        }

        CnbWriter writer(CnbAssetTypeId::Curve, CnbCurveSchemaVersion);
        writer.SetMetadata("Microsoft.Xna.Framework.Curve", contentName);
        writer.AddChunk(CnbCurveChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbCurveChunk::Keys, keyBytes.Take(), CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }
}
