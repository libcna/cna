// SPDX-License-Identifier: MS-PL

#include "CNA/C/media_player.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMediaDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/MediaQueue.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Media::Detail::BorrowMediaChild;
using CNA::C::Media::Detail::MediaChildResource;
using CNA::C::Media::Detail::PublishMediaChild;
using CNA::C::Media::Detail::SongCollectionResource;
using CNA::C::Media::Detail::SongResource;

namespace {

using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReleaseHandle;

using Microsoft::Xna::Framework::Media::MediaPlayer;
using Microsoft::Xna::Framework::Media::MediaQueue;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;
using Microsoft::Xna::Framework::Media::VisualizationData;

constexpr uint32_t StructureVersion = UINT32_C(1);

// The queue is one process-lifetime object owned by the player, so a handle to it is a view: the
// resource points at the singleton and owns nothing.
using MediaQueueResource = MediaChildResource<MediaQueue>;

[[nodiscard]] CNA_Result BorrowQueue(
    const CNA_MediaQueueHandle handle,
    std::shared_ptr<MediaQueueResource>* const outQueue)
{
    return BorrowMediaChild<MediaQueue>(
        handle,
        ObjectKind::MediaQueue,
        "The media queue handle is invalid for this call.",
        outQueue);
}

[[nodiscard]] CNA_Result BorrowSong(
    const CNA_SongHandle handle,
    std::shared_ptr<SongResource>* const outSong)
{
    return BorrowMediaChild<Song>(
        handle,
        ObjectKind::Song,
        "The song handle is invalid for this call.",
        outSong);
}

[[nodiscard]] CNA_Result BorrowSongCollection(
    const CNA_SongCollectionHandle handle,
    std::shared_ptr<SongCollectionResource>* const outCollection)
{
    return BorrowMediaChild<SongCollection>(
        handle,
        ObjectKind::SongCollection,
        "The song collection handle is invalid for this call.",
        outCollection);
}

// A queue entry is copied out rather than borrowed: the canonical queue destroys its entries
// whenever it is cleared -- which every play route does -- so a borrowed handle would dangle. The
// copy carries the same file and name, which is exactly what the player itself enqueues.
[[nodiscard]] CNA_Result PublishSongCopy(const Song& song, CNA_SongHandle* const outSong)
{
    auto resource = std::make_shared<SongResource>();
    auto copy = std::make_unique<Song>(song.getHandle(), song.getNameProperty());
    resource->value = copy.get();
    resource->owned = std::move(copy);
    return PublishMediaChild<Song>(
        ObjectKind::Song,
        std::move(resource),
        "The song handle could not be created.",
        outSong);
}

template<typename TApply>
[[nodiscard]] CNA_Result WithGame(const CNA_Handle gameHandle, const TApply apply)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return apply();
}

/** Which of the two process-wide player events a registration detaches from. */
enum class PlayerEventKind { ActiveSongChanged, MediaStateChanged };

// Both canonical events are static, so a registration owns only its subscription token.
class PlayerRegistration final {
public:
    using Token = System::EventHandler<System::EventArgs>::Token;

    PlayerRegistration(const PlayerEventKind kind, const Token token)
        : kind_(kind)
        , token_(token)
    {
    }

    PlayerRegistration(const PlayerRegistration&) = delete;
    PlayerRegistration& operator=(const PlayerRegistration&) = delete;

    ~PlayerRegistration()
    {
        switch (kind_) {
        case PlayerEventKind::ActiveSongChanged:
            (void)MediaPlayer::ActiveSongChanged.Remove(token_);
            break;
        case PlayerEventKind::MediaStateChanged:
            (void)MediaPlayer::MediaStateChanged.Remove(token_);
            break;
        }
    }

private:
    PlayerEventKind kind_;
    Token token_;
};

[[nodiscard]] CNA_Result SubscribePlayerEvent(
    System::EventHandler<System::EventArgs>& event,
    const PlayerEventKind kind,
    const CNA_MediaPlayerEventCallback callback,
    void* const context,
    CNA_MediaPlayerEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidArgument("The media player registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidArgument("The media player event callback is null.");
    }
    const auto token = event.Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    const auto resource = std::make_shared<PlayerRegistration>(kind, token);
    return CreateHandle(ObjectKind::MediaPlayerEventRegistration,
        resource,
        outRegistration);
}

} // namespace

