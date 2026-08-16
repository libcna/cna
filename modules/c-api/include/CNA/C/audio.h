// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_AUDIO_H
#define CNA_C_AUDIO_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width channel layout for interleaved PCM data. */
typedef uint32_t CNA_AudioChannels;

/** @brief Identifies monaural PCM data. */
#define CNA_AUDIO_CHANNELS_MONO UINT32_C(1)
/** @brief Identifies interleaved stereo PCM data. */
#define CNA_AUDIO_CHANNELS_STEREO UINT32_C(2)

/** @brief Fixed-width sound-effect-instance playback state. */
typedef uint32_t CNA_SoundState;

/** @brief Indicates that a sound instance is playing. */
#define CNA_SOUND_STATE_PLAYING UINT32_C(0)
/** @brief Indicates that a sound instance is paused. */
#define CNA_SOUND_STATE_PAUSED UINT32_C(1)
/** @brief Indicates that a sound instance is stopped. */
#define CNA_SOUND_STATE_STOPPED UINT32_C(2)

/** @brief Describes current native audio-playback availability. */
typedef struct CNA_AudioCapabilities {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when the native playback device can currently be opened. */
    CNA_Bool is_playback_available;

    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved0[3];

    /** @brief Reserved for future use; returned as zero. */
    uint32_t reserved1;
} CNA_AudioCapabilities;

/** @brief Configures creation of an owned sound effect from PCM16LE bytes. */
typedef struct CNA_SoundEffectCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Positive PCM sample rate in frames per second. */
    uint32_t sample_rate;

    /** @brief `CNA_AUDIO_CHANNELS_MONO` or `CNA_AUDIO_CHANNELS_STEREO`. */
    CNA_AudioChannels channels;

    /** @brief Reserved for future use; callers must initialize this to zero. */
    uint64_t reserved;
} CNA_SoundEffectCreateInfo;

/** @brief Immutable snapshot of controllable sound-effect-instance properties. */
typedef struct CNA_SoundEffectInstanceInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Current `CNA_SOUND_STATE_*` identity. */
    CNA_SoundState state;

    /** @brief `CNA_TRUE` when playback loops. */
    CNA_Bool is_looped;

    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved0[3];

    /** @brief Current per-instance volume, passed through by CNA without clamping. */
    float volume;

    /** @brief Current pitch adjustment after CNA clamping to `[-1, 1]`. */
    float pitch;

    /** @brief Current stereo pan in `[-1, 1]`. */
    float pan;

    /** @brief Reserved for future use; returned as zero. */
    uint32_t reserved1;
} CNA_SoundEffectInstanceInfo;

/**
 * @brief Probes native audio playback and returns an availability snapshot.
 *
 * @param game Active owned game handle used for creation-thread scope.
 * @param out_capabilities Caller-provided versioned structure to receive capabilities.
 * @return `CNA_RESULT_SUCCESS` even when playback is unavailable, or a documented
 * argument/handle/thread/native failure when the probe itself cannot be performed.
 *
 * The first call may initialize CNA's process-wide native audio mixer. Applications should use
 * `is_playback_available` instead of inferring support from the operating system, renderer or
 * build configuration. A false value is not an error and no owned C resource is created.
 */
CNA_C_API CNA_Result cna_audio_get_capabilities(
    CNA_Handle game,
    CNA_AudioCapabilities* out_capabilities);

/**
 * @brief Creates an owned sound effect by copying headerless signed PCM16LE bytes.
 *
 * @param game Active owned game handle used for lifetime and creation-thread scope.
 * @param create_info Versioned PCM format description.
 * @param pcm_bytes Interleaved signed PCM16LE bytes copied during this call.
 * @param byte_count Exact byte count; it must contain complete channel frames.
 * @param out_sound_effect Receives an owned sound-effect handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when no audio device is available, or
 * another documented argument/handle/thread/native failure.
 *
 * The returned effect must be destroyed after all instances created from it and before the game.
 * WAV, Ogg, MP3, XNB and other container bytes are not accepted by this raw PCM entry point.
 */
