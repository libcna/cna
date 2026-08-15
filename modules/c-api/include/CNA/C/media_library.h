// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MEDIA_LIBRARY_H
#define CNA_C_MEDIA_LIBRARY_H

#include "CNA/C/media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Owned handle to one media library.
 *
 * The library owns every album, artist, genre, playlist, song and collection reached through it.
 * A handle to any of those keeps the library alive, so releasing the library handle while one is
 * still held is safe: the library survives until the last handle into it is released.
 */
typedef CNA_Handle CNA_MediaLibraryHandle;

/** @brief Borrowed handle to one album in a media library. */
typedef CNA_Handle CNA_AlbumHandle;

/** @brief Borrowed handle to one ordered, read-only collection of albums. */
typedef CNA_Handle CNA_AlbumCollectionHandle;

/** @brief Borrowed handle to one artist in a media library. */
typedef CNA_Handle CNA_ArtistHandle;

/** @brief Borrowed handle to one ordered, read-only collection of artists. */
typedef CNA_Handle CNA_ArtistCollectionHandle;

/** @brief Borrowed handle to one genre in a media library. */
typedef CNA_Handle CNA_GenreHandle;

/** @brief Borrowed handle to one ordered, read-only collection of genres. */
typedef CNA_Handle CNA_GenreCollectionHandle;

/** @brief Borrowed handle to one playlist in a media library. */
typedef CNA_Handle CNA_PlaylistHandle;

/** @brief Borrowed handle to one ordered, read-only collection of playlists. */
typedef CNA_Handle CNA_PlaylistCollectionHandle;

/**
 * @brief Opens the device's media library using its default media source.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_library Receives an owned library handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Opening scans the device's music and picture locations. An empty library is an ordinary result,
 * not a failure — every collection simply reports a count of zero.
 */
CNA_C_API CNA_Result cna_media_library_create(
    CNA_Handle game,
    CNA_MediaLibraryHandle* out_library);

/**
 * @brief Opens the device's media library using one enumerated media source.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param source_index Index into the enumeration `cna_media_source_get_available_count` reports.
 * @param out_library Receives an owned library handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the source count, or a documented handle/thread/native failure.
 *
 * The canonical constructor **borrows** the source rather than adopting it — it copies that
 * source's kind and name into an object of its own — so C destroys every enumerated source before
 * returning and nothing is left for the caller to release. Read the library's own source back with
 * `cna_media_library_get_media_source_type` and the media-source name routes. A source whose kind
 * is not the local device is refused with `CNA_RESULT_NOT_SUPPORTED`, exactly as the canonical
 * constructor refuses it.
 */
CNA_C_API CNA_Result cna_media_library_create_from_source(
    CNA_Handle game,
    uint32_t source_index,
    CNA_MediaLibraryHandle* out_library);

/**
 * @brief Reports whether a media library has been disposed.
 *
 * @param library Owned library handle.
 * @param out_disposed Receives `CNA_TRUE` when the library has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_is_disposed(
    CNA_MediaLibraryHandle library,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a media library without releasing its handle.
 *
 * @param library Owned library handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal only sets a flag: the catalog stays readable and every handle into it keeps
 * working. Disposing twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_media_library_dispose(CNA_MediaLibraryHandle library);

/**
 * @brief Releases a media library handle.
 *
 * @param library Owned library handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The library object itself is destroyed once no handle into it — including album, artist, genre,
 * playlist, song and collection handles — is left.
 */
CNA_C_API CNA_Result cna_media_library_destroy(CNA_MediaLibraryHandle library);

/**
 * @brief Returns the kind of the media source a library was opened with.
 *
 * @param library Owned library handle.
 * @param out_type Receives one `CNA_MEDIA_SOURCE_TYPE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical source object is not exposed as a handle; its kind and name are read directly,
 * exactly as the enumerated sources are.
 */
