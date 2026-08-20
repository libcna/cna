// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

// plans/plan_media.md MEDIA-70: VideoReader -- see SongContentTypeReader.hpp's own note on why this
// lives in CNA::Internal::Xnb (FNA's VideoReader is `internal class`, never subclassed by game
// code). No CNA equivalent existed before this task; plans/plan_xnb.md never planned one either.

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.VideoReader`
     *        (`src/Content/ContentReaders/VideoReader.cs`).
     *
     * The reference string's trailing 4 characters are always stripped (FNA's real content always
     * names a literal, never-actually-produced `.wmv` reference) and the resulting stem re-probed
     * against `VideoReader::supportedExtensions` (`.ogv`/`.ogg`), matching FNA's `Normalize()`
     * exactly -- see `SongContentTypeReader`'s identically-shaped logic. If no supported extension
     * resolves to a real file, falls back to the un-stripped path as-is, matching FNA's own
     * fallback (the eventual failure surfaces from `Video`'s own constructor, which -- unlike
     * FNA -- now eagerly validates via `std::filesystem::exists()`, per plans/plan_media.md MEDIA-44).
     *
     * Field read order matches FNA's real binary layout exactly: reference string, durationMS
     * (int32), width (int32), height (int32), framesPerSecond (float32), soundTrackType (int32,
     * cast to VideoSoundtrackType). Uses direct typed reads (ReadInt32/ReadSingle), matching
     * SongContentTypeReader's established code style, rather than FNA's own internal
     * inconsistency (SongReader reads its one field directly; VideoReader reads all 5 of its own
     * fields via the more generic `input.ReadObject<T>()`) -- a pure C# implementation-detail
     * difference with no effect on the actual binary format, so not worth replicating here
     * (plans/plan_media.md MEDIA-71).
     */
    class VideoReader : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Media::Video>
    {
    public:
        VideoReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Media::Video>(
                  "Microsoft.Xna.Framework.Media.Video") {}

    protected:
        Microsoft::Xna::Framework::Media::Video Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Media::Video> existingInstance) override;
    };

    /** @brief Registers VideoReader under its real FNA canonical name. Idempotent. */
    void RegisterVideoXnbReader();
}
