// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34-style extraction: ComputeMetalNormalMatrixCols() is pure C++ (plain float
// arrays, basic arithmetic), so unlike the rest of the Metal renderer it compiles and runs on any
// platform CnaTests already builds for -- no #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// The MSL shader source's own comment already claims this formula was "independently re-derived
// and hand-verified" to equal transpose(inverse(world3x3)) -- this test file is that verification
// made real and repeatable: concrete numeric cases with an independently hand-derived expected
// answer (via the classic adjugate/cofactor method, not by re-reading the function's own code), plus
// a property-based check of the actual mathematical guarantee a normal matrix exists to provide
// (angle/perpendicularity preservation for a transformed tangent-plane pair under non-uniform
// scale), which would catch a real bug even if this test's own concrete expected values happened to
// share a transcription mistake with the function under test.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalNormalMatrix.hpp"
#include <cmath>

using namespace CNA::Internal::Renderers::Metal;

namespace
{
    constexpr float kEps = 1e-5f;

    // Column-major 4x4 identity, matching GpuDrawParams::worldColMajor's own real layout
    // (w[0..3]=col0, w[4..7]=col1, w[8..11]=col2, w[12..15]=col3/translation).
    void SetColumn(float w[16], int col, float x, float y, float z)
    {
        w[col*4+0] = x; w[col*4+1] = y; w[col*4+2] = z; w[col*4+3] = 0.0f;
    }

    // (M*v) using the SAME column-major convention ComputeMetalNormalMatrixCols() itself reads
    // (v.x scales column 0, v.y scales column 1, v.z scales column 2, summed) -- the standard
    // column-major 3x3 matrix-vector product, and the same one MSL's `float3x3(col0,col1,col2) *
    // vec` performs on the shader side.
    void Multiply3x3ColMajor(const float w[16], float vx, float vy, float vz, float out[3])
    {
        for (int row = 0; row < 3; ++row)
            out[row] = vx*w[0*4+row] + vy*w[1*4+row] + vz*w[2*4+row];
    }

    float Dot3(const float a[3], float bx, float by, float bz)
    {
        return a[0]*bx + a[1]*by + a[2]*bz;
    }
}

TEST(MetalNormalMatrix, IdentityProducesIdentity)
{
    float w[16] = {};
    SetColumn(w, 0, 1, 0, 0);
    SetColumn(w, 1, 0, 1, 0);
    SetColumn(w, 2, 0, 0, 1);
    float col0[4], col1[4], col2[4];
    ComputeMetalNormalMatrixCols(w, col0, col1, col2);

    EXPECT_NEAR(col0[0], 1.0f, kEps); EXPECT_NEAR(col0[1], 0.0f, kEps); EXPECT_NEAR(col0[2], 0.0f, kEps);
    EXPECT_NEAR(col1[0], 0.0f, kEps); EXPECT_NEAR(col1[1], 1.0f, kEps); EXPECT_NEAR(col1[2], 0.0f, kEps);
    EXPECT_NEAR(col2[0], 0.0f, kEps); EXPECT_NEAR(col2[1], 0.0f, kEps); EXPECT_NEAR(col2[2], 1.0f, kEps);
}

TEST(MetalNormalMatrix, UniformScaleProducesReciprocalScale)
{
    // A diagonal matrix's transpose is itself, and its inverse is the reciprocal diagonal --
    // transpose(inverse(diag(2,2,2))) = diag(0.5, 0.5, 0.5), independent of the cofactor formula
    // under test (a different, simpler derivation path reaching the same expected answer).
    float w[16] = {};
    SetColumn(w, 0, 2, 0, 0);
    SetColumn(w, 1, 0, 2, 0);
    SetColumn(w, 2, 0, 0, 2);
    float col0[4], col1[4], col2[4];
    ComputeMetalNormalMatrixCols(w, col0, col1, col2);

    EXPECT_NEAR(col0[0], 0.5f, kEps); EXPECT_NEAR(col0[1], 0.0f, kEps); EXPECT_NEAR(col0[2], 0.0f, kEps);
    EXPECT_NEAR(col1[0], 0.0f, kEps); EXPECT_NEAR(col1[1], 0.5f, kEps); EXPECT_NEAR(col1[2], 0.0f, kEps);
    EXPECT_NEAR(col2[0], 0.0f, kEps); EXPECT_NEAR(col2[1], 0.0f, kEps); EXPECT_NEAR(col2[2], 0.5f, kEps);
}