CNA_C_API CNA_Result cna_media_library_get_media_source_type(
    CNA_MediaLibraryHandle library,
    CNA_MediaSourceType* out_type);

/**
 * @brief Returns the byte count of a library's media-source name.
 *
 * @param library Owned library handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_media_source_name_size(
    CNA_MediaLibraryHandle library,
    uint64_t* out_bytes);

/**
 * @brief Copies a library's media-source name.
 *
 * @param library Owned library handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_copy_media_source_name(
    CNA_MediaLibraryHandle library,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the collection of every song in a library.
 *
 * @param library Owned library handle.
 * @param out_songs Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The returned handle keeps the library alive. Its elements are library-owned songs, so a song
 * handle taken out of it is a borrowed view rather than something the caller must keep.
 */
CNA_C_API CNA_Result cna_media_library_get_songs(
    CNA_MediaLibraryHandle library,
    CNA_SongCollectionHandle* out_songs);

/**
 * @brief Returns the collection of every album in a library.
 *
 * @param library Owned library handle.
 * @param out_albums Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_albums(
    CNA_MediaLibraryHandle library,
    CNA_AlbumCollectionHandle* out_albums);

/**
 * @brief Returns the collection of every artist in a library.
 *
 * @param library Owned library handle.
 * @param out_artists Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_artists(
    CNA_MediaLibraryHandle library,
    CNA_ArtistCollectionHandle* out_artists);

/**
 * @brief Returns the collection of every genre in a library.
 *
 * @param library Owned library handle.
 * @param out_genres Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_genres(
    CNA_MediaLibraryHandle library,
    CNA_GenreCollectionHandle* out_genres);

/**
 * @brief Returns the collection of every playlist in a library.
 *
 * @param library Owned library handle.
 * @param out_playlists Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_playlists(
    CNA_MediaLibraryHandle library,
    CNA_PlaylistCollectionHandle* out_playlists);

/**
 * @brief Returns the byte count of the library type's fully-qualified .NET type name.
 *
 * @param library Owned library handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_get_type_name_size(
    CNA_MediaLibraryHandle library,
    uint64_t* out_bytes);

/**
 * @brief Copies the library type's fully-qualified .NET type name.
 *
 * @param library Owned library handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_media_library_copy_type_name(
    CNA_MediaLibraryHandle library,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Album ---- */

/**
 * @brief Returns the byte count of an album's display name.
 *
 * @param album Borrowed album handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical string conversion returns this same name, so it needs no route of its own.
 */
CNA_C_API CNA_Result cna_album_get_name_size(CNA_AlbumHandle album, uint64_t* out_bytes);

/**
 * @brief Copies an album's display name.
 *
 * @param album Borrowed album handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_copy_name(
    CNA_AlbumHandle album,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether an album has been disposed.
 *
 * @param album Borrowed album handle.
 * @param out_disposed Receives `CNA_TRUE` when the album has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_get_is_disposed(CNA_AlbumHandle album, CNA_Bool* out_disposed);

/**
 * @brief Disposes an album without releasing its handle.
 *
 * @param album Borrowed album handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical disposal only sets a flag — an album is a view into the library's data and owns nothing
 * of its own — so every other member keeps answering and disposing twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_album_dispose(CNA_AlbumHandle album);

/**
 * @brief Releases an album handle.
 *
 * @param album Borrowed album handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The album itself belongs to its media library and is untouched.
 */
CNA_C_API CNA_Result cna_album_destroy(CNA_AlbumHandle album);

/**
 * @brief Returns an album's hash code.
 *
 * @param album Borrowed album handle.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_get_hash_code(CNA_AlbumHandle album, int32_t* out_hash);

/**
 * @brief Returns the byte count of the album type's fully-qualified .NET type name.
 *
 * @param album Borrowed album handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_get_type_name_size(CNA_AlbumHandle album, uint64_t* out_bytes);

/**
 * @brief Copies the album type's fully-qualified .NET type name.
 *
 * @param album Borrowed album handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_copy_type_name(
    CNA_AlbumHandle album,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the collection of songs on an album.
 *
 * @param album Borrowed album handle.
 * @param out_songs Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The collection belongs to the media library; the returned handle keeps that library alive.
 */