CNA_C_API CNA_Result cna_sound_effect_create_pcm16(
    CNA_Handle game,
    const CNA_SoundEffectCreateInfo* create_info,
    const uint8_t* pcm_bytes,
    uint64_t byte_count,
    CNA_Handle* out_sound_effect);

/**
 * @brief Gets the sound effect duration in 100-nanosecond ticks.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param out_duration_ticks Receives a nonnegative duration.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_duration_ticks(
    CNA_Handle sound_effect,
    int64_t* out_duration_ticks);

/**
 * @brief Creates an owned controllable instance of a sound effect.
 *
 * @param sound_effect Owned parent sound-effect handle.
 * @param out_instance Receives an owned instance handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The instance retains its native sound data but remains an explicitly ordered C child: destroy
 * it before its sound effect. It also remains a child of the game that owns the effect.
 */
CNA_C_API CNA_Result cna_sound_effect_create_instance(
    CNA_Handle sound_effect,
    CNA_Handle* out_instance);

/**
 * @brief Releases an owned sound effect.
 *
 * @param sound_effect Owned sound-effect handle with no live C instances.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/state/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_destroy(CNA_Handle sound_effect);

/**
 * @brief Starts or resumes a sound-effect instance.
 *
 * @param instance Owned sound-effect-instance handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. Native inability to
 * allocate a playback voice is observable as a stopped state through the info snapshot.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_play(CNA_Handle instance);

/**
 * @brief Pauses a playing sound-effect instance.
 *
 * @param instance Owned sound-effect-instance handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_pause(CNA_Handle instance);

/**
 * @brief Resumes a paused instance, or starts one that has not played.
 *
 * @param instance Owned sound-effect-instance handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_resume(CNA_Handle instance);

/**
 * @brief Stops a sound-effect instance immediately or exits its loop for a release tail.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param immediate `CNA_TRUE` to cut off now or `CNA_FALSE` to stop looping and finish naturally.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_stop(
    CNA_Handle instance,
    CNA_Bool immediate);

/**
 * @brief Captures current controllable properties and playback state.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param out_info Caller-provided versioned structure to receive the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_get_info(
    CNA_Handle instance,
    CNA_SoundEffectInstanceInfo* out_info);

/**
 * @brief Sets per-instance volume using CNA's unclamped pass-through behavior.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param volume Finite volume value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_set_volume(
    CNA_Handle instance,
    float volume);

/**
 * @brief Sets per-instance pitch using CNA's `[-1, 1]` clamping behavior.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param pitch Finite pitch adjustment.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_set_pitch(
    CNA_Handle instance,
    float pitch);

/**
 * @brief Sets per-instance stereo pan.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param pan Finite value in the inclusive range `[-1, 1]`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_set_pan(
    CNA_Handle instance,
    float pan);

/**
 * @brief Enables or disables full-effect looping before playback has begun.
 *
 * @param instance Owned sound-effect-instance handle.
 * @param is_looped `CNA_TRUE` to loop or `CNA_FALSE` to play once.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` after playback has begun, or another
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_set_is_looped(
    CNA_Handle instance,
    CNA_Bool is_looped);

/**
 * @brief Stops, detaches and releases an owned sound-effect instance.
 *
 * @param instance Owned sound-effect-instance handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Destruction is called only from the game creation thread. CNA synchronizes native track
 * detachment with its internal mixer; the C handle is invalid when this function returns and no
 * user C callback or context is retained by the audio mixer.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_destroy(CNA_Handle instance);

/* ---- Sound effect completion ---- */

/** @brief Fixed-width identity of how a sound stops. */
typedef uint32_t CNA_AudioStopOptions;

/** @brief Stop where the author's authored release allows. */
#define CNA_AUDIO_STOP_OPTIONS_AS_AUTHORED UINT32_C(0)
/** @brief Stop at once. */
#define CNA_AUDIO_STOP_OPTIONS_IMMEDIATE UINT32_C(1)
/** @brief Highest defined stop-option identity. */
#define CNA_AUDIO_STOP_OPTIONS_MAXIMUM CNA_AUDIO_STOP_OPTIONS_IMMEDIATE

