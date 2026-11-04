// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"
#include "CnaReconSmoke.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
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
        REC_EQ(cna_packed_vector_pack(test_case->format, test_case->source, &packed),
               CNA_RESULT_SUCCESS);
        REC_EQ(packed, test_case->expected_packed);
        REC_EQ(cna_packed_vector_unpack(test_case->format, packed, &unpacked), CNA_RESULT_SUCCESS);
        if (!vector_equals(unpacked, test_case->expected_unpacked)) {
            fprintf(stderr, "format=%u unpacked=(%.9g,%.9g,%.9g,%.9g) expected=(%.9g,%.9g,%.9g,%.9g)\n",
                    test_case->format, unpacked.x, unpacked.y, unpacked.z, unpacked.w,
                    test_case->expected_unpacked.x, test_case->expected_unpacked.y,
                    test_case->expected_unpacked.z, test_case->expected_unpacked.w);
            failed = 1;
            goto cleanup;
        }
        /* ToVector2/ToVector3 are the x/y[/z] projection of ToVector4.
         * C copies those lanes into its own value; no new packed object is owned. */
        const CNA_Vector2 projected2 = {unpacked.x, unpacked.y};
        const CNA_Vector3 projected3 = {unpacked.x, unpacked.y, unpacked.z};
        REC_EQ(nearly_equal(projected2.x, test_case->expected_unpacked.x), 1);
        REC_EQ(nearly_equal(projected2.y, test_case->expected_unpacked.y), 1);
        REC_EQ(nearly_equal(projected3.z, test_case->expected_unpacked.z), 1);
        REC_EQ(cna_packed_vector_equals(test_case->format, packed, packed, &equal),
               CNA_RESULT_SUCCESS);
        REC_EQ(equal, CNA_TRUE);
        REC_EQ(cna_packed_vector_equals(test_case->format, packed, packed ^ UINT64_C(1), &opposite),
               CNA_RESULT_SUCCESS);
        REC_EQ(opposite, CNA_FALSE);
        REC_EQ(cna_packed_vector_not_equals(test_case->format, packed, packed ^ UINT64_C(1), &not_equal),
               CNA_RESULT_SUCCESS);
        REC_EQ(not_equal, CNA_TRUE);
        REC_EQ(cna_packed_vector_not_equals(test_case->format, packed, packed, &opposite),
               CNA_RESULT_SUCCESS);
        REC_EQ(opposite, CNA_FALSE);
        REC_EQ(cna_packed_vector_equals(test_case->format, packed, packed, NULL),
               CNA_RESULT_INVALID_ARGUMENT);
        REC_EQ(cna_packed_vector_unpack(test_case->format, packed, NULL), CNA_RESULT_INVALID_ARGUMENT);
    }
cleanup:
    failed |= cna_smoke_cleanup(&smoke);
    return !failed;
}

