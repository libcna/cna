// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    template<class Action>
    void ExpectRangeRefusal(Action&& action, const char* parameter)
    {
        try
        {
            action();
            ADD_FAILURE() << "Expected ArgumentOutOfRangeException for " << parameter;
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), parameter);
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << "Wrong exception for " << parameter << ": " << error.what();
        }
    }
}

TEST(MatrixPerspectiveCompatibilityTest, FieldOfViewUsesPublicPiAndValidatesAngleFirst)
{
    // CNA-DEMOS-ISSUE-117; value/out forms must refuse before touching an output.
    for (const float angle : {0.0f, -1.0f, MathHelper::Pi,
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()})
    {
        SCOPED_TRACE(angle);
        ExpectRangeRefusal([&] { (void)Matrix::CreatePerspectiveFieldOfView(angle, 1, 0, 0); },
                           "fieldOfView");
        Matrix output = Matrix::getIdentityProperty();
        ExpectRangeRefusal([&] { Matrix::CreatePerspectiveFieldOfView(angle, 1, 1, 10, output); },
                           "fieldOfView");
        EXPECT_EQ(output, Matrix::getIdentityProperty());
    }
    const float belowPi = std::nextafter(MathHelper::Pi, 0.0f);
    EXPECT_GT(Matrix::CreatePerspectiveFieldOfView(belowPi, 1, 1, 10).M22, 0.0f);
}

TEST(MatrixPerspectiveCompatibilityTest, DepthExceptionTypeParameterOrderAndOutputAreXnaCompatible)
{
    struct Factory
    {
        const char* name;
        std::function<Matrix(float, float)> value;
        std::function<void(float, float, Matrix&)> output;
    };
    const std::array<Factory, 3> factories = {{
        {"size", [](float n, float f) { return Matrix::CreatePerspective(2, 2, n, f); },
            [](float n, float f, Matrix& r) { Matrix::CreatePerspective(2, 2, n, f, r); }},
        {"fieldOfView", [](float n, float f) { return Matrix::CreatePerspectiveFieldOfView(1, 1, n, f); },
            [](float n, float f, Matrix& r) { Matrix::CreatePerspectiveFieldOfView(1, 1, n, f, r); }},
        {"offCenter", [](float n, float f) { return Matrix::CreatePerspectiveOffCenter(-1, 1, -1, 1, n, f); },
            [](float n, float f, Matrix& r) { Matrix::CreatePerspectiveOffCenter(-1, 1, -1, 1, n, f, r); }}
    }};
    struct Refusal { float nearPlane, farPlane; const char* parameter; };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Refusal, 8> refusals = {{
        {0, 10, "nearPlaneDistance"}, {-1, 10, "nearPlaneDistance"},
        {1, 0, "farPlaneDistance"}, {10, 10, "nearPlaneDistance"},
        {10, 1, "nearPlaneDistance"}, {0, 0, "nearPlaneDistance"},
        {nan, 0, "farPlaneDistance"}, {0, nan, "nearPlaneDistance"}
    }};
    const Matrix sentinel = Matrix::CreateTranslation(3, 4, 5);
    for (const auto& factory : factories)
    {
        SCOPED_TRACE(factory.name);
        for (const auto& refusal : refusals)
        {
            SCOPED_TRACE(::testing::Message() << refusal.nearPlane << ", " << refusal.farPlane);
            ExpectRangeRefusal([&] { (void)factory.value(refusal.nearPlane, refusal.farPlane); },
                               refusal.parameter);
            Matrix result = sentinel;
            ExpectRangeRefusal([&] { factory.output(refusal.nearPlane, refusal.farPlane, result); },
                               refusal.parameter);
            EXPECT_EQ(result, sentinel);
        }
    }
}

TEST(MatrixPerspectiveCompatibilityTest, DegenerateDimensionsRemainRawIeeeInputs)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(Matrix::CreatePerspectiveFieldOfView(1, 0, 1, 10).M11, infinity);
    EXPECT_LT(Matrix::CreatePerspectiveFieldOfView(1, -1, 1, 10).M11, 0.0f);
    EXPECT_EQ(Matrix::CreatePerspective(0, 1, 1, 10).M11, infinity);
    EXPECT_EQ(Matrix::CreatePerspective(1, 0, 1, 10).M22, infinity);
    EXPECT_EQ(Matrix::CreatePerspectiveOffCenter(1, 1, -1, 1, 1, 10).M11, infinity);
    EXPECT_EQ(Matrix::CreatePerspectiveOffCenter(-1, 1, 1, 1, 1, 10).M22, infinity);
    EXPECT_TRUE(std::isnan(Matrix::CreatePerspectiveFieldOfView(nan, 1, 1, 10).M22));
    ExpectRangeRefusal([&] { (void)Matrix::CreatePerspectiveFieldOfView(nan, 1, 0, 10); },
                       "nearPlaneDistance");
}