TEST(MetalNormalMatrix, NonUniformNonDiagonalMatrixMatchesHandDerivedAdjugate)
{
    // World's 3x3 upper-left, as columns (matching this test's own SetColumn convention):
    //   col0 = (2, 1, 0), col1 = (0, 3, 0), col2 = (0, 0, 4)
    // i.e. M = | 2 0 0 |
    //          | 1 3 0 |
    //          | 0 0 4 |
    // Independently hand-derived via the classic adjugate/cofactor method (transpose(inverse(M)) =
    // transpose(adjugate(M))/det(M), NOT the same code path ComputeMetalNormalMatrixCols() itself
    // uses, which multiplies row-0-expansion cofactors directly): det(M)=24,
    // adjugate(M) = [[12,0,0],[-4,8,0],[0,0,6]], inverse(M) = adjugate(M)/24, and
    // transpose(inverse(M)) = [[0.5,0,0],[-1/6,1/3,0],[0,0,0.25]] -- whose COLUMNS are exactly the
    // col0/col1/col2 expected below.
    float w[16] = {};
    SetColumn(w, 0, 2, 1, 0);
    SetColumn(w, 1, 0, 3, 0);
    SetColumn(w, 2, 0, 0, 4);
    float col0[4], col1[4], col2[4];
    ComputeMetalNormalMatrixCols(w, col0, col1, col2);

    EXPECT_NEAR(col0[0], 0.5f, kEps);      EXPECT_NEAR(col0[1], 0.0f, kEps);      EXPECT_NEAR(col0[2], 0.0f, kEps);
    EXPECT_NEAR(col1[0], -1.0f/6.0f, kEps); EXPECT_NEAR(col1[1], 1.0f/3.0f, kEps); EXPECT_NEAR(col1[2], 0.0f, kEps);
    EXPECT_NEAR(col2[0], 0.0f, kEps);      EXPECT_NEAR(col2[1], 0.0f, kEps);      EXPECT_NEAR(col2[2], 0.25f, kEps);
}

TEST(MetalNormalMatrix, PreservesPerpendicularityUnderNonUniformScale)
{
    // The actual mathematical property a normal matrix exists to guarantee: for a tangent vector T
    // and normal N that are perpendicular in object space, transforming T by the ordinary world
    // matrix and N by the normal matrix must keep them perpendicular in world space -- even though
    // this specific M applies a non-uniform scale that would break perpendicularity if N were
    // (incorrectly) transformed by the same M instead of transpose(inverse(M)). This is
    // property-based, not a re-transcription check: it would catch a real bug even if this test's
    // own hand-derived expected values in the case above happened to share a transcription mistake
    // with the function under test.
    float w[16] = {};
    SetColumn(w, 0, 2, 1, 0);
    SetColumn(w, 1, 0, 3, 0);
    SetColumn(w, 2, 0, 1, 4);
    float col0[4], col1[4], col2[4];
    ComputeMetalNormalMatrixCols(w, col0, col1, col2);

    // A tangent vector lying in the plane whose normal is (0,1,0) in object space -- any vector
    // with y=0 is perpendicular to (0,1,0).
    const float tx = 1.0f, ty = 0.0f, tz = 2.0f;
    const float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    ASSERT_NEAR(tx*nx + ty*ny + tz*nz, 0.0f, kEps) << "test setup: T and N must start perpendicular";

    float worldT[3];
    Multiply3x3ColMajor(w, tx, ty, tz, worldT);

    float worldN[3] = {
        nx*col0[0] + ny*col1[0] + nz*col2[0],
        nx*col0[1] + ny*col1[1] + nz*col2[1],
        nx*col0[2] + ny*col1[2] + nz*col2[2],
    };

    // Sanity: this M is not orthogonal (it doesn't preserve lengths/angles on its own), so a
    // (deliberately wrong) transform of N by M directly would NOT stay perpendicular to worldT --
    // confirming this test setup actually exercises the non-uniform-scale case the normal matrix is
    // supposed to correct for, not a degenerate case where any transform would pass trivially.
    float wrongWorldN[3];
    Multiply3x3ColMajor(w, nx, ny, nz, wrongWorldN);
    EXPECT_GT(std::fabs(Dot3(wrongWorldN, worldT[0], worldT[1], worldT[2])), kEps)
        << "test setup: naively transforming N by M should NOT stay perpendicular here";

    // The real assertion: the genuine normal-matrix-transformed N stays perpendicular to the
    // genuinely world-transformed T.
    EXPECT_NEAR(Dot3(worldN, worldT[0], worldT[1], worldT[2]), 0.0f, kEps);
}
