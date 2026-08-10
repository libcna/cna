// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"

using namespace Microsoft::Xna::Framework::Net;

namespace {
    // Task 5.5: the (SerializationInfo&, StreamingContext&) constructor is protected, matching
    // .NET's ISerializable pattern where only a deserializing subclass ever calls it directly -
    // a small test-only subclass is the standard way to exercise a protected constructor.
    struct TestableNetworkSessionJoinException : NetworkSessionJoinException {
        TestableNetworkSessionJoinException(
            System::Runtime::Serialization::SerializationInfo& info,
            System::Runtime::Serialization::StreamingContext& context
        )
            : NetworkSessionJoinException(info, context)
        {
        }
    };
}

TEST(NetworkSessionJoinExceptionTest, SerializationConstructorIsCallableByDerivedTypesAndDefaultInitializes) {
    System::Runtime::Serialization::SerializationInfo info;
    System::Runtime::Serialization::StreamingContext context;
    TestableNetworkSessionJoinException ex(info, context);
    EXPECT_NE(nullptr, dynamic_cast<Microsoft::Xna::Framework::GamerServices::NetworkException*>(&ex));
    EXPECT_EQ(NetworkSessionJoinError::SessionNotFound, ex.getJoinErrorProperty());
}

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
