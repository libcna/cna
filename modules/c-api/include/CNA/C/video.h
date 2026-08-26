// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_VIDEO_H
#define CNA_C_VIDEO_H

#include "CNA/C/media.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle to one video asset. */
typedef CNA_Handle CNA_VideoHandle;

/** @brief Owned handle to one video player. */
typedef CNA_Handle CNA_VideoPlayerHandle;

/**
 * @brief Basic video file metadata.
 *
 * The canonical nested value carries exactly these three numbers. C fills it from a video rather
 * than leaving it a type with no producer.
 */
typedef struct CNA_VideoInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Frame width in pixels. */
    int32_t width;

    /** @brief Frame height in pixels. */
    int32_t height;

    /** @brief Frames per second. */
    double fps;
} CNA_VideoInfo;

/**
 * @brief Initializes video metadata to the canonical default.
 *
 * @param out_info Receives zeroed width, height and frame rate.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_video_info_init(CNA_VideoInfo* out_info);

/**
 * @brief Creates a video from a file, probing the file for its metadata.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param file_name UTF-8 path to the video file.
 * @param out_video Receives an owned video handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when no file exists at that path,
 *         `CNA_RESULT_NOT_SUPPORTED` when CNA was built without its optional video decoder,
 *         `CNA_RESULT_ENCODING`, or a documented argument/handle/thread/native failure.
 *
 * A file that exists but cannot be decoded is **not** an error here: the canonical constructor
 * leaves the width, height, frame rate and duration at zero, and this route reports that faithfully
 * rather than inventing a failure. Playing such a video is what surfaces the problem.
 */
CNA_C_API CNA_Result cna_video_create(
    CNA_Handle graphics_device,
    CNA_StringView file_name,
    CNA_VideoHandle* out_video);

/**
 * @brief Creates a video with explicit, trusted metadata.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param file_name UTF-8 path to the video file.
 * @param duration_milliseconds Declared duration in milliseconds.
 * @param width Declared frame width in pixels.
 * @param height Declared frame height in pixels.
 * @param frames_per_second Declared frame rate.
 * @param soundtrack_type One `CNA_VIDEO_SOUNDTRACK_TYPE_*` identity.
 * @param out_video Receives an owned video handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined soundtrack identity,
 *         `CNA_RESULT_ENCODING`, or a documented argument/handle/thread/native failure.
 *
 * This is the compiled-asset constructor: it does **not** touch the file or require an installed
 * video decoder, so a wrong path or disabled decoder is only discovered when the video is played.
 * Declared metadata that disagrees with the real file is refused at play time, not here.
 */
CNA_C_API CNA_Result cna_video_create_with_metadata(
    CNA_Handle graphics_device,
    CNA_StringView file_name,
    int32_t duration_milliseconds,
    int32_t width,
    int32_t height,
    float frames_per_second,
    CNA_VideoSoundtrackType soundtrack_type,
    CNA_VideoHandle* out_video);

/**
 * @brief Creates a video from a file URI or plain path.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param uri UTF-8 `file:` URI or plain path.
 * @param out_video Receives an owned video handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when nothing exists at that path,
 *         `CNA_RESULT_NOT_SUPPORTED` when CNA was built without its optional video decoder,
 *         `CNA_RESULT_ENCODING`, or a documented argument/handle/thread failure.
 *
 * The canonical factory carries an `EXT` suffix because it is an extension beyond XNA 4.0 rather
 * than a CNA invention, and this route keeps it. **It does not parse the string as a URI**: unlike
 * the song factory, it hands the text straight to the file constructor, so a `file:` URI is not
 * resolved and a `http:` one is simply a path that does not exist — `CNA_RESULT_IO`, not a
 * scheme refusal. That is the canonical behavior, reported rather than corrected here.
 */
CNA_C_API CNA_Result cna_video_create_from_uri_ext(
    CNA_Handle graphics_device,
    CNA_StringView uri,
    CNA_VideoHandle* out_video);

