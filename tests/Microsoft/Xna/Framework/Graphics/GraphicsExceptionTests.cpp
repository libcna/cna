// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceNotResetException.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

using Microsoft::Xna::Framework::Graphics::DeviceLostException;
using Microsoft::Xna::Framework::Graphics::DeviceNotResetException;
using Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException;

// =============================================================================
// DeviceLostException
// =============================================================================

TEST(DeviceLostExceptionTest, InheritsFromRuntimeError)
{
    EXPECT_NO_THROW({
        DeviceLostException e;
        const std::runtime_error& base = e;
        (void)base;
    });
}

TEST(DeviceLostExceptionTest, DefaultMessage)
{
    DeviceLostException e;
    EXPECT_NE(std::string(e.what()).find("lost"), std::string::npos);
}

TEST(DeviceLostExceptionTest, CustomMessage)
{
    DeviceLostException e("custom device lost");
    EXPECT_EQ(std::string(e.what()), "custom device lost");
}

TEST(DeviceLostExceptionTest, CatchableAsRuntimeError)
{
    EXPECT_THROW({
        throw DeviceLostException();
    }, std::runtime_error);
}

// =============================================================================
// DeviceNotResetException
// =============================================================================

TEST(DeviceNotResetExceptionTest, InheritsFromRuntimeError)
{
    EXPECT_NO_THROW({
        DeviceNotResetException e;
        const std::runtime_error& base = e;
        (void)base;
    });
}

TEST(DeviceNotResetExceptionTest, DefaultMessage)
{
    DeviceNotResetException e;
    EXPECT_NE(std::string(e.what()).find("reset"), std::string::npos);
}

TEST(DeviceNotResetExceptionTest, CustomMessage)
{
    DeviceNotResetException e("device not reset yet");
    EXPECT_EQ(std::string(e.what()), "device not reset yet");
}

TEST(DeviceNotResetExceptionTest, CatchableAsRuntimeError)
{
    EXPECT_THROW({
        throw DeviceNotResetException();
    }, std::runtime_error);
}

// =============================================================================
// NoSuitableGraphicsDeviceException
// =============================================================================

TEST(NoSuitableGraphicsDeviceExceptionTest, InheritsFromRuntimeError)
{
    EXPECT_NO_THROW({
        NoSuitableGraphicsDeviceException e;
        const std::runtime_error& base = e;
        (void)base;
    });
}

TEST(NoSuitableGraphicsDeviceExceptionTest, DefaultMessage)
{
    NoSuitableGraphicsDeviceException e;
    EXPECT_FALSE(std::string(e.what()).empty());
}

TEST(NoSuitableGraphicsDeviceExceptionTest, CustomMessage)
{
    NoSuitableGraphicsDeviceException e("no GPU found");
    EXPECT_EQ(std::string(e.what()), "no GPU found");
}

TEST(NoSuitableGraphicsDeviceExceptionTest, CatchableAsRuntimeError)
{
    EXPECT_THROW({
        throw NoSuitableGraphicsDeviceException();
    }, std::runtime_error);
}
