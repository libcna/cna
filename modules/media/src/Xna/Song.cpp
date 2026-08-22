// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Song.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    Song::Song(std::string fileName, std::string name)
        : name_(std::move(name)),
          duration_(System::TimeSpan::Zero),
          playCount_(0),
          isDisposed_(false),
          handle_(std::move(fileName))
    {
        // FNA's ctor throws FileNotFoundException(fileName) directly (Song.cs); match the
        // established CNA-wide convention (SoundBank/WaveBank) of a descriptive message plus the
        // path via getFileNameProperty(), rather than a bare std::runtime_error(handle_).
        if (!std::filesystem::exists(handle_))
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
        Dispose();
    }

    const std::string& Song::getNameProperty() const
    {
        return name_;
    }

    Album* Song::getAlbumProperty() const
    {
        return album_;
    }

    Artist* Song::getArtistProperty() const
    {
        return artist_;
    }

    Genre* Song::getGenreProperty() const
    {
        return genre_;
    }

    System::TimeSpan Song::getDurationProperty() const
    {
        return duration_;
    }

    void Song::setDurationProperty(System::TimeSpan value)
    {
        duration_ = value;
    }

    bool Song::getIsProtectedProperty() const
    {
        return false;
    }

    bool Song::getIsRatedProperty() const
    {
        return isRated_;
    }

    SharpRuntime::intcs Song::getPlayCountProperty() const
    {
        return playCount_;
    }

    void Song::setPlayCountProperty(SharpRuntime::intcs value)
    {
        playCount_ = value;
    }

    SharpRuntime::intcs Song::getRatingProperty() const
    {
        return rating_;
    }

    SharpRuntime::intcs Song::getTrackNumberProperty() const
    {
        return trackNumber_;
    }

    bool Song::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    void Song::Dispose()
    {
        isDisposed_ = true;
    }

    bool Song::Equals(const Song* song) const
    {
        return song != nullptr && handle_ == song->handle_;
    }

    bool Song::Equals(const System::Object* obj) const
    {
        return Equals(dynamic_cast<const Song*>(obj));
    }

    int Song::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(handle_));
    }

    std::string Song::ToString() const
    {
        // The XNA reference documents only "Returns a String representation of the Song" without
        // specifying the string. Returning the name matches what Album/Artist/Genre/Playlist in
        // this same namespace already do (each returns its own name_), so Song follows the
        // established convention rather than inventing a different format -- plans/plan_media.md
        // MEDIA-176. This is a documented inference from sibling types, NOT a behavior verified
        // against a decompiled XNA binary.
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

    Song* Song::FromUri(const std::string& name, const std::string& uri)
    {
        // FNA resolves the URI via Uri.LocalPath and rejects anything that is not a local file
        // ("Only local file URIs are supported for now" -- Song.cs). CNA takes a std::string rather
        // than a System::Uri, so the equivalent parsing happens here: the raw string used to go
        // straight to the Song constructor, which then asked std::filesystem::exists about a
        // literal "file:///..." and always failed (plans/plan_media.md MEDIA-217/219).
        const std::size_t colon = uri.find(':');

        // No scheme at all: a plain relative or absolute path, used as-is. (FNA combines a relative
        // URI with TitleLocation.Path; CNA has no such content-root concept for Song, and every
        // existing caller already passes a usable path.) A bare Windows drive letter like "C:/x" is
        // a path, not a scheme -- a single-character "scheme" is never a real one.
        if (colon == std::string::npos || colon <= 1)
        {
            return new Song(uri, name);
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
        // was never asked for (plans/plan_media.md MEDIA-223).
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
            // did) would silently resolve a REMOTE path to a local one (plans/plan_media.md MEDIA-219).
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
            // version of this fix did not recognise at all (plans/plan_media.md MEDIA-219).
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

        return new Song(path, name);
    }

    const std::string& Song::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Song";
        return typeName;
    }

    bool operator==(const Song& song1, const Song& song2)
    {
        return song1.Equals(&song2);
    }

    bool operator!=(const Song& song1, const Song& song2)
    {
        return !(song1 == song2);
    }
}