/**
 * @brief Returns a video's frame width in pixels.
 *
 * @param video Owned video handle.
 * @param out_width Receives the width, or zero when the file could not be probed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_width(CNA_VideoHandle video, int32_t* out_width);

/**
 * @brief Returns a video's frame height in pixels.
 *
 * @param video Owned video handle.
 * @param out_height Receives the height, or zero when the file could not be probed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_height(CNA_VideoHandle video, int32_t* out_height);

/**
 * @brief Returns a video's frame rate.
 *
 * @param video Owned video handle.
 * @param out_fps Receives the frame rate, or zero when the file could not be probed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_frames_per_second(CNA_VideoHandle video, float* out_fps);

/**
 * @brief Fills the metadata triple for a video.
 *
 * @param video Owned video handle.
 * @param out_info Receives the width, height and frame rate.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid structure, or
 *         a documented handle/thread failure.
 *
 * This is the canonical nested metadata value, filled from a video so it has a real producer.
 */
CNA_C_API CNA_Result cna_video_get_info(CNA_VideoHandle video, CNA_VideoInfo* out_info);

/**
 * @brief Returns the kind of audio content a video declares.
 *
 * @param video Owned video handle.
 * @param out_type Receives one `CNA_VIDEO_SOUNDTRACK_TYPE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Metadata only: no playback path branches on it.
 */
CNA_C_API CNA_Result cna_video_get_soundtrack_type(
    CNA_VideoHandle video,
    CNA_VideoSoundtrackType* out_type);

/**
 * @brief Returns a video's total duration.
 *
 * @param video Owned video handle.
 * @param out_ticks Receives the duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_duration(CNA_VideoHandle video, int64_t* out_ticks);

/**
 * @brief Sets a video's total duration.
 *
 * @param video Owned video handle.
 * @param ticks New duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical setter exists for the loader to correct a declared duration; it validates nothing,
 * and neither does this route.
 */
CNA_C_API CNA_Result cna_video_set_duration(CNA_VideoHandle video, int64_t ticks);

/**
 * @brief Selects which audio stream a video uses.
 *
 * @param video Owned video handle.
 * @param track Zero-based audio stream index.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The index is stored and applied when the video is next opened; an index the file does not have
 * is discovered then, not here.
 */
CNA_C_API CNA_Result cna_video_set_audio_track_ext(CNA_VideoHandle video, int32_t track);

/**
 * @brief Selects which video stream a video uses.
 *
 * @param video Owned video handle.
 * @param track Zero-based video stream index.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_set_video_track_ext(CNA_VideoHandle video, int32_t track);

/**
 * @brief Returns the byte count of the file path a video was created from.
 *
 * @param video Owned video handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_file_name_size(CNA_VideoHandle video, uint64_t* out_bytes);

/**
 * @brief Copies the file path a video was created from.
 *
 * @param video Owned video handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_copy_file_name(
    CNA_VideoHandle video,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a video is bound to a graphics device.
 *
 * @param video Owned video handle.
 * @param out_bound Receives `CNA_TRUE` when the video carries a device.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The device itself is not handed back: a borrowed device handle is valid only inside the callback
 * that produced it, so a video created in one frame cannot hand out a usable one later. This
 * reports presence, which is what the canonical getter's null check is for.
 */
CNA_C_API CNA_Result cna_video_get_has_graphics_device(
    CNA_VideoHandle video,
    CNA_Bool* out_bound);

