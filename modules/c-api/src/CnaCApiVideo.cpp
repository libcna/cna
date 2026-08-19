// SPDX-License-Identifier: MS-PL

#include "CNA/C/video.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateStandaloneTexture2D;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateCanonicalBool;

namespace {

using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

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
        return InvalidInput("The video text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the video text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

struct VideoResource final {
    std::unique_ptr<Video> value;
};

// The player owns and replaces its frame texture, so the C layer hands out at most one frame
// handle at a time and invalidates it before anything that could replace the texture. A stale
// frame handle then fails deterministically instead of pointing at freed memory.
struct VideoPlayerResource final {
    std::unique_ptr<VideoPlayer> value;
    std::shared_ptr<VideoResource> video;
    CNA_Handle playingVideoHandle = CNA_INVALID_HANDLE;
    CNA_Handle frameTexture = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result BorrowVideo(
    const CNA_VideoHandle handle,
    std::shared_ptr<VideoResource>* const outVideo)
{
    const CNA_Result result =
        CNA::C::Detail::GetRuntimeHandles().Get(handle, ObjectKind::Video, outVideo);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The video handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowPlayer(
    const CNA_VideoPlayerHandle handle,
    std::shared_ptr<VideoPlayerResource>* const outPlayer)
{
    const CNA_Result result =
        CNA::C::Detail::GetRuntimeHandles().Get(handle, ObjectKind::VideoPlayer, outPlayer);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The video player handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

void InvalidateFrameTexture(VideoPlayerResource& player) noexcept
{
    if (player.frameTexture == CNA_INVALID_HANDLE) {
        return;
    }
    (void)CNA::C::Detail::GetRuntimeHandles().Release(player.frameTexture);
    player.frameTexture = CNA_INVALID_HANDLE;
}

/** Borrows a player and drops any frame handle it handed out, which every route must do. */
[[nodiscard]] CNA_Result BorrowPlayerForCall(
    const CNA_VideoPlayerHandle handle,
    std::shared_ptr<VideoPlayerResource>* const outPlayer)
{
    if (const CNA_Result result = BorrowPlayer(handle, outPlayer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    InvalidateFrameTexture(**outPlayer);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToSoundtrackType(
    const CNA_VideoSoundtrackType type,
    VideoSoundtrackType* const outType)
{
    if (type > CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM) {
        return InvalidInput("The video soundtrack identity is undefined.");
    }
    *outType = static_cast<VideoSoundtrackType>(type);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowDevice(
    const CNA_Handle deviceHandle,
    std::shared_ptr<BorrowedGraphicsDevice>* const outDevice)
{
    if (const CNA_Result result = GetBorrowedGraphicsDevice(deviceHandle, outDevice);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The graphics device handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishVideo(
    std::unique_ptr<Video> video,
    CNA_VideoHandle* const outVideo)
{
    auto resource = std::make_shared<VideoResource>();
    resource->value = std::move(video);
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::Video,
        std::move(resource),
        outVideo);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The video handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TRead>
[[nodiscard]] CNA_Result ReadVideo(const CNA_VideoHandle handle, const TRead read)
{
    std::shared_ptr<VideoResource> resource;
    if (const CNA_Result result = BorrowVideo(handle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return read(*resource->value);
}

} // namespace

CNA_Result cna_video_info_init(CNA_VideoInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The video metadata output is null.");
        }
        CNA_VideoInfo info = {};
        info.struct_size = sizeof(CNA_VideoInfo);
        info.struct_version = StructureVersion;
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_create(
    const CNA_Handle deviceHandle,
    const CNA_StringView fileName,
    CNA_VideoHandle* const outVideo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVideo == nullptr) {
            return InvalidInput("The video output is null.");
        }
        *outVideo = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeName;
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(fileName, false, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video file name is not valid UTF-8.");
        }
        return PublishVideo(
            std::make_unique<Video>(std::move(nativeName), device->value),
            outVideo);
    });
}

CNA_Result cna_video_create_with_metadata(
    const CNA_Handle deviceHandle,
    const CNA_StringView fileName,
    const int32_t durationMilliseconds,
    const int32_t width,
    const int32_t height,
    const float framesPerSecond,
    const CNA_VideoSoundtrackType soundtrackType,
    CNA_VideoHandle* const outVideo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVideo == nullptr) {
            return InvalidInput("The video output is null.");
        }
        *outVideo = CNA_INVALID_HANDLE;
        VideoSoundtrackType nativeSoundtrack = VideoSoundtrackType::Music;
        if (const CNA_Result result = ToSoundtrackType(soundtrackType, &nativeSoundtrack);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeName;
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(fileName, false, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video file name is not valid UTF-8.");
        }
        return PublishVideo(
            std::make_unique<Video>(
                std::move(nativeName),
                device->value,
                static_cast<SharpRuntime::intcs>(durationMilliseconds),
                static_cast<SharpRuntime::intcs>(width),
                static_cast<SharpRuntime::intcs>(height),
                framesPerSecond,
                nativeSoundtrack),
            outVideo);
    });
}

CNA_Result cna_video_create_from_uri_ext(
    const CNA_Handle deviceHandle,
    const CNA_StringView uri,
    CNA_VideoHandle* const outVideo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVideo == nullptr) {
            return InvalidInput("The video output is null.");
        }
        *outVideo = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeUri;
        if (const CNA_Result result = CNA::C::Detail::CopyStringView(uri, false, &nativeUri);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video URI is not valid UTF-8.");
        }
        return PublishVideo(
            std::unique_ptr<Video>(Video::FromUriEXT(nativeUri, device->value)),
            outVideo);
    });
}

CNA_Result cna_video_get_width(const CNA_VideoHandle video, int32_t* const outWidth)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWidth == nullptr) {
            return InvalidInput("The video width output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outWidth = static_cast<int32_t>(value.getWidthProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_height(const CNA_VideoHandle video, int32_t* const outHeight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHeight == nullptr) {
            return InvalidInput("The video height output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outHeight = static_cast<int32_t>(value.getHeightProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_frames_per_second(const CNA_VideoHandle video, float* const outFps)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFps == nullptr) {
            return InvalidInput("The video frame-rate output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outFps = value.getFramesPerSecondProperty();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_info(const CNA_VideoHandle video, CNA_VideoInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_VideoInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The video metadata structure is invalid.");
        }
        return ReadVideo(video, [&](const Video& value) {
            outInfo->width = static_cast<int32_t>(value.getWidthProperty());
            outInfo->height = static_cast<int32_t>(value.getHeightProperty());
            outInfo->fps = static_cast<double>(value.getFramesPerSecondProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_soundtrack_type(
    const CNA_VideoHandle video,
    CNA_VideoSoundtrackType* const outType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outType == nullptr) {
            return InvalidInput("The video soundtrack output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outType =
                static_cast<CNA_VideoSoundtrackType>(value.getVideoSoundtrackTypeProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_duration(const CNA_VideoHandle video, int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The video duration output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outTicks = static_cast<int64_t>(value.getDurationProperty().getTicksProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_set_duration(const CNA_VideoHandle video, const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoResource> resource;
        if (const CNA_Result result = BorrowVideo(video, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setDurationProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_set_audio_track_ext(const CNA_VideoHandle video, const int32_t track)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoResource> resource;
        if (const CNA_Result result = BorrowVideo(video, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetAudioTrackEXT(static_cast<SharpRuntime::intcs>(track));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_set_video_track_ext(const CNA_VideoHandle video, const int32_t track)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoResource> resource;
        if (const CNA_Result result = BorrowVideo(video, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetVideoTrackEXT(static_cast<SharpRuntime::intcs>(track));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_get_file_name_size(const CNA_VideoHandle video, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The video file-name byte-count output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outBytes = value.getFileNameProperty().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_copy_file_name(
    const CNA_VideoHandle video,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadVideo(video, [&](const Video& value) {
            return CopyText(value.getFileNameProperty(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_video_get_has_graphics_device(
    const CNA_VideoHandle video,
    CNA_Bool* const outBound)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBound == nullptr) {
            return InvalidInput("The video device-presence output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            // Presence only: a borrowed device handle is valid solely inside the callback that
            // produced it, so handing one back later would be a promise this ABI cannot keep.
            *outBound = value.getGraphicsDeviceProperty() != nullptr ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_get_type_name_size(const CNA_VideoHandle video, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The video type-name byte-count output is null.");
        }
        return ReadVideo(video, [&](const Video& value) {
            *outBytes = value.GetTypeName().size();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_video_copy_type_name(
    const CNA_VideoHandle video,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadVideo(video, [&](const Video& value) {
            return CopyText(value.GetTypeName(), destination, capacity, outBytes);
        });
    });
}

CNA_Result cna_video_destroy(const CNA_VideoHandle video)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoResource> resource;
        if (const CNA_Result result = BorrowVideo(video, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(video);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_create(
    const CNA_Handle gameHandle,
    CNA_VideoPlayerHandle* const outPlayer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayer == nullptr) {
            return InvalidInput("The video player output is null.");
        }
        *outPlayer = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<VideoPlayerResource>();
        resource->value = std::make_unique<VideoPlayer>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::VideoPlayer,
            std::move(resource),
            outPlayer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video player handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_is_disposed(
    const CNA_VideoPlayerHandle player,
    CNA_Bool* const outDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDisposed == nullptr) {
            return InvalidInput("The video player disposal-state output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDisposed = resource->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_is_looped(
    const CNA_VideoPlayerHandle player,
    CNA_Bool* const outLooped)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLooped == nullptr) {
            return InvalidInput("The video player loop-state output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outLooped = resource->value->getIsLoopedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_set_is_looped(
    const CNA_VideoPlayerHandle player,
    const CNA_Bool looped)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(looped, "looped");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setIsLoopedProperty(looped != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_is_muted(
    const CNA_VideoPlayerHandle player,
    CNA_Bool* const outMuted)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMuted == nullptr) {
            return InvalidInput("The video player mute-state output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMuted = resource->value->getIsMutedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_set_is_muted(
    const CNA_VideoPlayerHandle player,
    const CNA_Bool muted)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(muted, "muted");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setIsMutedProperty(muted != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_play_position_ticks(
    const CNA_VideoPlayerHandle player,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The video player position output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks =
            static_cast<int64_t>(resource->value->getPlayPositionProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_state(
    const CNA_VideoPlayerHandle player,
    CNA_MediaState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The video player state output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_MediaState>(resource->value->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_video(
    const CNA_VideoPlayerHandle player,
    CNA_VideoHandle* const outVideo,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVideo == nullptr || outAvailable == nullptr) {
            return InvalidInput("The video player video output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (resource->value->getVideoProperty() == nullptr ||
            resource->playingVideoHandle == CNA_INVALID_HANDLE) {
            *outAvailable = CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        }
        *outVideo = resource->playingVideoHandle;
        *outAvailable = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_volume(
    const CNA_VideoPlayerHandle player,
    float* const outVolume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVolume == nullptr) {
            return InvalidInput("The video player volume output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVolume = resource->value->getVolumeProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_set_volume(const CNA_VideoPlayerHandle player, const float volume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setVolumeProperty(volume);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_texture(
    const CNA_VideoPlayerHandle player,
    CNA_Handle* const outTexture,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr || outAvailable == nullptr) {
            return InvalidInput("The video frame output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Texture2D* const frame = resource->value->GetTexture();
        if (frame == nullptr) {
            // The canonical implementation deliberately answers null before playback rather than
            // faulting the way the original API does, so this is an ordinary answer.
            *outAvailable = CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        }
        // Aliasing share: the texture stays owned by the player, and this keeps the player's
        // resource alive for as long as the handle exists. The handle is released again by the
        // next call on this player, before anything can replace the frame.
        const std::shared_ptr<Texture2D> borrowed(resource, frame);
        if (const CNA_Result result = CreateStandaloneTexture2D(borrowed, outTexture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->frameTexture = *outTexture;
        *outAvailable = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_play(
    const CNA_VideoPlayerHandle player,
    const CNA_VideoHandle video)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<VideoResource> videoResource;
        if (const CNA_Result result = BorrowVideo(video, &videoResource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Play(videoResource->value.get());
        // The player keeps a bare pointer to the video, so the C resource retains it: releasing
        // the caller's handle can then never leave the player pointing at a destroyed video.
        resource->video = videoResource;
        resource->playingVideoHandle = video;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_stop(const CNA_VideoPlayerHandle player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_pause(const CNA_VideoPlayerHandle player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Pause();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_resume(const CNA_VideoPlayerHandle player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Resume();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_set_audio_track_ext(
    const CNA_VideoPlayerHandle player,
    const int32_t track)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetAudioTrackEXT(static_cast<SharpRuntime::intcs>(track));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_set_video_track_ext(
    const CNA_VideoPlayerHandle player,
    const int32_t track)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetVideoTrackEXT(static_cast<SharpRuntime::intcs>(track));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_dispose(const CNA_VideoPlayerHandle player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_destroy(const CNA_VideoPlayerHandle player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(player);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The video player handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_get_type_name_size(
    const CNA_VideoPlayerHandle player,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The video player type-name byte-count output is null.");
        }
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_video_player_copy_type_name(
    const CNA_VideoPlayerHandle player,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VideoPlayerResource> resource;
        if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}
