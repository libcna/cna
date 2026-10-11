// SPDX-License-Identifier: MS-PL

#include "CNA/C/media_library.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMediaDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiStorageDetail.hpp"

#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Playlist.hpp"
#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/IO/Stream.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Media::Detail::BorrowMediaChild;
using CNA::C::Media::Detail::MediaChildResource;
using CNA::C::Media::Detail::MediaLibraryResource;
using CNA::C::Media::Detail::PublishLibraryChild;
using CNA::C::Media::Detail::SongCollectionResource;
using CNA::C::Media::Detail::SongResource;

namespace {

using CNA::C::Detail::CopyBytes;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToCBool;

using Microsoft::Xna::Framework::Media::Album;
using Microsoft::Xna::Framework::Media::AlbumCollection;
using Microsoft::Xna::Framework::Media::Artist;
using Microsoft::Xna::Framework::Media::ArtistCollection;
using Microsoft::Xna::Framework::Media::Genre;
using Microsoft::Xna::Framework::Media::GenreCollection;
using Microsoft::Xna::Framework::Media::MediaLibrary;
using Microsoft::Xna::Framework::Media::MediaSource;
using Microsoft::Xna::Framework::Media::Picture;
using Microsoft::Xna::Framework::Media::PictureAlbum;
using Microsoft::Xna::Framework::Media::PictureAlbumCollection;
using Microsoft::Xna::Framework::Media::PictureCollection;
using Microsoft::Xna::Framework::Media::Playlist;
using Microsoft::Xna::Framework::Media::PlaylistCollection;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;

// One trait per mapped canonical type, so every route below is one line of type-specific code over
// the same borrow/publish machinery rather than four near-identical copies.
template<typename TValue>
struct MediaTraits;

#define CNA_C_API_MEDIA_TRAITS(TYPE, KIND, MESSAGE)                                              \
    template<>                                                                                   \
    struct MediaTraits<TYPE> {                                                                   \
        static constexpr ObjectKind Kind = ObjectKind::KIND;                                     \
        [[maybe_unused]] static constexpr const char* InvalidHandleMessage = MESSAGE;            \
    }

CNA_C_API_MEDIA_TRAITS(Album, Album, "The album handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(AlbumCollection, AlbumCollection,
                       "The album collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(Artist, Artist, "The artist handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(ArtistCollection, ArtistCollection,
                       "The artist collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(Genre, Genre, "The genre handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(GenreCollection, GenreCollection,
                       "The genre collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(Playlist, Playlist, "The playlist handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(PlaylistCollection, PlaylistCollection,
                       "The playlist collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(Song, Song, "The song handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(SongCollection, SongCollection,
                       "The song collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(Picture, Picture, "The picture handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(PictureCollection, PictureCollection,
                       "The picture collection handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(PictureAlbum, PictureAlbum,
                       "The picture album handle is invalid for this call.");
CNA_C_API_MEDIA_TRAITS(PictureAlbumCollection, PictureAlbumCollection,
                       "The picture album collection handle is invalid for this call.");

#undef CNA_C_API_MEDIA_TRAITS

template<typename TValue>
[[nodiscard]] CNA_Result Borrow(
    const CNA_Handle handle,
    std::shared_ptr<MediaChildResource<TValue>>* const outResource)
{
    return BorrowMediaChild<TValue>(
        handle,
        MediaTraits<TValue>::Kind,
        MediaTraits<TValue>::InvalidHandleMessage,
        outResource);
}

template<typename TValue, typename TRead>
[[nodiscard]] CNA_Result Read(const CNA_Handle handle, const TRead read)
{
    std::shared_ptr<MediaChildResource<TValue>> resource;
    if (const CNA_Result result = Borrow<TValue>(handle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return read(*resource->value);
}

// Publishes a borrowed view of a library-owned object, keeping the owning library alive.
template<typename TValue, typename TParent>
[[nodiscard]] CNA_Result PublishChildOf(
    const std::shared_ptr<MediaChildResource<TParent>>& parent,
    TValue* const value,
    CNA_Handle* const outHandle)
{
    return PublishLibraryChild<TValue>(
        MediaTraits<TValue>::Kind,
        parent->library,
        value,
        "The media library child handle could not be created.",
        outHandle);
}

template<typename TValue>
[[nodiscard]] CNA_Result BorrowLibrary(
    const CNA_MediaLibraryHandle handle,
    std::shared_ptr<MediaLibraryResource>* const outLibrary)
{
    const CNA_Result result = LookupHandle(handle, ObjectKind::MediaLibrary, outLibrary);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowLibraryResource(
    const CNA_MediaLibraryHandle handle,
    std::shared_ptr<MediaLibraryResource>* const outLibrary)
{
    return BorrowLibrary<MediaLibrary>(handle, outLibrary);
}

// The canonical art members hand back a stream whose caller owns it. This reads it to the end and
// destroys it inside the call, so no stream object ever crosses the ABI.
[[nodiscard]] CNA_Result ReadStreamBytes(
    System::IO::Stream* const stream,
    std::vector<uint8_t>* const outBytes)
{
    const std::unique_ptr<System::IO::Stream> owned(stream);
    outBytes->clear();
    if (owned == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    SharpRuntime::bytecs chunk[4096];
    for (;;) {
        const SharpRuntime::intcs read =
            owned->Read(chunk, 0, static_cast<SharpRuntime::intcs>(sizeof(chunk)));
        if (read <= 0) {
            break;
        }
        outBytes->insert(outBytes->end(), chunk, chunk + read);
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TCollection, typename TElement>
[[nodiscard]] CNA_Result CollectionElementAt(
    const CNA_Handle collectionHandle,
    const int32_t index,
    CNA_Handle* const outElement)
{
    if (outElement == nullptr) {
        return InvalidArgument("The collection element output is null.");
    }
    *outElement = CNA_INVALID_HANDLE;
    std::shared_ptr<MediaChildResource<TCollection>> resource;
    if (const CNA_Result result = Borrow<TCollection>(collectionHandle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index < 0 || index >= static_cast<int32_t>(resource->value->getCountProperty())) {
        return InvalidArgument("The collection index is out of range.");
    }
    TElement* const element = (*resource->value)[static_cast<SharpRuntime::intcs>(index)];
    return PublishChildOf<TElement, TCollection>(resource, element, outElement);
}

template<typename TValue>
[[nodiscard]] CNA_Result ReleaseHandle(const CNA_Handle handle, const char* const message)
{
    std::shared_ptr<MediaChildResource<TValue>> resource;
    if (const CNA_Result result = Borrow<TValue>(handle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(handle);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_album_destroy(const CNA_AlbumHandle album)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<Album>(album, "The album handle could not be released.");
    });
}

CNA_Result cna_album_get_songs(
    const CNA_AlbumHandle album,
    CNA_SongCollectionHandle* const outSongs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSongs == nullptr) {
            return InvalidArgument("The song collection output is null.");
        }
        *outSongs = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Album>> resource;
        if (const CNA_Result result = Borrow<Album>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<SongCollection, Album>(
            resource, resource->value->getSongsProperty(), outSongs);
    });
}

CNA_Result cna_album_equals(
    const CNA_AlbumHandle left,
    const CNA_AlbumHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The album comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<Album>> nativeLeft;
        std::shared_ptr<MediaChildResource<Album>> nativeRight;
        if (const CNA_Result result = Borrow<Album>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<Album>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_album_collection_get_at(
    const CNA_AlbumCollectionHandle collection,
    const int32_t index,
    CNA_AlbumHandle* const outAlbum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<AlbumCollection, Album>(collection, index, outAlbum);
    });
}

CNA_Result cna_album_collection_destroy(const CNA_AlbumCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<AlbumCollection>(
            collection, "The album collection handle could not be released.");
    });
}

CNA_Result cna_artist_destroy(const CNA_ArtistHandle artist)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<Artist>(artist, "The artist handle could not be released.");
    });
}

CNA_Result cna_artist_get_songs(
    const CNA_ArtistHandle artist,
    CNA_SongCollectionHandle* const outSongs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSongs == nullptr) {
            return InvalidArgument("The song collection output is null.");
        }
        *outSongs = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Artist>> resource;
        if (const CNA_Result result = Borrow<Artist>(artist, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<SongCollection, Artist>(
            resource, resource->value->getSongsProperty(), outSongs);
    });
}

CNA_Result cna_artist_equals(
    const CNA_ArtistHandle left,
    const CNA_ArtistHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The artist comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<Artist>> nativeLeft;
        std::shared_ptr<MediaChildResource<Artist>> nativeRight;
        if (const CNA_Result result = Borrow<Artist>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<Artist>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_artist_collection_get_at(
    const CNA_ArtistCollectionHandle collection,
    const int32_t index,
    CNA_ArtistHandle* const outArtist)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<ArtistCollection, Artist>(collection, index, outArtist);
    });
}

CNA_Result cna_artist_collection_destroy(const CNA_ArtistCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<ArtistCollection>(
            collection, "The artist collection handle could not be released.");
    });
}

CNA_Result cna_genre_destroy(const CNA_GenreHandle genre)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<Genre>(genre, "The genre handle could not be released.");
    });
}

