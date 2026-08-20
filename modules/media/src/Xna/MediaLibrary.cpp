// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <utility>

#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Media/AudioDurationProbe.hpp"
#include "CNA/Internal/Media/AudioTagParser.hpp"
#include "CNA/Internal/Media/MediaLibraryIndex.hpp"
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"
#include "CNA/Internal/Media/PictureLibraryIndex.hpp"
#include "CNA/Internal/Media/PlaylistParser.hpp"
#include "CNA/Internal/Media/SavedPictureStore.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/IOException.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    namespace
    {
        // Looks for a cover-art image in the same directory as `songPath`, matching the common
        // "cover.jpg"/"folder.jpg" file-naming convention (plans/plan_media.md MEDIA-65/R2).
        std::string FindAlbumArtPath(const std::string& songPath)
        {
            std::filesystem::path dir = std::filesystem::path(songPath).parent_path();

            // Precedence order, most-specific first. Real-world libraries are wildly inconsistent
            // about this, so several conventions are accepted rather than just cover.jpg/folder.jpg
            // (plans/plan_media.md MEDIA-205).
            static const char* kCandidates[] = {
                "cover.jpg", "cover.jpeg", "cover.png",
                "folder.jpg", "folder.jpeg", "folder.png",
                "front.jpg", "front.jpeg", "front.png",
                "album.jpg", "album.png",
                "albumart.jpg", "albumart.png",
            };

            // Linux filesystems are case-sensitive but taggers are not consistent about case, so
            // match case-insensitively by scanning the directory once rather than stat-ing every
            // candidate in every casing.
            std::error_code ec;
            std::vector<std::string> namesInDir;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec) break;
                if (entry.is_regular_file(ec) && !ec)
                {
                    namesInDir.push_back(entry.path().filename().string());
                }
            }

            for (const char* candidate : kCandidates)
            {
                for (const std::string& name : namesInDir)
                {
                    std::string lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower == candidate)
                    {
                        return (dir / name).string();
                    }
                }
            }
            return {};
        }
    }

    MediaLibrary::MediaLibrary()
    {
        mediaSource_ = std::unique_ptr<MediaSource>(
            new MediaSource(MediaSourceType::LocalDevice, "Local Device"));
        BuildFromRoots(CNA::Internal::Media::MediaLibraryPaths::GetMusicRoot(),
                        CNA::Internal::Media::MediaLibraryPaths::GetPictureRoot());
    }

    MediaLibrary::MediaLibrary(MediaSource* mediaSource)
    {
        if (mediaSource == nullptr)
        {
            throw System::ArgumentNullException("mediaSource");
        }
        if (mediaSource->getMediaSourceTypeProperty() != MediaSourceType::LocalDevice)
        {
            throw System::NotSupportedException(
                "Only MediaSourceType::LocalDevice is supported.");
        }
        mediaSource_ = std::unique_ptr<MediaSource>(
            new MediaSource(mediaSource->getMediaSourceTypeProperty(), mediaSource->getNameProperty()));
        BuildFromRoots(CNA::Internal::Media::MediaLibraryPaths::GetMusicRoot(),
                        CNA::Internal::Media::MediaLibraryPaths::GetPictureRoot());
    }

    void MediaLibrary::BuildFromRoots(const std::string& musicRoot, const std::string& pictureRoot)
    {
        pictureRoot_ = pictureRoot;

        // --- Songs ---
        // Real Duration (plans/plan_media.md MEDIA-65/68's "Duration (sum of member Song.Duration)")
        // needs each Song's own duration populated for real -- a lightweight, decode-free
        // container-metadata probe (AudioDurationProbe, FFmpeg's avformat_find_stream_info only)
        // rather than the deferred "stays zero until actually played via MediaPlayer" design this
        // used to rely on, which never actually backfilled the library's own Song/Album/Playlist
        // objects even when a song *was* played (MediaPlayer::Play() operates on a duplicate --
        // see MediaPlayer::LoadSong() -- not the library's original Song instance). Found by
        // external code review.
        CNA::Internal::Media::MediaLibraryIndex musicIndex(musicRoot);
        for (const auto& indexed : musicIndex.GetSongs())
        {
            const SharpRuntime::intcs durationMs =
                CNA::Internal::Media::AudioDurationProbe::ProbeDurationMS(indexed.path);
            auto song = durationMs > 0
                ? std::make_unique<Song>(indexed.path, indexed.title, durationMs)
                : std::make_unique<Song>(indexed.path, indexed.title);
            songByPath_[indexed.path] = song.get();
            ownedSongs_.push_back(std::move(song));
        }
        std::vector<Song*> allSongPtrs;
        for (auto& s : ownedSongs_) allSongPtrs.push_back(s.get());
        songsCollection_ = std::unique_ptr<SongCollection>(new SongCollection(allSongPtrs));

        // --- Group songs by genre / artist / (artist,album), preserving first-seen order (the
        // scan itself is already deterministic -- MediaLibraryIndex sorts directory entries). ---
        std::vector<std::string> genreOrder;
        std::unordered_map<std::string, std::vector<Song*>> songsByGenre;
        std::vector<std::string> artistOrder;
        std::unordered_map<std::string, std::vector<Song*>> songsByArtist;
        std::vector<std::pair<std::string, std::string>> albumOrder; // (artist, album)
        std::map<std::pair<std::string, std::string>, std::vector<Song*>> songsByAlbumKey;
        std::map<std::pair<std::string, std::string>, std::string> albumArtPathByKey;
        std::map<std::pair<std::string, std::string>, std::string> albumGenreByKey;

        for (const auto& indexed : musicIndex.GetSongs())
        {
            Song* song = songByPath_[indexed.path];

            if (!indexed.genre.empty())
            {
                if (songsByGenre.find(indexed.genre) == songsByGenre.end())
                {
                    genreOrder.push_back(indexed.genre);
                }
                songsByGenre[indexed.genre].push_back(song);
            }

            if (indexed.artist.empty()) continue;

            if (songsByArtist.find(indexed.artist) == songsByArtist.end())
            {
                artistOrder.push_back(indexed.artist);
            }
            songsByArtist[indexed.artist].push_back(song);

            if (indexed.album.empty()) continue;

            auto key = std::make_pair(indexed.artist, indexed.album);
            if (songsByAlbumKey.find(key) == songsByAlbumKey.end())
            {
                albumOrder.push_back(key);
                albumArtPathByKey[key] = FindAlbumArtPath(indexed.path);
                albumGenreByKey[key] = indexed.genre; // first song's genre represents the album
            }
            songsByAlbumKey[key].push_back(song);
        }

        // --- Genres (AlbumCollection patched in after Albums are built, below) ---
        std::unordered_map<std::string, Genre*> genreByName;
        std::vector<Genre*> allGenres;
        for (const auto& name : genreOrder)
        {
            auto genreSongs = std::make_unique<SongCollection>(songsByGenre[name]);
            std::unique_ptr<Genre> genre(new Genre(name, nullptr, genreSongs.get()));
            ownedGroupSongCollections_.push_back(std::move(genreSongs));
            genreByName[name] = genre.get();
            allGenres.push_back(genre.get());
            ownedGenres_.push_back(std::move(genre));
        }
        genresCollection_ = std::unique_ptr<GenreCollection>(new GenreCollection(allGenres));

        // --- Artists (AlbumCollection patched in after Albums are built, below) ---
        std::unordered_map<std::string, Artist*> artistByName;
        std::vector<Artist*> allArtists;
        for (const auto& name : artistOrder)
        {
            auto artistSongs = std::make_unique<SongCollection>(songsByArtist[name]);
            std::unique_ptr<Artist> artist(new Artist(name, nullptr, artistSongs.get()));
            ownedGroupSongCollections_.push_back(std::move(artistSongs));
            artistByName[name] = artist.get();
            allArtists.push_back(artist.get());
            ownedArtists_.push_back(std::move(artist));
        }
        artistsCollection_ = std::unique_ptr<ArtistCollection>(new ArtistCollection(allArtists));

        // --- Albums ---
        std::unordered_map<std::string, std::vector<Album*>> albumsByArtistName;
        std::unordered_map<std::string, std::vector<Album*>> albumsByGenreName;
        std::map<std::pair<std::string, std::string>, Album*> albumByKey; // for Song::Album back-refs
        std::vector<Album*> allAlbums;
        for (const auto& key : albumOrder)
        {
            const std::string& artistName = key.first;
            const std::string& albumName = key.second;
            Artist* artist = artistByName[artistName];
            std::string genreName = albumGenreByKey[key];
            Genre* genre = genreName.empty() ? nullptr : genreByName[genreName];

            // plans/plan_media.md MEDIA-65: Duration is the real sum of member Song.Duration (now
            // populated for real by AudioDurationProbe above), not a hardcoded zero.
            System::TimeSpan albumDuration = System::TimeSpan::Zero;
            for (Song* memberSong : songsByAlbumKey[key])
            {
                albumDuration = albumDuration + memberSong->getDurationProperty();
            }

            auto albumSongs = std::make_unique<SongCollection>(songsByAlbumKey[key]);
            std::unique_ptr<Album> album(new Album(
                albumName, artist, genre, albumDuration,
                albumArtPathByKey[key], albumSongs.get()));
            ownedGroupSongCollections_.push_back(std::move(albumSongs));
            allAlbums.push_back(album.get());
            albumByKey[key] = album.get();

            // No folder image for this album -- fall back to embedded art from the first member
            // song that actually has some. Only the PATH is stored; the image is extracted on
            // demand so a big library doesn't hold every cover in memory (plans/plan_media.md
            // MEDIA-206/207/208).
            if (albumArtPathByKey[key].empty())
            {
                for (Song* memberSong : songsByAlbumKey[key])
                {
                    std::vector<uint8_t> probe;
                    if (CNA::Internal::Media::AudioTagParser::ExtractEmbeddedArt(
                            memberSong->getHandle(), probe) && !probe.empty())
                    {
                        albumByKey[key]->embeddedArtSourcePath_ = memberSong->getHandle();
                        break;
                    }
                }
            }
            albumsByArtistName[artistName].push_back(album.get());
            if (!genreName.empty()) albumsByGenreName[genreName].push_back(album.get());
            ownedAlbums_.push_back(std::move(album));
        }
        albumsCollection_ = std::unique_ptr<AlbumCollection>(new AlbumCollection(allAlbums));

        // Patch Genre::Albums / Artist::Albums now that Album objects exist.
        for (const auto& name : genreOrder)
        {
            auto* collection = new AlbumCollection(albumsByGenreName[name]);
            genreByName[name]->SetAlbums(collection);
            ownedGroupAlbumCollections_.emplace_back(collection);
        }
        for (const auto& name : artistOrder)
        {
            auto* collection = new AlbumCollection(albumsByArtistName[name]);
            artistByName[name]->SetAlbums(collection);
            ownedGroupAlbumCollections_.emplace_back(collection);
        }

        // --- Song -> Album/Artist/Genre back-references (plans/plan_media.md MEDIA-177) ---
        // XNA's Song exposes Album/Artist/Genre, but CNA had none of those members at all until
        // MEDIA-174 (FNA omits them too, which is why every earlier audit against FNA missed it).
        // Populated in a dedicated final pass, after every group object exists, so the pointers
        // written here are the same ones getAlbumsProperty()/getArtistsProperty()/
        // getGenresProperty() hand out. Storing raw pointers is safe across the owning vectors'
        // reallocation: ownedAlbums_/ownedArtists_/ownedGenres_ hold unique_ptrs, so growing the
        // vector moves the smart pointers but never the heap objects they point at.
        for (const auto& indexed : musicIndex.GetSongs())
        {
            Song* song = songByPath_[indexed.path];
            if (song == nullptr) continue;

            // MediaLibraryIndex already parsed this from the file's own tags; it just was never
            // handed to Song, so getTrackNumberProperty() returned a hardcoded 0 for every library
            // song -- real data parsed and then dropped (plans/plan_media.md MEDIA-181).
            song->trackNumber_ = static_cast<SharpRuntime::intcs>(indexed.trackNumber);
            song->rating_      = static_cast<SharpRuntime::intcs>(indexed.rating);
            song->isRated_     = indexed.hasRating;

            if (!indexed.genre.empty())
            {
                auto it = genreByName.find(indexed.genre);
                if (it != genreByName.end()) song->genre_ = it->second;
            }
            if (!indexed.artist.empty())
            {
                auto it = artistByName.find(indexed.artist);
                if (it != artistByName.end()) song->artist_ = it->second;
            }
            if (!indexed.artist.empty() && !indexed.album.empty())
            {
                auto it = albumByKey.find(std::make_pair(indexed.artist, indexed.album));
                if (it != albumByKey.end()) song->album_ = it->second;
            }
        }

        // --- Playlists ---
        std::vector<Playlist*> allPlaylists;
        for (const auto& parsed : CNA::Internal::Media::PlaylistParser::ScanDirectory(musicRoot))
        {
            std::vector<Song*> members;
            System::TimeSpan duration = System::TimeSpan::Zero;
            for (const auto& path : parsed.songPaths)
            {
                auto it = songByPath_.find(path);
                if (it != songByPath_.end())
                {
                    members.push_back(it->second);
                    duration = duration + it->second->getDurationProperty();
                }
            }
            auto playlistSongs = std::make_unique<SongCollection>(members);
            std::unique_ptr<Playlist> playlist(new Playlist(parsed.name, playlistSongs.get(), duration));
            ownedGroupSongCollections_.push_back(std::move(playlistSongs));
            allPlaylists.push_back(playlist.get());
            ownedPlaylists_.push_back(std::move(playlist));
        }
        playlistsCollection_ = std::unique_ptr<PlaylistCollection>(new PlaylistCollection(allPlaylists));

        // --- Pictures / PictureAlbum tree ---
        // Deliberately does NOT create the "Saved Pictures" directory here -- merely constructing
        // (or even just browsing) a MediaLibrary should not have the surprising side effect of
        // creating a new directory on disk; that only happens lazily, the first time SavePicture()
        // is actually called. If a Saved Pictures folder already exists from a previous session,
        // the scan below picks it up like any other real subdirectory.
        std::string savedDirIfExists;
        {
            std::filesystem::path candidate = std::filesystem::path(pictureRoot) / "Saved Pictures";
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                savedDirIfExists = candidate.string();
            }
        }

        CNA::Internal::Media::PictureLibraryIndex pictureIndex(pictureRoot);
        if (!pictureIndex.GetRootAlbumPath().empty())
        {
            rootPictureAlbum_ = BuildPictureAlbumTree(pictureIndex, pictureIndex.GetRootAlbumPath(), nullptr);
        }
        std::vector<Picture*> allPictures;
        for (auto& p : ownedPictures_) allPictures.push_back(p.get());
        picturesCollection_ = std::unique_ptr<PictureCollection>(new PictureCollection(allPictures));

        // --- SavedPictures: real files already present under Pictures/Saved Pictures (if any,
        // e.g. left over from a previous run) -- the PictureLibraryIndex scan above already
        // picked these up like any other picture; identify them via the real PictureAlbum tree
        // node for that directory. ---
        std::vector<Picture*> savedPictures;
        if (!savedDirIfExists.empty())
        {
            std::error_code ec;
            std::string canonicalSavedDir = std::filesystem::weakly_canonical(savedDirIfExists, ec).string();
            if (!ec)
            {
                auto it = pictureAlbumByPath_.find(canonicalSavedDir);
                if (it != pictureAlbumByPath_.end())
                {
                    savedPicturesAlbum_ = it->second;
                    for (Picture* p : *savedPicturesAlbum_->getPicturesProperty())
                    {
                        savedPictures.push_back(p);
                    }
                }
            }
        }
        savedPicturesCollection_ = std::unique_ptr<PictureCollection>(new PictureCollection(savedPictures));
    }

    PictureAlbum* MediaLibrary::BuildPictureAlbumTree(
        const CNA::Internal::Media::PictureLibraryIndex& pictureIndex,
        const std::string& nodePath, PictureAlbum* parent)
    {
        const auto& node = pictureIndex.GetAlbums().at(nodePath);

        std::unique_ptr<PictureAlbum> album(new PictureAlbum(node.name, parent, node.path));
        PictureAlbum* raw = album.get();
        ownedPictureAlbums_.push_back(std::move(album));

        std::vector<PictureAlbum*> childPtrs;
        for (const auto& childPath : node.childPaths)
        {
            childPtrs.push_back(BuildPictureAlbumTree(pictureIndex, childPath, raw));
        }

        std::vector<Picture*> picturePtrs;
        for (auto idx : node.pictureIndices)
        {
            const auto& ip = pictureIndex.GetPictures()[idx];
            std::unique_ptr<Picture> pic(new Picture(ip.name, raw, ip.width, ip.height, ip.date, ip.path));
            picturePtrs.push_back(pic.get());
            ownedPictures_.push_back(std::move(pic));
        }

        std::unique_ptr<PictureAlbumCollection> childCollection(new PictureAlbumCollection(childPtrs));
        std::unique_ptr<PictureCollection> pictureCollection(new PictureCollection(picturePtrs));
        raw->SetChildAlbumsAndPictures(childCollection.get(), pictureCollection.get());
        ownedPictureAlbumChildCollections_.push_back(std::move(childCollection));
        ownedPictureAlbumPictureCollections_.push_back(std::move(pictureCollection));

        pictureAlbumByPath_[nodePath] = raw;
        return raw;
    }

    void MediaLibrary::Dispose()
    {
        isDisposed_ = true;
    }

    AlbumCollection* MediaLibrary::getAlbumsProperty() const
    {
        return albumsCollection_.get();
    }

    ArtistCollection* MediaLibrary::getArtistsProperty() const
    {
        return artistsCollection_.get();
    }

    GenreCollection* MediaLibrary::getGenresProperty() const
    {
        return genresCollection_.get();
    }

    bool MediaLibrary::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    MediaSource* MediaLibrary::getMediaSourceProperty() const
    {
        return mediaSource_.get();
    }

    PictureCollection* MediaLibrary::getPicturesProperty() const
    {
        return picturesCollection_.get();
    }

    PlaylistCollection* MediaLibrary::getPlaylistsProperty() const
    {
        return playlistsCollection_.get();
    }

    PictureAlbum* MediaLibrary::getRootPictureAlbumProperty() const
    {
        return rootPictureAlbum_;
    }

    PictureCollection* MediaLibrary::getSavedPicturesProperty() const
    {
        return savedPicturesCollection_.get();
    }

    SongCollection* MediaLibrary::getSongsProperty() const
    {
        return songsCollection_.get();
    }

    Picture* MediaLibrary::GetPictureFromToken(std::string token)
    {
        // No real OS media-library token system exists to reference -- a Picture's own resolved
        // file path (Picture::getTokenEXT()) is used as its token, a simple, real, stable
        // identifier (plans/plan_media.md MEDIA-62; no FNA logic to port here, see plan §0).
        for (auto& p : ownedPictures_)
        {
            if (p->getTokenEXT() == token)
            {
                return p.get();
            }
        }
        return nullptr;
    }

    PictureAlbum* MediaLibrary::EnsureSavedPicturesAlbum()
    {
        if (savedPicturesAlbum_ != nullptr)
        {
            return savedPicturesAlbum_;
        }
        if (rootPictureAlbum_ == nullptr)
        {
            // The Pictures root itself didn't exist (or was inaccessible) at MediaLibrary
            // construction time, so PictureLibraryIndex never found a root to build a tree from
            // -- but SavedPictureStore::SavePicture() (called by our own caller, SavePicture(),
            // just before this) always creates the real directory on disk regardless. Bootstrap a
            // real root node now too, the same lazy-creation pattern this whole function already
            // uses for "Saved Pictures" below, instead of leaving the saved Picture unparented in
            // any tree at all (found by external code review, plans/plan_media.md MEDIA-132).
            if (pictureRoot_.empty())
            {
                return nullptr; // no pictures root configured at all -- genuinely nowhere to attach
            }
            std::error_code rootEc;
            std::filesystem::path rootPath = std::filesystem::path(pictureRoot_);
            std::string canonicalRootPath = std::filesystem::weakly_canonical(rootPath, rootEc).string();
            if (rootEc || canonicalRootPath.empty())
            {
                canonicalRootPath = rootPath.string(); // best-effort fallback if canonicalization fails
            }

            std::unique_ptr<PictureAlbum> rootAlbum(
                new PictureAlbum(rootPath.filename().string(), nullptr, canonicalRootPath));
            PictureAlbum* rawRoot = rootAlbum.get();
            ownedPictureAlbums_.push_back(std::move(rootAlbum));

            std::unique_ptr<PictureAlbumCollection> rootChildAlbums(new PictureAlbumCollection({}));
            std::unique_ptr<PictureCollection> rootPictures(new PictureCollection({}));
            rawRoot->SetChildAlbumsAndPictures(rootChildAlbums.get(), rootPictures.get());
            ownedPictureAlbumChildCollections_.push_back(std::move(rootChildAlbums));
            ownedPictureAlbumPictureCollections_.push_back(std::move(rootPictures));

            rootPictureAlbum_ = rawRoot;
            pictureAlbumByPath_[canonicalRootPath] = rawRoot;
        }

        std::error_code ec;
        std::string path = (std::filesystem::path(pictureRoot_) / "Saved Pictures").string();
        std::unique_ptr<PictureAlbum> album(new PictureAlbum("Saved Pictures", rootPictureAlbum_, path));
        PictureAlbum* raw = album.get();
        ownedPictureAlbums_.push_back(std::move(album));

        std::unique_ptr<PictureAlbumCollection> emptyChildAlbums(new PictureAlbumCollection({}));
        std::unique_ptr<PictureCollection> emptyPictures(new PictureCollection({}));
        raw->SetChildAlbumsAndPictures(emptyChildAlbums.get(), emptyPictures.get());
        ownedPictureAlbumChildCollections_.push_back(std::move(emptyChildAlbums));
        ownedPictureAlbumPictureCollections_.push_back(std::move(emptyPictures));

        // Register as a real child of the root album -- MediaLibrary is a friend of
        // PictureAlbumCollection, so base_ (the underlying MediaCollectionBase<PictureAlbum>) is
        // directly reachable for the runtime append the read-only public API doesn't expose (same
        // pattern SavePicture() itself already uses for PictureCollection below).
        if (rootPictureAlbum_->getAlbumsProperty() != nullptr)
        {
            rootPictureAlbum_->getAlbumsProperty()->base_.Add(raw);
        }

        savedPicturesAlbum_ = raw;
        return raw;
    }

    Picture* MediaLibrary::SavePicture(std::string name, const std::vector<uint8_t>& imageBuffer)
    {
        std::string savedPath = CNA::Internal::Media::SavedPictureStore::SavePicture(
            pictureRoot_, name, imageBuffer);
        if (savedPath.empty())
        {
            throw System::IO::IOException("Failed to save picture '" + name + "'.");
        }

        CNA::Internal::Graphics::ImageData img;
        try
        {
            img = CNA::Internal::Graphics::ImageLoader::Load(savedPath);
        }
        catch (const std::exception&)
        {
            img.width = 0;
            img.height = 0;
        }

        auto now = std::chrono::system_clock::now();
        PictureAlbum* parentAlbum = EnsureSavedPicturesAlbum();
        std::unique_ptr<Picture> pic(new Picture(name, parentAlbum, img.width, img.height, now, savedPath));
        Picture* raw = pic.get();
        ownedPictures_.push_back(std::move(pic));

        // Grow every collection this new picture is genuinely a member of. MediaLibrary is a
        // friend of PictureCollection, so base_ (the underlying MediaCollectionBase<Picture>) is
        // directly reachable here for the runtime append the read-only public API doesn't expose.
        picturesCollection_->base_.Add(raw);
        savedPicturesCollection_->base_.Add(raw);
        if (parentAlbum != nullptr && parentAlbum->getPicturesProperty() != nullptr)
        {
            parentAlbum->getPicturesProperty()->base_.Add(raw);
        }

        return raw;
    }

    Picture* MediaLibrary::SavePicture(std::string name, System::IO::Stream* source)
    {
        if (source == nullptr)
        {
            throw System::ArgumentNullException("source");
        }
        std::vector<uint8_t> buffer(static_cast<std::size_t>(source->getLengthProperty()));
        if (!buffer.empty())
        {
            source->Read(buffer.data(), 0, static_cast<SharpRuntime::intcs>(buffer.size()));
        }
        return SavePicture(std::move(name), buffer);
    }

    const std::string& MediaLibrary::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.MediaLibrary";
        return typeName;
    }
}
