// SPDX-License-Identifier: MS-PL
// ISSUE-089: native registration counterpart of measured XNA publication/cycle edges.
// Run each mode in a fresh process; a throwing initializer must not contaminate others.
#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/InvalidOperationException.hpp"

namespace I = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::ContentSerializerAttribute;
namespace
{
    int checks = 0, failures = 0;
    void Check(bool condition, const char* label)
    {
        ++checks;
        if (!condition) ++failures;
        std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
    }
    struct Self { static constexpr std::string_view XnaTypeName = "Repair.InitSelf"; };
    struct Left { static constexpr std::string_view XnaTypeName = "Repair.InitLeft"; };
    struct Right { static constexpr std::string_view XnaTypeName = "Repair.InitRight"; };
    struct Failing { static constexpr std::string_view XnaTypeName = "Repair.InitFailing"; };

    template<typename T, typename Nested = T>
    class EdgeSerializer final : public I::ContentTypeSerializer<T>
    {
    public:
        int initializations = 0, writes = 0;
        I::IntermediateSerializer* owner = nullptr;
        I::ContentTypeSerializerBase* nested = nullptr;
    protected:
        void Initialize(I::IntermediateSerializer& serializer) override
        {
            ++initializations;
            owner = &serializer;
            nested = &serializer.GetTypeSerializer(System::Type::From<Nested>());
            if constexpr (std::is_same_v<T, Failing>)
                throw System::InvalidOperationException("initializer failure");
        }
        void Serialize(I::IntermediateWriter&, const T&, const ContentSerializerAttribute&) override { ++writes; }
        T Deserialize(I::IntermediateReader&, const ContentSerializerAttribute&, T) override { return {}; }
    };

    template<typename T, typename Nested = T>
    EdgeSerializer<T, Nested>& Register()
    {
        auto serializer = std::make_unique<EdgeSerializer<T, Nested>>();
        auto* result = serializer.get();
        (void)I::IntermediateSerializer::RegisterTypeSerializer(std::move(serializer));
        return *result;
    }

    void SerializeInt()
    {
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
        I::IntermediateSerializer::Serialize<int>(*writer, 42, "");
    }

    void Cycles()
    {
        auto& self = Register<Self>();
        auto& left = Register<Left, Right>();
        auto& right = Register<Right, Left>();
        // No custom payload is written: measured XNA initializes discovered serializers eagerly.
        SerializeInt();
        Check(self.initializations == 1, "unused self initializer runs once before primitive operation completes");
        Check(left.initializations == 1, "unused left initializer runs once");
        Check(right.initializations == 1, "unused right initializer runs once");
        Check(self.nested == &self, "self lookup returns published initializing instance");
        Check(left.nested == &right, "left lookup finds published right instance");
        Check(right.nested == &left, "right lookup closes the published cycle");
        Check(self.owner && self.owner == left.owner && self.owner == right.owner, "all initializers share one owner");
        Check(self.owner && &self.owner->GetTypeSerializer(System::Type::From<Self>()) == &self,
              "retained owner preserves self cache after operation");
        Check(left.owner && &left.owner->GetTypeSerializer(System::Type::From<Right>()) == &right,
              "retained owner preserves cross cache after operation");
        SerializeInt();
        Check(self.initializations == 1 && left.initializations == 1 && right.initializations == 1,
              "second operation does not repeat initialization");
        Check(self.writes == 0 && left.writes == 0 && right.writes == 0, "unused serializer payload callbacks never run");
    }

    void Failure()
    {
        auto& failing = Register<Failing>();
        bool exactException = false, operationReturned = false;
        try { SerializeInt(); operationReturned = true; }
        catch (const System::InvalidOperationException& error)
        {
            exactException = typeid(error) == typeid(System::InvalidOperationException) &&
                             error.getMessageProperty() == "initializer failure";
        }
        Check(exactException, "eager initialization propagates original exception type and message");
        Check(!operationReturned, "failed initialization prevents a successful serialization result");
        Check(failing.initializations == 1, "failed owner invokes initializer once");
        Check(failing.owner && &failing.owner->GetTypeSerializer(System::Type::From<Failing>()) == &failing,
              "captured failed owner retains published serializer");
        Check(failing.initializations == 1 && failing.nested == &failing,
              "captured owner lookup does not retry failed initialization");
        Check(failing.writes == 0, "failed initializer does not invoke payload writer");
        // Deliberately no fresh-singleton assertion: explicit unique_ptr registration has no
        // factory with which to reconstruct a new instance. That CNAEXT boundary needs a
        // documented decision, not a fake retry of the same object or invented cloning.
    }
}

TEST(IntermediateInitializationEdgesTest, PublishedSelfAndMutualDependenciesInitializeOnce)
{
    checks = failures = 0;
    ASSERT_NO_THROW(Cycles());
    EXPECT_EQ(checks, 11);
    EXPECT_EQ(failures, 0);
}

TEST(IntermediateInitializationEdgesTest, FailedInitializerKeepsItsPublishedOwnerWithoutRetry)
{
#if GTEST_HAS_DEATH_TEST
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(
        {
            checks = failures = 0;
            Failure();
            std::exit(checks == 6 && failures == 0 ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
#else
    GTEST_SKIP() << "Fresh-process initialization failure test requires death-test support";
#endif
}
