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
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::IoError;
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
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::ReleaseHandle;
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
    return LookupHandle(handle, ObjectKind::MediaLibrary, outLibrary);
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

} // namespace

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
        return CreateResourceHandle<MediaLibraryResource>(
            ObjectKind::MediaLibrary,
            std::make_unique<MediaLibrary>(owned[static_cast<std::size_t>(sourceIndex)].get()),
            outLibrary);
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
            return InvalidState("The media library has no media source.");
        }
        *outType = static_cast<CNA_MediaSourceType>(source->getMediaSourceTypeProperty());
        return CNA_RESULT_SUCCESS;
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
        return InvalidState("The media library did not build this collection.");
    }
    return PublishLibraryChild<TCollection>(
        MediaTraits<TCollection>::Kind,
        resource,
        collection,
        "The media library collection handle could not be created.",
        outCollection);
}

} // namespace

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
            return InvalidState("The picture album has no child album collection.");
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
            return InvalidState("The picture album has no picture collection.");
        }
        return PublishChildOf<PictureCollection, PictureAlbum>(resource, pictures, outPictures);
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
        if (const CNA_Result result = CNA::C::Detail::ReadText(
                token, "The picture token is not valid UTF-8.", &nativeToken);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        return IoError("The picture could not be saved.");
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
        if (const CNA_Result result =
                CNA::C::Detail::ReadText(name, "The picture name is not valid UTF-8.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        if (const CNA_Result result =
                CNA::C::Detail::ReadText(name, "The picture name is not valid UTF-8.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
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

// The routes generated from modules/c-api/spec/MediaLibrary.toml.
#include "generated/MediaLibrary.inc"
