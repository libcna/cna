// SPDX-License-Identifier: MS-PL

#include "CNA/C/media.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMediaDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::C::Detail::CopyText;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ToCBool;

using Microsoft::Xna::Framework::Media::MediaSource;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;
using Microsoft::Xna::Framework::Media::VisualizationData;

// The canonical enumeration hands back raw `new`-ed sources its caller must delete. This owns that
// list for the length of one call so no ownership — and no leak — crosses the ABI.
class OwnedMediaSources final {
public:
    OwnedMediaSources()
        : sources_(MediaSource::GetAvailableMediaSources())
    {
    }

    OwnedMediaSources(const OwnedMediaSources&) = delete;
    OwnedMediaSources& operator=(const OwnedMediaSources&) = delete;

    ~OwnedMediaSources()
    {
        for (MediaSource* const source : sources_) {
            delete source;
        }
    }

    [[nodiscard]] std::size_t Count() const noexcept { return sources_.size(); }

    [[nodiscard]] const MediaSource* At(const std::size_t index) const noexcept
    {
        return sources_[index];
    }

private:
    std::vector<MediaSource*> sources_;
};

template<typename TRead>
[[nodiscard]] CNA_Result ReadEnumeratedSource(
    const CNA_Handle gameHandle,
    const uint32_t index,
    const TRead read)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const OwnedMediaSources sources;
    if (static_cast<std::size_t>(index) >= sources.Count()) {
        return InvalidArgument("The media source index is out of range.");
    }
    return read(*sources.At(static_cast<std::size_t>(index)));
}

using CNA::C::Media::Detail::SongCollectionResource;
using CNA::C::Media::Detail::SongResource;

[[nodiscard]] CNA_Result BorrowSong(
    const CNA_SongHandle handle,
    std::shared_ptr<SongResource>* const outSong)
{
    return CNA::C::Media::Detail::BorrowMediaChild<Song>(
        handle,
        CNA::C::Detail::ObjectKind::Song,
        "The song handle is invalid for this call.",
        outSong);
}

[[nodiscard]] CNA_Result BorrowSongCollection(
    const CNA_SongCollectionHandle handle,
    std::shared_ptr<SongCollectionResource>* const outCollection)
{
    return CNA::C::Media::Detail::BorrowMediaChild<SongCollection>(
        handle,
        CNA::C::Detail::ObjectKind::SongCollection,
        "The song collection handle is invalid for this call.",
        outCollection);
}

[[nodiscard]] CNA_Result PublishSong(
    std::shared_ptr<SongResource> resource,
    CNA_SongHandle* const outSong)
{
    return CNA::C::Media::Detail::PublishMediaChild<Song>(
        CNA::C::Detail::ObjectKind::Song,
        std::move(resource),
        "The song handle could not be created.",
        outSong);
}

[[nodiscard]] CNA_Result CreateOwnedSong(
    const CNA_Handle gameHandle,
    std::unique_ptr<Song> song,
    CNA_SongHandle* const outSong)
{
    (void)gameHandle;
    auto resource = std::make_shared<SongResource>();
    resource->value = song.get();
    resource->owned = std::move(song);
    return PublishSong(std::move(resource), outSong);
}

template<typename TRead>
[[nodiscard]] CNA_Result ReadSong(const CNA_SongHandle handle, const TRead read)
{
    std::shared_ptr<SongResource> resource;
    if (const CNA_Result result = BorrowSong(handle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return read(*resource->value);
}

[[nodiscard]] CNA_Result VisualizationDataInit(CNA_VisualizationData* const outData)
{
    const VisualizationData native;
    CNA_VisualizationData data = {};
    data.struct_size = sizeof(CNA_VisualizationData);
    data.struct_version = StructureVersion;
    const auto& frequencies = native.getFrequenciesProperty();
    const auto& samples = native.getSamplesProperty();
    for (std::size_t index = 0U; index < static_cast<std::size_t>(VisualizationData::Size);
         ++index) {
        data.frequencies[index] = frequencies[index];
        data.samples[index] = samples[index];
    }
    *outData = data;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MediaSourceCopyNameAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidArgument("The media source name output is invalid.");
    }
    return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
        // The canonical string conversion returns the display name unchanged, so one route
        // serves both members rather than the ABI carrying two spellings of one string.
        return CopyText(source.ToString(), destination, capacity, outBytes);
    });
}

[[nodiscard]] CNA_Result MediaSourceCopyTypeNameAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidArgument("The media source type-name output is invalid.");
    }
    return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
        return CopyText(source.GetTypeName(), destination, capacity, outBytes);
    });
}

} // namespace

