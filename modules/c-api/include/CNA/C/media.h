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

/**
 * @brief Owned handle to one song.
 *
 * A song created through one of the routes below belongs to its C caller. A song reached through a
 * collection shares the same underlying object with the handle it came from: releasing one handle
 * does not destroy a song another handle or collection still holds.
 */
typedef CNA_Handle CNA_SongHandle;

/** @brief Owned handle to one ordered, read-only collection of songs. */
typedef CNA_Handle CNA_SongCollectionHandle;

/**
 * @brief Creates a song from a local file path.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param file_name UTF-8 path to an existing audio file.
 * @param name UTF-8 display name; may be empty.
 * @param out_song Receives an owned song handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the file does not exist,
 *         `CNA_RESULT_ENCODING` for text that is not valid UTF-8, or a documented
 *         argument/handle/thread failure.
 *
 * The canonical constructor checks only that the file exists; it does not open or decode it.
 * **An empty @p name yields an empty display name** — the canonical parameter defaults to an empty
 * string and the constructor stores it verbatim, despite that constructor's own documentation
 * claiming the name defaults to the file name. This ABI follows the behavior, not the comment.
 */
CNA_C_API CNA_Result cna_song_create(
    CNA_Handle game,
    CNA_StringView file_name,
    CNA_StringView name,
    CNA_SongHandle* out_song);

/**
 * @brief Creates a song from a local file path with an explicit duration.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param file_name UTF-8 path to an existing audio file.
 * @param asset_name UTF-8 display name; may be empty.
 * @param duration_milliseconds Song duration in milliseconds.
 * @param out_song Receives an owned song handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the file does not exist,
 *         `CNA_RESULT_ENCODING`, or a documented argument/handle/thread failure.
 *
 * The duration is stored as given and is not validated against the file's real length, exactly as
 * the canonical constructor does.
 */
CNA_C_API CNA_Result cna_song_create_with_duration(
    CNA_Handle game,
    CNA_StringView file_name,
    CNA_StringView asset_name,
    int32_t duration_milliseconds,
    CNA_SongHandle* out_song);

/**
 * @brief Creates a song from a local file URI or path.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param name UTF-8 display name; may be empty.
 * @param uri UTF-8 `file:` URI or plain path.
 * @param out_song Receives an owned song handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a URI whose scheme is not `file`,
 *         `CNA_RESULT_IO` when the resolved path does not exist, `CNA_RESULT_ENCODING`, or a
 *         documented argument/handle/thread failure.
 *
 * A string with no scheme, or with a single-character one, is treated as a plain path — a Windows
 * drive letter is not a scheme. Percent escapes are decoded, and a query or fragment is stripped
 * before decoding, so a percent-encoded `?` or `#` stays part of the file name.
 */
CNA_C_API CNA_Result cna_song_create_from_uri(
    CNA_Handle game,
    CNA_StringView name,
    CNA_StringView uri,
    CNA_SongHandle* out_song);

/**
 * @brief Returns the byte count of a song's display name.
 *
 * @param song Song handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical string conversion returns this same display name, so it needs no route of its own.
 */
CNA_C_API CNA_Result cna_song_get_name_size(CNA_SongHandle song, uint64_t* out_bytes);

/**
 * @brief Copies a song's display name.
 *
 * @param song Song handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_copy_name(
    CNA_SongHandle song,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the file path or handle a song plays from.
 *
 * @param song Song handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This is the string song equality and hashing are computed from, not the display name.
 */
CNA_C_API CNA_Result cna_song_get_handle_text_size_ext(CNA_SongHandle song, uint64_t* out_bytes);

/**
 * @brief Copies the file path or handle a song plays from.
 *
 * @param song Song handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_copy_handle_text_ext(
    CNA_SongHandle song,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns a song's playback duration.
 *
 * @param song Song handle.
 * @param out_ticks Receives the duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_duration(CNA_SongHandle song, int64_t* out_ticks);

/**
 * @brief Sets a song's playback duration.
 *
 * @param song Song handle.
 * @param ticks Duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The value is stored verbatim; the canonical setter validates nothing, so a negative duration is
 * accepted exactly as it is in C++.
 */
CNA_C_API CNA_Result cna_song_set_duration(CNA_SongHandle song, int64_t ticks);

/**
 * @brief Reports whether a song is DRM-protected content.
 *
 * @param song Song handle.
 * @param out_protected Receives `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Always false, and that is the correct answer rather than a stub: the library scan only accepts
 * plain, unencrypted containers, so no song CNA can index is protected in the first place.
 */
CNA_C_API CNA_Result cna_song_get_is_protected(CNA_SongHandle song, CNA_Bool* out_protected);

/**
 * @brief Reports whether the user has rated a song.
 *
 * @param song Song handle.
 * @param out_rated Receives `CNA_TRUE` when the file carried a real rating tag.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **This is not "rating is nonzero".** Both tag formats reserve zero for "unrated", so a file with
 * an explicit zero rating still reports not-rated.
 */
CNA_C_API CNA_Result cna_song_get_is_rated(CNA_SongHandle song, CNA_Bool* out_rated);

