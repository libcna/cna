// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_XACT_H
#define CNA_C_XACT_H

#include "CNA/C/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Content version every XACT settings file this runtime loads must declare.
 *
 * A settings file built by a different tool version is refused rather than reinterpreted.
 */
#define CNA_AUDIO_ENGINE_CONTENT_VERSION INT32_C(46)

/**
 * @brief Snapshot of everything a cue reports about its own playback.
 *
 * A cue has eight independent state predicates, and reading them one route at a time would let a
 * caller see a torn combination that never existed. One versioned snapshot answers all eight from a
 * single observation.
 */
typedef struct CNA_CueInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Non-zero while the cue exists but has never been prepared or played. */
    CNA_Bool is_created;

    /** @brief Non-zero once the cue has been disposed. */
    CNA_Bool is_disposed;

    /** @brief Non-zero while the cue is paused. */
    CNA_Bool is_paused;

    /** @brief Non-zero while the cue is playing. */
    CNA_Bool is_playing;

    /** @brief Non-zero once the cue is prepared and ready to play. */
    CNA_Bool is_prepared;

    /** @brief Non-zero while the cue is still preparing. */
    CNA_Bool is_preparing;

    /** @brief Non-zero once the cue has stopped. */
    CNA_Bool is_stopped;

    /** @brief Non-zero while the cue is stopping but has not stopped yet. */
    CNA_Bool is_stopping;
} CNA_CueInfo;

/* ---- AudioEngine ---- */

/**
 * @brief Opens an XACT engine from a settings file.
 *
 * @param game Owning game handle.
 * @param settings_file Path to the `.xgs` settings file, borrowed for the duration of the call.
 * @param out_engine Receives an owned engine handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or unreadable file,
 *         `CNA_RESULT_INVALID_ARGUMENT` for a malformed file or bad arguments, or a documented
 *         handle/thread/native failure.
 *
 * The engine is a child of the game and is affine to the thread that created it.
 */
CNA_C_API CNA_Result cna_audio_engine_create(
    CNA_Handle game,
    CNA_StringView settings_file,
    CNA_Handle* out_engine);

/**
 * @brief Opens an XACT engine with an explicit look-ahead and renderer.
 *
 * @param game Owning game handle.
 * @param settings_file Path to the `.xgs` settings file, borrowed for the duration of the call.
 * @param look_ahead_ticks Look-ahead time in 100-nanosecond ticks.
 * @param renderer_id Renderer to open, borrowed for the duration of the call; an empty view selects
 *        the default renderer.
 * @param out_engine Receives an owned engine handle, or `CNA_INVALID_HANDLE` on failure.
 * @return The same answers as @ref cna_audio_engine_create.
 *
 * **Both extra arguments are accepted and ignored.** This runtime has exactly one audio backend, so
 * there is nothing for a renderer id to select between, and the backend has no scheduling look-ahead
 * to configure. The route exists because the canonical constructor does; it is not a way to pick a
 * renderer.
 */
CNA_C_API CNA_Result cna_audio_engine_create_with_renderer(
    CNA_Handle game,
    CNA_StringView settings_file,
    int64_t look_ahead_ticks,
    CNA_StringView renderer_id,
    CNA_Handle* out_engine);

/**
 * @brief Disposes an engine and releases its handle.
 *
 * @param engine Owned engine handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while a C bank or category child is still
 *         alive, or a documented handle/thread failure.
 *
 * Children are released before their parent throughout this ABI: cues before their sound bank,
 * banks and categories before their engine, the engine before its game.
 */
CNA_C_API CNA_Result cna_audio_engine_destroy(CNA_Handle engine);

/**
 * @brief Reports whether an engine has been disposed.
 *
 * @param engine Owned engine handle.
 * @param out_is_disposed Receives non-zero when the engine has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_get_is_disposed(CNA_Handle engine, CNA_Bool* out_is_disposed);

/**
 * @brief Reports how many audio renderers the engine describes.
 *
 * @param engine Owned engine handle.
 * @param out_count Receives the renderer count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A renderer detail is addressed by index rather than copied into a C value, because its two strings
 * are owned by the engine and the canonical type has no public constructor at all.
 */
CNA_C_API CNA_Result cna_audio_engine_get_renderer_count(CNA_Handle engine, uint64_t* out_count);

/**
 * @brief Reports the byte length of a renderer's friendly name.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown index, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_get_renderer_friendly_name_size(
    CNA_Handle engine,
    uint64_t renderer_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a renderer's friendly name.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written,
 *         `CNA_RESULT_INVALID_ARGUMENT` for an unknown index, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_copy_renderer_friendly_name(
    CNA_Handle engine,
    uint64_t renderer_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of a renderer's identifier.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return The same answers as @ref cna_audio_engine_get_renderer_friendly_name_size.
 */
