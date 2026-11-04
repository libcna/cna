// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <exception>
#include <stdexcept>
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"
using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;

TEST(StorageExceptionSecondPassTest, DefaultConstructorPreservesExternalExceptionDefaults)
{
    // XNA StorageDeviceNotConnectedException..ctor() IL_0000-0006 calls base().
    // FNA StorageDeviceNotConnectedException.cs:19-22 also delegates to base().
    const StorageDeviceNotConnectedException error;
    EXPECT_EQ(error.getMessageProperty(), "External component has thrown an exception.");
    EXPECT_EQ(error.getInnerExceptionProperty(), nullptr);
    EXPECT_EQ(static_cast<unsigned int>(error.getErrorCodeProperty()), 0x80004005U); // E_FAIL
}

TEST(StorageExceptionSecondPassTest, ExplicitMessageAndCauseArePreservedWithoutSubstitution)
{
    // XNA .ctor(string) IL_0000-0007 and .ctor(string,Exception) IL_0000-0008
    // forward their inputs unchanged to ExternalException.
    const StorageDeviceNotConnectedException empty("");
    EXPECT_EQ(empty.getMessageProperty(), "");
    EXPECT_EQ(empty.getInnerExceptionProperty(), nullptr);
    const auto cause = std::make_exception_ptr(std::runtime_error("removed"));
    const StorageDeviceNotConnectedException error("device lost", cause);
    EXPECT_EQ(error.getMessageProperty(), "device lost");
    EXPECT_EQ(error.getInnerExceptionProperty(), cause);
    EXPECT_EQ(static_cast<unsigned int>(error.getErrorCodeProperty()), 0x80004005U);
}
