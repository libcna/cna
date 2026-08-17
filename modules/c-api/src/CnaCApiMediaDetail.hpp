// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_MEDIA_DETAIL_HPP
#define CNA_C_API_MEDIA_DETAIL_HPP

#include "CNA/C/media.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace CNA::C::Media::Detail {

/** Owns one canonical media library; every other media object is reached through one. */
struct MediaLibraryResource final {
    std::unique_ptr<Microsoft::Xna::Framework::Media::MediaLibrary> value;
};

/**
 * One resource shape for every media object that is not the library itself.
 *
 * A media object is either created by the C caller — in which case @ref owned holds it — or owned
 * by a media library, in which case @ref library keeps that library alive for as long as any
 * handle into it survives. @ref retained holds anything else the object must outlive, which is how
 * a C-built song collection keeps its songs alive even after the caller releases their handles.
 * Several handles may share one resource, so releasing one never destroys an object another still
 * refers to.
 */
template<typename TValue>
struct MediaChildResource final {
    std::unique_ptr<TValue> owned;
    std::shared_ptr<MediaLibraryResource> library;
    std::vector<std::shared_ptr<void>> retained;
    TValue* value = nullptr;
};

using SongResource = MediaChildResource<Microsoft::Xna::Framework::Media::Song>;
using SongCollectionResource =
    MediaChildResource<Microsoft::Xna::Framework::Media::SongCollection>;

/** Fetches a media child resource of a known handle kind, converting handle failures. */
template<typename TValue>
[[nodiscard]] inline CNA_Result BorrowMediaChild(
    const CNA_Handle handle,
    const CNA::C::Detail::ObjectKind kind,
    const char* const message,
    std::shared_ptr<MediaChildResource<TValue>>* const outResource)
{
    const CNA_Result result =
        CNA::C::Detail::GetRuntimeHandles().Get(handle, kind, outResource);
    if (result != CNA_RESULT_SUCCESS) {
        return CNA::C::Detail::Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            message);
    }
    return CNA_RESULT_SUCCESS;
}

/** Publishes an already-built media child resource under a new handle. */
template<typename TValue>
[[nodiscard]] inline CNA_Result PublishMediaChild(
    const CNA::C::Detail::ObjectKind kind,
    std::shared_ptr<MediaChildResource<TValue>> resource,
    const char* const message,
    CNA_Handle* const outHandle)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        kind,
        std::move(resource),
        outHandle);
    if (result != CNA_RESULT_SUCCESS) {
        return CNA::C::Detail::Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            message);
    }
    return CNA_RESULT_SUCCESS;
}

/**
 * Publishes a borrowed view of a library-owned object.
 *
 * A null value is not an error here: the canonical getters that can return one are mapped with an
 * availability flag, and the caller decides what a missing object means.
 */
template<typename TValue>
[[nodiscard]] inline CNA_Result PublishLibraryChild(
    const CNA::C::Detail::ObjectKind kind,
    std::shared_ptr<MediaLibraryResource> library,
    TValue* const value,
    const char* const message,
    CNA_Handle* const outHandle)
{
    auto resource = std::make_shared<MediaChildResource<TValue>>();
    resource->library = std::move(library);
    resource->value = value;
    return PublishMediaChild<TValue>(kind, std::move(resource), message, outHandle);
}

} // namespace CNA::C::Media::Detail

#endif
