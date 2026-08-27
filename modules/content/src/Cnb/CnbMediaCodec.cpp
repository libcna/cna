// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"

#include <cmath>
#include <string>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        constexpr const char* kSongName = "Microsoft.Xna.Framework.Media.Song";
        constexpr const char* kVideoName = "Microsoft.Xna.Framework.Media.Video";
        constexpr std::uint32_t kMaxSoundtrackType = 2u; // Music, Dialog, MusicAndDialog

        [[noreturn]] void Fail(const char* label, const std::string& what)
        {
            throw ContentLoadException(std::string("CNB ") + label + ": " + what);
        }

        /// Builds the single-entry XREF table both schemas use for their media file. The reference
        /// is validated by CnbWriter itself (relative, `/`-separated, no `..`), so this only has
        /// to reject the empty case the table would otherwise accept as a legal-looking nothing.
        std::vector<CnbExternalReference> MakeStreamReference(const char* label,
                                                              const std::string& streamReference)
        {
            if (streamReference.empty())
            {
                Fail(label, "names no media file to stream. A " + std::string(label) +
                                " .cnb carries metadata and a reference, not the media itself, so "
                                "the reference is the one thing it cannot omit.");
            }
            // The READER enforces these rules on every XREF name (docs/cnb-format.md §5.1), but
            // the writer did not -- so an encoder could produce a file its own decoder refuses.
            // Every other CNB schema is written so that cannot happen, and this schema is the
            // first whose reference comes straight from a compiler's command line, which is
            // exactly where a traversal would enter. Checked here rather than in CnbWriter so the
            // frozen container layer is not touched.
            if (streamReference.find('\\') != std::string::npos)
            {
                Fail(label, "'" + streamReference +
                                "' contains a backslash; .cnb logical names use '/' only.");
            }
            if (streamReference.front() == '/' ||
                (streamReference.size() >= 2u && streamReference[1] == ':'))
            {
                Fail(label, "'" + streamReference +
                                "' is an absolute path; a media reference is always relative to "
                                "the content root.");
            }
            for (std::size_t start = 0u; start <= streamReference.size();)
            {
                const std::size_t slash = streamReference.find('/', start);
                const std::size_t end =
                    slash == std::string::npos ? streamReference.size() : slash;
                if (streamReference.compare(start, end - start, "..") == 0)
                {
                    Fail(label, "'" + streamReference + "' contains a '..' segment.");
                }
                if (slash == std::string::npos) { break; }
                start = slash + 1u;
            }
            CnbExternalReference reference;
            reference.flags = 0u;
            // The schema does not constrain the referenced type: the target is a media file on
            // disk (an .ogg, an .mp4), not a CNA asset with a CNB asset type identifier.
            reference.expectedAssetTypeId = CnbAssetTypeId::Invalid;
            reference.logicalName = streamReference;
            return {reference};
        }

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
            return references[0].logicalName;
        }
    }

    std::vector<std::uint8_t> EncodeSongToCnb(const CnbSongData& data,
                                              const std::string& contentName)
    {
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
        data.streamReference = RequireSingleStreamReference("Video", document);
        return data;
    }
}
