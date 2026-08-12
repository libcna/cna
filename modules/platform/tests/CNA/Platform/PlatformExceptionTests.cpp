// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformException.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace {

using namespace CNA::Platform;

TEST(PlatformExceptionTests, MessageNamesTheOperationAndDetail)
{
    const PlatformException exception("CreateWindow", "no available video device");
    const std::string what = exception.what();
    EXPECT_NE(what.find("CreateWindow"), std::string::npos);
    EXPECT_NE(what.find("no available video device"), std::string::npos);
}

TEST(PlatformExceptionTests, AccessorsReturnTheConstructorArguments)
{
    const PlatformException exception("AcquireSubsystem", "SDL detail text");
    EXPECT_EQ(exception.GetOperation(), "AcquireSubsystem");
    EXPECT_EQ(exception.GetDetail(), "SDL detail text");
}

TEST(PlatformExceptionTests, DetailIsOptionalAndOmittedFromTheMessage)
{
    const PlatformException exception("PollEvents");
    EXPECT_TRUE(exception.GetDetail().empty());
    // No dangling separator when there is nothing to append.
    const std::string what = exception.what();
    EXPECT_EQ(what.back(), 'd');  // "... failed"
}

TEST(PlatformExceptionTests, IsCatchableAsStdRuntimeError)
{
    // The rest of CNA throws std::runtime_error; a caller that only knows about that must still
    // catch platform failures.
    try
    {
        throw PlatformException("CreateWindow", "detail");
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("CreateWindow"), std::string::npos);
        return;
    }
    FAIL() << "PlatformException was not catchable as std::runtime_error";
}

TEST(PlatformNotSupportedExceptionTests, NamesTheMissingCapabilityAndThePlatform)
{
    const PlatformNotSupportedException exception(PlatformCapability::VulkanSurface, "Terminal");
    const std::string what = exception.what();
    EXPECT_NE(what.find("VulkanSurface"), std::string::npos);
    EXPECT_NE(what.find("Terminal"), std::string::npos);
}

TEST(PlatformNotSupportedExceptionTests, ExposesTheCapabilityMachineReadably)
{
    // A test asserting that a capability-gated call refuses must be able to check WHICH
    // capability was missing, not just that something threw.
    const PlatformNotSupportedException exception(PlatformCapability::Gamepad, "Headless");
    EXPECT_EQ(exception.GetCapability(), PlatformCapability::Gamepad);
}

TEST(PlatformNotSupportedExceptionTests, MessageIsNotComposedTwice)
{
    // A refusal reads as a complete sentence already. Routing it through the operation-plus-
    // detail constructor produced "CNA::Platform: CNA::Platform: SDL3 does not support
    // VulkanSurface failed" -- found in a real test failure message, not by inspection.
    const PlatformNotSupportedException exception(PlatformCapability::VulkanSurface, "SDL3");
    const std::string what = exception.what();

    EXPECT_EQ(what.find("CNA::Platform:"), what.rfind("CNA::Platform:")) << what;
    EXPECT_EQ(what.find(" failed"), std::string::npos) << what;
}

TEST(PlatformNotSupportedExceptionTests, IsCatchableAsPlatformException)
{
    try
    {
        throw PlatformNotSupportedException(PlatformCapability::Tray, "Headless");
    }
    catch (const PlatformException& error)
    {
        EXPECT_NE(std::string(error.what()).find("Tray"), std::string::npos);
        return;
    }
    FAIL() << "PlatformNotSupportedException was not catchable as PlatformException";
}

} // namespace
