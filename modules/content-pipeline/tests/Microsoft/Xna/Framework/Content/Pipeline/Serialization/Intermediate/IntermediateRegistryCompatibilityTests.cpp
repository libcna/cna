// SPDX-License-Identifier: MS-PL
// Genuine contract: cna-demos/docs/repair-evidence/content/IntermediateCollisionOracle.cs.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
    namespace I = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
    using S = I::IntermediateSerializer;
    using Microsoft::Xna::Framework::Content::ContentSerializerAttribute;

    struct ShortcutFirst { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.ShortcutFirst"; std::string text; };
    struct ShortcutSecond { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.ShortcutSecond"; std::string text; };
    struct AdditionalFirst { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.AdditionalFirst"; std::string text; };
    struct AdditionalSecond { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.AdditionalSecond"; std::string text; };
    struct FullFirst { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.FullFirst"; std::string text; };
    struct FullSecond { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.FullSecond"; std::string text; };
    struct NameFirst { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.NameFirst"; std::string text; };
    struct NameSecond { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.NameSecond"; std::string text; };
    struct Repeated { static constexpr std::string_view XnaTypeName = "CnaTests.Registry.Repeated"; std::string text; };

    template<class T> class AliasSerializer final : public I::ContentTypeSerializer<T>
    {
    public:
        explicit AliasSerializer(std::string alias) : I::ContentTypeSerializer<T>(std::move(alias)) {}
    protected:
        void Serialize(I::IntermediateWriter& output, const T& value, const ContentSerializerAttribute&) override
        { output.getXmlProperty().WriteString(value.text); }
        T Deserialize(I::IntermediateReader& input, const ContentSerializerAttribute& format, T) override
        { return T{input.ReadText(format)}; }
    };

    template<class T> I::ContentTypeSerializerBase& Register(std::string alias,
                                                           std::vector<std::string> names = {}, bool registerName = true)
    {
        return S::RegisterTypeSerializer(std::make_unique<AliasSerializer<T>>(std::move(alias)),
                                         std::move(names), registerName);
    }

    template<class T> void ExpectUnpublished()
    {
        EXPECT_EQ(S::FindTypeSerializer(System::Type::From<T>()), nullptr);
        EXPECT_EQ(S::FindTypeSerializerForCarrier(typeid(T)), nullptr);
        EXPECT_EQ(S::FindTypeSerializer(std::string(T::XnaTypeName)), nullptr);
        EXPECT_FALSE(S::IsKnownTypeName(std::string(T::XnaTypeName)));
    }

    template<class T> std::string Write(const T& value)
    {
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
        S::Serialize<T>(*writer, value, "");
        return writer->ToString();
    }

    template<class T> void ExpectRoundTrip(const std::string& text, const std::string& alias)
    {
        const auto xml = Write(T{text});
        EXPECT_NE(xml.find("Type=\"" + alias + "\""), std::string::npos);
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        EXPECT_EQ(S::Deserialize<T>(*reader, "").text, text);
    }
}

TEST(IntermediateRegistryCompatibilityTest, ShortcutCollisionIsAtomicAndRejectedTypeCanRecover)
{
    auto& first = Register<ShortcutFirst>("registryShortcut");
    const auto original = Write(ShortcutFirst{"retained"});
    EXPECT_THROW(Register<ShortcutSecond>("registryShortcut"), System::InvalidOperationException);
    ExpectUnpublished<ShortcutSecond>();
    EXPECT_EQ(S::FindTypeSerializer(std::string("registryShortcut")), &first);
    EXPECT_EQ(Write(ShortcutFirst{"retained"}), original);

    auto replacement = std::make_unique<AliasSerializer<ShortcutSecond>>("registryRecovered");
    const auto* expected = replacement.get();
    auto& second = S::RegisterTypeSerializer(std::move(replacement));
    EXPECT_EQ(&second, expected);
    EXPECT_EQ(S::FindTypeSerializer(std::string("registryRecovered")), &second);
    const auto xml = Write(ShortcutSecond{"recovered"});
    EXPECT_NE(xml.find("Type=\"registryRecovered\""), std::string::npos);
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
    EXPECT_EQ(S::Deserialize<ShortcutSecond>(*reader, "").text, "recovered");
}

TEST(IntermediateRegistryCompatibilityTest, LateCanonicalAdditionalNameCollisionPublishesNothing)
{
    auto& first = Register<AdditionalFirst>("", {"CnaTests.Registry.Shared, TestAssembly"});
    EXPECT_THROW(Register<AdditionalSecond>("", {"CnaTests.Registry.Fresh", "CnaTests.Registry.Shared"}, false),
                 System::InvalidOperationException);
    ExpectUnpublished<AdditionalSecond>();
    EXPECT_EQ(S::FindTypeSerializer(std::string("CnaTests.Registry.Fresh")), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(std::string("CnaTests.Registry.Shared")), &first);
}

TEST(IntermediateRegistryCompatibilityTest, ShortcutTakesPrecedenceOverAnExistingFullName)
{
    auto& first = Register<FullFirst>("registryFullFirst");
    auto& second = Register<FullSecond>(std::string(FullFirst::XnaTypeName));
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<FullFirst>()), &first);
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<FullSecond>()), &second);
    EXPECT_EQ(S::FindTypeSerializer(std::string(FullFirst::XnaTypeName)), &second);
    ExpectRoundTrip<FullFirst>("first retained", "registryFullFirst");
    ExpectRoundTrip<FullSecond>("second retained", std::string(FullFirst::XnaTypeName));
}

TEST(IntermediateRegistryCompatibilityTest, FullNameCannotShadowAnExistingShortcut)
{
    auto& first = Register<NameFirst>(std::string(NameSecond::XnaTypeName));
    ASSERT_TRUE(S::IsKnownTypeName(std::string(NameSecond::XnaTypeName)));
    auto& second = Register<NameSecond>("registryNameSecond");
    EXPECT_EQ(S::FindTypeSerializer(System::Type::From<NameSecond>()), &second);
    EXPECT_EQ(S::FindTypeSerializerForCarrier(typeid(NameSecond)), &second);
    EXPECT_EQ(S::FindTypeSerializer(std::string("registryNameSecond")), &second);
    // This name already belongs to the first serializer as an alias; preserve that fact.
    EXPECT_TRUE(S::IsKnownTypeName(std::string(NameSecond::XnaTypeName)));
    EXPECT_EQ(S::FindTypeSerializer(std::string(NameSecond::XnaTypeName)), &first);
    ExpectRoundTrip<NameFirst>("alias owner retained", std::string(NameSecond::XnaTypeName));
    ExpectRoundTrip<NameSecond>("full name owner retained", "registryNameSecond");
}

TEST(IntermediateRegistryCompatibilityTest, RepeatedTargetKeepsTheFirstRegistrationWithoutNewAliases)
{
    auto& first = Register<Repeated>("registryRepeated");
    auto& repeated = Register<Repeated>("registryUnused", {"CnaTests.Registry.Unused"});
    EXPECT_EQ(&first, &repeated);
    EXPECT_EQ(S::FindTypeSerializer(std::string("registryRepeated")), &first);
    EXPECT_EQ(S::FindTypeSerializer(std::string("registryUnused")), nullptr);
    EXPECT_EQ(S::FindTypeSerializer(std::string("CnaTests.Registry.Unused")), nullptr);
}

// .NET's Attribute.Equals compares the type and every instance field, and Match defaults to it, so
// two independently made instances of this field-less attribute are equal (museum wk-cnbloader:
// CNA inherited sharp-runtime's identity comparison).
TEST(IntermediateRegistryCompatibilityTest, ContentTypeSerializerAttributesCompareByValue)
{
    using Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeSerializerAttribute;
    const ContentTypeSerializerAttribute first;
    const ContentTypeSerializerAttribute second;
    EXPECT_TRUE(first.Equals(second));
    EXPECT_TRUE(second.Equals(first));
    EXPECT_TRUE(first.Match(second));
    EXPECT_EQ(first.GetHashCode(), second.GetHashCode());
}
