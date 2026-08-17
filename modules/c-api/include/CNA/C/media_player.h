// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MEDIA_PLAYER_H
#define CNA_C_MEDIA_PLAYER_H

#include "CNA/C/media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Borrowed handle to the process-wide media queue.
 *
 * There is exactly one queue, owned by the media player for the lifetime of the process, so this
 * handle is a view of it rather than something a caller owns — the same shape a stock mouse cursor
 * uses. Releasing the handle releases only the view.
 */
typedef CNA_Handle CNA_MediaQueueHandle;

/** @brief Owned handle to one media-player event subscription. */
typedef CNA_Handle CNA_MediaPlayerEventRegistrationHandle;

/**
 * @brief Handler invoked when a media-player event is raised.
 *
 * @param context The caller context supplied at subscription time.
 *
 * Neither canonical event carries data, so the handler receives only its context.
 */
typedef void (*CNA_MediaPlayerEventCallback)(void* context);

/**
 * @brief Reports whether the game controls its own background music.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_has_control Receives `CNA_TRUE` when the game controls music playback.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_game_has_control(
    CNA_Handle game,
    CNA_Bool* out_has_control);

/**
 * @brief Reports whether song playback is muted.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_muted Receives `CNA_TRUE` when playback is muted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_is_muted(CNA_Handle game, CNA_Bool* out_muted);

/**
 * @brief Sets whether song playback is muted.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param muted New muted state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_set_is_muted(CNA_Handle game, CNA_Bool muted);

/**
 * @brief Reports whether the playback queue repeats.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_repeating Receives `CNA_TRUE` when the queue repeats.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_is_repeating(CNA_Handle game, CNA_Bool* out_repeating);

/**
 * @brief Sets whether the playback queue repeats.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param repeating New repeating state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_set_is_repeating(CNA_Handle game, CNA_Bool repeating);

/**
 * @brief Reports whether the playback queue is shuffled.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_shuffled Receives `CNA_TRUE` when the queue is shuffled.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_is_shuffled(CNA_Handle game, CNA_Bool* out_shuffled);

/**
 * @brief Sets whether the playback queue is shuffled.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param shuffled New shuffled state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_set_is_shuffled(CNA_Handle game, CNA_Bool shuffled);

/**
 * @brief Returns the playback position within the active song.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_ticks Receives the position in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_play_position_ticks(
    CNA_Handle game,
    int64_t* out_ticks);

/**
 * @brief Returns the current playback state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_state Receives one `CNA_MEDIA_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_state(CNA_Handle game, CNA_MediaState* out_state);

/**
 * @brief Returns the current playback volume.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_volume Receives the volume in the range 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_volume(CNA_Handle game, float* out_volume);

/**
 * @brief Sets the playback volume.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param volume New volume; the canonical setter **clamps** it to the range 0 through 1 rather
 *        than refusing an out-of-range value, and this route preserves that.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_set_volume(CNA_Handle game, float volume);

/**
 * @brief Reports whether visualization data collection is enabled.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_enabled Receives `CNA_TRUE` when visualization is enabled.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_is_visualization_enabled(
    CNA_Handle game,
    CNA_Bool* out_enabled);

/**
 * @brief Enables or disables visualization data collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param enabled New visualization state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_set_is_visualization_enabled(
    CNA_Handle game,
    CNA_Bool enabled);

/**
 * @brief Fills a visualization buffer for the active song.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param data Caller-provided buffer; both arrays are filled, and stay as they were when the
 *        renderer supplies no data.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid structure, or
 *         a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_get_visualization_data(
    CNA_Handle game,
    CNA_VisualizationData* data);

/**
 * @brief Returns a view of the process-wide media queue.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_queue Receives a borrowed queue handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_player_get_queue(CNA_Handle game, CNA_MediaQueueHandle* out_queue);

/**
 * @brief Clears the queue, enqueues one song and starts playback.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param song Song to play; it stays the caller's.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the song's file has disappeared, or a
 *         documented argument/handle/thread/native failure.
 *
 * The canonical player **copies** the song into its queue rather than taking it, so the caller's
 * handle is unaffected and remains its own to release.
 */