/**
 * @brief Returns the byte count of the video type's fully-qualified .NET type name.
 *
 * @param video Owned video handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_get_type_name_size(CNA_VideoHandle video, uint64_t* out_bytes);

/**
 * @brief Copies the video type's fully-qualified .NET type name.
 *
 * @param video Owned video handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_copy_type_name(
    CNA_VideoHandle video,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases an owned video.
 *
 * @param video Owned video handle with no player still playing it.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_destroy(CNA_VideoHandle video);

/**
 * @brief Creates a video player in the stopped state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_player Receives an owned player handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_create(CNA_Handle game, CNA_VideoPlayerHandle* out_player);

/**
 * @brief Reports whether a video player has been disposed.
 *
 * @param player Owned player handle.
 * @param out_disposed Receives `CNA_TRUE` when the player has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_is_disposed(
    CNA_VideoPlayerHandle player,
    CNA_Bool* out_disposed);

/**
 * @brief Reports whether playback loops.
 *
 * @param player Owned player handle.
 * @param out_looped Receives `CNA_TRUE` when playback loops.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_is_looped(
    CNA_VideoPlayerHandle player,
    CNA_Bool* out_looped);

/**
 * @brief Sets whether playback loops.
 *
 * @param player Owned player handle.
 * @param looped New looping state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_set_is_looped(
    CNA_VideoPlayerHandle player,
    CNA_Bool looped);

/**
 * @brief Reports whether playback is muted.
 *
 * @param player Owned player handle.
 * @param out_muted Receives `CNA_TRUE` when playback is muted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_is_muted(
    CNA_VideoPlayerHandle player,
    CNA_Bool* out_muted);

/**
 * @brief Sets whether playback is muted.
 *
 * @param player Owned player handle.
 * @param muted New muted state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_set_is_muted(
    CNA_VideoPlayerHandle player,
    CNA_Bool muted);

/**
 * @brief Returns the current playback position.
 *
 * @param player Owned player handle.
 * @param out_ticks Receives the position in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_play_position_ticks(
    CNA_VideoPlayerHandle player,
    int64_t* out_ticks);

/**
 * @brief Returns the current playback state.
 *
 * @param player Owned player handle.
 * @param out_state Receives one `CNA_MEDIA_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_state(
    CNA_VideoPlayerHandle player,
    CNA_MediaState* out_state);

/**
 * @brief Returns the video a player is playing.
 *
 * @param player Owned player handle.
 * @param out_video Receives a borrowed video handle when one is playing; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the player has a video.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The handle refers to the same video the caller passed to `cna_video_player_play`; it does not
 * transfer ownership. A video whose file the platform could not open is **not** reported: the
 * canonical player clears its video when the decoder fails, so the answer is `CNA_FALSE` and the
 * player is left stopped.
 */
CNA_C_API CNA_Result cna_video_player_get_video(
    CNA_VideoPlayerHandle player,
    CNA_VideoHandle* out_video,
    CNA_Bool* out_available);

/**
 * @brief Returns the current playback volume.
 *
 * @param player Owned player handle.
 * @param out_volume Receives the volume in the range 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_volume(
    CNA_VideoPlayerHandle player,
    float* out_volume);

/**
 * @brief Sets the playback volume.
 *
 * @param player Owned player handle.
 * @param volume New volume; the canonical setter clamps it to 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_set_volume(CNA_VideoPlayerHandle player, float volume);

/**
 * @brief Returns the current video frame as a texture.
 *
 * @param player Owned player handle.
 * @param out_texture Receives a borrowed texture handle when a frame exists; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when a frame texture exists.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the player has been disposed, or a
 *         documented argument/handle/thread/native failure.
 *
 * **The returned handle is valid only until the next call on this player.** The player owns and
 * replaces its frame texture, so any later call — including another `get_texture` — invalidates the
 * handle it handed out, and using it afterwards fails with `CNA_RESULT_INVALID_HANDLE` rather than
 * touching freed memory. Draw with it or copy its pixels before calling anything else.
 *
 * Asking before playback has produced a frame is an ordinary answer of `CNA_FALSE`, not a failure:
 * the canonical implementation deliberately returns null there where the original API would fault.
 */
/** @brief Version of @ref CNA_VideoFrameEXT understood by this header. */
#define CNA_VIDEO_FRAME_EXT_STRUCT_VERSION UINT32_C(1)

/**
 * @brief A borrowed view of the frame a VideoPlayer currently holds.
 *
 * `cna_video_player_get_texture` hands back a fresh handle on every call, so two calls against one
 * undecoded frame are indistinguishable from two calls across a frame advance. This descriptor
 * answers that: `generation` changes only when a frame is actually decoded.
 *
 * There is deliberately **no slot or buffer index**. XNA owns two frame textures and alternates
 * between them, so its callers can rely on two stable identities; CNA decodes into a single
 * texture in place. A slot field here would report an alternation that does not happen, so a
 * binding modelling XNA's two slots must map both onto this one frame and use `generation` for
 * change detection.
 */
