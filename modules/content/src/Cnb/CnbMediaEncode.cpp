// SPDX-License-Identifier: MS-PL
// EncodeSongToCnb() and EncodeVideoToCnb(): the writing half of the .cnb media codec.
//
// Link boundary: apart from the decoder (CnbMediaCodec.cpp), which every program that loads such a .cnb asset
// links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <cmath>
#include <string>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

namespace CNA::Content::Cnb
{
    namespace
    {
        using namespace MediaCodecDetail;

        constexpr const char* kSongName = "Microsoft.Xna.Framework.Media.Song";
        constexpr const char* kVideoName = "Microsoft.Xna.Framework.Media.Video";

        void RequireRepresentableDuration(const char* label, std::uint32_t durationMs)
        {
            if (durationMs > kMaxDurationMs)
            {
                Fail(label, "declares a duration of " + std::to_string(durationMs) +
                                " ms, above the " + std::to_string(kMaxDurationMs) +
                                " ms this schema can carry: the runtime constructor takes a signed "
                                "32-bit millisecond count, so a larger value would arrive "
                                "negative.");
            }
        }

        /// Builds the single-entry XREF table both schemas use for their media file.
        ///
        /// The name is checked against the container's own rule (CnbLogicalNameProblem, the same
        /// function CnbDocument and CnbWriter call -- CNBF-115) rather than
        /// against a copy of it. This schema's reference comes straight from a compiler's command
        /// line, which is exactly where a traversal would enter, and the message names the media
        /// reference rather than an anonymous XREF row so the user learns which argument was wrong.
        std::vector<CnbExternalReference> MakeStreamReference(const char* label,
                                                              const std::string& streamReference)
        {
            if (streamReference.empty())
            {
                Fail(label, "names no media file to stream. A " + std::string(label) +
                                " .cnb carries metadata and a reference, not the media itself, so "
                                "the reference is the one thing it cannot omit.");
            }
            if (const std::string problem = CnbLogicalNameProblem(streamReference);
                !problem.empty())
            {
                Fail(label, "media reference '" + streamReference + "' " + problem +
                                "; a media reference is always relative to the content root.");
            }
            CnbExternalReference reference;
            reference.flags = 0u;
            // The schema does not constrain the referenced type: the target is a media file on
            // disk (an .ogg, an .mp4), not a CNA asset with a CNB asset type identifier.
            reference.expectedAssetTypeId = CnbAssetTypeId::Invalid;
            reference.logicalName = streamReference;
            return {reference};
        }
    }

    std::vector<std::uint8_t> EncodeSongToCnb(const CnbSongData& data,
                                              const std::string& contentName)
    {
        RequireRepresentableDuration("Song", data.durationMs);

        CnbByteWriter header;
        header.WriteU32(data.durationMs);
        header.WriteU32(0u); // flags: reserved, must be zero
        header.WriteString(data.name);

        CnbWriter writer(CnbAssetTypeId::Song, CnbMediaSchemaVersion);
        writer.SetMetadata(kSongName, contentName);
        writer.SetExternalReferences(MakeStreamReference("Song", data.streamReference));
        writer.AddChunk(CnbMediaChunk::SongHeader, header.Take(), CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }

    std::vector<std::uint8_t> EncodeVideoToCnb(const CnbVideoData& data,
                                               const std::string& contentName)
    {
        RequireRepresentableDuration("Video", data.durationMs);
        if (data.width == 0u || data.width > CnbMaxVideoDimension || data.height == 0u ||
            data.height > CnbMaxVideoDimension)
        {
            Fail("Video", "declares a " + std::to_string(data.width) + "x" +
                              std::to_string(data.height) + " frame; each dimension must be 1-" +
                              std::to_string(CnbMaxVideoDimension) + ".");
        }
        if (!std::isfinite(data.framesPerSecond) || data.framesPerSecond <= 0.0f)
        {
            Fail("Video", "declares a frame rate of " + std::to_string(data.framesPerSecond) +
                              "; it must be finite and greater than zero.");
        }
        if (data.soundtrackType > kMaxSoundtrackType)
        {
            Fail("Video", "declares soundtrack type " + std::to_string(data.soundtrackType) +
                              ", which is not a VideoSoundtrackType value (0-2).");
        }

        CnbByteWriter header;
        header.WriteU32(data.durationMs);
        header.WriteU32(data.width);
        header.WriteU32(data.height);
        header.WriteF32(data.framesPerSecond);
        header.WriteU32(data.soundtrackType);
        header.WriteU32(0u); // flags: reserved, must be zero

        CnbWriter writer(CnbAssetTypeId::Video, CnbMediaSchemaVersion);
        writer.SetMetadata(kVideoName, contentName);
        writer.SetExternalReferences(MakeStreamReference("Video", data.streamReference));
        writer.AddChunk(CnbMediaChunk::VideoHeader, header.Take(), CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }
}
