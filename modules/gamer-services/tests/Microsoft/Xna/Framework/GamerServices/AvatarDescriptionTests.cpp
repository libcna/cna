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

// ---------------------------------------------------------------------------
// XNA-MISSING-018: AvatarDescription.Changed is an instance event.
//
// Microsoft declares `public event EventHandler<EventArgs> Changed;` and raises it on the cached
// description of the player whose avatar changed
// (xna4-decomp/.../Microsoft.Xna.Framework.GamerServices/AvatarDescription.cs, OnAvatarChanged).
// CNA previously had it as a static member, so subscribers of any one description heard every
// notification. Nothing in this runtime raises the event; what is asserted here is the ownership
// and delivery contract.
// ---------------------------------------------------------------------------

namespace {
    AvatarDescription MakeDescription(SharpRuntime::bytecs firstByte) {
        std::vector<SharpRuntime::bytecs> data(kDescriptionSize, 0);
        data[0] = firstByte;
        return AvatarDescription(data);
    }

    /// Stands in for the sender XNA passes (the signed-in gamer whose avatar changed).
    /// SignedInGamer is not constructible from a test and does not derive from System::Object,
    /// which the event signature requires, so the delivery contract is exercised with a plain one.
    class ChangeSender final : public System::Object {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override {
            static const std::string typeName = "CNA.Tests.AvatarChangeSender";
            return typeName;
        }
    };
}

TEST(AvatarDescriptionTest, ChangedIsPerInstanceNotShared) {
    AvatarDescription first = MakeDescription(1);
    AvatarDescription second = MakeDescription(2);

    int firstCalls = 0;
    first.Changed += [&firstCalls](System::Object*, const System::EventArgs&) { ++firstCalls; };

    // Raising the second description's event must not reach the first description's subscriber.
    second.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(firstCalls, 0);

    first.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(firstCalls, 1);
}

TEST(AvatarDescriptionTest, ChangedDeliversTheSenderAndArguments) {
    AvatarDescription description = MakeDescription(1);
    System::Object* observed = nullptr;
    const System::EventArgs* observedArgs = nullptr;
    int calls = 0;

    description.Changed += [&](System::Object* sender, const System::EventArgs& args) {
        observed = sender;
        observedArgs = &args;
        ++calls;
    };

    ChangeSender sender;
    description.Changed.Raise(&sender, System::EventArgs::Empty);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(observed, &sender);
    EXPECT_EQ(observedArgs, &System::EventArgs::Empty);
}

TEST(AvatarDescriptionTest, ChangedSubscribersRunInSubscriptionOrder) {
    AvatarDescription description = MakeDescription(1);
    std::vector<int> order;

    description.Changed += [&order](System::Object*, const System::EventArgs&) { order.push_back(1); };
    description.Changed += [&order](System::Object*, const System::EventArgs&) { order.push_back(2); };
    description.Changed += [&order](System::Object*, const System::EventArgs&) { order.push_back(3); };

    description.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(AvatarDescriptionTest, ChangedSubscriptionCanBeRemoved) {
    AvatarDescription description = MakeDescription(1);
    int calls = 0;

    const auto token = description.Changed.Add(
        [&calls](System::Object*, const System::EventArgs&) { ++calls; });
    description.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(calls, 1);

    description.Changed.Remove(token);
    description.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(calls, 1);

    // Removing the same token twice is a no-op rather than a failure.
    description.Changed.Remove(token);
    description.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(calls, 1);
}

TEST(AvatarDescriptionTest, ANewDescriptionHasNoSubscribers) {
    AvatarDescription subscribed = MakeDescription(1);
    int calls = 0;
    subscribed.Changed += [&calls](System::Object*, const System::EventArgs&) { ++calls; };

    // A description built afterwards starts empty; with a static event it would not have.
    AvatarDescription fresh = MakeDescription(1);
    fresh.Changed.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(calls, 0);
}
