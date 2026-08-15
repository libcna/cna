// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>
#include <threads.h>

#ifndef CNA_C_API_MEDIA_FIXTURE_MUSIC
#error "CNA_C_API_MEDIA_FIXTURE_MUSIC must name the fixture music directory"
#endif

typedef struct LibrarySmokeState {
    int validated;
} LibrarySmokeState;

typedef struct WrongThreadState {
    CNA_MediaLibraryHandle library;
    CNA_Result result;
} WrongThreadState;

static const char ArtistName[] = "CNA Test Artist";
static const char AlbumName[] = "CNA Test Album";
static const char GenreName[] = "Test Genre";
static const char FirstTitle[] = "First Track";
static const char SecondTitle[] = "Second Track";
/* Twelve bytes with a recognizable pattern: the album-art routes must hand back exactly these. */
static const uint8_t CoverBytes[12] = {
    0xFFU, 0xD8U, 0xFFU, 0xE0U, 0x00U, 0x10U, 0x4AU, 0x46U, 0x49U, 0x46U, 0x00U, 0x01U
};

static CNA_StringView view(const char* const value)
{
    CNA_StringView result;
    result.data = value;
    result.byte_length = (uint64_t)strlen(value);
    return result;
}

static size_t push_frame(
    uint8_t* const buffer,
    size_t offset,
    const char* const id,
    const char* const text)
{
    /* ID3v2.3 text frame: four-character id, big-endian size, two flag bytes, then a one-byte
       encoding selector followed by the text itself. */
    const size_t length = strlen(text);
    const uint32_t size = (uint32_t)(length + 1U);
    memcpy(buffer + offset, id, 4U);
    buffer[offset + 4U] = (uint8_t)((size >> 24) & 0xFFU);
    buffer[offset + 5U] = (uint8_t)((size >> 16) & 0xFFU);
    buffer[offset + 6U] = (uint8_t)((size >> 8) & 0xFFU);
    buffer[offset + 7U] = (uint8_t)(size & 0xFFU);
    buffer[offset + 8U] = 0x00U;
    buffer[offset + 9U] = 0x00U;
    buffer[offset + 10U] = 0x00U; /* ISO-8859-1 */
    memcpy(buffer + offset + 11U, text, length);
    return offset + 11U + length;
}

/* A tag-only MP3: the library indexes a file by its tags, so this is enough to produce a real
   song, album, artist and genre without shipping an audio recording in the source tree. */
static int write_tagged_song(
    const char* const path,
    const char* const title,
    const char* const track)
{
    uint8_t buffer[512];
    size_t offset = 10U;
    uint32_t tag_size = 0U;
    FILE* file = 0;
    size_t written = 0U;

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "ID3", 3U);
    buffer[3] = 0x03U; /* ID3v2.3 */
    buffer[4] = 0x00U;
    buffer[5] = 0x00U;

    offset = push_frame(buffer, offset, "TIT2", title);
    offset = push_frame(buffer, offset, "TPE1", ArtistName);
    offset = push_frame(buffer, offset, "TALB", AlbumName);
    offset = push_frame(buffer, offset, "TCON", GenreName);
    offset = push_frame(buffer, offset, "TRCK", track);

    tag_size = (uint32_t)(offset - 10U);
    /* Synchsafe: seven significant bits per byte. */
    buffer[6] = (uint8_t)((tag_size >> 21) & 0x7FU);
    buffer[7] = (uint8_t)((tag_size >> 14) & 0x7FU);
    buffer[8] = (uint8_t)((tag_size >> 7) & 0x7FU);
    buffer[9] = (uint8_t)(tag_size & 0x7FU);

    file = fopen(path, "wb");
    if (file == 0) {
        return 0;
    }
    written = fwrite(buffer, 1U, offset, file);
    return fclose(file) == 0 && written == offset;
}

