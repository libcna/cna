// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"

// plans/plan_xnb.md XNB-34: SongReader -- see PrimitiveContentTypeReaders.hpp's own note on why this
// lives in CNA::Internal::Xnb (FNA's SongReader is `internal class`, never subclassed by game
// code).

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.SongReader`
     *        (`src/Content/ContentReaders/SongReader.cs`).
     *
     * Unlike every other reader in this plan, `Song` is not decoded/uploaded here -- it is a thin
     * wrapper around a resolved file path, exactly matching FNA's own design (`MediaPlayer` opens
     * the file lazily at playback time). The reference string's trailing 4 characters are always
     * stripped and the resulting stem re-probed against `SongReader::supportedExtensions`
     * (`.ogg`/`.oga`/`.qoa`), matching FNA exactly -- this is correct for both real XNA content
     * (which always names a literal, never-actually-produced `.wma` reference) and MonoGame
     * content (which already names the real extension directly, but happens to always be exactly
     * 4 characters too, e.g. `.ogg`, so the same strip-and-reprobe round-trips correctly either
     * way). If no supported extension resolves to a real file, this reader falls back to the
     * un-stripped path as-is, matching FNA's own `Normalize()` fallback -- but unlike FNA (which
     * defers any resulting failure to actual playback), CNA's own `Song` constructor eagerly
     * validates the final path with `std::filesystem::exists()` and throws immediately if it
     * still doesn't resolve to a real file (a pre-existing CNA behavior, not something this reader
     * introduces or works around).
     */
    class SongReader : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Media::Song>
    {
    public:
        SongReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Media::Song>(
                  "Microsoft.Xna.Framework.Media.Song") {}

    protected:
        Microsoft::Xna::Framework::Media::Song Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Media::Song> existingInstance) override;
    };

    /** @brief Registers SongReader under its real FNA canonical name. Idempotent. */
    void RegisterSongXnbReader();
}
