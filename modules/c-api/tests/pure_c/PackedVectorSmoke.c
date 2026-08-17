// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(CNA_PackedVectorFormat) == sizeof(uint32_t),
               "CNA_PackedVectorFormat must remain fixed width");
_Static_assert(CNA_PACKED_VECTOR_FORMAT_ALPHA8 == UINT32_C(0) &&
                   CNA_PACKED_VECTOR_FORMAT_BGR565 == UINT32_C(1) &&
                   CNA_PACKED_VECTOR_FORMAT_BGRA4444 == UINT32_C(2) &&
                   CNA_PACKED_VECTOR_FORMAT_BGRA5551 == UINT32_C(3) &&
                   CNA_PACKED_VECTOR_FORMAT_BYTE4 == UINT32_C(4) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE == UINT32_C(5) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2 == UINT32_C(6) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4 == UINT32_C(7) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2 == UINT32_C(8) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4 == UINT32_C(9) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2 == UINT32_C(10) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4 == UINT32_C(11) &&
                   CNA_PACKED_VECTOR_FORMAT_RG32 == UINT32_C(12) &&
                   CNA_PACKED_VECTOR_FORMAT_RGBA1010102 == UINT32_C(13) &&
                   CNA_PACKED_VECTOR_FORMAT_RGBA64 == UINT32_C(14) &&
                   CNA_PACKED_VECTOR_FORMAT_SHORT2 == UINT32_C(15) &&
                   CNA_PACKED_VECTOR_FORMAT_SHORT4 == UINT32_C(16),
               "CNA packed-vector identities must remain stable");

typedef struct PackedCase {
    CNA_PackedVectorFormat format;
    CNA_Vector4 source;
    uint64_t expected_packed;
    CNA_Vector4 expected_unpacked;
} PackedCase;

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) <= 0.00002F;
}

static int vector_equals(const CNA_Vector4 left, const CNA_Vector4 right)
{
    return nearly_equal(left.x, right.x) && nearly_equal(left.y, right.y) &&
        nearly_equal(left.z, right.z) && nearly_equal(left.w, right.w);
}