CNA_Result cna_genre_get_songs(
    const CNA_GenreHandle genre,
    CNA_SongCollectionHandle* const outSongs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSongs == nullptr) {
            return InvalidArgument("The song collection output is null.");
        }
        *outSongs = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Genre>> resource;
        if (const CNA_Result result = Borrow<Genre>(genre, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<SongCollection, Genre>(
            resource, resource->value->getSongsProperty(), outSongs);
    });
}

CNA_Result cna_genre_equals(
    const CNA_GenreHandle left,
    const CNA_GenreHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The genre comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<Genre>> nativeLeft;
        std::shared_ptr<MediaChildResource<Genre>> nativeRight;
        if (const CNA_Result result = Borrow<Genre>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<Genre>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_genre_collection_get_at(
    const CNA_GenreCollectionHandle collection,
    const int32_t index,
    CNA_GenreHandle* const outGenre)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<GenreCollection, Genre>(collection, index, outGenre);
    });
}

CNA_Result cna_genre_collection_destroy(const CNA_GenreCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<GenreCollection>(
            collection, "The genre collection handle could not be released.");
    });
}

CNA_Result cna_playlist_destroy(const CNA_PlaylistHandle playlist)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<Playlist>(playlist, "The playlist handle could not be released.");
    });
}

CNA_Result cna_playlist_get_songs(
    const CNA_PlaylistHandle playlist,
    CNA_SongCollectionHandle* const outSongs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSongs == nullptr) {
            return InvalidArgument("The song collection output is null.");
        }
        *outSongs = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Playlist>> resource;
        if (const CNA_Result result = Borrow<Playlist>(playlist, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<SongCollection, Playlist>(
            resource, resource->value->getSongsProperty(), outSongs);
    });
}

