// SPDX-License-Identifier: MS-PL
// ISSUE-046: external target identities need namespace resolution, not serialization.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace
{
    namespace P = Microsoft::Xna::Framework::Content::Pipeline;
    namespace I = P::Serialization::Intermediate;
    using S = I::IntermediateSerializer;
    class Target : public System::Object
    {
    public:
        static constexpr std::string_view XnaTypeName = "CnaTests.ExternalAlias.Target";
        const std::string& GetTypeName() const override { static const std::string name(XnaTypeName); return name; }
    private:
        Target() = default; // This target deliberately has no XML serializer or public factory.
    };
    struct Scene : System::Object
    {
        static constexpr std::string_view XnaTypeName = "CnaTests.ExternalAlias.Scene";
        const std::string& GetTypeName() const override { static const std::string name(XnaTypeName); return name; }
        std::shared_ptr<P::ExternalReference<Target>> reference;
        static void DescribeContent(I::ContentTypeDescriptor<Scene>& type) { type.Field("Reference", &Scene::reference); }
    };
    struct GenericScene : System::Object
    {
        static constexpr std::string_view XnaTypeName = "CnaTests.ExternalAlias.GenericScene";
        const std::string& GetTypeName() const override { static const std::string name(XnaTypeName); return name; }
        std::shared_ptr<P::ExternalReference<std::vector<Target>>> reference;
        static void DescribeContent(I::ContentTypeDescriptor<GenericScene>& type) { type.Field("Reference", &GenericScene::reference); }
    };
    struct AliasCase { const char* name; const char* target; const char* localNamespace; const char* failure; bool generic = false; };
    class IntermediateExternalAliasCompatibilityTest : public testing::TestWithParam<AliasCase> {};

    template<class T> void CheckRead(const std::string& xml, const char* failure)
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        try
        {
            auto result = S::Deserialize<T>(*reader, "");
            ASSERT_EQ(failure, nullptr) << "Expected rejection";
            ASSERT_NE(result, nullptr);
            ASSERT_NE(result->reference, nullptr);
            EXPECT_EQ(result->reference->getFilenameProperty(), "C:\\references\\payload.bin");
        }
        catch (const P::InvalidContentException& error)
        {
            ASSERT_NE(failure, nullptr) << error.getMessageProperty();
            EXPECT_NE(error.getMessageProperty().find(failure), std::string::npos) << error.getMessageProperty();
        }
    }
}

TEST_P(IntermediateExternalAliasCompatibilityTest, ResolvesIdentityWithoutCreatingTargetSerializer)
{
    const auto& item = GetParam();
    S::RegisterKnownTypeName("CnaTests.OtherAlias.Target");
    const std::string root = item.generic ? "GenericScene" : "Scene";
    const std::string xml = "<XnaContent xmlns:R=\"CnaTests.ExternalAlias\"><Asset Type=\"R:" + root +
        "\"><Reference><Reference>#e</Reference></Reference></Asset><ExternalReferences>" +
        "<ExternalReference ID=\"#e\" TargetType=\"" + item.target + "\"" + item.localNamespace +
        ">C:\\references\\payload.bin</ExternalReference></ExternalReferences></XnaContent>";
    if (item.generic) CheckRead<GenericScene>(xml, item.failure);
    else CheckRead<Scene>(xml, item.failure);
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<Target>()), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<std::vector<Target>>()), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(std::string("R:Target")), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(std::string("L:Target")), nullptr);
}

INSTANTIATE_TEST_SUITE_P(GenuineXna, IntermediateExternalAliasCompatibilityTest, testing::Values(
    AliasCase{"Canonical", "CnaTests.ExternalAlias.Target", "", nullptr},
    AliasCase{"RootAlias", "R:Target", "", nullptr},
    AliasCase{"EntryLocalAlias", "L:Target", " xmlns:L=\"CnaTests.ExternalAlias\"", nullptr},
    AliasCase{"AssemblyQualified", "CnaTests.ExternalAlias.Target, CnaTests", "", nullptr},
    AliasCase{"LocalShadow", "R:Target", " xmlns:R=\"CnaTests.OtherAlias\"", "wrong type for external reference"},
    AliasCase{"UnknownPrefix", "Missing:Target", "", "Cannot find type"},
    AliasCase{"UnknownTarget", "R:Absent", "", "Cannot find type"},
    AliasCase{"WrongShortcut", "int", "", "wrong type for external reference"},
    AliasCase{"CanonicalWhitespace", " CnaTests.ExternalAlias.Target ", "", "Cannot find type"},
    AliasCase{"GenericAliasArgument", "System.Collections.Generic.List[R:Target]", "", nullptr, true},
    AliasCase{"GenericLocalAliases", "G:List[L:Target]", " xmlns:G=\"System.Collections.Generic\" xmlns:L=\"CnaTests.ExternalAlias\"", nullptr, true},
    AliasCase{"GenericCanonical", "System.Collections.Generic.List[CnaTests.ExternalAlias.Target]", "", nullptr, true},
    AliasCase{"AssemblyTrailingWhitespace", "CnaTests.ExternalAlias.Target, CnaTests ", "", nullptr},
    AliasCase{"QualifiedLeadingWhitespace", " CnaTests.ExternalAlias.Target, CnaTests", "", nullptr},
    AliasCase{"GenericTrailingWhitespace", "System.Collections.Generic.List[R:Target] ", "", "Cannot find type", true}
), [](const testing::TestParamInfo<AliasCase>& info) { return info.param.name; });

TEST(IntermediateExternalAliasRoundTripTest, WriterAliasReadsBackWithoutATargetSerializer)
{
    auto scene = std::make_shared<Scene>();
    scene->reference = std::make_shared<P::ExternalReference<Target>>("C:\\references\\payload.bin");
    std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
    S::Serialize<Scene>(*writer, scene, "");
    const std::string xml = writer->ToString();
    EXPECT_NE(xml.find("TargetType=\"ExternalAlias:Target\""), std::string::npos);
    CheckRead<Scene>(xml, nullptr);
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<Target>()), nullptr);
}

TEST(IntermediateExternalAliasRoundTripTest, WriterGenericIdentityMatchesTheDeclaredTarget)
{
    auto scene = std::make_shared<GenericScene>();
    scene->reference = std::make_shared<P::ExternalReference<std::vector<Target>>>("C:\\references\\payload.bin");
    std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
    S::Serialize<GenericScene>(*writer, scene, "");
    CheckRead<GenericScene>(writer->ToString(), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<std::vector<Target>>()), nullptr);
}