CNA_C_API CNA_Result cna_album_get_songs(
    CNA_AlbumHandle album,
    CNA_SongCollectionHandle* out_songs);

/**
 * @brief Returns how many albums a collection holds.
 *
 * @param collection Borrowed collection handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_get_count(
    CNA_AlbumCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Returns a handle to the album at an index.
 *
 * @param collection Borrowed collection handle.
 * @param index Zero-based index below the current count.
 * @param out_album Receives a borrowed album handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_get_at(
    CNA_AlbumCollectionHandle collection,
    int32_t index,
    CNA_AlbumHandle* out_album);

/**
 * @brief Reports whether a album collection has been disposed.
 *
 * @param collection Borrowed collection handle.
 * @param out_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_get_is_disposed(
    CNA_AlbumCollectionHandle collection,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a album collection without releasing its handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal **empties** the collection, so afterwards its count is zero and every index is
 * out of range. Its elements are untouched, and disposing a collection the library still owns
 * empties it for every other reader too.
 */
CNA_C_API CNA_Result cna_album_collection_dispose(CNA_AlbumCollectionHandle collection);

/**
 * @brief Releases a album collection handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_destroy(CNA_AlbumCollectionHandle collection);

/**
 * @brief Returns the byte count of the album-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_get_type_name_size(
    CNA_AlbumCollectionHandle collection,
    uint64_t* out_bytes);

/**
 * @brief Copies the album-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_collection_copy_type_name(
    CNA_AlbumCollectionHandle collection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Artist ---- */

/**
 * @brief Returns the byte count of an artist's display name.
 *
 * @param artist Borrowed artist handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical string conversion returns this same name, so it needs no route of its own.
 */
CNA_C_API CNA_Result cna_artist_get_name_size(CNA_ArtistHandle artist, uint64_t* out_bytes);

/**
 * @brief Copies an artist's display name.
 *
 * @param artist Borrowed artist handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_copy_name(
    CNA_ArtistHandle artist,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether an artist has been disposed.
 *
 * @param artist Borrowed artist handle.
 * @param out_disposed Receives `CNA_TRUE` when the artist has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_get_is_disposed(CNA_ArtistHandle artist, CNA_Bool* out_disposed);

/**
 * @brief Disposes an artist without releasing its handle.
 *
 * @param artist Borrowed artist handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical disposal only sets a flag — an artist is a view into the library's data and owns nothing
 * of its own — so every other member keeps answering and disposing twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_artist_dispose(CNA_ArtistHandle artist);

/**
 * @brief Releases an artist handle.
 *
 * @param artist Borrowed artist handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The artist itself belongs to its media library and is untouched.
 */
CNA_C_API CNA_Result cna_artist_destroy(CNA_ArtistHandle artist);

/**
 * @brief Returns an artist's hash code.
 *
 * @param artist Borrowed artist handle.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_get_hash_code(CNA_ArtistHandle artist, int32_t* out_hash);

/**
 * @brief Returns the byte count of the artist type's fully-qualified .NET type name.
 *
 * @param artist Borrowed artist handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_get_type_name_size(CNA_ArtistHandle artist, uint64_t* out_bytes);

/**
 * @brief Copies the artist type's fully-qualified .NET type name.
 *
 * @param artist Borrowed artist handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_copy_type_name(
    CNA_ArtistHandle artist,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the collection of songs by an artist.
 *
 * @param artist Borrowed artist handle.
 * @param out_songs Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The collection belongs to the media library; the returned handle keeps that library alive.
 */
