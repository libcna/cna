// SPDX-License-Identifier: MS-PL
//
// plans/plan_fna3d.md FNA3D-27a: regression coverage for the `float4x3` parameter packing.
//
// This is the arithmetic that puts SkinnedEffect's bone palette into the shader. It shipped wrong
// and no test caught it, because the failure mode is invisible to every obvious check: a wrongly
// transposed IDENTITY matrix is still an identity matrix, and the bone palette is overwhelmingly
// identities. Only a bone carrying a translation reveals it -- and the translation row is exactly
// the part the old code dropped, so every translation bone silently became an identity bone and
// skinned geometry rendered unmoved.
//
// The XNA oracle corpus is what found it (skinned_twobone_quad's geometry sat 26 pixels left of
// real XNA's, which is precisely a 0.4-unit bone translation at 0.5 weight). These tests pin the
// arithmetic so it cannot regress without a red unit test rather than a red image.
#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dStockEffects.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <array>

namespace
{
    using CNA::Internal::Renderers::Fna3d::PackMatrix4x3;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    /// The source layout every caller uses: whatever `Matrix::ToColumnMajor` actually emits.
    std::array<float, 16> Flatten(const Matrix& matrix)
    {
        std::array<float, 16> out{};
        matrix.ToColumnMajor(out.data());
        return out;
    }
}

TEST(Fna3dMatrixPackingTests, IdentityPacksToThreeUnitRegisters)
{
    const std::array<float, 16> source = Flatten(Matrix::getIdentityProperty());
    std::array<float, 12> packed{};
    PackMatrix4x3(source.data(), packed.data());

    const std::array<float, 12> expected{ 1, 0, 0, 0,
                                          0, 1, 0, 0,
                                          0, 0, 1, 0 };
    EXPECT_EQ(packed, expected);
}

TEST(Fna3dMatrixPackingTests, TranslationLandsInTheLastSlotOfEachRegister)
{
    // The regression itself. M41/M42/M43 are the translation row; in the packed float4x3 they are
    // the fourth float of each register. The old code copied the source's first twelve floats,
    // which contain none of them -- so this test fails against that code and passes against the
    // transposing one.
    const Matrix translation = Matrix::CreateTranslation(Vector3(0.4f, -2.0f, 3.5f));
    const std::array<float, 16> source = Flatten(translation);
    std::array<float, 12> packed{};
    PackMatrix4x3(source.data(), packed.data());

    EXPECT_FLOAT_EQ(packed[3], 0.4f);   // M41
    EXPECT_FLOAT_EQ(packed[7], -2.0f);  // M42
    EXPECT_FLOAT_EQ(packed[11], 3.5f);  // M43

    // ... and the rotational part is still an identity basis.
    EXPECT_FLOAT_EQ(packed[0], 1.0f);
    EXPECT_FLOAT_EQ(packed[5], 1.0f);
    EXPECT_FLOAT_EQ(packed[10], 1.0f);
}

TEST(Fna3dMatrixPackingTests, EveryElementLandsWhereFnaWouldPutIt)
{
    // A matrix whose every element is distinct, so a transposition error cannot hide. The expected
    // values are FNA's EffectParameter.SetValue(Matrix[]) for ColumnCount=4/RowCount=3, written
    // out by hand: M11,M21,M31,M41, M12,M22,M32,M42, M13,M23,M33,M43.
    Matrix m;
    m.M11 = 11; m.M12 = 12; m.M13 = 13; m.M14 = 14;
    m.M21 = 21; m.M22 = 22; m.M23 = 23; m.M24 = 24;
    m.M31 = 31; m.M32 = 32; m.M33 = 33; m.M34 = 34;
    m.M41 = 41; m.M42 = 42; m.M43 = 43; m.M44 = 44;

    const std::array<float, 16> source = Flatten(m);
    std::array<float, 12> packed{};
    PackMatrix4x3(source.data(), packed.data());

    const std::array<float, 12> expected{ 11, 21, 31, 41,
                                          12, 22, 32, 42,
                                          13, 23, 33, 43 };
    EXPECT_EQ(packed, expected);

    // The fourth column (M14/M24/M34/M44) is what a float4x3 legitimately has no room for, and is
    // the only thing that may be dropped.
    for (const float value : packed)
    {
        EXPECT_NE(value, 14.0f);
        EXPECT_NE(value, 44.0f);
    }
}

TEST(Fna3dMatrixPackingTests, PackingIsIndependentPerMatrixInAnArray)
{
    // The bone palette is an array; a stride mistake would leak one bone's rows into the next.
    const Matrix first = Matrix::CreateTranslation(Vector3(1.0f, 0.0f, 0.0f));
    const Matrix second = Matrix::CreateTranslation(Vector3(0.0f, 2.0f, 0.0f));

    std::array<float, 32> source{};
    first.ToColumnMajor(source.data());
    second.ToColumnMajor(source.data() + 16);

    std::array<float, 24> packed{};
    PackMatrix4x3(source.data(), packed.data());
    PackMatrix4x3(source.data() + 16, packed.data() + 12);

    EXPECT_FLOAT_EQ(packed[3], 1.0f);    // first bone's M41
    EXPECT_FLOAT_EQ(packed[7], 0.0f);    // first bone's M42
    EXPECT_FLOAT_EQ(packed[12 + 3], 0.0f);  // second bone's M41
    EXPECT_FLOAT_EQ(packed[12 + 7], 2.0f);  // second bone's M42
}

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
