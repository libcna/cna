/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include "CnaRuntimeSmoke.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return CNA_TEST_FAIL(1); } while (0)
#define SV(x) (CNA_StringView){x, sizeof(x)-1}
uint64_t cna_test_shader_parse(const char*, uint32_t, uint64_t, CNA_ShaderDiagnosticInfoEXT*, char*, char*);
CNA_Bool cna_test_shader_descriptor(uint32_t);
int cna_test_shader_code_native(uint32_t, uint32_t, const char*, uint64_t, CNA_ShaderCodeInfoEXT*);
int cna_test_shader_worker(int (*function)(void));
const uint8_t* cna_test_shader_spirv(uint32_t stage, uint64_t* byte_count);
static CNA_ShaderCodeHandleEXT thread_code;
static int code_thread_refusal(void)
{
    CNA_ShaderCodeInfoEXT info = {sizeof(info), 1, 99, 99, 99, 99, {0}};
    CHECK(cna_shader_code_get_info_ext(thread_code, &info) == CNA_RESULT_THREAD && info.language == 99);
    return 0;
}
static int identities(void)
{
    CHECK(CNA_SHADER_LANGUAGE_UNKNOWN_EXT == 0 && CNA_SHADER_LANGUAGE_GLSL_DESKTOP_EXT == 1 &&
        CNA_SHADER_LANGUAGE_GLSL_ES_EXT == 2 && CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT == 3 &&
        CNA_SHADER_LANGUAGE_HLSL_EXT == 4 && CNA_SHADER_LANGUAGE_MSL_EXT == 5 && CNA_SHADER_LANGUAGE_WGSL_EXT == 6 &&
        CNA_SHADER_LANGUAGE_SPIRV_EXT == 7 && CNA_SHADER_LANGUAGE_DXIL_EXT == 8 && CNA_SHADER_LANGUAGE_COUNT_EXT == 9);
    CHECK(CNA_SHADER_STAGE_UNKNOWN_EXT == 0 && CNA_SHADER_STAGE_VERTEX_EXT == 1 && CNA_SHADER_STAGE_FRAGMENT_EXT == 2 && CNA_SHADER_STAGE_COUNT_EXT == 3);
    CHECK(CNA_SHADER_DIAGNOSTIC_SEVERITY_INFORMATION_EXT == 0 && CNA_SHADER_DIAGNOSTIC_SEVERITY_WARNING_EXT == 1 && CNA_SHADER_DIAGNOSTIC_SEVERITY_ERROR_EXT == 2 && CNA_SHADER_DIAGNOSTIC_SEVERITY_COUNT_EXT == 3);
    CHECK(CNA_SHADER_BINDING_SAMPLED_TEXTURE2D_EXT == 0 && CNA_SHADER_BINDING_SAMPLED_TEXTURE_CUBE_EXT == 1 && CNA_SHADER_BINDING_SAMPLED_TEXTURE3D_EXT == 2 && CNA_SHADER_BINDING_COUNT_EXT == 3);
    for (uint32_t language = 0; language < 9; ++language) {
        CNA_Bool actual = 99;
        CHECK(cna_shader_language_uses_descriptor_binding_contract_ext(language, &actual) == CNA_RESULT_SUCCESS && actual == cna_test_shader_descriptor(language));
    }
    CNA_Bool output = 99;
    CHECK(cna_shader_language_uses_descriptor_binding_contract_ext(9, &output) == CNA_RESULT_INVALID_ARGUMENT && output == 99);
    CHECK(cna_shader_language_uses_descriptor_binding_contract_ext(UINT32_MAX, &output) == CNA_RESULT_INVALID_ARGUMENT && output == 99);
    CHECK(cna_shader_language_uses_descriptor_binding_contract_ext(0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_graphics_device_supports_shader_language_ext(0, 1, 1, &output) == CNA_RESULT_INVALID_HANDLE && output == 99);
    CHECK(cna_graphics_device_supports_shader_language_ext(0, 9, 1, &output) == CNA_RESULT_INVALID_ARGUMENT && output == 99);
    CHECK(cna_graphics_device_supports_shader_language_ext(0, 1, 3, &output) == CNA_RESULT_INVALID_ARGUMENT && output == 99);
    return 0;
}
static int diagnostics(void)
{
    const char* fixtures[] = {"", "\n\r\n", "ERROR: 0:12:3: invalid value\nWARNING: FS file(7,2): warning\nvertex continuation\ninfo text", "VS: thing:4(6): error\nfragment warning: bad\n  continuation  "};
    for (unsigned fixture = 0; fixture < sizeof(fixtures)/sizeof(fixtures[0]); ++fixture) {
        for (uint32_t stage = 0; stage < 3; ++stage) {
            CNA_ShaderDiagnosticsHandleEXT handle = 99;
            CNA_StringView log = {fixtures[fixture], strlen(fixtures[fixture])};
            CHECK(cna_shader_diagnostics_parse_ext(log, stage, SV("fixture"), &handle) == CNA_RESULT_SUCCESS && handle != 0);
            uint64_t count = 99;
            CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS);
            CHECK(count == cna_test_shader_parse(fixtures[fixture], stage, UINT64_MAX, NULL, NULL, NULL));
            for (uint64_t i = 0; i < count; ++i) {
                CNA_ShaderDiagnosticInfoEXT actual = {sizeof(actual), 1, 99, 99, -1, -1}, native;
                char label[512] = {0}, message[512] = {0}, expected_label[512], expected_message[512];
                uint64_t bytes = 99;
                (void)cna_test_shader_parse(fixtures[fixture], stage, i, &native, expected_label, expected_message);
                CHECK(cna_shader_diagnostics_get_info_ext(handle, i, &actual) == CNA_RESULT_SUCCESS);
                CHECK(actual.severity == native.severity && actual.stage == native.stage && actual.line == native.line && actual.column == native.column);
                CHECK(cna_shader_diagnostics_copy_source_label_ext(handle, i, label, sizeof(label), &bytes) == CNA_RESULT_SUCCESS && bytes == strlen(expected_label) && memcmp(label, expected_label, (size_t)bytes) == 0);
                CHECK(cna_shader_diagnostics_copy_message_ext(handle, i, message, sizeof(message), &bytes) == CNA_RESULT_SUCCESS && bytes == strlen(expected_message) && memcmp(message, expected_message, (size_t)bytes) == 0);
                memset(message, '!', sizeof(message));
                CHECK(cna_shader_diagnostics_copy_message_ext(handle, i, message, bytes-1, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL && message[0] == '!');
            }
            CNA_ShaderDiagnosticInfoEXT sentinel = {sizeof(sentinel), 1, 99, 99, -1, -1};
            CHECK(cna_shader_diagnostics_get_info_ext(handle, count, &sentinel) == CNA_RESULT_INVALID_ARGUMENT && sentinel.severity == 99);
            CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
            CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_INVALID_HANDLE);
        }
    }
    CNA_ShaderDiagnosticInfoEXT info = {sizeof(info), 1, 1, 2, 7, 3};
    CNA_ShaderDiagnosticsHandleEXT handle = 99;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV("label"), SV("warning"), &handle) == CNA_RESULT_SUCCESS);
    uint64_t single_count = 99;
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &single_count) == CNA_RESULT_SUCCESS && single_count == 1);
    CNA_ShaderDiagnosticInfoEXT single = {sizeof(single), 1, 99, 99, -1, -1};
    CHECK(cna_shader_diagnostics_get_info_ext(handle, 0, &single) == CNA_RESULT_SUCCESS && single.severity == 1 && single.stage == 2 && single.line == 7 && single.column == 3);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    info.severity = 3; handle = 99;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV(""), SV("warning"), &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == 0);
    info.severity = 1; info.line = -1;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV(""), SV("warning"), &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == 0);
    info.line = 0;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV(""), SV(""), &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == 0);
    CHECK(cna_shader_diagnostics_parse_ext(SV("\xff"), 0, SV(""), &handle) == CNA_RESULT_ENCODING && handle == 0);
    CHECK(cna_shader_diagnostics_parse_ext(SV("a\0b"), 0, SV(""), &handle) == CNA_RESULT_ENCODING && handle == 0);
    CHECK(cna_shader_diagnostics_parse_ext(SV(""), 3, SV(""), &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == 0);
    return 0;
}
static int code_values(void)
{
    for (uint32_t language = 1; language < 9; ++language) {
        for (uint32_t stage = 1; stage < 3; ++stage) {
            CNA_ShaderCodeHandleEXT code = 0;
            const char payload[] = "code";
            CNA_Result result = language < 7 ? cna_shader_code_create_text_ext(language, stage, SV("main"), SV("code"), SV("code"), &code) : cna_shader_code_create_binary_ext(language, stage, SV("main"), SV("code"), (const uint8_t*)payload, 4, &code);
            CHECK(result == CNA_RESULT_SUCCESS && code != 0);
            CNA_ShaderCodeInfoEXT info = {sizeof(info), 1, 99, 99, 99, 99, {0}}, native;
            CHECK(cna_test_shader_code_native(language, stage, payload, 4, &native));
            CHECK(cna_shader_code_get_info_ext(code, &info) == CNA_RESULT_SUCCESS && info.language == native.language && info.stage == native.stage && info.payload_byte_count == native.payload_byte_count && info.is_text == native.is_text);
            char buffer[10] = {0}; uint64_t bytes = 0;
            CHECK(cna_shader_code_copy_entry_point_ext(code, buffer, sizeof(buffer), &bytes) == CNA_RESULT_SUCCESS && bytes == 4 && memcmp(buffer, "main", 4) == 0);
            CHECK(cna_shader_code_copy_source_label_ext(code, buffer, sizeof(buffer), &bytes) == CNA_RESULT_SUCCESS && bytes == 4 && memcmp(buffer, "code", 4) == 0);
            if (language < 7) {
                CHECK(cna_shader_code_copy_text_ext(code, buffer, sizeof(buffer), &bytes) == CNA_RESULT_SUCCESS && bytes == 4 && memcmp(buffer, payload, 4) == 0);
                memset(buffer, '!', sizeof(buffer));
                CHECK(cna_shader_code_copy_text_ext(code, buffer, 3, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL && buffer[0] == '!');
                CHECK(cna_shader_code_copy_bytes_ext(code, (uint8_t*)buffer, sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS && buffer[0] == '!');
            } else {
                CHECK(cna_shader_code_copy_bytes_ext(code, (uint8_t*)buffer, sizeof(buffer), &bytes) == CNA_RESULT_SUCCESS && bytes == 4 && memcmp(buffer, payload, 4) == 0);
                memset(buffer, '!', sizeof(buffer));
                CHECK(cna_shader_code_copy_bytes_ext(code, (uint8_t*)buffer, 3, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL && buffer[0] == '!');
                CHECK(cna_shader_code_copy_text_ext(code, buffer, sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS && buffer[0] == '!');
            }
            thread_code = code;
            CHECK(cna_test_shader_worker(code_thread_refusal) == 0);
            CHECK(cna_shader_code_destroy_ext(code) == CNA_RESULT_SUCCESS);
            CHECK(cna_shader_code_get_info_ext(code, &info) == CNA_RESULT_INVALID_HANDLE);
        }
    }
    CNA_ShaderCodeHandleEXT code = 99;
    CHECK(cna_shader_code_create_text_ext(7, 1, SV("main"), SV(""), SV("x"), &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_text_ext(UINT32_MAX, 1, SV("main"), SV(""), SV("x"), &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_text_ext(1, 0, SV("main"), SV(""), SV("x"), &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_text_ext(1, 1, SV(""), SV(""), SV("x"), &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_text_ext(1, 1, SV("main"), SV(""), SV(""), &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_text_ext(1, 1, SV("main"), SV(""), SV("\xff"), &code) == CNA_RESULT_ENCODING && code == 0);
    CHECK(cna_shader_code_create_binary_ext(7, 1, SV("main"), SV(""), (const uint8_t*)"abc", 3, &code) == CNA_RESULT_INVALID_ARGUMENT && code == 0);
    CHECK(cna_shader_code_create_binary_ext(7, 1, SV("main"), SV(""), (const uint8_t*)"abcd", UINT64_MAX, &code) == CNA_RESULT_OVERFLOW && code == 0);
    return 0;
}
static CNA_ShaderCodeHandleEXT variants[2];
static CNA_ShaderPackageHandleEXT saved_package;
static int package_values(void)
{
    CNA_ShaderStageEXT stages[2] = {1, 2};
    for (unsigned i = 0; i < 2; ++i)
        CHECK(cna_shader_code_create_text_ext(3, stages[i], SV("main"), SV("variant"), SV("code"), &variants[i]) == CNA_RESULT_SUCCESS);
    CNA_ShaderBindingRequirementEXT binding = {sizeof(binding), 1, SV("volume"), 2, CNA_SHADER_BINDING_SAMPLED_TEXTURE3D_EXT, 2};
    CHECK(cna_shader_package_create_ext(variants, 2, stages, 2, &binding, 1, &saved_package) == CNA_RESULT_SUCCESS);
    uint64_t count = 99;
    CHECK(cna_shader_package_get_variant_count_ext(saved_package, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_shader_package_get_required_stage_count_ext(saved_package, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_shader_package_get_binding_count_ext(saved_package, &count) == CNA_RESULT_SUCCESS && count == 1);
    CNA_ShaderStageEXT stage = 99; CNA_Bool required = 99;
    CHECK(cna_shader_package_get_required_stage_ext(saved_package, 1, &stage) == CNA_RESULT_SUCCESS && stage == 2);
    CHECK(cna_shader_package_get_required_stage_ext(saved_package, 2, &stage) == CNA_RESULT_INVALID_ARGUMENT && stage == 2);
    CHECK(cna_shader_package_requires_stage_ext(saved_package, 1, &required) == CNA_RESULT_SUCCESS && required == CNA_TRUE);
    CHECK(cna_shader_package_requires_stage_ext(saved_package, UINT32_MAX, &required) == CNA_RESULT_SUCCESS && required == CNA_FALSE);
    CNA_ShaderBindingRequirementEXT actual = {sizeof(actual), 1, {NULL, 0}, -1, 99, 99};
    CHECK(cna_shader_package_get_binding_ext(saved_package, 0, &actual) == CNA_RESULT_SUCCESS && actual.name.data == NULL && actual.name.byte_length == 0 && actual.binding == 2 && actual.type == binding.type && actual.stage == 2);
    char name[16]; memset(name, '!', sizeof(name));
    CHECK(cna_shader_package_copy_binding_name_ext(saved_package, 0, name, 5, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == 6 && name[0] == '!');
    CHECK(cna_shader_package_copy_binding_name_ext(saved_package, 0, name, sizeof(name), &count) == CNA_RESULT_SUCCESS && count == 6 && memcmp(name, "volume", 6) == 0);
    CHECK(cna_shader_package_get_binding_ext(saved_package, 1, &actual) == CNA_RESULT_INVALID_ARGUMENT && actual.binding == 2);
    CNA_ShaderCodeHandleEXT child = 99;
    CHECK(cna_shader_package_get_variant_ext(saved_package, 0, &child) == CNA_RESULT_SUCCESS && child != variants[0]);
    CHECK(cna_shader_code_destroy_ext(child) == CNA_RESULT_SUCCESS);
    CNA_ShaderSelectionHandleEXT selection = 99;
    CHECK(cna_shader_package_select_ext(saved_package, 0, &selection) == CNA_RESULT_INVALID_HANDLE && selection == 0);
    CNA_ShaderPackageHandleEXT bad = 99;
    CHECK(cna_shader_package_create_ext(variants, 2, stages, 2, NULL, UINT64_MAX, &bad) == CNA_RESULT_INVALID_ARGUMENT && bad == 0);
    CHECK(cna_shader_package_create_ext(variants, UINT64_MAX, stages, 2, NULL, 0, &bad) == CNA_RESULT_OVERFLOW && bad == 0);
    CHECK(cna_shader_package_create_ext(variants, 2, stages, 0, NULL, 0, &bad) == CNA_RESULT_INVALID_ARGUMENT && bad == 0);
    CHECK(cna_shader_package_create_ext(variants, 1, stages, 2, NULL, 0, &bad) == CNA_RESULT_INVALID_ARGUMENT && bad == 0);
    stages[1] = stages[0];
    CHECK(cna_shader_package_create_ext(variants, 2, stages, 2, NULL, 0, &bad) == CNA_RESULT_INVALID_ARGUMENT && bad == 0);
    stages[1] = 2;
    CNA_ShaderBindingRequirementEXT collision[2] = {binding, binding};
    CHECK(cna_shader_package_create_ext(variants, 2, stages, 2, collision, 2, &bad) == CNA_RESULT_INVALID_ARGUMENT && bad == 0);
    CNA_EffectHandle effect = 99; CNA_ShaderDiagnosticsHandleEXT diagnostics_handle = 99;
    CHECK(cna_shader_effect_create_from_code_ext(0, variants[0], variants[1], &effect, &diagnostics_handle) == CNA_RESULT_INVALID_HANDLE && effect == 0 && diagnostics_handle == 0);
    CHECK(cna_shader_effect_create_from_package_ext(0, saved_package, &effect, NULL) == CNA_RESULT_INVALID_HANDLE && effect == 0);
    CHECK(cna_shader_effect_create_from_package_with_fallback_ext(0, saved_package, SV(""), SV(""), &effect, NULL) == CNA_RESULT_INVALID_HANDLE && effect == 0);
    CHECK(cna_shader_effect_get_selected_language_ext(0, &stage) == CNA_RESULT_INVALID_HANDLE && stage == 2);
    CHECK(cna_shader_effect_get_diagnostics_ext(0, &diagnostics_handle) == CNA_RESULT_INVALID_HANDLE && diagnostics_handle == 0);
    CHECK(cna_shader_code_destroy_ext(variants[0]) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_code_destroy_ext(variants[1]) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_package_get_variant_ext(saved_package, 1, &child) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_package_destroy_ext(saved_package) == CNA_RESULT_SUCCESS);
    CNA_ShaderCodeInfoEXT info = {sizeof(info), 1, 0, 0, 0, 0, {0}};
    CHECK(cna_shader_code_get_info_ext(child, &info) == CNA_RESULT_SUCCESS && info.stage == 2);
    CHECK(cna_shader_code_destroy_ext(child) == CNA_RESULT_SUCCESS);
    return 0;
}
static int shader_runtime(void)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;

    CNA_PresentationParameters pp;
    CNA_Handle device = 0;
    variants[0] = variants[1] = saved_package = 0;
    RUNTIME_CHECK(SMOKE_API(cna_presentation_parameters_init(&pp)) == CNA_RESULT_SUCCESS);
    pp.is_full_screen = CNA_FALSE;
    pp.back_buffer_width = 64;
    pp.back_buffer_height = 64;
    RUNTIME_CHECK(SMOKE_OWN(cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_HI_DEF, &pp, &device), &device,
                            cna_graphics_device_destroy) == CNA_RESULT_SUCCESS);
    for (uint32_t language = 0; language < 9; ++language)
        for (uint32_t stage = 0; stage < 3; ++stage) {
            CNA_Bool supported = 99;
            RUNTIME_CHECK(SMOKE_API(cna_graphics_device_supports_shader_language_ext(
                              device, language, stage, &supported)) == CNA_RESULT_SUCCESS &&
                          SMOKE_VALUE(supported) <= CNA_TRUE);
            if (!language || !stage)
                RUNTIME_CHECK(SMOKE_VALUE(supported) == CNA_FALSE);
        }
    const char vertex[] = "#version 450\nlayout(location=0) in vec3 "
                          "position;void main(){gl_Position=vec4(position,1.0);}";
    const char fragment[] = "#version 450\nlayout(location=0) out vec4 "
                            "color;void main(){color=vec4(1.0,0.0,0.0,1.0);}";
    CNA_ShaderStageEXT stages[] = {CNA_SHADER_STAGE_VERTEX_EXT, CNA_SHADER_STAGE_FRAGMENT_EXT};
    CNA_Bool custom_effects = 99, vulkan_vertex = 99, vulkan_fragment = 99;
    RUNTIME_CHECK(
        SMOKE_API(cna_graphics_device_supports_capability(device, CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS,
                                                          &custom_effects)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_supports_shader_language_ext(
                      device, CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT, stages[0], &vulkan_vertex)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_supports_shader_language_ext(
                      device, CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT, stages[1], &vulkan_fragment)) ==
                  CNA_RESULT_SUCCESS);
    const CNA_Bool vulkan_usable = custom_effects && vulkan_vertex && vulkan_fragment;
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_code_create_text_ext(
                                CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT, stages[0], SV("main"), SV("vertex"),
                                (CNA_StringView){vertex, sizeof(vertex) - 1}, &variants[0]),
                            &variants[0], cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_code_create_text_ext(
                                CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT, stages[1], SV("main"), SV("fragment"),
                                (CNA_StringView){fragment, sizeof(fragment) - 1}, &variants[1]),
                            &variants[1], cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_create_ext(variants, 2, stages, 2, NULL, 0, &saved_package),
                            &saved_package, cna_shader_package_destroy_ext) == CNA_RESULT_SUCCESS);
    CNA_ShaderSelectionHandleEXT selection = 0;
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_select_ext(saved_package, device, &selection), &selection,
                            cna_shader_selection_destroy_ext) == CNA_RESULT_SUCCESS);
    CNA_Bool usable = 99, found = 99;
    uint32_t language = 99;
    uint64_t count = 99;
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_is_usable_ext(selection, &usable)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(usable) == SMOKE_VALUE(vulkan_usable));
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_language_ext(selection, &language)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(language) == SMOKE_VALUE((vulkan_usable ? CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT
                                                                      : CNA_SHADER_LANGUAGE_UNKNOWN_EXT)));
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_code_count_ext(selection, &count)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(count) == SMOKE_VALUE((vulkan_usable ? 2U : 0U)));
    CNA_EffectHandle effect = 0;
    CNA_ShaderDiagnosticsHandleEXT diagnostic = 0;
    if (!vulkan_usable) {
        RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_code_ext(device, variants[0], variants[1],
                                                                           &effect, &diagnostic),
                                    &effect, cna_effect_destroy, &diagnostic,
                                    cna_shader_diagnostics_destroy_ext) == CNA_RESULT_NOT_SUPPORTED &&
                      SMOKE_VALUE(effect) == 0 && SMOKE_VALUE(diagnostic) != 0);
        RUNTIME_CHECK(SMOKE_API(cna_shader_diagnostics_get_count_ext(diagnostic, &count)) ==
                          CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(count) == 2);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_diagnostics_destroy_ext(diagnostic), diagnostic) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_package_ext(device, saved_package, &effect,
                                                                              &diagnostic),
                                    &effect, cna_effect_destroy, &diagnostic,
                                    cna_shader_diagnostics_destroy_ext) == CNA_RESULT_NOT_SUPPORTED &&
                      SMOKE_VALUE(effect) == 0 && SMOKE_VALUE(diagnostic) != 0);
        RUNTIME_CHECK(SMOKE_API(cna_shader_diagnostics_get_count_ext(diagnostic, &count)) ==
                          CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(count) == 2);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_diagnostics_destroy_ext(diagnostic), diagnostic) ==
                      CNA_RESULT_SUCCESS);
        puts("Vulkan GLSL typed package: native refusal and full diagnostics "
             "verified");
    }
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_selection_destroy_ext(selection), selection) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_package_destroy_ext(saved_package), saved_package) ==
                  CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 2; ++i)
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(variants[i]), variants[i]) ==
                      CNA_RESULT_SUCCESS);

    /* Explicit SDLGPU code intake uses SPIR-V, independent of legacy GLSL source
     * compilation. */
    CNA_Bool spirv_vertex = 99, spirv_fragment = 99;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_supports_shader_language_ext(
                      device, CNA_SHADER_LANGUAGE_SPIRV_EXT, stages[0], &spirv_vertex)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_supports_shader_language_ext(
                      device, CNA_SHADER_LANGUAGE_SPIRV_EXT, stages[1], &spirv_fragment)) ==
                  CNA_RESULT_SUCCESS);
    const CNA_Bool spirv_usable = custom_effects && spirv_vertex && spirv_fragment;
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t bytes = 0;
        const uint8_t* payload = cna_test_shader_spirv(stages[i], &bytes);
        RUNTIME_CHECK(
            SMOKE_OWN(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_SPIRV_EXT, stages[i], SV("main"),
                                                        SV("spirv fixture"), payload, bytes, &variants[i]),
                      &variants[i], cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    }
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_create_ext(variants, 2, stages, 2, NULL, 0, &saved_package),
                            &saved_package, cna_shader_package_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_select_ext(saved_package, device, &selection), &selection,
                            cna_shader_selection_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_is_usable_ext(selection, &usable)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(usable) == SMOKE_VALUE(spirv_usable));
    RUNTIME_CHECK(
        SMOKE_API(cna_shader_selection_get_language_ext(selection, &language)) == CNA_RESULT_SUCCESS &&
        SMOKE_VALUE(language) ==
            SMOKE_VALUE((spirv_usable ? CNA_SHADER_LANGUAGE_SPIRV_EXT : CNA_SHADER_LANGUAGE_UNKNOWN_EXT)));
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_code_count_ext(selection, &count)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(count) == SMOKE_VALUE((spirv_usable ? 2U : 0U)));
    CNA_ShaderCodeHandleEXT child = 0;
    char text[2048];
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_copy_diagnostic_ext(selection, text, sizeof(text),
                                                                     &count)) == CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(count) > 0);
    if (spirv_usable) {
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_selection_find_stage_ext(selection, stages[0], &child, &found),
                                &child, cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(found) == CNA_TRUE && SMOKE_VALUE(child) != 0);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(child), child) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_selection_find_stage_ext(selection, UINT32_MAX, &child, &found),
                                &child, cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(found) == CNA_FALSE && SMOKE_VALUE(child) == 0);
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_selection_get_code_ext(selection, 1, &child), &child,
                                cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(child), child) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_code_ext(device, variants[0], variants[1],
                                                                           &effect, &diagnostic),
                                    &effect, cna_effect_destroy, &diagnostic,
                                    cna_shader_diagnostics_destroy_ext) == CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(effect) != 0 && SMOKE_VALUE(diagnostic) == 0);
        RUNTIME_CHECK(SMOKE_API(cna_shader_effect_get_selected_language_ext(effect, &language)) ==
                          CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(language) == CNA_SHADER_LANGUAGE_SPIRV_EXT);
        RUNTIME_CHECK(SMOKE_API(cna_shader_effect_is_valid(effect, &usable)) == CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(usable) == CNA_TRUE);
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_effect_get_diagnostics_ext(effect, &diagnostic), &diagnostic,
                                cna_shader_diagnostics_destroy_ext) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_shader_diagnostics_get_count_ext(diagnostic, &count)) ==
                          CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(count) == 0);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_diagnostics_destroy_ext(diagnostic), diagnostic) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_clear_rgba(device, 0, 0, 0, 1)) == CNA_RESULT_SUCCESS);
        CNA_RasterizerState rasterizer;
        RUNTIME_CHECK(SMOKE_API(cna_rasterizer_state_init(CNA_RASTERIZER_STATE_PRESET_CULL_NONE,
                                                          &rasterizer)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_rasterizer_state(device, &rasterizer)) ==
                      CNA_RESULT_SUCCESS);
        CNA_DepthStencilState depth;
        RUNTIME_CHECK(SMOKE_API(cna_depth_stencil_state_init(CNA_DEPTH_STENCIL_STATE_PRESET_NONE, &depth)) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_depth_stencil_state(device, &depth)) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_effect_apply(effect)) == CNA_RESULT_SUCCESS);
        const CNA_VertexPositionColor triangle[] = {{{-0.8F, -0.8F, 0.0F}, {255, 255, 255, 255}},
                                                    {{0.8F, -0.8F, 0.0F}, {255, 255, 255, 255}},
                                                    {{0.0F, 0.8F, 0.0F}, {255, 255, 255, 255}}};
        const CNA_UserPrimitives draw = {sizeof(draw),
                                         1,
                                         CNA_PRIMITIVE_TRIANGLE_LIST,
                                         CNA_USER_VERTEX_SOURCE_POSITION_COLOR,
                                         triangle,
                                         0,
                                         0,
                                         3,
                                         1,
                                         0};
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_draw_user_primitives(device, &draw)) ==
                      CNA_RESULT_SUCCESS);
        CNA_BackBufferReadback rb = {sizeof(rb), 1, CNA_TRUE, {0}, {32, 32, 1, 1}, 0, 1};
        CNA_Color pixel = {0, 0, 0, 0};
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(device, &rb, 4, &pixel, 1)) ==
                          CNA_RESULT_SUCCESS &&
                      SMOKE_VALUE(pixel.r) == 255 && SMOKE_VALUE(pixel.g) == 0 && SMOKE_VALUE(pixel.b) == 0);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_current_effect(device, 0)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_effect_destroy(effect), effect) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(
            SMOKE_OWN_TWO(cna_shader_effect_create_from_package_ext(device, saved_package, &effect, NULL),
                          &effect, cna_effect_destroy, NULL,
                          cna_shader_diagnostics_destroy_ext) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_effect_destroy(effect), effect) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_package_with_fallback_ext(
                                        device, saved_package, SV("invalid fallback"), SV("invalid fallback"),
                                        &effect, NULL),
                                    &effect, cna_effect_destroy, NULL,
                                    cna_shader_diagnostics_destroy_ext) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_effect_destroy(effect), effect) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_package_destroy_ext(saved_package), saved_package) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_selection_get_code_ext(selection, 0, &child), &child,
                                cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(child), child) == CNA_RESULT_SUCCESS);
    } else {
        RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_package_ext(device, saved_package, &effect,
                                                                              &diagnostic),
                                    &effect, cna_effect_destroy, &diagnostic,
                                    cna_shader_diagnostics_destroy_ext) == CNA_RESULT_NOT_SUPPORTED &&
                      SMOKE_VALUE(effect) == 0 && SMOKE_VALUE(diagnostic) != 0);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_diagnostics_destroy_ext(diagnostic), diagnostic) ==
                      CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_package_destroy_ext(saved_package), saved_package) ==
                      CNA_RESULT_SUCCESS);
        puts("SKIP SPIR-V compilation/draw: native package capability refusal "
             "verified");
    }
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_selection_destroy_ext(selection), selection) == CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 2; ++i)
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(variants[i]), variants[i]) ==
                      CNA_RESULT_SUCCESS);

    /* Unsupported DXIL stays inspectable, including exception snapshots and
     * legacy fallback. */
    const uint8_t binary[] = {1, 2, 3, 4};
    for (unsigned i = 0; i < 2; ++i)
        RUNTIME_CHECK(SMOKE_OWN(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_DXIL_EXT, stages[i],
                                                                  SV("main"), SV("unsupported"), binary,
                                                                  sizeof(binary), &variants[i]),
                                &variants[i], cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_create_ext(variants, 2, stages, 2, NULL, 0, &saved_package),
                            &saved_package, cna_shader_package_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_package_select_ext(saved_package, device, &selection), &selection,
                            cna_shader_selection_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_is_usable_ext(selection, &usable)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(usable) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_language_ext(selection, &language)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(language) == CNA_SHADER_LANGUAGE_UNKNOWN_EXT);
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_get_code_count_ext(selection, &count)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(count) == 0);
    RUNTIME_CHECK(SMOKE_OWN(cna_shader_selection_find_stage_ext(selection, stages[0], &child, &found), &child,
                            cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(found) == CNA_FALSE && SMOKE_VALUE(child) == 0);
    memset(text, '!', sizeof(text));
    RUNTIME_CHECK(SMOKE_API(cna_shader_selection_copy_diagnostic_ext(selection, text, 1, &count)) ==
                      CNA_RESULT_BUFFER_TOO_SMALL &&
                  SMOKE_VALUE(text[0]) == '!' && SMOKE_VALUE(count) > 1);
    RUNTIME_CHECK(
        SMOKE_OWN_TWO(cna_shader_effect_create_from_package_ext(device, saved_package, &effect, &diagnostic),
                      &effect, cna_effect_destroy, &diagnostic,
                      cna_shader_diagnostics_destroy_ext) == CNA_RESULT_NOT_SUPPORTED &&
        SMOKE_VALUE(effect) == 0 && SMOKE_VALUE(diagnostic) != 0);
    RUNTIME_CHECK(SMOKE_API(cna_shader_diagnostics_get_count_ext(diagnostic, &count)) == CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(count) == 2);
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_diagnostics_destroy_ext(diagnostic), diagnostic) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN_TWO(cna_shader_effect_create_from_package_with_fallback_ext(
                                    device, saved_package, (CNA_StringView){vertex, sizeof(vertex) - 1},
                                    (CNA_StringView){fragment, sizeof(fragment) - 1}, &effect, NULL),
                                &effect, cna_effect_destroy, NULL,
                                cna_shader_diagnostics_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_shader_effect_get_selected_language_ext(effect, &language)) ==
                      CNA_RESULT_SUCCESS &&
                  SMOKE_VALUE(language) == CNA_SHADER_LANGUAGE_UNKNOWN_EXT);
    RUNTIME_CHECK(SMOKE_DROP(cna_effect_destroy(effect), effect) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_selection_destroy_ext(selection), selection) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_shader_package_destroy_ext(saved_package), saved_package) ==
                  CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 2; ++i)
        RUNTIME_CHECK(SMOKE_DROP(cna_shader_code_destroy_ext(variants[i]), variants[i]) ==
                      CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_graphics_device_destroy(device), device) == CNA_RESULT_SUCCESS);
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0)
        status = 1;
    return status;
}
static int runtime_cleanup_probe(CNA_ShaderCodeHandleEXT* first, CNA_ShaderCodeHandleEXT* second,
                                 int scalar_failure)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    RUNTIME_CHECK(
        SMOKE_OWN(cna_shader_code_create_text_ext(1, 1, SV("main"), SV("cleanup first"), SV("x"), first),
                  first, cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(
        SMOKE_OWN(cna_shader_code_create_text_ext(1, 2, SV("main"), SV("cleanup second"), SV("x"), second),
                  second, cna_shader_code_destroy_ext) == CNA_RESULT_SUCCESS);
    if (scalar_failure) {
        CNA_ShaderCodeInfoEXT info = {sizeof(info), 1, 99, 99, 99, 99, {0}};
        RUNTIME_CHECK(SMOKE_API(cna_shader_code_get_info_ext(*first, &info)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_VALUE(info.language) == CNA_SHADER_LANGUAGE_DXIL_EXT);
    } else {
        RUNTIME_CHECK(SMOKE_API(cna_shader_code_get_info_ext(*first, NULL)) == CNA_RESULT_SUCCESS);
    }
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0)
        return 2;
    return status;
}
static int runtime_cleanup_self_test(void)
{
    for (int scalar_failure = 0; scalar_failure < 2; ++scalar_failure) {
        CNA_ShaderCodeHandleEXT first = 0, second = 0;
        fprintf(stderr, "Expected assertion failure follows (cleanup self-test %d):\n", scalar_failure);
        CHECK(runtime_cleanup_probe(&first, &second, scalar_failure) == 1);
        CHECK(first != 0 && second != 0);
        CNA_ShaderCodeInfoEXT info = {sizeof(info), 1, 99, 99, 99, 99, {0}};
        CHECK(cna_shader_code_get_info_ext(first, &info) == CNA_RESULT_INVALID_HANDLE && info.language == 99);
        CHECK(cna_shader_code_get_info_ext(second, &info) == CNA_RESULT_INVALID_HANDLE &&
              info.language == 99);
    }
    puts("Runtime cleanup/diagnostic self-test: PASS (2 expected failures, no "
         "live handles)");
    return 0;
}
int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--check-cleanup") == 0) return runtime_cleanup_self_test();
    CHECK(identities() == 0); CHECK(cna_test_shader_worker(identities) == 0);
    CHECK(diagnostics() == 0);
    CNA_ShaderCodeHandleEXT feature_probe = 99;
    CNA_Result feature = cna_shader_code_create_text_ext(1, 1, SV("main"), SV(""), SV("x"), &feature_probe);
    if (feature == CNA_RESULT_NOT_SUPPORTED) {
        CHECK(feature_probe == 0); puts("SKIP: shader package feature disabled"); return 0;
    }
    CHECK(feature == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_code_destroy_ext(feature_probe) == CNA_RESULT_SUCCESS);
    CHECK(code_values() == 0); CHECK(package_values() == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0) CHECK(shader_runtime() == 0);
    puts("ShaderSmoke: PASS"); return 0;
}