/**
 * @brief Creates a sound effect from PCM16 data with an explicit range and loop region.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param create_info Versioned sample rate and channel configuration.
 * @param pcm_bytes Caller-owned PCM16LE bytes copied during this call.
 * @param byte_count Bytes available at @p pcm_bytes.
 * @param offset First byte of the range to use.
 * @param count Number of bytes in the range.
 * @param loop_start First sample frame of the loop region.
 * @param loop_length Sample frames in the loop region; zero loops the whole range.
 * @param out_sound_effect Receives an owned sound-effect handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the machine has no audio hardware,
 *         or a documented argument/handle/thread failure.
 *
 * This is the canonical seven-argument constructor; `cna_sound_effect_create_pcm16` is the short one
 * that uses the whole buffer and no loop region.
 */
CNA_C_API CNA_Result cna_sound_effect_create_pcm16_range_ext(
    CNA_Handle game,
    const CNA_SoundEffectCreateInfo* create_info,
    const uint8_t* pcm_bytes,
    uint64_t byte_count,
    int32_t offset,
    int32_t count,
    int32_t loop_start,
    int32_t loop_length,
    CNA_Handle* out_sound_effect);

/**
 * @brief Creates a sound effect by decoding an encoded audio file already in memory.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param bytes Caller-owned encoded bytes copied during this call.
 * @param byte_count Bytes available at @p bytes; must not be zero.
 * @param out_sound_effect Receives an owned sound-effect handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the machine has no audio hardware or
 *         the bytes are not a format this build can decode, or a documented argument/handle/thread
 *         failure.
 *
 * The canonical operation takes a C++ stream and reads it to the end, so C takes the bytes it would
 * have read. Whatever the audio backend can decode is accepted, which is more than the raw PCM the
 * other creation routes take.
 */
CNA_C_API CNA_Result cna_sound_effect_create_from_encoded_ext(
    CNA_Handle game,
    const uint8_t* bytes,
    uint64_t byte_count,
    CNA_Handle* out_sound_effect);

/**
 * @brief Creates a sound effect by decoding an audio file from disk.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param asset_name UTF-8 file path; an empty path creates a silent effect rather than failing.
 * @param out_sound_effect Receives an owned sound-effect handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the machine has no audio hardware or
 *         the file is not a format this build can decode, or a documented argument/handle/thread
 *         failure.
 *
 * **An empty path is not an error**: the canonical constructor returns an effect with no audio
 * rather than throwing, and this route reports that as it is.
 */
CNA_C_API CNA_Result cna_sound_effect_create_from_asset_ext(
    CNA_Handle game,
    CNA_StringView asset_name,
    CNA_Handle* out_sound_effect);

/**
 * @brief Reports whether a sound effect has been disposed.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param out_disposed Receives `CNA_TRUE` after disposal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_is_disposed(
    CNA_Handle sound_effect,
    CNA_Bool* out_disposed);

/**
 * @brief Returns the byte count of a sound effect's name.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_name_size(CNA_Handle sound_effect, uint64_t* out_bytes);

/**
 * @brief Copies a sound effect's name.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_copy_name(
    CNA_Handle sound_effect,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Sets a sound effect's name.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param name UTF-8 name; borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical class has two setters, one taking a copy and one taking ownership of a moved string;
 * both store the same name, so C has one route.
 */
CNA_C_API CNA_Result cna_sound_effect_set_name(CNA_Handle sound_effect, CNA_StringView name);

/**
 * @brief Returns the process-wide master volume applied to every sound effect.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_volume Receives the volume.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The four settings below are canonical **statics**: they belong to the process, not to a sound
 * effect, and the game handle is taken for thread affinity only.
 */
CNA_C_API CNA_Result cna_sound_effect_get_master_volume(CNA_Handle game, float* out_volume);

