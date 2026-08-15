// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MEDIA_H
#define CNA_C_MEDIA_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of the media player's playback state. */
typedef uint32_t CNA_MediaState;

/** @brief Media playback is stopped. */
#define CNA_MEDIA_STATE_STOPPED UINT32_C(0)
/** @brief Media playback is running. */
#define CNA_MEDIA_STATE_PLAYING UINT32_C(1)
/** @brief Media playback is paused. */
#define CNA_MEDIA_STATE_PAUSED UINT32_C(2)
/** @brief Highest defined playback-state identity. */
#define CNA_MEDIA_STATE_MAXIMUM CNA_MEDIA_STATE_PAUSED

/**
 * @brief Fixed-width identity of a media source device.
 *
 * The two canonical values are **0 and 4**, not 0 and 1: the gap is part of the original API and is
 * reproduced rather than renumbered into a dense range, so routes validate against the two defined
 * values instead of an upper bound.
 */
typedef uint32_t CNA_MediaSourceType;

/** @brief The local device storage. */
#define CNA_MEDIA_SOURCE_TYPE_LOCAL_DEVICE UINT32_C(0)
/** @brief A Windows Media Connect streaming device. */
#define CNA_MEDIA_SOURCE_TYPE_WINDOWS_MEDIA_CONNECT UINT32_C(4)

/**
 * @brief Fixed-width identity of the kind of audio content in a video.
 *
 * This is metadata only, in CNA as in the original: no playback path branches on it to change
 * volume or ducking.
 */
typedef uint32_t CNA_VideoSoundtrackType;

/** @brief The video contains music only. */
#define CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC UINT32_C(0)
/** @brief The video contains dialog only. */
#define CNA_VIDEO_SOUNDTRACK_TYPE_DIALOG UINT32_C(1)
/** @brief The video contains both music and dialog. */
#define CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC_AND_DIALOG UINT32_C(2)
/** @brief Highest defined soundtrack-kind identity. */
#define CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC_AND_DIALOG

/** @brief Number of values in each visualization buffer. */
#define CNA_VISUALIZATION_DATA_SIZE UINT32_C(256)

/**
 * @brief Frequency-domain and sample-domain data for media visualization.
 *
 * Both canonical buffers are fixed at @ref CNA_VISUALIZATION_DATA_SIZE values, so this is a plain
 * value rather than a handle with count/copy routes: there is nothing variable to describe. The
 * canonical type exposes the same two arrays both as public fields and through getters, and this
 * one value is both.
 */
typedef struct CNA_VisualizationData {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Frequency-domain values. */
    float frequencies[CNA_VISUALIZATION_DATA_SIZE];

    /** @brief Sample-domain values. */
    float samples[CNA_VISUALIZATION_DATA_SIZE];
} CNA_VisualizationData;

/**
 * @brief Initializes visualization data to the canonical default.
 *
 * @param out_data Receives both buffers zeroed.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_visualization_data_init(CNA_VisualizationData* out_data);

/**
 * @brief Returns the byte count of the visualization type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name is a property of the type, not of any instance, so this route needs neither a value nor
 * a game handle and may run on any thread.
 */
CNA_C_API CNA_Result cna_visualization_data_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the visualization type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_visualization_data_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns how many media sources the device reports.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the source count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical enumeration allocates a fresh source list on every call and hands back raw
 * pointers its caller would have to free. C never exposes that: each route below enumerates,
 * reads the one source it was asked about and releases the whole list before returning, so no
 * ownership crosses the ABI and nothing leaks. The list is therefore a point-in-time snapshot —
 * an index is valid only until the device set changes.
 */
CNA_C_API CNA_Result cna_media_source_get_available_count(CNA_Handle game, uint32_t* out_count);

/**
 * @brief Returns the kind of one enumerated media source.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_type Receives one `CNA_MEDIA_SOURCE_TYPE_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_source_get_type_at(
    CNA_Handle game,
    uint32_t index,
    CNA_MediaSourceType* out_type);

/**
 * @brief Returns the byte count of one enumerated media source's display name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 *
 * The canonical string conversion returns this same display name unchanged, so it needs no route
 * of its own — these bytes are both the name and the text.
 */
CNA_C_API CNA_Result cna_media_source_get_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated media source's display name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_source_copy_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of one enumerated media source's fully-qualified .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 *
 * Unlike the visualization type name this is addressed by index, because the canonical member is an
 * instance method and the canonical source object is not constructible from outside the library.
 */
CNA_C_API CNA_Result cna_media_source_get_type_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated media source's fully-qualified .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_media_source_copy_type_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
