// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
    constexpr int kDescriptionSize = 1021;
}

TEST(AvatarDescriptionTest, ConstructorRejectsWrongSize) {
    std::vector<SharpRuntime::bytecs> tooShort(kDescriptionSize - 1, 0);
    EXPECT_THROW((void)AvatarDescription(tooShort), System::ArgumentException);

    std::vector<SharpRuntime::bytecs> tooLong(kDescriptionSize + 1, 0);
    EXPECT_THROW((void)AvatarDescription(tooLong), System::ArgumentException);
}

TEST(AvatarDescriptionTest, ConstructorAcceptsCorrectSize) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    data[0] = 1;
    EXPECT_NO_THROW((void)AvatarDescription(data));
}

TEST(AvatarDescriptionTest, IsValidFalseWhenFirstByteIsZero) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    AvatarDescription description(data);
    EXPECT_FALSE(description.getIsValidProperty());
}

TEST(AvatarDescriptionTest, IsValidTrueWhenFirstByteIsNonzero) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    data[0] = 42;
    AvatarDescription description(data);
    EXPECT_TRUE(description.getIsValidProperty());
}

TEST(AvatarDescriptionTest, DescriptionReturnsDefensiveCopy) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    data[0] = 7;
    AvatarDescription description(data);

    std::vector<SharpRuntime::bytecs> copy = description.getDescriptionProperty();
    ASSERT_EQ(copy.size(), static_cast<size_t>(kDescriptionSize));
    EXPECT_EQ(copy[0], 7);

    copy[0] = 99;
    EXPECT_EQ(description.getDescriptionProperty()[0], 7);
}

TEST(AvatarDescriptionTest, HeightLazilyDefaultsToZero) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    AvatarDescription description(data);
    EXPECT_FLOAT_EQ(description.getHeightProperty(), 0.0f);
}

TEST(AvatarDescriptionTest, BodyTypeLazilyDefaultsToFemale) {
    std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
    AvatarDescription description(data);
    EXPECT_EQ(description.getBodyTypeProperty(), AvatarBodyType::Female);
}

TEST(AvatarDescriptionTest, CreateRandomReturnsInvalidDescription) {
    // Despite the name, the real XNA implementation never actually randomizes anything - it
    // always returns an all-zero (invalid) description. Preserved exactly, not "fixed."
    AvatarDescription description = AvatarDescription::CreateRandom();
    EXPECT_FALSE(description.getIsValidProperty());
    EXPECT_EQ(description.getDescriptionProperty().size(), static_cast<size_t>(kDescriptionSize));
}

TEST(AvatarDescriptionTest, CreateRandomWithBodyTypeReturnsInvalidDescription) {
    AvatarDescription female = AvatarDescription::CreateRandom(AvatarBodyType::Female);
    AvatarDescription male = AvatarDescription::CreateRandom(AvatarBodyType::Male);
    EXPECT_FALSE(female.getIsValidProperty());
    EXPECT_FALSE(male.getIsValidProperty());
}

TEST(AvatarDescriptionTest, CreateRandomWithInvalidBodyTypeThrows) {
    auto invalid = static_cast<AvatarBodyType>(2);
    EXPECT_THROW((void)AvatarDescription::CreateRandom(invalid), System::ArgumentOutOfRangeException);
}

TEST(AvatarDescriptionTest, BeginGetFromGamerRejectsNullGamer) {
    EXPECT_THROW(
        (void)AvatarDescription::BeginGetFromGamer(nullptr, System::AsyncCallback{}, std::any{}),
        System::ArgumentNullException
    );
}

TEST(AvatarDescriptionTest, BeginGetFromGamerInvokesCallbackSynchronously) {
    // A fully synchronous fake-async operation, matching the real XNA implementation exactly -
    // the callback fires before BeginGetFromGamer even returns.
    auto gamer = SignedInGamer::CreateInternal("AvatarTestGamer");

    bool callbackInvoked = false;
    System::IAsyncResult* capturedResult = nullptr;
    System::AsyncCallback callback = [&](System::IAsyncResult& result) {
        callbackInvoked = true;
        capturedResult = &result;
    };

    System::IAsyncResult* returnedResult =
        AvatarDescription::BeginGetFromGamer(&gamer, callback, std::any{});

    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(capturedResult, returnedResult);
    ASSERT_NE(returnedResult, nullptr);
    EXPECT_TRUE(returnedResult->getIsCompletedProperty());
    EXPECT_TRUE(returnedResult->getCompletedSynchronouslyProperty());

    delete returnedResult;
}

TEST(AvatarDescriptionTest, EndGetFromGamerReturnsInvalidDescription) {
    auto gamer = SignedInGamer::CreateInternal("AvatarTestGamer2");
    System::IAsyncResult* result =
        AvatarDescription::BeginGetFromGamer(&gamer, System::AsyncCallback{}, std::any{});

    AvatarDescription description = AvatarDescription::EndGetFromGamer(result);
    EXPECT_FALSE(description.getIsValidProperty());

    delete result;
}

TEST(AvatarDescriptionTest, EndGetFromGamerRejectsResultNotFromBegin) {
    // Any IAsyncResult not returned by BeginGetFromGamer must be rejected via ArgumentException.
    // NetworkSession's own EndCreate-mismatch tests use the same "bogus result" pattern.
    class BogusResult : public System::IAsyncResult {
    public:
        [[nodiscard]] bool getIsCompletedProperty() const override { return true; }
        [[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return true; }
        [[nodiscard]] const std::any& getAsyncStateProperty() const override { return state_; }
        [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override {
            throw std::runtime_error("not used");
        }
    private:
        std::any state_;
    };

    BogusResult bogus;
    EXPECT_THROW((void)AvatarDescription::EndGetFromGamer(&bogus), System::ArgumentException);
}

// NOTE: BeginGetFromGamer's "throws ObjectDisposedException if gamer.IsDisposed" path is not
// tested here - Gamer (the base class) has no publicly or CNAEXT-accessible way to become
// disposed anywhere in this codebase (isDisposed_ is a protected field never set by any
// existing Gamer/SignedInGamer/NetworkGamer code path). Not fixed as part of this port; see
// NEXT.md's known-limitations table.
