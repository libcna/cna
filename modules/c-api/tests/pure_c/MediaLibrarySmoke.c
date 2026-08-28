// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdio.h>
#include <string.h>
#include <threads.h>

#ifndef CNA_C_API_MEDIA_FIXTURE_MUSIC
#error "CNA_C_API_MEDIA_FIXTURE_MUSIC must name the fixture music directory"
#endif

#ifndef CNA_C_API_MEDIA_FIXTURE_PICTURES
#error "CNA_C_API_MEDIA_FIXTURE_PICTURES must name the fixture picture directory"
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

static int join_path(
    char* const buffer,
    const size_t capacity,
    const char* const root,
    const char* const name)
{
    if (strlen(root) + strlen(name) + 2U > capacity) {
        return 0;
    }
    strcpy(buffer, root);
    strcat(buffer, "/");
    strcat(buffer, name);
    return 1;
}

static int join_fixture_path(char* const buffer, const size_t capacity, const char* const name)
{
    return join_path(buffer, capacity, CNA_C_API_MEDIA_FIXTURE_MUSIC, name);
}

/* A one-pixel BMP: small enough to embed, real enough for the image loader to measure. */
static const uint8_t FixtureBmp[58] = {
    0x42U, 0x4DU, 0x3AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x36U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x13U, 0x0BU,
    0x00U, 0x00U, 0x13U, 0x0BU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x14U,
    0x0AU, 0x00U
};

static int write_picture_fixture(void)
{
    char path[1024];
    FILE* file = 0;
    size_t written = 0U;
    if (!join_path(path, sizeof(path), CNA_C_API_MEDIA_FIXTURE_PICTURES, "fixture_picture.bmp")) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == 0) {
        return 0;
    }
    written = fwrite(FixtureBmp, 1U, sizeof(FixtureBmp), file);
    return fclose(file) == 0 && written == sizeof(FixtureBmp);
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
        write_tagged_song(second, SecondTitle, "1") && write_cover(cover) &&
        write_picture_fixture();
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