namespace {

[[nodiscard]] CNA_Result SongCreate(
    const CNA_Handle gameHandle,
    const CNA_StringView fileName,
    const CNA_StringView name,
    CNA_SongHandle* const outSong)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeFileName;
    std::string nativeName;
    if (const CNA_Result result = ReadText(
            fileName, "The song file name is not valid UTF-8.", &nativeFileName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ReadText(name, "The song name is not valid UTF-8.", &nativeName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateOwnedSong(
        gameHandle,
        std::make_unique<Song>(std::move(nativeFileName), std::move(nativeName)),
        outSong);
}

[[nodiscard]] CNA_Result SongCreateWithDuration(
    const CNA_Handle gameHandle,
    const CNA_StringView fileName,
    const CNA_StringView assetName,
    const int32_t durationMilliseconds,
    CNA_SongHandle* const outSong)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeFileName;
    std::string nativeAssetName;
    if (const CNA_Result result = ReadText(
            fileName, "The song file name is not valid UTF-8.", &nativeFileName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            assetName, "The song asset name is not valid UTF-8.", &nativeAssetName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateOwnedSong(
        gameHandle,
        std::make_unique<Song>(
            std::move(nativeFileName),
            std::move(nativeAssetName),
            static_cast<SharpRuntime::intcs>(durationMilliseconds)),
        outSong);
}

[[nodiscard]] CNA_Result SongCreateFromUri(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    const CNA_StringView uri,
    CNA_SongHandle* const outSong)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeName;
    std::string nativeUri;
    if (const CNA_Result result =
            ReadText(name, "The song name is not valid UTF-8.", &nativeName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ReadText(uri, "The song URI is not valid UTF-8.", &nativeUri);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical factory returns a raw owning pointer; it becomes C-owned immediately so no
    // ownership decision is left to the caller.
    return CreateOwnedSong(
        gameHandle,
        std::unique_ptr<Song>(Song::FromUri(nativeName, nativeUri)),
        outSong);
}

[[nodiscard]] CNA_Result SongCollectionCreate(
    const CNA_Handle gameHandle,
    const CNA_SongHandle* const songs,
    const uint64_t count,
    CNA_SongCollectionHandle* const outCollection)
{
    if (songs == nullptr && count != UINT64_C(0)) {
        return InvalidArgument("The song array is null.");
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<SongCollectionResource>();
    std::vector<Song*> values;
    values.reserve(static_cast<std::size_t>(count));
    resource->retained.reserve(static_cast<std::size_t>(count));
    for (uint64_t index = UINT64_C(0); index < count; ++index) {
        std::shared_ptr<SongResource> song;
        if (const CNA_Result result = BorrowSong(songs[index], &song);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        values.push_back(song->value);
        resource->retained.push_back(std::move(song));
    }
    resource->owned = std::make_unique<SongCollection>(std::move(values));
    resource->value = resource->owned.get();
    return CNA::C::Media::Detail::PublishMediaChild<SongCollection>(
        CNA::C::Detail::ObjectKind::SongCollection,
        std::move(resource),
        "The song collection handle could not be created.",
        outCollection);
}

[[nodiscard]] CNA_Result SongCollectionGetAt(
    const CNA_SongCollectionHandle collection,
    const int32_t index,
    CNA_SongHandle* const outSong)
{
    std::shared_ptr<SongCollectionResource> resource;
    if (const CNA_Result result = BorrowSongCollection(collection, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index < 0 ||
        index >= static_cast<int32_t>(resource->value->getCountProperty())) {
        return InvalidArgument("The song index is out of range.");
    }
    Song* const element = (*resource->value)[static_cast<SharpRuntime::intcs>(index)];
    // A C-built collection retains its songs, so the new handle shares the existing resource
    // and the song stays alive. A library-owned collection has none, so the new handle becomes
    // a borrowed view that keeps the library alive instead.
    for (const std::shared_ptr<void>& retained : resource->retained) {
        const auto song = std::static_pointer_cast<SongResource>(retained);
        if (song->value == element) {
            return PublishSong(song, outSong);
        }
    }
    auto borrowed = std::make_shared<SongResource>();
    borrowed->library = resource->library;
    borrowed->value = element;
    return PublishSong(std::move(borrowed), outSong);
}

} // namespace

// The routes generated from modules/c-api/spec/Media.toml.
#include "generated/Media.inc"