CNA_Result cna_playlist_equals(
    const CNA_PlaylistHandle left,
    const CNA_PlaylistHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The playlist comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<Playlist>> nativeLeft;
        std::shared_ptr<MediaChildResource<Playlist>> nativeRight;
        if (const CNA_Result result = Borrow<Playlist>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<Playlist>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_playlist_collection_get_at(
    const CNA_PlaylistCollectionHandle collection,
    const int32_t index,
    CNA_PlaylistHandle* const outPlaylist)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<PlaylistCollection, Playlist>(collection, index, outPlaylist);
    });
}

CNA_Result cna_playlist_collection_destroy(const CNA_PlaylistCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<PlaylistCollection>(
            collection, "The playlist collection handle could not be released.");
    });
}

CNA_Result cna_media_library_create(
    const CNA_Handle gameHandle,
    CNA_MediaLibraryHandle* const outLibrary)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLibrary == nullptr) {
            return InvalidArgument("The media library output is null.");
        }
        *outLibrary = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<MediaLibraryResource>();
        resource->value = std::make_unique<MediaLibrary>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::MediaLibrary,
            std::move(resource),
            outLibrary);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The media library handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_create_from_source(
    const CNA_Handle gameHandle,
    const uint32_t sourceIndex,
    CNA_MediaLibraryHandle* const outLibrary)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLibrary == nullptr) {
            return InvalidArgument("The media library output is null.");
        }
        *outLibrary = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical enumeration hands back raw owning pointers, and the canonical constructor
        // does NOT adopt the one it is given -- it copies that source's kind and name into its own
        // object. Sanitizer evidence, not an assumption: leaving the selected source to the
        // library leaked it. So every enumerated source is destroyed here, the selected one after
        // the library has copied it.
        std::vector<MediaSource*> sources = MediaSource::GetAvailableMediaSources();
        std::vector<std::unique_ptr<MediaSource>> owned;
        owned.reserve(sources.size());
        for (MediaSource* const source : sources) {
            owned.emplace_back(source);
        }
        if (static_cast<std::size_t>(sourceIndex) >= owned.size()) {
            return InvalidArgument("The media source index is out of range.");
        }
        auto resource = std::make_shared<MediaLibraryResource>();
        resource->value =
            std::make_unique<MediaLibrary>(owned[static_cast<std::size_t>(sourceIndex)].get());
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::MediaLibrary,
            std::move(resource),
            outLibrary);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The media library handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_destroy(const CNA_MediaLibraryHandle library)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(library);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The media library handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_get_media_source_type(
    const CNA_MediaLibraryHandle library,
    CNA_MediaSourceType* const outType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outType == nullptr) {
            return InvalidArgument("The media source kind output is null.");
        }
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const MediaSource* const source = resource->value->getMediaSourceProperty();
        if (source == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The media library has no media source.");
        }
        *outType = static_cast<CNA_MediaSourceType>(source->getMediaSourceTypeProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_get_media_source_name_size(
    const CNA_MediaLibraryHandle library,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The media source name byte-count output is null.");
        }
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const MediaSource* const source = resource->value->getMediaSourceProperty();
        if (source == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The media library has no media source.");
        }
        *outBytes = source->getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_copy_media_source_name(
    const CNA_MediaLibraryHandle library,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const MediaSource* const source = resource->value->getMediaSourceProperty();
        if (source == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The media library has no media source.");
        }
        return CopyText(source->getNameProperty(), destination, capacity, outBytes);
    });
}

