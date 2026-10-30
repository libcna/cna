// SPDX-License-Identifier: MS-PL

#include "CNA/C/video.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

#include <any>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateGameFrameTexture2D;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::IoError;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

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
    // The game the player was created for: its frames are that game's to draw.
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result BorrowVideo(
    const CNA_VideoHandle handle,
    std::shared_ptr<VideoResource>* const outVideo)
{
    return LookupHandle(handle, ObjectKind::Video, outVideo);
}

[[nodiscard]] CNA_Result BorrowPlayer(
    const CNA_VideoPlayerHandle handle,
    std::shared_ptr<VideoPlayerResource>* const outPlayer)
{
    return LookupHandle(handle, ObjectKind::VideoPlayer, outPlayer);
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
        return InvalidArgument("The video soundtrack identity is undefined.");
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
    return CreateResourceHandle<VideoResource>(ObjectKind::Video, std::move(video), outVideo);
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

[[nodiscard]] CNA_Result VideoInfoInit(CNA_VideoInfo* const outInfo)
{
    CNA_VideoInfo info = {};
    info.struct_size = sizeof(CNA_VideoInfo);
    info.struct_version = StructureVersion;
    *outInfo = info;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VideoCreate(
    const CNA_Handle deviceHandle,
    const CNA_StringView fileName,
    CNA_VideoHandle* const outVideo)
{
    std::shared_ptr<BorrowedGraphicsDevice> device;
    if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeName;
    if (const CNA_Result result = CNA::C::Detail::ReadText(
            fileName, "The video file name is not valid UTF-8.", &nativeName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return PublishVideo(
        std::make_unique<Video>(std::move(nativeName), device->value),
        outVideo);
}

[[nodiscard]] CNA_Result VideoCreateWithMetadata(
    const CNA_Handle deviceHandle,
    const CNA_StringView fileName,
    const int32_t durationMilliseconds,
    const int32_t width,
    const int32_t height,
    const float framesPerSecond,
    const CNA_VideoSoundtrackType soundtrackType,
    CNA_VideoHandle* const outVideo)
{
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
    if (const CNA_Result result = CNA::C::Detail::ReadText(
            fileName, "The video file name is not valid UTF-8.", &nativeName);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result VideoCreateFromUriExt(
    const CNA_Handle deviceHandle,
    const CNA_StringView uri,
    CNA_VideoHandle* const outVideo)
{
    std::shared_ptr<BorrowedGraphicsDevice> device;
    if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeUri;
    if (const CNA_Result result =
            CNA::C::Detail::ReadText(uri, "The video URI is not valid UTF-8.", &nativeUri);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return PublishVideo(
        std::unique_ptr<Video>(Video::FromUriEXT(nativeUri, device->value)),
        outVideo);
}

[[nodiscard]] CNA_Result ContentManagerLoadVideo(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_VideoHandle* const outVideo)
{
    std::string assetNameCopy;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameCopy,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (assetNameCopy.empty()) {
        return InvalidArgument("The content asset name must not be empty.");
    }
    CNA::C::Detail::BorrowedContentManager contentManager;
    if (const CNA_Result result = CNA::C::Detail::BorrowContentManager(
            contentManagerHandle, &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    try {
        Video loaded = contentManager.value->Load<Video>(assetNameCopy);
        return PublishVideo(std::make_unique<Video>(std::move(loaded)), outVideo);
    } catch (const Microsoft::Xna::Framework::Content::ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // A typed load reports a foreign root object as an I/O failure, as the other typed C
        // loaders do, rather than allowing the exception barrier to call it an internal fault.
        return IoError("The asset's root type reader did not produce a Video.");
    }
}

[[nodiscard]] CNA_Result VideoGetInfo(const CNA_VideoHandle video, CNA_VideoInfo* const outInfo)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(outInfo, "The video metadata structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReadVideo(video, [&](const Video& value) {
        outInfo->width = static_cast<int32_t>(value.getWidthProperty());
        outInfo->height = static_cast<int32_t>(value.getHeightProperty());
        outInfo->fps = static_cast<double>(value.getFramesPerSecondProperty());
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result VideoPlayerCreate(
    const CNA_Handle gameHandle,
    CNA_VideoPlayerHandle* const outPlayer)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<VideoPlayerResource>();
    resource->value = std::make_unique<VideoPlayer>();
    resource->parentGame = gameHandle;
    return CreateHandle(ObjectKind::VideoPlayer,
        std::move(resource),
        outPlayer);
}

[[nodiscard]] CNA_Result VideoPlayerGetVideo(
    const CNA_VideoPlayerHandle player,
    CNA_VideoHandle* const outVideo,
    CNA_Bool* const outAvailable)
{
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
}

[[nodiscard]] CNA_Result VideoPlayerGetFrameExt(
    const CNA_VideoPlayerHandle player,
    CNA_VideoFrameEXT* const outFrame)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outFrame, "The video frame descriptor is null or malformed.",
            CNA_VIDEO_FRAME_EXT_STRUCT_VERSION);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outFrame->texture = CNA_INVALID_HANDLE;
    outFrame->generation = 0U;
    outFrame->presentation_time = -1.0;
    outFrame->available = CNA_FALSE;
    outFrame->reserved[0] = 0U;
    outFrame->reserved[1] = 0U;
    outFrame->reserved[2] = 0U;

    std::shared_ptr<VideoPlayerResource> resource;
    if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // GetTexture may advance decoding; read the identity afterwards so it describes that frame.
    Texture2D* const frame = resource->value->GetTexture();
    outFrame->generation = resource->value->GetFrameGenerationEXT();
    outFrame->presentation_time = resource->value->GetFramePresentationTimeEXT();
    if (frame == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    // Same borrow as cna_video_player_get_texture: the player owns the texture and this keeps
    // it alive while the handle exists; the next call on the player releases it.
    const std::shared_ptr<Texture2D> borrowed(resource, frame);
    if (const CNA_Result result =
            CreateGameFrameTexture2D(borrowed, resource->parentGame, &outFrame->texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    resource->frameTexture = outFrame->texture;
    outFrame->available = CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VideoPlayerGetTexture(
    const CNA_VideoPlayerHandle player,
    CNA_Handle* const outTexture,
    CNA_Bool* const outAvailable)
{
    std::shared_ptr<VideoPlayerResource> resource;
    if (const CNA_Result result = BorrowPlayerForCall(player, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Texture2D* const frame = resource->value->GetTexture();
    if (frame == nullptr) {
        // A successfully opened video without a graphics device can have no texture.
        // Fresh/failed-play and disposed-player refusals propagate through the exception barrier.
        *outAvailable = CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    }
    // Aliasing share: the texture stays owned by the player, and this keeps the player's
    // resource alive for as long as the handle exists. The handle is released again by the
    // next call on this player, before anything can replace the frame.
    const std::shared_ptr<Texture2D> borrowed(resource, frame);
    if (const CNA_Result result =
            CreateGameFrameTexture2D(borrowed, resource->parentGame, outTexture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    resource->frameTexture = *outTexture;
    *outAvailable = CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VideoPlayerPlay(
    const CNA_VideoPlayerHandle player,
    const CNA_VideoHandle video)
{
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
}

} // namespace

// The routes generated from modules/c-api/spec/Video.toml.
#include "generated/Video.inc"