/**
 * @brief Sets the process-wide master volume applied to every sound effect.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param volume New volume.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_set_master_volume(CNA_Handle game, float volume);

/**
 * @brief Returns the process-wide distance scale used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_scale Receives the scale.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_distance_scale(CNA_Handle game, float* out_scale);

/**
 * @brief Sets the process-wide distance scale used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param scale New scale.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_set_distance_scale(CNA_Handle game, float scale);

/**
 * @brief Returns the process-wide Doppler scale used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_scale Receives the scale.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_doppler_scale(CNA_Handle game, float* out_scale);

/**
 * @brief Sets the process-wide Doppler scale used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param scale New scale.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_set_doppler_scale(CNA_Handle game, float scale);

/**
 * @brief Returns the process-wide speed of sound used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_speed Receives the speed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_speed_of_sound(CNA_Handle game, float* out_speed);

/**
 * @brief Sets the process-wide speed of sound used by 3D audio.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param speed New speed.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_set_speed_of_sound(CNA_Handle game, float speed);

/**
 * @brief Plays a sound effect once, without an instance to control it with.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param out_played Receives `CNA_TRUE` when playback started.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when too many instances are already
 *         playing, or a documented argument/handle/thread/native failure.
 *
 * A disposed effect answers `CNA_FALSE` rather than failing, which is the canonical behavior.
 */
CNA_C_API CNA_Result cna_sound_effect_play(CNA_Handle sound_effect, CNA_Bool* out_played);

/**
 * @brief Plays a sound effect once with explicit settings.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param volume Volume for this playback.
 * @param pitch Pitch for this playback; the canonical route **clamps** rather than refusing.
 * @param pan Pan for this playback, in the closed interval -1 to 1.
 * @param out_played Receives `CNA_TRUE` when playback started.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a pan outside its interval,
 *         `CNA_RESULT_INVALID_STATE` when too many instances are already playing, or a documented
 *         argument/handle/thread/native failure.
 *
 * The canonical asymmetry is preserved: **pan is range-checked and pitch is clamped**.
 */
CNA_C_API CNA_Result cna_sound_effect_play_with_settings(
    CNA_Handle sound_effect,
    float volume,
    float pitch,
    float pan,
    CNA_Bool* out_played);

/**
 * @brief Returns how long a PCM buffer of a given size would play for.
 *
 * @param size_in_bytes Buffer size in bytes.
 * @param sample_rate Sample rate in hertz.
 * @param channels One `CNA_AUDIO_CHANNELS_*` identity.
 * @param out_ticks Receives the duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an undefined channel identity
 *         or a null output.
 *
 * A pure computation: the canonical operation is static, so this takes no game handle either.
 */
CNA_C_API CNA_Result cna_sound_effect_get_sample_duration_ticks(
    int32_t size_in_bytes,
    int32_t sample_rate,
    CNA_AudioChannels channels,
    int64_t* out_ticks);

/**
 * @brief Returns how many bytes a PCM buffer of a given duration needs.
 *
 * @param duration_ticks Duration in 100-nanosecond ticks.
 * @param sample_rate Sample rate in hertz.
 * @param channels One `CNA_AUDIO_CHANNELS_*` identity.
 * @param out_bytes Receives the byte count.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an undefined channel identity
 *         or a null output.
 */
CNA_C_API CNA_Result cna_sound_effect_get_sample_size_in_bytes(
    int64_t duration_ticks,
    int32_t sample_rate,
    CNA_AudioChannels channels,
    int32_t* out_bytes);

/**
 * @brief Returns the byte count of the sound-effect type's .NET type name.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_get_type_name_size(
    CNA_Handle sound_effect,
    uint64_t* out_bytes);

/**
 * @brief Copies the sound-effect type's fully-qualified .NET type name.
 *
 * @param sound_effect Owned sound-effect handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_copy_type_name(
    CNA_Handle sound_effect,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a sound-effect instance has been disposed.
 *
 * @param instance Owned instance handle.
 * @param out_disposed Receives `CNA_TRUE` after disposal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_get_is_disposed(
    CNA_Handle instance,
    CNA_Bool* out_disposed);

/**
 * @brief Returns the byte count of the instance type's .NET type name.
 *
 * @param instance Owned instance handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_get_type_name_size(
    CNA_Handle instance,
    uint64_t* out_bytes);

/**
 * @brief Copies the instance type's fully-qualified .NET type name.
 *
 * @param instance Owned instance handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sound_effect_instance_copy_type_name(
    CNA_Handle instance,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
