// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>
#include <vector>

#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/Media/MediaObjectDisposal.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/IO/File.hpp"

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

    // XNA's Song installs Artist.Empty, Album.Empty and Genre.Empty in every constructor (URI
    // .ctor IL_0019-0035), and a relationship getter returns that sentinel unless native metadata
    // replaces it (IL_002a-0048): Album, Artist and Genre are never null (owner decision
    // 2026-11-12, superseding MEDIA-174). The sentinels are named, empty and shared, as XNA's
    // statics are (Artist/Album/Genre .cctor; their collections are SongCollection.Empty and
    // AlbumCollection.Empty).
    namespace
    {
        SongCollection& EmptySongs()
        {
            static SongCollection songs{std::vector<Song*>{}};
            return songs;
        }
    }
    AlbumCollection& Song::EmptyAlbumsInternal()
    {
        static AlbumCollection albums{std::vector<Album*>{}};
        return albums;
    }
    Artist& Song::EmptyArtistInternal()
    {
        static Artist artist{std::string(), &EmptyAlbumsInternal(), &EmptySongs()};
        return artist;
    }
    Genre& Song::EmptyGenreInternal()
    {
        static Genre genre{std::string(), &EmptyAlbumsInternal(), &EmptySongs()};
        return genre;
    }
    Album& Song::EmptyAlbumInternal()
    {
        static Album album{std::string(), &EmptyArtistInternal(), &EmptyGenreInternal(),
                           System::TimeSpan::Zero, std::string(), &EmptySongs()};
        return album;
    }

    Song::Song(std::string fileName, std::string name)
        : name_(std::move(name)),
          duration_(System::TimeSpan::Zero),
          playCount_(0),
          isDisposed_(false),
          handle_(std::move(fileName))
    {
        // File.Exists rejects directories and embedded NUL, including a URI's %00.
        // FNA Song.cs:93..96; preserve the requested path in FileNotFoundException.
        if (!System::IO::File::Exists(handle_))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + handle_ + "'.", handle_);
        }
    }

    Song::Song(std::string fileName, std::string assetName, SharpRuntime::intcs durationMS)
        : Song(std::move(fileName), std::move(assetName))
    {
        duration_ = System::TimeSpan::FromMilliseconds(durationMS);
    }

    Song::~Song()
    {
        contentOwnership_.Detach();
        Dispose();
    }

    Song::Song(const Song& other)
        : System::Object(other), album_(other.album_), artist_(other.artist_), genre_(other.genre_),
          trackNumber_(other.trackNumber_), rating_(other.rating_), isRated_(other.isRated_),
          name_(other.name_), duration_(other.duration_), playCount_(other.playCount_),
          isDisposed_(other.isDisposed_), handle_(other.handle_),
          contentOwnership_(this, other.contentOwnership_) { }

    Song& Song::operator=(const Song& other)
    {
        if (this != &other)
        {
            System::Object::operator=(other);
            album_ = other.album_;
            artist_ = other.artist_;
            genre_ = other.genre_;
            trackNumber_ = other.trackNumber_;
            rating_ = other.rating_;
            isRated_ = other.isRated_;
            name_ = other.name_;
            duration_ = other.duration_;
            playCount_ = other.playCount_;
            isDisposed_ = other.isDisposed_;
            handle_ = other.handle_;
            contentOwnership_ = other.contentOwnership_;
        }
        return *this;
    }

    std::shared_ptr<System::IDisposable> Song::GetContentDisposalRecord() const
    {
        if (auto record = contentOwnership_.Record()) return record;
        auto representative = std::make_shared<Song>(*this);
        return contentOwnership_.Capture(representative, representative->contentOwnership_);
    }

    const std::string& Song::getNameProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return name_;
    }

    Album* Song::getAlbumProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return album_ != nullptr ? album_ : &EmptyAlbumInternal();
    }

    Artist* Song::getArtistProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return artist_ != nullptr ? artist_ : &EmptyArtistInternal();
    }

    Genre* Song::getGenreProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return genre_ != nullptr ? genre_ : &EmptyGenreInternal();
    }

    System::TimeSpan Song::getDurationProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return duration_;
    }

    void Song::setDurationProperty(System::TimeSpan value)
    {
        duration_ = value;
    }

    bool Song::getIsProtectedProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return false;
    }

    bool Song::getIsRatedProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return isRated_;
    }

    SharpRuntime::intcs Song::getPlayCountProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return playCount_;
    }

    void Song::setPlayCountProperty(SharpRuntime::intcs value)
    {
        playCount_ = value;
    }

    SharpRuntime::intcs Song::getRatingProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return rating_;
    }

    SharpRuntime::intcs Song::getTrackNumberProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return trackNumber_;
    }

    bool Song::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    void Song::Dispose()
    {
        if (contentOwnership_.DisposeRepresentative()) return;
        isDisposed_ = true;
        contentOwnership_.MarkDisposed();
    }

    bool Song::Equals(const Song* song) const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        // A disposed Song has released its handle, so it equals only itself, as in XNA.
        return song == this || (song != nullptr && !song->isDisposed_ && handle_ == song->handle_);
    }

    bool Song::Equals(const System::Object* obj) const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return Equals(dynamic_cast<const Song*>(obj));
    }

    int Song::GetHashCode() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        return static_cast<int>(std::hash<std::string>{}(handle_));
    }

    std::string Song::ToString() const
    {
        ThrowIfDisposed(isDisposed_, "Song");
        // XNA's Song.ToString returns Name after its disposal check (Microsoft.Xna.Framework.Media
        // .Song in the decompiled Windows assembly), as Album/Artist/Genre/Playlist do.
        return name_;
    }

    const std::string& Song::getHandle() const
    {
        return handle_;
    }

    namespace
    {
        // Percent-decodes a URI path component ("My%20Song.ogg" -> "My Song.ogg"). Invalid escapes
        // are passed through literally rather than dropped, so a malformed URI degrades to a
        // path-not-found error instead of silently resolving to a different file.
        std::string PercentDecode(const std::string& in)
        {
            std::string out;
            out.reserve(in.size());
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                if (in[i] == '%' && i + 2 < in.size() &&
                    std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(in[i + 2])))
                {
                    out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
                    i += 2;
                }
                else
                {
                    out.push_back(in[i]);
                }
            }
            return out;
        }
    }

    Song* Song::FromUri(const std::string& name, const System::Uri& uri)
    {
        // XNA hands the Uri object to the native media stack; FNA reduces it to Uri.LocalPath and
        // rejects anything that is not a local file. The string overload below already performs
        // exactly that reduction -- scheme check, query/fragment removal, percent-decoding -- so
        // the typed overload contributes the documented parameter type and nothing else. Passing
        // OriginalString rather than AbsoluteUri keeps a relative URI usable, which is the one
        // case AbsoluteUri refuses to produce.
        return FromUri(name, uri.getOriginalStringProperty());
    }

    Song* Song::FromUri(const std::string& name, const std::string& uri)
    {
        // XNA Song..ctor(string,Uri) IL_0059..0079 validates name before URI resolution.
        if (name.empty())
        {
            throw System::ArgumentOutOfRangeException("name");
        }
        // Refresh Song::.ctor(string,Uri) IL_0045..004e retains -1 when native
        // rating/track metadata is unavailable. File constructors keep their zero
        // values: .ctor(string,string,int) explicitly sets them at IL_0060..0069.
        const auto fromResolvedPath = [&name](const std::string& path)
        {
            auto* song = new Song(path, name);
            song->rating_ = -1;
            song->trackNumber_ = -1;
            return song;
        };
        // FNA resolves the URI via Uri.LocalPath and rejects anything that is not a local file
        // ("Only local file URIs are supported for now" -- Song.cs). CNA takes a std::string rather
        // than a System::Uri, so the equivalent parsing happens here: the raw string used to go
        // straight to the Song constructor, which then asked std::filesystem::exists about a
        // literal "file:///..." and always failed (MEDIA-217/219).
        const std::size_t colon = uri.find(':');

        // A drive letter is a path, not a URI scheme. Absolute paths keep their existing spelling.
        if (colon == std::string::npos || colon <= 1)
        {
            const auto localPath = CNA::Internal::TryPathFromUtf8(uri);
            if (!uri.empty() && colon == std::string::npos && localPath && localPath->is_relative())
            {
                // XNA's MediaSong_FromUri combines a relative URI with TitleLocation.Path.
                // Use the lower-layer bridge so media does not depend on the runtime module.
                const auto configuredTitle = CNA::Internal::TryGetTitlePath();
                std::string title = configuredTitle ? *configuredTitle :
                    CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
                if (!configuredTitle && title.empty())
                {
                    title = CNA::Internal::PathToGenericUtf8(std::filesystem::current_path());
                }
                return fromResolvedPath(CNA::Internal::PathToGenericUtf8(
                    CNA::Internal::PathFromUtf8(title) / *localPath));
            }
            return fromResolvedPath(uri);
        }

        std::string scheme = uri.substr(0, colon);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (scheme != "file")
        {
            throw System::InvalidOperationException(
                "Only local file URIs are supported for now.");
        }

        std::string rest = uri.substr(colon + 1);

        // Strip the query and fragment: System.Uri.LocalPath returns ONLY the path component, so
        // "file:///music/song.mp3?v=1#intro" resolves to "/music/song.mp3". Keeping them made the
        // path unopenable and produced a misleading FileNotFoundException naming a filename that
        // was never asked for (MEDIA-223).
        //
        // Done BEFORE percent-decoding, deliberately: a '?' or '#' that is percent-encoded
        // (%3F / %23) is a LITERAL character in the filename, not a delimiter, and decoding first
        // would truncate the path at it.
        const std::size_t frag = rest.find('#');
        if (frag != std::string::npos) rest.erase(frag);
        const std::size_t query = rest.find('?');
        if (query != std::string::npos) rest.erase(query);

        std::string path;

        if (rest.rfind("//", 0) == 0)
        {
            // "file://<authority>/<path>". RFC 8089 allows a non-empty authority, which .NET's
            // Uri.LocalPath turns into a UNC path -- dropping it (as the first version of this fix
            // did) would silently resolve a REMOTE path to a local one (MEDIA-219).
            rest = rest.substr(2);
            const std::size_t slash = rest.find('/');
            const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
            const std::string rawPath   = (slash == std::string::npos) ? std::string() : rest.substr(slash);

            std::string lowerAuthority = authority;
            std::transform(lowerAuthority.begin(), lowerAuthority.end(), lowerAuthority.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (authority.empty() || lowerAuthority == "localhost")
            {
                // The two spellings of "this machine": "file:///path" and "file://localhost/path".
                if (rawPath.empty())
                {
                    throw System::InvalidOperationException(
                        "Only local file URIs are supported for now.");
                }
                path = PercentDecode(rawPath);
            }
            else
            {
                // A real host: becomes a UNC path, matching Uri.LocalPath. Kept rather than
                // rejected so the caller gets a genuine path-not-found if the share is unreachable,
                // instead of this code silently pretending it was local.
                path = "//" + PercentDecode(authority) + PercentDecode(rawPath);
            }
        }
        else if (rest.rfind("/", 0) == 0)
        {
            // "file:/path" -- the authority-less form RFC 8089 also permits, which the first
            // version of this fix did not recognise at all (MEDIA-219).
            path = PercentDecode(rest);
        }
        else
        {
            throw System::InvalidOperationException(
                "Only local file URIs are supported for now.");
        }

        // "file:///C:/music/song.mp3" -> "C:/music/song.mp3" for Windows-style absolute paths.
        if (path.size() >= 3 && path[0] == '/' &&
            std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
        {
            path.erase(0, 1);
        }

        return fromResolvedPath(path);
    }

    const std::string& Song::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Song";
        return typeName;
    }

    bool operator==(const Song& song1, const Song& song2)
    {
        // object.Equals(first, second): the same object is equal without asking it anything.
        return &song1 == &song2 || song1.Equals(&song2);
    }

    bool operator!=(const Song& song1, const Song& song2)
    {
        return !(song1 == song2);
    }
}