static int write_cover(const char* const path)
{
    FILE* const file = fopen(path, "wb");
    size_t written = 0U;
    if (file == 0) {
        return 0;
    }
    written = fwrite(CoverBytes, 1U, sizeof(CoverBytes), file);
    return fclose(file) == 0 && written == sizeof(CoverBytes);
}

static int join_fixture_path(char* const buffer, const size_t capacity, const char* const name)
{
    const size_t root = strlen(CNA_C_API_MEDIA_FIXTURE_MUSIC);
    if (root + strlen(name) + 2U > capacity) {
        return 0;
    }
    strcpy(buffer, CNA_C_API_MEDIA_FIXTURE_MUSIC);
    strcat(buffer, "/");
    strcat(buffer, name);
    return 1;
}

static int create_fixture(void)
{
    char first[1024];
    char second[1024];
    char cover[1024];
    if (!join_fixture_path(first, sizeof(first), "first_track.mp3") ||
        !join_fixture_path(second, sizeof(second), "second_track.mp3") ||
        !join_fixture_path(cover, sizeof(cover), "cover.jpg")) {
        return 0;
    }
    return write_tagged_song(first, FirstTitle, "3") &&
        write_tagged_song(second, SecondTitle, "1") && write_cover(cover);
}

static int text_equals(
    const CNA_Result size_result,
    const uint64_t bytes,
    const char* const actual,
    const char* const expected)
{
    return size_result == CNA_RESULT_SUCCESS && bytes == (uint64_t)strlen(expected) &&
        strcmp(actual, expected) == 0;
}

