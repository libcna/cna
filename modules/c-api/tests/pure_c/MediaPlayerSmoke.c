// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>
#include "CnaTestRunFrame.h"

#include "CnaTestReport.h"
#include "CnaRuntimeSmoke.h"

#include <string.h>
#include "CnaTestThreads.h"

#ifndef CNA_C_API_MEDIA_PLAYER_FIXTURE
#error "CNA_C_API_MEDIA_PLAYER_FIXTURE must name the player fixture file"
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

static int is_defined_state(const CNA_MediaState state)
{
    return state == CNA_MEDIA_STATE_STOPPED || state == CNA_MEDIA_STATE_PLAYING ||
        state == CNA_MEDIA_STATE_PAUSED;
}

/* Every one of these is process-wide state the suite does not own, so it is put back afterwards. */
static int validate_player_settings(const CNA_Handle game)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 0;
    CNA_Bool muted = UINT8_C(9);
    CNA_Bool repeating = UINT8_C(9);
    CNA_Bool shuffled = UINT8_C(9);
    CNA_Bool visualization = UINT8_C(9);
    CNA_Bool control = UINT8_C(9);
    CNA_Bool flag = UINT8_C(9);
    float volume = -1.0F;
    float restored = -1.0F;

    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_game_has_control(game, &control)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(control) == CNA_FALSE || control == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_muted(game, &muted)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_repeating(game, &repeating)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_shuffled(game, &shuffled)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_visualization_enabled(game, &visualization)) ==
            CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_volume(game, &volume)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(volume >= 0.0F);
    RUNTIME_CHECK(volume <= 1.0F);

    /* Each flag round-trips through its own setter. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_muted(game, CNA_TRUE)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_muted(game, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_muted(game, CNA_FALSE)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_muted(game, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_repeating(game, CNA_TRUE)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_repeating(game, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_shuffled(game, CNA_TRUE)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_shuffled(game, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_visualization_enabled(game, CNA_TRUE)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_visualization_enabled(game, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);

    /* The canonical volume setter clamps rather than refusing, and C preserves that. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_volume(game, 0.25F)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_volume(game, &restored)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(restored == 0.25F);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_volume(game, 5.0F)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_volume(game, &restored)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(restored == 1.0F);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_volume(game, -3.0F)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_volume(game, &restored)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(restored == 0.0F);

    /* Visualization fills a caller-provided value; with no renderer data it stays as it was. */
    {
        CNA_VisualizationData data;
        CNA_VisualizationData invalid;
        RUNTIME_CHECK(SMOKE_API(cna_visualization_data_init(&data)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_visualization_data(game, &data)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_visualization_data(game, 0)) == CNA_RESULT_INVALID_ARGUMENT);
        invalid = data;
        invalid.struct_version = UINT32_C(2);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_visualization_data(game, &invalid)) ==
            CNA_RESULT_INVALID_ARGUMENT);
    }

    /* Put every process-wide setting back the way it was found. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_muted(game, muted)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_repeating(game, repeating)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_shuffled(game, shuffled)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_is_visualization_enabled(game, visualization)) ==
            CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_set_volume(game, volume)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_is_muted(game, 0)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_volume(game, 0)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, 0)) == CNA_RESULT_INVALID_ARGUMENT);
    status = 1;
cleanup:
    return status;
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
    CnaRuntimeSmoke smoke = {0};
    int status = 0;
    EventState events = {0, 0};
    CNA_MediaPlayerEventRegistrationHandle active = CNA_INVALID_HANDLE;
    CNA_MediaPlayerEventRegistrationHandle changed = CNA_INVALID_HANDLE;
    CNA_MediaPlayerEventRegistrationHandle rejected = CNA_INVALID_HANDLE;

    RUNTIME_CHECK(SMOKE_API(cna_media_player_subscribe_active_song_changed_ext(0, &events, &active)) ==
            CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_subscribe_active_song_changed_ext(on_active_song_changed, &events, 0)) ==
            CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_player_subscribe_active_song_changed_ext(
            on_active_song_changed, &events, &active), &active, cna_media_player_unsubscribe_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_player_subscribe_media_state_changed_ext(
            on_media_state_changed, &events, &changed), &changed, cna_media_player_unsubscribe_ext) == CNA_RESULT_SUCCESS);
    /* Each raise reaches its own handler synchronously. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_raise_active_song_changed_ext(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(events.active_song_calls) == 1);
    RUNTIME_CHECK(SMOKE_VALUE(events.state_calls) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_raise_media_state_changed_ext(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(events.state_calls) == 1);
    RUNTIME_CHECK(SMOKE_VALUE(events.active_song_calls) == 1);
    /* One shared release route detaches exactly the subscription it was handed. */
    RUNTIME_CHECK(SMOKE_DROP(cna_media_player_unsubscribe_ext(active), active) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_unsubscribe_ext(active)) == CNA_RESULT_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_raise_active_song_changed_ext(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(events.active_song_calls) == 1);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_raise_media_state_changed_ext(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(events.state_calls) == 2);
    RUNTIME_CHECK(SMOKE_DROP(cna_media_player_unsubscribe_ext(changed), changed) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_unsubscribe_ext(rejected)) == CNA_RESULT_INVALID_HANDLE);
    status = 1;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0) {
        status = 0;
    }
    return status;
}

