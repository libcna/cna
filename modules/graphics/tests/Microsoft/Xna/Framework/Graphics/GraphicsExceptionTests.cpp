// SPDX-License-Identifier: MS-PL
//
// XNA derives DeviceLostException, DeviceNotResetException and NoSuitableGraphicsDeviceException
// from System.Exception and gives each one the `(String, Exception)` inner-cause constructor. CNA
// derived them from std::runtime_error until XNA-MISSING-007, which has no inner-cause field and is
// not the base every other XNA exception in CNA uses (GamerServices, Devices, Content Pipeline all
// use System::Exception). They now derive from System::Exception, which is both the XNA hierarchy
// and that convention, and carries the inner cause as a std::exception_ptr.

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceNotResetException.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"
#include "System/Exception.hpp"

using Microsoft::Xna::Framework::Graphics::DeviceLostException;
using Microsoft::Xna::Framework::Graphics::DeviceNotResetException;
using Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException;

namespace
{
    /// Everything the three share: the base, the message routes, and the inner cause.
    template <typename TException>
    void ExpectGraphicsExceptionContract(const std::string& defaultMessageFragment)
    {
        // System::Exception is the base, and it is still a std::exception, so a catch-all handler
        // written against the standard hierarchy keeps working.
        {
            TException value;
            const System::Exception& asXnaException = value;
            const std::exception& asStandard = value;
            EXPECT_FALSE(asXnaException.getMessageProperty().empty());
            EXPECT_NE(asStandard.what(), nullptr);
        }
        EXPECT_THROW({ throw TException(); }, System::Exception);
        EXPECT_THROW({ throw TException(); }, std::exception);

        // The default message, through both the .NET accessor and what().
        {
            const TException value;
            EXPECT_NE(value.getMessageProperty().find(defaultMessageFragment), std::string::npos);
            EXPECT_EQ(std::string(value.what()), value.getMessageProperty());
            EXPECT_EQ(value.getInnerExceptionProperty(), nullptr);
        }

        // A custom message is stored verbatim and still has no inner cause.
        {
            const TException value("a specific explanation");
            EXPECT_EQ(value.getMessageProperty(), "a specific explanation");
            EXPECT_EQ(std::string(value.what()), "a specific explanation");
            EXPECT_EQ(value.getInnerExceptionProperty(), nullptr);
        }

        // The documented (String, Exception) constructor keeps both the message and the cause, and
        // the cause rethrows as what it was.
        {
            std::exception_ptr cause;
            try
            {
                throw std::runtime_error("the underlying failure");
            }
            catch (...)
            {
                cause = std::current_exception();
            }
            ASSERT_NE(cause, nullptr);

            const TException value("the reported failure", cause);
            EXPECT_EQ(value.getMessageProperty(), "the reported failure");
            ASSERT_NE(value.getInnerExceptionProperty(), nullptr);
            EXPECT_THROW(std::rethrow_exception(value.getInnerExceptionProperty()),
                         std::runtime_error);
            try
            {
                std::rethrow_exception(value.getInnerExceptionProperty());
                FAIL() << "the inner cause must rethrow";
            }
            catch (const std::runtime_error& inner)
            {
                EXPECT_EQ(std::string(inner.what()), "the underlying failure");
            }
        }

        // An empty cause is the C++ counterpart of XNA's null innerException argument.
        {
            const TException value("no cause given", std::exception_ptr{});
            EXPECT_EQ(value.getMessageProperty(), "no cause given");
            EXPECT_EQ(value.getInnerExceptionProperty(), nullptr);
        }

        // The cause may itself be one of these exceptions, which is how a device failure chains.
        {
            std::exception_ptr cause;
            try
            {
                throw DeviceLostException("the device went away");
            }
            catch (...)
            {
                cause = std::current_exception();
            }
            const TException value("reset failed", cause);
            ASSERT_NE(value.getInnerExceptionProperty(), nullptr);
            EXPECT_THROW(std::rethrow_exception(value.getInnerExceptionProperty()),
                         DeviceLostException);
        }
    }
}

TEST(GraphicsExceptionTest, DeviceLostException)
{
    ExpectGraphicsExceptionContract<DeviceLostException>("lost");
}

TEST(GraphicsExceptionTest, DeviceNotResetException)
{
    ExpectGraphicsExceptionContract<DeviceNotResetException>("reset");
}

TEST(GraphicsExceptionTest, NoSuitableGraphicsDeviceException)
{
    ExpectGraphicsExceptionContract<NoSuitableGraphicsDeviceException>("device");
}

TEST(GraphicsExceptionTest, EachTypeIsCaughtSeparatelyFromItsSiblings)
{
    // The three are distinct types, so a handler for one must not swallow another -- which is what
    // makes the shared base safe.
    EXPECT_THROW({
        try
        {
            throw DeviceNotResetException();
        }
        catch (const DeviceLostException&)
        {
            FAIL() << "DeviceNotResetException must not be caught as DeviceLostException";
        }
    }, DeviceNotResetException);

    EXPECT_THROW({
        try
        {
            throw NoSuitableGraphicsDeviceException();
        }
        catch (const DeviceNotResetException&)
        {
            FAIL() << "NoSuitableGraphicsDeviceException must not be caught as DeviceNotResetException";
        }
    }, NoSuitableGraphicsDeviceException);
}
