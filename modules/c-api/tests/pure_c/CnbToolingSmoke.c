// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-111 -- the `.cnb` loader registry and the two compilation front ends.
 *
 * This is the one part of the CNB surface a C application uses as a **tool** rather than as a
 * runtime: it turns source files into `.cnb` bytes on a build machine, and it is the extension
 * point by which a game teaches CNA about an asset type CNA does not know. So the suite is built
 * around the three things that would be silently wrong otherwise.
 *
 *   - **The importers are measured, not merely called.** A 1x1 BMP is imported and its pixel
 *     compared; the same image is imported again *with* a colour key and the alpha checked, which
 *     is the only way to tell a colour key that was applied from one that was ignored. A DXT1 cube
 *     is imported and its six faces checked as `RGBA8`, because the contract is that the compiler
 *     decompresses rather than storing blocks a runtime could not upload.
 *   - **The compiler and the codec are checked against each other.** A `.cnj` curve is compiled,
 *     the produced bytes are parsed as a document, and the curve is decoded back out through
 *     `CBIND-110`'s route -- so the two halves agree rather than each agreeing with itself.
 *   - **The registry is driven end to end.** A custom type is minted from a name, a C loader is
 *     registered for it, a real `.cnb` carrying that identifier and that canonical name is built,
 *     resolved and invoked, and the pointer the callback produced comes back out. Then the
 *     identity rule is broken deliberately: a file whose number matches but whose name does not is
 *     refused, which is the whole reason a 31-bit hash needs a name beside it.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbToolingSmoke failure at line %d: %s\n", __LINE__, #condition); \
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

/* A 1x1 24-bit BMP whose single pixel is BGR 1E 14 0A, i.e. RGB (10, 20, 30). */
static const unsigned char FixtureBmp[58] = {
    0x42U, 0x4DU, 0x3AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x36U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x13U, 0x0BU,
    0x00U, 0x00U, 0x13U, 0x0BU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x14U,
    0x0AU, 0x00U
};

static void push_u16(uint8_t* const data, size_t* const offset, const uint32_t value)
{
    data[(*offset)++] = (uint8_t)(value & UINT32_C(0xff));
    data[(*offset)++] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
}

static void push_u32(uint8_t* const data, size_t* const offset, const uint32_t value)
{
    push_u16(data, offset, value & UINT32_C(0xffff));
    push_u16(data, offset, (value >> 16U) & UINT32_C(0xffff));
}

static void push_tag(uint8_t* const data, size_t* const offset, const char tag[4])
{
    for (size_t index = 0U; index < 4U; ++index) {
        data[(*offset)++] = (uint8_t)tag[index];
    }
}

static size_t build_minimal_cube_dds(uint8_t data[176])
{
    size_t offset = 0U;
    push_tag(data, &offset, "DDS ");
    push_u32(data, &offset, 124U);
    push_u32(data, &offset, UINT32_C(0x6));
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 1U);
    for (int index = 0; index < 11; ++index) { push_u32(data, &offset, 0U); }
    push_u32(data, &offset, 32U);
    push_u32(data, &offset, UINT32_C(0x4));
    push_tag(data, &offset, "DXT1");
    for (int index = 0; index < 5; ++index) { push_u32(data, &offset, 0U); }
    push_u32(data, &offset, UINT32_C(0x1000));
    push_u32(data, &offset, UINT32_C(0xfe00));
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    memset(data + offset, 0, 48U);
    return offset + 48U;
}

static size_t build_minimal_wav(uint8_t* const data, const uint16_t formatTag)
{
    size_t offset = 0U;
    push_tag(data, &offset, "RIFF");
    push_u32(data, &offset, 40U);
    push_tag(data, &offset, "WAVE");
    push_tag(data, &offset, "fmt ");
    push_u32(data, &offset, 16U);
    push_u16(data, &offset, formatTag);
    push_u16(data, &offset, 1U);
    push_u32(data, &offset, 8000U);
    push_u32(data, &offset, 16000U);
    push_u16(data, &offset, 2U);
    push_u16(data, &offset, 16U);
    push_tag(data, &offset, "data");
    push_u32(data, &offset, 4U);
    push_u16(data, &offset, UINT32_C(0x1234));
    push_u16(data, &offset, UINT32_C(0x5678));
    return offset;
}

