// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(NetworkSessionJoinExceptionTest, DefaultCtor) {
    NetworkSessionJoinException ex;
    EXPECT_NE(nullptr, dynamic_cast<Microsoft::Xna::Framework::GamerServices::NetworkException*>(&ex));
    EXPECT_EQ(NetworkSessionJoinError::SessionNotFound, ex.getJoinErrorProperty());
}

TEST(NetworkSessionJoinExceptionTest, MessageCtor) {
    NetworkSessionJoinException ex("join failed");
    EXPECT_STREQ("join failed", ex.what());
}

TEST(NetworkSessionJoinExceptionTest, MessageAndJoinErrorCtor) {
    NetworkSessionJoinException ex("session full", NetworkSessionJoinError::SessionFull);
    EXPECT_STREQ("session full", ex.what());
    EXPECT_EQ(NetworkSessionJoinError::SessionFull, ex.getJoinErrorProperty());
}

TEST(NetworkSessionJoinExceptionTest, MessageAndInnerCtor) {
    auto inner = std::make_exception_ptr(std::runtime_error("inner"));
    NetworkSessionJoinException ex("outer", inner);
    EXPECT_STREQ("outer", ex.what());
    EXPECT_NE(nullptr, ex.getInnerExceptionProperty());
}

TEST(NetworkSessionJoinExceptionTest, JoinErrorGetSet) {
    NetworkSessionJoinException ex;
    ex.setJoinErrorProperty(NetworkSessionJoinError::SessionNotJoinable);
    EXPECT_EQ(NetworkSessionJoinError::SessionNotJoinable, ex.getJoinErrorProperty());
}

TEST(NetworkSessionJoinExceptionTest, IsCatchableAsNetworkException) {
    try {
        throw NetworkSessionJoinException("test", NetworkSessionJoinError::SessionFull);
    } catch (const Microsoft::Xna::Framework::GamerServices::NetworkException& e) {
        EXPECT_STREQ("test", e.what());
    }
}

TEST(NetworkSessionJoinExceptionTest, IsCatchableAsSystemException) {
    try {
        throw NetworkSessionJoinException("test");
    } catch (const System::Exception& e) {
        EXPECT_STREQ("test", e.what());
    }
}
