// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include "Microsoft/Xna/Framework/MathHelper.hpp"

using Microsoft::Xna::Framework::MathHelper;

static constexpr float kEps = 1e-5f;
TEST(MathHelperTest, ReturnsValueWhenWithinRange)
{
    EXPECT_EQ(MathHelper::Clamp(5, 1, 10), 5);
    EXPECT_EQ(MathHelper::Clamp(0, -5, 5), 0);
}

TEST(MathHelperTest, ClampsToMinWhenBelow)
{
    EXPECT_EQ(MathHelper::Clamp(-10, 0, 100), 0);
    EXPECT_EQ(MathHelper::Clamp(2, 3, 5), 3);
}

TEST(MathHelperTest, ClampsToMaxWhenAbove)
{
    EXPECT_EQ(MathHelper::Clamp(101, 0, 100), 100);
    EXPECT_EQ(MathHelper::Clamp(10, 0, 5), 5);
}

TEST(MathHelperTest, MinAndMaxEqual)
{
    EXPECT_EQ(MathHelper::Clamp(5, 5, 5), 5);
    EXPECT_EQ(MathHelper::Clamp(0, 5, 5), 5);
    EXPECT_EQ(MathHelper::Clamp(10, 5, 5), 5);
}

// --- Constants ---

TEST(MathHelperTest, PiConstant)
{
    EXPECT_NEAR(MathHelper::Pi, static_cast<float>(M_PI), 1e-6f);
}

TEST(MathHelperTest, TwoPiIsTwicePI)
{
    EXPECT_NEAR(MathHelper::TwoPi, 2.0f * MathHelper::Pi, kEps);
}

TEST(MathHelperTest, PiOver2IsHalfPi)
{
    EXPECT_NEAR(MathHelper::PiOver2, MathHelper::Pi / 2.0f, kEps);
}

TEST(MathHelperTest, PiOver4IsQuarterPi)
{
    EXPECT_NEAR(MathHelper::PiOver4, MathHelper::Pi / 4.0f, kEps);
}

// --- Lerp ---

TEST(MathHelperTest, LerpAtZeroReturnsFirstValue)
{
    EXPECT_NEAR(MathHelper::Lerp(0.0f, 10.0f, 0.0f), 0.0f, kEps);
}

TEST(MathHelperTest, LerpAtOneReturnsSecondValue)
{
    EXPECT_NEAR(MathHelper::Lerp(0.0f, 10.0f, 1.0f), 10.0f, kEps);
}

TEST(MathHelperTest, LerpAtHalfReturnsMidpoint)
{
    EXPECT_NEAR(MathHelper::Lerp(0.0f, 10.0f, 0.5f), 5.0f, kEps);
}

TEST(MathHelperTest, LerpNegativeRange)
{
    EXPECT_NEAR(MathHelper::Lerp(-10.0f, 10.0f, 0.5f), 0.0f, kEps);
}

// --- SmoothStep ---

TEST(MathHelperTest, SmoothStepAtZeroReturnsFirst)
{
    EXPECT_NEAR(MathHelper::SmoothStep(0.0f, 10.0f, 0.0f), 0.0f, kEps);
}

TEST(MathHelperTest, SmoothStepAtOneReturnsSecond)
{
    EXPECT_NEAR(MathHelper::SmoothStep(0.0f, 10.0f, 1.0f), 10.0f, kEps);
}

TEST(MathHelperTest, SmoothStepAtHalfReturnsMidpoint)
{
    // SmoothStep(0,10,0.5): t=0.5, 3t^2-2t^3 = 3*0.25 - 2*0.125 = 0.75 - 0.25 = 0.5 → 5
    EXPECT_NEAR(MathHelper::SmoothStep(0.0f, 10.0f, 0.5f), 5.0f, kEps);
}

TEST(MathHelperTest, SmoothStepClampsBelowZero)
{
    EXPECT_NEAR(MathHelper::SmoothStep(0.0f, 10.0f, -1.0f), 0.0f, kEps);
}

TEST(MathHelperTest, SmoothStepClampsAboveOne)
{
    EXPECT_NEAR(MathHelper::SmoothStep(0.0f, 10.0f, 2.0f), 10.0f, kEps);
}

// --- ToDegrees / ToRadians ---

TEST(MathHelperTest, ToDegreesOfPiIs180)
{
    EXPECT_NEAR(MathHelper::ToDegrees(MathHelper::Pi), 180.0f, kEps);
}

