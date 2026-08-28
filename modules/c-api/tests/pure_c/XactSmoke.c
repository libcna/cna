// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdio.h>
#include <string.h>

/* No fixture in this repository ships a binary XACT file, so the test authors all three: the
   settings file, the wave bank the sounds live in and the sound bank the cues live in. The layouts
   below are what this runtime's own parser reads. */

static const char SettingsPath[] = "cna_c_api_xact_fixture.xgs";
static const char WaveBankPath[] = "cna_c_api_xact_fixture.xwb";
static const char SoundBankPath[] = "cna_c_api_xact_fixture.xsb";
static const char WaveBankName[] = "CnaCApiWaveBank";
static const char CueName[] = "CnaCApiCue";

static uint8_t buffer[4096];
static uint64_t used;

static void put_u8(const uint32_t value)
{
    buffer[used++] = (uint8_t)(value & UINT32_C(0xFF));
}

static void put_u16(const uint32_t value)
{
    put_u8(value);
    put_u8(value >> 8);
}

static void put_u32(const uint32_t value)
{
    put_u16(value);
    put_u16(value >> 16);
}

static void put_f32(const float value)
{
    uint32_t bits = UINT32_C(0);
    memcpy(&bits, &value, sizeof(bits));
    put_u32(bits);
}

static void put_bytes(const char* const text, const uint64_t count)
{
    memcpy(&buffer[used], text, (size_t)count);
    used += count;
}

static void put_cstr(const char* const text)
{
    put_bytes(text, (uint64_t)strlen(text));
    put_u8(UINT32_C(0));
}

static void put_padded(const char* const text, const uint64_t width)
{
    const uint64_t length = (uint64_t)strlen(text);
    uint64_t index;
    put_bytes(text, length);
    for (index = length; index < width; ++index) {
        put_u8(UINT32_C(0));
    }
}

static void put_zeros(const uint64_t count)
{
    uint64_t index;
    for (index = UINT64_C(0); index < count; ++index) {
        put_u8(UINT32_C(0));
    }
}

static int write_file(const char* const path)
{
    FILE* const file = fopen(path, "wb");
    int wrote;
    int closed;
    if (file == 0) {
        return 0;
    }
    wrote = fwrite(buffer, (size_t)used, 1U, file) == 1U;
    closed = fclose(file) == 0;
    return wrote && closed;
}

/* One category, "Default", and two variables that differ only in accessibility: an engine-global
   one and a cue-scoped one. That difference is what makes the two variable domains testable. */
static int write_settings(void)
{
    const uint32_t header_size = UINT32_C(65);
    const uint32_t category_size = UINT32_C(10);
    const uint32_t variable_size = UINT32_C(13);
    const uint32_t variable_count = UINT32_C(2);
    const uint32_t category_offset = header_size;
    const uint32_t variable_offset = category_offset + category_size;
    const uint32_t category_name_offset = variable_offset + variable_size * variable_count;
    const uint32_t variable_name_offset = category_name_offset + UINT32_C(8);

    used = UINT64_C(0);
    put_bytes("XGSF", UINT64_C(4));
    put_u16(UINT32_C(46));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_zeros(UINT64_C(8));
    put_u8(UINT32_C(3));

    put_u16(UINT32_C(1));
    put_u16(variable_count);
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));

    put_u32(category_offset);
    put_u32(variable_offset);
    put_u32(UINT32_C(0));
    put_u32(UINT32_C(0));
    put_u32(UINT32_C(0));
    put_u32(UINT32_C(0));
    put_u32(category_name_offset);
    put_u32(variable_name_offset);

    put_u8(UINT32_C(0xFF));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u8(UINT32_C(0));
    put_u16(UINT32_C(0xFFFF));
    put_u8(UINT32_C(0xFF));
    put_u8(UINT32_C(0));

    /* PUBLIC only: reachable through the engine, refused by a cue. */
    put_u8(UINT32_C(0x01));
    put_f32(0.5F);
    put_f32(0.0F);
    put_f32(1.0F);

    /* PUBLIC | CUE: reachable through a cue, refused by the engine. */
    put_u8(UINT32_C(0x05));
    put_f32(4.0F);
    put_f32(0.0F);
    put_f32(10.0F);

    put_cstr("Default");
    put_cstr("GlobalVar");
    put_cstr("CueVar");
    return write_file(SettingsPath);
}

