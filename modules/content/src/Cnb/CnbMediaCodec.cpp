// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <cmath>
#include <string>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace MediaCodecDetail
    {
        [[noreturn]] void Fail(const char* label, const std::string& what)
        {
            throw ContentLoadException(std::string("CNB ") + label + ": " + what);
        }
    }

    namespace
    {
        using namespace MediaCodecDetail;

        /// Both schemas require exactly one reference; the media file is the whole point of the
        /// asset, and a second one would be a dependency nothing knows how to interpret.
        std::string RequireSingleStreamReference(const char* label, const CnbDocument& document)
        {
            const std::vector<CnbExternalReference>& references = document.ExternalReferences();
            if (references.size() != 1u)
            {
                Fail(label, "names " + std::to_string(references.size()) +
                                " external references; exactly one media file is required.");
            }
            if (references[0].logicalName.empty())
            {
                Fail(label, "names an empty media file reference.");
            }
            // The schema specifies both of these (docs/cnb-format.md §19.1), and both were
            // written and neither was read back (`CNBF-119`). `flags` is
            // reserved, so a set bit means the file was written to a schema this build does not
            // know. `expectedAssetTypeId` must be Invalid because the target is a media file on
            // disk -- an .ogg, an .mp4 -- not a CNA asset with a CNB type identifier; a file
            // naming one is describing a dependency this schema cannot honour.
            if (references[0].flags != 0u)
            {
                Fail(label, "sets reserved flag bits on its media reference; this schema version "
                            "defines none.");
            }
            if (references[0].expectedAssetTypeId != CnbAssetTypeId::Invalid)
            {
                Fail(label, "expects its media reference to be a " +
                                AssetTypeIdToString(references[0].expectedAssetTypeId) +
                                " asset, but a media reference names a file to stream rather than "
                                "a CNA asset, so its expected type must be 0.");
            }
            return references[0].logicalName;
        }
    }

    CnbSongData DecodeSongFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Song, CnbMediaSchemaVersion);
        const CnbChunkId known[] = {CnbMediaChunk::SongHeader};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header = document.OpenChunk(document.RequireSingle(CnbMediaChunk::SongHeader));
        CnbSongData data;
        data.durationMs = header.ReadU32();
        const std::uint32_t flags = header.ReadU32();
        data.name = header.ReadString();
        header.RequireExhausted();

        if (flags != 0u)
        {
            header.Fail("sets reserved flag bits; this schema version defines none.");
        }
        if (data.durationMs > kMaxDurationMs)
        {
            header.Fail("declares a duration of " + std::to_string(data.durationMs) +
                        " ms, which does not fit the signed 32-bit millisecond count Song's "
                        "constructor takes.");
        }
        data.streamReference = RequireSingleStreamReference("Song", document);
        return data;
    }

    CnbVideoData DecodeVideoFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Video, CnbMediaSchemaVersion);
        const CnbChunkId known[] = {CnbMediaChunk::VideoHeader};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header =
            document.OpenChunk(document.RequireSingle(CnbMediaChunk::VideoHeader));
        CnbVideoData data;
        data.durationMs = header.ReadU32();
        data.width = header.ReadU32();
        data.height = header.ReadU32();
        data.framesPerSecond = header.ReadF32();
        data.soundtrackType = header.ReadU32();
        const std::uint32_t flags = header.ReadU32();
        header.RequireExhausted();

        if (flags != 0u)
        {
            header.Fail("sets reserved flag bits; this schema version defines none.");
        }
        if (data.width == 0u || data.width > CnbMaxVideoDimension || data.height == 0u ||
            data.height > CnbMaxVideoDimension)
        {
            header.Fail("declares a " + std::to_string(data.width) + "x" +
                        std::to_string(data.height) + " frame; each dimension must be 1-" +
                        std::to_string(CnbMaxVideoDimension) + ".");
        }
        // Read back with the same rule the writer applied. A non-finite frame rate is a perfectly
        // well-formed f32 that would divide badly in a player, so the check has to be here too --
        // §2.1 is explicit that the container stores whatever bit pattern it was given.
        if (!std::isfinite(data.framesPerSecond) || data.framesPerSecond <= 0.0f)
        {
            header.Fail("declares a frame rate that is not finite and positive.");
        }
        if (data.soundtrackType > kMaxSoundtrackType)
        {
            header.Fail("declares soundtrack type " + std::to_string(data.soundtrackType) +
                        ", which is not a VideoSoundtrackType value (0-2).");
        }
        if (data.durationMs > kMaxDurationMs)
        {
            header.Fail("declares a duration of " + std::to_string(data.durationMs) +
                        " ms, which does not fit the signed 32-bit millisecond count Video's "
                        "constructor takes.");
        }
        data.streamReference = RequireSingleStreamReference("Video", document);
        return data;
    }
}