static int write_file(const char* const path, const void* const bytes, const size_t byteCount)
{
    FILE* const file = fopen(path, "wb");
    if (file == 0) { return 0; }
    const int wrote = byteCount == 0U || fwrite(bytes, byteCount, 1U, file) == 1U;
    return fclose(file) == 0 && wrote;
}

static uint8_t g_bytes[262144];

/* An image is imported and its pixel measured, then imported again with a colour key and the
   alpha measured -- the only way to tell an applied key from an ignored one. */
static int validate_image_import(void)
{
    static const char path[] = "cna_c_api_cnb_tooling.bmp";
    REQUIRE(write_file(path, FixtureBmp, sizeof(FixtureBmp)));

    CNA_CnbTextureDataHandle texture = CNA_INVALID_HANDLE;
    {
        const CNA_Result importing =
            cna_cnb_import_image_as_texture2d(view(path), NULL, &texture);
        if (importing != CNA_RESULT_SUCCESS) {
            report_last_error("cna_cnb_import_image_as_texture2d");
        }
        REQUIRE(importing == CNA_RESULT_SUCCESS);
    }

    CNA_CnbTextureInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_texture_data_get_info(texture, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.width == 1U && info.height == 1U);
    REQUIRE(info.depth == 1U && info.face_count == 1U && info.mip_count == 1U);
    REQUIRE(info.representation_count == 1U);
    {
        CNA_CnbTextureFormat format = CNA_CNB_TEXTURE_FORMAT_UNKNOWN;
        REQUIRE(cna_cnb_texture_data_get_representation_format(texture, 0U, &format) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(format == CNA_CNB_TEXTURE_FORMAT_RGBA8);
    }

    uint8_t pixel[4];
    uint64_t produced = 0U;
    REQUIRE(cna_cnb_texture_data_copy_level(texture, 0U, 0U, pixel, sizeof(pixel), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 4U);
    REQUIRE(pixel[0] == 10U && pixel[1] == 20U && pixel[2] == 30U && pixel[3] == 255U);

    /* Now with the colour key: the RGB is kept and only the alpha goes to zero. */
    CNA_CnbImageImportOptions options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.struct_version = CNA_CNB_IMAGE_IMPORT_OPTIONS_STRUCT_VERSION;
    options.color_key[0] = 10U;
    options.color_key[1] = 20U;
    options.color_key[2] = 30U;
    options.has_color_key = CNA_TRUE;
    CNA_CnbTextureDataHandle keyed = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_import_image_as_texture2d(view(path), &options, &keyed) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_copy_level(keyed, 0U, 0U, pixel, sizeof(pixel), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(pixel[0] == 10U && pixel[1] == 20U && pixel[2] == 30U);
    REQUIRE(pixel[3] == 0U);

    /* A key that matches nothing changes nothing, which is what makes the check above meaningful. */
    options.color_key[0] = 200U;
    CNA_CnbTextureDataHandle unmatched = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_import_image_as_texture2d(view(path), &options, &unmatched) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_copy_level(unmatched, 0U, 0U, pixel, sizeof(pixel), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(pixel[3] == 255U);

    /* A structure version this build does not know, and a file that is not there. */
    {
        CNA_CnbImageImportOptions bad = options;
        bad.struct_version = CNA_CNB_IMAGE_IMPORT_OPTIONS_STRUCT_VERSION + 1U;
        CNA_CnbTextureDataHandle rejected = UINT64_MAX;
        REQUIRE(cna_cnb_import_image_as_texture2d(view(path), &bad, &rejected) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        bad = options;
        bad.has_color_key = (CNA_Bool)9;
        REQUIRE(cna_cnb_import_image_as_texture2d(view(path), &bad, &rejected) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_import_image_as_texture2d(
                    view("cna_c_api_cnb_tooling_missing.bmp"), NULL, &rejected) ==
                CNA_RESULT_IO);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_import_image_as_texture2d(view(path), NULL, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_cnb_texture_data_destroy(unmatched) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(keyed) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);
    (void)remove(path);
    return 1;
}

/* A DXT1 cube compiles to RGBA8, not to blocks: the contract is that the compiler decompresses. */
static int validate_dds_import(void)
{
    static const char path[] = "cna_c_api_cnb_tooling.dds";
    uint8_t dds[176];
    REQUIRE(build_minimal_cube_dds(dds) == sizeof(dds));

    CNA_CnbTextureDataHandle cube = CNA_INVALID_HANDLE;
    {
        const CNA_Result decoding =
            cna_cnb_decode_dds_as_texture_cube(dds, sizeof(dds), view("fixture.dds"), &cube);
        if (decoding != CNA_RESULT_SUCCESS) {
            report_last_error("cna_cnb_decode_dds_as_texture_cube");
        }
        REQUIRE(decoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbTextureInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_texture_data_get_info(cube, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.width == 4U && info.height == 4U && info.depth == 1U);
    REQUIRE(info.face_count == CNA_CNB_TEXTURE_CUBE_FACE_COUNT);
    {
        CNA_CnbTextureFormat format = CNA_CNB_TEXTURE_FORMAT_UNKNOWN;
        REQUIRE(cna_cnb_texture_data_get_representation_format(cube, 0U, &format) ==
                CNA_RESULT_SUCCESS);
        /* Not BC1: storing the blocks would produce a file this build could not upload. */
        REQUIRE(format == CNA_CNB_TEXTURE_FORMAT_RGBA8);
        REQUIRE(format != CNA_CNB_TEXTURE_FORMAT_BC1);
    }
    {
        /* Six faces, each a whole 4x4 RGBA8 level. */
        uint8_t level[64];
        uint64_t produced = 0U;
        for (uint64_t face = 0U; face < CNA_CNB_TEXTURE_CUBE_FACE_COUNT; ++face) {
            REQUIRE(cna_cnb_texture_data_copy_level(
                        cube, 0U, face, level, sizeof(level), &produced) == CNA_RESULT_SUCCESS);
            REQUIRE(produced == 64U);
        }
    }

    /* The same bytes through the file route. */
    REQUIRE(write_file(path, dds, sizeof(dds)));
    CNA_CnbTextureDataHandle fromFile = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_import_dds_as_texture_cube(view(path), &fromFile) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_get_info(fromFile, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.face_count == CNA_CNB_TEXTURE_CUBE_FACE_COUNT);

    {
        CNA_CnbTextureDataHandle rejected = UINT64_MAX;
        REQUIRE(cna_cnb_import_dds_as_texture_cube(
                    view("cna_c_api_cnb_tooling_missing.dds"), &rejected) == CNA_RESULT_IO);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        /* Truncated bytes are refused by the shared decoder, not read past. */
        REQUIRE(cna_cnb_decode_dds_as_texture_cube(dds, 8U, view("short"), &rejected) !=
                CNA_RESULT_SUCCESS);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_decode_dds_as_texture_cube(NULL, 4U, view(""), &rejected) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_cnb_texture_data_destroy(fromFile) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(cube) == CNA_RESULT_SUCCESS);
    (void)remove(path);
    return 1;
}

/* The WAV importer accepts exactly what converts to Pcm16 losslessly and refuses the rest by name. */
static int validate_wav_import(void)
{
    static const char path[] = "cna_c_api_cnb_tooling.wav";
    uint8_t wav[48];
    REQUIRE(build_minimal_wav(wav, 1U) == sizeof(wav));

    CNA_CnbSoundEffectDataHandle sound = CNA_INVALID_HANDLE;
    {
        const CNA_Result decoding =
            cna_cnb_decode_wav_as_sound_effect(wav, sizeof(wav), view("fixture.wav"), &sound);
        if (decoding != CNA_RESULT_SUCCESS) {
            report_last_error("cna_cnb_decode_wav_as_sound_effect");
        }
        REQUIRE(decoding == CNA_RESULT_SUCCESS);
    }

    CNA_CnbSoundEffectInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_SOUND_EFFECT_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_sound_effect_data_get_info(sound, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.format == CNA_CNB_AUDIO_FORMAT_PCM16);
    REQUIRE(info.sample_rate == 8000U && info.channels == 1U);
    REQUIRE(info.frame_count == 2U);
    REQUIRE(info.loop_start == 0U && info.loop_length == 0U);
    {
        uint8_t samples[8];
        uint64_t produced = 0U;
        REQUIRE(cna_cnb_sound_effect_data_copy_samples(
                    sound, samples, sizeof(samples), &produced) == CNA_RESULT_SUCCESS);
        REQUIRE(produced == 4U);
        REQUIRE(samples[0] == 0x34U && samples[1] == 0x12U);
        REQUIRE(samples[2] == 0x78U && samples[3] == 0x56U);
    }

    /* The whole point of the importer being narrow: an IEEE-float WAV is refused by name rather
       than converted, because that conversion is an authoring decision. */
    {
        uint8_t floatWav[48];
        REQUIRE(build_minimal_wav(floatWav, 3U) == sizeof(floatWav));
        CNA_CnbSoundEffectDataHandle rejected = UINT64_MAX;
        REQUIRE(cna_cnb_decode_wav_as_sound_effect(
                    floatWav, sizeof(floatWav), view("float.wav"), &rejected) == CNA_RESULT_IO);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
    }

    REQUIRE(write_file(path, wav, sizeof(wav)));
    CNA_CnbSoundEffectDataHandle fromFile = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_import_wav_as_sound_effect(view(path), &fromFile) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_sound_effect_data_get_info(fromFile, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.sample_rate == 8000U);
    {
        CNA_CnbSoundEffectDataHandle rejected = UINT64_MAX;
        REQUIRE(cna_cnb_import_wav_as_sound_effect(
                    view("cna_c_api_cnb_tooling_missing.wav"), &rejected) == CNA_RESULT_IO);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_decode_wav_as_sound_effect(wav, sizeof(wav), view(""), NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_cnb_sound_effect_data_destroy(fromFile) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_sound_effect_data_destroy(sound) == CNA_RESULT_SUCCESS);
    (void)remove(path);
    return 1;
}

/*
 * The compiler and the codec are checked against each other: the bytes the compiler produced are
 * parsed as a document and decoded back through CBIND-110's own route.
 */
static int validate_cnj_compile(void)
{
    static const char path[] = "cna_c_api_cnb_tooling_curve.cnj";
    static const char descriptor[] =
        "{\"cnjVersion\":1,\"type\":\"Curve\",\"preLoop\":\"Oscillate\","
        "\"postLoop\":\"Cycle\",\"keys\":["
        "{\"position\":0.0,\"value\":1.0,\"tangentIn\":0.0,\"tangentOut\":0.0,"
        "\"continuity\":\"Smooth\"},"
        "{\"position\":2.0,\"value\":-3.0,\"tangentIn\":0.0,\"tangentOut\":0.0,"
        "\"continuity\":\"Step\"}]}";
    REQUIRE(write_file(path, descriptor, sizeof(descriptor) - 1U));

    CNA_CnjToCnbResultHandle compiled = CNA_INVALID_HANDLE;
    {
        const CNA_Result compiling =
            cna_cnb_compile_cnj(view(path), view(""), view("Curves/Fade"), &compiled);
        if (compiling != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_compile_cnj"); }
        REQUIRE(compiling == CNA_RESULT_SUCCESS);
    }

    uint32_t assetTypeId = 0U;
    REQUIRE(cna_cnb_cnj_result_get_asset_type_id(compiled, &assetTypeId) == CNA_RESULT_SUCCESS);
    REQUIRE(assetTypeId == CNA_CNB_ASSET_TYPE_CURVE);

    char text[128];
    uint64_t bytes = 0U;
    REQUIRE(cna_cnb_cnj_result_get_asset_type_name_size(compiled, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes > 0U && bytes < sizeof(text));
    REQUIRE(cna_cnb_cnj_result_copy_asset_type_name(compiled, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    text[bytes] = '\0';
    REQUIRE(strstr(text, "Curve") != 0);

    /* The source document is always the first absorbed file, under its own name. */
    uint64_t absorbed = 0U;
    REQUIRE(cna_cnb_cnj_result_get_absorbed_file_count(compiled, &absorbed) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(absorbed >= 1U);
    REQUIRE(cna_cnb_cnj_result_get_absorbed_file_size(compiled, 0U, &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == (uint64_t)(sizeof(path) - 1U));
    REQUIRE(cna_cnb_cnj_result_copy_absorbed_file(compiled, 0U, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(memcmp(text, path, (size_t)bytes) == 0);
    /* A curve names nothing outside itself. */
    uint64_t external = UINT64_MAX;
    REQUIRE(cna_cnb_cnj_result_get_external_reference_count(compiled, &external) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(external == 0U);

    uint64_t produced = 0U;
    REQUIRE(cna_cnb_cnj_result_copy_bytes(compiled, NULL, 0U, &produced) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(produced > 0U && produced <= sizeof(g_bytes));
    REQUIRE(cna_cnb_cnj_result_copy_bytes(compiled, g_bytes, sizeof(g_bytes), &produced) ==
            CNA_RESULT_SUCCESS);

    /* The compiler's output is a real container the codec reads back. */
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_bytes, produced, view("compiled"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t parsedType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &parsedType) == CNA_RESULT_SUCCESS);
    REQUIRE(parsedType == assetTypeId);
    CNA_CurveHandle curve = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_decode_curve(document, &curve) == CNA_RESULT_SUCCESS);
    {
        CNA_CurveLoopType loop = UINT32_MAX;
        REQUIRE(cna_curve_get_pre_loop(curve, &loop) == CNA_RESULT_SUCCESS);
        REQUIRE(loop == CNA_CURVE_LOOP_OSCILLATE);
        CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
        REQUIRE(cna_curve_get_keys(curve, &keys) == CNA_RESULT_SUCCESS);
        uint64_t count = 0U;
        REQUIRE(cna_curve_key_collection_get_count(keys, &count) == CNA_RESULT_SUCCESS);
        REQUIRE(count == 2U);
        REQUIRE(cna_curve_key_collection_destroy(keys) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_curve_destroy(curve) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);

    /* Out-of-range indices and a missing document. */
    REQUIRE(cna_cnb_cnj_result_get_absorbed_file_size(compiled, absorbed, &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_cnj_result_get_external_reference_size(compiled, 0U, &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_cnj_result_copy_external_reference(compiled, 0U, text, sizeof(text), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT);
    {
        CNA_CnjToCnbResultHandle rejected = UINT64_MAX;
        REQUIRE(cna_cnb_compile_cnj(
                    view("cna_c_api_cnb_tooling_missing.cnj"), view(""), view(""), &rejected) ==
                CNA_RESULT_IO);
        REQUIRE(rejected == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_compile_cnj(view(path), view(""), view(""), NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_cnb_cnj_result_destroy(compiled) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_cnj_result_destroy(compiled) == CNA_RESULT_INVALID_HANDLE);
    (void)remove(path);
    return 1;
}

/* What the registered C loader saw, so the test can prove it was handed live handles. */
typedef struct LoaderWitness {
    int calls;
    int document_was_usable;
    int manager_was_usable;
    uint32_t seen_asset_type;
    char seen_asset_name[64];
    uint64_t seen_asset_name_length;
    int object;
} LoaderWitness;

static CNA_Result on_cnb_load(
    void* const context,
    const CNA_CnbDocumentHandle document,
    const CNA_Handle contentManager,
    const CNA_StringView assetName,
    void** const outObject)
{
    LoaderWitness* const witness = (LoaderWitness*)context;
    ++witness->calls;

    /* The borrowed document handle is live for the duration of this call. */
    uint32_t assetType = 0U;
    witness->document_was_usable =
        cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS;
    witness->seen_asset_type = assetType;

    /* So is the borrowed content-manager handle. */
    uint64_t rootBytes = 0U;
    witness->manager_was_usable =
        cna_content_manager_get_root_directory_size(contentManager, &rootBytes) ==
        CNA_RESULT_SUCCESS;

    witness->seen_asset_name_length = assetName.byte_length;
    if (assetName.byte_length < sizeof(witness->seen_asset_name)) {
        memcpy(witness->seen_asset_name, assetName.data, (size_t)assetName.byte_length);
        witness->seen_asset_name[assetName.byte_length] = '\0';
    }
    *outObject = &witness->object;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_cnb_load_failing(
    void* const context,
    const CNA_CnbDocumentHandle document,
    const CNA_Handle contentManager,
    const CNA_StringView assetName,
    void** const outObject)
{
    (void)context; (void)document; (void)contentManager; (void)assetName; (void)outObject;
    return CNA_RESULT_NOT_SUPPORTED;
}

/*
 * Builds a .cnb carrying one custom asset type id and one canonical name in CMET, and reports what
 * the writer said. The pair does not have to agree -- see the identity check in the registry case.
 */
static CNA_Result try_build_custom_file(
    const uint32_t assetTypeId,
    const char* const canonicalName,
    uint64_t* const outByteCount)
{
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    if (cna_cnb_writer_create(assetTypeId, 1U, &writer) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INTERNAL;
    }
    static const uint8_t payload[4] = {9U, 8U, 7U, 6U};
    CNA_CnbChunkId chunk = 0U;
    CNA_Result result = cna_cnb_writer_set_metadata(
        writer, view(canonicalName), view("Custom/Thing"));
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_cnb_make_chunk_id('G', 'A', 'M', 'E', &chunk);
    }
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_cnb_writer_add_chunk(
            writer, chunk, payload, sizeof(payload), CNA_CNB_CHUNK_FLAG_MANDATORY, 4U);
    }
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_cnb_writer_build(writer, g_bytes, sizeof(g_bytes), outByteCount);
    }
    (void)cna_cnb_writer_destroy(writer);
    return result;
}

static int build_custom_file(
    const uint32_t assetTypeId,
    const char* const canonicalName,
    uint64_t* const outByteCount)
{
    const CNA_Result building = try_build_custom_file(assetTypeId, canonicalName, outByteCount);
    if (building != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_writer_build"); }
    REQUIRE(building == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_loader_registry(const CNA_Handle contentManager)
{
    static const char typeName[] = "Contoso.Game.LevelScript";
    static const char otherName[] = "Contoso.Game.DialogueTree";

    REQUIRE(cna_cnb_loader_registry_clear() == CNA_RESULT_SUCCESS);

    /* A custom identifier is minted from the name, and the two must agree ever after. */
    uint32_t assetTypeId = 0U;
    REQUIRE(cna_cnb_asset_type_id_from_name(view(typeName), &assetTypeId) == CNA_RESULT_SUCCESS);
    {
        CNA_Bool custom = CNA_FALSE;
        REQUIRE(cna_cnb_is_custom_asset_type_id(assetTypeId, &custom) == CNA_RESULT_SUCCESS);
        REQUIRE(custom == CNA_TRUE);
    }

    CNA_Bool registered = CNA_TRUE;
    REQUIRE(cna_cnb_loader_registry_is_registered(assetTypeId, &registered) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(registered == CNA_FALSE);
    {
        /* Absence is an ordinary answer from find, not a refusal. */
        CNA_Bool found = CNA_TRUE;
        CNA_CnbLoaderHandle loader = UINT64_MAX;
        REQUIRE(cna_cnb_loader_registry_find(assetTypeId, &found, &loader) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(found == CNA_FALSE && loader == CNA_INVALID_HANDLE);
        uint64_t nameBytes = UINT64_MAX;
        REQUIRE(cna_cnb_loader_registry_get_registered_type_name_size(assetTypeId, &nameBytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(nameBytes == 0U);
        CNA_Bool removed = CNA_TRUE;
        REQUIRE(cna_cnb_loader_registry_remove(assetTypeId, &removed) == CNA_RESULT_SUCCESS);
        REQUIRE(removed == CNA_FALSE);
    }

    LoaderWitness witness;
    memset(&witness, 0, sizeof(witness));
    {
        const CNA_Result registering = cna_cnb_loader_registry_register(
            assetTypeId, view(typeName), on_cnb_load, &witness);
        if (registering != CNA_RESULT_SUCCESS) {
            report_last_error("cna_cnb_loader_registry_register");
        }
        REQUIRE(registering == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_cnb_loader_registry_is_registered(assetTypeId, &registered) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(registered == CNA_TRUE);
    {
        char name[64];
        uint64_t nameBytes = 0U;
        REQUIRE(cna_cnb_loader_registry_get_registered_type_name_size(assetTypeId, &nameBytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(nameBytes == (uint64_t)(sizeof(typeName) - 1U));
        REQUIRE(cna_cnb_loader_registry_copy_registered_type_name(
                    assetTypeId, name, sizeof(name), &nameBytes) == CNA_RESULT_SUCCESS);
        REQUIRE(memcmp(name, typeName, (size_t)nameBytes) == 0);
    }

    /* Registering the same identifier under the same name again is a no-op, not an error. */
    REQUIRE(cna_cnb_loader_registry_register(
                assetTypeId, view(typeName), on_cnb_load, &witness) == CNA_RESULT_SUCCESS);
    /* Under a different name it is refused: that is the hash-collision case, and letting the
       second registration win would load one game type's file with another's loader. */
    REQUIRE(cna_cnb_loader_registry_register(
                assetTypeId, view(otherName), on_cnb_load, &witness) !=
            CNA_RESULT_SUCCESS);
    /* A built-in identifier has no registration route at all. */
    REQUIRE(cna_cnb_loader_registry_register(
                CNA_CNB_ASSET_TYPE_CURVE, view("Microsoft.Xna.Framework.Curve"), on_cnb_load,
                &witness) == CNA_RESULT_INVALID_ARGUMENT);
    /* And a name that does not hash to the identifier is refused, so the two cannot drift. */
    REQUIRE(cna_cnb_loader_registry_register(
                assetTypeId + 1U, view(typeName), on_cnb_load, &witness) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_loader_registry_register(assetTypeId, view(typeName), NULL, &witness) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* A real file carrying that identifier and that canonical name resolves and loads. */
    uint64_t byteCount = 0U;
    if (!build_custom_file(assetTypeId, typeName, &byteCount)) { return 0; }
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(g_bytes, byteCount, view("custom"), NULL, &document) ==
            CNA_RESULT_SUCCESS);

    CNA_CnbLoaderHandle loader = CNA_INVALID_HANDLE;
    {
        const CNA_Result resolving =
            cna_cnb_loader_registry_resolve_for_document(document, &loader);
        if (resolving != CNA_RESULT_SUCCESS) {
            report_last_error("cna_cnb_loader_registry_resolve_for_document");
        }
        REQUIRE(resolving == CNA_RESULT_SUCCESS);
    }
    REQUIRE(loader != CNA_INVALID_HANDLE);

    void* object = NULL;
    {
        const CNA_Result invoking =
            cna_cnb_loader_invoke(loader, document, contentManager, view("Levels/One"), &object);
        if (invoking != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_loader_invoke"); }
        REQUIRE(invoking == CNA_RESULT_SUCCESS);
    }
    REQUIRE(object == &witness.object);
    REQUIRE(witness.calls == 1);
    /* Both borrowed handles were live inside the callback. */
    REQUIRE(witness.document_was_usable == 1);
    REQUIRE(witness.manager_was_usable == 1);
    REQUIRE(witness.seen_asset_type == assetTypeId);
    REQUIRE(witness.seen_asset_name_length == 10U);
    REQUIRE(strcmp(witness.seen_asset_name, "Levels/One") == 0);

    /* And they are dead again afterwards: the callback's handles were released before it
       returned, so nothing it could have cached still resolves. */
    REQUIRE(cna_cnb_loader_invoke(loader, document, contentManager, view(""), &object) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(witness.calls == 2);

    /* A loader that fails fails the load, carrying its own result into the message. */
    {
        static const char failingName[] = "Contoso.Game.BrokenThing";
        uint32_t failingId = 0U;
        REQUIRE(cna_cnb_asset_type_id_from_name(view(failingName), &failingId) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_loader_registry_register(
                    failingId, view(failingName), on_cnb_load_failing, NULL) ==
                CNA_RESULT_SUCCESS);
        uint64_t failingBytes = 0U;
        if (!build_custom_file(failingId, failingName, &failingBytes)) { return 0; }
        CNA_CnbDocumentHandle failingDocument = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_document_parse(
                    g_bytes, failingBytes, view("broken"), NULL, &failingDocument) ==
                CNA_RESULT_SUCCESS);
        CNA_CnbLoaderHandle failingLoader = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_loader_registry_resolve_for_document(failingDocument, &failingLoader) ==
                CNA_RESULT_SUCCESS);
        void* nothing = &witness;
        REQUIRE(cna_cnb_loader_invoke(
                    failingLoader, failingDocument, contentManager, view(""), &nothing) ==
                CNA_RESULT_IO);
        REQUIRE(nothing == NULL);
        REQUIRE(cna_cnb_loader_destroy(failingLoader) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_destroy(failingDocument) == CNA_RESULT_SUCCESS);
    }

    /*
     * The identity rule is the reason a 31-bit hash needs a name beside it: a file whose number
     * matches but whose canonical name does not is a *different* type that happens to collide, and
     * decoding it with this loader would misinterpret someone's content.
     *
     * **CNA's own writer will not produce such a file**, which is the stronger half of the same
     * rule and is what this checks: the identifier and the canonical name are compared when the
     * container is assembled, so the disagreement is refused a whole step before any loader sees
     * it. The load-time refusal therefore defends against files from somewhere else, and cannot be
     * reached from C without forging a container and its checksums by hand.
     */
    {
        uint64_t collidingBytes = 0U;
        REQUIRE(try_build_custom_file(assetTypeId, otherName, &collidingBytes) ==
                CNA_RESULT_IO);
    }

    /* find takes the number alone and performs no name check, which is why it is the wrong door
       for loading a file -- and it still hands back an invocable copy. */
    {
        CNA_Bool found = CNA_FALSE;
        CNA_CnbLoaderHandle byNumber = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_loader_registry_find(assetTypeId, &found, &byNumber) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(found == CNA_TRUE && byNumber != CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_loader_invoke(byNumber, document, contentManager, view(""), &object) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(witness.calls == 3);
        REQUIRE(cna_cnb_loader_destroy(byNumber) == CNA_RESULT_SUCCESS);
    }

    /* A built-in loader constructs a C++ object, and this route says so instead of handing back a
       pointer whose type nothing in C could name. */
    REQUIRE(cna_cnb_loader_registry_register_builtins() == CNA_RESULT_SUCCESS);
    {
        CNA_Bool found = CNA_FALSE;
        CNA_CnbLoaderHandle builtIn = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_loader_registry_find(CNA_CNB_ASSET_TYPE_CURVE, &found, &builtIn) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(found == CNA_TRUE && builtIn != CNA_INVALID_HANDLE);
        void* wrong = &witness;
        REQUIRE(cna_cnb_loader_invoke(builtIn, document, contentManager, view(""), &wrong) !=
                CNA_RESULT_SUCCESS);
        REQUIRE(wrong == NULL);
        REQUIRE(cna_cnb_loader_destroy(builtIn) == CNA_RESULT_SUCCESS);
    }

    /* Invoke requires a manager: the canonical signature takes one by reference. */
    REQUIRE(cna_cnb_loader_invoke(loader, document, CNA_INVALID_HANDLE, view(""), &object) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_loader_invoke(loader, document, contentManager, view(""), NULL) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* Withdrawing it makes every lookup answer as it did before the registration. */
    {
        CNA_Bool removed = CNA_FALSE;
        REQUIRE(cna_cnb_loader_registry_remove(assetTypeId, &removed) == CNA_RESULT_SUCCESS);
        REQUIRE(removed == CNA_TRUE);
        REQUIRE(cna_cnb_loader_registry_is_registered(assetTypeId, &registered) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(registered == CNA_FALSE);
        CNA_CnbLoaderHandle gone = UINT64_MAX;
        REQUIRE(cna_cnb_loader_registry_resolve_for_document(document, &gone) == CNA_RESULT_IO);
        REQUIRE(gone == CNA_INVALID_HANDLE);
    }
    /* The resolved copy still works: it is a value, not a cursor into the table. */
    REQUIRE(cna_cnb_loader_invoke(loader, document, contentManager, view(""), &object) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(witness.calls == 4);

    REQUIRE(cna_cnb_loader_destroy(loader) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_loader_destroy(loader) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_loader_registry_clear() == CNA_RESULT_SUCCESS);
    return 1;
}

int main(void)
{
    if (!validate_image_import()) { return 1; }
    if (!validate_dds_import()) { return 2; }
    if (!validate_wav_import()) { return 3; }
    if (!validate_cnj_compile()) { return 4; }

    /* The registry needs a content manager, because a loader is handed one to resolve the file's
       external references through. A caller-created device supplies it without a game. */
    CNA_PresentationParameters parameters;
    if (cna_presentation_parameters_init(&parameters) != CNA_RESULT_SUCCESS) { return 5; }
    CNA_Handle device = CNA_INVALID_HANDLE;
    if (cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &device) !=
        CNA_RESULT_SUCCESS) {
        fprintf(stderr, "CnbToolingSmoke: no caller-created graphics device\n");
        return 5;
    }
    CNA_ContentManagerCreateInfo createInfo;
    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.struct_size = (uint32_t)sizeof(createInfo);
    createInfo.struct_version = UINT32_C(1);
    createInfo.root_directory = view(".");
    CNA_Handle manager = CNA_INVALID_HANDLE;
    if (cna_content_manager_create_resource(device, &createInfo, &manager) !=
        CNA_RESULT_SUCCESS) {
        report_last_error("cna_content_manager_create_resource");
        return 6;
    }

    const int registryOk = validate_loader_registry(manager);
    const int destroyed =
        cna_content_manager_destroy(manager) == CNA_RESULT_SUCCESS &&
        cna_graphics_device_destroy(device) == CNA_RESULT_SUCCESS;
    if (!registryOk) { return 7; }
    if (!destroyed) { return 8; }
    return 0;
}