CNA_C_API CNA_Result cna_audio_engine_get_renderer_id_size(
    CNA_Handle engine,
    uint64_t renderer_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a renderer's identifier.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_audio_engine_copy_renderer_friendly_name.
 */
CNA_C_API CNA_Result cna_audio_engine_copy_renderer_id(
    CNA_Handle engine,
    uint64_t renderer_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of a renderer's text form.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return The same answers as @ref cna_audio_engine_get_renderer_friendly_name_size.
 */
CNA_C_API CNA_Result cna_audio_engine_get_renderer_text_size(
    CNA_Handle engine,
    uint64_t renderer_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a renderer's text form.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_audio_engine_copy_renderer_friendly_name.
 */
CNA_C_API CNA_Result cna_audio_engine_copy_renderer_text(
    CNA_Handle engine,
    uint64_t renderer_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports a renderer's hash code.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based renderer index.
 * @param out_hash_code Receives the canonical hash code.
 * @return The same answers as @ref cna_audio_engine_get_renderer_friendly_name_size.
 */
CNA_C_API CNA_Result cna_audio_engine_get_renderer_hash_code(
    CNA_Handle engine,
    uint64_t renderer_index,
    int32_t* out_hash_code);

/**
 * @brief Reports whether two renderers are equal.
 *
 * @param engine Owned engine handle.
 * @param renderer_index Zero-based index of the first renderer.
 * @param other_renderer_index Zero-based index of the second renderer.
 * @param out_equals Receives non-zero when the two are equal.
 * @return The same answers as @ref cna_audio_engine_get_renderer_friendly_name_size.
 *
 * This is the single route behind the canonical equality method and both equality operators: the
 * inequality operator is its negation, and a C caller needs one answer rather than three.
 */
CNA_C_API CNA_Result cna_audio_engine_renderers_equal(
    CNA_Handle engine,
    uint64_t renderer_index,
    uint64_t other_renderer_index,
    CNA_Bool* out_equals);

/**
 * @brief Reads an engine-global variable.
 *
 * @param engine Owned engine handle.
 * @param name Variable name, borrowed for the duration of the call.
 * @param out_value Receives the current value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown or cue-scoped variable,
 *         `CNA_RESULT_INVALID_STATE` for a disposed engine, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_get_global_variable(
    CNA_Handle engine,
    CNA_StringView name,
    float* out_value);

/**
 * @brief Writes an engine-global variable.
 *
 * @param engine Owned engine handle.
 * @param name Variable name, borrowed for the duration of the call.
 * @param value New value, clamped to the variable's authored range.
 * @return The same answers as @ref cna_audio_engine_get_global_variable.
 *
 * **Writing a read-only variable succeeds and changes nothing.** The canonical route never reports
 * that refusal, so neither does this one; read the value back to see what actually took effect.
 */
CNA_C_API CNA_Result cna_audio_engine_set_global_variable(
    CNA_Handle engine,
    CNA_StringView name,
    float value);

/**
 * @brief Advances the engine's own bookkeeping.
 *
 * @param engine Owned engine handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed engine, or a documented
 *         handle/thread failure.
 *
 * This is what retires finished fire-and-forget cues; a game that only ever calls
 * @ref cna_sound_bank_play_cue still needs to call it.
 */
CNA_C_API CNA_Result cna_audio_engine_update(CNA_Handle engine);

/**
 * @brief Reports the byte length of the engine's canonical type name.
 *
 * @param engine Owned engine handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_get_type_name_size(CNA_Handle engine, uint64_t* out_bytes);

/**
 * @brief Copies the engine's canonical type name.
 *
 * @param engine Owned engine handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_engine_copy_type_name(
    CNA_Handle engine,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to the engine's disposal notification.
 *
 * @param engine Owned engine handle.
 * @param callback Callback invoked synchronously while the engine is being disposed.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_audio_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure. A refused call routes
 *         the output handle to `CNA_INVALID_HANDLE` before it validates anything else.
 */
CNA_C_API CNA_Result cna_audio_engine_subscribe_disposing_ext(
    CNA_Handle engine,
    CNA_AudioEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

/* ---- AudioCategory ---- */

/**
 * @brief Looks up a named category in an engine.
 *
 * @param engine Owned engine handle.
 * @param name Category name, borrowed for the duration of the call.
 * @param out_category Receives an owned category handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown name,
 *         `CNA_RESULT_INVALID_STATE` for a disposed engine, or a documented handle/thread failure.
 *
 * The canonical lookup answers a value rather than a reference, and two lookups of the same name
 * answer two equal values. The handle here is what gives C somewhere to keep that value.
 */
CNA_C_API CNA_Result cna_audio_engine_get_category(
    CNA_Handle engine,
    CNA_StringView name,
    CNA_Handle* out_category);

/**
 * @brief Releases a category handle.
 *
 * @param category Owned category handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing a category changes nothing about the engine or about playback; it only gives back the
 * handle.
 */
CNA_C_API CNA_Result cna_audio_category_destroy(CNA_Handle category);

/**
 * @brief Reports the byte length of a category's name.
 *
 * @param category Owned category handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_category_get_name_size(CNA_Handle category, uint64_t* out_bytes);

/**
 * @brief Copies a category's name.
 *
 * @param category Owned category handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_category_copy_name(
    CNA_Handle category,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Pauses every playing cue in a category.
 *
 * @param category Owned category handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * **A category whose engine has been disposed does nothing and reports success.** That is the
 * canonical behavior: the operation is a no-op rather than a failure.
 */
CNA_C_API CNA_Result cna_audio_category_pause(CNA_Handle category);

/**
 * @brief Resumes every paused cue in a category.
 *
 * @param category Owned category handle.
 * @return The same answers as @ref cna_audio_category_pause.
 */
CNA_C_API CNA_Result cna_audio_category_resume(CNA_Handle category);

/**
 * @brief Sets a category's baseline volume.
 *
 * @param category Owned category handle.
 * @param volume New volume level.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite volume, or a
 *         documented handle/thread failure.
 *
 * The new volume applies **retroactively** to cues in this category that are already playing, not
 * only to later ones.
 */
CNA_C_API CNA_Result cna_audio_category_set_volume(CNA_Handle category, float volume);

/**
 * @brief Stops every active cue in a category.
 *
 * @param category Owned category handle.
 * @param options Whether to stop immediately or let authored release phases finish.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined stop option, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_category_stop(CNA_Handle category, CNA_AudioStopOptions options);

/**
 * @brief Reports whether two categories are equal.
 *
 * @param category Owned category handle.
 * @param other Owned category handle to compare with.
 * @param out_equals Receives non-zero when the two are equal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **Categories compare by name alone.** Two categories with the same name from two different engines
 * are equal, which is the canonical behavior rather than an accident of this binding. This one route
 * is what both canonical equality operators map to; inequality is its negation.
 */
CNA_C_API CNA_Result cna_audio_category_equals(
    CNA_Handle category,
    CNA_Handle other,
    CNA_Bool* out_equals);

/**
 * @brief Reports a category's hash code.
 *
 * @param category Owned category handle.
 * @param out_hash_code Receives the canonical hash code, which is derived from the name alone.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_audio_category_get_hash_code(CNA_Handle category, int32_t* out_hash_code);

/* ---- WaveBank ---- */

/**
 * @brief Opens a fully resident wave bank.
 *
 * @param engine Owned engine handle.
 * @param filename Path to the `.xwb` file, borrowed for the duration of the call.
 * @param out_wave_bank Receives an owned wave-bank handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or unreadable file,
 *         `CNA_RESULT_INVALID_ARGUMENT` for a malformed file or bad arguments, or a documented
 *         handle/thread/native failure.
 *
 * The bank registers itself with the engine under the name recorded inside the file, which is how a
 * sound bank later resolves it. Two banks are not required to have distinct names.
 */
CNA_C_API CNA_Result cna_wave_bank_create(
    CNA_Handle engine,
    CNA_StringView filename,
    CNA_Handle* out_wave_bank);

/**
 * @brief Opens a streaming wave bank.
 *
 * @param engine Owned engine handle.
 * @param filename Path to the `.xwb` file, borrowed for the duration of the call.
 * @param offset Byte offset of the bank inside the file.
 * @param packet_size Streaming packet size in sectors.
 * @param out_wave_bank Receives an owned wave-bank handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or a path that is
 *         not valid UTF-8, or a documented handle/thread failure.
 *
 * **This does *not* answer the same way as `cna_wave_bank_create`, and the difference matters.**
 * That one reports a missing or malformed `.xwb` as `CNA_RESULT_IO`. The canonical *streaming*
 * constructor swallows every parse failure -- it logs and returns -- so this route answers
 * `CNA_RESULT_SUCCESS` and hands back a live but **empty** wave bank for a file that does not
 * exist at all. A caller that needs to know whether the bank has content must ask it, rather than
 * reading success as "the file was there".
 *
 * This header claimed the opposite until `CBIND-065`, which found it by writing the first test
 * that called this route. The behaviour is canonical rather than introduced by this ABI, and
 * `XactSmoke.c` now pins it, so a canonical change becomes visible instead of silent.
 */
CNA_C_API CNA_Result cna_wave_bank_create_streaming(
    CNA_Handle engine,
    CNA_StringView filename,
    int32_t offset,
    int16_t packet_size,
    CNA_Handle* out_wave_bank);

/**
 * @brief Disposes a wave bank and releases its handle.
 *
 * @param wave_bank Owned wave-bank handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_destroy(CNA_Handle wave_bank);

/**
 * @brief Reports whether a wave bank has been disposed.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param out_is_disposed Receives non-zero when the bank has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_get_is_disposed(CNA_Handle wave_bank, CNA_Bool* out_is_disposed);

/**
 * @brief Reports whether a wave bank is ready for use.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param out_is_prepared Receives non-zero when the bank is prepared.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_get_is_prepared(CNA_Handle wave_bank, CNA_Bool* out_is_prepared);

/**
 * @brief Reports whether any cue is currently using a wave bank.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param out_is_in_use Receives non-zero while a cue is using the bank.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_get_is_in_use(CNA_Handle wave_bank, CNA_Bool* out_is_in_use);

/**
 * @brief Reports the byte length of the wave bank's canonical type name.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_get_type_name_size(CNA_Handle wave_bank, uint64_t* out_bytes);

/**
 * @brief Copies the wave bank's canonical type name.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_copy_type_name(
    CNA_Handle wave_bank,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to a wave bank's disposal notification.
 *
 * @param wave_bank Owned wave-bank handle.
 * @param callback Callback invoked synchronously while the bank is being disposed.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_audio_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_wave_bank_subscribe_disposing_ext(
    CNA_Handle wave_bank,
    CNA_AudioEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

/* ---- SoundBank ---- */

/**
 * @brief Opens a sound bank.
 *
 * @param engine Owned engine handle.
 * @param filename Path to the `.xsb` file, borrowed for the duration of the call.
 * @param out_sound_bank Receives an owned sound-bank handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or unreadable file,
 *         `CNA_RESULT_INVALID_ARGUMENT` for a malformed file or bad arguments, or a documented
 *         handle/thread/native failure.
 *
 * The wave banks a sound bank names do not have to be open yet; a cue resolves its wave bank when it
 * plays.
 */
CNA_C_API CNA_Result cna_sound_bank_create(
    CNA_Handle engine,
    CNA_StringView filename,
    CNA_Handle* out_sound_bank);

/**
 * @brief Disposes a sound bank and releases its handle.
 *
 * @param sound_bank Owned sound-bank handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while a C cue child is still alive, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_destroy(CNA_Handle sound_bank);

/**
 * @brief Reports whether a sound bank has been disposed.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param out_is_disposed Receives non-zero when the bank has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_get_is_disposed(
    CNA_Handle sound_bank,
    CNA_Bool* out_is_disposed);

/**
 * @brief Reports whether any cue from a sound bank is currently playing.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param out_is_in_use Receives non-zero while a cue from the bank is playing.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_get_is_in_use(CNA_Handle sound_bank, CNA_Bool* out_is_in_use);

/**
 * @brief Prepares a named cue the caller then drives itself.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param name Cue name, borrowed for the duration of the call.
 * @param out_cue Receives an owned cue handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an empty name,
 *         `CNA_RESULT_INVALID_STATE` for an unknown cue name or a disposed bank, or a documented
 *         handle/thread failure.
 *
 * **Each call answers a new cue**, not a shared one: two calls with the same name give two
 * independently playable cues. The caller owns each of them.
 */
CNA_C_API CNA_Result cna_sound_bank_get_cue(
    CNA_Handle sound_bank,
    CNA_StringView name,
    CNA_Handle* out_cue);

/**
 * @brief Plays a named cue the bank owns and forgets.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param name Cue name, borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an empty name,
 *         `CNA_RESULT_INVALID_STATE` for an unknown cue name or a disposed bank, or a documented
 *         handle/thread failure.
 *
 * The cue this plays has **no handle at all**, because the caller never gets to touch it: the bank
 * keeps it alive while it plays and retires it on a later `cna_audio_engine_update`.
 */
CNA_C_API CNA_Result cna_sound_bank_play_cue(CNA_Handle sound_bank, CNA_StringView name);

/**
 * @brief Plays a named fire-and-forget cue positioned in space.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param name Cue name, borrowed for the duration of the call.
 * @param listener Where the ears are.
 * @param emitter Where the sound is.
 * @return The same answers as @ref cna_sound_bank_play_cue, plus `CNA_RESULT_INVALID_ARGUMENT` for
 *         an invalid listener or emitter structure.
 */
CNA_C_API CNA_Result cna_sound_bank_play_cue_3d(
    CNA_Handle sound_bank,
    CNA_StringView name,
    const CNA_AudioListener* listener,
    const CNA_AudioEmitter* emitter);

/**
 * @brief Reports the byte length of the sound bank's canonical type name.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_get_type_name_size(CNA_Handle sound_bank, uint64_t* out_bytes);

/**
 * @brief Copies the sound bank's canonical type name.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_copy_type_name(
    CNA_Handle sound_bank,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to a sound bank's disposal notification.
 *
 * @param sound_bank Owned sound-bank handle.
 * @param callback Callback invoked synchronously while the bank is being disposed.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_audio_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_bank_subscribe_disposing_ext(
    CNA_Handle sound_bank,
    CNA_AudioEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

/* ---- Cue ---- */

/**
 * @brief Disposes a cue and releases its handle.
 *
 * @param cue Owned cue handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_destroy(CNA_Handle cue);

/**
 * @brief Reads every playback predicate a cue reports.
 *
 * @param cue Owned cue handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_get_info(CNA_Handle cue, CNA_CueInfo* out_info);

/**
 * @brief Reports the byte length of a cue's name.
 *
 * @param cue Owned cue handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_get_name_size(CNA_Handle cue, uint64_t* out_bytes);

/**
 * @brief Copies a cue's name.
 *
 * @param cue Owned cue handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_copy_name(
    CNA_Handle cue,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Positions a cue relative to a listener.
 *
 * @param cue Owned cue handle.
 * @param listener Where the ears are.
 * @param emitter Where the sound is.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid listener or emitter,
 *         `CNA_RESULT_INVALID_STATE` for a disposed cue, or a documented handle/thread failure.
 *
 * This is the cue's own positioning, distinct from a sound effect instance's: it applies to every
 * sound the cue is playing.
 */
CNA_C_API CNA_Result cna_cue_apply_3d(
    CNA_Handle cue,
    const CNA_AudioListener* listener,
    const CNA_AudioEmitter* emitter);

/**
 * @brief Reads a cue-scoped variable.
 *
 * @param cue Owned cue handle.
 * @param name Variable name, borrowed for the duration of the call.
 * @param out_value Receives the current value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown variable,
 *         `CNA_RESULT_INVALID_STATE` for a disposed cue, or a documented handle/thread failure.
 *
 * Cue-scoped and engine-global variables are **separate domains**: a name reachable here is refused
 * by `cna_audio_engine_get_global_variable`, and the other way round.
 */
CNA_C_API CNA_Result cna_cue_get_variable(CNA_Handle cue, CNA_StringView name, float* out_value);

/**
 * @brief Writes a cue-scoped variable.
 *
 * @param cue Owned cue handle.
 * @param name Variable name, borrowed for the duration of the call.
 * @param value New value, clamped to the variable's authored range.
 * @return The same answers as @ref cna_cue_get_variable.
 */
CNA_C_API CNA_Result cna_cue_set_variable(CNA_Handle cue, CNA_StringView name, float value);

/**
 * @brief Starts playing a cue.
 *
 * @param cue Owned cue handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed or already-playing cue, or
 *         a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_play(CNA_Handle cue);

/**
 * @brief Pauses a playing cue.
 *
 * @param cue Owned cue handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed cue, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_pause(CNA_Handle cue);

/**
 * @brief Resumes a paused cue.
 *
 * @param cue Owned cue handle.
 * @return The same answers as @ref cna_cue_pause.
 */
CNA_C_API CNA_Result cna_cue_resume(CNA_Handle cue);

/**
 * @brief Stops a cue.
 *
 * @param cue Owned cue handle.
 * @param options Whether to stop immediately or let authored release phases finish.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined stop option,
 *         `CNA_RESULT_INVALID_STATE` for a disposed cue, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_stop(CNA_Handle cue, CNA_AudioStopOptions options);

/**
 * @brief Reports the byte length of the cue's canonical type name.
 *
 * @param cue Owned cue handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_get_type_name_size(CNA_Handle cue, uint64_t* out_bytes);

/**
 * @brief Copies the cue's canonical type name.
 *
 * @param cue Owned cue handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_copy_type_name(
    CNA_Handle cue,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to a cue's disposal notification.
 *
 * @param cue Owned cue handle.
 * @param callback Callback invoked synchronously while the cue is being disposed.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_audio_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_cue_subscribe_disposing_ext(
    CNA_Handle cue,
    CNA_AudioEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

#ifdef __cplusplus
}
#endif

#endif
