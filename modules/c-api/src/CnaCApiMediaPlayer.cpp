// SPDX-License-Identifier: MS-PL

#include "CNA/C/media_player.h"
#include "CnaCApiDetail.hpp"
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
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Media::Detail::BorrowMediaChild;
using CNA::C::Media::Detail::MediaChildResource;
using CNA::C::Media::Detail::PublishMediaChild;
using CNA::C::Media::Detail::SongCollectionResource;
using CNA::C::Media::Detail::SongResource;

namespace {

using Microsoft::Xna::Framework::Media::MediaPlayer;
using Microsoft::Xna::Framework::Media::MediaQueue;
using Microsoft::Xna::Framework::Media::MediaState;
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
        return InvalidInput("The media player text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the media player text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

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
        return InvalidInput("The media player registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidInput("The media player event callback is null.");
    }
    const auto token = event.Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    const auto resource = std::make_shared<PlayerRegistration>(kind, token);
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::MediaPlayerEventRegistration,
        resource,
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The media player registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_media_player_get_game_has_control(
    const CNA_Handle gameHandle,
    CNA_Bool* const outHasControl)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasControl == nullptr) {
            return InvalidInput("The media player control-state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outHasControl = MediaPlayer::getGameHasControlProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_is_muted(const CNA_Handle gameHandle, CNA_Bool* const outMuted)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMuted == nullptr) {
            return InvalidInput("The media player mute-state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outMuted = MediaPlayer::getIsMutedProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_set_is_muted(const CNA_Handle gameHandle, const CNA_Bool muted)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(muted, "muted");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::setIsMutedProperty(muted != CNA_FALSE);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_is_repeating(
    const CNA_Handle gameHandle,
    CNA_Bool* const outRepeating)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRepeating == nullptr) {
            return InvalidInput("The media player repeat-state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outRepeating = MediaPlayer::getIsRepeatingProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_set_is_repeating(
    const CNA_Handle gameHandle,
    const CNA_Bool repeating)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(repeating, "repeating");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::setIsRepeatingProperty(repeating != CNA_FALSE);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_is_shuffled(
    const CNA_Handle gameHandle,
    CNA_Bool* const outShuffled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShuffled == nullptr) {
            return InvalidInput("The media player shuffle-state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outShuffled = MediaPlayer::getIsShuffledProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_set_is_shuffled(const CNA_Handle gameHandle, const CNA_Bool shuffled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(shuffled, "shuffled");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::setIsShuffledProperty(shuffled != CNA_FALSE);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_play_position_ticks(
    const CNA_Handle gameHandle,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The media player position output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outTicks = static_cast<int64_t>(
                MediaPlayer::getPlayPositionProperty().getTicksProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_state(
    const CNA_Handle gameHandle,
    CNA_MediaState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The media player state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outState = static_cast<CNA_MediaState>(MediaPlayer::getStateProperty());
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_volume(const CNA_Handle gameHandle, float* const outVolume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVolume == nullptr) {
            return InvalidInput("The media player volume output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outVolume = MediaPlayer::getVolumeProperty();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_set_volume(const CNA_Handle gameHandle, const float volume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            // The canonical setter clamps rather than refusing, so C passes the value through.
            MediaPlayer::setVolumeProperty(volume);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_is_visualization_enabled(
    const CNA_Handle gameHandle,
    CNA_Bool* const outEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnabled == nullptr) {
            return InvalidInput("The visualization-state output is null.");
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            *outEnabled =
                MediaPlayer::getIsVisualizationEnabledProperty() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_set_is_visualization_enabled(
    const CNA_Handle gameHandle,
    const CNA_Bool enabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(enabled, "enabled");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::setIsVisualizationEnabledProperty(enabled != CNA_FALSE);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_get_visualization_data(
    const CNA_Handle gameHandle,
    CNA_VisualizationData* const data)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr || data->struct_size < sizeof(CNA_VisualizationData) ||
            data->struct_version != StructureVersion) {
            return InvalidInput("The visualization data structure is invalid.");
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
            return InvalidInput("The media queue output is null.");
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

CNA_Result cna_media_player_move_next(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::MoveNext();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_move_previous(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::MovePrevious();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_pause(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Pause();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_resume(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Resume();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_stop(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Stop();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_update_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::Update();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_program_exit_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::ProgramExit();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_detect_song_ended_by_elapsed_time_ext(
    const CNA_SongHandle song,
    const int64_t elapsedTicks,
    CNA_Bool* const outEnded)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnded == nullptr) {
            return InvalidInput("The song-ended output is null.");
        }
        std::shared_ptr<SongResource> resource;
        if (const CNA_Result result = BorrowSong(song, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEnded = MediaPlayer::DetectSongEndedByElapsedTime(
                        resource->value,
                        System::TimeSpan(static_cast<SharpRuntime::longcs>(elapsedTicks)))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
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
        const CNA_Result getResult = CNA::C::Detail::GetRuntimeHandles().Get(
            registration,
            ObjectKind::MediaPlayerEventRegistration,
            &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The media player registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The media player registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_player_raise_active_song_changed_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::OnActiveSongChanged();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_player_raise_media_state_changed_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WithGame(gameHandle, [&]() -> CNA_Result {
            MediaPlayer::OnMediaStateChanged();
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_media_queue_get_count(
    const CNA_MediaQueueHandle queue,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The media queue count output is null.");
        }
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(resource->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_get_active_song_index(
    const CNA_MediaQueueHandle queue,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The media queue index output is null.");
        }
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(resource->value->getActiveSongIndexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_set_active_song_index(
    const CNA_MediaQueueHandle queue,
    const int32_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical setter stores the value unchecked, so C does not invent a range rule.
        resource->value->setActiveSongIndexProperty(static_cast<SharpRuntime::intcs>(index));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_get_active_song(
    const CNA_MediaQueueHandle queue,
    CNA_SongHandle* const outSong,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSong == nullptr || outAvailable == nullptr) {
            return InvalidInput("The active song output is null.");
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
            return InvalidInput("The media queue song output is null.");
        }
        *outSong = CNA_INVALID_HANDLE;
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index < 0 || index >= static_cast<int32_t>(resource->value->getCountProperty())) {
            return InvalidInput("The media queue index is out of range.");
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

CNA_Result cna_media_queue_clear(const CNA_MediaQueueHandle queue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_destroy(const CNA_MediaQueueHandle queue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(queue);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The media queue handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_get_type_name_size(
    const CNA_MediaQueueHandle queue,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The media queue type-name byte-count output is null.");
        }
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_media_queue_copy_type_name(
    const CNA_MediaQueueHandle queue,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MediaQueueResource> resource;
        if (const CNA_Result result = BorrowQueue(queue, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}
