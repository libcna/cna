// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>
#include <threads.h>

typedef struct MediaSmokeState {
    int validated;
} MediaSmokeState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_SongHandle song;
    CNA_Result count_result;
    CNA_Result song_result;
} WrongThreadState;

static const char SongContainerName[] = "MediaRoot";
static const char SongSuffix[] = "/MediaRoot/AllPlayers/";
static const char FirstSongFile[] = "first_song.wav";
/* A real non-ASCII UTF-8 file name, so the path crossing the ABI is not accidentally ASCII. */
static const char SecondSongFile[] = "druh\xC3\xA1_p\xC3\xADse\xC5\x88.wav";

typedef struct SongFixture {
    CNA_Handle device;
    CNA_Handle container;
    char root[512];
    char first_path[640];
    char second_path[640];
} SongFixture;

static CNA_StringView view(const char* const value)
{
    CNA_StringView result;
    result.data = value;
    result.byte_length = (uint64_t)strlen(value);
    return result;
}

static int write_container_file(const CNA_Handle container, const char* const name)
{
    static const uint8_t bytes[4] = {0x52U, 0x49U, 0x46U, 0x46U};
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    if (cna_storage_container_create_file(container, view(name), &stream) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    const int wrote =
        cna_storage_stream_write(stream, bytes, (uint64_t)sizeof(bytes)) == CNA_RESULT_SUCCESS;
    return cna_storage_stream_close(stream) == CNA_RESULT_SUCCESS && wrote;
}

/* A song only needs its file to exist, but it needs a real absolute path -- and a strict-C17 test
   has no portable way to ask the operating system for one. The storage API already answers that,
   so the fixture is built through it exactly as the content suite builds its content root. */
static int create_song_fixture(SongFixture* const fixture)
{
    uint64_t root_bytes = 0U;

    fixture->device = CNA_INVALID_HANDLE;
    fixture->container = CNA_INVALID_HANDLE;
    if (cna_storage_set_app_name_ext(view("cna-c-api-media-smoke")) != CNA_RESULT_SUCCESS ||
        cna_storage_device_show_selector(0, 0, &fixture->device) != CNA_RESULT_SUCCESS ||
        cna_storage_device_delete_container(fixture->device, view(SongContainerName)) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_open(
            fixture->device, view(SongContainerName), 0, 0, &fixture->container) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_get_root_size_ext(&root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes + sizeof(SongSuffix) > sizeof(fixture->root) ||
        cna_storage_copy_root_ext(fixture->root, (uint64_t)sizeof(fixture->root), &root_bytes) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    memcpy(fixture->root + root_bytes, SongSuffix, sizeof(SongSuffix));

    if (!write_container_file(fixture->container, FirstSongFile) ||
        !write_container_file(fixture->container, SecondSongFile)) {
        return 0;
    }
    if (strlen(fixture->root) + sizeof(SecondSongFile) > sizeof(fixture->first_path)) {
        return 0;
    }
    strcpy(fixture->first_path, fixture->root);
    strcat(fixture->first_path, FirstSongFile);
    strcpy(fixture->second_path, fixture->root);
    strcat(fixture->second_path, SecondSongFile);
    return 1;
}

static int destroy_song_fixture(const SongFixture* const fixture)
{
    return cna_storage_container_destroy(fixture->container) == CNA_RESULT_SUCCESS &&
        cna_storage_device_delete_container(fixture->device, view(SongContainerName)) ==
            CNA_RESULT_SUCCESS &&
        cna_storage_device_destroy(fixture->device) == CNA_RESULT_SUCCESS;
}

/* Pure value operations need no runtime at all, so they run before a game exists. */
static int validate_pure_visualization(void)
{
    CNA_VisualizationData data;
    uint64_t bytes = UINT64_C(9);
    uint64_t copied = UINT64_C(9);
    char name[128];
    char guard[8];

    memset(&data, 9, sizeof(data));
    if (cna_visualization_data_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_visualization_data_init(&data) != CNA_RESULT_SUCCESS ||
        data.struct_size != sizeof(CNA_VisualizationData) ||
        data.struct_version != UINT32_C(1)) {
        return 0;
    }
    /* Both canonical buffers start zeroed, over their whole length. */
    for (uint32_t index = UINT32_C(0); index < CNA_VISUALIZATION_DATA_SIZE; ++index) {
        if (data.frequencies[index] != 0.0F || data.samples[index] != 0.0F) {
            return 0;
        }
    }

    if (cna_visualization_data_get_type_name_size(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_visualization_data_get_type_name_size(&bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(name)) {
        return 0;
    }
    memset(name, 0, sizeof(name));
    if (cna_visualization_data_copy_type_name(name, (uint64_t)sizeof(name), &copied) !=
            CNA_RESULT_SUCCESS ||
        copied != bytes || (uint64_t)strlen(name) != bytes ||
        strcmp(name, "Microsoft.Xna.Framework.Media.VisualizationData") != 0) {
        return 0;
    }
    /* A short capacity reports the requirement and writes nothing. */
    memset(guard, 0x7F, sizeof(guard));
    if (cna_visualization_data_copy_type_name(guard, UINT64_C(4), &copied) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        copied != bytes || guard[0] != 0x7F) {
        return 0;
    }
    return cna_visualization_data_copy_type_name(0, UINT64_C(4), &copied) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_visualization_data_copy_type_name(name, (uint64_t)sizeof(name), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_media_sources(const CNA_Handle game)
{
    CNA_MediaSourceType type = UINT32_C(99);
    uint32_t count = UINT32_C(9);
    uint64_t bytes = UINT64_C(9);
    uint64_t copied = UINT64_C(9);
    char text[256];
    int saw_local_device = 0;

    if (cna_media_source_get_available_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_media_source_get_available_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The device always has at least its own local storage to report. */
    if (count == UINT32_C(0)) {
        return 0;
    }
    /* An index at or past the count is refused by every route. */
    if (cna_media_source_get_type_at(game, count, &type) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_source_get_name_size_at(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_source_copy_name_at(game, count, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_source_get_type_name_size_at(game, count, &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_source_copy_type_name_at(game, count, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    for (uint32_t index = UINT32_C(0); index < count; ++index) {
        memset(text, 0, sizeof(text));
        if (cna_media_source_get_type_at(game, index, &type) != CNA_RESULT_SUCCESS ||
            (type != CNA_MEDIA_SOURCE_TYPE_LOCAL_DEVICE &&
             type != CNA_MEDIA_SOURCE_TYPE_WINDOWS_MEDIA_CONNECT) ||
            cna_media_source_get_name_size_at(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(text) ||
            cna_media_source_copy_name_at(game, index, text, (uint64_t)sizeof(text), &copied) !=
                CNA_RESULT_SUCCESS ||
            copied != bytes || (uint64_t)strlen(text) != bytes) {
            return 0;
        }
        if (type == CNA_MEDIA_SOURCE_TYPE_LOCAL_DEVICE) {
            saw_local_device = 1;
        }

        memset(text, 0, sizeof(text));
        if (cna_media_source_get_type_name_size_at(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(text) ||
            cna_media_source_copy_type_name_at(
                game, index, text, (uint64_t)sizeof(text), &copied) != CNA_RESULT_SUCCESS ||
            copied != bytes ||
            strcmp(text, "Microsoft.Xna.Framework.Media.MediaSource") != 0) {
            return 0;
        }

        /* A short capacity reports the requirement and writes nothing. */
        {
            char guard[8];
            memset(guard, 0x7F, sizeof(guard));
            if (cna_media_source_copy_name_at(game, index, guard, UINT64_C(1), &copied) !=
                    CNA_RESULT_BUFFER_TOO_SMALL ||
                guard[0] != 0x7F ||
                cna_media_source_copy_type_name_at(game, index, guard, UINT64_C(1), &copied) !=
                    CNA_RESULT_BUFFER_TOO_SMALL ||
                guard[0] != 0x7F) {
                return 0;
            }
        }
    }
    if (!saw_local_device) {
        return 0;
    }

    return cna_media_source_get_type_at(game, UINT32_C(0), 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_source_get_name_size_at(game, UINT32_C(0), 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_source_copy_name_at(game, UINT32_C(0), 0, UINT64_C(4), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_source_copy_name_at(game, UINT32_C(0), text, (uint64_t)sizeof(text), 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_source_get_type_name_size_at(game, UINT32_C(0), 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_source_copy_type_name_at(game, UINT32_C(0), 0, UINT64_C(4), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT;
}


static int song_reports_defaults(const CNA_SongHandle song, const char* const path)
{
    CNA_Bool flag = UINT8_C(9);
    int64_t ticks = INT64_C(9);
    int32_t number = 9;
    uint64_t bytes = UINT64_C(9);
    char text[768];

    if (cna_song_get_is_protected(song, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_song_get_is_rated(song, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_song_get_is_disposed(song, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE) {
        return 0;
    }
    if (cna_song_get_play_count(song, &number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_song_get_rating(song, &number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_song_get_track_number(song, &number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_song_get_duration(song, &ticks) != CNA_RESULT_SUCCESS || ticks != INT64_C(0)) {
        return 0;
    }
    /* The handle text is the file the song plays from, and it is what equality compares. */
    memset(text, 0, sizeof(text));
    return cna_song_get_handle_text_size_ext(song, &bytes) == CNA_RESULT_SUCCESS &&
        bytes == (uint64_t)strlen(path) && bytes < (uint64_t)sizeof(text) &&
        cna_song_copy_handle_text_ext(song, text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, path) == 0;
}

static int validate_song_family(const CNA_Handle game, const SongFixture* const fixture)
{
    CNA_SongHandle song = CNA_INVALID_HANDLE;
    CNA_SongHandle second = CNA_INVALID_HANDLE;
    CNA_SongHandle same_file = CNA_INVALID_HANDLE;
    CNA_SongHandle scratch = CNA_INVALID_HANDLE;
    CNA_SongHandle rejected = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int64_t ticks = INT64_C(9);
    int32_t number = 9;
    int32_t hash = 0;
    int32_t other_hash = 0;
    uint64_t bytes = UINT64_C(9);
    char text[768];
    char uri[768];

    /* A missing file is an I/O failure, not a silently empty song. */
    if (cna_song_create(game, view("no_such_song_file.wav"), view("missing"), &scratch) !=
            CNA_RESULT_IO ||
        scratch != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_song_create(game, view(fixture->first_path), view("First"), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_song_create(game, view(fixture->first_path), view("First"), &song) !=
            CNA_RESULT_SUCCESS ||
        song == CNA_INVALID_HANDLE || !song_reports_defaults(song, fixture->first_path)) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_song_get_name_size(song, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(5) ||
        cna_song_copy_name(song, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "First") != 0) {
        return 0;
    }

    /* Documented deviation from the canonical constructor's own comment: an omitted name is
       stored verbatim as empty rather than defaulting to the file name. */
    if (cna_song_create(game, view(fixture->second_path), view(""), &second) !=
            CNA_RESULT_SUCCESS ||
        cna_song_get_name_size(second, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(0) ||
        !song_reports_defaults(second, fixture->second_path)) {
        return 0;
    }

    /* Equality is the file path, not handle identity, and equal songs hash equal. */
    if (cna_song_create(game, view(fixture->first_path), view("Different name"), &same_file) !=
            CNA_RESULT_SUCCESS ||
        cna_song_equals(song, same_file, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_equals(song, second, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_song_get_hash_code(song, &hash) != CNA_RESULT_SUCCESS ||
        cna_song_get_hash_code(same_file, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }

    /* Both mutable properties round-trip, and neither is validated. */
    if (cna_song_set_duration(song, INT64_C(1234567)) != CNA_RESULT_SUCCESS ||
        cna_song_get_duration(song, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(1234567) ||
        cna_song_set_duration(song, INT64_C(-5)) != CNA_RESULT_SUCCESS ||
        cna_song_get_duration(song, &ticks) != CNA_RESULT_SUCCESS || ticks != INT64_C(-5) ||
        cna_song_set_play_count(song, 42) != CNA_RESULT_SUCCESS ||
        cna_song_get_play_count(song, &number) != CNA_RESULT_SUCCESS || number != 42) {
        return 0;
    }

    /* The millisecond constructor converts to ticks at the canonical scale. */
    if (cna_song_create_with_duration(
            game, view(fixture->first_path), view("Timed"), 250, &scratch) !=
            CNA_RESULT_SUCCESS ||
        cna_song_get_duration(scratch, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(2500000) ||
        cna_song_destroy(scratch) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A file URI resolves to the same path a plain path gives, and a foreign scheme is refused. */
    if (strlen(fixture->first_path) + 8U > sizeof(uri)) {
        return 0;
    }
    strcpy(uri, "file://");
    strcat(uri, fixture->first_path);
    scratch = CNA_INVALID_HANDLE;
    if (cna_song_create_from_uri(game, view("Uri"), view(uri), &scratch) != CNA_RESULT_SUCCESS ||
        cna_song_equals(scratch, song, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_destroy(scratch) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    scratch = CNA_INVALID_HANDLE;
    if (cna_song_create_from_uri(game, view("Uri"), view(fixture->first_path), &scratch) !=
            CNA_RESULT_SUCCESS ||
        cna_song_destroy(scratch) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    scratch = CNA_INVALID_HANDLE;
    if (cna_song_create_from_uri(game, view("Uri"), view("http://example.com/x.mp3"), &scratch) !=
            CNA_RESULT_INVALID_STATE ||
        scratch != CNA_INVALID_HANDLE) {
        return 0;
    }

    memset(text, 0, sizeof(text));
    if (cna_song_get_type_name_size(song, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_song_copy_type_name(song, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.Song") != 0) {
        return 0;
    }

    /* Disposal only marks the song; every other member keeps answering, and it is idempotent. */
    if (cna_song_dispose(song) != CNA_RESULT_SUCCESS ||
        cna_song_dispose(song) != CNA_RESULT_SUCCESS ||
        cna_song_get_is_disposed(song, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_get_play_count(song, &number) != CNA_RESULT_SUCCESS || number != 42) {
        return 0;
    }

    {
        /* A collection keeps its songs alive after the caller releases its own handles. */
        CNA_SongCollectionHandle collection = CNA_INVALID_HANDLE;
        CNA_SongCollectionHandle empty = CNA_INVALID_HANDLE;
        CNA_SongHandle borrowed = CNA_INVALID_HANDLE;
        const CNA_SongHandle songs[2] = {song, second};
        int32_t count = 9;

        if (cna_song_collection_create(game, songs, UINT64_C(2), 0) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_song_collection_create(game, 0, UINT64_C(2), &collection) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_song_collection_create(game, 0, UINT64_C(0), &empty) != CNA_RESULT_SUCCESS ||
            cna_song_collection_get_count(empty, &count) != CNA_RESULT_SUCCESS || count != 0 ||
            cna_song_collection_destroy(empty) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (cna_song_collection_create(game, songs, UINT64_C(2), &collection) !=
                CNA_RESULT_SUCCESS ||
            cna_song_collection_get_count(collection, &count) != CNA_RESULT_SUCCESS ||
            count != 2 ||
            cna_song_collection_get_is_disposed(collection, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE) {
            return 0;
        }
        /* Release the caller's handles; the collection must still hand out live songs. */
        if (cna_song_destroy(song) != CNA_RESULT_SUCCESS ||
            cna_song_destroy(second) != CNA_RESULT_SUCCESS ||
            cna_song_destroy(same_file) != CNA_RESULT_SUCCESS ||
            cna_song_collection_get_at(collection, 0, &borrowed) != CNA_RESULT_SUCCESS ||
            borrowed == CNA_INVALID_HANDLE) {
            return 0;
        }
        memset(text, 0, sizeof(text));
        if (cna_song_copy_handle_text_ext(borrowed, text, (uint64_t)sizeof(text), &bytes) !=
                CNA_RESULT_SUCCESS ||
            strcmp(text, fixture->first_path) != 0 ||
            cna_song_get_is_disposed(borrowed, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_TRUE) {
            return 0;
        }
        if (cna_song_collection_get_at(collection, -1, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_song_collection_get_at(collection, 2, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_song_collection_get_at(collection, 0, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        memset(text, 0, sizeof(text));
        if (cna_song_collection_get_type_name_size(collection, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(text) ||
            cna_song_collection_copy_type_name(
                collection, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
            strcmp(text, "Microsoft.Xna.Framework.Media.SongCollection") != 0) {
            return 0;
        }
        /* Canonical disposal empties the collection, so every index becomes out of range -- but
           the songs themselves survive, because the collection never owned them. */
        if (cna_song_collection_dispose(collection) != CNA_RESULT_SUCCESS ||
            cna_song_collection_dispose(collection) != CNA_RESULT_SUCCESS ||
            cna_song_collection_get_is_disposed(collection, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_TRUE ||
            cna_song_collection_get_count(collection, &count) != CNA_RESULT_SUCCESS ||
            count != 0 ||
            cna_song_collection_get_at(collection, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_song_get_is_disposed(borrowed, &flag) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (cna_song_collection_destroy(collection) != CNA_RESULT_SUCCESS ||
            cna_song_collection_destroy(collection) != CNA_RESULT_INVALID_HANDLE ||
            cna_song_collection_get_count(collection, &count) != CNA_RESULT_INVALID_HANDLE ||
            cna_song_destroy(borrowed) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* A handle that was never created is refused by every song route. */
    return cna_song_get_name_size(rejected, &bytes) == CNA_RESULT_INVALID_HANDLE &&
        cna_song_dispose(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_song_destroy(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_song_get_duration(rejected, &ticks) == CNA_RESULT_INVALID_HANDLE &&
        cna_song_set_play_count(rejected, 1) == CNA_RESULT_INVALID_HANDLE &&
        cna_song_collection_get_count(rejected, &number) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    MediaSmokeState* const state = (MediaSmokeState*)context;
    SongFixture fixture;
    if (game_time == 0 || !validate_media_sources(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (!create_song_fixture(&fixture)) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (!validate_song_family(game, &fixture)) {
        (void)destroy_song_fixture(&fixture);
        return CNA_RESULT_INVALID_STATE;
    }
    if (!destroy_song_fixture(&fixture)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint32_t count = UINT32_C(0);
    uint64_t bytes = UINT64_C(0);
    state->count_result = cna_media_source_get_available_count(state->game, &count);
    state->song_result = cna_song_get_name_size(state->song, &bytes);
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_visualization()) {
        return CNA_TEST_FAIL(1);
    }

    MediaSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API media smoke", UINT64_C(17)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return CNA_TEST_FAIL(2);
    }

    /* Both the media statics and a song handle are thread-affine. */
    SongFixture fixture;
    CNA_SongHandle song = CNA_INVALID_HANDLE;
    if (!create_song_fixture(&fixture) ||
        cna_song_create(game, view(fixture.first_path), view("Threaded"), &song) !=
            CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(3);
    }
    WrongThreadState wrong_thread = {game, song, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.count_result != CNA_RESULT_THREAD ||
        wrong_thread.song_result != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(4);
    }
    if (cna_song_destroy(song) != CNA_RESULT_SUCCESS || !destroy_song_fixture(&fixture)) {
        return CNA_TEST_FAIL(5);
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(6);
    }
    return 0;
}
