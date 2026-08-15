// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>
#include <threads.h>

typedef struct MediaSmokeState {
    int validated;
} MediaSmokeState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result count_result;
} WrongThreadState;

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

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    MediaSmokeState* const state = (MediaSmokeState*)context;
    if (game_time == 0 || !validate_media_sources(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint32_t count = UINT32_C(0);
    state->count_result = cna_media_source_get_available_count(state->game, &count);
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_visualization()) {
        return 1;
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
        return 2;
    }

    WrongThreadState wrong_thread = {game, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.count_result != CNA_RESULT_THREAD) {
        return 3;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    return 0;
}