static int validate_queue_and_playback(const CNA_Handle game)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 0;
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
    char text[256];

    RUNTIME_CHECK(SMOKE_OWN(cna_song_create(game, view(CNA_C_API_MEDIA_PLAYER_FIXTURE),
        view("Queued"), &song), &song, cna_song_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_queue(game, 0)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_player_get_queue(game, &queue), &queue, cna_media_queue_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(queue) != CNA_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_clear(queue)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(count) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_active_song_index(queue, &index)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK((int32_t)SMOKE_VALUE(index) == -1);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_queue_get_active_song(queue, &copy, &flag), &copy, cna_song_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_VALUE(copy) == CNA_INVALID_HANDLE);
    memset(text, 0, sizeof(text));
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_type_name_size(queue, &bytes)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(bytes) < (uint64_t)sizeof(text));
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_copy_type_name(queue, text, (uint64_t)sizeof(text), &bytes)) ==
            CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(strcmp(text, "Microsoft.Xna.Framework.Media.MediaQueue") == 0);

    /* Appending copies the song, so the caller's handle keeps working and the copy compares
       equal to it -- equality is the file path. */
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_add(queue, song)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(count) == 1);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_at(queue, 1, &copy)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_at(queue, -1, &copy)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_queue_get_at(queue, 0, &copy), &copy, cna_song_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(copy) != CNA_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_song_equals(song, copy, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_song_get_name_size(song, &bytes)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_song_destroy(copy), copy) == CNA_RESULT_SUCCESS);
    /* The active index is stored unchecked, exactly as the canonical setter stores it. */
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_set_active_song_index(queue, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_active_song_index(queue, &index)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(index) == 0);
    RUNTIME_CHECK(SMOKE_OWN(cna_media_queue_get_active_song(queue, &copy, &flag), &copy, cna_song_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_song_equals(song, copy, &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_DROP(cna_song_destroy(copy), copy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_clear(queue)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(count) == 0);

    /* Playing one song fills the queue whether or not the platform can decode the file. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_play_song(game, song)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(count) == 1);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_active_song_index(queue, &index)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(index) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, &state)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(is_defined_state(state));
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_play_position_ticks(game, &ticks)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK((int64_t)SMOKE_VALUE(ticks) >= INT64_C(0));
    /* The transitions are asserted as a relationship, because whether playback actually started
       depends on the platform's ability to decode this file, not on the C API. */
    if (state == CNA_MEDIA_STATE_PLAYING) {
        RUNTIME_CHECK(SMOKE_API(cna_media_player_pause(game)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, &state)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(state) == CNA_MEDIA_STATE_PAUSED);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_resume(game)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, &state)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(state) == CNA_MEDIA_STATE_PLAYING);
    } else {
        /* Nothing is playing, so both are successful no-ops. */
        RUNTIME_CHECK(SMOKE_API(cna_media_player_pause(game)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_resume(game)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, &state)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(state) == CNA_MEDIA_STATE_STOPPED);
    }
    RUNTIME_CHECK(SMOKE_API(cna_media_player_stop(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_get_state(game, &state)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(state) == CNA_MEDIA_STATE_STOPPED);

    /* A collection plays as a whole; the index overload refuses an index outside the collection
     * before touching the queue (XNA MediaQueue.Play IL_0013-0047, TQ-023). */
    {
        const CNA_SongHandle songs[1] = {song};
        RUNTIME_CHECK(SMOKE_OWN(cna_song_collection_create(game, songs, UINT64_C(1), &collection), &collection, cna_song_collection_destroy) ==
                CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_play_songs(game, collection)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(count) == 1);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_play_songs_from(game, collection, 0)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_play_songs_from(game, collection, 99)) == CNA_RESULT_INVALID_ARGUMENT);
        RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(queue, &count)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(count) == 1);
        RUNTIME_CHECK(SMOKE_API(cna_media_player_stop(game)) == CNA_RESULT_SUCCESS);
    }

    /* Moving through an empty queue is safe, and maintenance and exit are ordinary calls. */
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_clear(queue)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_move_next(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_move_previous(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_update_ext(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_program_exit_ext(game)) == CNA_RESULT_SUCCESS);

    /* A song with no known duration never reports ended, however long it has played. */
    RUNTIME_CHECK(SMOKE_API(cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), &flag)) ==
            CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_detect_song_ended_by_elapsed_time_ext(
            song, INT64_C(600000000000), &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_FALSE);
    /* With a duration set, the detector reports ended once the elapsed time reaches it. */
    RUNTIME_CHECK(SMOKE_API(cna_song_set_duration(song, INT64_C(10000000))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), &flag)) ==
            CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_detect_song_ended_by_elapsed_time_ext(
            song, INT64_C(20000000), &flag)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(flag) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_media_player_detect_song_ended_by_elapsed_time_ext(song, INT64_C(0), 0)) ==
            CNA_RESULT_INVALID_ARGUMENT);

    RUNTIME_CHECK(validate_player_events(game));

    RUNTIME_CHECK(SMOKE_DROP(cna_song_collection_destroy(collection), collection) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_song_destroy(song), song) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_media_queue_destroy(queue), queue) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_destroy(queue)) == CNA_RESULT_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_get_count(rejected, &count)) == CNA_RESULT_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_media_queue_clear(rejected)) == CNA_RESULT_INVALID_HANDLE);
    status = 1;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0) {
        status = 0;
    }
    return status;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CnaRuntimeSmoke smoke = {0};
    CNA_Result result = CNA_RESULT_INVALID_STATE;
    PlayerSmokeState* const state = (PlayerSmokeState*)context;
    (void)out_error;
    RUNTIME_CHECK(game_time != 0);
    RUNTIME_CHECK(validate_player_settings(game));
    RUNTIME_CHECK(validate_queue_and_playback(game));
    state->validated = 1;
    result = CNA_RESULT_SUCCESS;
