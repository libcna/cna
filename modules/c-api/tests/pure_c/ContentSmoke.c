// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

static const char FixturePath[] = "cna_c_api_content_\xC5\xBE_fixture.bmp";

typedef struct ContentState {
    CNA_Handle content_manager;
    CNA_Handle texture;
    int load_validated;
} ContentState;

typedef struct WrongThreadState {
    CNA_Handle content_manager;
    CNA_Result result;
} WrongThreadState;

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

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

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
    for (int index = 0; index < 11; ++index) {
        push_u32(data, &offset, 0U);
    }
    push_u32(data, &offset, 32U);
    push_u32(data, &offset, UINT32_C(0x4));
    push_tag(data, &offset, "DXT1");
    for (int index = 0; index < 5; ++index) {
        push_u32(data, &offset, 0U);
    }
    push_u32(data, &offset, UINT32_C(0x1000));
    push_u32(data, &offset, UINT32_C(0xfe00));
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    memset(data + offset, 0, 48U);
    return offset + 48U;
}

static size_t build_minimal_wav(uint8_t data[48])
{
    size_t offset = 0U;
    push_tag(data, &offset, "RIFF");
    push_u32(data, &offset, 40U);
    push_tag(data, &offset, "WAVE");
    push_tag(data, &offset, "fmt ");
    push_u32(data, &offset, 16U);
    push_u16(data, &offset, 1U);
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

static int write_fixture(void)
{
    FILE* const file = fopen(FixturePath, "wb");
    if (file == 0) {
        return 0;
    }
    const int wrote = fwrite(FixtureBmp, sizeof(FixtureBmp), 1U, file) == 1U;
    const int closed = fclose(file) == 0;
    const int ok = wrote && closed;
    if (!ok) {
        (void)remove(FixturePath);
    }
    return ok;
}

static int pixel_is_fixture_color(const CNA_Handle texture)
{
    CNA_Texture2DInfo info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    CNA_Color pixel = {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    uint64_t pixels = 0U;
    return cna_texture2d_get_info(texture, &info) == CNA_RESULT_SUCCESS &&
        info.width == 1U && info.height == 1U && info.level_count == 1U &&
        info.format == CNA_SURFACE_FORMAT_COLOR &&
        cna_texture2d_get_data_rgba8(texture, &pixel, 1U, &pixels) == CNA_RESULT_SUCCESS &&
        pixels == 1U && pixel.r == UINT8_C(10) && pixel.g == UINT8_C(20) &&
        pixel.b == UINT8_C(30) && pixel.a == UINT8_C(255);
}

static const char RootContainerName[] = "ContentRoot";
static const char RootSuffix[] = "/ContentRoot/AllPlayers";

typedef struct RootFixture {
    CNA_Handle device;
    CNA_Handle container;
    char path[512];
} RootFixture;

static int write_container_file(
    const CNA_Handle container,
    const char* const name,
    const uint8_t* const data,
    const size_t byte_count)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    if (cna_storage_container_create_file(container, view(name), &stream) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    const int wrote =
        cna_storage_stream_write(stream, data, (uint64_t)byte_count) == CNA_RESULT_SUCCESS;
    return cna_storage_stream_close(stream) == CNA_RESULT_SUCCESS && wrote;
}

/* The manifest scans a whole directory tree, so the suite builds a private one through the
   storage API instead of pointing the content root at the working directory. */
static int create_root_fixture(RootFixture* const fixture)
{
    uint8_t dds[176];
    uint8_t wav[48];
    uint64_t root_bytes = 0U;

    fixture->device = CNA_INVALID_HANDLE;
    fixture->container = CNA_INVALID_HANDLE;
    if (cna_storage_set_app_name_ext(view("cna-c-api-content-smoke")) != CNA_RESULT_SUCCESS ||
        cna_storage_device_show_selector(0, 0, &fixture->device) != CNA_RESULT_SUCCESS ||
        cna_storage_device_delete_container(fixture->device, view(RootContainerName)) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_open(
            fixture->device,
            view(RootContainerName),
            0,
            0,
            &fixture->container) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_storage_get_root_size_ext(&root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes + sizeof(RootSuffix) > sizeof(fixture->path) ||
        cna_storage_copy_root_ext(fixture->path, (uint64_t)sizeof(fixture->path), &root_bytes) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    memcpy(fixture->path + root_bytes, RootSuffix, sizeof(RootSuffix));

    if (build_minimal_cube_dds(dds) != sizeof(dds) || build_minimal_wav(wav) != sizeof(wav)) {
        return 0;
    }
    return write_container_file(
            fixture->container,
            "manifest_asset.bmp",
            FixtureBmp,
            sizeof(FixtureBmp)) &&
        write_container_file(fixture->container, "cube_asset.dds", dds, sizeof(dds)) &&
        write_container_file(fixture->container, "sound_asset.wav", wav, sizeof(wav));
}

static int destroy_root_fixture(const RootFixture* const fixture)
{
    return cna_storage_container_destroy(fixture->container) == CNA_RESULT_SUCCESS &&
        cna_storage_device_delete_container(fixture->device, view(RootContainerName)) ==
            CNA_RESULT_SUCCESS &&
        cna_storage_device_destroy(fixture->device) == CNA_RESULT_SUCCESS;
}

static int validate_paths_and_device(const CNA_Handle manager, const CNA_Handle graphics_device)
{
    char buffer[64];
    uint64_t bytes = 0U;
    CNA_Bool has_service_provider = CNA_TRUE;
    CNA_Handle device = CNA_INVALID_HANDLE;

    /* The root is "." here, so the joined path is exactly root + separator + asset name. */
    if (cna_content_manager_get_asset_path_size(manager, view("a.bmp"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(7)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_manager_copy_asset_path(
            manager,
            view("a.bmp"),
            buffer,
            (uint64_t)sizeof(buffer),
            &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(7) || buffer[0] != '.' || memcmp(buffer + 2, "a.bmp", 5U) != 0) {
        return 0;
    }
    if (cna_content_manager_copy_asset_path(manager, view("a.bmp"), buffer, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != UINT64_C(7)) {
        return 0;
    }
    /* An empty asset name resolves to the root itself. */
    if (cna_content_manager_get_asset_path_size(manager, (CNA_StringView){0, 0U}, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(1)) {
        return 0;
    }

    if (cna_content_manager_get_normalized_key_size(manager, view("SUB\\A.BMP"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(9)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_manager_copy_normalized_key(
            manager,
            view("SUB\\A.BMP"),
            buffer,
            (uint64_t)sizeof(buffer),
            &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(9) || strcmp(buffer, "sub/a.bmp") != 0) {
        return 0;
    }
    if (cna_content_manager_copy_normalized_key(manager, view("A"), buffer, UINT64_C(0), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != UINT64_C(1)) {
        return 0;
    }

    if (cna_content_manager_register_builtin_loaders(manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_register_builtin_loaders(CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    /* A service provider is a Sharp Runtime object, so a C-created manager never has one. */
    if (cna_content_manager_get_has_service_provider(manager, &has_service_provider) !=
            CNA_RESULT_SUCCESS ||
        has_service_provider != CNA_FALSE ||
        cna_content_manager_get_has_service_provider(manager, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_content_manager_get_graphics_device(manager, &device) != CNA_RESULT_SUCCESS ||
        device != graphics_device ||
        cna_content_manager_set_graphics_device(manager, CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_content_manager_set_graphics_device(manager, graphics_device) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    device = CNA_INVALID_HANDLE;
    return cna_content_manager_get_graphics_device(manager, &device) == CNA_RESULT_SUCCESS &&
        device == graphics_device;
}

static int validate_manifest(const CNA_Handle manager, const RootFixture* const fixture)
{
    CNA_ContentManifestEntryInfo entry = {
        sizeof(CNA_ContentManifestEntryInfo), UINT32_C(1), CNA_TRUE, CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U}, 0U, 0U
    };
    CNA_ContentReaderUsageInfo usage = {
        sizeof(CNA_ContentReaderUsageInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, 0U
    };
    char buffer[128];
    uint64_t count = 0U;
    uint64_t bytes = 0U;
    uint64_t index = 0U;
    int found = 0;

    if (cna_content_manager_set_root_directory(manager, view(fixture->path)) !=
            CNA_RESULT_SUCCESS ||
        cna_content_manager_refresh_content_manifest(manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_get_manifest_entry_count(manager, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(3)) {
        return 0;
    }
    /* The canonical manifest order is unspecified, so the fixture entry is located by name. */
    for (index = 0U; index < count; ++index) {
        memset(buffer, 0, sizeof(buffer));
        if (cna_content_manager_copy_manifest_relative_path(
                manager,
                index,
                buffer,
                (uint64_t)sizeof(buffer),
                &bytes) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (strcmp(buffer, "manifest_asset") == 0) {
            found = 1;
            break;
        }
    }
    if (!found || bytes != UINT64_C(14)) {
        return 0;
    }
    if (cna_content_manager_get_manifest_entry(manager, index, &entry) != CNA_RESULT_SUCCESS ||
        entry.has_xnb != CNA_FALSE || entry.has_cnj != CNA_FALSE ||
        entry.native_extension_count != UINT64_C(1) ||
        entry.xnb_reader_name_count != UINT64_C(0)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_manager_copy_manifest_native_extension(
            manager,
            index,
            UINT64_C(0),
            buffer,
            (uint64_t)sizeof(buffer),
            &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(4) || strcmp(buffer, ".bmp") != 0) {
        return 0;
    }
    if (cna_content_manager_copy_manifest_native_extension(
            manager, index, UINT64_C(1), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_copy_manifest_xnb_reader_name(
            manager, index, UINT64_C(0), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_get_manifest_entry(manager, count, &entry) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_copy_manifest_relative_path(
            manager, index, buffer, UINT64_C(1), &bytes) != CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }

    /* No .xnb file exists in the fixture root, so the reader-usage summary is empty. */
    if (cna_content_manager_get_xnb_reader_usage_count(manager, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(0) ||
        cna_content_manager_get_xnb_reader_usage(manager, UINT64_C(0), &usage) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_copy_xnb_reader_usage_name(
            manager, UINT64_C(0), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_typed_loads(const CNA_Handle manager)
{
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle cube = CNA_INVALID_HANDLE;
    CNA_Handle sound = CNA_INVALID_HANDLE;
    int64_t duration = INT64_C(0);

    /* Extension-less resolution goes through the registered reader's own extension list. */
    if (cna_content_manager_load_texture2d(manager, view("manifest_asset"), &texture) !=
            CNA_RESULT_SUCCESS ||
        !pixel_is_fixture_color(texture) || cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Cube storage is a backend capability, so both outcomes are correct; a missing asset is not. */
    const CNA_Result cube_result =
        cna_content_manager_load_texture_cube(manager, view("cube_asset"), &cube);
    if (cube_result == CNA_RESULT_SUCCESS) {
        if (cube == CNA_INVALID_HANDLE || cna_texturecube_destroy(cube) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    } else if (cube_result != CNA_RESULT_NOT_SUPPORTED && cube_result != CNA_RESULT_IO) {
        return 0;
    } else if (cube != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_content_manager_load_texture_cube(manager, view("manifest_asset"), &cube) !=
            CNA_RESULT_IO ||
        cube != CNA_INVALID_HANDLE ||
        cna_content_manager_load_texture_cube(manager, (CNA_StringView){0, 0U}, &cube) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    const CNA_Result sound_result =
        cna_content_manager_load_sound_effect(manager, view("sound_asset"), &sound);
    if (sound_result == CNA_RESULT_SUCCESS) {
        if (sound == CNA_INVALID_HANDLE ||
            cna_sound_effect_get_duration_ticks(sound, &duration) != CNA_RESULT_SUCCESS ||
            duration <= INT64_C(0) || cna_sound_effect_destroy(sound) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    } else if (sound_result != CNA_RESULT_NOT_SUPPORTED && sound_result != CNA_RESULT_IO) {
        return 0;
    } else if (sound != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The canonical SoundEffect loader reports a missing or unreadable audio file as unsupported
       rather than as an I/O failure, and the C route preserves that distinction. */
    return cna_content_manager_load_sound_effect(manager, view("manifest_asset"), &sound) ==
            CNA_RESULT_NOT_SUPPORTED &&
        sound == CNA_INVALID_HANDLE &&
        cna_content_manager_load_sound_effect(manager, view("manifest_asset"), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_resource_manager(const CNA_Handle graphics_device)
{
    const CNA_StringView root = {".", UINT64_C(1)};
    CNA_ContentManagerCreateInfo create_info = {
        sizeof(CNA_ContentManagerCreateInfo), UINT32_C(1), root, UINT64_C(0)
    };
    CNA_Handle manager = CNA_INVALID_HANDLE;
    CNA_Handle rejected = UINT64_C(5);
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Bool has_service_provider = CNA_TRUE;
    uint64_t bytes = 0U;

    create_info.reserved = UINT64_C(1);
    if (cna_content_manager_create_resource(graphics_device, &create_info, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    create_info.reserved = UINT64_C(0);
    if (cna_content_manager_create_resource(graphics_device, &create_info, &manager) !=
            CNA_RESULT_SUCCESS ||
        manager == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* A resource-backed manager answers every shared content-manager operation unchanged. */
    const int ok =
        cna_content_manager_get_root_directory_size(manager, &bytes) == CNA_RESULT_SUCCESS &&
        bytes == UINT64_C(1) &&
        cna_content_manager_get_has_service_provider(manager, &has_service_provider) ==
            CNA_RESULT_SUCCESS &&
        has_service_provider == CNA_FALSE &&
        cna_content_manager_get_graphics_device(manager, &device) == CNA_RESULT_SUCCESS &&
        device == graphics_device &&
        cna_content_manager_unload(manager) == CNA_RESULT_SUCCESS;
    return cna_content_manager_destroy(manager) == CNA_RESULT_SUCCESS && ok;
}

/* --------------------------------------------------------------------------------------------
   CBIND-055/CBIND-056: the two loaders this ABI grew, each proved on a fixture it writes itself.

   The font loader removes the need for a consumer to parse an .xnb or .cnj font container in
   order to obtain a font this ABI accepts; the foreign loader is the only route that reaches a
   caller-registered reader at all.
   -------------------------------------------------------------------------------------------- */

static const char FontAtlasPath[] = "cna_c_api_content_font_atlas.bmp";
static const char FontAssetName[] = "cna_c_api_content_font";
static const char FontDescriptorPath[] = "cna_c_api_content_font.cnj";
static const char ForeignAssetName[] = "cna_c_api_content_foreign";
static const char ForeignAssetPath[] = "cna_c_api_content_foreign.xnb";
static const char ForeignLoadReaderName[] = "CNA.Test.ContentSmoke.ForeignReader";
static const char ReflectiveAssetName[] = "cna_c_api_content_reflective";
static const char ReflectiveAssetPath[] = "cna_c_api_content_reflective.xnb";
static const char ReflectiveTypeName[] = "CNA.Test.ContentSmoke.Settings";
static const char ReflectiveEnumName[] = "CNA.Test.ContentSmoke.Mode";
static const char CnbAssetName[] = "cna_c_api_content_level";
static const char CnbAssetPath[] = "cna_c_api_content_level.cnb";
static const char CnbTypeName[] = "CNA.Test.ContentSmoke.Level";

static int write_text_file(const char* const path, const char* const text)
{
    FILE* const file = fopen(path, "wb");
    size_t length = 0U;
    if (file == 0) {
        return 0;
    }
    length = strlen(text);
    if (fwrite(text, 1U, length, file) != length) {
        (void)fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int write_binary_file(const char* const path, const uint8_t* const data, const size_t count)
{
    FILE* const file = fopen(path, "wb");
    if (file == 0) {
        return 0;
    }
    if (fwrite(data, 1U, count, file) != count) {
        (void)fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int write_font_fixture(void)
{
    /* The atlas is the same one-pixel bitmap the texture fixture uses; what the font needs from
       it is that it loads, not what it contains. */
    static const char descriptor[] =
        "{\"cnjVersion\":1,\"type\":\"SpriteFont\","
        "\"texture\":\"cna_c_api_content_font_atlas.bmp\","
        "\"lineSpacing\":10,\"spacing\":1.0,\"defaultCharacter\":\"?\","
        /* Strictly ascending by character: '?' (63) before 'A' (65). SpriteFont binary-searches
           its character map, so the .cnj reader refuses an unsorted one rather than letting a
           lookup return the wrong glyph -- this descriptor listed 'A' first and stopped loading
           at all when that rule landed. */
        "\"glyphs\":["
        "{\"char\":63,\"source\":[2,0,2,8],\"crop\":[0,0,2,8],\"kerning\":[1.0,4.0,2.0]},"
        "{\"char\":65,\"source\":[0,0,2,8],\"crop\":[0,0,2,8],\"kerning\":[0.0,5.0,0.0]}"
        "]}";
    return write_binary_file(FontAtlasPath, FixtureBmp, sizeof(FixtureBmp)) &&
        write_text_file(FontDescriptorPath, descriptor);
}

static int validate_font_load(const CNA_Handle manager)
{
    CNA_Handle font = UINT64_C(77);
    CNA_Handle atlas = UINT64_C(77);
    CNA_SpriteFontInfo info;
    CNA_SpriteFontGlyph glyphs[2];
    uint64_t glyph_count = 0U;
    CNA_Vector2 measured = {0.0f, 0.0f};

    /* Both outputs are required, and a refusal creates neither handle. */
    if (cna_content_manager_load_sprite_font(manager, view(FontAssetName), 0, &atlas) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_sprite_font(manager, view(FontAssetName), &font, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_sprite_font(
            manager, (CNA_StringView){0, 0U}, &font, &atlas) != CNA_RESULT_INVALID_ARGUMENT ||
        font != CNA_INVALID_HANDLE || atlas != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_content_manager_load_sprite_font(
            manager, view("cna_c_api_content_no_such_font"), &font, &atlas) != CNA_RESULT_IO ||
        font != CNA_INVALID_HANDLE || atlas != CNA_INVALID_HANDLE) {
        return 0;
    }

    if (cna_content_manager_load_sprite_font(manager, view(FontAssetName), &font, &atlas) !=
            CNA_RESULT_SUCCESS ||
        font == CNA_INVALID_HANDLE || atlas == CNA_INVALID_HANDLE) {
        return 0;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    if (cna_sprite_font_get_info(font, &info) != CNA_RESULT_SUCCESS ||
        info.character_count != UINT64_C(2) || info.line_spacing != 10 ||
        info.has_default_character != CNA_TRUE || info.default_character != (CNA_Char16)'?') {
        (void)cna_sprite_font_destroy(font);
        (void)cna_texture2d_destroy(atlas);
        return 0;
    }
    /* A loaded font is a font like any other: it measures, and its glyph table reads back. */
    memset(glyphs, 0, sizeof(glyphs));
    if (cna_sprite_font_copy_glyphs(font, glyphs, UINT64_C(2), &glyph_count) !=
            CNA_RESULT_SUCCESS ||
        glyph_count != UINT64_C(2) || glyphs[0].character != (CNA_Char16)'?' ||
        glyphs[0].glyph_bounds.width != 2 || glyphs[0].kerning.y != 4.0f ||
        glyphs[1].character != (CNA_Char16)'A' || glyphs[1].kerning.y != 5.0f ||
        cna_sprite_font_measure_utf8(font, view("A"), &measured) != CNA_RESULT_SUCCESS ||
        measured.x != 5.0f || measured.y != 10.0f) {
        (void)cna_sprite_font_destroy(font);
        (void)cna_texture2d_destroy(atlas);
        return 0;
    }
    /* The documented destroy order: the atlas is retained while the font lives. */
    if (cna_texture2d_destroy(atlas) != CNA_RESULT_INVALID_STATE ||
        cna_sprite_font_destroy(font) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(atlas) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

typedef struct ForeignLoadState {
    int create_calls;
    int read_calls;
    int destroy_calls;
    int payload;
} ForeignLoadState;

static CNA_Result foreign_load_create(void* const context, void** const out_reader_context)
{
    ForeignLoadState* const state = (ForeignLoadState*)context;
    ++state->create_calls;
    *out_reader_context = context;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result foreign_load_read(
    void* const reader_context,
    const CNA_ContentReaderHandle input,
    void* const existing_object,
    void** const out_object)
{
    ForeignLoadState* const state = (ForeignLoadState*)reader_context;
    uint64_t asset_bytes = 0U;
    (void)existing_object;
    ++state->read_calls;
    /* The borrowed handle names the asset CNA is loading, which is the reader's own context. */
    if (cna_content_reader_get_asset_name_size(input, &asset_bytes) != CNA_RESULT_SUCCESS ||
        asset_bytes == 0U) {
        return CNA_RESULT_INVALID_STATE;
    }
    *out_object = &state->payload;
    return CNA_RESULT_SUCCESS;
}

static void foreign_load_destroy(void* const reader_context)
{
    ++((ForeignLoadState*)reader_context)->destroy_calls;
}

static size_t push_seven_bit(uint8_t* const data, size_t offset, uint32_t value)
{
    while (value >= UINT32_C(0x80)) {
        data[offset++] = (uint8_t)((value & UINT32_C(0x7f)) | UINT32_C(0x80));
        value >>= 7U;
    }
    data[offset++] = (uint8_t)value;
    return offset;
}

static int write_foreign_asset(void)
{
    uint8_t asset[128];
    const size_t name_length = strlen(ForeignLoadReaderName);
    size_t offset = 10U; /* the header is back-filled once the total length is known */
    uint32_t total = 0U;

    offset = push_seven_bit(asset, offset, UINT32_C(1));            /* one type reader */
    offset = push_seven_bit(asset, offset, (uint32_t)name_length);  /* its name */
    memcpy(asset + offset, ForeignLoadReaderName, name_length);
    offset += name_length;
    asset[offset++] = 3U;                                           /* reader version 3, LE int32 */
    asset[offset++] = 0U;
    asset[offset++] = 0U;
    asset[offset++] = 0U;
    offset = push_seven_bit(asset, offset, UINT32_C(0));             /* no shared resources */
    offset = push_seven_bit(asset, offset, UINT32_C(1));             /* root object: reader 1 */

    total = (uint32_t)offset;
    asset[0] = (uint8_t)'X';
    asset[1] = (uint8_t)'N';
    asset[2] = (uint8_t)'B';
    asset[3] = (uint8_t)'w';   /* platform */
    asset[4] = 5U;             /* XNA 4.0 format version */
    asset[5] = 0U;             /* neither compressed nor HiDef */
    asset[6] = (uint8_t)(total & UINT32_C(0xff));
    asset[7] = (uint8_t)((total >> 8U) & UINT32_C(0xff));
    asset[8] = (uint8_t)((total >> 16U) & UINT32_C(0xff));
    asset[9] = (uint8_t)((total >> 24U) & UINT32_C(0xff));
    return write_binary_file(ForeignAssetPath, asset, offset);
}

/*
 * CBIND-105: a reflectively-serialized type declared from C.
 *
 * XNA compiles a type with no explicit writer through an implicit `ReflectiveReader<T>` that walks
 * the type's fields with .NET reflection. CNA has no runtime reflection, so the game supplies the
 * one thing reflection provided -- its own field list. The canonical builder captures a
 * pointer-to-member per field; C has no such thing, so a field is a **kind and a byte offset**,
 * which carries the same information.
 *
 * This fixture is a hand-written `.xnb` whose type-reader table names the reflective reader and the
 * enum reader, so the declaration is proved against a real file rather than against a mock.
 */
typedef struct ReflectiveSettings {
    int32_t count;
    float scale;
    int32_t mode;
    uint8_t flag;
    uint8_t padding[3];
    int32_t custom;
    int32_t created;
} ReflectiveSettings;

static ReflectiveSettings g_reflective_object;

static CNA_Result reflective_create(void* const context, void** const out_object)
{
    ReflectiveSettings* const settings = (ReflectiveSettings*)context;
    memset(settings, 0, sizeof(*settings));
    settings->created = 1;
    *out_object = settings;
    return CNA_RESULT_SUCCESS;
}

/* A member the wire format does not map onto an offset is read by the caller, positionally. */
static CNA_Result reflective_custom(
    void* const context,
    void* const object,
    const CNA_ContentReaderHandle input)
{
    uint8_t raw[4];
    uint64_t produced = 0U;
    ReflectiveSettings* const settings = (ReflectiveSettings*)object;
    (void)context;
    if (cna_content_reader_read_bytes_exact(
            input, 4, view("reflective-custom"), raw, sizeof(raw), &produced) !=
        CNA_RESULT_SUCCESS) {
        return CNA_RESULT_IO;
    }
    /* Stored doubled, so the test can tell a value this callback wrote from one CNA wrote. */
    settings->custom = 2 * (int32_t)((uint32_t)raw[0] | ((uint32_t)raw[1] << 8U) |
                                     ((uint32_t)raw[2] << 16U) | ((uint32_t)raw[3] << 24U));
    return CNA_RESULT_SUCCESS;
}

static size_t push_u32_le(uint8_t* const data, size_t offset, const uint32_t value)
{
    data[offset++] = (uint8_t)(value & UINT32_C(0xff));
    data[offset++] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    data[offset++] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    data[offset++] = (uint8_t)((value >> 24U) & UINT32_C(0xff));
    return offset;
}

static size_t push_reader_name(uint8_t* const data, size_t offset, const char* const name)
{
    const size_t length = strlen(name);
    offset = push_seven_bit(data, offset, (uint32_t)length);
    memcpy(data + offset, name, length);
    offset += length;
    return push_u32_le(data, offset, UINT32_C(0));   /* reader version 0 */
}

static int write_reflective_asset(void)
{
    uint8_t asset[512];
    char reflective_name[256];
    char enum_name[256];
    uint64_t reflective_bytes = 0U;
    uint64_t enum_bytes = 0U;
    size_t offset = 10U;
    uint32_t total = 0U;
    float scale = 2.5f;
    uint32_t scale_bits = 0U;

    /* The canonical names come from the ABI rather than being transcribed, which is what makes
       them worth publishing: they are the keys the reader table is looked up by. */
    if (cna_reflective_type_reader_copy_canonical_name(
            view(ReflectiveTypeName), reflective_name, sizeof(reflective_name),
            &reflective_bytes) != CNA_RESULT_SUCCESS ||
        cna_enum_type_reader_copy_canonical_name(
            view(ReflectiveEnumName), enum_name, sizeof(enum_name), &enum_bytes) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    reflective_name[reflective_bytes] = '\0';
    enum_name[enum_bytes] = '\0';

    offset = push_seven_bit(asset, offset, UINT32_C(2));   /* two type readers */
    offset = push_reader_name(asset, offset, reflective_name);
    offset = push_reader_name(asset, offset, enum_name);
    offset = push_seven_bit(asset, offset, UINT32_C(0));   /* no shared resources */
    offset = push_seven_bit(asset, offset, UINT32_C(1));   /* root object: reader 1 */

    /* The payload, in the order the fields are declared: every value type written inline. */
    offset = push_u32_le(asset, offset, UINT32_C(7));      /* count */
    memcpy(&scale_bits, &scale, sizeof(scale_bits));
    offset = push_u32_le(asset, offset, scale_bits);       /* scale */
    offset = push_u32_le(asset, offset, UINT32_C(2));      /* mode, an enum written inline */
    asset[offset++] = 1U;                                  /* flag */
    offset = push_u32_le(asset, offset, UINT32_C(21));     /* the custom member */

    total = (uint32_t)offset;
    asset[0] = (uint8_t)'X';
    asset[1] = (uint8_t)'N';
    asset[2] = (uint8_t)'B';
    asset[3] = (uint8_t)'w';
    asset[4] = 5U;
    asset[5] = 0U;
    (void)push_u32_le(asset, 6U, total);
    return write_binary_file(ReflectiveAssetPath, asset, offset);
}

static int validate_reflective_reader(const CNA_Handle manager)
{
    CNA_ReflectiveTypeReaderBuilderHandle builder = CNA_INVALID_HANDLE;
    void* object = 0;
    uint64_t bytes = 0U;
    char name[256];

    /* The canonical names are the reader table's keys, and both are published. */
    if (cna_reflective_type_reader_get_canonical_name_size(view("A.B"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_copy_canonical_name(
            view("A.B"), name, sizeof(name), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != strlen("Microsoft.Xna.Framework.Content.ReflectiveReader`1[[A.B]]") ||
        memcmp(name, "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[A.B]]",
               (size_t)bytes) != 0 ||
        cna_enum_type_reader_get_canonical_name_size(view("A.B"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != strlen("Microsoft.Xna.Framework.Content.EnumReader`1[[A.B]]") ||
        cna_enum_type_reader_copy_canonical_name(
            view("A.B"), name, sizeof(name), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != strlen("Microsoft.Xna.Framework.Content.EnumReader`1[[A.B]]") ||
        memcmp(name, "Microsoft.Xna.Framework.Content.EnumReader`1[[A.B]]",
               (size_t)bytes) != 0 ||
        /* A short destination writes nothing and still reports the size needed. */
        cna_reflective_type_reader_copy_canonical_name(view("A.B"), name, 4U, &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }

    if (!write_reflective_asset()) {
        return 0;
    }

    if (cna_reflective_type_reader_builder_create(
            view(ReflectiveTypeName), reflective_create, &g_reflective_object, &builder) !=
            CNA_RESULT_SUCCESS ||
        builder == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Declared in wire order -- which is not necessarily the C struct's field order, and is why
       the header says to check it against a decoded file. */
    if (cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_INT32, offsetof(ReflectiveSettings, count)) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_SINGLE, offsetof(ReflectiveSettings, scale)) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_add_enum_field(
            builder, offsetof(ReflectiveSettings, mode), view(ReflectiveEnumName)) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_BOOLEAN, offsetof(ReflectiveSettings, flag)) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_add_custom(builder, reflective_custom, 0) !=
            CNA_RESULT_SUCCESS) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }

    /* Refusals: an undefined kind, a null callback, and a null factory at creation. */
    {
        CNA_ReflectiveTypeReaderBuilderHandle rejected = UINT64_MAX;
        if (cna_reflective_type_reader_builder_add_field(
                builder, CNA_CONTENT_FIELD_MAXIMUM + 1U, 0U) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_reflective_type_reader_builder_add_custom(builder, 0, 0) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_reflective_type_reader_builder_add_enum_field(
                builder, 0U, (CNA_StringView){0, 0U}) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_reflective_type_reader_builder_create(
                view(ReflectiveTypeName), 0, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            rejected != CNA_INVALID_HANDLE ||
            cna_reflective_type_reader_builder_create(
                (CNA_StringView){0, 0U}, reflective_create, 0, &rejected) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            (void)cna_reflective_type_reader_builder_destroy(builder);
            return 0;
        }
    }

    if (cna_reflective_type_reader_builder_register(builder) != CNA_RESULT_SUCCESS ||
        /* Registering twice replaces the entry rather than failing, as the canonical does. */
        cna_reflective_type_reader_builder_register(builder) != CNA_RESULT_SUCCESS) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }

    /* The object comes out through the same foreign route a caller-supplied reader's does. */
    if (cna_content_manager_load_foreign_ext(manager, view(ReflectiveAssetName), &object) !=
            CNA_RESULT_SUCCESS ||
        object != &g_reflective_object) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }

    if (g_reflective_object.created != 1 ||
        g_reflective_object.count != 7 ||
        g_reflective_object.scale < 2.4f || g_reflective_object.scale > 2.6f ||
        g_reflective_object.mode != 2 ||
        g_reflective_object.flag != 1U ||
        /* Doubled by the callback, so this cannot be a value CNA stored inline. */
        g_reflective_object.custom != 42) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }

    /* Releasing the builder does not withdraw the registration it made. */
    if (cna_reflective_type_reader_builder_destroy(builder) != CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_destroy(builder) != CNA_RESULT_INVALID_HANDLE ||
        cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_INT32, 0U) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

/*
 * CBIND-105: `ContentManager::RegisterCnbLoaderEXT` is a static wrapper over the registry route
 * `CBIND-111` already bound -- it drops the asset name and boxes the result -- so it adds no second
 * C route. What it *does* claim is that a registered loader is reached by an ordinary load, and
 * `CBIND-111` could only say a C loader was invoked directly. This proves the manager route: a
 * `.cnb` on disk, a C loader registered for its type, and the object arriving through the same
 * foreign door a caller-supplied `.xnb` reader's object comes out of.
 */
static int g_cnb_level = 0;

static CNA_Result on_level_load(
    void* const context,
    const CNA_CnbDocumentHandle document,
    const CNA_Handle content_manager,
    const CNA_StringView asset_name,
    void** const out_object)
{
    uint32_t asset_type = 0U;
    (void)content_manager;
    (void)asset_name;
    if (cna_cnb_document_get_asset_type_id(document, &asset_type) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_IO;
    }
    *(int*)context = 1;
    *out_object = context;
    return CNA_RESULT_SUCCESS;
}

/*
 * CBIND-116: the Dictionary<string, object> a custom ContentProcessor writes.
 *
 * The shape XNA's own TrianglePickingSample uses -- a BoundingSphere and a Vector3[] of triangle
 * vertices, handed to the game as side data the stock pipeline has no type for. In C++ it arrives
 * on Model.Tag; this ABI has no route that loads a Model from content, so it is reached here as an
 * asset whose root object is the dictionary, which is the same reader producing the same map.
 *
 * The fixture is a hand-written .xnb so the entries are proved against a real file, and one of its
 * four entries is a type declared from C through the reflective builder -- which is what makes
 * cna_reflective_type_reader_builder_register_shared testable at all.
 */

static const char DictionaryAssetPath[] = "cna_c_api_content_dictionary.xnb";
static const char DictionaryAssetName[] = "cna_c_api_content_dictionary";
/* Two declared types, because one canonical name can only ever hold one reader: see
   compare_registration_shapes for the measurement that forced this shape on the fixture. */
static const char DictionarySharedTypeName[] = "CNA.Test.DictionaryEntry";
static const char DictionaryValueTypeName[] = "CNA.Test.DictionaryEntryValue";

typedef struct DictionaryCustom {
    int32_t magic;
    float weight;
} DictionaryCustom;

static DictionaryCustom g_dictionary_shared;
static DictionaryCustom g_dictionary_value;

static CNA_Result dictionary_shared_create(void* const context, void** const out_object)
{
    DictionaryCustom* const entry = (DictionaryCustom*)context;
    memset(entry, 0, sizeof(*entry));
    *out_object = entry;
    return CNA_RESULT_SUCCESS;
}

static size_t push_f32_le(uint8_t* const data, const size_t offset, const float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return push_u32_le(data, offset, bits);
}

static size_t push_utf8(uint8_t* const data, size_t offset, const char* const text)
{
    const size_t length = strlen(text);
    offset = push_seven_bit(data, offset, (uint32_t)length);
    memcpy(data + offset, text, length);
    return offset + length;
}

static int write_dictionary_asset(void)
{
    uint8_t asset[1024];
    char shared_name[256];
    char value_name[256];
    uint64_t shared_bytes = 0U;
    uint64_t value_bytes = 0U;
    size_t offset = 10U;
    uint32_t total = 0U;

    if (cna_reflective_type_reader_copy_canonical_name(
            view(DictionarySharedTypeName), shared_name, sizeof(shared_name) - 1U,
            &shared_bytes) != CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_copy_canonical_name(
            view(DictionaryValueTypeName), value_name, sizeof(value_name) - 1U,
            &value_bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    shared_name[shared_bytes] = '\0';
    value_name[value_bytes] = '\0';

    offset = push_seven_bit(asset, offset, UINT32_C(7));   /* seven type readers */
    offset = push_reader_name(
        asset, offset,
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]");
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.StringReader");
    offset = push_reader_name(
        asset, offset, "Microsoft.Xna.Framework.Content.BoundingSphereReader");
    offset = push_reader_name(
        asset, offset,
        "Microsoft.Xna.Framework.Content.ArrayReader`1[[Microsoft.Xna.Framework.Vector3]]");
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.Vector3Reader");
    offset = push_reader_name(asset, offset, shared_name);
    offset = push_reader_name(asset, offset, value_name);
    offset = push_seven_bit(asset, offset, UINT32_C(0));   /* no shared resources */
    offset = push_seven_bit(asset, offset, UINT32_C(1));   /* root object: the dictionary */

    offset = push_u32_le(asset, offset, UINT32_C(5));      /* five entries */

    offset = push_seven_bit(asset, offset, UINT32_C(2));   /* key: StringReader */
    offset = push_utf8(asset, offset, "BoundingSphere");
    offset = push_seven_bit(asset, offset, UINT32_C(3));   /* BoundingSphereReader */
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 2.0f);
    offset = push_f32_le(asset, offset, 3.0f);
    offset = push_f32_le(asset, offset, 4.0f);

    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "Custom");
    offset = push_seven_bit(asset, offset, UINT32_C(6));   /* reference-shaped, from C */
    offset = push_u32_le(asset, offset, UINT32_C(1234));
    offset = push_f32_le(asset, offset, 0.5f);

    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "CustomValue");
    offset = push_seven_bit(asset, offset, UINT32_C(7));   /* value-shaped, from C */
    offset = push_u32_le(asset, offset, UINT32_C(4321));
    offset = push_f32_le(asset, offset, 1.5f);

    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "Name");
    offset = push_seven_bit(asset, offset, UINT32_C(2));   /* StringReader */
    offset = push_utf8(asset, offset, "triangles");

    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "Vertices");
    offset = push_seven_bit(asset, offset, UINT32_C(4));   /* ArrayReader<Vector3> */
    offset = push_u32_le(asset, offset, UINT32_C(3));
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 0.0f);

    total = (uint32_t)offset;
    asset[0] = (uint8_t)'X';
    asset[1] = (uint8_t)'N';
    asset[2] = (uint8_t)'B';
    asset[3] = (uint8_t)'w';
    asset[4] = 5U;
    asset[5] = 0U;
    (void)push_u32_le(asset, 6U, total);
    return write_binary_file(DictionaryAssetPath, asset, offset);
}

/* Declares the two-field type and registers it in one of the two shapes. */
static int register_dictionary_reader(
    const char* const type_name,
    DictionaryCustom* const storage,
    const int shared)
{
    CNA_ReflectiveTypeReaderBuilderHandle builder = CNA_INVALID_HANDLE;
    CNA_Result registered = CNA_RESULT_INVALID_STATE;

    if (cna_reflective_type_reader_builder_create(
            view(type_name), dictionary_shared_create, storage, &builder) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_INT32, offsetof(DictionaryCustom, magic)) !=
            CNA_RESULT_SUCCESS ||
        cna_reflective_type_reader_builder_add_field(
            builder, CNA_CONTENT_FIELD_SINGLE, offsetof(DictionaryCustom, weight)) !=
            CNA_RESULT_SUCCESS) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }
    registered = shared
        ? cna_reflective_type_reader_builder_register_shared(builder)
        : cna_reflective_type_reader_builder_register(builder);
    if (registered != CNA_RESULT_SUCCESS) {
        (void)cna_reflective_type_reader_builder_destroy(builder);
        return 0;
    }
    /* A released builder does not withdraw the registration it made, and refuses further use. */
    return cna_reflective_type_reader_builder_destroy(builder) == CNA_RESULT_SUCCESS &&
        cna_reflective_type_reader_builder_register_shared(builder) == CNA_RESULT_INVALID_HANDLE;
}

/*
 * What the two registration routes actually change, measured rather than assumed.
 *
 * Both entries carry the same two fields and both come back as the caller's own object, so a C
 * caller reads either one the same way. What differs is the C++ type the entry holds --
 * `shared_ptr<System::Object>` for the reference-shaped registration, the value carrier for the
 * other -- and that is the difference this asserts, because it is the one this ABI can observe.
 *
 * The wire consequence the canonical layer warns about needs a container that dispatches on that
 * shape, and the one that does -- ModelReader's tag path, which takes a `shared_ptr<System::Object>`
 * and refuses anything else -- is not reachable from C, because no route loads a Model from
 * content. A dictionary value is read through type-erased dispatch that consumes the reader index
 * either way, which is why both are readable here and why this asserts an inequality of types
 * rather than a failure this ABI cannot produce.
 *
 * **A second registration under a name already in the table is ignored**, which is measured here
 * too: registering the value-shaped reader over the reference-shaped one leaves the first in place.
 */
static int compare_registration_shapes(const CNA_Handle manager)
{
    CNA_ObjectDictionaryHandle dictionary = CNA_INVALID_HANDLE;
    char shared_name[128];
    char value_name[128];
    uint64_t shared_bytes = 0U;
    uint64_t value_bytes = 0U;
    void* object = 0;

    /* The second registration for the shared name must not take: first registered wins. */
    if (!register_dictionary_reader(DictionarySharedTypeName, &g_dictionary_shared, 1) ||
        !register_dictionary_reader(DictionarySharedTypeName, &g_dictionary_shared, 0) ||
        !register_dictionary_reader(DictionaryValueTypeName, &g_dictionary_value, 0) ||
        !write_dictionary_asset() ||
        cna_content_manager_load_object_dictionary_ext(
            manager, view(DictionaryAssetName), &dictionary) != CNA_RESULT_SUCCESS) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    if (cna_object_dictionary_ext_copy_type_name(
            dictionary, view("Custom"), shared_name, sizeof(shared_name), &shared_bytes) !=
            CNA_RESULT_SUCCESS ||
        cna_object_dictionary_ext_copy_type_name(
            dictionary, view("CustomValue"), value_name, sizeof(value_name), &value_bytes) !=
            CNA_RESULT_SUCCESS ||
        /* Two shapes, two stored C++ types. */
        (shared_bytes == value_bytes &&
         memcmp(shared_name, value_name, (size_t)shared_bytes) == 0) ||
        /* Both are the caller's own object, with the fields the file declared. */
        cna_object_dictionary_ext_get_foreign_object(dictionary, view("Custom"), &object) !=
            CNA_RESULT_SUCCESS ||
        object != (void*)&g_dictionary_shared ||
        g_dictionary_shared.magic != 1234 || g_dictionary_shared.weight != 0.5f ||
        cna_object_dictionary_ext_get_foreign_object(dictionary, view("CustomValue"), &object) !=
            CNA_RESULT_SUCCESS ||
        object != (void*)&g_dictionary_value ||
        g_dictionary_value.magic != 4321 || g_dictionary_value.weight != 1.5f) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }
    return cna_object_dictionary_ext_destroy(dictionary) == CNA_RESULT_SUCCESS;
}

static int validate_object_dictionary(const CNA_Handle manager)
{
    CNA_ObjectDictionaryHandle dictionary = CNA_INVALID_HANDLE;
    CNA_ObjectDictionaryEntry entry;
    CNA_BoundingSphere sphere;
    CNA_Vector3 vertices[3];
    CNA_Bool present = CNA_FALSE;
    uint64_t count = 0U;
    uint64_t bytes = 0U;
    char text[64];
    void* custom = 0;

    if (!compare_registration_shapes(manager)) {
        return 0;
    }

    /* A missing asset is an IO failure, and the output handle stays invalid. */
    if (cna_content_manager_load_object_dictionary_ext(
            manager, view("cna_c_api_content_absent"), &dictionary) != CNA_RESULT_IO ||
        dictionary != CNA_INVALID_HANDLE ||
        cna_content_manager_load_object_dictionary_ext(manager, view(""), &dictionary) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_object_dictionary_ext(
            manager, view(DictionaryAssetName), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_content_manager_load_object_dictionary_ext(
            manager, view(DictionaryAssetName), &dictionary) != CNA_RESULT_SUCCESS ||
        dictionary == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Four entries, and the keys come back in the container's own order, which is sorted. */
    if (cna_object_dictionary_ext_get_count(dictionary, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(5) ||
        cna_object_dictionary_ext_contains_key(
            dictionary, view("BoundingSphere"), &present) != CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_object_dictionary_ext_contains_key(
            dictionary, view("NotThere"), &present) != CNA_RESULT_SUCCESS ||
        present != CNA_FALSE ||
        cna_object_dictionary_ext_get_key_size_at(dictionary, UINT64_C(0), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != strlen("BoundingSphere") ||
        cna_object_dictionary_ext_copy_key_at(
            dictionary, UINT64_C(1), text, sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != strlen("Custom") || memcmp(text, "Custom", (size_t)bytes) != 0 ||
        /* A short destination writes nothing and still reports the size needed. */
        cna_object_dictionary_ext_copy_key_at(dictionary, UINT64_C(4), text, 2U, &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != strlen("Vertices") ||
        cna_object_dictionary_ext_get_key_size_at(dictionary, UINT64_C(5), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    /* Each entry reports the type ITS OWN reader produced, which is the whole point. */
    entry.struct_size = (uint32_t)sizeof(entry);
    if (cna_object_dictionary_ext_get_entry(dictionary, view("BoundingSphere"), &entry) !=
            CNA_RESULT_SUCCESS ||
        entry.kind != CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE ||
        entry.is_array != CNA_FALSE || entry.element_count != UINT64_C(1)) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }
    entry.struct_size = (uint32_t)sizeof(entry);
    if (cna_object_dictionary_ext_get_entry(dictionary, view("Vertices"), &entry) !=
            CNA_RESULT_SUCCESS ||
        entry.kind != CNA_OBJECT_DICTIONARY_VALUE_VECTOR3 ||
        entry.is_array != CNA_TRUE || entry.element_count != UINT64_C(3)) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }
    entry.struct_size = (uint32_t)sizeof(entry);
    if (cna_object_dictionary_ext_get_entry(dictionary, view("Custom"), &entry) !=
            CNA_RESULT_SUCCESS ||
        entry.kind != CNA_OBJECT_DICTIONARY_VALUE_FOREIGN_OBJECT ||
        entry.is_array != CNA_FALSE ||
        /* An absent key and an undersized structure are both refused. */
        cna_object_dictionary_ext_get_entry(dictionary, view("NotThere"), &entry) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }
    {
        CNA_ObjectDictionaryEntry small;
        small.struct_size = 1U;
        if (cna_object_dictionary_ext_get_entry(dictionary, view("Name"), &small) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            (void)cna_object_dictionary_ext_destroy(dictionary);
            return 0;
        }
    }

    /* The values themselves. */
    if (cna_object_dictionary_ext_copy_value(
            dictionary, view("BoundingSphere"), CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE,
            &sphere, sizeof(sphere)) != CNA_RESULT_SUCCESS ||
        sphere.center.x != 1.0f || sphere.center.y != 2.0f || sphere.center.z != 3.0f ||
        sphere.radius != 4.0f ||
        /* Naming the wrong kind is the C form of an InvalidCastException, not a reinterpret. */
        cna_object_dictionary_ext_copy_value(
            dictionary, view("BoundingSphere"), CNA_OBJECT_DICTIONARY_VALUE_MATRIX,
            &sphere, sizeof(sphere)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_object_dictionary_ext_copy_value(
            dictionary, view("BoundingSphere"), CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE,
            &sphere, sizeof(sphere) - 1U) != CNA_RESULT_BUFFER_TOO_SMALL ||
        /* An array is not read through the scalar route, and an absent key is refused. */
        cna_object_dictionary_ext_copy_value(
            dictionary, view("Vertices"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR3,
            vertices, sizeof(vertices)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_object_dictionary_ext_copy_value(
            dictionary, view("NotThere"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR3,
            vertices, sizeof(vertices)) != CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    if (cna_object_dictionary_ext_copy_array(
            dictionary, view("Vertices"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR3,
            vertices, sizeof(vertices), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(vertices) ||
        vertices[0].x != 0.0f || vertices[0].y != 0.0f || vertices[0].z != 0.0f ||
        vertices[1].x != 1.0f || vertices[1].y != 0.0f || vertices[1].z != 0.0f ||
        vertices[2].x != 0.0f || vertices[2].y != 1.0f || vertices[2].z != 0.0f ||
        /* Sizing first is a supported call, not an error to avoid. */
        cna_object_dictionary_ext_copy_array(
            dictionary, view("Vertices"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR3, 0, 0U, &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != sizeof(vertices) ||
        cna_object_dictionary_ext_copy_array(
            dictionary, view("Vertices"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR4,
            vertices, sizeof(vertices), &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_object_dictionary_ext_copy_array(
            dictionary, view("BoundingSphere"), CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE,
            vertices, sizeof(vertices), &bytes) != CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    if (cna_object_dictionary_ext_get_string_size(dictionary, view("Name"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != strlen("triangles") ||
        cna_object_dictionary_ext_copy_string(
            dictionary, view("Name"), text, sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        memcmp(text, "triangles", (size_t)bytes) != 0 ||
        cna_object_dictionary_ext_copy_string(dictionary, view("Name"), text, 3U, &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        cna_object_dictionary_ext_get_string_size(dictionary, view("Vertices"), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    /* The entry the caller's own reflective reader produced: the pointer is the one its factory
       returned, and the fields it declared were filled from the file. */
    if (cna_object_dictionary_ext_get_foreign_object(dictionary, view("Custom"), &custom) !=
            CNA_RESULT_SUCCESS ||
        custom != (void*)&g_dictionary_shared ||
        g_dictionary_shared.magic != 1234 || g_dictionary_shared.weight != 0.5f ||
        cna_object_dictionary_ext_get_foreign_object(dictionary, view("Name"), &custom) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    /* The C++ type name is a diagnostic: it is present and non-empty, and its spelling is not
       asserted, because it is the toolchain's rather than this ABI's. */
    if (cna_object_dictionary_ext_get_type_name_size(dictionary, view("Vertices"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) ||
        cna_object_dictionary_ext_copy_type_name(
            dictionary, view("Vertices"), text, sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        cna_object_dictionary_ext_copy_type_name(
            dictionary, view("Vertices"), text, 1U, &bytes) != CNA_RESULT_BUFFER_TOO_SMALL) {
        (void)cna_object_dictionary_ext_destroy(dictionary);
        return 0;
    }

    /* A released handle is gone, and releasing it twice is refused rather than repeated. */
    if (cna_object_dictionary_ext_destroy(dictionary) != CNA_RESULT_SUCCESS ||
        cna_object_dictionary_ext_destroy(dictionary) != CNA_RESULT_INVALID_HANDLE ||
        cna_object_dictionary_ext_get_count(dictionary, &count) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

/*
 * CBIND-118: the model an XNA game loads, and the Tag its processor wrote.
 *
 * This is TrianglePickingSample's shape, which is the reason both routes exist: its content
 * processor tags every model with the model's world-space triangle vertices and a BoundingSphere,
 * and the game reads them back off Model.Tag to pick against real triangles rather than against a
 * bounding volume. In C# that is three steps -- load, cast the tag, index it. It is three steps
 * here too, and this proves each one against a hand-written .xnb.
 *
 * The model carries one bone and no meshes on purpose: what is under test is the load, the resource
 * graph it rebuilds and the tag, none of which needs a GPU-resident mesh to be wrong.
 */

static const char TaggedModelPath[] = "cna_c_api_content_tagged_model.xnb";
static const char TaggedModelName[] = "cna_c_api_content_tagged_model";
static const char ForeignModelPath[] = "cna_c_api_content_foreign_model.xnb";
static const char ForeignModelName[] = "cna_c_api_content_foreign_model";
static const char ForeignTagTypeName[] = "CNA.Test.ModelTagEntry";
/* A second name, because one canonical name holds one reader: the value-shaped arm needs
   its own entry in the table rather than a second registration under the first. */
static const char ValueTagTypeName[] = "CNA.Test.ModelTagEntryValue";
static const char ValueModelPath[] = "cna_c_api_content_value_model.xnb";
static const char ValueModelName[] = "cna_c_api_content_value_model";

static DictionaryCustom g_model_tag_object;
static DictionaryCustom g_model_tag_value_object;

/* Writes the model body every fixture here shares: one named root bone, no meshes. */
static size_t push_tagged_model_body(uint8_t* const asset, size_t offset)
{
    int index = 0;
    offset = push_u32_le(asset, offset, UINT32_C(1));   /* one bone */
    offset = push_seven_bit(asset, offset, UINT32_C(2));/* bone name: StringReader */
    offset = push_utf8(asset, offset, "Root");
    for (index = 0; index < 16; ++index) {             /* identity transform */
        offset = push_f32_le(asset, offset, (index % 5) == 0 ? 1.0f : 0.0f);
    }
    asset[offset++] = 0U;                              /* no parent */
    offset = push_u32_le(asset, offset, UINT32_C(0));  /* no children */
    offset = push_u32_le(asset, offset, UINT32_C(0));  /* no meshes */
    asset[offset++] = 1U;                              /* root bone reference */
    return offset;
}

static void finish_xnb(uint8_t* const asset, const size_t offset)
{
    asset[0] = (uint8_t)'X';
    asset[1] = (uint8_t)'N';
    asset[2] = (uint8_t)'B';
    asset[3] = (uint8_t)'w';
    asset[4] = 5U;
    asset[5] = 0U;
    (void)push_u32_le(asset, 6U, (uint32_t)offset);
}

static int write_tagged_model_asset(void)
{
    uint8_t asset[1024];
    size_t offset = 10U;

    offset = push_seven_bit(asset, offset, UINT32_C(6));
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.ModelReader");
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.StringReader");
    offset = push_reader_name(
        asset, offset,
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]");
    offset = push_reader_name(
        asset, offset,
        "Microsoft.Xna.Framework.Content.ArrayReader`1[[Microsoft.Xna.Framework.Vector3]]");
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.Vector3Reader");
    offset = push_reader_name(
        asset, offset, "Microsoft.Xna.Framework.Content.BoundingSphereReader");
    offset = push_seven_bit(asset, offset, UINT32_C(0));  /* no shared resources */
    offset = push_seven_bit(asset, offset, UINT32_C(1));  /* root object: ModelReader */

    offset = push_tagged_model_body(asset, offset);

    offset = push_seven_bit(asset, offset, UINT32_C(3));  /* Tag: the dictionary */
    offset = push_u32_le(asset, offset, UINT32_C(2));     /* two entries */
    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "BoundingSphere");
    offset = push_seven_bit(asset, offset, UINT32_C(6));
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 2.0f);
    offset = push_f32_le(asset, offset, 3.0f);
    offset = push_f32_le(asset, offset, 4.0f);
    offset = push_seven_bit(asset, offset, UINT32_C(2));
    offset = push_utf8(asset, offset, "Vertices");
    offset = push_seven_bit(asset, offset, UINT32_C(4));  /* ArrayReader<Vector3> */
    offset = push_u32_le(asset, offset, UINT32_C(3));
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 0.0f);
    offset = push_f32_le(asset, offset, 1.0f);
    offset = push_f32_le(asset, offset, 0.0f);

    finish_xnb(asset, offset);
    return write_binary_file(TaggedModelPath, asset, offset);
}

/* The same model, tagged with a type the caller declared from C instead. */
static int write_foreign_tagged_model_asset(
    const char* const path,
    const char* const type_name)
{
    uint8_t asset[1024];
    char reader_name[256];
    uint64_t reader_bytes = 0U;
    size_t offset = 10U;

    if (cna_reflective_type_reader_copy_canonical_name(
            view(type_name), reader_name, sizeof(reader_name) - 1U, &reader_bytes) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    reader_name[reader_bytes] = '\0';

    offset = push_seven_bit(asset, offset, UINT32_C(3));
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.ModelReader");
    offset = push_reader_name(asset, offset, "Microsoft.Xna.Framework.Content.StringReader");
    offset = push_reader_name(asset, offset, reader_name);
    offset = push_seven_bit(asset, offset, UINT32_C(0));
    offset = push_seven_bit(asset, offset, UINT32_C(1));

    offset = push_tagged_model_body(asset, offset);

    offset = push_seven_bit(asset, offset, UINT32_C(3));  /* Tag: the C-declared type */
    offset = push_u32_le(asset, offset, UINT32_C(7));     /* magic */
    offset = push_f32_le(asset, offset, 2.5f);            /* weight */

    finish_xnb(asset, offset);
    return write_binary_file(path, asset, offset);
}

/*
 * The one place the reference shape stops being cosmetic.
 *
 * A Dictionary<string, object> value reaches its reader through type-erased dispatch and reads
 * correctly whichever shape the reader produces, so CBIND-116 could only assert an inequality of
 * stored types. ModelReader's tag path is the container that actually dispatches on the shape: it
 * takes a reference and refuses anything else. With the model loader bound, that refusal is
 * reachable from C, and this is the measurement -- registered reference-shaped the asset loads and
 * the caller's object comes back; registered value-shaped the SAME asset fails to load.
 */
static int validate_foreign_model_tag(const CNA_Handle manager)
{
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_ObjectDictionaryHandle tag = UINT64_C(9);
    CNA_Bool present = CNA_FALSE;
    void* object = 0;

    if (!register_dictionary_reader(ForeignTagTypeName, &g_model_tag_object, 1) ||
        !register_dictionary_reader(ValueTagTypeName, &g_model_tag_value_object, 0) ||
        !write_foreign_tagged_model_asset(ForeignModelPath, ForeignTagTypeName) ||
        !write_foreign_tagged_model_asset(ValueModelPath, ValueTagTypeName)) {
        return 0;
    }
    /* The measurement CBIND-116 could not make. Same fixture, same fields, one registration each:
       the reference-shaped reader loads and the value-shaped one is refused by ModelReader's tag
       path, which takes a reference and accepts nothing else. */
    if (cna_content_manager_load_model(manager, view(ValueModelName), &model) != CNA_RESULT_IO ||
        model != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_content_manager_load_model(manager, view(ForeignModelName), &model) !=
            CNA_RESULT_SUCCESS ||
        cna_model_get_content_tag_foreign_object_ext(model, &present, &object) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        object != (void*)&g_model_tag_object ||
        g_model_tag_object.magic != 7 || g_model_tag_object.weight != 2.5f ||
        /* A tag of another shape is reported, not failed: the dictionary question simply gets
           "no" for a model whose tag is a caller-made object. */
        cna_model_get_content_tag_dictionary_ext(model, &present, &tag) != CNA_RESULT_SUCCESS ||
        present != CNA_FALSE || tag != CNA_INVALID_HANDLE) {
        (void)cna_model_destroy(model);
        return 0;
    }
    return cna_model_destroy(model) == CNA_RESULT_SUCCESS;
}

static int validate_content_model(const CNA_Handle manager)
{
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_ObjectDictionaryHandle tag = CNA_INVALID_HANDLE;
    CNA_ModelBoneCollectionHandle bones = CNA_INVALID_HANDLE;
    CNA_ModelMeshCollectionHandle meshes = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle root = CNA_INVALID_HANDLE;
    CNA_ObjectDictionaryEntry entry;
    CNA_BoundingSphere sphere;
    CNA_Vector3 vertices[3];
    CNA_Bool present = CNA_FALSE;
    uint64_t bytes = 0U;
    uint64_t count = 0U;

    if (!write_tagged_model_asset()) {
        return 0;
    }

    /* Refusals first, so a later success cannot be a coincidence. */
    if (cna_content_manager_load_model(
            manager, view("cna_c_api_content_absent_model"), &model) != CNA_RESULT_IO ||
        model != CNA_INVALID_HANDLE ||
        cna_content_manager_load_model(manager, view(""), &model) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_model(manager, view(TaggedModelName), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        /* An asset whose root object is not a Model reports the mismatch rather than faulting. */
        cna_content_manager_load_model(manager, view(DictionaryAssetName), &model) !=
            CNA_RESULT_IO) {
        return 0;
    }

    if (cna_content_manager_load_model(manager, view(TaggedModelName), &model) !=
            CNA_RESULT_SUCCESS ||
        model == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* The resource graph the load rebuilt: a loaded model that skipped it would report no bones
       and no meshes while still looking like a valid handle. */
    if (cna_model_get_bones(model, &bones) != CNA_RESULT_SUCCESS ||
        cna_model_bone_collection_get_count(bones, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(1) ||
        cna_model_get_meshes(model, &meshes) != CNA_RESULT_SUCCESS ||
        cna_model_mesh_collection_get_count(meshes, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(0) ||
        cna_model_get_root(model, &present, &root) != CNA_RESULT_SUCCESS ||
        present != CNA_TRUE || root == CNA_INVALID_HANDLE) {
        (void)cna_model_destroy(model);
        return 0;
    }
    {
        char name[8];
        if (cna_model_bone_copy_name(root, name, sizeof(name), &bytes) != CNA_RESULT_SUCCESS ||
            bytes != strlen("Root") || memcmp(name, "Root", (size_t)bytes) != 0) {
            (void)cna_model_destroy(model);
            return 0;
        }
    }

    /* The tag, read the way TrianglePickingSample reads it. */
    if (cna_model_get_content_tag_dictionary_ext(model, &present, &tag) != CNA_RESULT_SUCCESS ||
        present != CNA_TRUE || tag == CNA_INVALID_HANDLE) {
        (void)cna_model_destroy(model);
        return 0;
    }
    entry.struct_size = (uint32_t)sizeof(entry);
    if (cna_object_dictionary_ext_contains_key(
            tag, view("BoundingSphere"), &present) != CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_object_dictionary_ext_contains_key(tag, view("NotThere"), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_FALSE ||
        cna_object_dictionary_ext_copy_value(
            tag, view("BoundingSphere"), CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE,
            &sphere, sizeof(sphere)) != CNA_RESULT_SUCCESS ||
        sphere.center.x != 1.0f || sphere.center.y != 2.0f || sphere.center.z != 3.0f ||
        sphere.radius != 4.0f ||
        cna_object_dictionary_ext_get_entry(tag, view("Vertices"), &entry) !=
            CNA_RESULT_SUCCESS ||
        entry.kind != CNA_OBJECT_DICTIONARY_VALUE_VECTOR3 ||
        entry.is_array != CNA_TRUE || entry.element_count != UINT64_C(3) ||
        cna_object_dictionary_ext_copy_array(
            tag, view("Vertices"), CNA_OBJECT_DICTIONARY_VALUE_VECTOR3,
            vertices, sizeof(vertices), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(vertices) ||
        vertices[1].x != 1.0f || vertices[2].y != 1.0f) {
        (void)cna_object_dictionary_ext_destroy(tag);
        (void)cna_model_destroy(model);
        return 0;
    }

    /* The tag handle keeps the loaded asset alive on its own, so destroying the model first is
       safe -- which is the whole reason it is an owned handle rather than a borrowed one. */
    if (cna_model_destroy(model) != CNA_RESULT_SUCCESS ||
        cna_object_dictionary_ext_contains_key(
            tag, view("Vertices"), &present) != CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_object_dictionary_ext_destroy(tag) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A hand-built model has no content tag, and says so without failing. */
    if (cna_model_create_default(&model) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    present = CNA_TRUE;
    tag = UINT64_C(7);
    if (cna_model_get_content_tag_dictionary_ext(model, &present, &tag) != CNA_RESULT_SUCCESS ||
        present != CNA_FALSE || tag != CNA_INVALID_HANDLE) {
        (void)cna_model_destroy(model);
        return 0;
    }
    {
        void* object = (void*)&g_model_tag_object;
        present = CNA_TRUE;
        if (cna_model_get_content_tag_foreign_object_ext(model, &present, &object) !=
                CNA_RESULT_SUCCESS ||
            present != CNA_FALSE || object != 0) {
            (void)cna_model_destroy(model);
            return 0;
        }
    }
    if (cna_model_destroy(model) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return validate_foreign_model_tag(manager);
}

static int validate_cnb_loader_through_manager(const CNA_Handle manager)
{
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    static uint8_t bytes[4096];
    static const uint8_t payload[4] = {1U, 2U, 3U, 4U};
    uint64_t produced = 0U;
    uint32_t asset_type = 0U;
    CNA_CnbChunkId chunk = 0U;
    void* object = 0;

    if (cna_cnb_asset_type_id_from_name(view(CnbTypeName), &asset_type) != CNA_RESULT_SUCCESS ||
        cna_cnb_writer_create(asset_type, 1U, &writer) != CNA_RESULT_SUCCESS ||
        cna_cnb_writer_set_metadata(writer, view(CnbTypeName), view(CnbAssetName)) !=
            CNA_RESULT_SUCCESS ||
        cna_cnb_make_chunk_id('L', 'V', 'L', 'D', &chunk) != CNA_RESULT_SUCCESS ||
        cna_cnb_writer_add_chunk(
            writer, chunk, payload, sizeof(payload), CNA_CNB_CHUNK_FLAG_MANDATORY, 4U) !=
            CNA_RESULT_SUCCESS ||
        cna_cnb_writer_build(writer, bytes, sizeof(bytes), &produced) != CNA_RESULT_SUCCESS ||
        cna_cnb_writer_destroy(writer) != CNA_RESULT_SUCCESS ||
        !write_binary_file(CnbAssetPath, bytes, (size_t)produced)) {
        return 0;
    }

    g_cnb_level = 0;
    if (cna_cnb_loader_registry_register(
            asset_type, view(CnbTypeName), on_level_load, &g_cnb_level) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The manager finds the `.cnb` beside the name, resolves the loader by the file's own asset
       type, and hands the object out through the foreign route. */
    if (cna_content_manager_load_foreign_ext(manager, view(CnbAssetName), &object) !=
            CNA_RESULT_SUCCESS ||
        object != &g_cnb_level || g_cnb_level != 1) {
        (void)cna_cnb_loader_registry_remove(asset_type, 0);
        return 0;
    }

    /* Withdrawing the registration makes the next load of a fresh name fail rather than fall
       back -- the same rule an `.xnb` naming an unregistered reader follows. */
    {
        CNA_Bool removed = CNA_FALSE;
        if (cna_cnb_loader_registry_remove(asset_type, &removed) != CNA_RESULT_SUCCESS ||
            removed != CNA_TRUE) {
            return 0;
        }
    }
    return 1;
}

static int validate_foreign_load(const CNA_Handle manager)
{
    ForeignLoadState state;
    CNA_ContentTypeReaderCallbacks callbacks;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    void* object = (void*)&state;
    void* second = 0;

    memset(&state, 0, sizeof(state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.target_type_name = view("CNA.Test.ContentSmoke.ForeignType");
    callbacks.type_version = INT32_C(3);
    callbacks.can_deserialize_into_existing_object = CNA_FALSE;
    callbacks.create = foreign_load_create;
    callbacks.read = foreign_load_read;
    callbacks.destroy = foreign_load_destroy;
    callbacks.context = &state;

    /* With nothing registered the file names an unknown reader, and the load fails rather than
       guessing -- the negative that proves the success below comes from the registration. */
    if (cna_content_manager_load_foreign_ext(manager, view(ForeignAssetName), &object) !=
            CNA_RESULT_IO ||
        object != 0) {
        return 0;
    }
    if (cna_content_manager_load_foreign_ext(manager, view(ForeignAssetName), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_foreign_ext(manager, (CNA_StringView){0, 0U}, &object) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_content_type_reader_manager_register(
            view(ForeignLoadReaderName), &callbacks, &registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_content_manager_load_foreign_ext(manager, view(ForeignAssetName), &object) !=
            CNA_RESULT_SUCCESS ||
        object != (void*)&state.payload || state.create_calls != 1 || state.read_calls != 1) {
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    /* Cached like any other asset: the second load answers the same object without re-reading. */
    if (cna_content_manager_load_foreign_ext(manager, view(ForeignAssetName), &second) !=
            CNA_RESULT_SUCCESS ||
        second != object || state.read_calls != 1) {
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    /* A typed loader asked for the same asset is refused rather than handed a foreign object. */
    {
        CNA_Handle wrong_type = UINT64_C(77);
        if (cna_content_manager_load_texture2d(manager, view(ForeignAssetName), &wrong_type) !=
                CNA_RESULT_IO ||
            wrong_type != CNA_INVALID_HANDLE) {
            (void)cna_content_type_reader_manager_unregister(registration);
            return 0;
        }
    }
    if (cna_content_type_reader_manager_unregister(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Unloading drops the cache, so the withdrawn registration is what the next load sees. */
    object = (void*)&state;
    if (cna_content_manager_unload(manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_load_foreign_ext(manager, view(ForeignAssetName), &object) !=
            CNA_RESULT_IO ||
        object != 0) {
        return 0;
    }
    return state.destroy_calls >= 1;
}

/* CBIND-061: ContentManager.Load<Effect>. The stock-effect descriptor is the shape that works on
   any renderer with shaders, so it is the one this fixture uses; a compiled .xnb Effect depends on
   the compiled-effect capability, which no tree in this campaign advertises. */
static const char EffectAssetName[] = "cna_c_api_content_effect";
static const char EffectDescriptorPath[] = "cna_c_api_content_effect.cnj";

static int write_effect_fixture(void)
{
    /* The envelope's own "type" names the stock effect; there is no separate field for it. */
    static const char descriptor[] = "{\"cnjVersion\":1,\"type\":\"BasicEffect\"}";
    return write_text_file(EffectDescriptorPath, descriptor);
}

static int validate_effect_load(const CNA_Handle manager)
{
    CNA_EffectHandle effect = UINT64_C(77);
    CNA_Result loaded = CNA_RESULT_SUCCESS;

    if (cna_content_manager_load_effect(manager, view(EffectAssetName), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_load_effect(manager, (CNA_StringView){0, 0U}, &effect) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        effect != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_content_manager_load_effect(
            manager, view("cna_c_api_content_no_such_effect"), &effect) != CNA_RESULT_IO ||
        effect != CNA_INVALID_HANDLE) {
        return 0;
    }

    /* A renderer without a programmable pipeline refuses the effect rather than substituting one,
       so both answers are correct here and only a third would be a fault. */
    loaded = cna_content_manager_load_effect(manager, view(EffectAssetName), &effect);
    if (loaded == CNA_RESULT_NOT_SUPPORTED) {
        return effect == CNA_INVALID_HANDLE;
    }
    if (loaded != CNA_RESULT_SUCCESS || effect == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* What comes back is an ordinary effect handle: it names its type and owns its collections. */
    {
        CNA_Handle owner = CNA_INVALID_HANDLE;
        uint64_t bytes = 0U;
        if (cna_effect_get_graphics_device(effect, &owner) != CNA_RESULT_SUCCESS ||
            owner == CNA_INVALID_HANDLE ||
            cna_effect_get_type_name_byte_count(effect, &bytes) != CNA_RESULT_SUCCESS ||
            bytes == 0U) {
            (void)cna_effect_destroy(effect);
            return 0;
        }
    }
    return cna_effect_destroy(effect) == CNA_RESULT_SUCCESS;
}

/* CBIND-069: a caller-supplied .cnj loader, the descriptor counterpart of the registered XNB
   reader. The coverage matrix recorded this family as having no C form "because registering a
   reader requires naming an arbitrary C++ type T"; true of the general template, and not of the
   one instantiation a C caller needs, which is the same carrier CBIND-056 already produces. */

static const char CnjAssetName[] = "cna_c_api_content_custom";
static const char CnjDescriptorPath[] = "cna_c_api_content_custom.cnj";
static const char CnjTypeName[] = "CnaCApiCustomType";

typedef struct CnjLoaderState {
    int calls;
    int saw_type_name;
    int payload;
} CnjLoaderState;

static CNA_Result on_cnj_load(
    void* const context,
    const CNA_StringView cnj_json,
    void** const out_object)
{
    CnjLoaderState* const state = (CnjLoaderState*)context;
    ++state->calls;
    /* The whole descriptor text arrives, and it is not NUL-terminated -- reading it as a C string
       is the mistake this assertion exists to catch. */
    if (cnj_json.data == 0 || cnj_json.byte_length == 0U) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (memchr(cnj_json.data, 'C', (size_t)cnj_json.byte_length) != 0) {
        state->saw_type_name = 1;
    }
    *out_object = &state->payload;
    return CNA_RESULT_SUCCESS;
}

static int write_cnj_fixture(void)
{
    static const char descriptor[] =
        "{\"cnjVersion\":1,\"type\":\"CnaCApiCustomType\",\"payload\":7}";
    return write_text_file(CnjDescriptorPath, descriptor);
}

static int validate_cnj_loader(const CNA_Handle manager)
{
    CnjLoaderState state;
    void* object = 0;

    memset(&state, 0, sizeof(state));

    /* Refusals first, none of which may register anything. */
    if (cna_content_manager_register_cnj_loader_ext(manager, view(CnjTypeName), 0, &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_manager_register_cnj_loader_ext(
            manager, (CNA_StringView){0, 0U}, on_cnj_load, &state) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_content_manager_register_cnj_loader_ext(
            manager, view(CnjTypeName), on_cnj_load, &state) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The same type name twice on one manager is a state failure, not a silent replacement. */
    if (cna_content_manager_register_cnj_loader_ext(
            manager, view(CnjTypeName), on_cnj_load, &state) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    /* And the descriptor loads through the same foreign route a registered XNB reader uses --
       that route takes no type argument and does not care which of the two produced the object. */
    if (cna_content_manager_load_foreign_ext(manager, view(CnjAssetName), &object) !=
            CNA_RESULT_SUCCESS ||
        object != (void*)&state.payload || state.calls != 1 || state.saw_type_name != 1) {
        return 0;
    }
    /* Cached like any other asset. */
    {
        void* again = 0;
        if (cna_content_manager_load_foreign_ext(manager, view(CnjAssetName), &again) !=
                CNA_RESULT_SUCCESS ||
            again != object || state.calls != 1) {
            return 0;
        }
    }
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    ContentState* const state = (ContentState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_StringView root = {".", UINT64_C(1)};
    CNA_ContentManagerCreateInfo create_info = {
        sizeof(CNA_ContentManagerCreateInfo), UINT32_C(1), root, UINT64_C(1)
    };
    CNA_Handle invalid_manager = UINT64_C(77);
    if (cna_content_manager_create(graphics_device, &create_info, &invalid_manager) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        invalid_manager != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    create_info.reserved = 0U;
    if (cna_content_manager_create(graphics_device, &create_info, &state->content_manager) !=
            CNA_RESULT_SUCCESS ||
        state->content_manager == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    uint64_t root_bytes = 0U;
    char root_copy[2] = {'x', 'x'};
    if (cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 1U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            0U,
            &root_bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        root_copy[0] != 'x' || root_bytes != 1U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            1U,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_copy[0] != '.' || root_copy[1] != 'x') {
        return CNA_RESULT_INVALID_STATE;
    }

    const char invalid_utf8[] = {(char)0xC3, '('};
    if (cna_content_manager_set_root_directory(
            state->content_manager,
            (CNA_StringView){invalid_utf8, UINT64_C(2)}) != CNA_RESULT_ENCODING ||
        cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 1U ||
        cna_content_manager_set_root_directory(
            state->content_manager,
            (CNA_StringView){"missing", UINT64_C(7)}) != CNA_RESULT_SUCCESS ||
        cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 7U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            sizeof(root_copy),
            &root_bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        root_copy[0] != '.' || root_copy[1] != 'x' ||
        cna_content_manager_set_root_directory(state->content_manager, root) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle invalid_texture = UINT64_C(88);
    const char embedded_nul[] = {'x', '\0', 'y'};
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            (CNA_StringView){0, 0U},
            &invalid_texture) != CNA_RESULT_INVALID_ARGUMENT ||
        invalid_texture != CNA_INVALID_HANDLE ||
        cna_content_manager_load_texture2d(
            state->content_manager,
            (CNA_StringView){embedded_nul, UINT64_C(3)},
            &invalid_texture) != CNA_RESULT_ENCODING ||
        invalid_texture != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_StringView asset = {FixturePath, sizeof(FixturePath) - 1U};
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &state->texture) != CNA_RESULT_SUCCESS ||
        state->texture == CNA_INVALID_HANDLE || !pixel_is_fixture_color(state->texture)) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Handle cached_texture = CNA_INVALID_HANDLE;
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &cached_texture) != CNA_RESULT_SUCCESS ||
        cached_texture == CNA_INVALID_HANDLE || cached_texture == state->texture ||
        !pixel_is_fixture_color(cached_texture) ||
        cna_texture2d_destroy(cached_texture) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (!validate_paths_and_device(state->content_manager, graphics_device) ||
        !validate_resource_manager(graphics_device) ||
        !validate_font_load(state->content_manager) ||
        !validate_foreign_load(state->content_manager) ||
        !validate_reflective_reader(state->content_manager) ||
        !validate_object_dictionary(state->content_manager) ||
        !validate_content_model(state->content_manager) ||
        !validate_cnb_loader_through_manager(state->content_manager) ||
        !validate_effect_load(state->content_manager) ||
        !validate_cnj_loader(state->content_manager)) {
        return CNA_RESULT_INVALID_STATE;
    }

    RootFixture fixture;
    if (!create_root_fixture(&fixture)) {
        return CNA_RESULT_INVALID_STATE;
    }
    const int manifest_ok = validate_manifest(state->content_manager, &fixture) &&
        validate_typed_loads(state->content_manager);
    const int cleaned = destroy_root_fixture(&fixture);
    if (!manifest_ok || !cleaned ||
        cna_content_manager_set_root_directory(state->content_manager, root) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_content_manager_unload(state->content_manager) != CNA_RESULT_SUCCESS ||
        !pixel_is_fixture_color(state->texture) || remove(FixturePath) != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    invalid_texture = UINT64_C(99);
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &invalid_texture) != CNA_RESULT_IO ||
        invalid_texture != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_ErrorInfo error = {
        sizeof(CNA_ErrorInfo), UINT32_C(1), CNA_RESULT_SUCCESS, CNA_ERROR_CATEGORY_NONE, 0U
    };
    if (cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS ||
        error.result != CNA_RESULT_IO || error.category != CNA_ERROR_CATEGORY_IO ||
        error.message_byte_length == 0U) {
        return CNA_RESULT_INVALID_STATE;
    }

    state->load_validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int call_unload_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    state->result = cna_content_manager_unload(state->content_manager);
    return 0;
}

int main(void)
{
    if (!write_fixture() || !write_font_fixture() || !write_foreign_asset() ||
        !write_effect_fixture() || !write_cnj_fixture()) {
        return CNA_TEST_FAIL(1);
    }

    ContentState state = {CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API content smoke", UINT64_C(19)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.load_validated != 1 ||
        state.content_manager == CNA_INVALID_HANDLE || state.texture == CNA_INVALID_HANDLE) {
        (void)remove(FixturePath);
        return CNA_TEST_FAIL(2);
    }
    if (cna_game_destroy(game) != CNA_RESULT_INVALID_STATE) {
        return CNA_TEST_FAIL(3);
    }

    WrongThreadState wrong_thread = {state.content_manager, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, call_unload_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(4);
    }

    (void)remove(FontAtlasPath);
    (void)remove(FontDescriptorPath);
    (void)remove(ForeignAssetPath);
    (void)remove(ReflectiveAssetPath);
    (void)remove(CnbAssetPath);
    (void)remove(EffectDescriptorPath);
    (void)remove(CnjDescriptorPath);

    if (cna_content_manager_destroy(state.content_manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_unload(state.content_manager) != CNA_RESULT_INVALID_HANDLE ||
        !pixel_is_fixture_color(state.texture) ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(5);
    }
    return 0;
}