static int validate_all_formats(void)
{
    static const PackedCase cases[] = {
        {CNA_PACKED_VECTOR_FORMAT_ALPHA8,
         {9.0F, 8.0F, 7.0F, 1.0F}, UINT64_C(0x00000000000000ff),
         {0.0F, 0.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_BGR565,
         {1.0F, 1.0F, 1.0F, 9.0F}, UINT64_C(0x000000000000ffff),
         {1.0F, 1.0F, 1.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_BGRA4444,
         {1.0F, 1.0F, 1.0F, 1.0F}, UINT64_C(0x000000000000ffff),
         {1.0F, 1.0F, 1.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_BGRA5551,
         {1.0F, 1.0F, 1.0F, 1.0F}, UINT64_C(0x000000000000ffff),
         {1.0F, 1.0F, 1.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_BYTE4,
         {1.0F, 2.0F, 3.0F, 4.0F}, UINT64_C(0x0000000004030201),
         {1.0F, 2.0F, 3.0F, 4.0F}},
        {CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE,
         {1.0F, 9.0F, 8.0F, 7.0F}, UINT64_C(0x0000000000003c00),
         {1.0F, 0.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2,
         {1.0F, -2.0F, 8.0F, 7.0F}, UINT64_C(0x00000000c0003c00),
         {1.0F, -2.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4,
         {1.0F, -2.0F, 0.5F, 4.0F}, UINT64_C(0x44003800c0003c00),
         {1.0F, -2.0F, 0.5F, 4.0F}},
        {CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2,
         {-1.0F, 1.0F, 8.0F, 7.0F}, UINT64_C(0x0000000000007f81),
         {-1.0F, 1.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4,
         {-1.0F, -0.5F, 0.5F, 1.0F}, UINT64_C(0x000000007f40c081),
         {-1.0F, -64.0F / 127.0F, 64.0F / 127.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2,
         {-1.0F, 1.0F, 8.0F, 7.0F}, UINT64_C(0x000000007fff8001),
         {-1.0F, 1.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4,
         {-1.0F, 0.0F, 1.0F, 0.5F}, UINT64_C(0x40007fff00008001),
         {-1.0F, 0.0F, 1.0F, 16384.0F / 32767.0F}},
        {CNA_PACKED_VECTOR_FORMAT_RG32,
         {1.0F, 0.5F, 8.0F, 7.0F}, UINT64_C(0x000000008000ffff),
         {1.0F, 32768.0F / 65535.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_RGBA1010102,
         {1.0F, 0.0F, 0.5F, 1.0F}, UINT64_C(0x00000000e00003ff),
         {1.0F, 0.0F, 512.0F / 1023.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_RGBA64,
         {1.0F, 0.0F, 0.5F, 1.0F}, UINT64_C(0xffff80000000ffff),
         {1.0F, 0.0F, 32768.0F / 65535.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_SHORT2,
         {-1.0F, 32767.0F, 8.0F, 7.0F}, UINT64_C(0x000000007fffffff),
         {-1.0F, 32767.0F, 0.0F, 1.0F}},
        {CNA_PACKED_VECTOR_FORMAT_SHORT4,
         {-32768.0F, -1.0F, 1.0F, 32767.0F}, UINT64_C(0x7fff0001ffff8000),
         {-32768.0F, -1.0F, 1.0F, 32767.0F}}
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const PackedCase* const test_case = &cases[index];
        uint64_t packed = UINT64_MAX;
        CNA_Vector4 unpacked = {99.0F, 98.0F, 97.0F, 96.0F};
        CNA_Bool equal = CNA_FALSE;
        CNA_Bool not_equal = CNA_FALSE;
        CNA_Bool opposite = CNA_TRUE;
        if (cna_packed_vector_pack(test_case->format, test_case->source, &packed) !=
                CNA_RESULT_SUCCESS || packed != test_case->expected_packed ||
            cna_packed_vector_unpack(test_case->format, packed, &unpacked) != CNA_RESULT_SUCCESS ||
            !vector_equals(unpacked, test_case->expected_unpacked) ||
            cna_packed_vector_equals(test_case->format, packed, packed, &equal) !=
                CNA_RESULT_SUCCESS || equal != CNA_TRUE ||
            cna_packed_vector_equals(
                test_case->format, packed, packed ^ UINT64_C(1), &opposite) !=
                CNA_RESULT_SUCCESS || opposite != CNA_FALSE ||
            cna_packed_vector_not_equals(
                test_case->format, packed, packed ^ UINT64_C(1), &not_equal) !=
                CNA_RESULT_SUCCESS || not_equal != CNA_TRUE ||
            cna_packed_vector_not_equals(test_case->format, packed, packed, &opposite) !=
                CNA_RESULT_SUCCESS || opposite != CNA_FALSE) {
            return 0;
        }
    }
    return 1;
}

static int validate_half_helpers_and_special_values(void)
{
    uint16_t half = UINT16_MAX;
    float single = -9.0F;
    uint64_t packed = 0U;
    CNA_Vector4 unpacked = {0.0F, 0.0F, 0.0F, 0.0F};
    if (cna_half_from_single(1.0F, &half) != CNA_RESULT_SUCCESS || half != UINT16_C(0x3c00) ||
        cna_half_from_single_bits(INT32_C(0x3f800000), &half) != CNA_RESULT_SUCCESS ||
        half != UINT16_C(0x3c00) ||
        cna_half_to_single(UINT16_C(0xc000), &single) != CNA_RESULT_SUCCESS || single != -2.0F ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE,
            (CNA_Vector4){NAN, 0.0F, 0.0F, 0.0F}, &packed) != CNA_RESULT_SUCCESS ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || !isnan(unpacked.x) ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2,
            (CNA_Vector4){INFINITY, -INFINITY, 0.0F, 0.0F}, &packed) != CNA_RESULT_SUCCESS ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || !isinf(unpacked.x) || unpacked.x < 0.0F ||
        !isinf(unpacked.y) || unpacked.y > 0.0F ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4,
            (CNA_Vector4){NAN, INFINITY, -INFINITY, -0.0F}, &packed) != CNA_RESULT_SUCCESS ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || !isnan(unpacked.x) || !isinf(unpacked.y) ||
        !isinf(unpacked.z) || !signbit(unpacked.z) || !signbit(unpacked.w)) {
        return 0;
    }

    if (cna_half_from_single(1.0F, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_half_from_single_bits(INT32_C(0x3f800000), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_half_to_single(UINT16_C(0x3c00), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_failures_are_atomic(void)
{
    const CNA_PackedVectorFormat invalid_format = UINT32_C(17);
    const CNA_Vector4 finite = {1.0F, 2.0F, 3.0F, 4.0F};
    uint64_t packed = UINT64_C(0x1122334455667788);
    CNA_Vector4 vector = {11.0F, 22.0F, 33.0F, 44.0F};
    CNA_Bool boolean = UINT8_C(77);

    if (cna_packed_vector_pack(invalid_format, finite, &packed) != CNA_RESULT_INVALID_ARGUMENT ||
        packed != UINT64_C(0x1122334455667788) ||
        cna_packed_vector_unpack(invalid_format, 0U, &vector) != CNA_RESULT_INVALID_ARGUMENT ||
        !vector_equals(vector, (CNA_Vector4){11.0F, 22.0F, 33.0F, 44.0F}) ||
        cna_packed_vector_equals(invalid_format, 0U, 0U, &boolean) !=
            CNA_RESULT_INVALID_ARGUMENT || boolean != UINT8_C(77) ||
        cna_packed_vector_not_equals(invalid_format, 0U, 1U, &boolean) !=
            CNA_RESULT_INVALID_ARGUMENT || boolean != UINT8_C(77) ||
        cna_packed_vector_pack(CNA_PACKED_VECTOR_FORMAT_ALPHA8, finite, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_ALPHA8, 0U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_equals(CNA_PACKED_VECTOR_FORMAT_ALPHA8, 0U, 0U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_not_equals(CNA_PACKED_VECTOR_FORMAT_ALPHA8, 0U, 1U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_packed_vector_unpack(
            CNA_PACKED_VECTOR_FORMAT_ALPHA8, UINT64_C(0x100), &vector) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_unpack(
            CNA_PACKED_VECTOR_FORMAT_BGR565, UINT64_C(0x10000), &vector) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_unpack(
            CNA_PACKED_VECTOR_FORMAT_BYTE4, UINT64_C(0x100000000), &vector) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector_equals(vector, (CNA_Vector4){11.0F, 22.0F, 33.0F, 44.0F}) ||
        cna_packed_vector_equals(
            CNA_PACKED_VECTOR_FORMAT_ALPHA8, UINT64_C(0x100), 0U, &boolean) !=
            CNA_RESULT_INVALID_ARGUMENT || boolean != UINT8_C(77) ||
        cna_packed_vector_not_equals(
            CNA_PACKED_VECTOR_FORMAT_BGR565, 0U, UINT64_C(0x10000), &boolean) !=
            CNA_RESULT_INVALID_ARGUMENT || boolean != UINT8_C(77)) {
        return 0;
    }

    if (cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_ALPHA8,
            (CNA_Vector4){NAN, NAN, NAN, NAN}, &packed) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_BGR565,
            (CNA_Vector4){0.0F, 0.0F, INFINITY, NAN}, &packed) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2,
            (CNA_Vector4){NAN, 0.0F, NAN, NAN}, &packed) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_RGBA64,
            (CNA_Vector4){0.0F, 0.0F, 0.0F, -INFINITY}, &packed) !=
            CNA_RESULT_INVALID_ARGUMENT || packed != UINT64_C(0x1122334455667788)) {
        return 0;
    }

    if (cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_ALPHA8,
            (CNA_Vector4){NAN, INFINITY, -INFINITY, 1.0F}, &packed) != CNA_RESULT_SUCCESS ||
        packed != UINT64_C(0xff) ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_BGR565,
            (CNA_Vector4){1.0F, 1.0F, 1.0F, NAN}, &packed) != CNA_RESULT_SUCCESS ||
        packed != UINT64_C(0xffff) ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_SHORT2,
            (CNA_Vector4){1.0F, 2.0F, NAN, INFINITY}, &packed) != CNA_RESULT_SUCCESS ||
        packed != UINT64_C(0x00020001)) {
        return 0;
    }

    {
        CNA_ErrorInfo error = {sizeof(CNA_ErrorInfo), UINT32_C(1), 0U, 0U, 0U};
        vector = (CNA_Vector4){11.0F, 22.0F, 33.0F, 44.0F};
        if (cna_packed_vector_unpack(
                CNA_PACKED_VECTOR_FORMAT_ALPHA8, UINT64_C(0x100), &vector) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS ||
            error.category != CNA_ERROR_CATEGORY_RANGE) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    if (!validate_all_formats()) {
        return 1;
    }
    if (!validate_half_helpers_and_special_values()) {
        return 2;
    }
    if (!validate_failures_are_atomic()) {
        return 3;
    }
    return 0;
}