namespace {

template<typename TCollection, typename TGet>
[[nodiscard]] CNA_Result LibraryCollection(
    const CNA_MediaLibraryHandle library,
    CNA_Handle* const outCollection,
    const TGet get,
    const char* const message)
{
    if (outCollection == nullptr) {
        return InvalidArgument(message);
    }
    *outCollection = CNA_INVALID_HANDLE;
    std::shared_ptr<MediaLibraryResource> resource;
    if (const CNA_Result result = BorrowLibraryResource(library, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TCollection* const collection = get(*resource->value);
    if (collection == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The media library did not build this collection.");
    }
    return PublishLibraryChild<TCollection>(
        MediaTraits<TCollection>::Kind,
        resource,
        collection,
        "The media library collection handle could not be created.",
        outCollection);
}

} // namespace

CNA_Result cna_media_library_get_songs(
    const CNA_MediaLibraryHandle library,
    CNA_SongCollectionHandle* const outSongs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<SongCollection>(
            library,
            outSongs,
            [](const MediaLibrary& value) { return value.getSongsProperty(); },
            "The song collection output is null.");
    });
}

CNA_Result cna_media_library_get_albums(
    const CNA_MediaLibraryHandle library,
    CNA_AlbumCollectionHandle* const outAlbums)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<AlbumCollection>(
            library,
            outAlbums,
            [](const MediaLibrary& value) { return value.getAlbumsProperty(); },
            "The album collection output is null.");
    });
}