CNA_C_API CNA_Result cna_artist_get_songs(
    CNA_ArtistHandle artist,
    CNA_SongCollectionHandle* out_songs);

/**
 * @brief Returns how many artists a collection holds.
 *
 * @param collection Borrowed collection handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_get_count(
    CNA_ArtistCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Returns a handle to the artist at an index.
 *
 * @param collection Borrowed collection handle.
 * @param index Zero-based index below the current count.
 * @param out_artist Receives a borrowed artist handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_get_at(
    CNA_ArtistCollectionHandle collection,
    int32_t index,
    CNA_ArtistHandle* out_artist);

/**
 * @brief Reports whether a artist collection has been disposed.
 *
 * @param collection Borrowed collection handle.
 * @param out_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_get_is_disposed(
    CNA_ArtistCollectionHandle collection,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a artist collection without releasing its handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal **empties** the collection, so afterwards its count is zero and every index is
 * out of range. Its elements are untouched, and disposing a collection the library still owns
 * empties it for every other reader too.
 */
CNA_C_API CNA_Result cna_artist_collection_dispose(CNA_ArtistCollectionHandle collection);

/**
 * @brief Releases a artist collection handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_destroy(CNA_ArtistCollectionHandle collection);

/**
 * @brief Returns the byte count of the artist-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_get_type_name_size(
    CNA_ArtistCollectionHandle collection,
    uint64_t* out_bytes);

/**
 * @brief Copies the artist-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_collection_copy_type_name(
    CNA_ArtistCollectionHandle collection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Genre ---- */

/**
 * @brief Returns the byte count of a genre's display name.
 *
 * @param genre Borrowed genre handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical string conversion returns this same name, so it needs no route of its own.
 */
CNA_C_API CNA_Result cna_genre_get_name_size(CNA_GenreHandle genre, uint64_t* out_bytes);

/**
 * @brief Copies a genre's display name.
 *
 * @param genre Borrowed genre handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_copy_name(
    CNA_GenreHandle genre,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a genre has been disposed.
 *
 * @param genre Borrowed genre handle.
 * @param out_disposed Receives `CNA_TRUE` when the genre has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_get_is_disposed(CNA_GenreHandle genre, CNA_Bool* out_disposed);

/**
 * @brief Disposes a genre without releasing its handle.
 *
 * @param genre Borrowed genre handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical disposal only sets a flag — a genre is a view into the library's data and owns nothing
 * of its own — so every other member keeps answering and disposing twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_genre_dispose(CNA_GenreHandle genre);

/**
 * @brief Releases a genre handle.
 *
 * @param genre Borrowed genre handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The genre itself belongs to its media library and is untouched.
 */
CNA_C_API CNA_Result cna_genre_destroy(CNA_GenreHandle genre);

/**
 * @brief Returns a genre's hash code.
 *
 * @param genre Borrowed genre handle.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_get_hash_code(CNA_GenreHandle genre, int32_t* out_hash);

/**
 * @brief Returns the byte count of the genre type's fully-qualified .NET type name.
 *
 * @param genre Borrowed genre handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_get_type_name_size(CNA_GenreHandle genre, uint64_t* out_bytes);

/**
 * @brief Copies the genre type's fully-qualified .NET type name.
 *
 * @param genre Borrowed genre handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_copy_type_name(
    CNA_GenreHandle genre,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the collection of songs in a genre.
 *
 * @param genre Borrowed genre handle.
 * @param out_songs Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The collection belongs to the media library; the returned handle keeps that library alive.
 */
CNA_C_API CNA_Result cna_genre_get_songs(
    CNA_GenreHandle genre,
    CNA_SongCollectionHandle* out_songs);

