/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/packed_vectors.h>
#include "CnaTestReport.h"
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
uint64_t cna_test_packed_native(uint32_t format, uint64_t bits, int32_t *hash, char *text);
int cna_test_packed_worker(int (*test)(void));
static int values(void)
{
    const unsigned widths[] = {8, 16, 16, 16, 32, 16, 32, 64, 16, 32, 32, 64, 32, 32, 64, 32, 64};
    const uint64_t samples[] = {0,
                                1,
                                UINT64_MAX,
                                UINT64_C(0x8000800080008000),
                                UINT64_C(0x7c007e00fc00fe00),
                                UINT64_C(0x123456789abcdef0)};
    for (uint32_t format = 0; format <= CNA_PACKED_VECTOR_FORMAT_SHORT4; ++format) {
        for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
            uint64_t bits = samples[i] & (UINT64_MAX >> (64 - widths[format]));
            char native[1024], actual[1024];
            int32_t expected_hash, hash = 99, again = 0;
            uint64_t expected = cna_test_packed_native(format, bits, &expected_hash, native), size = 999;
            CHECK(cna_packed_vector_get_hash_code(format, bits, &hash) == CNA_RESULT_SUCCESS &&
                  hash == expected_hash);
            CHECK(cna_packed_vector_get_hash_code(format, bits, &again) == CNA_RESULT_SUCCESS &&
                  hash == again);
            CHECK(cna_packed_vector_get_string_size(format, bits, &size) == CNA_RESULT_SUCCESS &&
                  size == expected);
            memset(actual, 'Z', sizeof(actual));
            CHECK(cna_packed_vector_copy_string(format, bits, actual, size, &size) == CNA_RESULT_SUCCESS);
            CHECK(memcmp(native, actual, (size_t)size) == 0 && actual[size] == 'Z');
            memset(actual, 'Z', sizeof(actual));
            CHECK(cna_packed_vector_copy_string(format, bits, actual, expected - 1, &size) ==
                      CNA_RESULT_BUFFER_TOO_SMALL &&
                  size == expected);
            for (unsigned j = 0; j < sizeof(actual); ++j)
                CHECK(actual[j] == 'Z');
            CHECK(cna_packed_vector_copy_string(format, bits, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL);
            CHECK(cna_packed_vector_copy_string(format, bits, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_packed_vector_copy_string(format, bits, actual, sizeof(actual), NULL) ==
                  CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_packed_vector_get_string_size(format, bits, NULL) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_packed_vector_get_hash_code(format, bits, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        }
        if (widths[format] < 64) {
            int32_t hash = 99;
            uint64_t size = 999;
            char text[16] = "untouched";
            uint64_t excess = UINT64_C(1) << widths[format];
            CHECK(cna_packed_vector_get_hash_code(format, excess, &hash) == CNA_RESULT_INVALID_ARGUMENT &&
                  hash == 99);
            CHECK(cna_packed_vector_get_string_size(format, excess, &size) == CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999);
            CHECK(cna_packed_vector_copy_string(format, excess, text, sizeof(text), &size) ==
                      CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999 && strcmp(text, "untouched") == 0);
        }
    }
    {
        const uint32_t bad[] = {17, UINT32_MAX, UINT32_C(0x80000000)};
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            int32_t hash = 99;
            uint64_t size = 999;
            char text[16] = "untouched";
            CHECK(cna_packed_vector_get_hash_code(bad[i], 0, &hash) == CNA_RESULT_INVALID_ARGUMENT &&
                  hash == 99);
            CHECK(cna_packed_vector_get_string_size(bad[i], 0, &size) == CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999);
            CHECK(cna_packed_vector_copy_string(bad[i], 0, text, sizeof(text), &size) ==
                      CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999 && strcmp(text, "untouched") == 0);
        }
    }
    return 0;
}
int main(void)
{
    CHECK(values() == 0);
    CHECK(cna_test_packed_worker(values) == 0);
    puts("PackedValueSmoke: 17 formats x 6 bit patterns, native hash/text and failures: PASS");
    return 0;
}