CNA_Result cna_media_library_get_artists(
    const CNA_MediaLibraryHandle library,
    CNA_ArtistCollectionHandle* const outArtists)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<ArtistCollection>(
            library,
            outArtists,
            [](const MediaLibrary& value) { return value.getArtistsProperty(); },
            "The artist collection output is null.");
    });
}

CNA_Result cna_media_library_get_genres(
    const CNA_MediaLibraryHandle library,
    CNA_GenreCollectionHandle* const outGenres)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<GenreCollection>(
            library,
            outGenres,
            [](const MediaLibrary& value) { return value.getGenresProperty(); },
            "The genre collection output is null.");
    });
}

CNA_Result cna_media_library_get_playlists(
    const CNA_MediaLibraryHandle library,
    CNA_PlaylistCollectionHandle* const outPlaylists)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<PlaylistCollection>(
            library,
            outPlaylists,
            [](const MediaLibrary& value) { return value.getPlaylistsProperty(); },
            "The playlist collection output is null.");
    });
}

namespace {

// A canonical getter that may answer null becomes an availability flag plus an untouched output,
// the same rule the sensor and gamepad queries follow.
template<typename TOwner, typename TValue, typename TGet>
[[nodiscard]] CNA_Result OptionalChild(
    const CNA_Handle ownerHandle,
    CNA_Handle* const outValue,
    CNA_Bool* const outAvailable,
    const TGet get)
{
    if (outValue == nullptr || outAvailable == nullptr) {
        return InvalidArgument("The optional media object output is null.");
    }
    std::shared_ptr<MediaChildResource<TOwner>> resource;
    if (const CNA_Result result = Borrow<TOwner>(ownerHandle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TValue* const value = get(*resource->value);
    if (value == nullptr) {
        *outAvailable = CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    }
    if (const CNA_Result result = PublishChildOf<TValue, TOwner>(resource, value, outValue);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outAvailable = CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_album_get_artist(
    const CNA_AlbumHandle album,
    CNA_ArtistHandle* const outArtist,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Album, Artist>(
            album,
            outArtist,
            outAvailable,
            [](const Album& value) { return value.getArtistProperty(); });
    });
}

CNA_Result cna_album_get_genre(
    const CNA_AlbumHandle album,
    CNA_GenreHandle* const outGenre,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Album, Genre>(
            album,
            outGenre,
            outAvailable,
            [](const Album& value) { return value.getGenreProperty(); });
    });
}

CNA_Result cna_album_get_art_size(const CNA_AlbumHandle album, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The album art byte-count output is null.");
        }
        std::shared_ptr<MediaChildResource<Album>> resource;
        if (const CNA_Result result = Borrow<Album>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetAlbumArt(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(bytes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_album_copy_art(
    const CNA_AlbumHandle album,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaChildResource<Album>> resource;
        if (const CNA_Result result = Borrow<Album>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetAlbumArt(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(bytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_album_get_thumbnail_size(const CNA_AlbumHandle album, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The album thumbnail byte-count output is null.");
        }
        std::shared_ptr<MediaChildResource<Album>> resource;
        if (const CNA_Result result = Borrow<Album>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetThumbnail(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(bytes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_album_copy_thumbnail(
    const CNA_AlbumHandle album,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaChildResource<Album>> resource;
        if (const CNA_Result result = Borrow<Album>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetThumbnail(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(bytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_artist_get_albums(
    const CNA_ArtistHandle artist,
    CNA_AlbumCollectionHandle* const outAlbums)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAlbums == nullptr) {
            return InvalidArgument("The album collection output is null.");
        }
        *outAlbums = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Artist>> resource;
        if (const CNA_Result result = Borrow<Artist>(artist, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<AlbumCollection, Artist>(
            resource, resource->value->getAlbumsProperty(), outAlbums);
    });
}

CNA_Result cna_genre_get_albums(
    const CNA_GenreHandle genre,
    CNA_AlbumCollectionHandle* const outAlbums)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAlbums == nullptr) {
            return InvalidArgument("The album collection output is null.");
        }
        *outAlbums = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<Genre>> resource;
        if (const CNA_Result result = Borrow<Genre>(genre, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishChildOf<AlbumCollection, Genre>(
            resource, resource->value->getAlbumsProperty(), outAlbums);
    });
}

CNA_Result cna_song_get_album(
    const CNA_SongHandle song,
    CNA_AlbumHandle* const outAlbum,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Song, Album>(
            song,
            outAlbum,
            outAvailable,
            [](const Song& value) { return value.getAlbumProperty(); });
    });
}

CNA_Result cna_song_get_artist(
    const CNA_SongHandle song,
    CNA_ArtistHandle* const outArtist,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Song, Artist>(
            song,
            outArtist,
            outAvailable,
            [](const Song& value) { return value.getArtistProperty(); });
    });
}

CNA_Result cna_song_get_genre(
    const CNA_SongHandle song,
    CNA_GenreHandle* const outGenre,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Song, Genre>(
            song,
            outGenre,
            outAvailable,
            [](const Song& value) { return value.getGenreProperty(); });
    });
}

CNA_Result cna_picture_destroy(const CNA_PictureHandle handle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<Picture>(handle, "The picture handle could not be released.");
    });
}

CNA_Result cna_picture_equals(
    const CNA_PictureHandle left,
    const CNA_PictureHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The picture comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<Picture>> nativeLeft;
        std::shared_ptr<MediaChildResource<Picture>> nativeRight;
        if (const CNA_Result result = Borrow<Picture>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<Picture>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_picture_album_destroy(const CNA_PictureAlbumHandle handle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<PictureAlbum>(handle, "The picture album handle could not be released.");
    });
}

CNA_Result cna_picture_album_equals(
    const CNA_PictureAlbumHandle left,
    const CNA_PictureAlbumHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The picture album comparison output is null.");
        }
        std::shared_ptr<MediaChildResource<PictureAlbum>> nativeLeft;
        std::shared_ptr<MediaChildResource<PictureAlbum>> nativeRight;
        if (const CNA_Result result = Borrow<PictureAlbum>(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = Borrow<PictureAlbum>(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = ToCBool(nativeLeft->value->Equals(nativeRight->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_picture_collection_get_at(
    const CNA_PictureCollectionHandle collection,
    const int32_t index,
    CNA_PictureHandle* const outPicture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<PictureCollection, Picture>(collection, index, outPicture);
    });
}

CNA_Result cna_picture_collection_destroy(const CNA_PictureCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<PictureCollection>(collection, "The collection handle could not be released.");
    });
}

CNA_Result cna_picture_album_collection_get_at(
    const CNA_PictureAlbumCollectionHandle collection,
    const int32_t index,
    CNA_PictureAlbumHandle* const outPictureAlbum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CollectionElementAt<PictureAlbumCollection, PictureAlbum>(collection, index, outPictureAlbum);
    });
}

CNA_Result cna_picture_album_collection_destroy(const CNA_PictureAlbumCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReleaseHandle<PictureAlbumCollection>(collection, "The collection handle could not be released.");
    });
}

