// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

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
        "\"glyphs\":["
        "{\"char\":65,\"source\":[0,0,2,8],\"crop\":[0,0,2,8],\"kerning\":[0.0,5.0,0.0]},"
        "{\"char\":63,\"source\":[2,0,2,8],\"crop\":[0,0,2,8],\"kerning\":[1.0,4.0,2.0]}"
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
        glyph_count != UINT64_C(2) || glyphs[0].character != (CNA_Char16)'A' ||
        glyphs[0].glyph_bounds.width != 2 || glyphs[1].character != (CNA_Char16)'?' ||
        glyphs[1].kerning.y != 4.0f ||
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
        !validate_effect_load(state->content_manager)) {
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
        !write_effect_fixture()) {
        return 1;
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
        return 2;
    }
    if (cna_game_destroy(game) != CNA_RESULT_INVALID_STATE) {
        return 3;
    }

    WrongThreadState wrong_thread = {state.content_manager, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, call_unload_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 4;
    }

    (void)remove(FontAtlasPath);
    (void)remove(FontDescriptorPath);
    (void)remove(ForeignAssetPath);
    (void)remove(EffectDescriptorPath);

    if (cna_content_manager_destroy(state.content_manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_unload(state.content_manager) != CNA_RESULT_INVALID_HANDLE ||
        !pixel_is_fixture_color(state.texture) ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 5;
    }
    return 0;
}
