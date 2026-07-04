// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "System/Exception.hpp"

using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::SensorFailedException;

TEST(AccelerometerFailedExceptionTests, DefaultConstructorDoesNotThrow)
{
    EXPECT_NO_THROW({ const AccelerometerFailedException ex; (void)ex; });
}

TEST(AccelerometerFailedExceptionTests, CStringConstructorMessageMatches)
{
    const AccelerometerFailedException ex("Accelerometer sensor is unavailable.");
    EXPECT_STREQ(ex.what(), "Accelerometer sensor is unavailable.");
}

TEST(AccelerometerFailedExceptionTests, CanBeCaughtAsSensorFailedException)
{
    try
    {
        throw AccelerometerFailedException("Accelerometer failed.");
    }
    catch (const SensorFailedException& ex)
    {
        EXPECT_STREQ(ex.what(), "Accelerometer failed.");
        return;
    }
    FAIL() << "Expected AccelerometerFailedException to be caught as SensorFailedException";
}

TEST(AccelerometerFailedExceptionTests, CanBeCaughtAsSystemException)
{
    try
    {
        throw AccelerometerFailedException("Accelerometer failed.");
    }
    catch (const System::Exception& ex)
    {
        EXPECT_STREQ(ex.what(), "Accelerometer failed.");
        return;
    }
    FAIL() << "Expected AccelerometerFailedException to be caught as System::Exception";
}

TEST(AccelerometerFailedExceptionTests, DefaultConstructorErrorIdIsZero)
{
    const AccelerometerFailedException ex;
    EXPECT_EQ(ex.getErrorIdProperty(), 0);
}

TEST(AccelerometerFailedExceptionTests, CStringConstructorErrorIdIsZero)
{
    const AccelerometerFailedException ex("Accelerometer sensor is unavailable.");
    EXPECT_EQ(ex.getErrorIdProperty(), 0);
}

TEST(AccelerometerFailedExceptionTests, ErrorIdConstructorRoundTripsErrorIdAndMessage)
{
    const AccelerometerFailedException ex("Accelerometer failed.", 7);
    EXPECT_EQ(ex.getErrorIdProperty(), 7);
    EXPECT_STREQ(ex.what(), "Accelerometer failed.");
}

// Task P3-11: nothing in the implementation clamps/validates errorId, so a
// negative value is a real untested code path, not just a missing
// assertion.
TEST(AccelerometerFailedExceptionTests, ErrorIdConstructorRoundTripsNegativeErrorId)
{
    const AccelerometerFailedException ex("Accelerometer failed.", -7);
    EXPECT_EQ(ex.getErrorIdProperty(), -7);
    EXPECT_STREQ(ex.what(), "Accelerometer failed.");
}
