// SPDX-License-Identifier: MS-PL
// ISSUE-148: genuine XNA evidence in cna-demos IntermediateSectionsOracle.cs.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace
{
    struct SectionCase
    {
        const char* name;
        const char* xml;
        const char* rejectedNode;
    };
    class IntermediateSectionCompatibilityTest : public testing::TestWithParam<SectionCase> {};
}

TEST_P(IntermediateSectionCompatibilityTest, MatchesGenuineSectionTraversal)
{
    using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
    using Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::IntermediateSerializer;
    const auto& test = GetParam();
    const std::string xml = std::string("<XnaContent><Asset Type=\"int\">37</Asset>") + test.xml + "</XnaContent>";
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
    try
    {
        const int result = IntermediateSerializer::Deserialize<int>(*reader, "");
        EXPECT_EQ(result, 37);
        EXPECT_EQ(test.rejectedNode, nullptr) << "XNA rejects this section form";
    }
    catch (const InvalidContentException& error)
    {
        ASSERT_NE(test.rejectedNode, nullptr) << error.getMessageProperty();
        EXPECT_EQ(typeid(error), typeid(InvalidContentException));
        const std::string prefix = std::string("There was an error while deserializing intermediate XML. '") +
                                   test.rejectedNode + "' is an invalid XmlNodeType.";
        EXPECT_TRUE(error.getMessageProperty().starts_with(prefix)) << error.getMessageProperty();
    }
}

INSTANTIATE_TEST_SUITE_P(GenuineXna, IntermediateSectionCompatibilityTest, testing::Values(
    SectionCase{"Absent", "", nullptr},
    SectionCase{"ResourcesPaired", "<Resources></Resources>", nullptr},
    SectionCase{"ExternalsPaired", "<ExternalReferences></ExternalReferences>", nullptr},
    SectionCase{"BothPaired", "<Resources></Resources><ExternalReferences></ExternalReferences>", nullptr},
    SectionCase{"BothPairedWithComments", "<Resources><!--a--></Resources><!--b--><ExternalReferences> </ExternalReferences>", nullptr},
    SectionCase{"ResourcesSelfClosing", "<Resources />", "None"},
    SectionCase{"ExternalsSelfClosing", "<ExternalReferences />", "None"},
    SectionCase{"ResourcesSelfClosingBeforePaired", "<Resources /><ExternalReferences></ExternalReferences>", "Element"},
    SectionCase{"ExternalsSelfClosingAfterPaired", "<Resources></Resources><ExternalReferences />", "None"},
    SectionCase{"BothSelfClosing", "<Resources /><ExternalReferences />", "Element"},
    SectionCase{"ResourcesSelfClosingWithComment", "<Resources /><!--tail-->", "None"},
    SectionCase{"ExternalsSelfClosingWithComment", "<ExternalReferences /><!--tail-->", "None"},
    SectionCase{"ReversePairedOrder", "<ExternalReferences></ExternalReferences><Resources></Resources>", "Element"}
), [](const testing::TestParamInfo<SectionCase>& info) { return info.param.name; });

namespace
{
    namespace P = Microsoft::Xna::Framework::Content::Pipeline;
    namespace I = P::Serialization::Intermediate;
    struct SectionNode : System::Object
    {
        static constexpr std::string_view XnaTypeName = "CnaTests.SectionNode";
        const std::string& GetTypeName() const override { static const std::string name(XnaTypeName); return name; }
        int value = 0;
        static void DescribeContent(I::ContentTypeDescriptor<SectionNode>& type) { type.Field("Value", &SectionNode::value); }
    };
    struct SectionRoot : System::Object
    {
        static constexpr std::string_view XnaTypeName = "CnaTests.SectionRoot";
        const std::string& GetTypeName() const override { static const std::string name(XnaTypeName); return name; }
        std::shared_ptr<SectionNode> shared;
        std::shared_ptr<P::ExternalReference<SectionNode>> external;
        static void DescribeContent(I::ContentTypeDescriptor<SectionRoot>& type)
        {
            type.Field("Shared", &SectionRoot::shared).SharedResource();
            type.Field("External", &SectionRoot::external);
        }
    };
    struct FixupCase { const char* name; const char* shared; const char* sections; const char* message; bool location = false; };
    class IntermediateSectionFixupOrderTest : public testing::TestWithParam<FixupCase> {};
}

TEST_P(IntermediateSectionFixupOrderTest, ResolvesEachSectionBeforeReadingTheNext)
{
    const auto& item = GetParam();
    const std::string xml = std::string("<XnaContent><Asset Type=\"CnaTests.SectionRoot\">") + item.shared +
        "<External><Reference>#e</Reference></External></Asset>" + item.sections + "</XnaContent>";
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
    try
    {
        (void)I::IntermediateSerializer::Deserialize<SectionRoot>(*reader, "");
        FAIL() << "Genuine XNA rejects this document";
    }
    catch (const P::InvalidContentException& error)
    {
        if (item.location) EXPECT_TRUE(error.getMessageProperty().starts_with(item.message)) << error.getMessageProperty();
        else EXPECT_EQ(error.getMessageProperty(), item.message);
    }
}

INSTANTIATE_TEST_SUITE_P(GenuineXna, IntermediateSectionFixupOrderTest, testing::Values(
    FixupCase{"SharedBeforeMissingExternalSection", "<Shared>#s</Shared>", "<Resources />", "Missing shared resource \"#s\"."},
    FixupCase{"SharedBeforeInvalidExternalType", "<Shared>#s</Shared>",
        "<Resources></Resources><ExternalReferences><ExternalReference ID=\"#e\" TargetType=\"No.Such.Type\">C:\\a.bin</ExternalReference></ExternalReferences>",
        "Missing shared resource \"#s\"."},
    FixupCase{"SectionEndBeforeSharedFixup", "<Shared>#s</Shared>", "<Resources /><Unexpected />",
        "There was an error while deserializing intermediate XML. 'Element' is an invalid XmlNodeType.", true},
    FixupCase{"ExternalBeforeEndDocument", "<Shared Null=\"true\" />", "<ExternalReferences />", "Missing external reference \"#e\"."},
    FixupCase{"ExternalBeforeTrailingElement", "<Shared Null=\"true\" />", "<ExternalReferences></ExternalReferences><Unexpected />", "Missing external reference \"#e\"."}
), [](const testing::TestParamInfo<FixupCase>& info) { return info.param.name; });