static int validate_half_helpers_and_special_values(void)
{
    uint16_t half = UINT16_MAX;
    float single = -9.0F;
    uint64_t packed = 0U;
    CNA_Vector4 unpacked = {0.0F, 0.0F, 0.0F, 0.0F};
    const struct {
        uint32_t single_bits;
        uint16_t half_bits;
        float decoded;
    } cases[] = {
        {UINT32_C(0x00000000), UINT16_C(0x0000), 0.0F},
        {UINT32_C(0x80000000), UINT16_C(0x8000), -0.0F},
        {UINT32_C(0x33800000), UINT16_C(0x0001), 0x1p-24F},
        {UINT32_C(0x3f800000), UINT16_C(0x3c00), 1.0F},
        {UINT32_C(0xc0000000), UINT16_C(0xc000), -2.0F},
        {UINT32_C(0x47800000), UINT16_C(0x7c00), 65536.0F},
        {UINT32_C(0x47c00000), UINT16_C(0x7e00), 98304.0F},
        {UINT32_C(0x47ffe000), UINT16_C(0x7fff), 131008.0F},
        {UINT32_C(0xc7800000), UINT16_C(0xfc00), -65536.0F},
        {UINT32_C(0xc7c00000), UINT16_C(0xfe00), -98304.0F},
        {UINT32_C(0x47ffefff), UINT16_C(0x7fff), 131008.0F},
        {UINT32_C(0x47fff000), UINT16_C(0x7fff), 131008.0F},
        {UINT32_C(0xc7fff000), UINT16_C(0xffff), -131008.0F},
        {UINT32_C(0x7f800000), UINT16_C(0x7fff), 131008.0F},
        {UINT32_C(0xff800000), UINT16_C(0xffff), -131008.0F},
        {UINT32_C(0x7fc00000), UINT16_C(0x7fff), 131008.0F},
        {UINT32_C(0xffc00000), UINT16_C(0xffff), -131008.0F}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        float input;
        int32_t bits;
        memcpy(&input, &cases[i].single_bits, sizeof(input));
        memcpy(&bits, &cases[i].single_bits, sizeof(bits));
        if (cna_half_from_single(input, &half) != CNA_RESULT_SUCCESS ||
            half != cases[i].half_bits ||
            cna_half_from_single_bits(bits, &half) != CNA_RESULT_SUCCESS ||
            half != cases[i].half_bits ||
            cna_half_to_single(half, &single) != CNA_RESULT_SUCCESS ||
            single != cases[i].decoded ||
            !!signbit(single) != !!signbit(cases[i].decoded)) {
            return 0;
        }
    }

    if (cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE,
            (CNA_Vector4){copysignf(NAN, 1.0F), 0.0F, 0.0F, 0.0F}, &packed) !=
            CNA_RESULT_SUCCESS || packed != UINT64_C(0x7fff) ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || unpacked.x != 131008.0F ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2,
            (CNA_Vector4){INFINITY, -INFINITY, 0.0F, 0.0F}, &packed) != CNA_RESULT_SUCCESS ||
        packed != UINT64_C(0xffff7fff) ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || unpacked.x != 131008.0F || unpacked.y != -131008.0F ||
        cna_packed_vector_pack(
            CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4,
            (CNA_Vector4){copysignf(NAN, 1.0F), INFINITY, -INFINITY, -0.0F}, &packed) !=
            CNA_RESULT_SUCCESS || packed != UINT64_C(0x8000ffff7fff7fff) ||
        cna_packed_vector_unpack(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4, packed, &unpacked) !=
            CNA_RESULT_SUCCESS || unpacked.x != 131008.0F || unpacked.y != 131008.0F ||
        unpacked.z != -131008.0F || unpacked.w != 0.0F || !signbit(unpacked.w)) {
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

/* C contract packed_vectors.h:53-59: only consumed integer lanes must be finite;
 * failure preserves output. This deliberately differs from native XNA NaN=>0. */
static int validate_each_consumed_component(void)
{
    static const unsigned consumed[] = {8,7,15,15,15,1,3,15,3,15,3,15,3,15,15,3,15};
    static const uint64_t widths[] = {8,16,16,16,32,16,32,64,16,32,32,64,32,32,64,32,64};
    const float nonfinite[] = {NAN, INFINITY, -INFINITY};
    for (uint32_t f=0; f<=CNA_PACKED_VECTOR_FORMAT_SHORT4; ++f) {
        const int half = f>=5 && f<=7;
        for (unsigned channel=0; channel<4; ++channel) {
            for (unsigned n=0; n<3; ++n) {
                float c[4] = {0,0,0,0}; c[channel]=nonfinite[n];
                uint64_t packed=UINT64_C(0x1122334455667788);
                const CNA_Result result=cna_packed_vector_pack(f,(CNA_Vector4){c[0],c[1],c[2],c[3]},&packed);
                if (!half && (consumed[f] & (1u<<channel))) {
                    if (result!=CNA_RESULT_INVALID_ARGUMENT || packed!=UINT64_C(0x1122334455667788)) return 0;
                } else {
                    /* HalfUtils.Pack IL_001b..002a saturates either NaN sign or infinity;
                     * force positive NaN sign to keep the bit expectation unambiguous. */
                    if (n==0) {
                        c[channel]=copysignf(NAN,1.0f);
                        if (cna_packed_vector_pack(f,(CNA_Vector4){c[0],c[1],c[2],c[3]},&packed)!=CNA_RESULT_SUCCESS) return 0;
                    }
                    const uint64_t expected=half && (consumed[f] & (1u<<channel)) ?
                        (UINT64_C(0x7fff) | (n==2?UINT64_C(0x8000):0)) << (16*channel) : 0;
                    if (result!=CNA_RESULT_SUCCESS || packed!=expected) return 0;
                }
            }
        }
        if (widths[f]<64) {
            const uint64_t excess=UINT64_C(1)<<widths[f];
            CNA_Vector4 value={11,22,33,44}; CNA_Bool b=77;
            if (cna_packed_vector_unpack(f,excess,&value)!=CNA_RESULT_INVALID_ARGUMENT ||
                (value.x!=11 || value.y!=22 || value.z!=33 || value.w!=44)) return 0;
            if (cna_packed_vector_equals(f,excess,0,&b)!=CNA_RESULT_INVALID_ARGUMENT || b!=77 ||
                cna_packed_vector_equals(f,0,excess,&b)!=CNA_RESULT_INVALID_ARGUMENT || b!=77 ||
                cna_packed_vector_not_equals(f,excess,0,&b)!=CNA_RESULT_INVALID_ARGUMENT || b!=77 ||
                cna_packed_vector_not_equals(f,0,excess,&b)!=CNA_RESULT_INVALID_ARGUMENT || b!=77) return 0;
        }
    }
    /* PackUtils.ClampAndRound IL_0036..003d: ties even, positive AND negative.
     * These words are hand assembled, little-endian fields, not native-CNA expectations. */
    const struct { uint32_t format; CNA_Vector4 input; uint64_t bits; } cases[] = {
        {4,{0.5f,1.5f,2.5f,3.5f},UINT64_C(0x04020200)},
        {15,{-0.5f,-1.5f,0,0},UINT64_C(0xfffe0000)},
        {16,{-0.5f,-1.5f,-2.5f,-3.5f},UINT64_C(0xfffcfffefffe0000)},
        {3,{0,0,0,0.5f},0}, /* one-bit alpha: 0.5 rounds to even 0 */
        {13,{0,0,0,0.5f},UINT64_C(0x80000000)} /* 0.5*3=1.5 -> 2 */
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        uint64_t bits=99;
        if (cna_packed_vector_pack(cases[i].format,cases[i].input,&bits)!=CNA_RESULT_SUCCESS || bits!=cases[i].bits) return 0;
    }
    return 1;
}

int main(void)
{
    if (!validate_each_consumed_component()) {
        return CNA_TEST_FAIL(4);
    }
    if (!validate_all_formats()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_half_helpers_and_special_values()) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_failures_are_atomic()) {
        return CNA_TEST_FAIL(3);
    }
    return 0;
}
