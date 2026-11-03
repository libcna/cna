// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>
#include "CnaTestReport.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, __func__, #expression); \
        return CNA_TEST_FAIL(1); \
    } \
} while (0)
#define SV(text) ((CNA_StringView){(text), sizeof(text) - 1U})
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static int filled(const void* data, size_t size, unsigned char byte)
{
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != byte) return 0;
    }
    return 1;
}

static int last_error(CNA_Result result, CNA_ErrorCategory category)
{
    CNA_ErrorInfo info = {sizeof(info), 1, 0, 0, 0};
    uint64_t size = UINT64_MAX, copied = UINT64_MAX;
    char message[2048];
    memset(message, 0x5a, sizeof(message));
    CHECK(cna_error_get_last_info(&info) == CNA_RESULT_SUCCESS);
    CHECK(info.result == result && info.category == category);
    CHECK(cna_error_get_last_message_size(&size) == CNA_RESULT_SUCCESS);
    CHECK(size > 0 && size < sizeof(message) && size == info.message_byte_length);
    CHECK(cna_error_copy_last_message(message, size, &copied) == CNA_RESULT_SUCCESS);
    CHECK(copied == size && memchr(message, '\0', (size_t)size) == NULL);
    CHECK(filled(message + size, sizeof(message) - (size_t)size, 0x5a));
    return 0;
}

