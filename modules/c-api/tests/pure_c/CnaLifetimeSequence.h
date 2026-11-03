// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_LIFETIME_SEQUENCE_H
#define CNA_C_API_LIFETIME_SEQUENCE_H

#include <CNA/C/cna.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define SEQUENCE_COUNT 256U
#define OPERATION_COUNT 128U
#define SLOT_COUNT 28U

static const char* sequence_suite;
static unsigned sequence_index;
static unsigned sequence_step;
static uint32_t sequence_seed;
static uint32_t sequence_random;
static unsigned sequence_trace[OPERATION_COUNT];
static uint64_t sequence_calls;
static uint64_t sequence_refusals;

static uint32_t next_random(void)
{
    sequence_random ^= sequence_random << 13;
    sequence_random ^= sequence_random >> 17;
    sequence_random ^= sequence_random << 5;
    return sequence_random;
}

static void begin_sequence(const char* suite, unsigned index)
{
    sequence_suite = suite;
    sequence_index = index;
    sequence_step = 0U;
    sequence_seed = UINT32_C(0x5afe0004) + (uint32_t)index;
    sequence_random = sequence_seed;
    for (unsigned i = 0U; i < OPERATION_COUNT; ++i)
        sequence_trace[i] = 0U;
}

static int sequence_failure(const char* expression, int line, CNA_Result actual, CNA_Result expected)
{
    CNA_ErrorInfo error = {sizeof(CNA_ErrorInfo), 1U, 0U, 0U, 0U};
    char message[1024];
    uint64_t bytes = 0U;
    fprintf(stderr, "%s sequence=%u seed=0x%08" PRIx32 " step=%u line=%d: %s; result=%" PRIu32
        " expected=%" PRIu32 "\nreplay: <executable> %u\ntrace (operation*32+slot):",
        sequence_suite, sequence_index, sequence_seed, sequence_step, line, expression,
        actual, expected, sequence_index);
    for (unsigned i = 0U; i <= sequence_step && i < OPERATION_COUNT; ++i)
        fprintf(stderr, " %u", sequence_trace[i]);
    fputc('\n', stderr);
    if (cna_error_get_last_info(&error) == CNA_RESULT_SUCCESS) {
        fprintf(stderr, "last error: result=%" PRIu32 " category=%" PRIu32 " bytes=%" PRIu64 "\n",
            error.result, error.category, error.message_byte_length);
        if (cna_error_copy_last_message(message, sizeof(message), &bytes) == CNA_RESULT_SUCCESS) {
            fputs("last error text: ", stderr);
            fwrite(message, 1U, (size_t)bytes, stderr);
            fputc('\n', stderr);
        }
    }
    return 0;
}

static int sequence_result(CNA_Result actual, CNA_Result expected, const char* expression, int line)
{
    ++sequence_calls;
    if (actual != expected)
        return sequence_failure(expression, line, actual, expected);
    if (expected != CNA_RESULT_SUCCESS) {
        CNA_ErrorInfo error = {sizeof(CNA_ErrorInfo), 1U, 0U, 0U, 0U};
        ++sequence_refusals;
        if (cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS || error.result != expected ||
            error.category == CNA_ERROR_CATEGORY_NONE || error.message_byte_length == 0U)
            return sequence_failure("last error matches refused operation", line, error.result, expected);
    }
    return 1;
}

#define EXPECT_RESULT(call, expected) do { \
    if (!sequence_result((call), (expected), #call, __LINE__)) return 0; \
} while (0)
#define REQUIRE(condition) do { if (!(condition)) \
    return sequence_failure(#condition, __LINE__, CNA_RESULT_INTERNAL, CNA_RESULT_SUCCESS); \
} while (0)

/* One numeric argument replays exactly the reported sequence, including its setup and teardown. */
static int sequence_range(int argc, char** argv, unsigned* first, unsigned* end)
{
    *first = 0U;
    *end = SEQUENCE_COUNT;
    if (argc == 2) {
        char* tail = NULL;
        unsigned long parsed = strtoul(argv[1], &tail, 10);
        if (argv[1][0] == '\0' || *tail != '\0' || parsed >= SEQUENCE_COUNT)
            return 0;
        *first = (unsigned)parsed;
        *end = *first + 1U;
    }
    return argc == 1 || argc == 2;
}

static void sequence_summary(unsigned count)
{
    printf("%s: %u sequences, %u transitions, %" PRIu64 " checked calls, %" PRIu64
        " refusals; seeds 0x5afe0004+sequence; PASS\n",
        sequence_suite, count, count * OPERATION_COUNT, sequence_calls, sequence_refusals);
}

#endif
