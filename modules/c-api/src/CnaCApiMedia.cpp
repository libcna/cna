// SPDX-License-Identifier: MS-PL

#include "CNA/C/media.h"
#include "CnaCApiDetail.hpp"
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
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using Microsoft::Xna::Framework::Media::MediaSource;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;
using Microsoft::Xna::Framework::Media::VisualizationData;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The media text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the media text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

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
        return InvalidInput("The media source index is out of range.");
    }
    return read(*sources.At(static_cast<std::size_t>(index)));
}

// A song handle owns its object only when C created it. Several handles -- and any collection
// holding the song -- share one resource, so releasing one handle never destroys a song another
// still refers to.
struct SongResource final {
    std::unique_ptr<Song> owned;
    Song* value = nullptr;
};

// The canonical collection stores non-owning song pointers, so the C resource retains the song
// resources beside it: a caller that releases its own handles after building a collection cannot
// leave a dangling element behind.
struct SongCollectionResource final {
    std::unique_ptr<SongCollection> value;
    std::vector<std::shared_ptr<SongResource>> songs;
};

[[nodiscard]] CNA_Result BorrowSong(
    const CNA_SongHandle handle,
    std::shared_ptr<SongResource>* const outSong)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        CNA::C::Detail::ObjectKind::Song,
        outSong);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            "The song handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowSongCollection(
    const CNA_SongCollectionHandle handle,
    std::shared_ptr<SongCollectionResource>* const outCollection)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        CNA::C::Detail::ObjectKind::SongCollection,
        outCollection);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            "The song collection handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishSong(
    std::shared_ptr<SongResource> resource,
    CNA_SongHandle* const outSong)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        CNA::C::Detail::ObjectKind::Song,
        std::move(resource),
        outSong);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            "The song handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
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

} // namespace

CNA_Result cna_visualization_data_init(CNA_VisualizationData* const outData)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outData == nullptr) {
            return InvalidInput("The visualization data output is null.");
        }
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
    });
}

CNA_Result cna_visualization_data_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The visualization type-name byte-count output is null.");
        }
        const VisualizationData native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_visualization_data_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const VisualizationData native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_media_source_get_available_count(
    const CNA_Handle gameHandle,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The media source count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const OwnedMediaSources sources;
        *outCount = static_cast<uint32_t>(sources.Count());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_source_get_type_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_MediaSourceType* const outType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outType == nullptr) {
            return InvalidInput("The media source kind output is null.");
        }
        return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
            *outType = static_cast<CNA_MediaSourceType>(source.getMediaSourceTypeProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_source_get_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The media source name byte-count output is null.");
        }
        return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
            *outBytes = source.getNameProperty().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_source_copy_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The media source name output is invalid.");
        }
        return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
            // The canonical string conversion returns the display name unchanged, so one route
            // serves both members rather than the ABI carrying two spellings of one string.
            return CopyText(source.ToString(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_media_source_get_type_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The media source type-name byte-count output is null.");
        }
        return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
            *outBytes = source.GetTypeName().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_source_copy_type_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The media source type-name output is invalid.");
        }
        return ReadEnumeratedSource(gameHandle, index, [&](const MediaSource& source) {
            return CopyText(source.GetTypeName(), destination, capacity, outBytes);
        });
    });
}