static int validate_library_source(const CNA_MediaLibraryHandle library)
{
    CNA_MediaSourceType type = UINT32_C(99);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(text, 0, sizeof(text));
    if (cna_media_library_get_media_source_type(library, &type) != CNA_RESULT_SUCCESS ||
        type != CNA_MEDIA_SOURCE_TYPE_LOCAL_DEVICE ||
        cna_media_library_get_media_source_name_size(library, &bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(text) ||
        cna_media_library_copy_media_source_name(library, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_media_library_get_type_name_size(library, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_media_library_copy_type_name(library, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.MediaLibrary") != 0) {
        return 0;
    }
    return cna_media_library_get_media_source_type(library, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_library_get_media_source_name_size(library, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_media_library_get_type_name_size(library, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_album(const CNA_AlbumHandle album)
{
    CNA_ArtistHandle artist = CNA_INVALID_HANDLE;
    CNA_GenreHandle genre = CNA_INVALID_HANDLE;
    CNA_SongCollectionHandle songs = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int64_t ticks = INT64_C(-1);
    int32_t count = 9;
    uint64_t bytes = UINT64_C(9);
    uint8_t image[64];
    char text[256];

    memset(text, 0, sizeof(text));
    if (cna_album_get_name_size(album, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen(AlbumName) ||
        cna_album_copy_name(album, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        !text_equals(CNA_RESULT_SUCCESS, bytes, text, AlbumName)) {
        return 0;
    }
    /* The duration is whatever the scan measured; a tag-only file has no audio to measure. */
    if (cna_album_get_duration(album, &ticks) != CNA_RESULT_SUCCESS || ticks < INT64_C(0)) {
        return 0;
    }

    /* Both back-pointers are present for a scanned album, and name the scanned tag values. */
    memset(text, 0, sizeof(text));
    if (cna_album_get_artist(album, &artist, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_artist_get_name_size(artist, &bytes) != CNA_RESULT_SUCCESS ||
        cna_artist_copy_name(artist, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, ArtistName) != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_album_get_genre(album, &genre, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_genre_get_name_size(genre, &bytes) != CNA_RESULT_SUCCESS ||
        cna_genre_copy_name(genre, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, GenreName) != 0) {
        return 0;
    }

    /* The album holds both scanned songs. */
    if (cna_album_get_songs(album, &songs) != CNA_RESULT_SUCCESS ||
        cna_song_collection_get_count(songs, &count) != CNA_RESULT_SUCCESS || count != 2) {
        return 0;
    }

    /* The folder cover is real art, and the routes return exactly the fixture's bytes. */
    memset(image, 0, sizeof(image));
    if (cna_album_get_has_art(album, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_album_get_art_size(album, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)sizeof(CoverBytes) ||
        cna_album_copy_art(album, image, (uint64_t)sizeof(image), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)sizeof(CoverBytes) ||
        memcmp(image, CoverBytes, sizeof(CoverBytes)) != 0) {
        return 0;
    }
    /* No separate thumbnail is generated, so it is the same image -- canonical behavior. */
    memset(image, 0, sizeof(image));
    if (cna_album_get_thumbnail_size(album, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)sizeof(CoverBytes) ||
        cna_album_copy_thumbnail(album, image, (uint64_t)sizeof(image), &bytes) !=
            CNA_RESULT_SUCCESS ||
        memcmp(image, CoverBytes, sizeof(CoverBytes)) != 0) {
        return 0;
    }
    {
        uint8_t guard[4];
        memset(guard, 0x7FU, sizeof(guard));
        if (cna_album_copy_art(album, guard, UINT64_C(2), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            bytes != (uint64_t)sizeof(CoverBytes) || guard[0] != 0x7FU ||
            cna_album_copy_thumbnail(album, guard, UINT64_C(2), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            guard[0] != 0x7FU) {
            return 0;
        }
    }

    return cna_song_collection_destroy(songs) == CNA_RESULT_SUCCESS &&
        cna_artist_destroy(artist) == CNA_RESULT_SUCCESS &&
        cna_genre_destroy(genre) == CNA_RESULT_SUCCESS;
}

static int validate_song_back_pointers(const CNA_SongHandle song)
{
    CNA_AlbumHandle album = CNA_INVALID_HANDLE;
    CNA_ArtistHandle artist = CNA_INVALID_HANDLE;
    CNA_GenreHandle genre = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t track = 0;
    uint64_t bytes = UINT64_C(9);
    char text[256];

    /* A scanned song names all three library entities. */
    if (cna_song_get_album(song, &album, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_get_artist(song, &artist, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_song_get_genre(song, &genre, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_album_get_name_size(album, &bytes) != CNA_RESULT_SUCCESS ||
        cna_album_copy_name(album, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, AlbumName) != 0) {
        return 0;
    }
    /* The track number came from the file's own tag rather than defaulting to zero. */
    if (cna_song_get_track_number(song, &track) != CNA_RESULT_SUCCESS ||
        (track != 1 && track != 3)) {
        return 0;
    }
    return cna_album_destroy(album) == CNA_RESULT_SUCCESS &&
        cna_artist_destroy(artist) == CNA_RESULT_SUCCESS &&
        cna_genre_destroy(genre) == CNA_RESULT_SUCCESS;
}

static int validate_library_graph(const CNA_Handle game)
{
    CNA_MediaLibraryHandle library = CNA_INVALID_HANDLE;
    CNA_MediaLibraryHandle from_source = CNA_INVALID_HANDLE;
    CNA_MediaLibraryHandle rejected = CNA_INVALID_HANDLE;
    CNA_SongCollectionHandle songs = CNA_INVALID_HANDLE;
    CNA_AlbumCollectionHandle albums = CNA_INVALID_HANDLE;
    CNA_ArtistCollectionHandle artists = CNA_INVALID_HANDLE;
    CNA_GenreCollectionHandle genres = CNA_INVALID_HANDLE;
    CNA_PlaylistCollectionHandle playlists = CNA_INVALID_HANDLE;
    CNA_AlbumHandle album = CNA_INVALID_HANDLE;
    CNA_AlbumHandle other_album = CNA_INVALID_HANDLE;
    CNA_ArtistHandle artist = CNA_INVALID_HANDLE;
    CNA_SongHandle song = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t count = 9;
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_media_library_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_media_library_create(game, &library) != CNA_RESULT_SUCCESS ||
        library == CNA_INVALID_HANDLE ||
        cna_media_library_get_is_disposed(library, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || !validate_library_source(library)) {
        return 0;
    }

    /* The scan found exactly the fixture: two songs on one album by one artist in one genre. */
    if (cna_media_library_get_songs(library, &songs) != CNA_RESULT_SUCCESS ||
        cna_song_collection_get_count(songs, &count) != CNA_RESULT_SUCCESS || count != 2 ||
        cna_media_library_get_albums(library, &albums) != CNA_RESULT_SUCCESS ||
        cna_album_collection_get_count(albums, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_media_library_get_artists(library, &artists) != CNA_RESULT_SUCCESS ||
        cna_artist_collection_get_count(artists, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_media_library_get_genres(library, &genres) != CNA_RESULT_SUCCESS ||
        cna_genre_collection_get_count(genres, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_media_library_get_playlists(library, &playlists) != CNA_RESULT_SUCCESS ||
        cna_playlist_collection_get_count(playlists, &count) != CNA_RESULT_SUCCESS ||
        count != 0) {
        return 0;
    }

    /* Every collection refuses an index at or past its count, including the empty one. */
    if (cna_album_collection_get_at(albums, 1, &album) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_album_collection_get_at(albums, -1, &album) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_playlist_collection_get_at(playlists, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_album_collection_get_at(albums, 0, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    memset(text, 0, sizeof(text));
    if (cna_album_collection_get_type_name_size(albums, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_album_collection_copy_type_name(albums, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.AlbumCollection") != 0) {
        return 0;
    }

    if (cna_album_collection_get_at(albums, 0, &album) != CNA_RESULT_SUCCESS ||
        !validate_album(album) ||
        cna_artist_collection_get_at(artists, 0, &artist) != CNA_RESULT_SUCCESS ||
        cna_song_collection_get_at(songs, 0, &song) != CNA_RESULT_SUCCESS ||
        !validate_song_back_pointers(song)) {
        return 0;
    }

    /* An artist's album collection is the same album, so entity equality holds across the two
       routes that reach it. */
    {
        CNA_AlbumCollectionHandle artist_albums = CNA_INVALID_HANDLE;
        if (cna_artist_get_albums(artist, &artist_albums) != CNA_RESULT_SUCCESS ||
            cna_album_collection_get_count(artist_albums, &count) != CNA_RESULT_SUCCESS ||
            count != 1 ||
            cna_album_collection_get_at(artist_albums, 0, &other_album) != CNA_RESULT_SUCCESS ||
            cna_album_equals(album, other_album, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_TRUE ||
            cna_album_collection_destroy(artist_albums) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    {
        int32_t hash = 0;
        int32_t other_hash = 0;
        if (cna_album_get_hash_code(album, &hash) != CNA_RESULT_SUCCESS ||
            cna_album_get_hash_code(other_album, &other_hash) != CNA_RESULT_SUCCESS ||
            hash != other_hash) {
            return 0;
        }
    }

    /* Disposal marks the entity and empties a collection, exactly as the canonical types do. */
    if (cna_album_dispose(album) != CNA_RESULT_SUCCESS ||
        cna_album_dispose(album) != CNA_RESULT_SUCCESS ||
        cna_album_get_is_disposed(album, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_album_get_name_size(album, &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_artist_collection_dispose(artists) != CNA_RESULT_SUCCESS ||
        cna_artist_collection_get_is_disposed(artists, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_artist_collection_get_count(artists, &count) != CNA_RESULT_SUCCESS || count != 0 ||
        cna_artist_collection_get_at(artists, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The artist handle taken before the collection was emptied still answers: the collection
       never owned it. */
    memset(text, 0, sizeof(text));
    if (cna_artist_get_name_size(artist, &bytes) != CNA_RESULT_SUCCESS ||
        cna_artist_copy_name(artist, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, ArtistName) != 0) {
        return 0;
    }

    /* Releasing the library handle first is safe: the entity handles keep the library alive. */
    if (cna_media_library_destroy(library) != CNA_RESULT_SUCCESS ||
        cna_media_library_destroy(library) != CNA_RESULT_INVALID_HANDLE ||
        cna_album_get_name_size(album, &bytes) != CNA_RESULT_SUCCESS ||
        cna_song_get_name_size(song, &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_album_destroy(album) != CNA_RESULT_SUCCESS ||
        cna_album_destroy(other_album) != CNA_RESULT_SUCCESS ||
        cna_artist_destroy(artist) != CNA_RESULT_SUCCESS ||
        cna_song_destroy(song) != CNA_RESULT_SUCCESS ||
        cna_song_collection_destroy(songs) != CNA_RESULT_SUCCESS ||
        cna_album_collection_destroy(albums) != CNA_RESULT_SUCCESS ||
        cna_artist_collection_destroy(artists) != CNA_RESULT_SUCCESS ||
        cna_genre_collection_destroy(genres) != CNA_RESULT_SUCCESS ||
        cna_playlist_collection_destroy(playlists) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A library opened from an enumerated source reports that source back. */
    if (cna_media_library_create_from_source(game, UINT32_C(0xFFFFFFFF), &from_source) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        from_source != CNA_INVALID_HANDLE ||
        cna_media_library_create_from_source(game, UINT32_C(0), &from_source) !=
            CNA_RESULT_SUCCESS ||
        !validate_library_source(from_source) ||
        cna_media_library_dispose(from_source) != CNA_RESULT_SUCCESS ||
        cna_media_library_dispose(from_source) != CNA_RESULT_SUCCESS ||
        cna_media_library_get_is_disposed(from_source, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_media_library_get_songs(from_source, &songs) != CNA_RESULT_SUCCESS ||
        cna_song_collection_destroy(songs) != CNA_RESULT_SUCCESS ||
        cna_media_library_destroy(from_source) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A song a caller built has no library context, so all three back-pointers are absent. */
    {
        CNA_SongHandle standalone = CNA_INVALID_HANDLE;
        char path[1024];
        CNA_AlbumHandle absent = CNA_INVALID_HANDLE;
        if (!join_fixture_path(path, sizeof(path), "first_track.mp3") ||
            cna_song_create(game, view(path), view("Standalone"), &standalone) !=
                CNA_RESULT_SUCCESS ||
            cna_song_get_album(standalone, &absent, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE || absent != CNA_INVALID_HANDLE ||
            cna_song_get_artist(standalone, &absent, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE ||
            cna_song_get_genre(standalone, &absent, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE ||
            cna_song_destroy(standalone) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* A handle that was never created is refused by every route in this family. */
    return cna_media_library_get_is_disposed(rejected, &flag) == CNA_RESULT_INVALID_HANDLE &&
        cna_media_library_destroy(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_album_get_name_size(rejected, &bytes) == CNA_RESULT_INVALID_HANDLE &&
        cna_artist_get_hash_code(rejected, &count) == CNA_RESULT_INVALID_HANDLE &&
        cna_genre_dispose(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_playlist_get_duration(rejected, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_album_collection_get_count(rejected, &count) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    LibrarySmokeState* const state = (LibrarySmokeState*)context;
    if (game_time == 0 || !validate_library_graph(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_Bool disposed = CNA_FALSE;
    state->result = cna_media_library_get_is_disposed(state->library, &disposed);
    return 0;
}

int main(void)
{
    if (!create_fixture()) {
        return 1;
    }

    LibrarySmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API media library smoke", UINT64_C(25)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return 2;
    }

    CNA_MediaLibraryHandle library = CNA_INVALID_HANDLE;
    if (cna_media_library_create(game, &library) != CNA_RESULT_SUCCESS) {
        return 3;
    }
    WrongThreadState wrong_thread = {library, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        return 4;
    }
    if (cna_media_library_destroy(library) != CNA_RESULT_SUCCESS) {
        return 5;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 6;
    }
    return 0;
}
