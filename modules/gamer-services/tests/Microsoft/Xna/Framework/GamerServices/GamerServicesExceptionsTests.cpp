// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

// --- NetworkException ---

// Task 9.10: confirmed against FNA's own real default constructor (`: base()`, no hardcoded
// message in any of the 6 GamerServices exception types) that the default ctor's message is
// simply whatever System::Exception()'s own default produces.
//
// UPDATED for sharp-runtime #2323 (downstream ticket #2377, 2026-08-19): that default STOPPED
// being an empty string. It is now .NET's documented fallback, "Exception of type '{0}' was
// thrown." (Exception.cs:61, Strings.resx:2333). Inheriting it would make every type here report
// 'System.Exception' -- or, for NetworkNotAvailableException, its BASE -- so each supplies its own
// message naming itself, which is the repair sharp-runtime applied to HttpRequestException and
// JsonException. Asserting the exact text still guards against a regression silently blanking or
// changing the default with nothing else to catch it.
TEST(NetworkExceptionTest, DefaultCtor) {
    NetworkException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.NetworkException' was thrown.", ex.what());
}

TEST(NetworkExceptionTest, MessageCtor) {
    NetworkException ex("network error");
    EXPECT_STREQ("network error", ex.what());
}

TEST(NetworkExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    NetworkException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

TEST(NetworkExceptionTest, IsCatchableAsSystemException) {
    try {
        throw NetworkException("test");
    } catch (const System::Exception& e) {
        EXPECT_STREQ("test", e.what());
    }
}

// --- NetworkNotAvailableException ---

TEST(NetworkNotAvailableExceptionTest, DefaultCtor) {
    NetworkNotAvailableException ex;
    EXPECT_NE(nullptr, dynamic_cast<NetworkException*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.NetworkNotAvailableException' was thrown.", ex.what());
}

TEST(NetworkNotAvailableExceptionTest, MessageCtor) {
    NetworkNotAvailableException ex("not available");
    EXPECT_STREQ("not available", ex.what());
}

TEST(NetworkNotAvailableExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    NetworkNotAvailableException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

TEST(NetworkNotAvailableExceptionTest, IsCatchableAsNetworkException) {
    try {
        throw NetworkNotAvailableException("na");
    } catch (const NetworkException& e) {
        EXPECT_STREQ("na", e.what());
    }
}

// --- GamerPrivilegeException ---

TEST(GamerPrivilegeExceptionTest, DefaultCtor) {
    GamerPrivilegeException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.GamerPrivilegeException' was thrown.", ex.what());
}

TEST(GamerPrivilegeExceptionTest, MessageCtor) {
    GamerPrivilegeException ex("no privilege");
    EXPECT_STREQ("no privilege", ex.what());
}

TEST(GamerPrivilegeExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    GamerPrivilegeException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

// --- GamerServicesNotAvailableException ---

TEST(GamerServicesNotAvailableExceptionTest, DefaultCtor) {
    GamerServicesNotAvailableException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.GamerServicesNotAvailableException' was thrown.", ex.what());
}

TEST(GamerServicesNotAvailableExceptionTest, MessageCtor) {
    GamerServicesNotAvailableException ex("not supported");
    EXPECT_STREQ("not supported", ex.what());
}

TEST(GamerServicesNotAvailableExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    GamerServicesNotAvailableException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

// --- GameUpdateRequiredException ---

TEST(GameUpdateRequiredExceptionTest, DefaultCtor) {
    GameUpdateRequiredException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.GameUpdateRequiredException' was thrown.", ex.what());
}

TEST(GameUpdateRequiredExceptionTest, MessageCtor) {
    GameUpdateRequiredException ex("update required");
    EXPECT_STREQ("update required", ex.what());
}

TEST(GameUpdateRequiredExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    GameUpdateRequiredException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

// --- GuideAlreadyVisibleException ---

TEST(GuideAlreadyVisibleExceptionTest, DefaultCtor) {
    GuideAlreadyVisibleException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
    // INVERTED by sharp-runtime #2323 (downstream ticket #2377): System::Exception()
    // no longer produces an empty message -- it produces .NET's fallback,
    // "Exception of type '{0}' was thrown.". Each type here now supplies its OWN,
    // naming ITSELF, because inheriting the base's would name the WRONG type.
    EXPECT_STREQ("Exception of type 'Microsoft.Xna.Framework.GamerServices.GuideAlreadyVisibleException' was thrown.", ex.what());
}

TEST(GuideAlreadyVisibleExceptionTest, MessageCtor) {
    GuideAlreadyVisibleException ex("guide visible");
    EXPECT_STREQ("guide visible", ex.what());
}

TEST(GuideAlreadyVisibleExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    GuideAlreadyVisibleException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}
