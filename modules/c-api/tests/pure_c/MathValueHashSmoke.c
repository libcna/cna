/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include <stdint.h>
#include <string.h>
#include "CnaTestReport.h"

static int validate_signed_zero_hashes(void)
{
    for (unsigned mask = 0; mask < 16; ++mask) {
        const float x = (mask & 1U) ? -0.0F : 0.0F;
        const float y = (mask & 2U) ? -0.0F : 0.0F;
        const float z = (mask & 4U) ? -0.0F : 0.0F;
        const float w = (mask & 8U) ? -0.0F : 0.0F;
        int32_t hash = 99;
        if (cna_vector2_get_hash_code((CNA_Vector2){x, y}, &hash) != CNA_RESULT_SUCCESS || hash != 0 ||
            cna_vector3_get_hash_code((CNA_Vector3){x, y, z}, &hash) != CNA_RESULT_SUCCESS || hash != 0 ||
            cna_vector4_get_hash_code((CNA_Vector4){x, y, z, w}, &hash) != CNA_RESULT_SUCCESS || hash != 0 ||
            cna_quaternion_get_hash_code((CNA_Quaternion){x, y, z, w}, &hash) != CNA_RESULT_SUCCESS || hash != 0 ||
            cna_curve_key_get_hash_code((CNA_CurveKey){x, y, z, w, CNA_CURVE_CONTINUITY_STEP}, &hash) !=
                CNA_RESULT_SUCCESS || hash != 1) {
            return 0;
        }
    }
    CNA_Matrix matrix = {0};
    float* fields[] = {&matrix.m11, &matrix.m12, &matrix.m13, &matrix.m14,
        &matrix.m21, &matrix.m22, &matrix.m23, &matrix.m24,
        &matrix.m31, &matrix.m32, &matrix.m33, &matrix.m34,
        &matrix.m41, &matrix.m42, &matrix.m43, &matrix.m44};
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        int32_t hash = 99;
        *fields[i] = -0.0F;
        if (cna_matrix_get_hash_code(matrix, &hash) != CNA_RESULT_SUCCESS || hash != 0) return 0;
        *fields[i] = 0.0F;
    }
    return 1;
}

static int validate_nonzero_hash_bits(void)
{
    const uint32_t samples[] = {UINT32_C(0x3f800000), UINT32_C(0xbf800000),
        UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fc00001), UINT32_C(0xffc12345)};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        float value;
        memcpy(&value, &samples[i], sizeof(value));
        int32_t hash = 0;
        CNA_Matrix matrix = {0};
        matrix.m11 = value;
        if (cna_vector2_get_hash_code((CNA_Vector2){value, 0}, &hash) != CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i] ||
            cna_vector3_get_hash_code((CNA_Vector3){value, 0, 0}, &hash) != CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i] ||
            cna_vector4_get_hash_code((CNA_Vector4){value, 0, 0, 0}, &hash) != CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i] ||
            cna_quaternion_get_hash_code((CNA_Quaternion){value, 0, 0, 0}, &hash) != CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i] ||
            cna_curve_key_get_hash_code((CNA_CurveKey){value, 0, 0, 0, CNA_CURVE_CONTINUITY_SMOOTH}, &hash) !=
                CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i] ||
            cna_matrix_get_hash_code(matrix, &hash) != CNA_RESULT_SUCCESS || (uint32_t)hash != samples[i]) return 0;
    }
    return 1;
}

static int validate_curve_key_combination(void)
{
    int32_t hash = 0;
    return cna_curve_key_get_hash_code((CNA_CurveKey){-0.0F, 1, 2, 3, CNA_CURVE_CONTINUITY_SMOOTH}, &hash) ==
        CNA_RESULT_SUCCESS && (uint32_t)hash == UINT32_C(0xbfc00000) &&
        cna_curve_key_get_hash_code((CNA_CurveKey){0, 1, 2, 3, CNA_CURVE_CONTINUITY_STEP}, &hash) ==
        CNA_RESULT_SUCCESS && (uint32_t)hash == UINT32_C(0xbfc00001);
}

int main(void)
{
    /* Run every group even when one fails, so adjacent controls stay visible. */
    const int zero = CNA_TEST_STAGE(validate_signed_zero_hashes());
    const int nonzero = CNA_TEST_STAGE(validate_nonzero_hash_bits());
    const int curve = CNA_TEST_STAGE(validate_curve_key_combination());
    return zero && nonzero && curve ? 0 : 1;
}