CNA_C_API CNA_Result cna_media_player_play_song(CNA_Handle game, CNA_SongHandle song);

/**
 * @brief Clears the queue, enqueues a collection and starts at its first song.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param songs Collection to enqueue; it and its songs stay the caller's.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO`, or a documented argument/handle/thread/native
 *         failure.
 */
CNA_C_API CNA_Result cna_media_player_play_songs(
    CNA_Handle game,
    CNA_SongCollectionHandle songs);

/**
 * @brief Clears the queue, enqueues a collection and starts at one index.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param songs Collection to enqueue; it and its songs stay the caller's.
 * @param index Zero-based index of the first song to play.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO`, or a documented argument/handle/thread/native
 *         failure.
 *
 * The canonical overload does not range-check the index — an out-of-range one simply leaves no
 * active song — and this route preserves that rather than inventing a refusal.
 */
CNA_C_API CNA_Result cna_media_player_play_songs_from(
    CNA_Handle game,
    CNA_SongCollectionHandle songs,
    int32_t index);

/**
 * @brief Advances playback to the next song in the queue.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_move_next(CNA_Handle game);

/**
 * @brief Moves playback back to the previous song in the queue.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_move_previous(CNA_Handle game);

/**
 * @brief Pauses playback when a song is playing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Pausing when nothing is playing is a successful no-op, exactly as the canonical operation is.
 */
CNA_C_API CNA_Result cna_media_player_pause(CNA_Handle game);

/**
 * @brief Resumes playback from a paused state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Resuming when not paused is a successful no-op.
 */
CNA_C_API CNA_Result cna_media_player_resume(CNA_Handle game);

/**
 * @brief Stops playback and resets the queue's playback state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_stop(CNA_Handle game);

/**
 * @brief Performs pending media-player maintenance.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This is the canonical timer and state-transition pump. A game that runs the CNA loop does not
 * need it; a C application driving playback outside a frame does.
 */
CNA_C_API CNA_Result cna_media_player_update_ext(CNA_Handle game);

/**
 * @brief Releases renderer media resources.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical helper is meant for application exit; calling it earlier simply releases whatever
 * the player had initialized, and playback can start again afterwards.
 */
CNA_C_API CNA_Result cna_media_player_program_exit_ext(CNA_Handle game);

/**
 * @brief Reports whether a song should be considered ended after an elapsed time.
 *
 * @param song Song handle to test.
 * @param elapsed_ticks Elapsed playback time in 100-nanosecond ticks.
 * @param out_ended Receives `CNA_TRUE` when the song should be considered ended.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This is the fallback song-end detector for builds with no native track-stopped signal. It only
 * reports an end when the song's duration is genuinely known: a song whose duration is zero never
 * reports ended, rather than reporting it immediately.
 */
CNA_C_API CNA_Result cna_media_player_detect_song_ended_by_elapsed_time_ext(
    CNA_SongHandle song,
    int64_t elapsed_ticks,
    CNA_Bool* out_ended);

/**
 * @brief Subscribes to the active-song-changed event.
 *
 * @param callback Handler invoked when the active song changes.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event is **static**, so the subscription belongs to the process rather than to a
 * game and takes no game handle. Release it with `cna_media_player_unsubscribe_ext`.
 */
