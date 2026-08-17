// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
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

/* CBIND-049: the renderer selection surface. This is the runtime counterpart of the compile-time
   CNA_GRAPHICS_RENDERER the ABI already publishes -- which renderer will be tried, which are in
   this build, which one survived, and what was passed over on the way. */
static int validate_renderer_selection(void)
{
    CNA_GraphicsRendererType selected = CNA_GRAPHICS_RENDERER_UNKNOWN;
    CNA_GraphicsRendererType active = CNA_GRAPHICS_RENDERER_UNKNOWN;
    CNA_GraphicsRendererType current = CNA_GRAPHICS_RENDERER_UNKNOWN;
    CNA_GraphicsRendererType parsed = CNA_GRAPHICS_RENDERER_UNKNOWN;
    CNA_GraphicsRendererType available[64];
    CNA_Bool latched = UINT8_C(9);
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool recognized = UINT8_C(9);
    uint64_t count = UINT64_C(99);
    uint64_t reported = UINT64_C(99);
    uint64_t index = UINT64_C(0);
    char name[128];

    /* The build's own renderer must be among the available ones, and must be reported available. */
    if (cna_graphics_renderer_get_current_type(&current) != CNA_RESULT_SUCCESS ||
        cna_graphics_renderer_get_available_count_ext(&count) != CNA_RESULT_SUCCESS ||
        count == UINT64_C(0) || count > (uint64_t)(sizeof(available) / sizeof(available[0])) ||
        cna_graphics_renderer_get_is_available_ext(current, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return (fprintf(stderr, "SEL ARM %d\n", 1), 0);
    }
    /* Count, then copy: a capacity one short is refused without writing a single element. */
    memset(available, 0, sizeof(available));
    if (cna_graphics_renderer_copy_available_ext(available, count - UINT64_C(1), &reported) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        reported != count || available[0] != UINT32_C(0) ||
        cna_graphics_renderer_copy_available_ext(available, count, &reported) !=
            CNA_RESULT_SUCCESS ||
        reported != count) {
        return (fprintf(stderr, "SEL ARM %d\n", 2), 0);
    }
    {
        int found = 0;
        for (index = UINT64_C(0); index < count; ++index) {
            if (available[index] == current) {
                found = 1;
            }
        }
        if (!found) {
            return (fprintf(stderr, "SEL ARM %d\n", 3), 0);
        }
    }

    /* Selected is what will be tried; active is what survived, and asking for it before anything
       has been created is refused rather than guessed -- until the selection latches there is no
       honest answer to give, which is why the two are separate questions. */
    if (cna_graphics_renderer_get_selected_ext(&selected) != CNA_RESULT_SUCCESS ||
        selected == CNA_GRAPHICS_RENDERER_UNKNOWN ||
        cna_graphics_renderer_get_is_latched_ext(&latched) != CNA_RESULT_SUCCESS ||
        (latched != CNA_FALSE && latched != CNA_TRUE)) {
        return 0;
    }
    {
        const CNA_Result activeResult = cna_graphics_renderer_get_active_ext(&active);
        if (latched == CNA_FALSE) {
            if (activeResult != CNA_RESULT_INVALID_STATE) {
                return 0;
            }
        } else if (activeResult != CNA_RESULT_SUCCESS ||
                   active == CNA_GRAPHICS_RENDERER_UNKNOWN) {
            return 0;
        }
        if (cna_graphics_renderer_get_active_ext(0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* Automatic fallback round-trips. */
    if (cna_graphics_renderer_get_automatic_fallback_ext(&flag) != CNA_RESULT_SUCCESS ||
        cna_graphics_renderer_set_automatic_fallback_ext(CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_graphics_renderer_get_automatic_fallback_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return (fprintf(stderr, "SEL ARM %d\n", 5), 0);
    }

    /* A name parses to its identity; an unrecognized one is an answer, not a failure. */
    if (cna_graphics_renderer_try_parse_name_ext(view("HEADLESS"), &parsed, &recognized) !=
            CNA_RESULT_SUCCESS ||
        recognized != CNA_TRUE || parsed != CNA_GRAPHICS_RENDERER_HEADLESS ||
        cna_graphics_renderer_try_parse_name_ext(view("headless"), &parsed, &recognized) !=
            CNA_RESULT_SUCCESS ||
        recognized != CNA_TRUE ||
        cna_graphics_renderer_try_parse_name_ext(view("NOT_A_RENDERER"), &parsed, &recognized) !=
            CNA_RESULT_SUCCESS ||
        recognized != CNA_FALSE || parsed != CNA_GRAPHICS_RENDERER_UNKNOWN ||
        cna_graphics_renderer_try_parse_name_ext(view("HEADLESS"), 0, &recognized) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return (fprintf(stderr, "SEL ARM %d\n", 6), 0);
    }

    /* Every fallback reason has a stable, non-empty name, and an undefined one is refused. */
    {
        CNA_GraphicsRendererFallbackReason reason = UINT32_C(0);
        for (reason = UINT32_C(0); reason <= CNA_GRAPHICS_RENDERER_FALLBACK_MAXIMUM; ++reason) {
            memset(name, 0, sizeof(name));
            if (cna_graphics_renderer_fallback_reason_get_name_size_ext(reason, &reported) !=
                    CNA_RESULT_SUCCESS ||
                reported == UINT64_C(0) || reported >= (uint64_t)sizeof(name) ||
                cna_graphics_renderer_fallback_reason_copy_name_ext(
                    reason, name, (uint64_t)sizeof(name), &reported) != CNA_RESULT_SUCCESS ||
                name[reported] != '\0') {
                return (fprintf(stderr, "SEL ARM %d\n", 7), 0);
            }
        }
        if (cna_graphics_renderer_fallback_reason_get_name_size_ext(
                CNA_GRAPHICS_RENDERER_FALLBACK_MAXIMUM + UINT32_C(1), &reported) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return (fprintf(stderr, "SEL ARM %d\n", 8), 0);
        }
    }

    /* Nothing fell back in a build whose first choice works, and a position outside an empty
       history is refused rather than answered with a blank record. */
    {
        CNA_GraphicsRendererFallbackRecord record;
        memset(&record, 0, sizeof(record));
        record.struct_size = (uint32_t)sizeof(record);
        record.struct_version = UINT32_C(1);
        if (cna_graphics_renderer_get_fallback_count_ext(&count) != CNA_RESULT_SUCCESS) {
            return (fprintf(stderr, "SEL ARM %d\n", 9), 0);
        }
        if (cna_graphics_renderer_get_fallback_at_ext(count, &record) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_graphics_renderer_fallback_get_message_size_ext(count, &reported) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return (fprintf(stderr, "SEL ARM %d\n", 10), 0);
        }
        for (index = UINT64_C(0); index < count; ++index) {
            memset(name, 0, sizeof(name));
            if (cna_graphics_renderer_get_fallback_at_ext(index, &record) != CNA_RESULT_SUCCESS ||
                record.reason > CNA_GRAPHICS_RENDERER_FALLBACK_MAXIMUM ||
                cna_graphics_renderer_fallback_get_message_size_ext(index, &reported) !=
                    CNA_RESULT_SUCCESS ||
                reported == UINT64_C(0) || reported >= (uint64_t)sizeof(name) ||
                cna_graphics_renderer_fallback_copy_message_ext(
                    index, name, (uint64_t)sizeof(name), &reported) != CNA_RESULT_SUCCESS) {
                return (fprintf(stderr, "SEL ARM %d\n", 11), 0);
            }
        }
        /* A structure whose version this ABI does not know is refused. */
        record.struct_version = UINT32_C(2);
        if (cna_graphics_renderer_get_fallback_at_ext(UINT64_C(0), &record) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return (fprintf(stderr, "SEL ARM %d\n", 12), 0);
        }
    }

    /* An undefined identity is refused everywhere it can be passed. */
    if (cna_graphics_renderer_set_preferred_ext(UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_renderer_get_is_available_ext(UINT32_MAX, &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_renderer_set_preferred_by_name_ext(view("NOT_A_RENDERER")) ==
            CNA_RESULT_SUCCESS) {
        return (fprintf(stderr, "SEL ARM %d\n", 13), 0);
    }
    {
        /* A chain with one bad entry changes nothing rather than being applied up to the mistake. */
        CNA_GraphicsRendererType chain[2];
        chain[0] = current;
        chain[1] = UINT32_MAX;
        if (cna_graphics_renderer_set_fallback_chain_ext(chain, UINT64_C(2)) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_graphics_renderer_set_fallback_chain_ext(0, UINT64_C(1)) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_graphics_renderer_set_fallback_chain_ext(chain, UINT64_C(1)) !=
                CNA_RESULT_SUCCESS) {
            return (fprintf(stderr, "SEL ARM %d\n", 14), 0);
        }
    }
    return 1;
}

/* CBIND-049: the log sink and the compile-time platform answers. A C sink matters more than it
   looks: the default writes to stderr and never stdout, because a terminal-hosted game draws its
   frame on stdout and a log line there would corrupt it. */
typedef struct SinkLog {
    int calls;
    CNA_LogLevel lastLevel;
    CNA_LogCategory lastCategory;
    char lastMessage[256];
    uint64_t lastLength;
} SinkLog;

static void record_line(
    const CNA_LogLevel level,
    const CNA_LogCategory category,
    const CNA_StringView message,
    void* const context)
{
    SinkLog* const log = (SinkLog*)context;
    ++log->calls;
    log->lastLevel = level;
    log->lastCategory = category;
    log->lastLength = message.byte_length;
    if (message.byte_length < (uint64_t)sizeof(log->lastMessage) && message.data != 0) {
        memcpy(log->lastMessage, message.data, (size_t)message.byte_length);
        log->lastMessage[message.byte_length] = '\0';
    }
}

static int validate_log_sink_and_platform(void)
{
    SinkLog log;
    CNA_Bool apple = UINT8_C(9);
    CNA_Bool mobile = UINT8_C(9);
    uint64_t bytes = UINT64_C(99);
    char name[128];

    /* Both are compile-time facts, so both must answer, and the name must be real text. */
    memset(name, 0, sizeof(name));
    if (cna_platform_get_is_apple_ext(&apple) != CNA_RESULT_SUCCESS ||
        (apple != CNA_FALSE && apple != CNA_TRUE) ||
        cna_platform_get_is_mobile_ext(&mobile) != CNA_RESULT_SUCCESS ||
        (mobile != CNA_FALSE && mobile != CNA_TRUE) ||
        cna_platform_get_is_apple_ext(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_platform_get_is_mobile_ext(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_platform_get_current_name_size_ext(&bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(name) ||
        cna_platform_copy_current_name_ext(name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_SUCCESS ||
        name[bytes] != '\0' || name[0] == '\0') {
        return 0;
    }
    /* One byte short is refused without writing anything. */
    {
        char small[4];
        uint64_t reported = UINT64_C(0);
        memset(small, '#', sizeof(small));
        if (cna_platform_copy_current_name_ext(small, UINT64_C(1), &reported) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            reported != bytes || small[0] != '#') {
            return 0;
        }
    }

    memset(&log, 0, sizeof(log));
    if (cna_logger_set_sink_ext(record_line, &log) != CNA_RESULT_SUCCESS ||
        cna_logger_log(CNA_LOG_LEVEL_INFO, view("sink line"), CNA_LOG_CATEGORY_APPLICATION,
                       CNA_TRUE) != CNA_RESULT_SUCCESS ||
        log.calls != 1 || log.lastLevel != CNA_LOG_LEVEL_INFO ||
        log.lastCategory != CNA_LOG_CATEGORY_APPLICATION ||
        log.lastLength == UINT64_C(0) ||
        strstr(log.lastMessage, "sink line") == 0) {
        (void)cna_logger_reset_sink_ext();
        return 0;
    }
    /* Resetting restores the default, and nothing reaches the C sink afterwards. */
    if (cna_logger_reset_sink_ext() != CNA_RESULT_SUCCESS ||
        cna_logger_log(CNA_LOG_LEVEL_INFO, view("after reset"), CNA_LOG_CATEGORY_APPLICATION,
                       CNA_TRUE) != CNA_RESULT_SUCCESS ||
        log.calls != 1) {
        return 0;
    }
    /* A null callback is the documented way to restore the default too. */
    if (cna_logger_set_sink_ext(0, 0) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
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
    if (!validate_renderer_selection()) {
        return 7;
    }
    if (!validate_log_sink_and_platform()) {
        return 8;
    }
    return 0;
}
