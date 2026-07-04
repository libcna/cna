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

TEST(NetworkExceptionTest, DefaultCtor) {
    NetworkException ex;
    EXPECT_NE(nullptr, dynamic_cast<System::Exception*>(&ex));
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