static int validate_picture(const CNA_PictureHandle picture, const char* const expected_name)
{
    CNA_PictureAlbumHandle album = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int64_t ticks = INT64_C(-1);
    int32_t number = -1;
    uint64_t bytes = UINT64_C(9);
    uint8_t image[128];
    char text[1024];

    memset(text, 0, sizeof(text));
    if (cna_picture_get_name_size(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_picture_copy_name(picture, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (expected_name != 0 && strcmp(text, expected_name) != 0)) {
        return 0;
    }
    /* The scan measured the fixture image, so the dimensions are the real ones. */
    if (cna_picture_get_width(picture, &number) != CNA_RESULT_SUCCESS || number != 1 ||
        cna_picture_get_height(picture, &number) != CNA_RESULT_SUCCESS || number != 1) {
        return 0;
    }
    /* The date is a point in time counted from the Unix epoch, and a real file has a real one. */
    if (cna_picture_get_date_unix_ticks(picture, &ticks) != CNA_RESULT_SUCCESS ||
        ticks <= INT64_C(0)) {
        return 0;
    }
    /* The token is the picture's own path, and it round-trips through the library lookup. */
    memset(text, 0, sizeof(text));
    if (cna_picture_get_token_size_ext(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(text) ||
        cna_picture_copy_token_ext(picture, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes) {
        return 0;
    }
    /* The image bytes are exactly the fixture's, and the thumbnail is the same image. */
    memset(image, 0, sizeof(image));
    if (cna_picture_get_image_size(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)sizeof(FixtureBmp) ||
        cna_picture_copy_image(picture, image, (uint64_t)sizeof(image), &bytes) !=
            CNA_RESULT_SUCCESS ||
        memcmp(image, FixtureBmp, sizeof(FixtureBmp)) != 0) {
        return 0;
    }
    memset(image, 0, sizeof(image));
    if (cna_picture_get_thumbnail_size(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)sizeof(FixtureBmp) ||
        cna_picture_copy_thumbnail(picture, image, (uint64_t)sizeof(image), &bytes) !=
            CNA_RESULT_SUCCESS ||
        memcmp(image, FixtureBmp, sizeof(FixtureBmp)) != 0) {
        return 0;
    }
    {
        uint8_t guard[4];
        memset(guard, 0x7FU, sizeof(guard));
        if (cna_picture_copy_image(picture, guard, UINT64_C(2), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            guard[0] != 0x7FU) {
            return 0;
        }
    }
    /* Every scanned picture belongs to an album. */
    if (cna_picture_get_album(picture, &album, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_picture_album_get_name_size(album, &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_picture_album_destroy(album) == CNA_RESULT_SUCCESS;
}

static int validate_picture_family(const CNA_Handle game, const CNA_MediaLibraryHandle library)
{
    CNA_PictureCollectionHandle pictures = CNA_INVALID_HANDLE;
    CNA_PictureCollectionHandle saved = CNA_INVALID_HANDLE;
    CNA_PictureAlbumHandle root = CNA_INVALID_HANDLE;
    CNA_PictureAlbumHandle parent = CNA_INVALID_HANDLE;
    CNA_PictureAlbumCollectionHandle child_albums = CNA_INVALID_HANDLE;
    CNA_PictureHandle picture = CNA_INVALID_HANDLE;
    CNA_PictureHandle found = CNA_INVALID_HANDLE;
    CNA_PictureHandle rejected = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t count = 9;
    int32_t hash = 0;
    int32_t other_hash = 0;
    uint64_t bytes = UINT64_C(9);
    char token[1024];
    char text[256];

    (void)game;
    if (cna_media_library_get_pictures(library, &pictures) != CNA_RESULT_SUCCESS ||
        cna_picture_collection_get_count(pictures, &count) != CNA_RESULT_SUCCESS || count < 1 ||
        cna_media_library_get_saved_pictures(library, &saved) != CNA_RESULT_SUCCESS ||
        cna_picture_collection_get_count(saved, &count) != CNA_RESULT_SUCCESS || count < 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_picture_collection_get_type_name_size(pictures, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_picture_collection_copy_type_name(pictures, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.PictureCollection") != 0) {
        return 0;
    }

    /* The picture tree has a root, and the root is the one album with no parent. */
    if (cna_media_library_get_root_picture_album(library, &root, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_picture_album_get_parent(root, &parent, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || parent != CNA_INVALID_HANDLE ||
        cna_picture_album_get_albums(root, &child_albums) != CNA_RESULT_SUCCESS ||
        cna_picture_album_collection_get_count(child_albums, &count) != CNA_RESULT_SUCCESS ||
        count < 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_picture_album_get_type_name_size(root, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_picture_album_copy_type_name(root, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.PictureAlbum") != 0) {
        return 0;
    }

    /* The fixture picture is reachable both through the collection and through its own token. */
    if (cna_picture_collection_get_at(pictures, 0, &picture) != CNA_RESULT_SUCCESS ||
        !validate_picture(picture, 0)) {
        return 0;
    }
    memset(token, 0, sizeof(token));
    if (cna_picture_get_token_size_ext(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(token) ||
        cna_picture_copy_token_ext(picture, token, (uint64_t)sizeof(token), &bytes) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        CNA_StringView token_view;
        token_view.data = token;
        token_view.byte_length = bytes;
        if (cna_media_library_get_picture_from_token(library, token_view, &found, &flag) !=
                CNA_RESULT_SUCCESS ||
            flag != CNA_TRUE ||
            cna_picture_equals(picture, found, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
            cna_picture_get_hash_code(picture, &hash) != CNA_RESULT_SUCCESS ||
            cna_picture_get_hash_code(found, &other_hash) != CNA_RESULT_SUCCESS ||
            hash != other_hash) {
            return 0;
        }
    }
    /* An unknown token is an ordinary answer, not a failure. */
    {
        CNA_PictureHandle absent = CNA_INVALID_HANDLE;
        if (cna_media_library_get_picture_from_token(
                library, view("no such token"), &absent, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE || absent != CNA_INVALID_HANDLE) {
            return 0;
        }
    }

    /* Saving writes a real file, indexes it, and hands back a live picture. */
    {
        CNA_PictureHandle saved_picture = CNA_INVALID_HANDLE;
        char saved_token[1024];
        if (cna_media_library_save_picture(
                library, view("cna_saved"), FixtureBmp, (uint64_t)sizeof(FixtureBmp),
                &saved_picture) != CNA_RESULT_SUCCESS ||
            saved_picture == CNA_INVALID_HANDLE ||
            cna_media_library_save_picture(
                library, view("cna_saved"), 0, (uint64_t)sizeof(FixtureBmp), &rejected) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_media_library_save_picture(
                library, view("cna_saved"), FixtureBmp, (uint64_t)sizeof(FixtureBmp), 0) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        memset(saved_token, 0, sizeof(saved_token));
        if (cna_picture_get_token_size_ext(saved_picture, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(saved_token) ||
            cna_picture_copy_token_ext(
                saved_picture, saved_token, (uint64_t)sizeof(saved_token), &bytes) !=
                CNA_RESULT_SUCCESS ||
            !validate_picture(saved_picture, "cna_saved")) {
            return 0;
        }
        /* The saved picture joined the saved-picture collection. */
        if (cna_media_library_get_saved_pictures(library, &saved) != CNA_RESULT_SUCCESS ||
            cna_picture_collection_get_count(saved, &count) != CNA_RESULT_SUCCESS || count < 1 ||
            cna_picture_destroy(saved_picture) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        /* The suite owns this file, so it does not leave it behind for the next run. */
        (void)remove(saved_token);
    }

    /* CBIND-065: the picture-tree surface the walk above reached around. The root album, its
       child-album collection and its own pictures are all already in hand. */
    memset(text, 0, sizeof(text));
    if (cna_picture_album_copy_name(root, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes == 0U ||
        cna_picture_album_equals(root, root, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    {
        CNA_PictureCollectionHandle root_pictures = CNA_INVALID_HANDLE;
        int32_t root_count = 9;
        if (cna_picture_album_get_pictures(root, &root_pictures) != CNA_RESULT_SUCCESS ||
            cna_picture_collection_get_count(root_pictures, &root_count) != CNA_RESULT_SUCCESS ||
            root_count < 0 ||
            cna_picture_collection_get_is_disposed(root_pictures, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE ||
            cna_picture_collection_destroy(root_pictures) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    memset(text, 0, sizeof(text));
    if (cna_picture_album_collection_get_type_name_size(child_albums, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_picture_album_collection_copy_type_name(
            child_albums, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.PictureAlbumCollection") != 0 ||
        cna_picture_album_collection_get_is_disposed(child_albums, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    {
        /* The fixture's picture folder may or may not hold a sub-album, so the index route is
           asserted against the count rather than against an assumed shape. */
        CNA_PictureAlbumHandle child = CNA_INVALID_HANDLE;
        int32_t child_count = 0;
        if (cna_picture_album_collection_get_count(child_albums, &child_count) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (child_count > 0) {
            if (cna_picture_album_collection_get_at(child_albums, 0, &child) !=
                CNA_RESULT_SUCCESS) {
                return 0;
            }
        }
        if (cna_picture_album_collection_get_at(child_albums, child_count, &child) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    memset(text, 0, sizeof(text));
    if (cna_picture_get_type_name_size(picture, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_picture_copy_type_name(picture, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Media.Picture") != 0) {
        return 0;
    }
    /* The stream-taking save is covered by its refusal only. Its accepting path needs a storage
       container, and this test's whole determinism rests on the XDG root its own scan reads --
       creating a storage device here would move that root out from under the fixture. Said
       plainly rather than dressed up: this proves the handle is validated, and nothing more. */
    {
        CNA_PictureHandle from_stream = UINT64_C(9);
        if (cna_media_library_save_picture_from_stream(
                library, view("cna_stream_saved"), CNA_INVALID_HANDLE, &from_stream) !=
                CNA_RESULT_INVALID_HANDLE ||
            from_stream != CNA_INVALID_HANDLE) {
            return 0;
        }
    }

    /* Disposal marks a picture and empties a collection, and every index is then refused. */
    if (cna_picture_dispose(picture) != CNA_RESULT_SUCCESS ||
        cna_picture_dispose(picture) != CNA_RESULT_SUCCESS ||
        cna_picture_get_is_disposed(picture, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_picture_get_name_size(picture, &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_picture_album_dispose(root) != CNA_RESULT_SUCCESS ||
        cna_picture_album_get_is_disposed(root, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_picture_collection_dispose(pictures) != CNA_RESULT_SUCCESS ||
        cna_picture_collection_get_count(pictures, &count) != CNA_RESULT_SUCCESS || count != 0 ||
        cna_picture_collection_get_at(pictures, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_picture_album_collection_dispose(child_albums) != CNA_RESULT_SUCCESS ||
        cna_picture_album_collection_get_count(child_albums, &count) != CNA_RESULT_SUCCESS ||
        count != 0) {
        return 0;
    }

    return cna_picture_destroy(picture) == CNA_RESULT_SUCCESS &&
        cna_picture_destroy(found) == CNA_RESULT_SUCCESS &&
        cna_picture_album_destroy(root) == CNA_RESULT_SUCCESS &&
        cna_picture_album_collection_destroy(child_albums) == CNA_RESULT_SUCCESS &&
        cna_picture_collection_destroy(pictures) == CNA_RESULT_SUCCESS &&
        cna_picture_collection_destroy(saved) == CNA_RESULT_SUCCESS &&
        cna_picture_get_name_size(rejected, &bytes) == CNA_RESULT_INVALID_HANDLE &&
        cna_picture_album_get_hash_code(rejected, &count) == CNA_RESULT_INVALID_HANDLE &&
        cna_picture_collection_get_count(rejected, &count) == CNA_RESULT_INVALID_HANDLE;
}

/* CBIND-065: the entity surface the graph test reached around.
 *
 * `check_route_test_coverage.py` measured 49 media-library routes that no test named, while the
 * coverage matrix recorded their families implemented and cited this file. That is the shape
 * CBIND-052A found once already: a rule credits its test description to every symbol it covers,
 * including the ones the test never calls. Everything below is reachable from the same fixture the
 * graph test already builds -- one artist, one album, one genre, one folder of pictures -- so none
 * of it needed new scaffolding, only asking.
 */
static int validate_entity_type_names(
    const CNA_ArtistCollectionHandle artists,
    const CNA_GenreCollectionHandle genres,
    const CNA_PlaylistCollectionHandle playlists,
    const CNA_AlbumHandle album,
    const CNA_ArtistHandle artist,
    const CNA_GenreHandle genre)
{
    char text[256];
    uint64_t bytes = UINT64_C(9);

    /* Every entity and collection answers its own fully qualified .NET name, and the two-call
       contract holds for each: the size first, then a copy that writes exactly that many bytes. */
    struct { const char* expected; CNA_Result (*size)(CNA_Handle, uint64_t*);
             CNA_Result (*copy)(CNA_Handle, char*, uint64_t, uint64_t*); CNA_Handle handle; }
    cases[] = {
        {"Microsoft.Xna.Framework.Media.Album", cna_album_get_type_name_size,
         cna_album_copy_type_name, album},
        {"Microsoft.Xna.Framework.Media.Artist", cna_artist_get_type_name_size,
         cna_artist_copy_type_name, artist},
        {"Microsoft.Xna.Framework.Media.ArtistCollection",
         cna_artist_collection_get_type_name_size, cna_artist_collection_copy_type_name, artists},
        {"Microsoft.Xna.Framework.Media.GenreCollection",
         cna_genre_collection_get_type_name_size, cna_genre_collection_copy_type_name, genres},
        {"Microsoft.Xna.Framework.Media.PlaylistCollection",
         cna_playlist_collection_get_type_name_size, cna_playlist_collection_copy_type_name,
         playlists},
        {"Microsoft.Xna.Framework.Media.Genre", cna_genre_get_type_name_size,
         cna_genre_copy_type_name, genre},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memset(text, 0, sizeof(text));
        if (cases[i].size(cases[i].handle, &bytes) != CNA_RESULT_SUCCESS ||
            bytes != (uint64_t)strlen(cases[i].expected) ||
            bytes >= (uint64_t)sizeof(text) ||
            cases[i].copy(cases[i].handle, text, (uint64_t)sizeof(text), &bytes) !=
                CNA_RESULT_SUCCESS ||
            strcmp(text, cases[i].expected) != 0 ||
            /* and the same too-small refusal every count/copy pair in this ABI makes */
            cases[i].copy(cases[i].handle, text, UINT64_C(1), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL) {
            return 0;
        }
    }
    return 1;
}

static int validate_genre_and_artist_reach(
    const CNA_GenreCollectionHandle genres,
    const CNA_ArtistHandle artist,
    const CNA_AlbumHandle album)
{
    CNA_GenreHandle genre = CNA_INVALID_HANDLE;
    CNA_GenreHandle same_genre = CNA_INVALID_HANDLE;
    CNA_SongCollectionHandle genre_songs = CNA_INVALID_HANDLE;
    CNA_SongCollectionHandle artist_songs = CNA_INVALID_HANDLE;
    CNA_AlbumCollectionHandle genre_albums = CNA_INVALID_HANDLE;
    CNA_AlbumHandle genre_album = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t count = 9;
    int32_t hash = 0;
    int32_t other_hash = 1;

    /* The fixture's one genre, reached by index, is the same genre twice -- which is what makes
       equality and the hash meaningful rather than tautological. */
    if (cna_genre_collection_get_at(genres, 0, &genre) != CNA_RESULT_SUCCESS ||
        cna_genre_collection_get_at(genres, 0, &same_genre) != CNA_RESULT_SUCCESS ||
        cna_genre_equals(genre, same_genre, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_genre_get_hash_code(genre, &hash) != CNA_RESULT_SUCCESS ||
        cna_genre_get_hash_code(same_genre, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }
    /* Both songs carry that genre, and its one album is the album the library also reports. */
    if (cna_genre_get_songs(genre, &genre_songs) != CNA_RESULT_SUCCESS ||
        cna_song_collection_get_count(genre_songs, &count) != CNA_RESULT_SUCCESS || count != 2 ||
        cna_genre_get_albums(genre, &genre_albums) != CNA_RESULT_SUCCESS ||
        cna_album_collection_get_count(genre_albums, &count) != CNA_RESULT_SUCCESS || count != 1 ||
        cna_album_collection_get_at(genre_albums, 0, &genre_album) != CNA_RESULT_SUCCESS ||
        cna_album_equals(album, genre_album, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    /* The artist's songs are the same two, reached by the other edge of the graph. */
    if (cna_artist_get_songs(artist, &artist_songs) != CNA_RESULT_SUCCESS ||
        cna_song_collection_get_count(artist_songs, &count) != CNA_RESULT_SUCCESS || count != 2) {
        return 0;
    }
    {
        /* An artist compared with itself, for the same reason as the genre above. */
        CNA_Bool same = UINT8_C(9);
        if (cna_artist_equals(artist, artist, &same) != CNA_RESULT_SUCCESS || same != CNA_TRUE) {
            return 0;
        }
    }

    /* Disposal is idempotent and observable on every entity kind, and an emptied collection
       still answers its count as zero rather than refusing. */
    if (cna_genre_get_is_disposed(genre, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_genre_collection_get_is_disposed(genres, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_genre_collection_dispose(genres) != CNA_RESULT_SUCCESS ||
        cna_genre_collection_dispose(genres) != CNA_RESULT_SUCCESS ||
        cna_genre_collection_get_is_disposed(genres, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_genre_collection_get_count(genres, &count) != CNA_RESULT_SUCCESS || count != 0 ||
        cna_genre_collection_get_at(genres, 0, &same_genre) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The genre handle taken before the collection was emptied still answers -- the collection
       never owned it, the same rule the artist handle already proves below. The artist is only
       *observed* here: disposing it is left to the caller, because the graph test still reads its
       name after emptying the artist collection. */
    if (cna_genre_get_is_disposed(genre, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_artist_get_is_disposed(artist, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE) {
        return 0;
    }
    return cna_song_collection_destroy(genre_songs) == CNA_RESULT_SUCCESS &&
        cna_song_collection_destroy(artist_songs) == CNA_RESULT_SUCCESS &&
        cna_album_collection_destroy(genre_albums) == CNA_RESULT_SUCCESS;
}

/* CBIND-065: the collection disposal surface, and the playlist family the fixture cannot build.
 *
 * A music folder with no playlist file produces an empty playlist collection, so the playlist
 * *entity* routes have no entity to answer for. They are covered here by the refusal a caller
 * actually meets -- an invalid handle -- and that is said plainly rather than dressed up: this
 * proves each route validates its handle before doing anything, and nothing more.
 */
static int validate_collection_disposal_and_playlists(
    const CNA_AlbumCollectionHandle albums,
    const CNA_PlaylistCollectionHandle playlists)
{
    CNA_Bool flag = UINT8_C(9);
    int32_t count = 9;
    int32_t hash = 0;
    uint64_t bytes = UINT64_C(9);
    char text[128];
    CNA_SongCollectionHandle songs = CNA_INVALID_HANDLE;

    if (cna_album_collection_get_is_disposed(albums, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_album_collection_dispose(albums) != CNA_RESULT_SUCCESS ||
        cna_album_collection_dispose(albums) != CNA_RESULT_SUCCESS ||
        cna_album_collection_get_is_disposed(albums, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_album_collection_get_count(albums, &count) != CNA_RESULT_SUCCESS || count != 0) {
        return 0;
    }
    /* An already-empty collection disposes exactly as a populated one does. */
    if (cna_playlist_collection_get_is_disposed(playlists, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_playlist_collection_dispose(playlists) != CNA_RESULT_SUCCESS ||
        cna_playlist_collection_get_is_disposed(playlists, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_playlist_collection_get_count(playlists, &count) != CNA_RESULT_SUCCESS ||
        count != 0) {
        return 0;
    }

    /* Refusal-path only, for the reason given above. */
    memset(text, 0, sizeof(text));
    if (cna_playlist_get_name_size(CNA_INVALID_HANDLE, &bytes) != CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_copy_name(CNA_INVALID_HANDLE, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_get_type_name_size(CNA_INVALID_HANDLE, &bytes) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_copy_type_name(CNA_INVALID_HANDLE, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_get_songs(CNA_INVALID_HANDLE, &songs) != CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_get_hash_code(CNA_INVALID_HANDLE, &hash) != CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_equals(CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, &flag) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_get_is_disposed(CNA_INVALID_HANDLE, &flag) != CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_dispose(CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE ||
        cna_playlist_destroy(CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
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

    /* CBIND-065: everything the graph walk above reached around -- type names, the genre and
       artist edges, entity equality and the disposal observability -- while every one of those
       routes was recorded implemented. Run before the disposals below, which empty the
       collections these need. */
    {
        CNA_GenreHandle first_genre = CNA_INVALID_HANDLE;
        if (cna_genre_collection_get_at(genres, 0, &first_genre) != CNA_RESULT_SUCCESS ||
            !validate_entity_type_names(artists, genres, playlists, album, artist, first_genre) ||
            !validate_genre_and_artist_reach(genres, artist, album)) {
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

    /* CBIND-065: the collection disposal surface and the playlist family. Placed after the
       artist-name check for the same reason -- these empty the album collection. */
    if (!validate_collection_disposal_and_playlists(albums, playlists)) {
        return 0;
    }

    /* CBIND-065: and now the artist may go. Disposal is idempotent and observable, and it is
       asserted last precisely because the check above needs the artist still answering. */
    if (cna_artist_dispose(artist) != CNA_RESULT_SUCCESS ||
        cna_artist_dispose(artist) != CNA_RESULT_SUCCESS ||
        cna_artist_get_is_disposed(artist, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }

    if (!validate_picture_family(game, library)) {
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
        return CNA_TEST_FAIL(1);
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
        return CNA_TEST_FAIL(2);
    }

    CNA_MediaLibraryHandle library = CNA_INVALID_HANDLE;
    if (cna_media_library_create(game, &library) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(3);
    }
    WrongThreadState wrong_thread = {library, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(4);
    }
    if (cna_media_library_destroy(library) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(5);
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(6);
    }
    return 0;
}
