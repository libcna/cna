// SPDX-License-Identifier: MS-PL
// EncodeSoundEffectToCnb(): the writing half of the .cnb sound codec.
//
// Link boundary: apart from the decoder (CnbSoundEffectCodec.cpp), which every program that loads
// a .cnb sound links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <string>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

namespace CNA::Content::Cnb
{
    std::vector<std::uint8_t> EncodeSoundEffectToCnb(const CnbSoundEffectData& data,
                                                     const std::string& contentName)
    {
        // Every sound this build writes is schema 2, whatever its format: the writer declares one
        // schema identity to the build graph and the manifest, and a version that varied per asset
        // would make that declaration a guess. Schema 1 files stay readable; none are written.
        SoundEffectCodecDetail::ValidateSound(data, CnbSoundEffectSchemaVersion, nullptr);

        CnbByteWriter header;
        header.WriteU32(static_cast<std::uint32_t>(data.format));
        header.WriteU32(data.sampleRate);
        header.WriteU32(data.channels);
        header.WriteU32(data.frameCount);
        header.WriteU32(data.loopStart);
        header.WriteU32(data.loopLength);
        header.WriteU32(0u); // flags: reserved, must be zero

        CnbWriter writer(CnbAssetTypeId::SoundEffect, CnbSoundEffectSchemaVersion);
        writer.SetMetadata(SoundEffectCodecDetail::kCanonicalName, contentName);
        writer.AddChunk(CnbSoundEffectChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbSoundEffectChunk::Data, data.samples, CnbChunkFlags::Mandatory,
                        SoundEffectCodecDetail::kPayloadAlignment);
        return writer.Build();
    }
}