TEST(MathHelperTest, ToDegreesOfPiOver2Is90)
{
    EXPECT_NEAR(MathHelper::ToDegrees(MathHelper::PiOver2), 90.0f, kEps);
}

TEST(MathHelperTest, ToDegreesOfZeroIsZero)
{
    EXPECT_NEAR(MathHelper::ToDegrees(0.0f), 0.0f, kEps);
}

TEST(MathHelperTest, ToRadiansOf180IsPi)
{
    EXPECT_NEAR(MathHelper::ToRadians(180.0f), MathHelper::Pi, kEps);
}

TEST(MathHelperTest, ToRadiansOf90IsPiOver2)
{
    EXPECT_NEAR(MathHelper::ToRadians(90.0f), MathHelper::PiOver2, kEps);
}

TEST(MathHelperTest, ToDegreesToRadiansRoundTrip)
{
    EXPECT_NEAR(MathHelper::ToRadians(MathHelper::ToDegrees(1.23f)), 1.23f, kEps);
}

// --- Distance ---

TEST(MathHelperTest, DistanceIsAbsoluteDifference)
{
    EXPECT_NEAR(MathHelper::Distance(1.0f, 5.0f), 4.0f, kEps);
    EXPECT_NEAR(MathHelper::Distance(5.0f, 1.0f), 4.0f, kEps);
    EXPECT_NEAR(MathHelper::Distance(-3.0f, 3.0f), 6.0f, kEps);
}

TEST(MathHelperTest, DistanceOfSameValueIsZero)
{
    EXPECT_NEAR(MathHelper::Distance(7.0f, 7.0f), 0.0f, kEps);
}

// --- Max / Min ---

TEST(MathHelperTest, MaxReturnsLargerValue)
{
    EXPECT_NEAR(MathHelper::Max(3.0f, 5.0f), 5.0f, kEps);
    EXPECT_NEAR(MathHelper::Max(-2.0f, -1.0f), -1.0f, kEps);
}

TEST(MathHelperTest, MinReturnsSmallerValue)
{
    EXPECT_NEAR(MathHelper::Min(3.0f, 5.0f), 3.0f, kEps);
    EXPECT_NEAR(MathHelper::Min(-2.0f, -1.0f), -2.0f, kEps);
}

TEST(MathHelperTest, MaxOfEqualValuesReturnsThatValue)
{
    EXPECT_NEAR(MathHelper::Max(4.0f, 4.0f), 4.0f, kEps);
}

// --- WrapAngle ---

TEST(MathHelperTest, WrapAngleZeroStaysZero)
{
    EXPECT_NEAR(MathHelper::WrapAngle(0.0f), 0.0f, kEps);
}

TEST(MathHelperTest, WrapAngleFullTurnWrapsToNearZero)
{
    EXPECT_NEAR(MathHelper::WrapAngle(MathHelper::TwoPi), 0.0f, kEps);
}

TEST(MathHelperTest, WrapAngleSmallPositiveUnchanged)
{
    EXPECT_NEAR(MathHelper::WrapAngle(1.0f), 1.0f, kEps);
}

TEST(MathHelperTest, WrapAngleSlightlyOverPiWrapsToNegative)
{
    float wrapped = MathHelper::WrapAngle(MathHelper::Pi + 0.1f);
    EXPECT_NEAR(wrapped, -(MathHelper::Pi - 0.1f), kEps);
}

// --- WithinEpsilon ---

TEST(MathHelperTest, WithinEpsilonSameValueIsTrue)
{
    EXPECT_TRUE(MathHelper::WithinEpsilon(1.0f, 1.0f));
}

TEST(MathHelperTest, WithinEpsilonFarApartIsFalse)
{
    EXPECT_FALSE(MathHelper::WithinEpsilon(0.0f, 1.0f));
}

// --- ClosestMSAAPower ---

TEST(MathHelperTest, ClosestMSAAPowerForPowerOfTwo)
{
    EXPECT_EQ(MathHelper::ClosestMSAAPower(2), 2);
    EXPECT_EQ(MathHelper::ClosestMSAAPower(4), 4);
    EXPECT_EQ(MathHelper::ClosestMSAAPower(8), 8);
}

TEST(MathHelperTest, ClosestMSAAPowerRoundsDownToNextPowerOfTwo)
{
    EXPECT_EQ(MathHelper::ClosestMSAAPower(3), 2);
    EXPECT_EQ(MathHelper::ClosestMSAAPower(5), 4);
    EXPECT_EQ(MathHelper::ClosestMSAAPower(7), 4);
}

