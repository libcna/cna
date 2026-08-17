// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>
#include <threads.h>

typedef struct VideoSmokeState {
    int validated;
} VideoSmokeState;

typedef struct WrongThreadState {
    CNA_VideoPlayerHandle player;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView view(const char* const value)
{
    CNA_StringView result;
    result.data = value;
    result.byte_length = (uint64_t)strlen(value);
    return result;
}

/* Any existing file is enough to exercise the raw-file constructor: the probe simply fails on a
   file that is not a video, which is a contract worth pinning in its own right. The suite writes
   its own so it depends on no other test having run first. */
static const char FixturePath[] = "cna_c_api_video_smoke_fixture.bin";

static int write_fixture(void)
{
    /* Deliberately not any container's magic: the probe must fail cleanly. */
    static const uint8_t bytes[16] = {
        0x6EU, 0x6FU, 0x74U, 0x20U, 0x61U, 0x20U, 0x76U, 0x69U,
        0x64U, 0x65U, 0x6FU, 0x20U, 0x66U, 0x69U, 0x6CU, 0x65U
    };
    FILE* const file = fopen(FixturePath, "wb");
    size_t written = 0U;
    if (file == 0) {
        return 0;
    }
    written = fwrite(bytes, 1U, sizeof(bytes), file);
    return fclose(file) == 0 && written == sizeof(bytes);
}

static int validate_video_values(const CNA_Handle device, const char* const path)
{
    CNA_VideoHandle video = CNA_INVALID_HANDLE;
    CNA_VideoHandle declared = CNA_INVALID_HANDLE;
    CNA_VideoHandle rejected = CNA_INVALID_HANDLE;
    CNA_VideoInfo info;
    CNA_VideoSoundtrackType soundtrack = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    int32_t number = -1;
    float fps = -1.0F;
    int64_t ticks = INT64_C(-1);
    uint64_t bytes = UINT64_C(9);
    char text[1024];

    /* A missing file is refused by the probing constructor, exactly as the canonical one refuses
       it, while the metadata constructor never touches the file. */
    if (cna_video_create(device, view("no_such_video.mp4"), &rejected) != CNA_RESULT_IO ||
        rejected != CNA_INVALID_HANDLE ||
        cna_video_create(device, view(path), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_video_create(device, view(path), &video) != CNA_RESULT_SUCCESS ||
        video == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The file exists but is not decodable video, so the probe leaves every value at zero rather
       than failing -- the canonical behavior this ABI reports faithfully. */
    if (cna_video_get_width(video, &number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_video_get_height(video, &number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_video_get_frames_per_second(video, &fps) != CNA_RESULT_SUCCESS || fps != 0.0F ||
        cna_video_get_duration(video, &ticks) != CNA_RESULT_SUCCESS || ticks != INT64_C(0)) {
        return 0;
    }
    if (cna_video_get_has_graphics_device(video, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_video_get_file_name_size(video, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen(path) || bytes >= (uint64_t)sizeof(text) ||
        cna_video_copy_file_name(video, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, path) != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_video_get_type_name_size(video, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_video_copy_type_name(video, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.Video") != 0) {
        return 0;
    }

    /* The compiled-asset constructor stores exactly the metadata it is given. */
    if (cna_video_create_with_metadata(
            device, view(path), 2500, 640, 360, 29.97F,
            CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM + UINT32_C(1), &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_video_create_with_metadata(
            device, view("this_file_does_not_exist.mp4"), 2500, 640, 360, 29.97F,
            CNA_VIDEO_SOUNDTRACK_TYPE_DIALOG, &declared) != CNA_RESULT_SUCCESS ||
        cna_video_get_width(declared, &number) != CNA_RESULT_SUCCESS || number != 640 ||
        cna_video_get_height(declared, &number) != CNA_RESULT_SUCCESS || number != 360 ||
        cna_video_get_frames_per_second(declared, &fps) != CNA_RESULT_SUCCESS ||
        fps != 29.97F ||
        cna_video_get_soundtrack_type(declared, &soundtrack) != CNA_RESULT_SUCCESS ||
        soundtrack != CNA_VIDEO_SOUNDTRACK_TYPE_DIALOG ||
        cna_video_get_duration(declared, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(25000000)) {
        return 0;
    }

    /* The metadata triple is the canonical nested value, filled from a video. */
    memset(&info, 9, sizeof(info));
    if (cna_video_info_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_video_info_init(&info) != CNA_RESULT_SUCCESS ||
        info.struct_size != sizeof(CNA_VideoInfo) || info.struct_version != UINT32_C(1) ||
        info.width != 0 || info.height != 0 || info.fps != 0.0 ||
        cna_video_get_info(declared, &info) != CNA_RESULT_SUCCESS ||
        info.width != 640 || info.height != 360 || info.fps < 29.9 || info.fps > 30.0) {
        return 0;
    }
    {
        CNA_VideoInfo invalid = info;
        invalid.struct_version = UINT32_C(2);
        if (cna_video_get_info(declared, &invalid) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_video_get_info(declared, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* The duration setter and both track selectors validate nothing, as canonically. */
    if (cna_video_set_duration(declared, INT64_C(999)) != CNA_RESULT_SUCCESS ||
        cna_video_get_duration(declared, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(999) ||
        cna_video_set_audio_track_ext(declared, 3) != CNA_RESULT_SUCCESS ||
        cna_video_set_video_track_ext(declared, -1) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The URI factory does not parse URIs: it forwards the text to the file constructor, so a
       plain path works and a scheme-bearing string is simply a path that does not exist. */
    {
        CNA_VideoHandle from_uri = CNA_INVALID_HANDLE;
        if (cna_video_create_from_uri_ext(device, view(path), &from_uri) != CNA_RESULT_SUCCESS ||
            cna_video_destroy(from_uri) != CNA_RESULT_SUCCESS ||
            cna_video_create_from_uri_ext(
                device, view("http://example.com/v.mp4"), &rejected) != CNA_RESULT_IO ||
            rejected != CNA_INVALID_HANDLE) {
            return 0;
        }
    }

    return cna_video_destroy(declared) == CNA_RESULT_SUCCESS &&
        cna_video_destroy(video) == CNA_RESULT_SUCCESS &&
        cna_video_destroy(video) == CNA_RESULT_INVALID_HANDLE &&
        cna_video_get_width(rejected, &number) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_video_player(const CNA_Handle game, const CNA_Handle device,
                                 const char* const path)
{
    CNA_VideoPlayerHandle player = CNA_INVALID_HANDLE;
    CNA_VideoPlayerHandle rejected = CNA_INVALID_HANDLE;
    CNA_VideoHandle video = CNA_INVALID_HANDLE;
    CNA_VideoHandle playing = CNA_INVALID_HANDLE;
    CNA_Handle frame = CNA_INVALID_HANDLE;
    CNA_MediaState state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    float volume = -1.0F;
    int64_t ticks = INT64_C(-1);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_video_player_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_video_player_create(game, &player) != CNA_RESULT_SUCCESS ||
        player == CNA_INVALID_HANDLE ||
        cna_video_player_get_is_disposed(player, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_video_player_get_state(player, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_MEDIA_STATE_STOPPED ||
        cna_video_player_get_video(player, &playing, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || playing != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* Asking for a frame before playback is an ordinary "none", not a fault: the canonical
       implementation deliberately answers null where the original API would crash. */
    if (cna_video_player_get_texture(player, &frame, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || frame != CNA_INVALID_HANDLE) {
        return 0;
    }

    if (cna_video_player_set_is_looped(player, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_is_looped(player, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_video_player_set_is_muted(player, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_is_muted(player, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_video_player_set_volume(player, 0.5F) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_volume(player, &volume) != CNA_RESULT_SUCCESS || volume != 0.5F ||
        cna_video_player_get_play_position_ticks(player, &ticks) != CNA_RESULT_SUCCESS ||
        ticks < INT64_C(0)) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_video_player_get_type_name_size(player, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_video_player_copy_type_name(player, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.VideoPlayer") != 0) {
        return 0;
    }

    /* Playing a file the platform cannot decode leaves the player stopped rather than failing --
       the canonical behavior. The two answers are asserted as a relationship: a player that really
       started playing reports the video it was given, while one whose decoder failed reports none,
       because the canonical player clears its video in that case. */
    if (cna_video_create(device, view(path), &video) != CNA_RESULT_SUCCESS ||
        cna_video_player_play(player, video) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_state(player, &state) != CNA_RESULT_SUCCESS ||
        (state != CNA_MEDIA_STATE_STOPPED && state != CNA_MEDIA_STATE_PLAYING) ||
        cna_video_player_get_video(player, &playing, &flag) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (state == CNA_MEDIA_STATE_PLAYING) {
        if (flag != CNA_TRUE || playing != video) {
            return 0;
        }
    } else if (flag != CNA_FALSE || playing != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* Pause, resume and stop are safe in every state; stop always ends stopped. */
    if (cna_video_player_pause(player) != CNA_RESULT_SUCCESS ||
        cna_video_player_resume(player) != CNA_RESULT_SUCCESS ||
        cna_video_player_stop(player) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_state(player, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_MEDIA_STATE_STOPPED ||
        cna_video_player_set_audio_track_ext(player, 0) != CNA_RESULT_SUCCESS ||
        cna_video_player_set_video_track_ext(player, 0) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Releasing the caller's video handle cannot leave the player pointing at a destroyed video,
       because the player retains what it was given. */
    if (cna_video_destroy(video) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_state(player, &state) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Disposal refuses every playback route with the canonical disposed-object failure, while the
       disposal query itself keeps answering. */
    if (cna_video_player_dispose(player) != CNA_RESULT_SUCCESS ||
        cna_video_player_get_is_disposed(player, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_video_player_stop(player) != CNA_RESULT_INVALID_STATE ||
        cna_video_player_get_texture(player, &frame, &flag) != CNA_RESULT_INVALID_STATE ||
        cna_video_player_destroy(player) != CNA_RESULT_SUCCESS ||
        cna_video_player_destroy(player) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    return cna_video_player_get_is_disposed(rejected, &flag) == CNA_RESULT_INVALID_HANDLE &&
        cna_video_player_stop(rejected) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    VideoSmokeState* const state = (VideoSmokeState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time == 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (!validate_video_values(graphics_device, FixturePath) ||
        !validate_video_player(game, graphics_device, FixturePath)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_Bool disposed = CNA_FALSE;
    state->result = cna_video_player_get_is_disposed(state->player, &disposed);
    return 0;
}

int main(void)
{
    if (!write_fixture()) {
        return 1;
    }

    VideoSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API video smoke", UINT64_C(17)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        (void)remove(FixturePath);
        return 2;
    }

    CNA_VideoPlayerHandle player = CNA_INVALID_HANDLE;
    if (cna_video_player_create(game, &player) != CNA_RESULT_SUCCESS) {
        (void)remove(FixturePath);
        return 3;
    }
    WrongThreadState wrong_thread = {player, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        (void)remove(FixturePath);
        return 4;
    }
    if (cna_video_player_destroy(player) != CNA_RESULT_SUCCESS) {
        (void)remove(FixturePath);
        return 5;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        (void)remove(FixturePath);
        return 6;
    }
    (void)remove(FixturePath);
    return 0;
}