cleanup:
    return result;
}

static int query_on_wrong_thread(void* const context)
{
    CnaRuntimeSmoke smoke = {0};
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_MediaState media_state = CNA_MEDIA_STATE_STOPPED;
    state->result = SMOKE_API(cna_media_player_get_state(state->game, &media_state));
    if (state->result != CNA_RESULT_THREAD) {
        cna_smoke_failure(&smoke, __FILE__, __LINE__, __func__, "expected CNA_RESULT_THREAD");
    }
    return 0;
}

int main(void)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    FILE* fixture = NULL;
    int fixture_created = 0;
    /* A tag-only file exercises the stopped-playback path without an optional codec.
       Keep it outside the library fixture: that smoke indexes exactly its own two songs. */
    static const unsigned char song_tag[] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
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
    fixture = fopen(CNA_C_API_MEDIA_PLAYER_FIXTURE, "wb");
    if (fixture == NULL) {
        perror("create media player fixture: " CNA_C_API_MEDIA_PLAYER_FIXTURE);
    }
    fixture_created = fixture != NULL;
    RUNTIME_CHECK(fixture != NULL);
    RUNTIME_CHECK(SMOKE_VALUE(fwrite(song_tag, 1, sizeof(song_tag), fixture)) == sizeof(song_tag));
    {
        const int close_result = fclose(fixture);
        fixture = NULL;
        RUNTIME_CHECK(SMOKE_VALUE(close_result) == 0);
    }
    RUNTIME_CHECK(SMOKE_OWN(cna_game_create(&create_info, &game), &game,
        cna_game_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_test_run_initialized_frame(game)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(smoke_state.validated) == 1);

    status = 2;
    WrongThreadState wrong_thread = {game, CNA_RESULT_SUCCESS};
    thrd_t thread;
    RUNTIME_CHECK(SMOKE_VALUE(thrd_create(&thread, query_on_wrong_thread, &wrong_thread)) ==
        thrd_success);
    RUNTIME_CHECK(SMOKE_VALUE(thrd_join(thread, 0)) == thrd_success);
    RUNTIME_CHECK(SMOKE_VALUE(wrong_thread.result) == CNA_RESULT_THREAD);

    status = 3;
    RUNTIME_CHECK(SMOKE_DROP(cna_game_destroy(game), game) == CNA_RESULT_SUCCESS);
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0 && status == 0) {
        status = 3;
    }
    if (fixture != NULL && fclose(fixture) != 0) {
        perror("close media player fixture");
        cna_smoke_error();
        status = 3;
    }
    if (fixture_created && remove(CNA_C_API_MEDIA_PLAYER_FIXTURE) != 0) {
        perror("remove media player fixture: " CNA_C_API_MEDIA_PLAYER_FIXTURE);
        cna_smoke_error();
        status = 3;
    }
    return status == 0 ? 0 : CNA_TEST_FAIL(status);
}