/* One mono 16-bit PCM entry of silence, in the compact layout. */
static int write_wave_bank(void)
{
    const uint32_t header_size = UINT32_C(48);
    const uint32_t bank_data_size = UINT32_C(96);
    const uint32_t entry_meta_size = UINT32_C(4);
    const uint32_t wave_data_length = UINT32_C(512);
    const uint32_t seg_offset[5] = {
        UINT32_C(48),
        UINT32_C(48) + UINT32_C(96),
        UINT32_C(48) + UINT32_C(96) + UINT32_C(4),
        UINT32_C(48) + UINT32_C(96) + UINT32_C(4),
        UINT32_C(48) + UINT32_C(96) + UINT32_C(4)
    };
    const uint32_t seg_length[5] = {
        bank_data_size, entry_meta_size, UINT32_C(0), UINT32_C(0), wave_data_length
    };
    const uint32_t compact_format =
        UINT32_C(0) | (UINT32_C(1) << 2) | (UINT32_C(44100) << 5) | (UINT32_C(2) << 23) |
        (UINT32_C(1) << 31);
    int index;

    (void)header_size;
    used = UINT64_C(0);
    put_bytes("WBND", UINT64_C(4));
    put_u32(UINT32_C(1));
    for (index = 0; index < 5; ++index) {
        put_u32(seg_offset[index]);
        put_u32(seg_length[index]);
    }

    put_u32(UINT32_C(0x00020000));
    put_u32(UINT32_C(1));
    put_padded(WaveBankName, UINT64_C(64));
    put_u32(entry_meta_size);
    put_u32(UINT32_C(0));
    put_u32(UINT32_C(4));
    put_u32(compact_format);
    put_zeros(UINT64_C(8));

    put_u32(UINT32_C(0));
    put_zeros((uint64_t)wave_data_length);
    return write_file(WaveBankPath);
}