/**
 * @brief Returns how many times a song has been played.
 *
 * @param song Song handle.
 * @param out_play_count Receives the play count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_play_count(CNA_SongHandle song, int32_t* out_play_count);

/**
 * @brief Sets how many times a song has been played.
 *
 * @param song Song handle.
 * @param play_count New play count, stored verbatim.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_set_play_count(CNA_SongHandle song, int32_t play_count);

/**
 * @brief Returns the user's rating for a song, on the canonical 0 through 10 scale.
 *
 * @param song Song handle.
 * @param out_rating Receives the rating, or zero when the song is unrated.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_rating(CNA_SongHandle song, int32_t* out_rating);

/**
 * @brief Returns a song's track number on its album.
 *
 * @param song Song handle.
 * @param out_track_number Receives the track number, or zero when unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A song built from a file path has no scanned tag data and reports zero, which is also the tag
 * formats' own "unknown" convention.
 */
CNA_C_API CNA_Result cna_song_get_track_number(CNA_SongHandle song, int32_t* out_track_number);

/**
 * @brief Reports whether a song has been disposed.
 *
 * @param song Song handle.
 * @param out_disposed Receives `CNA_TRUE` when the song has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_is_disposed(CNA_SongHandle song, CNA_Bool* out_disposed);

/**
 * @brief Disposes a song without releasing its handle.
 *
 * @param song Song handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical disposal only marks the song disposed; every other member keeps answering
 * afterwards, and disposing twice is a successful no-op. Release the handle with
 * `cna_song_destroy`.
 */
CNA_C_API CNA_Result cna_song_dispose(CNA_SongHandle song);

/**
 * @brief Releases a song handle.
 *
 * @param song Song handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The underlying song is destroyed once no handle and no collection still refers to it.
 */
CNA_C_API CNA_Result cna_song_destroy(CNA_SongHandle song);

/**
 * @brief Compares two songs.
 *
 * @param left First song handle.
 * @param right Second song handle.
 * @param out_equal Receives `CNA_TRUE` when both songs play from the same file path.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Equality is the canonical file-path comparison, not handle identity: two independently created
 * songs over the same file compare equal. The canonical `==` operator is this same comparison and
 * `!=` is its negation, so neither needs a route of its own.
 */
CNA_C_API CNA_Result cna_song_equals(
    CNA_SongHandle left,
    CNA_SongHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a song's hash code.
 *
 * @param song Song handle.
 * @param out_hash Receives the hash of the song's file path.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The hash is content-based, so two songs that compare equal always hash equal — a deliberate CNA
 * improvement over FNA's identity-based hash, which does not hold that property.
 */
CNA_C_API CNA_Result cna_song_get_hash_code(CNA_SongHandle song, int32_t* out_hash);

/**
 * @brief Returns the byte count of the song type's fully-qualified .NET type name.
 *
 * @param song Song handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_type_name_size(CNA_SongHandle song, uint64_t* out_bytes);

/**
 * @brief Copies the song type's fully-qualified .NET type name.
 *
 * @param song Song handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_copy_type_name(
    CNA_SongHandle song,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Creates a song collection from an array of song handles.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param songs Array of song handles; may be null only when @p count is zero.
 * @param count Number of handles in @p songs.
 * @param out_collection Receives an owned collection handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or a null array
 *         with a nonzero count, `CNA_RESULT_INVALID_HANDLE` when any element is not a live song,
 *         or a documented handle/thread failure.
 *
 * The collection **keeps every song it was given alive**: the canonical collection stores
 * non-owning pointers, so C retains the songs instead of letting a released handle leave a
 * dangling element behind. A caller may therefore release its own song handles immediately after
 * building a collection.
 */
CNA_C_API CNA_Result cna_song_collection_create(
    CNA_Handle game,
    const CNA_SongHandle* songs,
    uint64_t count,
    CNA_SongCollectionHandle* out_collection);

/**
 * @brief Returns a new handle to the song at an index.
 *
 * @param collection Collection handle.
 * @param index Zero-based index below the current count.
 * @param out_song Receives a handle sharing the collection's song.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 *
 * The returned handle refers to the same song the collection holds, not a copy. Releasing it does
 * not remove the song from the collection.
 */
CNA_C_API CNA_Result cna_song_collection_get_at(
    CNA_SongCollectionHandle collection,
    int32_t index,
    CNA_SongHandle* out_song);

/**
 * @brief Returns how many songs a collection holds.
 *
 * @param collection Collection handle.
 * @param out_count Receives the song count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_collection_get_count(
    CNA_SongCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Reports whether a collection has been disposed.
 *
 * @param collection Collection handle.
 * @param out_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_collection_get_is_disposed(
    CNA_SongCollectionHandle collection,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a collection without releasing its handle.
 *
 * @param collection Collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal **empties the collection**: afterwards its count is zero and every index is
 * out of range. The songs themselves are untouched — the collection never owned them. Disposing
 * twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_song_collection_dispose(CNA_SongCollectionHandle collection);

/**
 * @brief Releases a collection handle.
 *
 * @param collection Collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_collection_destroy(CNA_SongCollectionHandle collection);

/**
 * @brief Returns the byte count of the collection type's fully-qualified .NET type name.
 *
 * @param collection Collection handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_collection_get_type_name_size(
    CNA_SongCollectionHandle collection,
    uint64_t* out_bytes);

/**
 * @brief Copies the collection type's fully-qualified .NET type name.
 *
 * @param collection Collection handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_collection_copy_type_name(
    CNA_SongCollectionHandle collection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