typedef struct CNA_VideoFrameEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Borrowed frame texture, or `CNA_INVALID_HANDLE` when no frame exists. */
    CNA_Handle texture;
    /** @brief Frames decoded since playback started; zero before the first. */
    uint64_t generation;
    /** @brief Presentation timestamp of the held frame in seconds; negative when none. */
    double presentation_time;
    /** @brief Whether a frame texture exists. */
    CNA_Bool available;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[3];
} CNA_VideoFrameEXT;

/**
 * @brief Reads the current frame together with the identity a caller needs to track it.
 *
 * The texture is borrowed on exactly the terms @ref cna_video_player_get_texture documents: valid
 * only until the next call on this player, after which the handle fails with
 * `CNA_RESULT_INVALID_HANDLE` rather than touching freed memory.
 *
 * `generation` is what this route adds. Equal across two calls means the same pixels; a higher
 * value means the frame advanced. `Stop` and playing a different video restart it, so a stale
 * generation can never compare equal across a change.
 *
 * @param player Owned player handle.
 * @param out_frame Receives the descriptor. Its `struct_size`/`struct_version` must be set by the
 *        caller before the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed descriptor,
 *         `CNA_RESULT_INVALID_STATE` when the player has been disposed, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_get_frame_ext(
    CNA_VideoPlayerHandle player,
    CNA_VideoFrameEXT* out_frame);

CNA_C_API CNA_Result cna_video_player_get_texture(
    CNA_VideoPlayerHandle player,
    CNA_Handle* out_texture,
    CNA_Bool* out_available);

/**
 * @brief Starts playing a video from the beginning.
 *
 * @param player Owned player handle.
 * @param video Owned video handle; it stays the caller's.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the player has been disposed or the
 *         video's declared metadata disagrees with the file, `CNA_RESULT_NOT_SUPPORTED` when CNA
 *         was built without its optional video decoder, or a documented argument/handle/thread/
 *         native failure.
 *
 * A file the platform cannot decode leaves the player stopped rather than failing, exactly as the
 * canonical operation does — read `cna_video_player_get_state` back to see whether playback began.
 */
CNA_C_API CNA_Result cna_video_player_play(
    CNA_VideoPlayerHandle player,
    CNA_VideoHandle video);

/**
 * @brief Stops playback and resets the position.
 *
 * @param player Owned player handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the player has been disposed, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_stop(CNA_VideoPlayerHandle player);

/**
 * @brief Pauses playback.
 *
 * @param player Owned player handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the player has been disposed, or a
 *         documented handle/thread/native failure.
 *
 * Pausing when nothing is playing is a successful no-op.
 */
CNA_C_API CNA_Result cna_video_player_pause(CNA_VideoPlayerHandle player);

/**
 * @brief Resumes paused playback.
 *
 * @param player Owned player handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the player has been disposed, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_resume(CNA_VideoPlayerHandle player);

/**
 * @brief Selects which audio stream the player uses.
 *
 * @param player Owned player handle.
 * @param track Zero-based audio stream index.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_set_audio_track_ext(
    CNA_VideoPlayerHandle player,
    int32_t track);

/**
 * @brief Selects which video stream the player uses.
 *
 * @param player Owned player handle.
 * @param track Zero-based video stream index.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_video_player_set_video_track_ext(
    CNA_VideoPlayerHandle player,
    int32_t track);

/**
 * @brief Disposes a video player without releasing its handle.
 *
 * @param player Owned player handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Disposal releases the decoder and audio resources. Every playback route then refuses with
 * `CNA_RESULT_INVALID_STATE`, which is the canonical disposed-object failure; the disposal query
 * itself keeps answering.
 */
CNA_C_API CNA_Result cna_video_player_dispose(CNA_VideoPlayerHandle player);

/**
 * @brief Releases a video player handle.
 *
 * @param player Owned player handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_destroy(CNA_VideoPlayerHandle player);

/**
 * @brief Returns the byte count of the player type's fully-qualified .NET type name.
 *
 * @param player Owned player handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_get_type_name_size(
    CNA_VideoPlayerHandle player,
    uint64_t* out_bytes);

/**
 * @brief Copies the player type's fully-qualified .NET type name.
 *
 * @param player Owned player handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_video_player_copy_type_name(
    CNA_VideoPlayerHandle player,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
