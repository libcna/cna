// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_metal.md METAL-34-style extraction: this formula is pure C++ (plain float arrays, basic
// arithmetic) with zero Objective-C/Metal-framework dependency, so -- like MetalPipelineKey.hpp --
// it can be genuinely unit-tested on any platform without an Apple toolchain, unlike the rest of
// the Metal renderer. MetalRenderer.mm includes this header instead of defining the function
// inline; logic is unchanged from the original inline definition.
namespace CNA::Internal::Renderers::Metal
{
    // Normal matrix = transpose(inverse(world3x3)), via the cofactor/determinant shortcut --
    // ported verbatim from EasyGLRenderer::BindDrawParams()'s own real formula (Task 398:
    // handles non-uniform-scale World transforms correctly, unlike the raw upper-left 3x3). Reads
    // directly from GpuDrawParams::worldColMajor (already column-major, already provided by the
    // shared XNA-facing effect layer) rather than re-deriving a world matrix independently, so
    // there is no separate risk of a row-major/column-major mixup here. Output is 3 separate
    // 4-float "columns" (xyz + unused pad), matching LitTransform's own layout.
    /**
     * @brief Computes the inverse-transpose normal matrix from a column-major world matrix.
     *
     * @param w Source column-major four-by-four world matrix.
     * @param col0 Receives the first padded normal-matrix column.
     * @param col1 Receives the second padded normal-matrix column.
     * @param col2 Receives the third padded normal-matrix column.
     */
    inline void ComputeMetalNormalMatrixCols(const float* w, float col0[4],
                                              float col1[4], float col2[4])
    {
        const float a=w[0], d=w[1], g=w[2];
        const float b=w[4], e=w[5], h=w[6];
        const float c=w[8], f=w[9], i=w[10];
        const float det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
        const float invDet = (det != 0.0f) ? (1.0f/det) : 0.0f;
        // Independently re-derived and hand-verified (not just transcribed) that this produces
        // exactly transpose(inverse(M)) when consumed as float3x3(col0,col1,col2) in MSL (a
        // columns-constructor): EasyGL's own `nm[9]` is inv(M) written out ROW-major
        // (nm[0..2]=row0, nm[3..5]=row1, nm[6..8]=row2), fed to glUniformMatrix3fv(transpose=
        // GL_FALSE) which reads COLUMN-major -- so GL ends up storing transpose(inv(M)), the
        // correct result, without an explicit transpose step. Each `colN` group below is
        // literally EasyGL's `nm[3*N .. 3*N+2]` (row N of inv(M)), which is exactly column N of
        // transpose(inv(M)) -- i.e. the same target matrix, reached the same way, just spelled
        // for MSL's column-vector constructor instead of GL's transpose-flag uniform upload.
        col0[0]=(e*i-f*h)*invDet; col0[1]=-(b*i-c*h)*invDet; col0[2]=(b*f-c*e)*invDet; col0[3]=0;
        col1[0]=-(d*i-f*g)*invDet; col1[1]=(a*i-c*g)*invDet; col1[2]=-(a*f-c*d)*invDet; col1[3]=0;
        col2[0]=(d*h-e*g)*invDet; col2[1]=-(a*h-b*g)*invDet; col2[2]=(a*e-b*d)*invDet; col2[3]=0;
    }
}