TEST(MathHelperTest, ClosestMSAAPowerOneReturnsZero)
{
    // 1 is invalid for MSAA; special-cased to return 0
    EXPECT_EQ(MathHelper::ClosestMSAAPower(1), 0);
}

// --- Float Clamp overload ---

TEST(MathHelperTest, FloatClampInRange)
{
    EXPECT_NEAR(MathHelper::Clamp(5.0f, 1.0f, 10.0f), 5.0f, kEps);
}

TEST(MathHelperTest, FloatClampBelowMin)
{
    EXPECT_NEAR(MathHelper::Clamp(-1.0f, 0.0f, 1.0f), 0.0f, kEps);
}

TEST(MathHelperTest, FloatClampAboveMax)
{
    EXPECT_NEAR(MathHelper::Clamp(2.0f, 0.0f, 1.0f), 1.0f, kEps);
}

// --- Barycentric ---

TEST(MathHelperTest, BarycentricAtOrigin)
{
    // amount1=0, amount2=0 → value1
    EXPECT_NEAR(MathHelper::Barycentric(1.0f, 2.0f, 3.0f, 0.0f, 0.0f), 1.0f, kEps);
}

TEST(MathHelperTest, BarycentricFullWeight2)
{
    // amount1=1, amount2=0 → value2
    EXPECT_NEAR(MathHelper::Barycentric(1.0f, 2.0f, 3.0f, 1.0f, 0.0f), 2.0f, kEps);
}

TEST(MathHelperTest, BarycentricFullWeight3)
{
    // amount1=0, amount2=1 → value3
    EXPECT_NEAR(MathHelper::Barycentric(1.0f, 2.0f, 3.0f, 0.0f, 1.0f), 3.0f, kEps);
}

// --- CatmullRom ---

TEST(MathHelperTest, CatmullRomAtZeroReturnsValue2)
{
    // amount=0 → value2
    EXPECT_NEAR(MathHelper::CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 0.0f), 1.0f, kEps);
}

TEST(MathHelperTest, CatmullRomAtOneReturnsValue3)
{
    // amount=1 → value3
    EXPECT_NEAR(MathHelper::CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 1.0f), 2.0f, kEps);
}

TEST(MathHelperTest, CatmullRomAtHalfIsMidpoint)
{
    // Symmetric uniform points 0,1,2,3 → midpoint at 0.5 is 1.5
    EXPECT_NEAR(MathHelper::CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 0.5f), 1.5f, 1e-4f);
}

TEST(MathHelperTest, CatmullRomMatchesXnaIntermediatePrecision)
{
    const float result = MathHelper::CatmullRom(
        1732.28723f, 1593.86609f, 1325.83545f, 1195.86243f, 0.01f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result), UINT32_C(0x44c6fa0d));
}

// --- Hermite ---

TEST(MathHelperTest, HermiteAtZeroReturnsValue1)
{
    EXPECT_NEAR(MathHelper::Hermite(1.0f, 0.0f, 5.0f, 0.0f, 0.0f), 1.0f, kEps);
}

TEST(MathHelperTest, HermiteAtOneReturnsValue2)
{
    EXPECT_NEAR(MathHelper::Hermite(1.0f, 0.0f, 5.0f, 0.0f, 1.0f), 5.0f, kEps);
}

TEST(MathHelperTest, HermiteAtHalfReturnsMidpointForZeroTangents)
{
    // Zero tangents → same as SmoothStep midpoint
    EXPECT_NEAR(MathHelper::Hermite(0.0f, 0.0f, 10.0f, 0.0f, 0.5f), 5.0f, kEps);
}

// --- Constants ---

TEST(MathHelperTest, EConstantApprox)
{
    EXPECT_NEAR(MathHelper::E, 2.71828175f, 1e-6f);
}

TEST(MathHelperTest, Log10EConstantApprox)
{
    EXPECT_NEAR(MathHelper::Log10E, 0.4342945f, 1e-6f);
}

TEST(MathHelperTest, Log2EConstantApprox)
{
    EXPECT_NEAR(MathHelper::Log2E, 1.442695f, 1e-5f);
}

TEST(MathHelperTest, MachineEpsilonFloatIsPositive)
{
    EXPECT_GT(MathHelper::MachineEpsilonFloat, 0.0f);
}

TEST(MathHelperTest, MachineEpsilonFloatIsSmall)
{
    EXPECT_LT(MathHelper::MachineEpsilonFloat, 1e-4f);
}