CNA_Result cna_picture_get_album(
    const CNA_PictureHandle picture,
    CNA_PictureAlbumHandle* const outAlbum,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<Picture, PictureAlbum>(
            picture,
            outAlbum,
            outAvailable,
            [](const Picture& value) { return value.getAlbumProperty(); });
    });
}

CNA_Result cna_picture_get_date_unix_ticks(
    const CNA_PictureHandle picture,
    int64_t* const outUnixTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outUnixTicks == nullptr) {
            return InvalidArgument("The picture date output is null.");
        }
        return Read<Picture>(picture, [&](const Picture& value) {
            // A point in time, not a duration: the ABI's 100-nanosecond tick counted from the Unix
            // epoch, which is exactly what the canonical clock's own epoch already is.
            const auto since =
                value.getDateProperty().time_since_epoch();
            *outUnixTicks = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(
                    since)
                    .count());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_picture_get_image_size(const CNA_PictureHandle picture, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The picture image byte-count output is null.");
        }
        std::shared_ptr<MediaChildResource<Picture>> resource;
        if (const CNA_Result result = Borrow<Picture>(picture, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetImage(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(bytes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_picture_copy_image(
    const CNA_PictureHandle picture,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaChildResource<Picture>> resource;
        if (const CNA_Result result = Borrow<Picture>(picture, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetImage(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(bytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_picture_get_thumbnail_size(
    const CNA_PictureHandle picture,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The picture thumbnail byte-count output is null.");
        }
        std::shared_ptr<MediaChildResource<Picture>> resource;
        if (const CNA_Result result = Borrow<Picture>(picture, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetThumbnail(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(bytes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_picture_copy_thumbnail(
    const CNA_PictureHandle picture,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaChildResource<Picture>> resource;
        if (const CNA_Result result = Borrow<Picture>(picture, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> bytes;
        if (const CNA_Result result = ReadStreamBytes(resource->value->GetThumbnail(), &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(bytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_picture_album_get_parent(
    const CNA_PictureAlbumHandle album,
    CNA_PictureAlbumHandle* const outParent,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OptionalChild<PictureAlbum, PictureAlbum>(
            album,
            outParent,
            outAvailable,
            [](const PictureAlbum& value) { return value.getParentProperty(); });
    });
}

CNA_Result cna_picture_album_get_albums(
    const CNA_PictureAlbumHandle album,
    CNA_PictureAlbumCollectionHandle* const outAlbums)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAlbums == nullptr) {
            return InvalidArgument("The picture album collection output is null.");
        }
        *outAlbums = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<PictureAlbum>> resource;
        if (const CNA_Result result = Borrow<PictureAlbum>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PictureAlbumCollection* const children = resource->value->getAlbumsProperty();
        if (children == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The picture album has no child album collection.");
        }
        return PublishChildOf<PictureAlbumCollection, PictureAlbum>(resource, children, outAlbums);
    });
}

CNA_Result cna_picture_album_get_pictures(
    const CNA_PictureAlbumHandle album,
    CNA_PictureCollectionHandle* const outPictures)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPictures == nullptr) {
            return InvalidArgument("The picture collection output is null.");
        }
        *outPictures = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaChildResource<PictureAlbum>> resource;
        if (const CNA_Result result = Borrow<PictureAlbum>(album, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PictureCollection* const pictures = resource->value->getPicturesProperty();
        if (pictures == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The picture album has no picture collection.");
        }
        return PublishChildOf<PictureCollection, PictureAlbum>(resource, pictures, outPictures);
    });
}

CNA_Result cna_media_library_get_pictures(
    const CNA_MediaLibraryHandle library,
    CNA_PictureCollectionHandle* const outPictures)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<PictureCollection>(
            library,
            outPictures,
            [](const MediaLibrary& value) { return value.getPicturesProperty(); },
            "The picture collection output is null.");
    });
}

CNA_Result cna_media_library_get_saved_pictures(
    const CNA_MediaLibraryHandle library,
    CNA_PictureCollectionHandle* const outPictures)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LibraryCollection<PictureCollection>(
            library,
            outPictures,
            [](const MediaLibrary& value) { return value.getSavedPicturesProperty(); },
            "The saved picture collection output is null.");
    });
}

CNA_Result cna_media_library_get_root_picture_album(
    const CNA_MediaLibraryHandle library,
    CNA_PictureAlbumHandle* const outAlbum,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAlbum == nullptr || outAvailable == nullptr) {
            return InvalidArgument("The root picture album output is null.");
        }
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PictureAlbum* const root = resource->value->getRootPictureAlbumProperty();
        if (root == nullptr) {
            *outAvailable = CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = PublishLibraryChild<PictureAlbum>(
                ObjectKind::PictureAlbum,
                resource,
                root,
                "The picture album handle could not be created.",
                outAlbum);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAvailable = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_library_get_picture_from_token(
    const CNA_MediaLibraryHandle library,
    const CNA_StringView token,
    CNA_PictureHandle* const outPicture,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPicture == nullptr || outAvailable == nullptr) {
            return InvalidArgument("The picture lookup output is null.");
        }
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeToken;
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(token, false, &nativeToken);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The picture token is not valid UTF-8.");
        }
        Picture* const picture = resource->value->GetPictureFromToken(nativeToken);
        if (picture == nullptr) {
            *outAvailable = CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = PublishLibraryChild<Picture>(
                ObjectKind::Picture,
                resource,
                picture,
                "The picture handle could not be created.",
                outPicture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAvailable = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result PublishSavedPicture(
    const std::shared_ptr<MediaLibraryResource>& library,
    Picture* const picture,
    CNA_PictureHandle* const outPicture)
{
    if (picture == nullptr) {
        return Fail(
            CNA_RESULT_IO,
            CNA_ERROR_CATEGORY_IO,
            "The picture could not be saved.");
    }
    return PublishLibraryChild<Picture>(
        ObjectKind::Picture,
        library,
        picture,
        "The picture handle could not be created.",
        outPicture);
}

} // namespace

CNA_Result cna_media_library_save_picture(
    const CNA_MediaLibraryHandle library,
    const CNA_StringView name,
    const uint8_t* const imageData,
    const uint64_t imageByteCount,
    CNA_PictureHandle* const outPicture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPicture == nullptr) {
            return InvalidArgument("The saved picture output is null.");
        }
        *outPicture = CNA_INVALID_HANDLE;
        if (imageData == nullptr && imageByteCount != UINT64_C(0)) {
            return InvalidArgument("The image buffer is null.");
        }
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeName;
        if (const CNA_Result result = CNA::C::Detail::CopyStringView(name, false, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The picture name is not valid UTF-8.");
        }
        const std::vector<uint8_t> bytes(
            imageData,
            imageData + static_cast<std::size_t>(imageByteCount));
        return PublishSavedPicture(
            resource,
            resource->value->SavePicture(std::move(nativeName), bytes),
            outPicture);
    });
}

CNA_Result cna_media_library_save_picture_from_stream(
    const CNA_MediaLibraryHandle library,
    const CNA_StringView name,
    const CNA_Handle source,
    CNA_PictureHandle* const outPicture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPicture == nullptr) {
            return InvalidArgument("The saved picture output is null.");
        }
        *outPicture = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaLibraryResource> resource;
        if (const CNA_Result result = BorrowLibraryResource(library, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeName;
        if (const CNA_Result result = CNA::C::Detail::CopyStringView(name, false, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The picture name is not valid UTF-8.");
        }
        // A storage stream is the only byte source this ABI owns, so it is what the canonical
        // stream overload accepts. The borrow keeps it open for the call and hands it straight
        // back afterwards -- the stream stays the caller's to close.
        CNA::C::Detail::BorrowedStorageStream stream;
        if (const CNA_Result result = CNA::C::Detail::AcquireStorageStream(source, &stream);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The storage stream handle is invalid for this call.");
        }
        CNA_Result result = CNA_RESULT_SUCCESS;
        try {
            result = PublishSavedPicture(
                resource,
                resource->value->SavePicture(std::move(nativeName), stream.value),
                outPicture);
        } catch (...) {
            CNA::C::Detail::ReleaseStorageStream(stream);
            throw;
        }
        CNA::C::Detail::ReleaseStorageStream(stream);
        return result;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/MediaLibrary.toml.
#include "generated/MediaLibrary.inc"
