// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView value;
    value.data = text;
    value.byte_length = (uint64_t)strlen(text);
    return value;
}

static int validate_levels_and_categories(void)
{
    static const CNA_LogCategory categories[9] = {
        CNA_LOG_CATEGORY_APPLICATION, CNA_LOG_CATEGORY_ERROR, CNA_LOG_CATEGORY_SYSTEM,
        CNA_LOG_CATEGORY_AUDIO, CNA_LOG_CATEGORY_VIDEO, CNA_LOG_CATEGORY_RENDER,
        CNA_LOG_CATEGORY_INPUT, CNA_LOG_CATEGORY_TEST, CNA_LOG_CATEGORY_GPU
    };
    static const CNA_LogLevel levels[7] = {
        CNA_LOG_LEVEL_FATAL, CNA_LOG_LEVEL_ERROR, CNA_LOG_LEVEL_WARN, CNA_LOG_LEVEL_INFO,
        CNA_LOG_LEVEL_DEBUG, CNA_LOG_LEVEL_TRACE, CNA_LOG_LEVEL_EXPERIMENT
    };
    size_t index = 0U;

    for (index = 0U; index < 9U; ++index) {
        if (cna_logger_log(
                CNA_LOG_LEVEL_TRACE, view("category"), categories[index], CNA_FALSE) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    for (index = 0U; index < 7U; ++index) {
        if (cna_logger_log(
                levels[index], view("level"), CNA_LOG_CATEGORY_TEST, CNA_FALSE) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* The canonical experiment level is 100, so 6 is not an identity. */
    return cna_logger_log(UINT32_C(6), view("bad"), CNA_LOG_CATEGORY_TEST, CNA_FALSE) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_logger_log(CNA_LOG_LEVEL_INFO, view("bad"), UINT32_C(9), CNA_FALSE) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_logger_routes(void)
{
    CNA_StringView bad;
    bad.data = 0;
    bad.byte_length = UINT64_C(4);

    /* Every route below is exercised with the minimum level raised to FATAL, so nothing but the
       fatal routes actually reaches the log sink. The validation still runs. */
    return cna_logger_fatal(view("fatal"), CNA_LOG_CATEGORY_TEST) == CNA_RESULT_SUCCESS &&
        cna_logger_error(view("error"), CNA_LOG_CATEGORY_ERROR) == CNA_RESULT_SUCCESS &&
        cna_logger_warn(view("warn"), CNA_LOG_CATEGORY_SYSTEM) == CNA_RESULT_SUCCESS &&
        cna_logger_info(view("info"), CNA_LOG_CATEGORY_APPLICATION) == CNA_RESULT_SUCCESS &&
        cna_logger_debug(view("debug"), CNA_LOG_CATEGORY_RENDER) == CNA_RESULT_SUCCESS &&
        cna_logger_trace(view("trace"), CNA_LOG_CATEGORY_INPUT) == CNA_RESULT_SUCCESS &&
        cna_logger_experiment(view("experiment"), CNA_LOG_CATEGORY_GPU) == CNA_RESULT_SUCCESS &&
        cna_logger_fatal_if(view("fatal-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_error_if(view("error-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_warn_if(view("warn-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_info_if(view("info-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_debug_if(view("debug-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_trace_if(view("trace-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_experiment_if(view("experiment-if"), CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_logger_info(bad, CNA_LOG_CATEGORY_TEST) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_logger_info_if(bad, CNA_TRUE) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_logger_info(view("bad"), UINT32_C(9)) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_minimum_level(void)
{
    CNA_LogLevel restored = CNA_LOG_LEVEL_INFO;
    CNA_LogLevel level = UINT32_C(999);
    int ok = 0;

    if (cna_logger_get_minimum_level(&restored) != CNA_RESULT_SUCCESS ||
        cna_logger_get_minimum_level(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_logger_set_minimum_level(UINT32_C(6)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    ok = cna_logger_set_minimum_level(CNA_LOG_LEVEL_FATAL) == CNA_RESULT_SUCCESS &&
        cna_logger_get_minimum_level(&level) == CNA_RESULT_SUCCESS &&
        level == CNA_LOG_LEVEL_FATAL &&
        validate_logger_routes() &&
        cna_logger_set_minimum_level(CNA_LOG_LEVEL_EXPERIMENT) == CNA_RESULT_SUCCESS &&
        cna_logger_get_minimum_level(&level) == CNA_RESULT_SUCCESS &&
        level == CNA_LOG_LEVEL_EXPERIMENT;
    /* The canonical minimum level is process-wide static state, so it is put back. */
    return cna_logger_set_minimum_level(restored) == CNA_RESULT_SUCCESS && ok;
}

static int validate_platform(void)
{
    CNA_Platform platform = UINT32_C(999);
    CNA_DesktopOS os = UINT32_C(999);

    if (cna_platform_get_current(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_platform_get_current(&platform) != CNA_RESULT_SUCCESS ||
        platform > CNA_PLATFORM_WEB) {
        return 0;
    }
    if (cna_desktop_os_get_current(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (platform == CNA_PLATFORM_DESKTOP) {
        return cna_desktop_os_get_current(&os) == CNA_RESULT_SUCCESS && os <= CNA_DESKTOP_OS_OTHER;
    }
    /* Off the desktop the canonical query throws rather than returning a fallback. */
    return cna_desktop_os_get_current(&os) == CNA_RESULT_INVALID_STATE;
}

static int validate_names(void)
{
    static const char* const category_names[5] = {
        "Native", "TranslationLayer", "Software", "Web", "Diagnostic"
    };
    static const char* const maturity_names[5] = {
        "Production", "Supported", "Experimental", "Historical", "Deprecated"
    };
    char buffer[32];
    uint64_t bytes = UINT64_C(0);
    uint32_t index = 0U;

    for (index = 0U; index < 5U; ++index) {
        memset(buffer, 0, sizeof(buffer));
        if (cna_graphics_backend_category_get_name_size(index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes != (uint64_t)strlen(category_names[index]) ||
            cna_graphics_backend_category_copy_name(
                index, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
            strcmp(buffer, category_names[index]) != 0) {
            return 0;
        }
        memset(buffer, 0, sizeof(buffer));
        if (cna_graphics_backend_maturity_get_name_size(index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes != (uint64_t)strlen(maturity_names[index]) ||
            cna_graphics_backend_maturity_copy_name(
                index, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
            strcmp(buffer, maturity_names[index]) != 0) {
            return 0;
        }
    }

    /* A short capacity reports the requirement and writes nothing. */
    memset(buffer, 0, sizeof(buffer));
    bytes = UINT64_C(0);
    if (cna_graphics_backend_category_copy_name(
            CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER, buffer, UINT64_C(4), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != UINT64_C(16) || buffer[0] != '\0') {
        return 0;
    }
    return cna_graphics_backend_category_get_name_size(UINT32_C(5), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_graphics_backend_maturity_get_name_size(UINT32_C(5), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_graphics_backend_category_get_name_size(
            CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_graphics_backend_maturity_copy_name(
            CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION, buffer, UINT64_C(32), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_backend_classification(void)
{
    CNA_GraphicsBackendCategory category = UINT32_C(999);
    CNA_GraphicsBackendMaturity maturity = UINT32_C(999);
    CNA_GraphicsRendererType type = UINT32_C(999);
    CNA_GraphicsRendererType identity = UINT32_C(0);

    /* Every public identity classifies, not only the one this build compiled in. */
    for (identity = UINT32_C(1); identity <= UINT32_C(46); ++identity) {
        if (cna_graphics_backend_get_category(identity, &category) != CNA_RESULT_SUCCESS ||
            category > CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC ||
            cna_graphics_backend_get_maturity(identity, &maturity) != CNA_RESULT_SUCCESS ||
            maturity > CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED) {
            return 0;
        }
    }
    if (cna_graphics_backend_get_category(CNA_GRAPHICS_RENDERER_UNKNOWN, &category) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_backend_get_maturity(UINT32_C(47), &maturity) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_backend_get_category(CNA_GRAPHICS_RENDERER_HEADLESS, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_backend_get_maturity(CNA_GRAPHICS_RENDERER_HEADLESS, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* A fixed classification a caller can rely on without naming this build's renderer. */
    if (cna_graphics_backend_get_category(CNA_GRAPHICS_RENDERER_HEADLESS, &category) !=
            CNA_RESULT_SUCCESS ||
        category != CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC ||
        cna_graphics_backend_get_category(CNA_GRAPHICS_RENDERER_SOFTWARE, &category) !=
            CNA_RESULT_SUCCESS ||
        category != CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE ||
        cna_graphics_backend_get_category(CNA_GRAPHICS_RENDERER_VULKAN, &category) !=
            CNA_RESULT_SUCCESS ||
        category != CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE) {
        return 0;
    }

    /* The compiled-in answers agree with classifying the compiled-in identity. */
    if (cna_graphics_renderer_get_current_type(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_renderer_get_current_type(&type) != CNA_RESULT_SUCCESS ||
        type == CNA_GRAPHICS_RENDERER_UNKNOWN || type > UINT32_C(46)) {
        return 0;
    }
    if (cna_graphics_backend_get_current_category(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_backend_get_current_category(&category) != CNA_RESULT_SUCCESS ||
        cna_graphics_backend_get_current_maturity(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_backend_get_current_maturity(&maturity) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        CNA_GraphicsBackendCategory expected_category = UINT32_C(999);
        CNA_GraphicsBackendMaturity expected_maturity = UINT32_C(999);
        if (cna_graphics_backend_get_category(type, &expected_category) != CNA_RESULT_SUCCESS ||
            expected_category != category ||
            cna_graphics_backend_get_maturity(type, &expected_maturity) != CNA_RESULT_SUCCESS ||
            expected_maturity != maturity) {
            return 0;
        }
    }
    return 1;
}

static int validate_renderer_name(void)
{
    char buffer[64];
    uint64_t bytes = UINT64_C(0);
    uint64_t size = UINT64_C(0);

    memset(buffer, 0, sizeof(buffer));
    if (cna_graphics_renderer_get_current_name_size(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_renderer_get_current_name_size(&size) != CNA_RESULT_SUCCESS ||
        size == UINT64_C(0) || size >= (uint64_t)sizeof(buffer)) {
        return 0;
    }
    if (cna_graphics_renderer_copy_current_name(buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != size || (uint64_t)strlen(buffer) != size) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    bytes = UINT64_C(0);
    return cna_graphics_renderer_copy_current_name(buffer, UINT64_C(1), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        bytes == size && buffer[0] == '\0' &&
        cna_graphics_renderer_copy_current_name(buffer, UINT64_C(64), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

int main(void)
{
    if (!validate_levels_and_categories()) {
        return 1;
    }
    if (!validate_minimum_level()) {
        return 2;
    }
    if (!validate_platform()) {
        return 3;
    }
    if (!validate_names()) {
        return 4;
    }
    if (!validate_backend_classification()) {
        return 5;
    }
    if (!validate_renderer_name()) {
        return 6;
    }
    return 0;
}