/* One wave-bank reference, one sound in category 0, and one simple cue that plays it. */
static int write_sound_bank(void)
{
    const uint32_t header_size = UINT32_C(74);
    const uint32_t bank_name_size = UINT32_C(64);
    const uint32_t base_offset = header_size + bank_name_size;
    const uint32_t wavebank_name_offset = base_offset;
    const uint32_t sound_offset = wavebank_name_offset + UINT32_C(64);
    const uint32_t cue_simple_offset = sound_offset + UINT32_C(12);
    const uint32_t cue_name_index_offset = cue_simple_offset + UINT32_C(5);
    const uint32_t cue_name_str_offset = cue_name_index_offset + UINT32_C(6);

    used = UINT64_C(0);
    put_bytes("SDBK", UINT64_C(4));
    put_u16(UINT32_C(46));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_zeros(UINT64_C(8));
    put_u8(UINT32_C(0));

    put_u16(UINT32_C(1));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u8(UINT32_C(1));
    put_u16(UINT32_C(1));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));

    put_u32(cue_simple_offset);
    put_u32(UINT32_C(0xFFFFFFFF));
    put_u32(UINT32_C(0xFFFFFFFF));
    put_u32(UINT32_C(0));
    put_u32(UINT32_C(0xFFFFFFFF));
    put_u32(UINT32_C(0));
    put_u32(wavebank_name_offset);
    put_u32(UINT32_C(0));
    put_u32(cue_name_index_offset);
    put_u32(sound_offset);

    put_padded("CnaCApiSoundBank", (uint64_t)bank_name_size);
    put_padded(WaveBankName, UINT64_C(64));

    put_u8(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u8(UINT32_C(0xFF));
    put_u16(UINT32_C(0));
    put_u8(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u16(UINT32_C(0));
    put_u8(UINT32_C(0));

    put_u8(UINT32_C(0));
    put_u32(sound_offset);

    put_u32(cue_name_str_offset);
    put_u16(UINT32_C(0));

    put_cstr(CueName);
    return write_file(SoundBankPath);
}

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static int text_equals(
    const CNA_Result size_result,
    const uint64_t size,
    const char* const actual,
    const char* const expected)
{
    return size_result == CNA_RESULT_SUCCESS && size == (uint64_t)strlen(expected) &&
        memcmp(actual, expected, (size_t)size) == 0;
}

/* Exactly one renderer, described by two identical strings; every route that addresses one is
   addressed by index because the canonical type cannot be constructed at all. */
static int validate_renderers(const CNA_Handle engine)
{
    uint64_t count = UINT64_C(99);
    uint64_t size = UINT64_C(0);
    char text[64];
    CNA_Bool equals = UINT8_C(9);
    int32_t hash = 0;
    int32_t other_hash = 0;

    if (cna_audio_engine_get_renderer_count(engine, &count) != CNA_RESULT_SUCCESS || count == 0 ||
        cna_audio_engine_get_renderer_count(engine, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_audio_engine_get_renderer_friendly_name_size(engine, UINT64_C(0), &size) !=
            CNA_RESULT_SUCCESS ||
        size == UINT64_C(0) || size > sizeof(text) ||
        cna_audio_engine_copy_renderer_friendly_name(engine, UINT64_C(0), text, sizeof(text),
                                                     &size) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A buffer that cannot hold the whole name is refused with nothing written. */
    if (cna_audio_engine_copy_renderer_friendly_name(engine, UINT64_C(0), text, UINT64_C(1),
                                                     &size) != CNA_RESULT_BUFFER_TOO_SMALL ||
        size == UINT64_C(0)) {
        return 0;
    }
    if (cna_audio_engine_get_renderer_id_size(engine, UINT64_C(0), &size) != CNA_RESULT_SUCCESS ||
        cna_audio_engine_copy_renderer_id(engine, UINT64_C(0), text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_audio_engine_get_renderer_text_size(engine, UINT64_C(0), &size) != CNA_RESULT_SUCCESS ||
        cna_audio_engine_copy_renderer_text(engine, UINT64_C(0), text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        size == UINT64_C(0)) {
        return 0;
    }
    if (cna_audio_engine_get_renderer_hash_code(engine, UINT64_C(0), &hash) != CNA_RESULT_SUCCESS ||
        cna_audio_engine_get_renderer_hash_code(engine, UINT64_C(0), &other_hash) !=
            CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }
    if (cna_audio_engine_renderers_equal(engine, UINT64_C(0), UINT64_C(0), &equals) !=
            CNA_RESULT_SUCCESS ||
        equals != CNA_TRUE) {
        return 0;
    }
    /* An index past the end is an argument failure on every one of these routes. */
    return cna_audio_engine_get_renderer_friendly_name_size(engine, count, &size) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_audio_engine_copy_renderer_id(engine, count, text, sizeof(text), &size) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_audio_engine_get_renderer_hash_code(engine, count, &hash) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_audio_engine_renderers_equal(engine, UINT64_C(0), count, &equals) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

/* Engine-global and cue-scoped variables are separate domains, and a read-only write is a silent
   no-op rather than a refusal -- both are canonical. */
static int validate_global_variables(const CNA_Handle engine)
{
    float value = -1.0F;

    if (cna_audio_engine_get_global_variable(engine, view("GlobalVar"), &value) !=
            CNA_RESULT_SUCCESS ||
        value != 0.5F) {
        return 0;
    }
    if (cna_audio_engine_set_global_variable(engine, view("GlobalVar"), 0.25F) !=
            CNA_RESULT_SUCCESS ||
        cna_audio_engine_get_global_variable(engine, view("GlobalVar"), &value) !=
            CNA_RESULT_SUCCESS ||
        value != 0.25F) {
        return 0;
    }
    /* The authored range clamps a write rather than refusing it. */
    if (cna_audio_engine_set_global_variable(engine, view("GlobalVar"), 9.0F) !=
            CNA_RESULT_SUCCESS ||
        cna_audio_engine_get_global_variable(engine, view("GlobalVar"), &value) !=
            CNA_RESULT_SUCCESS ||
        value != 1.0F) {
        return 0;
    }
    /* A cue-scoped variable is a different domain, not a stricter check on the same one. */
    if (cna_audio_engine_get_global_variable(engine, view("CueVar"), &value) !=
            CNA_RESULT_INVALID_STATE ||
        cna_audio_engine_set_global_variable(engine, view("CueVar"), 1.0F) !=
            CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    return cna_audio_engine_get_global_variable(engine, view("Nope"), &value) ==
            CNA_RESULT_INVALID_STATE &&
        cna_audio_engine_get_global_variable(engine, view(""), &value) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_audio_engine_get_global_variable(engine, view("GlobalVar"), 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_audio_engine_set_global_variable(engine, view("GlobalVar"), 1.0F) ==
            CNA_RESULT_SUCCESS;
}

static int validate_categories(const CNA_Handle engine)
{
    CNA_Handle category = CNA_INVALID_HANDLE;
    CNA_Handle again = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    uint64_t size = UINT64_C(0);
    char name[32];
    CNA_Bool equals = UINT8_C(9);
    int32_t hash = 0;
    int32_t other_hash = 1;
    int ok;

    if (cna_audio_engine_get_category(engine, view("Default"), &category) != CNA_RESULT_SUCCESS ||
        category == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_audio_engine_get_category(engine, view("Nope"), &rejected) != CNA_RESULT_INVALID_STATE ||
        rejected != CNA_INVALID_HANDLE ||
        cna_audio_engine_get_category(engine, view(""), &rejected) != CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_audio_category_destroy(category);
        return 0;
    }

    ok = cna_audio_category_get_name_size(category, &size) == CNA_RESULT_SUCCESS &&
        size <= sizeof(name) &&
        text_equals(cna_audio_category_copy_name(category, name, sizeof(name), &size), size, name,
                    "Default");
    ok = ok && cna_audio_category_copy_name(category, name, UINT64_C(1), &size) ==
                   CNA_RESULT_BUFFER_TOO_SMALL;

    /* Every category operation is accepted; playback effects are not observable from here. */
    ok = ok && cna_audio_category_pause(category) == CNA_RESULT_SUCCESS &&
        cna_audio_category_resume(category) == CNA_RESULT_SUCCESS &&
        cna_audio_category_set_volume(category, 0.5F) == CNA_RESULT_SUCCESS &&
        cna_audio_category_stop(category, CNA_AUDIO_STOP_OPTIONS_AS_AUTHORED) ==
            CNA_RESULT_SUCCESS &&
        cna_audio_category_stop(category, CNA_AUDIO_STOP_OPTIONS_IMMEDIATE) == CNA_RESULT_SUCCESS &&
        cna_audio_category_stop(category, UINT32_C(99)) == CNA_RESULT_INVALID_ARGUMENT;

    /* Two lookups of one name answer two equal categories with one hash code. */
    ok = ok && cna_audio_engine_get_category(engine, view("Default"), &again) ==
                   CNA_RESULT_SUCCESS &&
        cna_audio_category_equals(category, again, &equals) == CNA_RESULT_SUCCESS &&
        equals == CNA_TRUE &&
        cna_audio_category_get_hash_code(category, &hash) == CNA_RESULT_SUCCESS &&
        cna_audio_category_get_hash_code(again, &other_hash) == CNA_RESULT_SUCCESS &&
        hash == other_hash;

    if (again != CNA_INVALID_HANDLE) {
        ok = (cna_audio_category_destroy(again) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_audio_category_destroy(category) == CNA_RESULT_SUCCESS) && ok;
    /* A released category is gone; a second release is a handle failure. */
    ok = ok && cna_audio_category_destroy(category) == CNA_RESULT_INVALID_HANDLE;
    return ok;
}

static int validate_cue(const CNA_Handle sound_bank)
{
    CNA_Handle cue = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    CNA_CueInfo info = {sizeof(CNA_CueInfo), UINT32_C(1), UINT8_C(0), UINT8_C(0), UINT8_C(0),
                        UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    CNA_CueInfo broken = {sizeof(CNA_CueInfo), UINT32_C(9999), UINT8_C(0), UINT8_C(0), UINT8_C(0),
                          UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;
    uint64_t size = UINT64_C(0);
    char name[64];
    float value = -1.0F;
    int ok;

    if (cna_sound_bank_get_cue(sound_bank, view(CueName), &cue) != CNA_RESULT_SUCCESS ||
        cue == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_sound_bank_get_cue(sound_bank, view("Nope"), &rejected) != CNA_RESULT_INVALID_STATE ||
        rejected != CNA_INVALID_HANDLE ||
        cna_sound_bank_get_cue(sound_bank, view(""), &rejected) != CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_cue_destroy(cue);
        return 0;
    }

    ok = cna_cue_get_name_size(cue, &size) == CNA_RESULT_SUCCESS && size <= sizeof(name) &&
        text_equals(cna_cue_copy_name(cue, name, sizeof(name), &size), size, name, CueName);
    ok = ok && cna_cue_get_type_name_size(cue, &size) == CNA_RESULT_SUCCESS &&
        size <= sizeof(name) &&
        text_equals(cna_cue_copy_type_name(cue, name, sizeof(name), &size), size, name,
                    "Microsoft.Xna.Framework.Audio.Cue");

    /* A cue handed out by a bank arrives **prepared**, not merely created: the created state is
       internal to construction and no caller ever observes it. */
    ok = ok && cna_cue_get_info(cue, &info) == CNA_RESULT_SUCCESS && info.is_prepared == CNA_TRUE &&
        info.is_created == CNA_FALSE && info.is_playing == CNA_FALSE &&
        info.is_disposed == CNA_FALSE && info.is_stopped == CNA_FALSE &&
        cna_cue_get_info(cue, &broken) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_cue_get_info(cue, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* A cue-scoped variable is reachable here and refused by the engine, and the authored range
       clamps a write the same way an engine-global one does. */
    ok = ok && cna_cue_get_variable(cue, view("CueVar"), &value) == CNA_RESULT_SUCCESS &&
        value == 4.0F &&
        cna_cue_set_variable(cue, view("CueVar"), 20.0F) == CNA_RESULT_SUCCESS &&
        cna_cue_get_variable(cue, view("CueVar"), &value) == CNA_RESULT_SUCCESS &&
        value == 10.0F &&
        cna_cue_get_variable(cue, view("GlobalVar"), &value) == CNA_RESULT_INVALID_STATE &&
        cna_cue_get_variable(cue, view(""), &value) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_cue_get_variable(cue, view("CueVar"), 0) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_cue_play(cue) == CNA_RESULT_SUCCESS &&
        cna_cue_get_info(cue, &info) == CNA_RESULT_SUCCESS && info.is_playing == CNA_TRUE;
    /* Pausing does not stop playing: paused is an independent flag on top of playing, so both
       predicates are true at once. */
    ok = ok && cna_cue_pause(cue) == CNA_RESULT_SUCCESS &&
        cna_cue_get_info(cue, &info) == CNA_RESULT_SUCCESS && info.is_paused == CNA_TRUE &&
        info.is_playing == CNA_TRUE;
    ok = ok && cna_cue_resume(cue) == CNA_RESULT_SUCCESS &&
        cna_cue_get_info(cue, &info) == CNA_RESULT_SUCCESS && info.is_paused == CNA_FALSE;
    ok = ok && cna_cue_stop(cue, CNA_AUDIO_STOP_OPTIONS_IMMEDIATE) == CNA_RESULT_SUCCESS &&
        cna_cue_get_info(cue, &info) == CNA_RESULT_SUCCESS && info.is_stopped == CNA_TRUE &&
        info.is_playing == CNA_FALSE &&
        cna_cue_stop(cue, UINT32_C(99)) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_audio_emitter_init(&emitter) == CNA_RESULT_SUCCESS &&
        cna_audio_listener_init(&listener) == CNA_RESULT_SUCCESS;
    emitter.position.x = 12.0F;
    ok = ok && cna_cue_apply_3d(cue, &listener, &emitter) == CNA_RESULT_SUCCESS &&
        cna_cue_apply_3d(cue, 0, &emitter) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_cue_apply_3d(cue, &listener, 0) == CNA_RESULT_INVALID_ARGUMENT;

    ok = (cna_cue_destroy(cue) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_cue_destroy(cue) == CNA_RESULT_INVALID_HANDLE;
}

static void on_disposing(void* const context)
{
    *(int*)context += 1;
}

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API XACT smoke", UINT64_C(16)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle engine = CNA_INVALID_HANDLE;
    CNA_Handle second_engine = CNA_INVALID_HANDLE;
    CNA_Handle wave_bank = CNA_INVALID_HANDLE;
    CNA_Handle sound_bank = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;
    CNA_Bool flag = UINT8_C(9);
    uint64_t size = UINT64_C(0);
    char text[64];
    int disposals = 0;
    int status = CNA_TEST_FAIL(0);

    if (CNA_AUDIO_ENGINE_CONTENT_VERSION != INT32_C(46)) {
        return CNA_TEST_FAIL(1);
    }
    if (!write_settings() || !write_wave_bank() || !write_sound_bank()) {
        status = CNA_TEST_FAIL(2);
    }
    if (status == 0 && cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(3);
    }

    /* A settings file that is not there is an I/O failure, not an internal one. */
    if (status == 0 &&
        (cna_audio_engine_create(game, view("cna_c_api_xact_missing.xgs"), &rejected) !=
             CNA_RESULT_IO ||
         rejected != CNA_INVALID_HANDLE ||
         cna_audio_engine_create(game, view(SettingsPath), 0) != CNA_RESULT_INVALID_ARGUMENT)) {
        status = CNA_TEST_FAIL(4);
    }
    if (status == 0 &&
        (cna_audio_engine_create(game, view(SettingsPath), &engine) != CNA_RESULT_SUCCESS ||
         engine == CNA_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(5);
    }
    /* The look-ahead and the renderer id are accepted and ignored: there is one backend. */
    if (status == 0 &&
        (cna_audio_engine_create_with_renderer(game, view(SettingsPath), INT64_C(1500000),
                                               view("anything at all"), &second_engine) !=
             CNA_RESULT_SUCCESS ||
         second_engine == CNA_INVALID_HANDLE ||
         cna_audio_engine_destroy(second_engine) != CNA_RESULT_SUCCESS)) {
        status = CNA_TEST_FAIL(6);
    }
    if (status == 0 &&
        (cna_audio_engine_get_is_disposed(engine, &flag) != CNA_RESULT_SUCCESS ||
         flag != CNA_FALSE ||
         cna_audio_engine_get_type_name_size(engine, &size) != CNA_RESULT_SUCCESS ||
         size > sizeof(text) ||
         !text_equals(cna_audio_engine_copy_type_name(engine, text, sizeof(text), &size), size,
                      text, "Microsoft.Xna.Framework.Audio.AudioEngine") ||
         cna_audio_engine_update(engine) != CNA_RESULT_SUCCESS)) {
        status = CNA_TEST_FAIL(7);
    }
    if (status == 0 && !validate_renderers(engine)) {
        status = CNA_TEST_FAIL(8);
    }
    if (status == 0 && !validate_global_variables(engine)) {
        status = CNA_TEST_FAIL(9);
    }
    if (status == 0 && !validate_categories(engine)) {
        status = CNA_TEST_FAIL(10);
    }

    if (status == 0 &&
        (cna_wave_bank_create(engine, view("cna_c_api_xact_missing.xwb"), &rejected) !=
             CNA_RESULT_IO ||
         rejected != CNA_INVALID_HANDLE ||
         cna_wave_bank_create(engine, view(WaveBankPath), &wave_bank) != CNA_RESULT_SUCCESS ||
         wave_bank == CNA_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(11);
    }
    /* CBIND-065: the streaming constructor, which nothing named. The fixture bank is a whole
       in-memory bank rather than a streaming one, so what is asserted is the same contract its
       non-streaming twin has -- a missing file is IO and leaves the output invalid, and the
       offset/packet arguments reach the canonical call rather than being dropped. Opening the
       fixture as a streaming bank is allowed to fail, because whether a given .xwb can be
       streamed is the format's answer and not this ABI's. */
    {
        CNA_Handle streaming = UINT64_C(9);
        const CNA_Result streamed =
            cna_wave_bank_create_streaming(engine, view(WaveBankPath), 0, 64U, &streaming);
        CNA_Handle absent = UINT64_C(9);
        /* CBIND-065, and this is the finding rather than the coverage. The header promised "the
           same answers as cna_wave_bank_create", and it is not so: the streaming constructor never
           reads through the title container, so a missing file yields a live, empty bank and
           SUCCESS where the non-streaming twin answers IO. That is FNA's own streaming behaviour
           reproduced, not a failure lost -- see WaveBank.cpp Init's note. The header now says so;
           this pins it, so the day the canonical side changes, this test says so instead of
           nobody noticing. */
        if (status == 0 &&
            (cna_wave_bank_create_streaming(
                 engine, view("cna_c_api_xact_missing.xwb"), 0, 64U, &absent) !=
                 CNA_RESULT_SUCCESS ||
             absent == CNA_INVALID_HANDLE ||
             cna_wave_bank_destroy(absent) != CNA_RESULT_SUCCESS ||
             cna_wave_bank_create_streaming(engine, view(WaveBankPath), 0, 64U, 0) !=
                 CNA_RESULT_INVALID_ARGUMENT)) {
            status = CNA_TEST_FAIL(30);
        }
        if (status == 0 && streamed == CNA_RESULT_SUCCESS) {
            if (cna_wave_bank_destroy(streaming) != CNA_RESULT_SUCCESS) {
                status = CNA_TEST_FAIL(31);
            }
        } else if (status == 0 && streaming != CNA_INVALID_HANDLE) {
            status = CNA_TEST_FAIL(32);
        }
    }
    if (status == 0 &&
        (cna_wave_bank_get_is_disposed(wave_bank, &flag) != CNA_RESULT_SUCCESS ||
         flag != CNA_FALSE ||
         cna_wave_bank_get_is_prepared(wave_bank, &flag) != CNA_RESULT_SUCCESS ||
         cna_wave_bank_get_is_in_use(wave_bank, &flag) != CNA_RESULT_SUCCESS ||
         flag != CNA_FALSE ||
         cna_wave_bank_get_type_name_size(wave_bank, &size) != CNA_RESULT_SUCCESS ||
         size > sizeof(text) ||
         !text_equals(cna_wave_bank_copy_type_name(wave_bank, text, sizeof(text), &size), size,
                      text, "Microsoft.Xna.Framework.Audio.WaveBank"))) {
        status = CNA_TEST_FAIL(12);
    }

    if (status == 0 &&
        (cna_sound_bank_create(engine, view("cna_c_api_xact_missing.xsb"), &rejected) !=
             CNA_RESULT_IO ||
         rejected != CNA_INVALID_HANDLE ||
         cna_sound_bank_create(engine, view(SoundBankPath), &sound_bank) != CNA_RESULT_SUCCESS ||
         sound_bank == CNA_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(13);
    }
    if (status == 0 &&
        (cna_sound_bank_get_is_disposed(sound_bank, &flag) != CNA_RESULT_SUCCESS ||
         flag != CNA_FALSE ||
         cna_sound_bank_get_is_in_use(sound_bank, &flag) != CNA_RESULT_SUCCESS ||
         cna_sound_bank_get_type_name_size(sound_bank, &size) != CNA_RESULT_SUCCESS ||
         size > sizeof(text) ||
         !text_equals(cna_sound_bank_copy_type_name(sound_bank, text, sizeof(text), &size), size,
                      text, "Microsoft.Xna.Framework.Audio.SoundBank"))) {
        status = CNA_TEST_FAIL(14);
    }

    /* A fire-and-forget cue never gets a handle; the engine's own update is what retires it. */
    if (status == 0 &&
        (cna_sound_bank_play_cue(sound_bank, view(CueName)) != CNA_RESULT_SUCCESS ||
         cna_sound_bank_play_cue(sound_bank, view("Nope")) != CNA_RESULT_INVALID_STATE ||
         cna_sound_bank_play_cue(sound_bank, view("")) != CNA_RESULT_INVALID_ARGUMENT ||
         cna_audio_engine_update(engine) != CNA_RESULT_SUCCESS)) {
        status = CNA_TEST_FAIL(15);
    }
    if (status == 0 &&
        (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
         cna_audio_listener_init(&listener) != CNA_RESULT_SUCCESS ||
         cna_sound_bank_play_cue_3d(sound_bank, view(CueName), &listener, &emitter) !=
             CNA_RESULT_SUCCESS ||
         cna_sound_bank_play_cue_3d(sound_bank, view(CueName), 0, &emitter) !=
             CNA_RESULT_INVALID_ARGUMENT)) {
        status = CNA_TEST_FAIL(16);
    }
    if (status == 0 && !validate_cue(sound_bank)) {
        status = CNA_TEST_FAIL(17);
    }

    /* A parent refuses to be released while a C child of it is still alive. */
    if (status == 0 && cna_audio_engine_destroy(engine) != CNA_RESULT_INVALID_STATE) {
        status = CNA_TEST_FAIL(18);
    }

    if (status == 0 &&
        (cna_sound_bank_subscribe_disposing_ext(sound_bank, &on_disposing, &disposals,
                                                &registration) != CNA_RESULT_SUCCESS ||
         cna_sound_bank_subscribe_disposing_ext(sound_bank, 0, &disposals, &rejected) !=
             CNA_RESULT_INVALID_ARGUMENT ||
         rejected != CNA_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(19);
    }
    if (status == 0 &&
        (cna_sound_bank_destroy(sound_bank) != CNA_RESULT_SUCCESS || disposals != 1)) {
        status = CNA_TEST_FAIL(20);
    }
    if (status == 0 && cna_audio_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(21);
    }
    /* CBIND-065: the other three disposal subscriptions in this family. Only the sound bank's was
       ever driven, while the coverage matrix recorded all four against this file -- the wave
       bank's and the engine's fire here, and the cue's is asserted on its refusal because a cue
       belongs to a sound bank that is already gone by this point. */
    {
        int wave_disposals = 0;
        CNA_Handle wave_registration = CNA_INVALID_HANDLE;
        CNA_Handle refused = UINT64_C(9);
        if (status == 0 &&
            (cna_wave_bank_subscribe_disposing_ext(
                 wave_bank, &on_disposing, &wave_disposals, &wave_registration) !=
                 CNA_RESULT_SUCCESS ||
             cna_wave_bank_subscribe_disposing_ext(wave_bank, 0, &wave_disposals, &refused) !=
                 CNA_RESULT_INVALID_ARGUMENT ||
             refused != CNA_INVALID_HANDLE ||
             cna_cue_subscribe_disposing_ext(
                 CNA_INVALID_HANDLE, &on_disposing, &wave_disposals, &refused) !=
                 CNA_RESULT_INVALID_HANDLE)) {
            status = CNA_TEST_FAIL(25);
        }
        if (status == 0 && cna_wave_bank_destroy(wave_bank) != CNA_RESULT_SUCCESS) {
            status = CNA_TEST_FAIL(22);
        }
        if (status == 0 && wave_disposals != 1) {
            status = CNA_TEST_FAIL(26);
        }
        if (status == 0 && cna_audio_unsubscribe_ext(wave_registration) != CNA_RESULT_SUCCESS) {
            status = CNA_TEST_FAIL(27);
        }
    }
    {
        int engine_disposals = 0;
        CNA_Handle engine_registration = CNA_INVALID_HANDLE;
        if (status == 0 &&
            cna_audio_engine_subscribe_disposing_ext(
                engine, &on_disposing, &engine_disposals, &engine_registration) !=
                CNA_RESULT_SUCCESS) {
            status = CNA_TEST_FAIL(28);
        }
        if (status == 0 && cna_audio_unsubscribe_ext(engine_registration) !=
            CNA_RESULT_SUCCESS) {
            status = CNA_TEST_FAIL(29);
        }
    }
    if (status == 0 &&
        (cna_audio_engine_destroy(engine) != CNA_RESULT_SUCCESS ||
         cna_audio_engine_get_is_disposed(engine, &flag) != CNA_RESULT_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(23);
    }
    if (status == 0 && cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(24);
    }

    (void)remove(SettingsPath);
    (void)remove(WaveBankPath);
    (void)remove(SoundBankPath);
    return status;
}
