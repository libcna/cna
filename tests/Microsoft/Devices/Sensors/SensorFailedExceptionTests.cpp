// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "System/Exception.hpp"

using Microsoft::Devices::Sensors::SensorFailedException;

TEST(SensorFailedExceptionTests, DefaultConstructorMessageIsNonEmpty)
{
    const SensorFailedException ex;
    EXPECT_STRNE(ex.what(), "");
}

TEST(SensorFailedExceptionTests, CStringConstructorMessageMatches)
{
    const SensorFailedException ex("Accelerometer hardware unavailable.");
    EXPECT_STREQ(ex.what(), "Accelerometer hardware unavailable.");
}

TEST(SensorFailedExceptionTests, CanBeCaughtAsSystemException)
{
    try
    {
        throw SensorFailedException("Sensor failed.");
    }
    catch (const System::Exception& ex)
    {
        EXPECT_STREQ(ex.what(), "Sensor failed.");
        return;
    }
    FAIL() << "Expected SensorFailedException to be caught as System::Exception";
}

TEST(SensorFailedExceptionTests, DefaultConstructorErrorIdIsZero)
{
    const SensorFailedException ex;
    EXPECT_EQ(ex.getErrorIdProperty(), 0);
}

TEST(SensorFailedExceptionTests, CStringConstructorErrorIdIsZero)
{
    const SensorFailedException ex("Accelerometer hardware unavailable.");
    EXPECT_EQ(ex.getErrorIdProperty(), 0);
}

TEST(SensorFailedExceptionTests, ErrorIdConstructorRoundTripsErrorIdAndMessage)
{
    const SensorFailedException ex("Sensor failed.", 42);
    EXPECT_EQ(ex.getErrorIdProperty(), 42);
    EXPECT_STREQ(ex.what(), "Sensor failed.");
}
