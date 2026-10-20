// SPDX-License-Identifier: MS-PL
// ISSUE-089: actual-library counterpart of the genuine21 initialization controls.
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::ContentSerializerAttribute;
namespace
{
    int checks = 0, failures = 0;
    void Check(bool condition, const char* label)
    {
        ++checks;
        if (!condition) ++failures;
        EXPECT_TRUE(condition) << label;
    }
    struct InitializedPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.InitializedPayload";
        std::string text = "retained";
    };
    class InitializedPayloadSerializer final : public Intermediate::ContentTypeSerializer<InitializedPayload>
    {
    public:
        static inline int instances = 0;
        int calls = 0, scans = 0, writes = 0, reads = 0;
        Intermediate::IntermediateSerializer* owner = nullptr;
        Intermediate::ContentTypeSerializerBase* nested = nullptr;
        InitializedPayloadSerializer() { ++instances; }
    protected:
        void Initialize(Intermediate::IntermediateSerializer& serializer) override
        {
            ++calls;
            owner = &serializer;
            nested = &serializer.GetTypeSerializer(System::Type::From<std::string>());
        }
        void ScanChildren(Intermediate::IntermediateSerializer& serializer,
                          const ChildCallback&, const InitializedPayload&) override
        {
            ++scans;
            Check(calls == 1, "initialized before child scan");
            Check(owner == &serializer, "scan uses initialization owner");
        }
        void Serialize(Intermediate::IntermediateWriter& output, const InitializedPayload& value,
                       const ContentSerializerAttribute&) override
        {
            ++writes;
            Check(calls == 1, "initialized before write");
            Check(owner == &output.getSerializerProperty(), "write uses initialization owner");
            output.getXmlProperty().WriteString(value.text);
        }
        InitializedPayload Deserialize(Intermediate::IntermediateReader& input,
                                       const ContentSerializerAttribute& format, InitializedPayload) override
        {
            ++reads;
            Check(calls == 1, "initialized before read");
            Check(owner == &input.getSerializerProperty(), "read uses initialization owner");
            return {input.ReadText(format)};
        }
    };
}
TEST(IntermediateInitializationCompatibilityTest, RetainsOwnerAndNestedCachesAcrossOperations)
{
    using S = Intermediate::IntermediateSerializer;
    try
    {
        (void)S::TypeSerializerFor<std::string>();
        auto candidate = std::make_unique<InitializedPayloadSerializer>();
        auto* serializer = candidate.get();
        (void)S::RegisterTypeSerializer(std::move(candidate));
        std::string previous;
        for (int pass = 0; pass != 2; ++pass)
        {
            std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
            S::Serialize<InitializedPayload>(*writer, InitializedPayload{}, "");
            const auto text = writer->ToString();
            if (pass != 0) Check(text == previous, "repeated output is identical");
            previous = text;
            std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(text));
            Check(S::Deserialize<InitializedPayload>(*reader, "").text == "retained", "exact round trip");
        }
        Check(InitializedPayloadSerializer::instances == 1, "one cached custom serializer across four operations");
        Check(serializer->calls == 1, "one Initialize invocation");
        Check(serializer->calls == 1 && serializer->scans == 2 && serializer->writes == 2 && serializer->reads == 2,
              "exact callback totals");
        Check(serializer->nested != nullptr && serializer->nested->getTargetTypeProperty() == System::Type::From<std::string>(),
              "nested lookup inside Initialize");
        Check(serializer->owner != nullptr && &serializer->owner->GetTypeSerializer(System::Type::From<InitializedPayload>()) == serializer,
              "owner and custom cache survive all operations");
        Check(serializer->owner != nullptr && &serializer->owner->GetTypeSerializer(System::Type::From<std::string>()) == serializer->nested,
              "nested cached identity survives all operations");
    }
    catch (const std::exception& error) { ++failures; std::cout << "UNEXPECTED " << error.what() << '\n'; }
    std::cout << "checks=" << checks << " failed=" << failures << '\n';
    EXPECT_EQ(checks, 21);
    EXPECT_EQ(failures, 0);
}
