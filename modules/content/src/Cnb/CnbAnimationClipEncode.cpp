// SPDX-License-Identifier: MS-PL
// EncodeAnimationClipToCnb() and WriteCnbKeyframe(): the writing half of the .cnb clip codec.
//
// Link boundary: apart from the decoder (CnbAnimationClipCodec.cpp), which every program that loads such a .cnb asset
// links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"

#include <limits>
#include <string>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CNA::Content::Cnb
{
    void WriteCnbKeyframe(CnbByteWriter& writer, const KeyframeEXT& key)
    {
        writer.WriteF64(key.Time.getTotalSecondsProperty());
        writer.WriteF32(key.Translation.X);
        writer.WriteF32(key.Translation.Y);
        writer.WriteF32(key.Translation.Z);
        writer.WriteF32(key.Rotation.X);
        writer.WriteF32(key.Rotation.Y);
        writer.WriteF32(key.Rotation.Z);
        writer.WriteF32(key.Rotation.W);
        writer.WriteF32(key.Scale.X);
        writer.WriteF32(key.Scale.Y);
        writer.WriteF32(key.Scale.Z);
    }

    std::vector<std::uint8_t> EncodeAnimationClipToCnb(const AnimationClipEXT& clip,
                                                        const std::string& contentName)
    {
        const std::size_t trackCount = clip.Tracks.size();
        std::uint64_t totalKeys = 0;
        for (const BoneTrackEXT& track : clip.Tracks)
        {
            totalKeys += track.Keys.size();
        }
        const auto maxU32 = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        if (trackCount > maxU32 || totalKeys > maxU32)
        {
            throw ContentLoadException("CNB AnimationClip: track or key count is not representable.");
        }

        CnbByteWriter header;
        header.WriteF64(clip.Duration.getTotalSecondsProperty());
        header.WriteU32(static_cast<std::uint32_t>(clip.TargetSpace));
        header.WriteU32(static_cast<std::uint32_t>(trackCount));
        header.WriteU32(static_cast<std::uint32_t>(totalKeys));

        CnbByteWriter tracks;
        CnbByteWriter keys;
        std::uint32_t firstKey = 0;
        for (const BoneTrackEXT& track : clip.Tracks)
        {
            tracks.WriteI32(track.BoneIndex);
            tracks.WriteU32(firstKey);
            tracks.WriteU32(static_cast<std::uint32_t>(track.Keys.size()));
            firstKey += static_cast<std::uint32_t>(track.Keys.size());

            for (const KeyframeEXT& key : track.Keys) { WriteCnbKeyframe(keys, key); }
        }

        CnbWriter writer(CnbAssetTypeId::AnimationClip, CnbAnimationClipSchemaVersion);
        writer.SetMetadata("Microsoft.Xna.Framework.Graphics.AnimationClipEXT", contentName);
        writer.AddChunk(CnbAnimationClipChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 8u);
        writer.AddChunk(CnbAnimationClipChunk::Tracks, tracks.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbAnimationClipChunk::Keys, keys.Take(), CnbChunkFlags::Mandatory, 8u);
        return writer.Build();
    }
}