/**
 * @brief Returns how many genres a collection holds.
 *
 * @param collection Borrowed collection handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_get_count(
    CNA_GenreCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Returns a handle to the genre at an index.
 *
 * @param collection Borrowed collection handle.
 * @param index Zero-based index below the current count.
 * @param out_genre Receives a borrowed genre handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_get_at(
    CNA_GenreCollectionHandle collection,
    int32_t index,
    CNA_GenreHandle* out_genre);

/**
 * @brief Reports whether a genre collection has been disposed.
 *
 * @param collection Borrowed collection handle.
 * @param out_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_get_is_disposed(
    CNA_GenreCollectionHandle collection,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a genre collection without releasing its handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal **empties** the collection, so afterwards its count is zero and every index is
 * out of range. Its elements are untouched, and disposing a collection the library still owns
 * empties it for every other reader too.
 */
CNA_C_API CNA_Result cna_genre_collection_dispose(CNA_GenreCollectionHandle collection);

/**
 * @brief Releases a genre collection handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_destroy(CNA_GenreCollectionHandle collection);

/**
 * @brief Returns the byte count of the genre-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_get_type_name_size(
    CNA_GenreCollectionHandle collection,
    uint64_t* out_bytes);

/**
 * @brief Copies the genre-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_collection_copy_type_name(
    CNA_GenreCollectionHandle collection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Playlist ---- */

/**
 * @brief Returns the byte count of a playlist's display name.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical string conversion returns this same name, so it needs no route of its own.
 */
CNA_C_API CNA_Result cna_playlist_get_name_size(CNA_PlaylistHandle playlist, uint64_t* out_bytes);

/**
 * @brief Copies a playlist's display name.
 *
 * @param playlist Borrowed playlist handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_copy_name(
    CNA_PlaylistHandle playlist,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a playlist has been disposed.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_disposed Receives `CNA_TRUE` when the playlist has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_get_is_disposed(CNA_PlaylistHandle playlist, CNA_Bool* out_disposed);

/**
 * @brief Disposes a playlist without releasing its handle.
 *
 * @param playlist Borrowed playlist handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical disposal only sets a flag — a playlist is a view into the library's data and owns nothing
 * of its own — so every other member keeps answering and disposing twice is a successful no-op.
 */
CNA_C_API CNA_Result cna_playlist_dispose(CNA_PlaylistHandle playlist);

/**
 * @brief Releases a playlist handle.
 *
 * @param playlist Borrowed playlist handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The playlist itself belongs to its media library and is untouched.
 */
CNA_C_API CNA_Result cna_playlist_destroy(CNA_PlaylistHandle playlist);

/**
 * @brief Returns a playlist's hash code.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_get_hash_code(CNA_PlaylistHandle playlist, int32_t* out_hash);

/**
 * @brief Returns the byte count of the playlist type's fully-qualified .NET type name.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_get_type_name_size(CNA_PlaylistHandle playlist, uint64_t* out_bytes);

/**
 * @brief Copies the playlist type's fully-qualified .NET type name.
 *
 * @param playlist Borrowed playlist handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_copy_type_name(
    CNA_PlaylistHandle playlist,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the collection of songs in a playlist.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_songs Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The collection belongs to the media library; the returned handle keeps that library alive.
 */
CNA_C_API CNA_Result cna_playlist_get_songs(
    CNA_PlaylistHandle playlist,
    CNA_SongCollectionHandle* out_songs);

/**
 * @brief Returns how many playlists a collection holds.
 *
 * @param collection Borrowed collection handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_get_count(
    CNA_PlaylistCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Returns a handle to the playlist at an index.
 *
 * @param collection Borrowed collection handle.
 * @param index Zero-based index below the current count.
 * @param out_playlist Receives a borrowed playlist handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_get_at(
    CNA_PlaylistCollectionHandle collection,
    int32_t index,
    CNA_PlaylistHandle* out_playlist);

/**
 * @brief Reports whether a playlist collection has been disposed.
 *
 * @param collection Borrowed collection handle.
 * @param out_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_get_is_disposed(
    CNA_PlaylistCollectionHandle collection,
    CNA_Bool* out_disposed);

/**
 * @brief Disposes a playlist collection without releasing its handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Canonical disposal **empties** the collection, so afterwards its count is zero and every index is
 * out of range. Its elements are untouched, and disposing a collection the library still owns
 * empties it for every other reader too.
 */
