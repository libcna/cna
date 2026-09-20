// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// XNA-MISSING-014: GetObjectData and the joinError round trip.
//
// XNA writes the base state and then adds the join error under the name `joinError`, and its
// serialization constructor reads that same name back
// (xna4-decomp/.../Microsoft.Xna.Framework.Net/NetworkSessionJoinException.cs). CNA's constructor
// previously discarded the info entirely, so a round trip silently reported SessionNotFound
// whatever had been serialized -- which is why these cases assert the round trip rather than only
// that the constructor is reachable.
// ---------------------------------------------------------------------------

TEST(NetworkSessionJoinExceptionTest, GetObjectDataAndTheSerializationConstructorRoundTripEveryField) {
    const NetworkSessionJoinException original("the session was full",
                                               NetworkSessionJoinError::SessionFull);

    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext writeContext;
    original.GetObjectData(info, writeContext);

    System::Runtime::Serialization::StreamingContext readContext;
    const TestableNetworkSessionJoinException restored(info, readContext);

    EXPECT_EQ(restored.getJoinErrorProperty(), NetworkSessionJoinError::SessionFull);
    EXPECT_EQ(restored.getMessageProperty(), "the session was full");
}

TEST(NetworkSessionJoinExceptionTest, EveryJoinErrorValueSurvivesTheRoundTrip) {
    // The value is written as an Int32 and read back as one, so no value may be lost or aliased.
    for (const auto error : {NetworkSessionJoinError::SessionNotFound,
                             NetworkSessionJoinError::SessionNotJoinable,
                             NetworkSessionJoinError::SessionFull}) {
        const NetworkSessionJoinException original("failed", error);
        System::Runtime::Serialization::SerializationInfo info;
        const System::Runtime::Serialization::StreamingContext writeContext;
        original.GetObjectData(info, writeContext);

        System::Runtime::Serialization::StreamingContext readContext;
        const TestableNetworkSessionJoinException restored(info, readContext);
        EXPECT_EQ(restored.getJoinErrorProperty(), error);
    }
}

TEST(NetworkSessionJoinExceptionTest, GetObjectDataWritesTheBaseStateBeforeTheJoinError) {
    // XNA calls base.GetObjectData first. Order is observable here because SerializationInfo
    // preserves it, and a derived type that wrote its own state first would not match.
    const NetworkSessionJoinException original("ordered", NetworkSessionJoinError::SessionFull);
    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    original.GetObjectData(info, context);

    const std::vector<std::string> names = info.GetNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Message");
    EXPECT_EQ(names[1], "InnerException");
    EXPECT_EQ(names[2], "joinError");
}

TEST(NetworkSessionJoinExceptionTest, TheInnerCauseTravelsWithTheException) {
    std::exception_ptr cause;
    try {
        throw std::runtime_error("the socket closed");
    } catch (...) {
        cause = std::current_exception();
    }
    const NetworkSessionJoinException original("join failed", cause);

    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext writeContext;
    original.GetObjectData(info, writeContext);

    System::Runtime::Serialization::StreamingContext readContext;
    const TestableNetworkSessionJoinException restored(info, readContext);
    ASSERT_NE(restored.getInnerExceptionProperty(), nullptr);
    EXPECT_THROW(std::rethrow_exception(restored.getInnerExceptionProperty()), std::runtime_error);
}

TEST(NetworkSessionJoinExceptionTest, AnEmptyStoreStillConstructs) {
    // A store written by something that recorded nothing yields the empty values rather than
    // throwing, which is what .NET's own serialization constructor does with a partial store.
    System::Runtime::Serialization::SerializationInfo info;
    System::Runtime::Serialization::StreamingContext context;
    const TestableNetworkSessionJoinException restored(info, context);
    EXPECT_EQ(restored.getJoinErrorProperty(), NetworkSessionJoinError::SessionNotFound);
    EXPECT_TRUE(restored.getMessageProperty().empty());
    EXPECT_EQ(restored.getInnerExceptionProperty(), nullptr);
}

TEST(NetworkSessionJoinExceptionTest, GetObjectDataIsReachedThroughTheBaseException) {
    // XNA declares it as an override, so a caller holding the base type must reach the derived
    // implementation -- which is what carries the join error.
    const NetworkSessionJoinException original("virtual", NetworkSessionJoinError::SessionNotJoinable);
    const Microsoft::Xna::Framework::GamerServices::NetworkException& asBase = original;

    System::Runtime::Serialization::SerializationInfo info;
    const System::Runtime::Serialization::StreamingContext context;
    asBase.GetObjectData(info, context);

    EXPECT_TRUE(info.Contains("joinError"))
        << "the derived override must run, not the base implementation alone";
    System::Runtime::Serialization::StreamingContext readContext;
    const TestableNetworkSessionJoinException restored(info, readContext);
    EXPECT_EQ(restored.getJoinErrorProperty(), NetworkSessionJoinError::SessionNotJoinable);
}