CNA_C_API CNA_Result cna_media_player_subscribe_active_song_changed_ext(
    CNA_MediaPlayerEventCallback callback,
    void* context,
    CNA_MediaPlayerEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the media-state-changed event.
 *
 * @param callback Handler invoked when the playback state changes.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_media_player_subscribe_media_state_changed_ext(
    CNA_MediaPlayerEventCallback callback,
    void* context,
    CNA_MediaPlayerEventRegistrationHandle* out_registration);

/**
 * @brief Releases a media-player event registration.
 *
 * @param registration Owned registration handle from either subscribe route.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 *
 * One route releases both events, because a registration already knows which one it came from.
 */
CNA_C_API CNA_Result cna_media_player_unsubscribe_ext(
    CNA_MediaPlayerEventRegistrationHandle registration);

/**
 * @brief Raises the active-song-changed event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This maps the canonical deferred raise the framework dispatcher calls, so a C application can
 * observe its own wiring without waiting for a song to change. Every subscribed handler runs
 * synchronously before the call returns.
 */
CNA_C_API CNA_Result cna_media_player_raise_active_song_changed_ext(CNA_Handle game);

/**
 * @brief Raises the media-state-changed event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_player_raise_media_state_changed_ext(CNA_Handle game);

/**
 * @brief Returns how many songs the queue holds.
 *
 * @param queue Borrowed queue handle.
 * @param out_count Receives the song count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_queue_get_count(CNA_MediaQueueHandle queue, int32_t* out_count);

/**
 * @brief Returns the zero-based index of the active song.
 *
 * @param queue Borrowed queue handle.
 * @param out_index Receives the index, which is -1 when the queue is empty.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_queue_get_active_song_index(
    CNA_MediaQueueHandle queue,
    int32_t* out_index);

/**
 * @brief Sets the zero-based index of the active song.
 *
 * @param queue Borrowed queue handle.
 * @param index New active song index.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical setter stores the value without range-checking it, and this route preserves that.
 */
CNA_C_API CNA_Result cna_media_queue_set_active_song_index(
    CNA_MediaQueueHandle queue,
    int32_t index);

/**
 * @brief Returns a copy of the active song.
 *
 * @param queue Borrowed queue handle.
 * @param out_song Receives an owned song handle when a song is active; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the queue has an active song.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **The song is an independently owned copy, not a view into the queue.** The canonical queue
 * destroys its entries whenever it is cleared — which every `play` route does — so a borrowed
 * handle would dangle. The copy carries the same file and name, and therefore compares equal to
 * the entry, exactly as the player's own internal copy of a song does. Release it with
 * `cna_song_destroy`.
 */
CNA_C_API CNA_Result cna_media_queue_get_active_song(
    CNA_MediaQueueHandle queue,
    CNA_SongHandle* out_song,
    CNA_Bool* out_available);

/**
 * @brief Returns a copy of the song at an index.
 *
 * @param queue Borrowed queue handle.
 * @param index Zero-based index below the current count.
 * @param out_song Receives an owned song handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 *
 * As with the active song, this is an independently owned copy rather than a view into the queue.
 */
CNA_C_API CNA_Result cna_media_queue_get_at(
    CNA_MediaQueueHandle queue,
    int32_t index,
    CNA_SongHandle* out_song);

/**
 * @brief Appends a copy of a song to the queue.
 *
 * @param queue Borrowed queue handle.
 * @param song Song to append; it stays the caller's.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the song's file has disappeared, or a
 *         documented argument/handle/thread/native failure.
 *
 * The canonical operation takes ownership of the pointer it is given. C cannot hand a handle's
 * object away — the caller would be left holding a stale handle — so this route appends a copy,
 * which is exactly what the canonical player itself does when it enqueues a song.
 */
CNA_C_API CNA_Result cna_media_queue_add(CNA_MediaQueueHandle queue, CNA_SongHandle song);

/**
 * @brief Removes every song from the queue and resets its active index.
 *
 * @param queue Borrowed queue handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_queue_clear(CNA_MediaQueueHandle queue);

/**
 * @brief Releases a queue handle.
 *
 * @param queue Borrowed queue handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Only the view is released; the process-wide queue itself is untouched.
 */
CNA_C_API CNA_Result cna_media_queue_destroy(CNA_MediaQueueHandle queue);

/**
 * @brief Returns the byte count of the queue type's fully-qualified .NET type name.
 *
 * @param queue Borrowed queue handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_queue_get_type_name_size(
    CNA_MediaQueueHandle queue,
    uint64_t* out_bytes);

/**
 * @brief Copies the queue type's fully-qualified .NET type name.
 *
 * @param queue Borrowed queue handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_queue_copy_type_name(
    CNA_MediaQueueHandle queue,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
