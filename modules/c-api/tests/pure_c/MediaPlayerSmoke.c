// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>
#include <threads.h>

#ifndef CNA_C_API_MEDIA_FIXTURE_MUSIC
#error "CNA_C_API_MEDIA_FIXTURE_MUSIC must name the fixture music directory"
#endif

typedef struct PlayerSmokeState {
    int validated;
} PlayerSmokeState;

typedef struct EventState {
    int active_song_calls;
    int state_calls;
} EventState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView view(const char* const value)
{
    CNA_StringView result;
    result.data = value;
    result.byte_length = (uint64_t)strlen(value);
    return result;
}

static int fixture_song_path(char* const buffer, const size_t capacity)
{
    static const char name[] = "/first_track.mp3";
    if (strlen(CNA_C_API_MEDIA_FIXTURE_MUSIC) + sizeof(name) > capacity) {
        return 0;
    }
    strcpy(buffer, CNA_C_API_MEDIA_FIXTURE_MUSIC);
    strcat(buffer, name);
    return 1;
}

static int is_defined_state(const CNA_MediaState state)
{
    return state == CNA_MEDIA_STATE_STOPPED || state == CNA_MEDIA_STATE_PLAYING ||
        state == CNA_MEDIA_STATE_PAUSED;
}

/* Every one of these is process-wide state the suite does not own, so it is put back afterwards. */
static int validate_player_settings(const CNA_Handle game)
{
    CNA_Bool muted = UINT8_C(9);
    CNA_Bool repeating = UINT8_C(9);
    CNA_Bool shuffled = UINT8_C(9);
    CNA_Bool visualization = UINT8_C(9);
    CNA_Bool control = UINT8_C(9);
    CNA_Bool flag = UINT8_C(9);
    float volume = -1.0F;
    float restored = -1.0F;

    if (cna_media_player_get_game_has_control(game, &control) != CNA_RESULT_SUCCESS ||
        (control != CNA_FALSE && control != CNA_TRUE) ||
        cna_media_player_get_is_muted(game, &muted) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_repeating(game, &repeating) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_shuffled(game, &shuffled) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_visualization_enabled(game, &visualization) !=
            CNA_RESULT_SUCCESS ||
        cna_media_player_get_volume(game, &volume) != CNA_RESULT_SUCCESS ||
        volume < 0.0F || volume > 1.0F) {
        return 0;
    }

    /* Each flag round-trips through its own setter. */
    if (cna_media_player_set_is_muted(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_muted(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_media_player_set_is_muted(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_muted(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_media_player_set_is_repeating(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_repeating(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_media_player_set_is_shuffled(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_shuffled(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_media_player_set_is_visualization_enabled(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_is_visualization_enabled(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }

    /* The canonical volume setter clamps rather than refusing, and C preserves that. */
    if (cna_media_player_set_volume(game, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_volume(game, &restored) != CNA_RESULT_SUCCESS ||
        restored != 0.25F ||
        cna_media_player_set_volume(game, 5.0F) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_volume(game, &restored) != CNA_RESULT_SUCCESS ||
        restored != 1.0F ||
        cna_media_player_set_volume(game, -3.0F) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_volume(game, &restored) != CNA_RESULT_SUCCESS ||
        restored != 0.0F) {
        return 0;
    }

    /* Visualization fills a caller-provided value; with no renderer data it stays as it was. */
    {
        CNA_VisualizationData data;
        CNA_VisualizationData invalid;
        if (cna_visualization_data_init(&data) != CNA_RESULT_SUCCESS ||
            cna_media_player_get_visualization_data(game, &data) != CNA_RESULT_SUCCESS ||
            cna_media_player_get_visualization_data(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        invalid = data;
        invalid.struct_version = UINT32_C(2);
        if (cna_media_player_get_visualization_data(game, &invalid) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* Put every process-wide setting back the way it was found. */
    return cna_media_player_set_is_muted(game, muted) == CNA_RESULT_SUCCESS &&
        cna_media_player_set_is_repeating(game, repeating) == CNA_RESULT_SUCCESS &&
        cna_media_player_set_is_shuffled(game, shuffled) == CNA_RESULT_SUCCESS &&
        cna_media_player_set_is_visualization_enabled(game, visualization) ==
            CNA_RESULT_SUCCESS &&
        cna_media_player_set_volume(game, volume) == CNA_RESULT_SUCCESS &&
        cna_media_player_get_is_muted(game, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_player_get_volume(game, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_player_get_state(game, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static void on_active_song_changed(void* const context)
{
    ++((EventState*)context)->active_song_calls;
}

static void on_media_state_changed(void* const context)
{
    ++((EventState*)context)->state_calls;
}

static int validate_player_events(const CNA_Handle game)
{
    EventState events = {0, 0};
    CNA_MediaPlayerEventRegistrationHandle active = CNA_INVALID_HANDLE;
    CNA_MediaPlayerEventRegistrationHandle changed = CNA_INVALID_HANDLE;
    CNA_MediaPlayerEventRegistrationHandle rejected = CNA_INVALID_HANDLE;

    if (cna_media_player_subscribe_active_song_changed_ext(0, &events, &active) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_player_subscribe_active_song_changed_ext(on_active_song_changed, &events, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_player_subscribe_active_song_changed_ext(
            on_active_song_changed, &events, &active) != CNA_RESULT_SUCCESS ||
        cna_media_player_subscribe_media_state_changed_ext(
            on_media_state_changed, &events, &changed) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Each raise reaches its own handler synchronously. */
    if (cna_media_player_raise_active_song_changed_ext(game) != CNA_RESULT_SUCCESS ||
        events.active_song_calls != 1 || events.state_calls != 0 ||
        cna_media_player_raise_media_state_changed_ext(game) != CNA_RESULT_SUCCESS ||
        events.state_calls != 1 || events.active_song_calls != 1) {
        return 0;
    }
    /* One shared release route detaches exactly the subscription it was handed. */
    if (cna_media_player_unsubscribe_ext(active) != CNA_RESULT_SUCCESS ||
        cna_media_player_unsubscribe_ext(active) != CNA_RESULT_INVALID_HANDLE ||
        cna_media_player_raise_active_song_changed_ext(game) != CNA_RESULT_SUCCESS ||
        events.active_song_calls != 1 ||
        cna_media_player_raise_media_state_changed_ext(game) != CNA_RESULT_SUCCESS ||
        events.state_calls != 2) {
        return 0;
    }
    return cna_media_player_unsubscribe_ext(changed) == CNA_RESULT_SUCCESS &&
        cna_media_player_unsubscribe_ext(rejected) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_queue_and_playback(const CNA_Handle game)
{
    CNA_MediaQueueHandle queue = CNA_INVALID_HANDLE;
    CNA_MediaQueueHandle rejected = CNA_INVALID_HANDLE;
    CNA_SongHandle song = CNA_INVALID_HANDLE;
    CNA_SongHandle copy = CNA_INVALID_HANDLE;
    CNA_SongCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_MediaState state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    int32_t count = 9;
    int32_t index = 9;
    int64_t ticks = INT64_C(-1);
    uint64_t bytes = UINT64_C(9);
    char path[1024];
    char text[256];

    if (!fixture_song_path(path, sizeof(path)) ||
        cna_song_create(game, view(path), view("Queued"), &song) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_media_player_get_queue(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_player_get_queue(game, &queue) != CNA_RESULT_SUCCESS ||
        queue == CNA_INVALID_HANDLE ||
        cna_media_queue_clear(queue) != CNA_RESULT_SUCCESS ||
        cna_media_queue_get_count(queue, &count) != CNA_RESULT_SUCCESS || count != 0 ||
        cna_media_queue_get_active_song_index(queue, &index) != CNA_RESULT_SUCCESS ||
        index != -1 ||
        cna_media_queue_get_active_song(queue, &copy, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || copy != CNA_INVALID_HANDLE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_media_queue_get_type_name_size(queue, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_media_queue_copy_type_name(queue, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.MediaQueue") != 0) {
        return 0;
    }

    /* Appending copies the song, so the caller's handle keeps working and the copy compares
       equal to it -- equality is the file path. */
    if (cna_media_queue_add(queue, song) != CNA_RESULT_SUCCESS ||
        cna_media_queue_get_count(queue, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_media_queue_get_at(queue, 1, &copy) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_queue_get_at(queue, -1, &copy) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_queue_get_at(queue, 0, &copy) != CNA_RESULT_SUCCESS ||
        copy == CNA_INVALID_HANDLE ||
        cna_song_equals(song, copy, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_get_name_size(song, &bytes) != CNA_RESULT_SUCCESS ||
        cna_song_destroy(copy) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The active index is stored unchecked, exactly as the canonical setter stores it. */
    if (cna_media_queue_set_active_song_index(queue, 0) != CNA_RESULT_SUCCESS ||
        cna_media_queue_get_active_song_index(queue, &index) != CNA_RESULT_SUCCESS ||
        index != 0 ||
        cna_media_queue_get_active_song(queue, &copy, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_song_equals(song, copy, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_destroy(copy) != CNA_RESULT_SUCCESS ||
        cna_media_queue_clear(queue) != CNA_RESULT_SUCCESS ||
        cna_media_queue_get_count(queue, &count) != CNA_RESULT_SUCCESS || count != 0) {
        return 0;
    }

    /* Playing one song fills the queue whether or not the platform can decode the file. */
    if (cna_media_player_play_song(game, song) != CNA_RESULT_SUCCESS ||
        cna_media_queue_get_count(queue, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_media_queue_get_active_song_index(queue, &index) != CNA_RESULT_SUCCESS ||
        index != 0 ||
        cna_media_player_get_state(game, &state) != CNA_RESULT_SUCCESS ||
        !is_defined_state(state) ||
        cna_media_player_get_play_position_ticks(game, &ticks) != CNA_RESULT_SUCCESS ||
        ticks < INT64_C(0)) {
        return 0;
    }
    /* The transitions are asserted as a relationship, because whether playback actually started
       depends on the platform's ability to decode this file, not on the C API. */
    if (state == CNA_MEDIA_STATE_PLAYING) {
        if (cna_media_player_pause(game) != CNA_RESULT_SUCCESS ||
            cna_media_player_get_state(game, &state) != CNA_RESULT_SUCCESS ||
            state != CNA_MEDIA_STATE_PAUSED ||
            cna_media_player_resume(game) != CNA_RESULT_SUCCESS ||
            cna_media_player_get_state(game, &state) != CNA_RESULT_SUCCESS ||
            state != CNA_MEDIA_STATE_PLAYING) {
            return 0;
        }
    } else {
        /* Nothing is playing, so both are successful no-ops. */
        if (cna_media_player_pause(game) != CNA_RESULT_SUCCESS ||
            cna_media_player_resume(game) != CNA_RESULT_SUCCESS ||
            cna_media_player_get_state(game, &state) != CNA_RESULT_SUCCESS ||
            state != CNA_MEDIA_STATE_STOPPED) {
            return 0;
        }
    }
    if (cna_media_player_stop(game) != CNA_RESULT_SUCCESS ||
        cna_media_player_get_state(game, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_MEDIA_STATE_STOPPED) {
        return 0;
    }

    /* A collection plays as a whole, and the index overload is not range-checked canonically. */
    {
        const CNA_SongHandle songs[1] = {song};
        if (cna_song_collection_create(game, songs, UINT64_C(1), &collection) !=
                CNA_RESULT_SUCCESS ||
            cna_media_player_play_songs(game, collection) != CNA_RESULT_SUCCESS ||
            cna_media_queue_get_count(queue, &count) != CNA_RESULT_SUCCESS || count != 1 ||
            cna_media_player_play_songs_from(game, collection, 0) != CNA_RESULT_SUCCESS ||
            cna_media_player_play_songs_from(game, collection, 99) != CNA_RESULT_SUCCESS ||
            cna_media_player_stop(game) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* Moving through an empty queue is safe, and maintenance and exit are ordinary calls. */
    if (cna_media_queue_clear(queue) != CNA_RESULT_SUCCESS ||
        cna_media_player_move_next(game) != CNA_RESULT_SUCCESS ||
        cna_media_player_move_previous(game) != CNA_RESULT_SUCCESS ||
        cna_media_player_update_ext(game) != CNA_RESULT_SUCCESS ||
        cna_media_player_program_exit_ext(game) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A song with no known duration never reports ended, however long it has played. */
    if (cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_media_player_detect_song_ended_by_elapsed_time_ext(
            song, INT64_C(600000000000), &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* With a duration set, the detector reports ended once the elapsed time reaches it. */
    if (cna_song_set_duration(song, INT64_C(10000000)) != CNA_RESULT_SUCCESS ||
        cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_media_player_detect_song_ended_by_elapsed_time_ext(
            song, INT64_C(20000000), &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (!validate_player_events(game)) {
        return 0;
    }

    return cna_song_collection_destroy(collection) == CNA_RESULT_SUCCESS &&
        cna_song_destroy(song) == CNA_RESULT_SUCCESS &&
        cna_media_queue_destroy(queue) == CNA_RESULT_SUCCESS &&
        cna_media_queue_destroy(queue) == CNA_RESULT_INVALID_HANDLE &&
        cna_media_queue_get_count(rejected, &count) == CNA_RESULT_INVALID_HANDLE &&
        cna_media_queue_clear(rejected) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    PlayerSmokeState* const state = (PlayerSmokeState*)context;
    if (game_time == 0 || !validate_player_settings(game) ||
        !validate_queue_and_playback(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_MediaState media_state = CNA_MEDIA_STATE_STOPPED;
    state->result = cna_media_player_get_state(state->game, &media_state);
    return 0;
}

int main(void)
{
    PlayerSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API media player smoke", UINT64_C(24)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return 1;
    }

    WrongThreadState wrong_thread = {game, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        return 2;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 3;
    }
    return 0;
}
