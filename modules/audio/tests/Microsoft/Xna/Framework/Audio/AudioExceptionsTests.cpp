// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/InstancePlayLimitException.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp"

#include "System/Exception.hpp"
#include "System/SystemException.hpp"
#include "System/Runtime/InteropServices/ExternalException.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>

using Microsoft::Xna::Framework::Audio::InstancePlayLimitException;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::NoMicrophoneConnectedException;
using System::Exception;
using System::SystemException;
using System::Runtime::InteropServices::ExternalException;

// ---------------------------------------------------------------------------
// Compile-time hierarchy locks — these mirror the FNA base classes exactly:
//   InstancePlayLimitException : ExternalException
//   NoAudioHardwareException   : ExternalException
//   NoMicrophoneConnectedException : Exception (NOT SystemException)
// ---------------------------------------------------------------------------
static_assert(std::is_base_of_v<ExternalException, InstancePlayLimitException>);
static_assert(std::is_base_of_v<SystemException, InstancePlayLimitException>);
static_assert(std::is_base_of_v<Exception, InstancePlayLimitException>);

static_assert(std::is_base_of_v<ExternalException, NoAudioHardwareException>);
static_assert(std::is_base_of_v<SystemException, NoAudioHardwareException>);
static_assert(std::is_base_of_v<Exception, NoAudioHardwareException>);

static_assert(std::is_base_of_v<Exception, NoMicrophoneConnectedException>);
static_assert(!std::is_base_of_v<SystemException, NoMicrophoneConnectedException>);
static_assert(!std::is_base_of_v<ExternalException, NoMicrophoneConnectedException>);

namespace
{
    // Rethrows an inner exception_ptr and returns its what() text (empty if null).
    std::string innerWhat(std::exception_ptr p)
    {
        if (!p)
        {
            return {};
        }
        try
        {
            std::rethrow_exception(p);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }
}

// ===================== InstancePlayLimitException =====================

TEST(InstancePlayLimitExceptionTest, DefaultCtorHasExternalComponentMessage)
{
    InstancePlayLimitException e;
    EXPECT_FALSE(std::string(e.what()).empty());          // ExternalException default message
    EXPECT_EQ(e.getInnerExceptionProperty(), nullptr);
}

TEST(InstancePlayLimitExceptionTest, MessageCtorStoresMessage)
{
    InstancePlayLimitException e("limit reached");
    EXPECT_EQ(e.getMessageProperty(), "limit reached");
    EXPECT_NE(std::string(e.what()).find("limit reached"), std::string::npos);
}

TEST(InstancePlayLimitExceptionTest, MessageAndInnerCtorRoundTrips)
{
    auto inner = std::make_exception_ptr(std::runtime_error("cause"));
    InstancePlayLimitException e("outer", inner);
    EXPECT_EQ(e.getMessageProperty(), "outer");
    EXPECT_NE(e.getInnerExceptionProperty(), nullptr);
    EXPECT_EQ(innerWhat(e.getInnerExceptionProperty()), "cause");
}

TEST(InstancePlayLimitExceptionTest, IsCatchableAsBases)
{
    try
    {
        throw InstancePlayLimitException("boom");
    }
    catch (const ExternalException& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "boom");
        return;
    }
    catch (...)
    {
        FAIL() << "should have been caught as ExternalException";
    }
    FAIL() << "nothing thrown";
}

// ===================== NoAudioHardwareException =====================

TEST(NoAudioHardwareExceptionTest, DefaultCtorHasExternalComponentMessage)
{
    NoAudioHardwareException e;
    EXPECT_FALSE(std::string(e.what()).empty());
    EXPECT_EQ(e.getInnerExceptionProperty(), nullptr);
}

TEST(NoAudioHardwareExceptionTest, MessageCtorStoresMessage)
{
    NoAudioHardwareException e("no device");
    EXPECT_EQ(e.getMessageProperty(), "no device");
}

TEST(NoAudioHardwareExceptionTest, MessageAndInnerCtorRoundTrips)
{
    auto inner = std::make_exception_ptr(std::runtime_error("sdl failed"));
    NoAudioHardwareException e("init failed", inner);
    EXPECT_EQ(e.getMessageProperty(), "init failed");
    EXPECT_EQ(innerWhat(e.getInnerExceptionProperty()), "sdl failed");
}

TEST(NoAudioHardwareExceptionTest, IsCatchableAsSystemException)
{
    try
    {
        throw NoAudioHardwareException("boom");
    }
    catch (const SystemException& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "boom");
        return;
    }
    catch (...)
    {
        FAIL() << "should have been caught as SystemException";
    }
    FAIL() << "nothing thrown";
}

// ===================== NoMicrophoneConnectedException =====================

TEST(NoMicrophoneConnectedExceptionTest, DefaultCtorNamesTheRuntimeType)
{
    NoMicrophoneConnectedException e;
    EXPECT_EQ(
        e.getMessageProperty(),
        "Exception of type 'Microsoft.Xna.Framework.Audio.NoMicrophoneConnectedException' was thrown."
    );
    EXPECT_EQ(e.getInnerExceptionProperty(), nullptr);
}

TEST(NoMicrophoneConnectedExceptionTest, MessageCtorStoresMessage)
{
    NoMicrophoneConnectedException e("unplugged");
    EXPECT_EQ(e.getMessageProperty(), "unplugged");
    EXPECT_NE(std::string(e.what()).find("unplugged"), std::string::npos);
}

TEST(NoMicrophoneConnectedExceptionTest, MessageAndInnerCtorRoundTrips)
{
    auto inner = std::make_exception_ptr(std::runtime_error("disconnected"));
    NoMicrophoneConnectedException e("mic error", inner);
    EXPECT_EQ(e.getMessageProperty(), "mic error");
    EXPECT_EQ(innerWhat(e.getInnerExceptionProperty()), "disconnected");
}

TEST(NoMicrophoneConnectedExceptionTest, IsCatchableAsException)
{
    try
    {
        throw NoMicrophoneConnectedException("boom");
    }
    catch (const Exception& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "boom");
        return;
    }
    catch (...)
    {
        FAIL() << "should have been caught as Exception";
    }
    FAIL() << "nothing thrown";
}