CNA_Result cna_media_player_get_visualization_data(
    const CNA_Handle gameHandle,
    CNA_VisualizationData* const data)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr || data->struct_size < sizeof(CNA_VisualizationData) ||
            data->struct_version != StructureVersion) {
            return InvalidArgument("The visualization data structure is invalid.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            VisualizationData native;
            const auto& frequencies = native.getFrequenciesProperty();
            const auto& samples = native.getSamplesProperty();
            for (std::size_t index = 0U;
                 index < static_cast<std::size_t>(VisualizationData::Size);
                 ++index) {
                native.freq[index] = data->frequencies[index];
                native.samp[index] = data->samples[index];
            }
            MediaPlayer::GetVisualizationData(native);
            for (std::size_t index = 0U;
                 index < static_cast<std::size_t>(VisualizationData::Size);
                 ++index) {
                data->frequencies[index] = frequencies[index];
                data->samples[index] = samples[index];
            }
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_queue(
    const CNA_Handle gameHandle,
    CNA_MediaQueueHandle* const outQueue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outQueue == nullptr) {
            return InvalidArgument("The media queue output is null.");
        }
        *outQueue = CNA_INVALID_HANDLE;
        return WithGame(gameHandle, [&]() -> CNA_Result {
            auto resource = std::make_shared<MediaQueueResource>();
            resource->value = &MediaPlayer::getQueueProperty();
            return PublishMediaChild<MediaQueue>(
                ObjectKind::MediaQueue,
                std::move(resource),
                "The media queue handle could not be created.",
                outQueue);
        });
    });
}

CNA_Result cna_media_player_play_song(const CNA_Handle gameHandle, const CNA_SongHandle song)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Play(resource->value);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_play_songs(
    const CNA_Handle gameHandle,
    const CNA_SongCollectionHandle songs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(songs, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Play(*resource->value);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_play_songs_from(
    const CNA_Handle gameHandle,
    const CNA_SongCollectionHandle songs,
    const int32_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SongCollectionResource> resource;
        if (const CNA_Result result = BorrowSongCollection(songs, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            // The canonical overload does not range-check the index, so neither does this route.
            MediaPlayer::Play(*resource->value, static_cast<SharpRuntime::intcs>(index));
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_subscribe_active_song_changed_ext(
    const CNA_MediaPlayerEventCallback callback,
    void* const context,
    CNA_MediaPlayerEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribePlayerEvent(
            MediaPlayer::ActiveSongChanged,
            PlayerEventKind::ActiveSongChanged,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_media_player_subscribe_media_state_changed_ext(
    const CNA_MediaPlayerEventCallback callback,
    void* const context,
    CNA_MediaPlayerEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribePlayerEvent(
            MediaPlayer::MediaStateChanged,
            PlayerEventKind::MediaStateChanged,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_media_player_unsubscribe_ext(
    const CNA_MediaPlayerEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PlayerRegistration> resource;
        const CNA_Result getResult = LookupHandle(registration, ObjectKind::MediaPlayerEventRegistration, &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return getResult;
        }
        return ReleaseHandle(registration);
    });
}

CNA_Result cna_media_queue_get_active_song(
    const CNA_MediaQueueHandle queue,
    CNA_SongHandle* const outSong,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr || outAvailable == nullptr) {
            return InvalidArgument("The active song output is null.");
        }
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Song* const active = resource->value->getActiveSongProperty();
        if (active == nullptr) {
            *outAvailable = CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = PublishSongCopy(*active, outSong);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAvailable = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_get_at(
    const CNA_MediaQueueHandle queue,
    const int32_t index,
    CNA_SongHandle* const outSong)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr) {
            return InvalidArgument("The media queue song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index < 0 || index >= static_cast<int32_t>(resource->value->getCountProperty())) {
            return InvalidArgument("The media queue index is out of range.");
        }
        const Song* const song = (*resource->value)[static_cast<SharpRuntime::intcs>(index)];
        if (song == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The media queue holds no song at this index.");
        }
        return PublishSongCopy(*song, outSong);
    });
}

CNA_Result cna_media_queue_add(const CNA_MediaQueueHandle queue, const CNA_SongHandle song)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaQueueResource> queueResource;
        if (const CNA_Result result = BorrowQueue(queue, &queueResource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SongResource> songResource;
        if (const CNA_Result result = BorrowSong(song, &songResource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical Add adopts the pointer it is given. A handle's object cannot be handed
        // away without leaving the caller a stale handle, so C appends a copy -- which is exactly
        // what the canonical player does when it enqueues a song.
        queueResource->value->Add(new Song(
            songResource->value->getHandle(),
            songResource->value->getNameProperty()));
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/MediaPlayer.toml.
#include "generated/MediaPlayer.inc"
