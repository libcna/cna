// SPDX-License-Identifier: MS-PL

#include "CNA/C/media.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"
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
