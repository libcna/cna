/* SPDX-License-Identifier: MS-PL */
#ifndef CNA_C_API_RUNTIME_SMOKE_H
#define CNA_C_API_RUNTIME_SMOKE_H
#include <CNA/C/cna.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runtime assertions retain evaluated operands and ownership before taking a
 * failure exit. */
typedef CNA_Result (*CnaSmokeDestroy)(CNA_Handle);
typedef struct CnaSmokeOwned {
    CNA_Handle handle;
    CnaSmokeDestroy destroy;
} CnaSmokeOwned;
typedef struct CnaRuntimeSmoke {
    CNA_Handle device;
    const char* last_operation;
    CNA_Result last_result;
    int queued_draw;
    CnaSmokeOwned owned[64];
    size_t owned_count;
    char trace[8192];
    size_t trace_size;
} CnaRuntimeSmoke;

static inline void cna_smoke_value(CnaRuntimeSmoke* smoke, const char* expression, uint64_t value)
{
    size_t available = sizeof(smoke->trace) - smoke->trace_size;
    if (available <= 1)
        return;
    int written = snprintf(smoke->trace + smoke->trace_size, available,
                           "  %s = %" PRIu64 " (0x%" PRIx64 ")\n", expression, value, value);
    if (written > 0)
        smoke->trace_size += (size_t)written < available ? (size_t)written : available - 1;
}
static inline void cna_smoke_error(void)
{
    CNA_ErrorInfo error = {sizeof(error), 1, 0, 0, 0};
    CNA_Result info_result = cna_error_get_last_info(&error);
    fprintf(stderr, "  last_error: info_result=%u result=%u category=%u bytes=%" PRIu64 "\n", info_result,
            error.result, error.category, error.message_byte_length);
    if (error.message_byte_length >= SIZE_MAX)
        return;
    char* message = (char*)malloc((size_t)error.message_byte_length + 1);
    if (message == NULL)
        return;
    uint64_t bytes = 0;
    CNA_Result copy_result = cna_error_copy_last_message(message, error.message_byte_length, &bytes);
    if (copy_result == CNA_RESULT_SUCCESS) {
        message[(size_t)bytes] = '\0';
        fprintf(stderr, "  last_error_message: %s\n", message);
    } else
        fprintf(stderr, "  last_error_message: copy_result=%u required=%" PRIu64 "\n", copy_result, bytes);
    free(message);
}
static inline void cna_smoke_failure(CnaRuntimeSmoke* smoke, const char* file, int line, const char* function,
                                     const char* expression)
{
    fprintf(stderr, "%s:%d: %s: FAILED: %s\n%s", file, line, function, expression, smoke->trace);
    if (smoke->last_operation != NULL)
        fprintf(stderr, "  last_api: %s returned %u\n", smoke->last_operation, smoke->last_result);
    cna_smoke_error();
    fflush(stderr);
}
static inline uint64_t cna_smoke_operand(CnaRuntimeSmoke* smoke, const char* name, uint64_t value)
{
    cna_smoke_value(smoke, name, value);
    return value;
}
static inline CNA_Result cna_smoke_api(CnaRuntimeSmoke* smoke, const char* name, CNA_Result result)
{
    smoke->last_operation = name;
    smoke->last_result = result;
    if (result == CNA_RESULT_SUCCESS) {
        if (strstr(name, "cna_graphics_device_draw_") == name)
            smoke->queued_draw = 1;
        if (strstr(name, "cna_graphics_device_get_backbuffer_data") == name ||
            strstr(name, "cna_graphics_device_present") == name)
            smoke->queued_draw = 0;
    }
    cna_smoke_value(smoke, name, result);
    return result;
}
static inline void cna_smoke_track(CnaRuntimeSmoke* smoke, const CNA_Handle* output, CnaSmokeDestroy destroy)
{
    if (output == NULL || *output == CNA_INVALID_HANDLE)
        return;
    if (smoke->owned_count == sizeof(smoke->owned) / sizeof(smoke->owned[0]))
        abort();
    smoke->owned[smoke->owned_count++] = (CnaSmokeOwned){*output, destroy};
    if (destroy == cna_graphics_device_destroy)
        smoke->device = *output;
}
static inline CNA_Result cna_smoke_publish(CnaRuntimeSmoke* smoke, const char* name, CNA_Result result,
                                           const CNA_Handle* output, CnaSmokeDestroy destroy,
                                           const CNA_Handle* second_output, CnaSmokeDestroy second_destroy)
{
    cna_smoke_track(smoke, output, destroy);
    cna_smoke_track(smoke, second_output, second_destroy);
    return cna_smoke_api(smoke, name, result);
}
static inline CNA_Result cna_smoke_drop(CnaRuntimeSmoke* smoke, const char* name, CNA_Result result,
                                        CNA_Handle handle)
{
    if (result == CNA_RESULT_SUCCESS) {
        for (size_t i = 0; i < smoke->owned_count; ++i) {
            if (smoke->owned[i].handle != handle)
                continue;
            if (smoke->owned[i].destroy == cna_graphics_device_destroy)
                smoke->device = 0;
            memmove(smoke->owned + i, smoke->owned + i + 1,
                    (smoke->owned_count - i - 1) * sizeof(smoke->owned[0]));
            --smoke->owned_count;
            break;
        }
    }
    return cna_smoke_api(smoke, name, result);
}
static inline int cna_smoke_cleanup(CnaRuntimeSmoke* smoke)
{
    int failed = 0;
    if (smoke->device != CNA_INVALID_HANDLE) {
        /* Deferred draws must finish before their effect and buffer views are released. */
        if (smoke->queued_draw) {
            CNA_Result result = cna_graphics_device_present(smoke->device);
            if (result != CNA_RESULT_SUCCESS) {
                fprintf(stderr, "cleanup present: result=%u\n", result);
                cna_smoke_error();
                failed = 1;
            }
        }
        /* Bound native views must be removed while both their device and children
         * still live. */
        const CNA_Result results[] = {
            cna_graphics_device_set_current_effect(smoke->device, CNA_INVALID_HANDLE),
            cna_graphics_device_set_vertex_buffers(smoke->device, NULL, 0),
            cna_graphics_device_set_index_buffer(smoke->device, CNA_INVALID_HANDLE)};
        for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
            if (results[i] != CNA_RESULT_SUCCESS) {
                fprintf(stderr, "cleanup unbind %zu: result=%u\n", i, results[i]);
                failed = 1;
            }
        }
    }
    while (smoke->owned_count != 0) {
        CnaSmokeOwned resource = smoke->owned[--smoke->owned_count];
        CNA_Result result = resource.destroy(resource.handle);
        if (result != CNA_RESULT_SUCCESS) {
            fprintf(stderr, "cleanup handle=%" PRIu64 ": result=%u\n", resource.handle, result);
            cna_smoke_error();
            failed = 1;
        }
    }
    return failed;
}
static inline int cna_smoke_inject_failure(const char* expression)
{
    const char* match = getenv("CNA_C_API_SMOKE_FAIL_MATCH");
    return match != NULL && *match != '\0' && strstr(expression, match) != NULL;
}

#define SMOKE_VALUE(value) cna_smoke_operand(&smoke, #value, (uint64_t)(value))
#define SMOKE_API(call) cna_smoke_api(&smoke, #call, (call))
#define SMOKE_OWN(call, output, destroy)                                                                     \
    cna_smoke_publish(&smoke, #call, (call), (output), (destroy), NULL, NULL)
#define SMOKE_OWN_TWO(call, output, destroy, second, second_destroy)                                         \
    cna_smoke_publish(&smoke, #call, (call), (output), (destroy), (second), (second_destroy))
#define SMOKE_DROP(call, handle) cna_smoke_drop(&smoke, #call, (call), (handle))
#define RUNTIME_CHECK(expression)                                                                            \
    do {                                                                                                     \
        smoke.trace_size = 0;                                                                                \
        smoke.trace[0] = '\0';                                                                               \
        int cna_smoke_ok = !!(expression);                                                                   \
        if (!cna_smoke_ok || cna_smoke_inject_failure(#expression)) {                                        \
            cna_smoke_failure(&smoke, __FILE__, __LINE__, __func__, #expression);                            \
            goto cleanup;                                                                                    \
        }                                                                                                    \
    } while (0)
#endif