CNA_C_API CNA_Result cna_playlist_collection_dispose(CNA_PlaylistCollectionHandle collection);

/**
 * @brief Releases a playlist collection handle.
 *
 * @param collection Borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_destroy(CNA_PlaylistCollectionHandle collection);

/**
 * @brief Returns the byte count of the playlist-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_get_type_name_size(
    CNA_PlaylistCollectionHandle collection,
    uint64_t* out_bytes);

/**
 * @brief Copies the playlist-collection type's fully-qualified .NET type name.
 *
 * @param collection Borrowed collection handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_collection_copy_type_name(
    CNA_PlaylistCollectionHandle collection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Comparisons ---- */

/**
 * @brief Compares two albums.
 *
 * @param left First album handle.
 * @param right Second album handle.
 * @param out_equal Receives `CNA_TRUE` when both the name and the artist match.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Album equality is **not** name alone: album names collide across artists, so the canonical
 * comparison pairs the name with the artist, and two albums with no artist match on the name. The
 * canonical `==` operator is this comparison and `!=` is its negation, so neither needs a route.
 */
CNA_C_API CNA_Result cna_album_equals(
    CNA_AlbumHandle left,
    CNA_AlbumHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Compares two artists.
 *
 * @param left First artist handle.
 * @param right Second artist handle.
 * @param out_equal Receives `CNA_TRUE` when the names match.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical `==` operator is this comparison and `!=` is its negation.
 */
CNA_C_API CNA_Result cna_artist_equals(
    CNA_ArtistHandle left,
    CNA_ArtistHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Compares two genres.
 *
 * @param left First genre handle.
 * @param right Second genre handle.
 * @param out_equal Receives `CNA_TRUE` when the names match.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical `==` operator is this comparison and `!=` is its negation.
 */
CNA_C_API CNA_Result cna_genre_equals(
    CNA_GenreHandle left,
    CNA_GenreHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Compares two playlists.
 *
 * @param left First playlist handle.
 * @param right Second playlist handle.
 * @param out_equal Receives `CNA_TRUE` when the names match.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical `==` operator is this comparison and `!=` is its negation.
 */
CNA_C_API CNA_Result cna_playlist_equals(
    CNA_PlaylistHandle left,
    CNA_PlaylistHandle right,
    CNA_Bool* out_equal);

/* ---- Album-specific members ---- */

/**
 * @brief Returns the artist an album belongs to.
 *
 * @param album Borrowed album handle.
 * @param out_artist Receives a borrowed artist handle when one is available; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the album has an artist.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * An album with no artist is an ordinary answer rather than a failure, because the canonical
 * property returns null for one.
 */
CNA_C_API CNA_Result cna_album_get_artist(
    CNA_AlbumHandle album,
    CNA_ArtistHandle* out_artist,
    CNA_Bool* out_available);

/**
 * @brief Returns the genre an album belongs to.
 *
 * @param album Borrowed album handle.
 * @param out_genre Receives a borrowed genre handle when one is available; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the album has a genre.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_get_genre(
    CNA_AlbumHandle album,
    CNA_GenreHandle* out_genre,
    CNA_Bool* out_available);

/**
 * @brief Returns an album's total duration.
 *
 * @param album Borrowed album handle.
 * @param out_ticks Receives the duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_album_get_duration(CNA_AlbumHandle album, int64_t* out_ticks);

/**
 * @brief Reports whether an album has cover art.
 *
 * @param album Borrowed album handle.
 * @param out_has_art Receives `CNA_TRUE` when cover art is available.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Check this before asking for the art or the thumbnail: both refuse an album without art.
 */
CNA_C_API CNA_Result cna_album_get_has_art(CNA_AlbumHandle album, CNA_Bool* out_has_art);

/**
 * @brief Returns the byte count of an album's cover-art image.
 *
 * @param album Borrowed album handle.
 * @param out_bytes Receives the image's byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the album has no art, or a
 *         documented argument/handle/thread/native failure.
 *
 * The canonical member returns a stream whose caller owns it. C reads that stream to its end and
 * releases it inside the call, so the image crosses as bytes and no stream object enters the ABI.
 * The image is decoded from disk on every call, so query the size and copy in one sequence.
 */
CNA_C_API CNA_Result cna_album_get_art_size(CNA_AlbumHandle album, uint64_t* out_bytes);

/**
 * @brief Copies an album's cover-art image.
 *
 * @param album Borrowed album handle.
 * @param destination Buffer receiving the image bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_STATE` when the album has no art, `CNA_RESULT_INVALID_ARGUMENT`, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_album_copy_art(
    CNA_AlbumHandle album,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of an album's thumbnail image.
 *
 * @param album Borrowed album handle.
 * @param out_bytes Receives the image's byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the album has no art, or a
 *         documented argument/handle/thread/native failure.
 *
 * CNA generates no separate thumbnail, so this is the same image the cover-art routes return. That
 * is the canonical behavior, not a C limitation.
 */
CNA_C_API CNA_Result cna_album_get_thumbnail_size(CNA_AlbumHandle album, uint64_t* out_bytes);

/**
 * @brief Copies an album's thumbnail image.
 *
 * @param album Borrowed album handle.
 * @param destination Buffer receiving the image bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_STATE` when the album has no art, `CNA_RESULT_INVALID_ARGUMENT`, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_album_copy_thumbnail(
    CNA_AlbumHandle album,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Artist, genre and playlist specific members ---- */

/**
 * @brief Returns the collection of albums by an artist.
 *
 * @param artist Borrowed artist handle.
 * @param out_albums Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_artist_get_albums(
    CNA_ArtistHandle artist,
    CNA_AlbumCollectionHandle* out_albums);

/**
 * @brief Returns the collection of albums in a genre.
 *
 * @param genre Borrowed genre handle.
 * @param out_albums Receives a borrowed collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_genre_get_albums(
    CNA_GenreHandle genre,
    CNA_AlbumCollectionHandle* out_albums);

/**
 * @brief Returns a playlist's total duration.
 *
 * @param playlist Borrowed playlist handle.
 * @param out_ticks Receives the duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_playlist_get_duration(CNA_PlaylistHandle playlist, int64_t* out_ticks);

/* ---- Song members that name a library entity ---- */

/**
 * @brief Returns the album a song appears on.
 *
 * @param song Song handle.
 * @param out_album Receives a borrowed album handle when one is available; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the song belongs to a library album.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Only a song obtained from a media library has one. A song a caller created from a file path has
 * no library context, so this reports `CNA_FALSE` — an ordinary answer, not a failure.
 */
CNA_C_API CNA_Result cna_song_get_album(
    CNA_SongHandle song,
    CNA_AlbumHandle* out_album,
    CNA_Bool* out_available);

/**
 * @brief Returns the artist of a song.
 *
 * @param song Song handle.
 * @param out_artist Receives a borrowed artist handle when one is available; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the song belongs to a library artist.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_artist(
    CNA_SongHandle song,
    CNA_ArtistHandle* out_artist,
    CNA_Bool* out_available);

/**
 * @brief Returns the genre of a song.
 *
 * @param song Song handle.
 * @param out_genre Receives a borrowed genre handle when one is available; untouched otherwise.
 * @param out_available Receives `CNA_TRUE` when the song belongs to a library genre.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_song_get_genre(
    CNA_SongHandle song,
    CNA_GenreHandle* out_genre,
    CNA_Bool* out_available);

#ifdef __cplusplus
}
#endif

#endif
