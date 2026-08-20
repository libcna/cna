// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

// plans/plan_metal.md METAL-34-style extraction: this is the small row-major 4x4 matrix helper set used
// to build every 3D draw's WVP matrix (transpose(multiply(multiply(fromXna(world),fromXna(view)),
// fromXna(projection)))) -- plain float arithmetic plus Microsoft::Xna::Framework::Matrix (itself
// plain C++, zero Objective-C/Metal-framework dependency), so it compiles and runs on any platform
// CnaTests already builds for. MetalRenderer.mm includes this header instead of defining
// these inline; logic is unchanged from the original inline definitions.
namespace CNA::Internal::Renderers::Metal
{
    // Row-major storage: m[row*4+col], matching Microsoft::Xna::Framework::Matrix's own M<row><col>
    // field naming directly (M11 -> m[0], M12 -> m[1], ..., M21 -> m[4], ...).
    /** @brief Row-major four-by-four matrix used by portable Metal helpers. */
    struct MetalMat4 { float m[16]; };

    /**
     * @brief Multiplies two row-major Metal helper matrices.
     *
     * @param a Left-hand matrix.
     * @param b Right-hand matrix.
     * @return The matrix product.
     */
    [[nodiscard]] inline MetalMat4 MetalMat4Multiply(const MetalMat4& a, const MetalMat4& b)
    {
        MetalMat4 r{};
        for (int row=0; row<4; ++row)
            for (int col=0; col<4; ++col)
                for (int k=0; k<4; ++k)
                    r.m[row*4+col] += a.m[row*4+k] * b.m[k*4+col];
        return r;
    }

    /**
     * @brief Copies an XNA matrix into Metal's row-major helper representation.
     *
     * @param x XNA matrix to copy.
     * @return The equivalent Metal helper matrix.
     */
    [[nodiscard]] inline MetalMat4 MetalMat4FromXna(const Microsoft::Xna::Framework::Matrix& x)
    {
        return {{x.M11,x.M12,x.M13,x.M14, x.M21,x.M22,x.M23,x.M24,
                 x.M31,x.M32,x.M33,x.M34, x.M41,x.M42,x.M43,x.M44}};
    }

    /**
     * @brief Transposes a Metal helper matrix.
     *
     * @param x Matrix to transpose.
     * @return The transposed matrix.
     */
    [[nodiscard]] inline MetalMat4 MetalMat4Transpose(const MetalMat4& x)
    {
        MetalMat4 r{}; for(int i=0;i<4;++i) for(int j=0;j<4;++j) r.m[j*4+i]=x.m[i*4+j]; return r;
    }
}
