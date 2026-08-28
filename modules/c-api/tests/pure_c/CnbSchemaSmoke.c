// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-110 -- the `.cnb` sprite-font, sound-effect, song, video, curve and
 * animation-clip schemas.
 *
 * Five schemas share one suite because what has to be proved about them is the same shape, and
 * because three of them are bound to C values other families already publish: a font's glyphs are
 * `CNA_SpriteFontGlyph`, a curve is the `CNA_CurveHandle` the curve family owns, and a clip is the
 * `CNA_AnimationClipEXTDescriptor` the skinned-model routes take. A round trip that came back
 * through a *parallel* set of types would prove nothing about that, so each round trip here ends by
 * reading the decoded asset through the very routes the rest of the ABI uses.
 *
 * Four things get more than a round trip, because a round trip cannot see them:
 *
 *   - the **audio format numbering**, which is wire format and would survive being wrong in both
 *     directions at once;
 *   - the **strictly-ascending character map**, which the encoder enforces and an unsorted font
 *     would otherwise carry into a file no reader accepts;
 *   - the **48-byte keyframe**, written through the byte writer and measured, because the whole
 *     point of the shared keyframe routines is that a model's embedded clips and a standalone clip
 *     store keyframes identically;
 *   - and the **single `XREF`** a song and a video each name, checked against the container's own
 *     external-reference table so the two views of one fact cannot drift.
 */

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbSchemaSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static CNA_StringView view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static void report_last_error(const char* const what)
{
    char message[512];
    uint64_t produced = 0U;
    if (cna_error_copy_last_message(message, sizeof(message) - 1U, &produced) ==
        CNA_RESULT_SUCCESS) {
        message[produced] = '\0';
        fprintf(stderr, "%s: %s\n", what, message);
    }
}

static int near_enough(const float actual, const float expected)
{
    return fabsf(actual - expected) < 1.0e-6f;
}

static uint8_t g_encoded[262144];