static int point_contract(void)
{
    CNA_Point output = {77, -88};
    CHECK(cna_point_get_zero(&output) == CNA_RESULT_SUCCESS);
    CHECK(output.x == 0 && output.y == 0);
    CHECK(cna_point_get_zero(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CHECK(cna_point_divide((CNA_Point){-19, 23}, (CNA_Point){4, -5}, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.x == -4 && output.y == -4);
    /* The second component fails after a valid first quotient could have been computed. */
    const CNA_Point left[] = {{19, 23}, {19, 23}, {INT32_MIN, 23}, {19, INT32_MIN}};
    const CNA_Point right[] = {{0, 5}, {4, 0}, {-1, 5}, {4, -1}};
    for (size_t i = 0; i < ARRAY_COUNT(left); ++i) {
        output = (CNA_Point){77, -88};
        CNA_Result expected = i < 2 ? CNA_RESULT_INVALID_ARGUMENT : CNA_RESULT_OVERFLOW;
        CHECK(cna_point_divide(left[i], right[i], &output) == expected);
        CHECK(output.x == 77 && output.y == -88);
        CHECK(last_error(expected, i < 2 ? CNA_ERROR_CATEGORY_ARGUMENT : CNA_ERROR_CATEGORY_RANGE) == 0);
    }
    CHECK(cna_point_divide((CNA_Point){1, 2}, (CNA_Point){1, 1}, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static int rectangle_contract(void)
{
    const CNA_Rectangle rectangle = {-12, 34, 56, 78};
    const char expected[] = "{X:-12 Y:34 Width:56 Height:78}";
    char output[96];
    uint64_t size = UINT64_MAX, written = UINT64_MAX;
    CHECK(cna_rectangle_get_string_size(rectangle, &size) == CNA_RESULT_SUCCESS);
    CHECK(size == sizeof(expected) - 1U);
    CHECK(cna_rectangle_copy_string(rectangle, NULL, 0, &written) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(written == size);
    CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    for (uint64_t capacity = 0; capacity < size; ++capacity) {
        memset(output, 0xa5, sizeof(output));
        written = UINT64_MAX;
        CHECK(cna_rectangle_copy_string(rectangle, output + 1, capacity, &written) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(written == size && filled(output, sizeof(output), 0xa5));
    }
    for (unsigned extra = 0; extra < 2; ++extra) {
        memset(output, 0xa5, sizeof(output));
        CHECK(cna_rectangle_copy_string(rectangle, output + 1, size + extra, &written) == CNA_RESULT_SUCCESS);
        CHECK(written == size && (unsigned char)output[0] == 0xa5);
        CHECK(memcmp(output + 1, expected, (size_t)size) == 0);
        CHECK(filled(output + 1 + size, sizeof(output) - 1U - (size_t)size, 0xa5));
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_rectangle_copy_string(rectangle, output, sizeof(output), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output), 0xa5));
    CHECK(cna_rectangle_copy_string(rectangle, NULL, 1, &written) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CHECK(cna_rectangle_get_string_size(rectangle, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}

static int vector_contract(void)
{
    const CNA_Matrix matrix = {2, 0, 0, 0, 0, -3, 0, 0, 0, 0, 1, 0, 10, 20, 0, 1};
    const CNA_Vector2 source[] = {{1, 2}, {3, 4}, {5, 6}};
    CNA_Vector2 output[4];
    const CNA_Vector2 sentinel = {-77, 88};
    for (size_t i = 0; i < ARRAY_COUNT(output); ++i) output[i] = sentinel;
    CHECK(cna_vector2_transform_matrix_array(source, 3, 1, matrix, output, 4, 1, 2) == CNA_RESULT_SUCCESS);
    CHECK(output[0].x == -77 && output[0].y == 88 && output[3].x == -77 && output[3].y == 88);
    CHECK(output[1].x == 16 && output[1].y == 8 && output[2].x == 20 && output[2].y == 2);
    CHECK(cna_vector2_transform_normal_array(source, 3, 1, matrix, output, 4, 1, 2) == CNA_RESULT_SUCCESS);
    CHECK(output[1].x == 6 && output[1].y == -12 && output[2].x == 10 && output[2].y == -18);
    const uint64_t ranges[][3] = {{2, 0, 2}, {0, 3, 2}, {4, 0, 0}, {0, 5, 0}, {UINT64_MAX, 0, 1}, {0, UINT64_MAX, 1}};
    for (size_t i = 0; i < ARRAY_COUNT(ranges); ++i) {
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) output[j] = sentinel;
        CHECK(cna_vector2_transform_matrix_array(source, 3, ranges[i][0], matrix,
              output, 4, ranges[i][1], ranges[i][2]) == CNA_RESULT_INVALID_ARGUMENT);
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) CHECK(output[j].x == -77 && output[j].y == 88);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
        CHECK(cna_vector2_transform_normal_array(source, 3, ranges[i][0], matrix,
              output, 4, ranges[i][1], ranges[i][2]) == CNA_RESULT_INVALID_ARGUMENT);
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) CHECK(output[j].x == -77 && output[j].y == 88);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_vector2_transform_matrix_array(NULL, 0, 0, matrix, NULL, 0, 0, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_normal_array(NULL, 0, 0, matrix, NULL, 0, 0, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_matrix_array(source, 3, 3, matrix, output, 4, 4, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_matrix_array(NULL, 1, 0, matrix, output, 4, 0, 0) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_vector2_transform_matrix_array(source, 3, 0, matrix, NULL, 1, 0, 0) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}

/* Uniform signature adapters share the guarded capacity checks across text routes. */
typedef CNA_Result (*TextCopy)(CNA_Handle, char*, uint64_t, uint64_t*);
static CNA_Result diagnostic_message(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_diagnostics_copy_message_ext(handle, 0, out, capacity, count);
}
static CNA_Result diagnostic_label(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_diagnostics_copy_source_label_ext(handle, 0, out, capacity, count);
}
static CNA_Result binding_name(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_package_copy_binding_name_ext(handle, 0, out, capacity, count);
}

static int text_copy_contract(TextCopy copy, CNA_Handle handle, const char* expected, size_t size)
{
    char output[128];
    uint64_t count = UINT64_MAX;
    CHECK(size + 2 < sizeof(output));
    CHECK(copy(handle, NULL, 0, &count) == (size ? CNA_RESULT_BUFFER_TOO_SMALL : CNA_RESULT_SUCCESS));
    CHECK(count == size);
    if (size) CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    for (size_t capacity = 0; capacity < size; ++capacity) {
        memset(output, 0xa5, sizeof(output));
        count = UINT64_MAX;
        CHECK(copy(handle, output + 1, capacity, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == size && filled(output, sizeof(output), 0xa5));
    }
    for (unsigned extra = 0; extra < 2; ++extra) {
        memset(output, 0xa5, sizeof(output));
        CHECK(copy(handle, output + 1, size + extra, &count) == CNA_RESULT_SUCCESS);
        CHECK(count == size && (unsigned char)output[0] == 0xa5);
        CHECK(memcmp(output + 1, expected, size) == 0);
        CHECK(filled(output + 1 + size, sizeof(output) - 1U - size, 0xa5));
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(copy(handle, output, sizeof(output), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output), 0xa5));
    CHECK(copy(handle, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static const CNA_StringView invalid_text[] = {
    {"\xc0\xaf", 2},             /* Overlong encoding. */
    {"\xed\xa0\x80", 3},         /* Surrogate. */
    {"\xf4\x90\x80\x80", 4},     /* Above U+10FFFF. */
    {"\xe2\x82", 2},             /* Truncated sequence. */
    {"\x80", 1},                 /* Lone continuation. */
    {"a\0b", 3}
};
static const char unicode_text[] = "\xc5\xbe\xe7\x8c\xab\xf0\x9f\x98\x80";

static int diagnostic_contract(void)
{
    CNA_ShaderDiagnosticInfoEXT info = {sizeof(info), 1, CNA_SHADER_DIAGNOSTIC_SEVERITY_WARNING_EXT,
                                       CNA_SHADER_STAGE_FRAGMENT_EXT, 12, 7};
    CNA_ShaderDiagnosticsHandleEXT handle = CNA_INVALID_HANDLE;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV(unicode_text), SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    uint64_t count = UINT64_MAX;
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 1);
    CNA_ShaderDiagnosticInfoEXT output;
    memset(&output, 0xa5, sizeof(output));
    output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_diagnostics_get_info_ext(handle, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.severity == info.severity && output.stage == info.stage && output.line == 12 && output.column == 7);
    CHECK(text_copy_contract(diagnostic_message, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(diagnostic_label, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    for (unsigned mode = 0; mode < 3; ++mode) {
        memset(&output, 0xa5, sizeof(output));
        output.struct_size = mode == 0 ? sizeof(output) - 1U : sizeof(output);
        output.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(output)]; memcpy(before, &output, sizeof(output));
        CHECK(cna_shader_diagnostics_get_info_ext(handle, mode == 2 ? 1 : 0, &output) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &output, sizeof(output)) == 0);
    }
    char bytes[32]; memset(bytes, 0xa5, sizeof(bytes)); count = 77;
    CHECK(cna_shader_diagnostics_copy_message_ext(handle, 1, bytes, sizeof(bytes), &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(count == 77 && filled(bytes, sizeof(bytes), 0xa5));
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_create_ext(&info, SV("label"), SV("message"), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 2; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_diagnostics_create_ext(&info, field == 0 ? invalid_text[i] : SV("label"),
                  field == 1 ? invalid_text[i] : SV("message"), &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    for (unsigned field = 0; field < 2; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_diagnostics_create_ext(&info, field == 0 ? (CNA_StringView){NULL, 1} : SV("label"),
              field == 1 ? (CNA_StringView){NULL, 1} : SV("message"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
    }
    CHECK(cna_shader_diagnostics_create_ext(&info, (CNA_StringView){NULL, 0}, SV("message"), &handle) == CNA_RESULT_SUCCESS);
    CHECK(text_copy_contract(diagnostic_label, handle, "", 0) == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_parse_ext((CNA_StringView){NULL, 0}, CNA_SHADER_STAGE_UNKNOWN_EXT,
          (CNA_StringView){NULL, 0}, &handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_parse_ext(SV(unicode_text), CNA_SHADER_STAGE_VERTEX_EXT,
          SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 1);
    output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_diagnostics_get_info_ext(handle, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.severity == CNA_SHADER_DIAGNOSTIC_SEVERITY_ERROR_EXT &&
          output.stage == CNA_SHADER_STAGE_VERTEX_EXT && output.line == 0 && output.column == 0);
    CHECK(text_copy_contract(diagnostic_message, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(diagnostic_label, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 2; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_diagnostics_parse_ext(field == 0 ? invalid_text[i] : SV("log"),
                  CNA_SHADER_STAGE_VERTEX_EXT, field == 1 ? invalid_text[i] : SV("label"),
                  &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    info.stage = UINT32_MAX; handle = UINT64_MAX;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV("label"), SV("message"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(handle == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static int shader_code_contract(void)
{
    CNA_ShaderCodeHandleEXT handle = CNA_INVALID_HANDLE;
    const CNA_ShaderLanguageEXT language = CNA_SHADER_LANGUAGE_WGSL_EXT;
    const CNA_ShaderStageEXT stage = CNA_SHADER_STAGE_VERTEX_EXT;
    CHECK(cna_shader_code_create_text_ext(language, stage, SV(unicode_text), SV(unicode_text),
          SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    CNA_ShaderCodeInfoEXT info;
    memset(&info, 0xa5, sizeof(info)); info.struct_size = sizeof(info); info.struct_version = 1;
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_SUCCESS);
    CHECK(info.language == language && info.stage == stage && info.is_text == CNA_TRUE);
    CHECK(info.payload_byte_count == sizeof(unicode_text) - 1U && filled(info.reserved, sizeof(info.reserved), 0));
    CHECK(text_copy_contract(cna_shader_code_copy_entry_point_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(cna_shader_code_copy_source_label_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(cna_shader_code_copy_text_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    unsigned char before[sizeof(info)]; info.struct_version = 2; memcpy(before, &info, sizeof(info));
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(memcmp(before, &info, sizeof(info)) == 0);
    uint8_t binary[24]; memset(binary, 0xa5, sizeof(binary)); uint64_t count = 77;
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary, sizeof(binary), &count) == CNA_RESULT_INVALID_STATE);
    CHECK(count == 77 && filled(binary, sizeof(binary), 0xa5));
    CHECK(last_error(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE) == 0);
    CHECK(cna_shader_code_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 3; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_code_create_text_ext(language, stage,
                  field == 0 ? invalid_text[i] : SV("entry"), field == 1 ? invalid_text[i] : SV("label"),
                  field == 2 ? invalid_text[i] : SV("source"), &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    for (unsigned field = 0; field < 3; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_code_create_text_ext(language, stage,
              field == 0 ? (CNA_StringView){NULL, 1} : SV("entry"),
              field == 1 ? (CNA_StringView){NULL, 1} : SV("label"),
              field == 2 ? (CNA_StringView){NULL, 1} : SV("source"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    for (unsigned field = 0; field < 4; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_code_create_text_ext(field == 0 ? UINT32_MAX : language,
              field == 1 ? UINT32_MAX : stage, field == 2 ? (CNA_StringView){NULL, 0} : SV("entry"),
              (CNA_StringView){NULL, 0}, field == 3 ? (CNA_StringView){NULL, 0} : SV("source"),
              &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    const uint8_t payload[] = {0x03, 0x02, 0x23, 0x07, 0xff, 0x00, 0x80, 0xfe};
    CHECK(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_SPIRV_EXT, stage, SV("main"),
          (CNA_StringView){NULL, 0}, payload, sizeof(payload), &handle) == CNA_RESULT_SUCCESS);
    info.struct_version = 1;
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_SUCCESS);
    CHECK(info.language == CNA_SHADER_LANGUAGE_SPIRV_EXT && info.is_text == CNA_FALSE && info.payload_byte_count == sizeof(payload));
    for (size_t capacity = 0; capacity < sizeof(payload); ++capacity) {
        memset(binary, 0xa5, sizeof(binary));
        CHECK(cna_shader_code_copy_bytes_ext(handle, binary + 1, capacity, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == sizeof(payload) && filled(binary, sizeof(binary), 0xa5));
    }
    CHECK(cna_shader_code_copy_bytes_ext(handle, NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == sizeof(payload));
    CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    memset(binary, 0xa5, sizeof(binary));
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary + 1, sizeof(payload), &count) == CNA_RESULT_SUCCESS);
    CHECK(count == sizeof(payload) && binary[0] == 0xa5 && memcmp(binary + 1, payload, sizeof(payload)) == 0);
    CHECK(filled(binary + 1 + sizeof(payload), sizeof(binary) - 1U - sizeof(payload), 0xa5));
    memset(binary, 0xa5, sizeof(binary));
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary, sizeof(binary), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(binary, sizeof(binary), 0xa5));
    CHECK(cna_shader_code_copy_bytes_ext(handle, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    memset(binary, 0xa5, sizeof(binary)); count = 77;
    CHECK(cna_shader_code_copy_text_ext(handle, (char*)binary, sizeof(binary), &count) == CNA_RESULT_INVALID_STATE);
    CHECK(count == 77 && filled(binary, sizeof(binary), 0xa5));
    CHECK(last_error(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE) == 0);
    CHECK(cna_shader_code_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    handle = UINT64_MAX;
    CHECK(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_SPIRV_EXT, stage, SV("main"), SV(""),
          NULL, 0, &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static int shader_package_contract(void)
{
    CNA_ShaderCodeHandleEXT code = CNA_INVALID_HANDLE;
    CNA_ShaderPackageHandleEXT package = CNA_INVALID_HANDLE;
    CNA_ShaderStageEXT stage = CNA_SHADER_STAGE_VERTEX_EXT;
    CHECK(cna_shader_code_create_text_ext(CNA_SHADER_LANGUAGE_WGSL_EXT, stage, SV("main"), SV(""), SV("source"), &code) == CNA_RESULT_SUCCESS);
    CNA_ShaderBindingRequirementEXT binding = {sizeof(binding), 1, SV(unicode_text), 3,
        CNA_SHADER_BINDING_SAMPLED_TEXTURE2D_EXT, CNA_SHADER_STAGE_VERTEX_EXT};
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &binding, 1, &package) == CNA_RESULT_SUCCESS);
    uint64_t count = UINT64_MAX;
    CHECK(cna_shader_package_get_binding_count_ext(package, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(text_copy_contract(binding_name, package, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CNA_ShaderBindingRequirementEXT output;
    memset(&output, 0xa5, sizeof(output)); output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_package_get_binding_ext(package, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.name.data == NULL && output.name.byte_length == 0 && output.binding == 3);
    CHECK(output.type == binding.type && output.stage == stage);
    for (unsigned mode = 0; mode < 3; ++mode) {
        memset(&output, 0xa5, sizeof(output));
        output.struct_size = mode == 0 ? sizeof(output) - 1U : sizeof(output);
        output.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(output)]; memcpy(before, &output, sizeof(output));
        CHECK(cna_shader_package_get_binding_ext(package, mode == 2 ? 1 : 0, &output) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &output, sizeof(output)) == 0);
    }
    CHECK(cna_shader_package_destroy_ext(package) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        binding.name = invalid_text[i]; package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &binding, 1, &package) == CNA_RESULT_ENCODING);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
    }
    binding.name = SV("texture");
    for (unsigned field = 0; field < 3; ++field) {
        CNA_ShaderBindingRequirementEXT invalid = binding;
        if (field == 0) invalid.type = UINT32_MAX;
        if (field == 1) invalid.stage = UINT32_MAX;
        if (field == 2) invalid.binding = -1;
        package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &invalid, 1, &package) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, NULL, 0, &package) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_package_get_binding_count_ext(package, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_shader_package_destroy_ext(package) == CNA_RESULT_SUCCESS);
    package = UINT64_MAX;
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, NULL, 1, &package) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(package == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    for (unsigned field = 0; field < 4; ++field) {
        package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(field == 0 ? NULL : &code, field == 1 ? 0 : 1,
              field == 2 ? NULL : &stage, field == 3 ? 0 : 1, NULL, 0, &package) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_shader_code_destroy_ext(code) == CNA_RESULT_SUCCESS);
    return 0;
}

static int error_contract(void)
{
    CHECK(cna_point_get_zero(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CNA_ErrorInfo original = {sizeof(original), 1, 0, 0, 0};
    CHECK(cna_error_get_last_info(&original) == CNA_RESULT_SUCCESS);
    char original_text[2048], copy[2048]; uint64_t count = 0;
    CHECK(cna_error_copy_last_message(original_text, sizeof(original_text), &count) == CNA_RESULT_SUCCESS);
    CHECK(count == original.message_byte_length);
    for (unsigned mode = 0; mode < 2; ++mode) {
        CNA_ErrorInfo invalid;
        memset(&invalid, 0xa5, sizeof(invalid));
        invalid.struct_size = mode == 0 ? sizeof(invalid) - 1U : sizeof(invalid);
        invalid.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(invalid)]; memcpy(before, &invalid, sizeof(invalid));
        CHECK(cna_error_get_last_info(&invalid) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &invalid, sizeof(invalid)) == 0);
    }
    CHECK(cna_error_get_last_info(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_error_get_last_message_size(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    memset(copy, 0xa5, sizeof(copy));
    CHECK(cna_error_copy_last_message(copy, original.message_byte_length - 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(count == original.message_byte_length && filled(copy, sizeof(copy), 0xa5));
    CHECK(cna_error_copy_last_message(NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == original.message_byte_length);
    CHECK(cna_error_copy_last_message(NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_error_copy_last_message(copy, sizeof(copy), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CNA_Point zero = {1, 2};
    CHECK(cna_point_get_zero(&zero) == CNA_RESULT_SUCCESS && zero.x == 0 && zero.y == 0);
    CHECK(cna_error_copy_last_message(copy, original.message_byte_length, &count) == CNA_RESULT_SUCCESS);
    CHECK(count == original.message_byte_length && memcmp(copy, original_text, (size_t)count) == 0);
    CHECK(filled(copy + count, sizeof(copy) - (size_t)count, 0xa5));
    CNA_ErrorInfo after = {sizeof(after), 1, 0, 0, 0};
    CHECK(cna_error_get_last_info(&after) == CNA_RESULT_SUCCESS);
    CHECK(after.result == original.result && after.category == original.category && after.message_byte_length == original.message_byte_length);
    count = UINT64_MAX;
    CHECK(cna_error_get_last_exception_type_size_ext(&count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_copy_last_exception_type_ext(NULL, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_get_last_exception_param_name_size_ext(&count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_copy_last_exception_param_name_ext(NULL, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    count = 77;
    CHECK(cna_shader_diagnostics_get_count_ext(CNA_INVALID_HANDLE, &count) == CNA_RESULT_INVALID_HANDLE);
    CHECK(count == 77);
    CHECK(last_error(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE) == 0);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) return CNA_TEST_FAIL(1);
    const struct {const char* name; int (*run)(void);} cases[] = {
        {"point", point_contract}, {"rectangle", rectangle_contract}, {"vector", vector_contract},
        {"diagnostics", diagnostic_contract}, {"code", shader_code_contract},
        {"package", shader_package_contract}, {"error", error_contract}
    };
    for (size_t i = 0; i < ARRAY_COUNT(cases); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            int result = cases[i].run();
            if (result == 0) printf("OutputContracts/%s: PASS\n", cases[i].name);
            return result;
        }
    }
    return CNA_TEST_FAIL(1);
}
