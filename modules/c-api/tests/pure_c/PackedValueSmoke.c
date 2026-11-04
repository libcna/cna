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
/* Literal vectors follow the measured XNA value contracts recorded in
 * docs/xna-fna-behavior-decisions.md (packed-vector and packed-half rows).
 * Integer hashes: byte/ushort identity, uint signed bits, ulong low XOR high.
 * Half exponent 31 is finite in XNA; these are expanded-value strings. */
static int literal_values(void)
{
    static const struct { uint32_t format; uint64_t bits; int32_t hash; const char* text; } cases[] = {
        {0, 0, 0, "00"}, {0, 255, 255, "FF"},
        {1, 32769, 32769, "8001"}, {2, 32769, 32769, "8001"}, {3, 32769, 32769, "8001"},
        {4, UINT64_C(0x80000001), -2147483647, "80000001"},
        /* HalfSingle.ToString IL_0009..0013: invariant Single G7.
         * 0x3c01 = 1+2^-10 -> 1.000977; hashes below are unsigned storage/fold. */
        {5, UINT64_C(0x3c01), 15361, "1.000977"},
        {5, UINT64_C(0x0001), 1, "5.960464E-08"},
        {6, UINT64_C(0x3c013c01), 1006713857, "{X:1.000977 Y:1.000977}"},
        {7, UINT64_C(0x3c013c013c013c01), 0, "{X:1.000977 Y:1.000977 Z:1.000977 W:1.000977}"},
        /* HalfSingle.ToString IL_0009..0013 delegates to .NET Framework 4 Single:
         * negative zero displays as "0"; packed storage and its hash retain 0x8000. */
        {5, 0, 0, "0"}, {5, 32768, 32768, "0"}, {5, 31744, 31744, "65536"},
        {6, UINT64_C(0x80008000), -2147450880, "{X:0 Y:0}"},
        {7, UINT64_C(0x8000800080008000), 0, "{X:0 Y:0 Z:0 W:0}"},
        {5, 32256, 32256, "98304"}, {5, 64512, 64512, "-65536"},
        {6, UINT64_C(0xbc003c00), -1140835328, "{X:1 Y:-1}"},
        {7, UINT64_C(0x40000000bc003c00), -67093504, "{X:1 Y:-1 Z:0 W:2}"},
        {8, 32769, 32769, "8001"},
        {9, UINT64_C(0x80000001), -2147483647, "80000001"},
        {10, UINT64_C(0x80000001), -2147483647, "80000001"},
        {11, UINT64_C(0x8000000000000001), -2147483647, "8000000000000001"},
        {12, UINT64_C(0x80000001), -2147483647, "80000001"},
        {13, UINT64_C(0x80000001), -2147483647, "80000001"},
        {14, UINT64_C(0x123456789abcdef0), -2004318072, "123456789ABCDEF0"},
        {15, UINT64_C(0x80000001), -2147483647, "80000001"},
        {16, UINT64_C(0x8000000000000001), -2147483647, "8000000000000001"}
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        char text[96] = {0}; uint64_t size = 99; int32_t hash = 99;
        CHECK(cna_packed_vector_get_hash_code(cases[i].format, cases[i].bits, &hash) == CNA_RESULT_SUCCESS && hash == cases[i].hash);
        CHECK(cna_packed_vector_get_string_size(cases[i].format, cases[i].bits, &size) == CNA_RESULT_SUCCESS && size == strlen(cases[i].text));
        CHECK(cna_packed_vector_copy_string(cases[i].format, cases[i].bits, text, sizeof(text), &size) == CNA_RESULT_SUCCESS && size == strlen(cases[i].text) && memcmp(text, cases[i].text, (size_t)size) == 0);
    }
    return 0;
}
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
    CHECK(literal_values() == 0);
    CHECK(values() == 0);
    CHECK(cna_test_packed_worker(values) == 0);
    puts("PackedValueSmoke: 17 formats x 6 bit patterns, native hash/text and failures: PASS");
    return 0;
}
