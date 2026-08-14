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

#ifdef __cplusplus
}
#endif

#endif