namespace {

[[nodiscard]] CNA_Result CopyBorrowedText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (const CNA_Result result = CNA::C::Detail::CopyStringView(view, false, outText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, CNA::C::Detail::ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_song_create(
    const CNA_Handle gameHandle,
    const CNA_StringView fileName,
    const CNA_StringView name,
    CNA_SongHandle* const outSong)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr) {
            return InvalidInput("The song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeFileName;
        std::string nativeName;
        if (const CNA_Result result = CopyBorrowedText(
                fileName,
                "The song file name is not valid UTF-8.",
                &nativeFileName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                CopyBorrowedText(name, "The song name is not valid UTF-8.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateOwnedSong(
            gameHandle,
            std::make_unique<Song>(std::move(nativeFileName), std::move(nativeName)),
            outSong);
    });
}

CNA_Result cna_song_create_with_duration(
    const CNA_Handle gameHandle,
    const CNA_StringView fileName,
    const CNA_StringView assetName,
    const int32_t durationMilliseconds,
    CNA_SongHandle* const outSong)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr) {
            return InvalidInput("The song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeFileName;
        std::string nativeAssetName;
        if (const CNA_Result result = CopyBorrowedText(
                fileName,
                "The song file name is not valid UTF-8.",
                &nativeFileName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                assetName,
                "The song asset name is not valid UTF-8.",
                &nativeAssetName);
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
    });
}

CNA_Result cna_song_create_from_uri(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    const CNA_StringView uri,
    CNA_SongHandle* const outSong)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr) {
            return InvalidInput("The song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeName;
        std::string nativeUri;
        if (const CNA_Result result =
                CopyBorrowedText(name, "The song name is not valid UTF-8.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                CopyBorrowedText(uri, "The song URI is not valid UTF-8.", &nativeUri);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical factory returns a raw owning pointer; it becomes C-owned immediately so no
        // ownership decision is left to the caller.
        return CreateOwnedSong(
            gameHandle,
            std::unique_ptr<Song>(Song::FromUri(nativeName, nativeUri)),
            outSong);
    });
}

CNA_Result cna_song_get_name_size(const CNA_SongHandle song, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The song name byte-count output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outBytes = value.getNameProperty().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_copy_name(
    const CNA_SongHandle song,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The song name output is invalid.");
        }
        return ReadSong(song, [&](const Song& value) {
            // The canonical string conversion returns the display name unchanged, so one route
            // serves both members.
            return CopyText(value.ToString(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_song_get_handle_text_size_ext(const CNA_SongHandle song, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The song handle-text byte-count output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outBytes = value.getHandle().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_copy_handle_text_ext(
    const CNA_SongHandle song,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The song handle-text output is invalid.");
        }
        return ReadSong(song, [&](const Song& value) {
            return CopyText(value.getHandle(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_song_get_duration(const CNA_SongHandle song, int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The song duration output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outTicks = static_cast<int64_t>(value.getDurationProperty().getTicksProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_set_duration(const CNA_SongHandle song, const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setDurationProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_get_is_protected(const CNA_SongHandle song, CNA_Bool* const outProtected)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProtected == nullptr) {
            return InvalidInput("The song protection output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outProtected = value.getIsProtectedProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_get_is_rated(const CNA_SongHandle song, CNA_Bool* const outRated)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRated == nullptr) {
            return InvalidInput("The song rating-presence output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outRated = value.getIsRatedProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_get_play_count(const CNA_SongHandle song, int32_t* const outPlayCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayCount == nullptr) {
            return InvalidInput("The song play-count output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outPlayCount = static_cast<int32_t>(value.getPlayCountProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_set_play_count(const CNA_SongHandle song, const int32_t playCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPlayCountProperty(static_cast<SharpRuntime::intcs>(playCount));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_get_rating(const CNA_SongHandle song, int32_t* const outRating)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRating == nullptr) {
            return InvalidInput("The song rating output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outRating = static_cast<int32_t>(value.getRatingProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_get_track_number(const CNA_SongHandle song, int32_t* const outTrackNumber)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTrackNumber == nullptr) {
            return InvalidInput("The song track-number output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outTrackNumber = static_cast<int32_t>(value.getTrackNumberProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_get_is_disposed(const CNA_SongHandle song, CNA_Bool* const outDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDisposed == nullptr) {
            return InvalidInput("The song disposal-state output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outDisposed = value.getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_dispose(const CNA_SongHandle song)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_destroy(const CNA_SongHandle song)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(song);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                CNA::C::Detail::ErrorCategoryForResult(result),
                "The song handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_equals(
    const CNA_SongHandle left,
    const CNA_SongHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The song comparison output is null.");
        }
        std::shared_ptr<SongResource> nativeLeft;
        std::shared_ptr<SongResource> nativeRight;
        if (const CNA_Result result = BorrowSong(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowSong(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft->value->Equals(nativeRight->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_get_hash_code(const CNA_SongHandle song, int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The song hash output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outHash = static_cast<int32_t>(value.GetHashCode());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_get_type_name_size(const CNA_SongHandle song, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The song type-name byte-count output is null.");
        }
        return ReadSong(song, [&](const Song& value) {
            *outBytes = value.GetTypeName().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_song_copy_type_name(
    const CNA_SongHandle song,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The song type-name output is invalid.");
        }
        return ReadSong(song, [&](const Song& value) {
            return CopyText(value.GetTypeName(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_song_collection_create(
    const CNA_Handle gameHandle,
    const CNA_SongHandle* const songs,
    const uint64_t count,
    CNA_SongCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidInput("The song collection output is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (songs == nullptr && count != UINT64_C(0)) {
            return InvalidInput("The song array is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SongCollectionResource>();
        std::vector<Song*> values;
        values.reserve(static_cast<std::size_t>(count));
        resource->songs.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = UINT64_C(0); index < count; ++index) {
            std::shared_ptr<SongResource> song;
            if (const CNA_Result result = BorrowSong(songs[index], &song);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            values.push_back(song->value);
            resource->songs.push_back(std::move(song));
        }
        resource->value = std::make_unique<SongCollection>(std::move(values));
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            CNA::C::Detail::ObjectKind::SongCollection,
            std::move(resource),
            outCollection);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                CNA::C::Detail::ErrorCategoryForResult(result),
                "The song collection handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_get_at(
    const CNA_SongCollectionHandle collection,
    const int32_t index,
    CNA_SongHandle* const outSong)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr) {
            return InvalidInput("The song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index < 0 ||
            index >= static_cast<int32_t>(resource->value->getCountProperty())) {
            return InvalidInput("The song index is out of range.");
        }
        const Song* const element =
            (*resource->value)[static_cast<SharpRuntime::intcs>(index)];
        // The canonical element is a bare pointer; the matching retained resource is what the new
        // handle must share, so the returned handle keeps the same song alive.
        for (const std::shared_ptr<SongResource>& song : resource->songs) {
            if (song->value == element) {
                return PublishSong(song, outSong);
            }
        }
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The collection element does not match a retained song.");
    });
}

CNA_Result cna_song_collection_get_count(
    const CNA_SongCollectionHandle collection,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The song collection count output is null.");
        }
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(resource->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_get_is_disposed(
    const CNA_SongCollectionHandle collection,
    CNA_Bool* const outDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDisposed == nullptr) {
            return InvalidInput("The song collection disposal-state output is null.");
        }
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDisposed = resource->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_dispose(const CNA_SongCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_destroy(const CNA_SongCollectionHandle collection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(collection);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                CNA::C::Detail::ErrorCategoryForResult(result),
                "The song collection handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_get_type_name_size(
    const CNA_SongCollectionHandle collection,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The song collection type-name byte-count output is null.");
        }
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_song_collection_copy_type_name(
    const CNA_SongCollectionHandle collection,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The song collection type-name output is invalid.");
        }
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(collection, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}