/* The identities are wire format: a renumbering must break here, not in a file written months on. */
static int validate_identities(void)
{
    REQUIRE(sizeof(CNA_CnbAudioFormat) == sizeof(uint32_t));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_UNKNOWN == UINT32_C(0));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_PCM16 == UINT32_C(1));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_PCM8 == UINT32_C(2));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_PCM_FLOAT32 == UINT32_C(3));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_ADPCM == UINT32_C(4));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_VORBIS == UINT32_C(5));
    REQUIRE(CNA_CNB_AUDIO_FORMAT_MAXIMUM == CNA_CNB_AUDIO_FORMAT_VORBIS);

    /* Every chunk identifier equals what packing its four letters gives. */
    {
        CNA_CnbChunkId id = 0U;
        REQUIRE(cna_cnb_make_chunk_id('F', 'O', 'N', 'T', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_SPRITE_FONT_CHUNK_HEADER);
        REQUIRE(cna_cnb_make_chunk_id('C', 'H', 'A', 'R', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_SPRITE_FONT_CHUNK_CHARACTERS);
        REQUIRE(cna_cnb_make_chunk_id('A', 'U', 'D', 'D', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_SOUND_EFFECT_CHUNK_DATA);
        REQUIRE(cna_cnb_make_chunk_id('S', 'N', 'G', 'H', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_MEDIA_CHUNK_SONG_HEADER);
        REQUIRE(cna_cnb_make_chunk_id('V', 'I', 'D', 'H', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_MEDIA_CHUNK_VIDEO_HEADER);
        REQUIRE(cna_cnb_make_chunk_id('C', 'R', 'V', 'K', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_CURVE_CHUNK_KEYS);
        REQUIRE(cna_cnb_make_chunk_id('A', 'C', 'L', 'K', &id) == CNA_RESULT_SUCCESS);
        REQUIRE(id == CNA_CNB_ANIMATION_CLIP_CHUNK_KEYS);
    }

    REQUIRE(CNA_CNB_SPRITE_FONT_SCHEMA_VERSION == UINT32_C(1));
    REQUIRE(CNA_CNB_SPRITE_FONT_HEADER_STRIDE == UINT32_C(24));
    REQUIRE(CNA_CNB_SPRITE_FONT_RECTANGLE_STRIDE == UINT32_C(16));
    REQUIRE(CNA_CNB_SPRITE_FONT_KERNING_STRIDE == UINT32_C(12));
    REQUIRE(CNA_CNB_SPRITE_FONT_CHARACTER_STRIDE == UINT32_C(4));
    REQUIRE(CNA_CNB_MAX_SPRITE_FONT_GLYPHS == UINT32_C(65536));
    REQUIRE(CNA_CNB_SOUND_EFFECT_HEADER_STRIDE == UINT32_C(28));
    REQUIRE(CNA_CNB_MAX_AUDIO_SAMPLE_RATE == UINT32_C(384000));
    REQUIRE(CNA_CNB_SONG_HEADER_FIXED_STRIDE == UINT32_C(8));
    REQUIRE(CNA_CNB_VIDEO_HEADER_STRIDE == UINT32_C(24));
    REQUIRE(CNA_CNB_MAX_VIDEO_DIMENSION == UINT32_C(65536));
    REQUIRE(CNA_CNB_CURVE_KEY_STRIDE == UINT32_C(20));
    REQUIRE(CNA_CNB_ANIMATION_TRACK_STRIDE == UINT32_C(12));
    REQUIRE(CNA_CNB_ANIMATION_KEY_STRIDE == UINT32_C(48));

    /* A frame size is a real answer for a format with one, and 0 for a format without. */
    {
        uint32_t frame = UINT32_MAX;
        REQUIRE(cna_cnb_audio_frame_bytes(CNA_CNB_AUDIO_FORMAT_PCM16, 1U, &frame) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(frame == 2U);
        REQUIRE(cna_cnb_audio_frame_bytes(CNA_CNB_AUDIO_FORMAT_PCM16, 2U, &frame) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(frame == 4U);
        REQUIRE(cna_cnb_audio_frame_bytes(CNA_CNB_AUDIO_FORMAT_VORBIS, 2U, &frame) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(frame == 0U);
        /* An identity outside the named set is an ordinary answer, not a refusal: a corrupt file
           can contain any 32-bit number and a diagnostic must survive reading it. */
        REQUIRE(cna_cnb_audio_frame_bytes(UINT32_C(99), 2U, &frame) == CNA_RESULT_SUCCESS);
        REQUIRE(frame == 0U);
    }

    /* And an unnamed identity still renders, so a reader can say which it found. */
    {
        char name[64];
        uint64_t produced = 0U;
        REQUIRE(cna_cnb_get_audio_format_name_size(CNA_CNB_AUDIO_FORMAT_PCM16, &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced > 0U);
        REQUIRE(cna_cnb_copy_audio_format_name(
                    CNA_CNB_AUDIO_FORMAT_PCM16, name, sizeof(name), &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced > 0U);
        REQUIRE(cna_cnb_get_audio_format_name_size(UINT32_C(99), &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced > 0U);
        REQUIRE(cna_cnb_copy_audio_format_name(UINT32_C(99), name, sizeof(name), &produced) ==
                CNA_RESULT_SUCCESS);
    }
    return 1;
}

static CNA_SpriteFontGlyph make_glyph(const CNA_Char16 character, const int32_t seed)
{
    CNA_SpriteFontGlyph glyph;
    memset(&glyph, 0, sizeof(glyph));
    glyph.struct_size = (uint32_t)sizeof(glyph);
    glyph.struct_version = UINT32_C(1);
    glyph.glyph_bounds.x = seed;
    glyph.glyph_bounds.y = seed + 1;
    glyph.glyph_bounds.width = 4;
    glyph.glyph_bounds.height = 6;
    glyph.cropping.x = seed + 2;
    glyph.cropping.y = seed + 3;
    glyph.cropping.width = 1;
    glyph.cropping.height = 2;
    glyph.character = character;
    glyph.kerning.x = (float)seed;
    glyph.kerning.y = 4.0f;
    glyph.kerning.z = (float)seed + 0.5f;
    return glyph;
}

static int expect_glyph(
    const CNA_CnbSpriteFontDataHandle font,
    const uint64_t index,
    const CNA_Char16 character,
    const int32_t seed)
{
    CNA_SpriteFontGlyph glyph;
    memset(&glyph, 0, sizeof(glyph));
    glyph.struct_size = (uint32_t)sizeof(glyph);
    glyph.struct_version = UINT32_C(1);
    REQUIRE(cna_cnb_sprite_font_data_get_glyph(font, index, &glyph) == CNA_RESULT_SUCCESS);
    REQUIRE(glyph.character == character);
    REQUIRE(glyph.glyph_bounds.x == seed && glyph.glyph_bounds.y == seed + 1);
    REQUIRE(glyph.glyph_bounds.width == 4 && glyph.glyph_bounds.height == 6);
    REQUIRE(glyph.cropping.x == seed + 2 && glyph.cropping.y == seed + 3);
    REQUIRE(glyph.cropping.width == 1 && glyph.cropping.height == 2);
    REQUIRE(near_enough(glyph.kerning.x, (float)seed));
    REQUIRE(near_enough(glyph.kerning.y, 4.0f));
    REQUIRE(near_enough(glyph.kerning.z, (float)seed + 0.5f));
    REQUIRE(glyph.reserved == 0U);
    return 1;
}

/* The atlas travels inside the font, so it is built, embedded and read back out again. */
static int build_font(CNA_CnbSpriteFontDataHandle* const outFont, const int ascending)
{
    CNA_CnbSpriteFontDataHandle font = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_sprite_font_data_create(&font) == CNA_RESULT_SUCCESS);
    REQUIRE(font != CNA_INVALID_HANDLE);

    static uint8_t rgba[4 * 4 * 4];
    for (size_t i = 0U; i < sizeof(rgba); ++i) { rgba[i] = (uint8_t)(i * 3U); }
    CNA_CnbTextureDataHandle atlas = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_texture_data_create_rgba8(4U, 4U, rgba, sizeof(rgba), &atlas) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_sprite_font_data_set_atlas(font, atlas) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(atlas) == CNA_RESULT_SUCCESS);

    CNA_CnbSpriteFontInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION;
    info.line_spacing = 17;
    info.spacing = 1.25f;
    info.default_character = (CNA_Char16)'?';
    info.has_default_character = CNA_TRUE;
    REQUIRE(cna_cnb_sprite_font_data_set_info(font, &info) == CNA_RESULT_SUCCESS);

    {
        const CNA_SpriteFontGlyph first =
            make_glyph((CNA_Char16)(ascending ? '?' : 'A'), 10);
        const CNA_SpriteFontGlyph second =
            make_glyph((CNA_Char16)(ascending ? 'A' : '?'), 20);
        uint64_t index = UINT64_MAX;
        REQUIRE(cna_cnb_sprite_font_data_add_glyph(font, &first, &index) == CNA_RESULT_SUCCESS);
        REQUIRE(index == 0U);
        REQUIRE(cna_cnb_sprite_font_data_add_glyph(font, &second, &index) == CNA_RESULT_SUCCESS);
        REQUIRE(index == 1U);
    }
    *outFont = font;
    return 1;
}

static int expect_font(const CNA_CnbSpriteFontDataHandle font)
{
    CNA_CnbSpriteFontInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_sprite_font_data_get_info(font, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.glyph_count == 2U);
    REQUIRE(info.line_spacing == 17);
    REQUIRE(near_enough(info.spacing, 1.25f));
    REQUIRE(info.has_default_character == CNA_TRUE);
    REQUIRE(info.default_character == (CNA_Char16)'?');

    if (!expect_glyph(font, 0U, (CNA_Char16)'?', 10)) { return 0; }
    if (!expect_glyph(font, 1U, (CNA_Char16)'A', 20)) { return 0; }

    /* The atlas comes back out as a texture description of its own, not a borrow. */
    {
        CNA_CnbTextureDataHandle atlas = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_sprite_font_data_copy_atlas(font, &atlas) == CNA_RESULT_SUCCESS);
        CNA_CnbTextureInfo textureInfo;
        memset(&textureInfo, 0, sizeof(textureInfo));
        textureInfo.struct_size = (uint32_t)sizeof(textureInfo);
        textureInfo.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
        REQUIRE(cna_cnb_texture_data_get_info(atlas, &textureInfo) == CNA_RESULT_SUCCESS);
        REQUIRE(textureInfo.width == 4U && textureInfo.height == 4U);
        uint8_t level[64];
        uint64_t produced = 0U;
        REQUIRE(cna_cnb_texture_data_copy_level(atlas, 0U, 0U, level, sizeof(level), &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced == 64U && level[9] == (uint8_t)27U);
        /* Destroying the copy leaves the font's own atlas intact. */
        REQUIRE(cna_cnb_texture_data_destroy(atlas) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_sprite_font_data_get_info(font, &info) == CNA_RESULT_SUCCESS);
    }
    return 1;
}

static int validate_sprite_font(void)
{
    CNA_CnbSpriteFontDataHandle font = CNA_INVALID_HANDLE;
    if (!build_font(&font, 1)) { return 0; }
    if (!expect_font(font)) { return 0; }

    uint64_t required = 0U;
    REQUIRE(cna_cnb_encode_sprite_font(font, view("Fonts/Hud"), NULL, 0U, &required) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(required > 0U && required <= sizeof(g_encoded));
    uint64_t produced = 0U;
    {
        const CNA_Result encoding =
            cna_cnb_encode_sprite_font(font, view("Fonts/Hud"), g_encoded, sizeof(g_encoded), &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_sprite_font"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }
    REQUIRE(produced == required);

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("font"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_SPRITE_FONT);
    /* Every chunk the header names is one the encoder wrote. */
    {
        static const CNA_CnbChunkId written[5] = {
            CNA_CNB_SPRITE_FONT_CHUNK_HEADER,
            CNA_CNB_SPRITE_FONT_CHUNK_GLYPH_BOUNDS,
            CNA_CNB_SPRITE_FONT_CHUNK_CROPPING,
            CNA_CNB_SPRITE_FONT_CHUNK_KERNING,
            CNA_CNB_SPRITE_FONT_CHUNK_CHARACTERS};
        for (size_t which = 0U; which < 5U; ++which) {
            CNA_Bool found = CNA_FALSE;
            uint64_t at = 0U;
            REQUIRE(cna_cnb_document_find_single(document, written[which], &found, &at) ==
                    CNA_RESULT_SUCCESS);
            REQUIRE(found == CNA_TRUE);
        }
    }

    CNA_CnbSpriteFontDataHandle decoded = CNA_INVALID_HANDLE;
    {
        const CNA_Result decoding = cna_cnb_decode_sprite_font(document, &decoded);
        if (decoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_decode_sprite_font"); }
        REQUIRE(decoding == CNA_RESULT_SUCCESS);
    }
    if (!expect_font(decoded)) { return 0; }

    /* No default character is an absence the info structure reports, not a zero character. */
    {
        CNA_CnbSpriteFontInfo info;
        memset(&info, 0, sizeof(info));
        info.struct_size = (uint32_t)sizeof(info);
        info.struct_version = CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION;
        info.line_spacing = 17;
        info.spacing = 1.25f;
        info.has_default_character = CNA_FALSE;
        REQUIRE(cna_cnb_sprite_font_data_set_info(decoded, &info) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_sprite_font_data_get_info(decoded, &info) == CNA_RESULT_SUCCESS);
        REQUIRE(info.has_default_character == CNA_FALSE);
        REQUIRE(cna_cnb_encode_sprite_font(decoded, view(""), g_encoded, sizeof(g_encoded),
                                           &produced) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_cnb_sprite_font_data_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_sprite_font_data_destroy(font) == CNA_RESULT_SUCCESS);
    return 1;
}

/*
 * The character map must be strictly ascending. A caller may build the table in any order -- the
 * add route stores what it is given -- and the encoder is what refuses, so a font that would
 * produce a file no reader accepts cannot be written out.
 */
static int validate_character_order(void)
{
    CNA_CnbSpriteFontDataHandle font = CNA_INVALID_HANDLE;
    if (!build_font(&font, 0)) { return 0; }
    uint64_t produced = 0U;
    REQUIRE(cna_cnb_encode_sprite_font(font, view(""), g_encoded, sizeof(g_encoded), &produced) ==
            CNA_RESULT_IO);

    /* Sorting the same two glyphs makes it encode. */
    {
        const CNA_SpriteFontGlyph first = make_glyph((CNA_Char16)'?', 10);
        const CNA_SpriteFontGlyph second = make_glyph((CNA_Char16)'A', 20);
        REQUIRE(cna_cnb_sprite_font_data_set_glyph(font, 0U, &first) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_sprite_font_data_set_glyph(font, 1U, &second) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_encode_sprite_font(font, view(""), g_encoded, sizeof(g_encoded),
                                           &produced) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_cnb_sprite_font_data_destroy(font) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_sound_effect(void)
{
    static int16_t frames[64];
    for (size_t i = 0U; i < 64U; ++i) { frames[i] = (int16_t)(i * 100 - 3200); }

    CNA_CnbSoundEffectInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_SOUND_EFFECT_INFO_STRUCT_VERSION;
    info.format = CNA_CNB_AUDIO_FORMAT_PCM16;
    info.sample_rate = 22050U;
    info.channels = 2U;
    info.frame_count = 32U;
    info.loop_start = 8U;
    info.loop_length = 16U;

    CNA_CnbSoundEffectDataHandle sound = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_sound_effect_data_create(
                &info, (const uint8_t*)frames, sizeof(frames), &sound) == CNA_RESULT_SUCCESS);

    uint64_t produced = 0U;
    {
        const CNA_Result encoding =
            cna_cnb_encode_sound_effect(sound, view("Sounds/Ping"), g_encoded, sizeof(g_encoded),
                                        &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_sound_effect"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("sound"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_SOUND_EFFECT);

    CNA_CnbSoundEffectDataHandle decoded = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_decode_sound_effect(document, &decoded) == CNA_RESULT_SUCCESS);

    CNA_CnbSoundEffectInfo readBack;
    memset(&readBack, 0, sizeof(readBack));
    readBack.struct_size = (uint32_t)sizeof(readBack);
    readBack.struct_version = CNA_CNB_SOUND_EFFECT_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_sound_effect_data_get_info(decoded, &readBack) == CNA_RESULT_SUCCESS);
    REQUIRE(readBack.format == CNA_CNB_AUDIO_FORMAT_PCM16);
    REQUIRE(readBack.sample_rate == 22050U && readBack.channels == 2U);
    REQUIRE(readBack.frame_count == 32U);
    REQUIRE(readBack.loop_start == 8U && readBack.loop_length == 16U);

    /* Frame count times frame size is the sample byte count, which is the invariant the schema
       enforces on both sides -- so reading it back through the published frame size proves the two
       agree rather than each agreeing with itself. */
    {
        uint32_t frameBytes = 0U;
        REQUIRE(cna_cnb_audio_frame_bytes(readBack.format, readBack.channels, &frameBytes) ==
                CNA_RESULT_SUCCESS);
        uint64_t sampleBytes = 0U;
        REQUIRE(cna_cnb_sound_effect_data_copy_samples(decoded, NULL, 0U, &sampleBytes) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(sampleBytes == (uint64_t)readBack.frame_count * frameBytes);
        static uint8_t samples[512];
        REQUIRE(cna_cnb_sound_effect_data_copy_samples(
                    decoded, samples, sizeof(samples), &sampleBytes) == CNA_RESULT_SUCCESS);
        REQUIRE(memcmp(samples, frames, (size_t)sampleBytes) == 0);
    }

    /* A reserved identifier has no v1 codec: it can be named, and it cannot be written. */
    {
        CNA_CnbSoundEffectDataHandle reserved = CNA_INVALID_HANDLE;
        info.format = CNA_CNB_AUDIO_FORMAT_VORBIS;
        REQUIRE(cna_cnb_sound_effect_data_create(
                    &info, (const uint8_t*)frames, sizeof(frames), &reserved) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_encode_sound_effect(reserved, view(""), g_encoded, sizeof(g_encoded),
                                            &produced) == CNA_RESULT_IO);
        REQUIRE(cna_cnb_sound_effect_data_destroy(reserved) == CNA_RESULT_SUCCESS);
    }

    /* A loop region outside the sound is refused by the encoder, not stored silently. */
    {
        CNA_CnbSoundEffectDataHandle broken = CNA_INVALID_HANDLE;
        info.format = CNA_CNB_AUDIO_FORMAT_PCM16;
        info.loop_start = 30U;
        info.loop_length = 16U;
        REQUIRE(cna_cnb_sound_effect_data_create(
                    &info, (const uint8_t*)frames, sizeof(frames), &broken) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_encode_sound_effect(broken, view(""), g_encoded, sizeof(g_encoded),
                                            &produced) == CNA_RESULT_IO);
        REQUIRE(cna_cnb_sound_effect_data_destroy(broken) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_cnb_sound_effect_data_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_sound_effect_data_destroy(sound) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_media(void)
{
    uint64_t produced = 0U;
    {
        const CNA_Result encoding = cna_cnb_encode_song(
            view("Music/Theme.ogg"), view("Main Theme"), 195000U, view("Music/Theme"),
            g_encoded, sizeof(g_encoded), &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_song"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("song"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_SONG);

    uint32_t duration = 0U;
    REQUIRE(cna_cnb_decode_song_duration_milliseconds(document, &duration) == CNA_RESULT_SUCCESS);
    REQUIRE(duration == 195000U);

    char text[64];
    uint64_t bytes = 0U;
    REQUIRE(cna_cnb_decode_song_name_size(document, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 10U);
    REQUIRE(cna_cnb_decode_song_name(document, text, sizeof(text), &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 10U && memcmp(text, "Main Theme", 10U) == 0);
    REQUIRE(cna_cnb_decode_song_stream_reference_size(document, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 15U);
    REQUIRE(cna_cnb_decode_song_stream_reference(document, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 15U && memcmp(text, "Music/Theme.ogg", 15U) == 0);

    /* The stream reference is also the file's single XREF entry, and the two views must agree --
       the schema route is the one that also enforces "exactly one". */
    {
        uint64_t referenceCount = 0U;
        REQUIRE(cna_cnb_document_get_external_reference_count(document, &referenceCount) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(referenceCount == 1U);
        char reference[64];
        uint64_t referenceBytes = 0U;
        REQUIRE(cna_cnb_document_copy_external_reference_name(
                    document, 0U, reference, sizeof(reference), &referenceBytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(referenceBytes == 15U && memcmp(reference, "Music/Theme.ogg", 15U) == 0);
    }

    /* A song with no display name reports a zero-length name, not a failure. */
    {
        CNA_CnbDocumentHandle nameless = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_encode_song(view("Music/Sting.ogg"), view(""), 0U, view(""),
                                    g_encoded, sizeof(g_encoded), &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("song"), NULL, &nameless) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_decode_song_name_size(nameless, &bytes) == CNA_RESULT_SUCCESS);
        REQUIRE(bytes == 0U);
        REQUIRE(cna_cnb_decode_song_duration_milliseconds(nameless, &duration) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(duration == 0U);
        REQUIRE(cna_cnb_document_destroy(nameless) == CNA_RESULT_SUCCESS);
    }

    /* An empty stream reference is the one thing a song may not have. */
    REQUIRE(cna_cnb_encode_song(view(""), view("x"), 0U, view(""), g_encoded, sizeof(g_encoded),
                                &produced) == CNA_RESULT_IO);

    CNA_CnbVideoInfo video;
    memset(&video, 0, sizeof(video));
    video.struct_size = (uint32_t)sizeof(video);
    video.struct_version = CNA_CNB_VIDEO_INFO_STRUCT_VERSION;
    video.duration_milliseconds = 42000U;
    video.width = 1280U;
    video.height = 720U;
    video.frames_per_second = 29.97f;
    video.soundtrack_type = CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC_AND_DIALOG;
    {
        const CNA_Result encoding = cna_cnb_encode_video(
            view("Videos/Intro.mp4"), &video, view("Videos/Intro"), g_encoded, sizeof(g_encoded),
            &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_video"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbDocumentHandle videoDocument = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("video"), NULL, &videoDocument) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_asset_type_id(videoDocument, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_VIDEO);

    CNA_CnbVideoInfo readBack;
    memset(&readBack, 0, sizeof(readBack));
    readBack.struct_size = (uint32_t)sizeof(readBack);
    readBack.struct_version = CNA_CNB_VIDEO_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_decode_video(videoDocument, &readBack) == CNA_RESULT_SUCCESS);
    REQUIRE(readBack.duration_milliseconds == 42000U);
    REQUIRE(readBack.width == 1280U && readBack.height == 720U);
    REQUIRE(near_enough(readBack.frames_per_second, 29.97f));
    REQUIRE(readBack.soundtrack_type == CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC_AND_DIALOG);
    REQUIRE(readBack.reserved == 0U);
    REQUIRE(cna_cnb_decode_video_stream_reference_size(videoDocument, &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 16U);
    REQUIRE(cna_cnb_decode_video_stream_reference(videoDocument, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 16U && memcmp(text, "Videos/Intro.mp4", 16U) == 0);

    /* Out-of-range metadata is refused rather than written. */
    {
        CNA_CnbVideoInfo broken = video;
        broken.width = CNA_CNB_MAX_VIDEO_DIMENSION + 1U;
        REQUIRE(cna_cnb_encode_video(view("Videos/Intro.mp4"), &broken, view(""), g_encoded,
                                     sizeof(g_encoded), &produced) == CNA_RESULT_IO);
        broken = video;
        broken.frames_per_second = 0.0f;
        REQUIRE(cna_cnb_encode_video(view("Videos/Intro.mp4"), &broken, view(""), g_encoded,
                                     sizeof(g_encoded), &produced) == CNA_RESULT_IO);
        broken = video;
        broken.soundtrack_type = CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM + 1U;
        REQUIRE(cna_cnb_encode_video(view("Videos/Intro.mp4"), &broken, view(""), g_encoded,
                                     sizeof(g_encoded), &produced) == CNA_RESULT_IO);
    }

    /* Each media decoder refuses the other's file. */
    REQUIRE(cna_cnb_decode_song_duration_milliseconds(videoDocument, &duration) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_decode_video(document, &readBack) == CNA_RESULT_IO);

    REQUIRE(cna_cnb_document_destroy(videoDocument) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    return 1;
}

/* A curve goes out and comes back as the CNA_CurveHandle the curve family already publishes. */
static int validate_curve(void)
{
    CNA_CurveHandle curve = CNA_INVALID_HANDLE;
    REQUIRE(cna_curve_create(&curve) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_OSCILLATE) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_CYCLE_OFFSET) == CNA_RESULT_SUCCESS);
    {
        CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
        REQUIRE(cna_curve_get_keys(curve, &keys) == CNA_RESULT_SUCCESS);
        CNA_CurveKey key;
        REQUIRE(cna_curve_key_init_full(0.0f, 1.0f, 0.25f, 0.5f, CNA_CURVE_CONTINUITY_SMOOTH,
                                        &key) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_curve_key_collection_add(keys, key) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_curve_key_init_full(2.0f, -3.0f, 1.5f, -1.5f, CNA_CURVE_CONTINUITY_STEP,
                                        &key) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_curve_key_collection_add(keys, key) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_curve_key_collection_destroy(keys) == CNA_RESULT_SUCCESS);
    }

    uint64_t produced = 0U;
    {
        const CNA_Result encoding = cna_cnb_encode_curve(
            curve, view("Curves/Fade"), g_encoded, sizeof(g_encoded), &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_curve"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("curve"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_CURVE);

    CNA_CurveHandle decoded = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_decode_curve(document, &decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(decoded != CNA_INVALID_HANDLE);

    /* Read it back through the curve family's own routes: the decoded handle is an ordinary curve,
       not a second kind of object that merely looks like one. */
    {
        CNA_CurveLoopType loop = UINT32_MAX;
        REQUIRE(cna_curve_get_pre_loop(decoded, &loop) == CNA_RESULT_SUCCESS);
        REQUIRE(loop == CNA_CURVE_LOOP_OSCILLATE);
        REQUIRE(cna_curve_get_post_loop(decoded, &loop) == CNA_RESULT_SUCCESS);
        REQUIRE(loop == CNA_CURVE_LOOP_CYCLE_OFFSET);
        CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
        REQUIRE(cna_curve_get_keys(decoded, &keys) == CNA_RESULT_SUCCESS);
        uint64_t count = 0U;
        REQUIRE(cna_curve_key_collection_get_count(keys, &count) == CNA_RESULT_SUCCESS);
        REQUIRE(count == 2U);
        CNA_CurveKey key;
        memset(&key, 0, sizeof(key));
        REQUIRE(cna_curve_key_collection_get(keys, 1U, &key) == CNA_RESULT_SUCCESS);
        REQUIRE(near_enough(key.position, 2.0f) && near_enough(key.value, -3.0f));
        REQUIRE(near_enough(key.tangent_in, 1.5f) && near_enough(key.tangent_out, -1.5f));
        REQUIRE(key.continuity == CNA_CURVE_CONTINUITY_STEP);
        REQUIRE(cna_curve_key_collection_destroy(keys) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_curve_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_curve_destroy(curve) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_animation_clip(void)
{
    CNA_KeyframeEXT keys[3];
    memset(keys, 0, sizeof(keys));
    for (size_t i = 0U; i < 3U; ++i) {
        keys[i].time_seconds = (double)i * 0.5;
        keys[i].rotation.w = 1.0f;
        keys[i].scale.x = 1.0f;
        keys[i].scale.y = 1.0f;
        keys[i].scale.z = 1.0f;
        keys[i].translation.x = (float)i * 2.0f;
    }
    CNA_BoneTrackEXTDescriptor tracks[2];
    memset(tracks, 0, sizeof(tracks));
    tracks[0].bone_index = 3;
    tracks[0].keyframes = keys;
    tracks[0].keyframe_count = 3U;
    tracks[1].bone_index = 7;
    tracks[1].keyframes = keys;
    tracks[1].keyframe_count = 2U;
    CNA_AnimationClipEXTDescriptor clip;
    memset(&clip, 0, sizeof(clip));
    clip.duration_seconds = 1.0;
    clip.tracks = tracks;
    clip.track_count = 2U;

    uint64_t produced = 0U;
    {
        const CNA_Result encoding = cna_cnb_encode_animation_clip(
            &clip, CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT, view("Clips/Walk"), g_encoded,
            sizeof(g_encoded), &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_animation_clip"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("clip"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_ANIMATION_CLIP);

    CNA_CnbAnimationClipHandle decoded = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_decode_animation_clip(document, &decoded) == CNA_RESULT_SUCCESS);

    double duration = 0.0;
    uint64_t trackCount = 0U;
    CNA_ClipTargetSpaceEXT space = UINT32_MAX;
    REQUIRE(cna_cnb_animation_clip_get(decoded, &duration, &trackCount, &space) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(duration == 1.0 && trackCount == 2U);
    /* The default is the other value, so a dropped target space reads as a joint palette. */
    REQUIRE(space == CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT);

    {
        int32_t bone = 0;
        uint64_t keyframeCount = 0U;
        REQUIRE(cna_cnb_animation_clip_get_track(decoded, 0U, &bone, &keyframeCount) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(bone == 3 && keyframeCount == 3U);
        REQUIRE(cna_cnb_animation_clip_get_track(decoded, 1U, &bone, &keyframeCount) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(bone == 7 && keyframeCount == 2U);
        REQUIRE(cna_cnb_animation_clip_get_track(decoded, 2U, &bone, &keyframeCount) ==
                CNA_RESULT_INVALID_ARGUMENT);
        CNA_KeyframeEXT readKeys[4];
        uint64_t count = 0U;
        REQUIRE(cna_cnb_animation_clip_copy_keyframes(decoded, 0U, readKeys, 4U, &count) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(count == 3U);
        REQUIRE(readKeys[2].time_seconds == 1.0);
        REQUIRE(near_enough(readKeys[2].translation.x, 4.0f));
        REQUIRE(near_enough(readKeys[2].rotation.w, 1.0f));
        REQUIRE(cna_cnb_animation_clip_copy_keyframes(decoded, 0U, readKeys, 2U, &count) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(count == 3U);
    }

    /* An unknown target space and a non-finite time are the caller's mistake, not the file's. */
    REQUIRE(cna_cnb_encode_animation_clip(&clip, CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT + 1U, view(""),
                                          g_encoded, sizeof(g_encoded), &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_encode_animation_clip(NULL, CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT, view(""),
                                          g_encoded, sizeof(g_encoded), &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_cnb_animation_clip_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_animation_clip_destroy(decoded) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    return 1;
}

/*
 * There is exactly one keyframe encoding in CNB, shared by a model's embedded clips and by a
 * standalone clip. So it is written through the byte writer, measured against the published
 * stride, and read back through the cursor.
 */
static int validate_shared_keyframe(void)
{
    CNA_CnbByteWriterHandle writer = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_byte_writer_create(&writer) == CNA_RESULT_SUCCESS);

    CNA_KeyframeEXT key;
    memset(&key, 0, sizeof(key));
    key.time_seconds = 2.75;
    key.translation.x = 1.0f;
    key.translation.y = -2.0f;
    key.translation.z = 3.0f;
    key.rotation.x = 0.0f;
    key.rotation.y = 0.0f;
    key.rotation.z = 0.0f;
    key.rotation.w = 1.0f;
    key.scale.x = 2.0f;
    key.scale.y = 3.0f;
    key.scale.z = 4.0f;
    REQUIRE(cna_cnb_byte_writer_write_keyframe(writer, &key) == CNA_RESULT_SUCCESS);

    uint64_t size = 0U;
    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS);
    /* The published stride is not a transcribed constant: it is what the writer actually wrote. */
    REQUIRE(size == (uint64_t)CNA_CNB_ANIMATION_KEY_STRIDE);

    static uint8_t bytes[64];
    uint64_t produced = 0U;
    REQUIRE(cna_cnb_byte_writer_copy_bytes(writer, bytes, sizeof(bytes), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == size);

    CNA_CnbReaderHandle reader = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_reader_create(bytes, produced, view("keyframe"), NULL, &reader) ==
            CNA_RESULT_SUCCESS);
    CNA_KeyframeEXT readBack;
    memset(&readBack, 0, sizeof(readBack));
    REQUIRE(cna_cnb_reader_read_keyframe(reader, &readBack) == CNA_RESULT_SUCCESS);
    REQUIRE(readBack.time_seconds == 2.75);
    REQUIRE(near_enough(readBack.translation.y, -2.0f));
    REQUIRE(near_enough(readBack.rotation.w, 1.0f));
    REQUIRE(near_enough(readBack.scale.z, 4.0f));
    /* The cursor is now at the end, so a second read is a truncated read rather than junk. */
    REQUIRE(cna_cnb_reader_read_keyframe(reader, &readBack) == CNA_RESULT_IO);
    REQUIRE(cna_cnb_reader_destroy(reader) == CNA_RESULT_SUCCESS);

    /* A seconds value no TimeSpan can hold is a content failure naming the file, not a numeric
       exception from somewhere below. */
    {
        CNA_CnbByteWriterHandle bad = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_byte_writer_create(&bad) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_write_f64(bad, 1.0 / 0.0) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_copy_bytes(bad, bytes, sizeof(bytes), &produced) ==
                CNA_RESULT_SUCCESS);
        CNA_CnbReaderHandle badReader = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_reader_create(bytes, produced, view("seconds"), NULL, &badReader) ==
                CNA_RESULT_SUCCESS);
        double seconds = 0.0;
        REQUIRE(cna_cnb_reader_read_seconds(badReader, view("the clip duration"), &seconds) ==
                CNA_RESULT_IO);
        REQUIRE(cna_cnb_reader_destroy(badReader) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_destroy(bad) == CNA_RESULT_SUCCESS);
    }

    /* And a good one reads. */
    {
        CNA_CnbByteWriterHandle good = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_byte_writer_create(&good) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_write_f64(good, 12.5) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_copy_bytes(good, bytes, sizeof(bytes), &produced) ==
                CNA_RESULT_SUCCESS);
        CNA_CnbReaderHandle goodReader = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_reader_create(bytes, produced, view("seconds"), NULL, &goodReader) ==
                CNA_RESULT_SUCCESS);
        double seconds = 0.0;
        REQUIRE(cna_cnb_reader_read_seconds(goodReader, view(""), &seconds) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(seconds == 12.5);
        REQUIRE(cna_cnb_reader_destroy(goodReader) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_destroy(good) == CNA_RESULT_SUCCESS);
    }

    /* A time no TimeSpan can hold is refused on the way in too. */
    key.time_seconds = 1.0 / 0.0;
    REQUIRE(cna_cnb_byte_writer_write_keyframe(writer, &key) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_byte_writer_write_keyframe(writer, NULL) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_cnb_byte_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    return 1;
}

/* Each decoder refuses a document of the wrong asset type, leaving the output handle invalid. */
static int validate_wrong_asset_types(void)
{
    uint64_t produced = 0U;
    REQUIRE(cna_cnb_encode_song(view("Music/Theme.ogg"), view(""), 0U, view(""), g_encoded,
                                sizeof(g_encoded), &produced) == CNA_RESULT_SUCCESS);
    CNA_CnbDocumentHandle song = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_encoded, produced, view("song"), NULL, &song) ==
            CNA_RESULT_SUCCESS);

    CNA_CnbSpriteFontDataHandle font = UINT64_MAX;
    REQUIRE(cna_cnb_decode_sprite_font(song, &font) == CNA_RESULT_IO);
    REQUIRE(font == CNA_INVALID_HANDLE);
    CNA_CnbSoundEffectDataHandle sound = UINT64_MAX;
    REQUIRE(cna_cnb_decode_sound_effect(song, &sound) == CNA_RESULT_IO);
    REQUIRE(sound == CNA_INVALID_HANDLE);
    CNA_CurveHandle curve = UINT64_MAX;
    REQUIRE(cna_cnb_decode_curve(song, &curve) == CNA_RESULT_IO);
    REQUIRE(curve == CNA_INVALID_HANDLE);
    CNA_CnbAnimationClipHandle clip = UINT64_MAX;
    REQUIRE(cna_cnb_decode_animation_clip(song, &clip) == CNA_RESULT_IO);
    REQUIRE(clip == CNA_INVALID_HANDLE);
    CNA_CnbModelDataHandle model = UINT64_MAX;
    REQUIRE(cna_cnb_decode_model(song, &model) == CNA_RESULT_IO);
    REQUIRE(model == CNA_INVALID_HANDLE);

    /* An invalid handle is refused by every family in this slice. */
    {
        uint64_t bytes = 0U;
        CNA_CnbSpriteFontInfo info;
        memset(&info, 0, sizeof(info));
        info.struct_size = (uint32_t)sizeof(info);
        info.struct_version = CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION;
        REQUIRE(cna_cnb_sprite_font_data_get_info(CNA_INVALID_HANDLE, &info) ==
                CNA_RESULT_INVALID_HANDLE);
        REQUIRE(cna_cnb_sound_effect_data_copy_samples(CNA_INVALID_HANDLE, NULL, 0U, &bytes) ==
                CNA_RESULT_INVALID_HANDLE);
        REQUIRE(cna_cnb_animation_clip_get(CNA_INVALID_HANDLE, NULL, NULL, NULL) ==
                CNA_RESULT_INVALID_HANDLE);
        REQUIRE(cna_cnb_encode_curve(CNA_INVALID_HANDLE, view(""), NULL, 0U, &bytes) ==
                CNA_RESULT_INVALID_HANDLE);
        REQUIRE(cna_cnb_sprite_font_data_create(NULL) == CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_sound_effect_data_create(NULL, NULL, 0U, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_cnb_document_destroy(song) == CNA_RESULT_SUCCESS);
    return 1;
}

int main(void)
{
    if (!validate_identities()) { return 1; }
    if (!validate_sprite_font()) { return 2; }
    if (!validate_character_order()) { return 3; }
    if (!validate_sound_effect()) { return 4; }
    if (!validate_media()) { return 5; }
    if (!validate_curve()) { return 6; }
    if (!validate_animation_clip()) { return 7; }
    if (!validate_shared_keyframe()) { return 8; }
    if (!validate_wrong_asset_types()) { return 9; }
    return 0;
}
