// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-031..042, XNAPP-045: behaviour and compile-parity tests
// for the pipeline-core facade -- every type here is an XNA 4.0 public type, exercised through
// the C++ spelling docs/xna-content-pipeline-compat-api.md defines, and bridged into the one
// canonical registry.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ChildCollection.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/NamedValueDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineComponentScanner.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ProcessorParameter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/FormatException.hpp"
#include "System/InvalidCastException.hpp"

namespace
{
    namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Canon = CNA::Content::Pipeline;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;

    // ---- fixture types -------------------------------------------------------------------

    /// A reference-typed content item, as a game's own intermediate type would be declared.
    class GreetingContent final : public Xna::ContentItem
    {
    public:
        static constexpr std::string_view XnaTypeName = "TestGame.Pipeline.GreetingContent";
        std::string text;
        const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    /// A node type with a parent back-reference, to exercise ChildCollection.
    class TreeNode;
    class TreeNodeCollection final : public Xna::ChildCollection<TreeNode, TreeNode>
    {
    public:
        explicit TreeNodeCollection(TreeNode* parent) : ChildCollection(parent) {}

    protected:
        TreeNode* GetParent(const std::shared_ptr<TreeNode>& child) const override;
        void SetParent(const std::shared_ptr<TreeNode>& child, TreeNode* parent) override;
    };

    class TreeNode final : public Xna::ContentItem
    {
    public:
        static constexpr std::string_view XnaTypeName = "TestGame.Pipeline.TreeNode";
        TreeNode() : children(this) {}
        TreeNode* parent = nullptr;
        TreeNodeCollection children;
        const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    TreeNode* TreeNodeCollection::GetParent(const std::shared_ptr<TreeNode>& child) const { return child->parent; }
    void TreeNodeCollection::SetParent(const std::shared_ptr<TreeNode>& child, TreeNode* parent) { child->parent = parent; }

    enum class Tone { Plain, Loud, Whisper };
}

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    template<>
    struct ContentTypeName<Tone>
    {
        static std::string Name() { return "TestGame.Pipeline.Tone"; }
    };
}

namespace
{
    /// An XNA-shaped importer reading a text file into GreetingContent.
    class GreetingImporter final : public Xna::ContentImporter<GreetingContent>
    {
    public:
        std::shared_ptr<GreetingContent> Import(const std::string& filename,
                                                Xna::ContentImporterContext& context) override
        {
            std::ifstream stream(filename, std::ios::binary);
            auto content = std::make_shared<GreetingContent>();
            std::getline(stream, content->text);
            content->setIdentityProperty(Xna::ContentIdentity(filename, "GreetingImporter"));
            content->setNameProperty(std::filesystem::path(filename).stem().string());
            context.getLoggerProperty().LogMessage("imported {0} bytes", content->text.size());
            return content;
        }
    };

    /// An XNA-shaped processor with three declared parameters, one of them an enum.
    class GreetingProcessor final : public Xna::ContentProcessor<GreetingContent, std::string>
    {
    public:
        [[nodiscard]] std::string getPrefixProperty() const { return prefix_; }
        void setPrefixProperty(std::string value) { prefix_ = std::move(value); }
        [[nodiscard]] std::int32_t getRepeatProperty() const { return repeat_; }
        void setRepeatProperty(std::int32_t value) { repeat_ = value; }
        [[nodiscard]] Tone getToneProperty() const { return tone_; }
        void setToneProperty(Tone value) { tone_ = value; }
        [[nodiscard]] Color getTintProperty() const { return tint_; }
        void setTintProperty(Color value) { tint_ = value; }

        static void DescribeParameters(Xna::ProcessorParameterBindings<GreetingProcessor>& bindings)
        {
            bindings.Add<std::string>("Prefix", &GreetingProcessor::getPrefixProperty, &GreetingProcessor::setPrefixProperty,
                                      "Prefix", "Text placed before the greeting.");
            bindings.Add<std::int32_t>("Repeat", &GreetingProcessor::getRepeatProperty, &GreetingProcessor::setRepeatProperty);
            bindings.AddEnum<Tone>("Tone", &GreetingProcessor::getToneProperty, &GreetingProcessor::setToneProperty,
                                   {{"Plain", Tone::Plain}, {"Loud", Tone::Loud}, {"Whisper", Tone::Whisper}});
            bindings.Add<Color>("Tint", &GreetingProcessor::getTintProperty, &GreetingProcessor::setTintProperty);
        }

        std::string Process(const std::shared_ptr<GreetingContent>& input, Xna::ContentProcessorContext& context) override
        {
            std::string out;
            for (std::int32_t i = 0; i < repeat_; ++i) { out += prefix_ + input->text; }
            if (tone_ == Tone::Loud) { for (char& c : out) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); } }
            context.getLoggerProperty().LogImportantMessage("platform {0}", Xna::TargetPlatformName(context.getTargetPlatformProperty()));
            return out;
        }

    private:
        std::string prefix_ = "> ";
        std::int32_t repeat_ = 1;
        Tone tone_ = Tone::Plain;
        Color tint_{255, 0, 255, 255};
    };

    class RecordingLogger final : public Canon::ContentBuildLogger
    {
    public:
        std::vector<Canon::ContentLogMessage> messages;
        void Log(const Canon::ContentLogMessage& message) override { messages.push_back(message); }
    };

    std::filesystem::path MakeTempRoot(const std::string& tag)
    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("cna-xnapp-core-" + tag + "-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    // ---- ContentIdentity / ContentItem ------------------------------------------------------

    TEST(XnaContentIdentity, ConstructorsFillTheThreeStringsInOrder)
    {
        Xna::ContentIdentity empty;
        EXPECT_TRUE(empty.IsEmpty());
        Xna::ContentIdentity one("C:/a.png");
        EXPECT_EQ(one.getSourceFilenameProperty(), "C:/a.png");
        EXPECT_TRUE(one.getSourceToolProperty().empty());
        Xna::ContentIdentity two("C:/a.png", "tool");
        EXPECT_EQ(two.getSourceToolProperty(), "tool");
        Xna::ContentIdentity three("C:/a.png", "tool", "line 7");
        EXPECT_EQ(three.getFragmentIdentifierProperty(), "line 7");
        EXPECT_EQ(three.ToString(), "C:/a.png#line 7");
        EXPECT_EQ(one.ToString(), "C:/a.png");
        three.setSourceToolProperty("other");
        three.setFragmentIdentifierProperty("");
        three.setSourceFilenameProperty("b.png");
        EXPECT_EQ(three, Xna::ContentIdentity("b.png", "other"));
        EXPECT_NE(three, one);
    }

    TEST(XnaContentItem, PropertiesRoundTripAndTypeNameIsTheDotNetName)
    {
        Xna::ContentItem item;
        EXPECT_TRUE(item.getIdentityProperty().IsEmpty());
        EXPECT_TRUE(item.getNameProperty().empty());
        EXPECT_EQ(item.getOpaqueDataProperty().getCountProperty(), 0);
        item.setNameProperty("hero");
        item.setIdentityProperty(Xna::ContentIdentity("hero.fbx"));
        item.getOpaqueDataProperty().SetValue<std::int32_t>("lod", 3);
        EXPECT_EQ(item.getNameProperty(), "hero");
        EXPECT_EQ(item.getIdentityProperty().getSourceFilenameProperty(), "hero.fbx");
        EXPECT_EQ(item.getOpaqueDataProperty().GetValue<std::int32_t>("lod", 0), 3);
        EXPECT_EQ(item.GetTypeName(), "Microsoft.Xna.Framework.Content.Pipeline.ContentItem");
        EXPECT_EQ(Xna::ContentTypeName<Xna::ContentItem>::Name(), item.GetTypeName());
    }

    // ---- NamedValueDictionary / OpaqueDataDictionary -----------------------------------------

    TEST(XnaNamedValueDictionary, KeepsInsertionOrderAndEnforcesDictionarySemantics)
    {
        Xna::NamedValueDictionary<int> d;
        d.Add("b", 2);
        d.Add("a", 1);
        EXPECT_EQ(d.getCountProperty(), 2);
        EXPECT_EQ(d["a"], 1);
        EXPECT_THROW(d.Add("a", 9), System::ArgumentException);
        EXPECT_THROW(d.Add("", 9), System::ArgumentException);
        EXPECT_THROW((void)d["zzz"], System::Collections::Generic::KeyNotFoundException);
        d.Set("a", 11);
        d.Set("c", 3);
        EXPECT_EQ(d["a"], 11);
        EXPECT_EQ(d.getKeysProperty(), (std::vector<std::string>{"b", "a", "c"}));
        EXPECT_EQ(d.getValuesProperty(), (std::vector<int>{2, 11, 3}));
        int value = 0;
        EXPECT_TRUE(d.TryGetValue("c", value));
        EXPECT_EQ(value, 3);
        EXPECT_FALSE(d.TryGetValue("nope", value));
        EXPECT_TRUE(d.ContainsKey("b"));
        EXPECT_TRUE(d.Remove("b"));
        EXPECT_FALSE(d.Remove("b"));
        std::vector<std::string> seen;
        for (const auto& entry : d) { seen.push_back(entry.first); }
        EXPECT_EQ(seen, (std::vector<std::string>{"a", "c"}));
        std::unique_ptr<System::Collections::Generic::IEnumerator<std::pair<std::string, int>>> e(d.GetEnumerator());
        ASSERT_TRUE(e->MoveNext());
        EXPECT_EQ(e->Current().first, "a");
        ASSERT_TRUE(e->MoveNext());
        EXPECT_FALSE(e->MoveNext());
        d.Clear();
        EXPECT_EQ(d.getCountProperty(), 0);
    }

    TEST(XnaOpaqueDataDictionary, GetValueReturnsDefaultTypedValueOrThrowsInvalidCast)
    {
        Xna::OpaqueDataDictionary data;
        data.SetValue<float>("Scale", 2.5f);
        data.SetValue<std::string>("Name", "hero");
        data.SetValue<Vector3>("Offset", Vector3(1, 2, 3));
        EXPECT_FLOAT_EQ(data.GetValue<float>("Scale", 1.0f), 2.5f);
        EXPECT_EQ(data.GetValue<std::string>("Name", "none"), "hero");
        EXPECT_EQ(data.GetValue<Vector3>("Offset", Vector3()), Vector3(1, 2, 3));
        EXPECT_EQ(data.GetValue<std::int32_t>("Missing", 42), 42);
        EXPECT_THROW((void)data.GetValue<std::int32_t>("Scale", 0), System::InvalidCastException);
        EXPECT_THROW(data.Add("Empty", Xna::ContentObject{}), System::ArgumentException);
        EXPECT_EQ(data.getKeysProperty(), (std::vector<std::string>{"Scale", "Name", "Offset"}));
        // The measured XNA form (tests/reference/xna40/intermediate/opaque_data_dictionary.getcontentasxml.txt):
        // compact, a utf-16 declaration, a Type attribute on every value that is not a string.
        EXPECT_EQ(data.GetContentAsXml(),
                  "<?xml version=\"1.0\" encoding=\"utf-16\"?><XnaContent xmlns:Pipeline=\"Microsoft.Xna.Framework.Content.Pipeline\" "
                  "xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Pipeline:OpaqueDataDictionary\">"
                  "<Data Key=\"Scale\" Type=\"float\">2.5</Data><Data Key=\"Name\">hero</Data>"
                  "<Data Key=\"Offset\" Type=\"Framework:Vector3\">1 2 3</Data></Asset></XnaContent>");
        EXPECT_EQ(Xna::OpaqueDataDictionary().GetContentAsXml(), "");
    }

    // ---- ChildCollection ------------------------------------------------------------------

    TEST(XnaChildCollection, AttachesDetachesAndRefusesAdoptedChildren)
    {
        auto root = std::make_shared<TreeNode>();
        auto other = std::make_shared<TreeNode>();
        auto a = std::make_shared<TreeNode>();
        auto b = std::make_shared<TreeNode>();
        root->children.Add(a);
        EXPECT_EQ(a->parent, root.get());
        EXPECT_THROW(other->children.Add(a), System::ArgumentException);
        EXPECT_THROW(root->children.Add(nullptr), System::ArgumentNullException);
        root->children.Add(b);
        EXPECT_EQ(root->children.getCountProperty(), 2);
        EXPECT_TRUE(root->children.Remove(a));
        EXPECT_EQ(a->parent, nullptr);
        other->children.Add(a);
        EXPECT_EQ(a->parent, other.get());
        auto c = std::make_shared<TreeNode>();
        // The C# indexer setter is setItem() in sharp-runtime's Collection<T>; an assignment
        // through operator[] writes the slot directly and bypasses the virtual SetItem hook.
        root->children.setItem(0, c);
        EXPECT_EQ(b->parent, nullptr);
        EXPECT_EQ(c->parent, root.get());
        root->children.Clear();
        EXPECT_EQ(c->parent, nullptr);
        EXPECT_EQ(root->children.getCountProperty(), 0);
    }

    // ---- exceptions -----------------------------------------------------------------------

    TEST(XnaExceptions, InvalidContentCarriesIdentityAndInnerAndPipelineFormatsMessages)
    {
        Xna::InvalidContentException plain;
        EXPECT_FALSE(plain.getMessageProperty().empty());
        Xna::InvalidContentException withIdentity("bad normals", Xna::ContentIdentity("m.fbx", "fbx", "mesh 3"));
        EXPECT_EQ(withIdentity.getMessageProperty(), "bad normals");
        EXPECT_EQ(withIdentity.getContentIdentityProperty().getFragmentIdentifierProperty(), "mesh 3");
        std::exception_ptr inner;
        try { throw std::runtime_error("root cause"); } catch (...) { inner = std::current_exception(); }
        Xna::InvalidContentException chained("outer", Xna::ContentIdentity("m.fbx"), inner);
        EXPECT_TRUE(chained.getInnerExceptionProperty() != nullptr);
        Xna::InvalidContentException chained2("outer", inner);
        EXPECT_TRUE(chained2.getContentIdentityProperty().IsEmpty());
        chained2.setContentIdentityProperty(Xna::ContentIdentity("x"));
        EXPECT_EQ(chained2.getContentIdentityProperty().getSourceFilenameProperty(), "x");
        EXPECT_THROW(throw withIdentity, System::Exception);

        Xna::PipelineException formatted("missing processor '{0}' for {1}", std::string("Foo"), 3);
        EXPECT_EQ(formatted.getMessageProperty(), "missing processor 'Foo' for 3");
        Xna::PipelineException sequential("{} and {}", "a", "b");
        EXPECT_EQ(sequential.getMessageProperty(), "a and b");
        Xna::PipelineException simple("plain");
        EXPECT_EQ(simple.getMessageProperty(), "plain");
        Xna::PipelineException withInner("outer", inner);
        EXPECT_TRUE(withInner.getInnerExceptionProperty() != nullptr);
        EXPECT_FALSE(Xna::PipelineException().getMessageProperty().empty());
    }

    // ---- ExternalReference ----------------------------------------------------------------

    TEST(XnaExternalReference, ResolvesRelativeNamesAgainstTheReferencingSource)
    {
        Xna::ExternalReference<GreetingContent> empty;
        EXPECT_TRUE(empty.getFilenameProperty().empty());
        Xna::ExternalReference<GreetingContent> absolute("/content/textures/wall.png");
        EXPECT_EQ(absolute.getFilenameProperty(), "/content/textures/wall.png");
        Xna::ContentIdentity model("/content/models/hero.fbx");
        Xna::ExternalReference<GreetingContent> relative("../textures/skin.png", model);
        EXPECT_EQ(relative.getFilenameProperty(), "/content/textures/skin.png");
        Xna::ExternalReference<GreetingContent> alreadyAbsolute("/elsewhere/x.png", model);
        EXPECT_EQ(alreadyAbsolute.getFilenameProperty(), "/elsewhere/x.png");
        EXPECT_THROW((Xna::ExternalReference<GreetingContent>("skin.png", Xna::ContentIdentity())), System::ArgumentException);
        EXPECT_THROW((Xna::ExternalReference<GreetingContent>("", model)), System::ArgumentException);
        EXPECT_EQ(relative.GetTypeName(),
                  "Microsoft.Xna.Framework.Content.Pipeline.ExternalReference`1[[TestGame.Pipeline.GreetingContent]]");
        EXPECT_EQ(Xna::ContentTypeName<Xna::ExternalReference<GreetingContent>>::Name(), relative.GetTypeName());
        relative.setFilenameProperty("z");
        EXPECT_EQ(relative.getFilenameProperty(), "z");
    }

    // ---- TargetPlatform / type names / boxing ---------------------------------------------

    TEST(XnaTargetPlatform, ValuesNamesAndSpellingsMatchTheAssembly)
    {
        EXPECT_EQ(static_cast<int>(Xna::TargetPlatform::Windows), 0);
        EXPECT_EQ(static_cast<int>(Xna::TargetPlatform::Xbox360), 1);
        EXPECT_EQ(static_cast<int>(Xna::TargetPlatform::WindowsPhone), 2);
        EXPECT_STREQ(Xna::TargetPlatformName(Xna::TargetPlatform::Xbox360), "Xbox360");
        EXPECT_EQ(Xna::TryParseTargetPlatform("Xbox 360"), Xna::TargetPlatform::Xbox360);
        EXPECT_EQ(Xna::TryParseTargetPlatform("windowsphone"), Xna::TargetPlatform::WindowsPhone);
        EXPECT_EQ(Xna::TryParseTargetPlatform("Windows Phone"), Xna::TargetPlatform::WindowsPhone);
        EXPECT_EQ(Xna::TryParseTargetPlatform("Windows"), Xna::TargetPlatform::Windows);
        EXPECT_FALSE(Xna::TryParseTargetPlatform("Linux").has_value());
        EXPECT_EQ(Canon::ToXnaTargetPlatform(Canon::ContentTargetPlatform::WindowsPhone), Xna::TargetPlatform::WindowsPhone);
        EXPECT_EQ(Canon::FromXnaTargetPlatform(Xna::TargetPlatform::Xbox360), Canon::ContentTargetPlatform::Xbox360);
    }

    TEST(XnaContentTypeName, SpellsPrimitivesFrameworkTypesContainersAndPointers)
    {
        EXPECT_EQ(Xna::ContentTypeName<std::int32_t>::Name(), "System.Int32");
        EXPECT_EQ(Xna::ContentTypeName<std::string>::Name(), "System.String");
        EXPECT_EQ(Xna::ContentTypeName<Vector3>::Name(), "Microsoft.Xna.Framework.Vector3");
        EXPECT_EQ(Xna::ContentTypeName<std::vector<std::string>>::Name(), "System.Collections.Generic.List`1[[System.String]]");
        EXPECT_EQ(Xna::ContentTypeName<std::optional<float>>::Name(), "System.Nullable`1[[System.Single]]");
        EXPECT_EQ((Xna::ContentTypeName<std::map<std::string, std::int32_t>>::Name()),
                  "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]");
        EXPECT_EQ(Xna::ContentTypeName<std::shared_ptr<GreetingContent>>::Name(), "TestGame.Pipeline.GreetingContent");
        static_assert(std::is_same_v<Xna::Carrier<GreetingContent>, std::shared_ptr<GreetingContent>>);
        static_assert(std::is_same_v<Xna::Carrier<Vector3>, Vector3>);
        static_assert(std::is_same_v<Xna::Carrier<std::string>, std::string>);
    }

    TEST(XnaContentObject, BoxUnboxAndHoldsFollowTheTypeNames)
    {
        auto greeting = std::make_shared<GreetingContent>();
        greeting->text = "hi";
        Xna::ContentObject boxed = Xna::Box<GreetingContent>(greeting);
        EXPECT_EQ(boxed.StableType(), "TestGame.Pipeline.GreetingContent");
        EXPECT_TRUE(Xna::Holds<GreetingContent>(boxed));
        EXPECT_FALSE(Xna::Holds<std::string>(boxed));
        EXPECT_EQ(Xna::Unbox<GreetingContent>(boxed).get(), greeting.get());
        Xna::Unbox<GreetingContent>(boxed)->text = "changed";
        EXPECT_EQ(greeting->text, "changed");
        EXPECT_THROW((void)Xna::Unbox<std::string>(boxed), System::InvalidCastException);
        EXPECT_THROW((void)Xna::Unbox<std::string>(Xna::ContentObject{}), System::InvalidCastException);
        EXPECT_EQ(Xna::Unbox<float>(Xna::Box<float>(1.5f)), 1.5f);
    }

    // ---- attributes / logger ----------------------------------------------------------------

    TEST(XnaAttributes, DescriptorsKeepTheAttributeProperties)
    {
        Xna::ContentImporterAttribute one(".png");
        EXPECT_EQ(one.getFileExtensionsProperty(), (std::vector<std::string>{".png"}));
        Xna::ContentImporterAttribute many({".bmp", ".dds"});
        EXPECT_EQ(many.getFileExtensionsProperty().size(), 2u);
        EXPECT_FALSE(many.getCacheImportedDataProperty());
        many.setCacheImportedDataProperty(true);
        many.setDefaultProcessorProperty("TextureProcessor");
        many.setDisplayNameProperty("Texture - XNA Framework");
        EXPECT_TRUE(many.getCacheImportedDataProperty());
        EXPECT_EQ(many.getDefaultProcessorProperty(), "TextureProcessor");
        EXPECT_EQ(many.getDisplayNameProperty(), "Texture - XNA Framework");
        Xna::ContentProcessorAttribute processor;
        EXPECT_TRUE(processor.getDisplayNameProperty().empty());
        processor.setDisplayNameProperty("Model - XNA Framework");
        EXPECT_EQ(processor.getDisplayNameProperty(), "Model - XNA Framework");
        static_assert(std::is_base_of_v<System::Attribute, Xna::ContentImporterAttribute>);
        static_assert(std::is_base_of_v<System::Attribute, Xna::ContentProcessorAttribute>);
    }

    class CollectingLogger final : public Xna::ContentBuildLogger
    {
    public:
        using Xna::ContentBuildLogger::LogImportantMessage;
        using Xna::ContentBuildLogger::LogMessage;
        using Xna::ContentBuildLogger::LogWarning;
        std::vector<std::string> lines;
        void LogImportantMessage(const std::string& message) override { lines.push_back("!" + message); }
        void LogMessage(const std::string& message) override { lines.push_back(message); }
        void LogWarning(const std::string& helpLink, const Xna::ContentIdentity& identity, const std::string& message) override
        {
            lines.push_back("W[" + GetCurrentFilename(identity) + "]" + message + (helpLink.empty() ? "" : "@" + helpLink));
        }
    };

    TEST(XnaContentBuildLogger, FormatsMessagesAndTracksTheFileStackRelativeToTheRoot)
    {
        CollectingLogger logger;
        logger.setLoggerRootDirectoryProperty("/proj/Content");
        logger.PushFile("/proj/Content/Models/hero.fbx");
        logger.LogMessage("{0} bones", 12);
        logger.LogImportantMessage("done {}", "now");
        logger.LogWarning("", Xna::ContentIdentity(), "no material");
        logger.LogWarning("http://help", Xna::ContentIdentity("/proj/Content/Textures/t.png"), "{0} px", 4);
        logger.PopFile();
        logger.LogWarning("", Xna::ContentIdentity(), "outside");
        EXPECT_THROW(logger.PopFile(), std::logic_error);
        ASSERT_EQ(logger.lines.size(), 5u);
        EXPECT_EQ(logger.lines[0], "12 bones");
        EXPECT_EQ(logger.lines[1], "!done now");
        EXPECT_EQ(logger.lines[2], "W[Models/hero.fbx]no material");
        EXPECT_EQ(logger.lines[3], "W[Textures/t.png]4 px@http://help");
        EXPECT_EQ(logger.lines[4], "W[]outside");
    }

    // ---- processor parameters ---------------------------------------------------------------

    TEST(XnaProcessorParameter, TextAndObjectConversionsCoverTheParameterTypes)
    {
        EXPECT_TRUE(Xna::ParseProcessorParameterText<bool>(" True "));
        EXPECT_FALSE(Xna::ParseProcessorParameterText<bool>("false"));
        EXPECT_THROW((void)Xna::ParseProcessorParameterText<bool>("yes"), System::FormatException);
        EXPECT_EQ(Xna::ParseProcessorParameterText<std::int32_t>("-7"), -7);
        EXPECT_THROW((void)Xna::ParseProcessorParameterText<std::int32_t>("7.5"), System::FormatException);
        EXPECT_FLOAT_EQ(Xna::ParseProcessorParameterText<float>("0.4"), 0.4f);
        EXPECT_EQ(Xna::ParseProcessorParameterText<Color>("255, 0, 255, 255"), Color(255, 0, 255, 255));
        EXPECT_EQ(Xna::ParseProcessorParameterText<Color>("{R:1 G:2 B:3 A:4}"), Color(1, 2, 3, 4));
        EXPECT_EQ(Xna::ParseProcessorParameterText<Color>("10,20,30"), Color(10, 20, 30, 255));
        EXPECT_THROW((void)Xna::ParseProcessorParameterText<Color>("1,2"), System::FormatException);
        EXPECT_EQ(Xna::ParseProcessorParameterText<Vector3>("1, 2.5, -3"), Vector3(1.0f, 2.5f, -3.0f));
        EXPECT_EQ(Xna::ParseProcessorParameterText<char16_t>(" "), u' ');
        EXPECT_EQ(Xna::ParseProcessorParameterText<char16_t>("\xC3\xA9"), u'\u00e9');
        EXPECT_THROW((void)Xna::ParseProcessorParameterText<char16_t>("ab"), System::FormatException);
        EXPECT_EQ(Xna::ConvertProcessorParameterObject<float>(Xna::Box<std::int64_t>(3), "Scale"), 3.0f);
        EXPECT_EQ(Xna::ConvertProcessorParameterObject<float>(Xna::Box<std::string>("2.5"), "Scale"), 2.5f);
        EXPECT_EQ(Xna::ConvertProcessorParameterObject<std::int32_t>(Xna::Box<std::int64_t>(5), "N"), 5);
        EXPECT_THROW((void)Xna::ConvertProcessorParameterObject<std::int32_t>(Xna::Box<std::int64_t>(1LL << 40), "N"),
                     System::InvalidCastException);
        EXPECT_THROW((void)Xna::ConvertProcessorParameterObject<bool>(Xna::Box<Vector3>(Vector3()), "B"),
                     System::InvalidCastException);
    }

    TEST(XnaProcessorParameterBindings, DescribeDefaultsAssignAndListParameters)
    {
        auto bindings = Xna::DescribeProcessorParameters<GreetingProcessor>();
        ASSERT_EQ(bindings.Bindings().size(), 4u);
        Xna::ProcessorParameterCollection collection = bindings.ToCollection();
        EXPECT_EQ(collection.getCountProperty(), 4);
        const Xna::ProcessorParameter* tone = collection.Find("Tone");
        ASSERT_NE(tone, nullptr);
        EXPECT_TRUE(tone->getIsEnumProperty());
        EXPECT_EQ(tone->getPossibleEnumValuesProperty().getCountProperty(), 3);
        EXPECT_EQ(tone->getPropertyTypeProperty(), "TestGame.Pipeline.Tone");
        EXPECT_EQ(Xna::Unbox<std::string>(tone->getDefaultValueProperty()), "Plain");
        const Xna::ProcessorParameter* prefix = collection.Find("Prefix");
        ASSERT_NE(prefix, nullptr);
        EXPECT_EQ(prefix->getDescriptionProperty(), "Text placed before the greeting.");
        EXPECT_EQ(prefix->getPropertyTypeProperty(), "System.String");
        EXPECT_EQ(Xna::Unbox<std::string>(prefix->getDefaultValueProperty()), "> ");
        EXPECT_FALSE(prefix->getIsEnumProperty());
        EXPECT_EQ(collection.Find("Repeat")->getDisplayNameProperty(), "Repeat");
        EXPECT_EQ(Xna::Unbox<Color>(collection.Find("Tint")->getDefaultValueProperty()), Color(255, 0, 255, 255));
        EXPECT_EQ(collection.Find("Nope"), nullptr);

        GreetingProcessor processor;
        bindings.Find("Repeat")->assignText(processor, "3");
        bindings.Find("Tone")->assignText(processor, "Loud");
        bindings.Find("Tint")->assignObject(processor, Xna::Box<std::string>("1,2,3,4"));
        EXPECT_EQ(processor.getRepeatProperty(), 3);
        EXPECT_EQ(processor.getToneProperty(), Tone::Loud);
        EXPECT_EQ(processor.getTintProperty(), Color(1, 2, 3, 4));
        EXPECT_EQ(Xna::Unbox<std::string>(bindings.Find("Tone")->read(processor)), "Loud");
        EXPECT_THROW(bindings.Find("Tone")->assignText(processor, "Shout"), System::InvalidCastException);
        EXPECT_THROW(bindings.Find("Repeat")->assignText(processor, "many"), System::FormatException);
    }

    // ---- bridge into the canonical registry ---------------------------------------------------

    TEST(XnaPipelineBridge, RegisteredXnaComponentsImportAndProcessThroughCanonicalContexts)
    {
        const std::filesystem::path root = MakeTempRoot("bridge");
        { std::ofstream(root / "hello.greet") << "hello\n"; }

        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Xna::ContentImporterAttribute attribute(".greet");
        attribute.setDisplayNameProperty("Greeting - TestGame");
        attribute.setDefaultProcessorProperty("GreetingProcessor");
        Canon::RegisterXnaImporter<GreetingImporter>(*registry, "GreetingImporter", attribute, "2", "TestGame.Pipeline");
        Xna::ContentProcessorAttribute processorAttribute;
        processorAttribute.setDisplayNameProperty("Greeting - TestGame");
        Canon::RegisterXnaProcessor<GreetingProcessor>(*registry, "GreetingProcessor", processorAttribute, "1", "TestGame.Pipeline");

        auto importer = registry->ResolveImporter(root / "hello.greet");
        ASSERT_NE(importer, nullptr);
        EXPECT_EQ(importer->Identity().name, "GreetingImporter");
        EXPECT_EQ(importer->Identity().version, "2");
        EXPECT_EQ(importer->SourceExtensions(), (std::vector<std::string>{".greet"}));
        EXPECT_EQ(importer->OutputTypes(), (std::vector<std::string>{"TestGame.Pipeline.GreetingContent"}));
        EXPECT_EQ(importer->DefaultProcessor(), "GreetingProcessor");

        Canon::ContentSourceRootCapabilities roots;
        Canon::ContentDependencyCollector dependencies;
        RecordingLogger logger;
        Canon::ContentBuildEnvironment environment;
        environment.targetPlatform = Canon::ContentTargetPlatform::WindowsPhone;
        environment.buildConfiguration = "Debug";
        environment.outputDirectory = root / "out";
        Canon::ContentImporterContext importContext(root, root / "hello.greet", "hello", "GreetingImporter", roots,
                                                    dependencies, logger, environment);
        Canon::ContentValue imported = importer->Import(importContext);
        ASSERT_TRUE(Xna::Holds<GreetingContent>(imported));
        EXPECT_EQ(Xna::Unbox<GreetingContent>(imported)->text, "hello");
        EXPECT_EQ(Xna::Unbox<GreetingContent>(imported)->getNameProperty(), "hello");
        ASSERT_EQ(logger.messages.size(), 1u);
        EXPECT_EQ(logger.messages[0].text, "imported 5 bytes");
        EXPECT_EQ(logger.messages[0].component, "GreetingImporter");

        auto processor = registry->ResolveProcessor(imported.StableType(), importer->DefaultProcessor());
        ASSERT_NE(processor, nullptr);
        EXPECT_EQ(processor->InputType(), "TestGame.Pipeline.GreetingContent");
        EXPECT_EQ(processor->OutputType(), "System.String");
        Canon::ContentProcessorParameters parameters;
        parameters.Set("Repeat", std::int64_t{2});
        parameters.Set("Tone", std::string("Loud"));
        parameters.Set("Prefix", std::string("* "));
        processor->ValidateParameters(parameters);
        Canon::ContentProcessorParameters bad;
        bad.Set("Volume", std::int64_t{1});
        EXPECT_THROW(processor->ValidateParameters(bad), std::invalid_argument);
        Canon::ContentProcessorParameters badValue;
        badValue.Set("Repeat", std::string("lots"));
        EXPECT_THROW(processor->ValidateParameters(badValue), std::invalid_argument);

        Canon::ContentProcessorContext processContext(root, root / "hello.greet", "hello", "GreetingProcessor", parameters,
                                                      roots, dependencies, logger, Canon::ContentOutputFormat::Xnb,
                                                      environment, nullptr);
        Canon::ContentValue processed = processor->Process(imported, processContext);
        EXPECT_EQ(processed.StableType(), "System.String");
        EXPECT_EQ(Xna::Unbox<std::string>(processed), "* HELLO* HELLO");
        ASSERT_EQ(logger.messages.size(), 2u);
        EXPECT_EQ(logger.messages[1].text, "platform WindowsPhone");
        EXPECT_EQ(logger.messages[1].component, "GreetingProcessor");

        // The XNA context view over the canonical one reports the environment.
        Canon::XnaBridgeProcessorContext view(processContext);
        EXPECT_EQ(view.getBuildConfigurationProperty(), "Debug");
        EXPECT_EQ(view.getTargetPlatformProperty(), Xna::TargetPlatform::WindowsPhone);
        EXPECT_EQ(view.getTargetProfileProperty(), Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach);
        EXPECT_EQ(view.getOutputDirectoryProperty(), (root / "out").string());
        EXPECT_EQ(view.getOutputFilenameProperty(), (root / "out" / "hello.xnb").generic_string());
        EXPECT_EQ(view.getParametersProperty().GetValue<std::int64_t>("Repeat", 0), 2);
        EXPECT_EQ(view.getParametersProperty().GetValue<std::string>("Tone", ""), "Loud");
        EXPECT_TRUE(view.getIntermediateDirectoryProperty().empty());

        // Dependencies declared through the XNA context reach the canonical collector.
        { std::ofstream(root / "extra.txt") << "x"; }
        view.AddDependency("extra.txt");
        bool recorded = false;
        for (const auto& dependency : dependencies.Dependencies())
        {
            if (dependency.identity.find("extra.txt") != std::string::npos) { recorded = true; }
        }
        EXPECT_TRUE(recorded);
        EXPECT_THROW(view.AddDependency("../escape.txt"), std::invalid_argument);

        std::filesystem::remove_all(root);
    }

    TEST(XnaPipelineBridge, ConvertRunsARegisteredProcessorInProcess)
    {
        const std::filesystem::path root = MakeTempRoot("convert");
        { std::ofstream(root / "c.greet") << "c\n"; }
        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Canon::RegisterXnaImporter<GreetingImporter>(*registry, "GreetingImporter", Xna::ContentImporterAttribute(".greet"));
        Canon::RegisterXnaProcessor<GreetingProcessor>(*registry, "GreetingProcessor", Xna::ContentProcessorAttribute{});
        Canon::ContentPipeline pipeline(registry);
        Canon::ContentSourceRootCapabilities roots;
        Canon::ContentDependencyCollector dependencies;
        RecordingLogger logger;
        Canon::ContentProcessorParameters none;
        Canon::ContentProcessorContext canonical(root, root / "c.greet", "c", "Outer", none, roots, dependencies, logger,
                                                 Canon::ContentOutputFormat::Xnb, {}, &pipeline);
        Canon::XnaBridgeProcessorContext context(canonical);
        auto greeting = std::make_shared<GreetingContent>();
        greeting->text = "abc";
        Xna::OpaqueDataDictionary parameters;
        parameters.SetValue<std::string>("Tone", "Loud");
        parameters.SetValue<std::int32_t>("Repeat", 2);
        const std::string converted = context.Convert<GreetingContent, std::string>(greeting, "GreetingProcessor", parameters);
        EXPECT_EQ(converted, "> ABC> ABC");
        EXPECT_THROW(((void)context.Convert<GreetingContent, std::string>(greeting, "NoSuchProcessor")), Xna::PipelineException);
        EXPECT_THROW(((void)context.Convert<GreetingContent, std::string>(greeting, "")), Xna::PipelineException);
        // The wrong output type is refused rather than mis-unboxed.
        EXPECT_THROW(((void)context.Convert<GreetingContent, std::int32_t>(greeting, "GreetingProcessor")), Xna::PipelineException);
        // Outside a coordinator there is nothing to convert with.
        Canon::ContentProcessorContext detached(root, root / "c.greet", "c", "Outer", none, roots, dependencies, logger);
        Canon::XnaBridgeProcessorContext detachedView(detached);
        EXPECT_THROW(((void)detachedView.Convert<GreetingContent, std::string>(greeting, "GreetingProcessor")), Xna::PipelineException);
        std::filesystem::remove_all(root);
    }

    /// A canonical writer for the processed System.String, so a build through the real
    /// coordinator can complete; its bytes are the string itself.
    class StringBytesWriter final : public Canon::ContentTypeWriter
    {
    public:
        [[nodiscard]] Canon::ContentComponentIdentity Identity() const override { return {"TestGame.StringBytesWriter", "1"}; }
        [[nodiscard]] Canon::ContentOutputFormat OutputFormat() const override { return Canon::ContentOutputFormat::Xnb; }
        [[nodiscard]] std::vector<Canon::ContentWriterSchemaIdentity> OutputSchemaIdentities() const override
        {
            Canon::ContentWriterSchemaIdentity identity;
            identity.assetTypeId = 77u;
            identity.assetSchemaVersion = 1u;
            identity.assetTypeName = "System.String";
            identity.codec = {"TestGame.StringBytes", "1"};
            return {identity};
        }
        [[nodiscard]] std::string InputType() const override { return "System.String"; }
        [[nodiscard]] Canon::ContentWriteResult Write(const Canon::ContentValue& input, const std::string&) const override
        {
            Canon::ContentWriteResult result;
            const std::string text = Xna::Unbox<std::string>(input);
            result.bytes.assign(text.begin(), text.end());
            result.assetTypeId = 77u;
            result.assetSchemaVersion = 1u;
            result.assetTypeName = "System.String";
            result.rootReaderName = "TestGame.StringReader";
            return result;
        }
    };

    /// An XNA-shaped processor that builds two nested assets and loads a third in-process.
    class NestingProcessor final : public Xna::ContentProcessor<GreetingContent, std::string>
    {
    public:
        std::string Process(const std::shared_ptr<GreetingContent>& input, Xna::ContentProcessorContext& context) override
        {
            const Xna::ContentIdentity& identity = input->getIdentityProperty();
            Xna::ExternalReference<GreetingContent> first("nested/first.greet", identity);
            Xna::ExternalReference<GreetingContent> second("nested/second.greet", identity);
            Xna::OpaqueDataDictionary loud;
            loud.SetValue<std::string>("Tone", "Loud");
            const Xna::ExternalReference<std::string> a =
                context.BuildAsset<GreetingContent, std::string>(first, "GreetingProcessor");
            const Xna::ExternalReference<std::string> again =
                context.BuildAsset<GreetingContent, std::string>(first, "GreetingProcessor");
            const Xna::ExternalReference<std::string> b =
                context.BuildAsset<GreetingContent, std::string>(second, "GreetingProcessor", loud, "", "Named/Second");
            const std::string loaded =
                context.BuildAndLoadAsset<GreetingContent, std::string>(second, "GreetingProcessor", loud);
            return input->text + "|" + a.getFilenameProperty() + "|" + again.getFilenameProperty() + "|" +
                   b.getFilenameProperty() + "|" + loaded;
        }
    };

    /// Names the same asset twice from different processing, which XNA refuses.
    class CollidingProcessor final : public Xna::ContentProcessor<GreetingContent, std::string>
    {
    public:
        std::string Process(const std::shared_ptr<GreetingContent>& input, Xna::ContentProcessorContext& context) override
        {
            Xna::ExternalReference<GreetingContent> first("nested/first.greet", input->getIdentityProperty());
            Xna::OpaqueDataDictionary loud;
            loud.SetValue<std::string>("Tone", "Loud");
            (void)context.BuildAsset<GreetingContent, std::string>(first, "GreetingProcessor", {}, "", "Same");
            (void)context.BuildAsset<GreetingContent, std::string>(first, "GreetingProcessor", loud, "", "Same");
            return input->text;
        }
    };

    TEST(XnaPipelineBridge, BuildAssetAndBuildAndLoadAssetRunNestedBuildsOnTheCanonicalGraph)
    {
        const std::filesystem::path root = MakeTempRoot("nested");
        std::filesystem::create_directories(root / "nested");
        { std::ofstream(root / "outer.greet") << "outer\n"; }
        { std::ofstream(root / "nested" / "first.greet") << "first\n"; }
        { std::ofstream(root / "nested" / "second.greet") << "second\n"; }
        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Xna::ContentImporterAttribute attribute(".greet");
        Canon::RegisterXnaImporter<GreetingImporter>(*registry, "GreetingImporter", attribute);
        Canon::RegisterXnaProcessor<GreetingProcessor>(*registry, "GreetingProcessor", Xna::ContentProcessorAttribute{});
        Canon::RegisterXnaProcessor<NestingProcessor>(*registry, "NestingProcessor", Xna::ContentProcessorAttribute{});
        Canon::RegisterXnaProcessor<CollidingProcessor>(*registry, "CollidingProcessor", Xna::ContentProcessorAttribute{});
        registry->RegisterWriter(std::make_shared<StringBytesWriter>());
        Canon::ContentPipeline pipeline(registry);

        Canon::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "outer.greet";
        request.logicalName = "outer";
        request.processor = "NestingProcessor";
        request.outputFormat = Canon::ContentOutputFormat::Xnb;
        request.environment.outputDirectory = root / "bin";
        RecordingLogger logger;
        request.logger = &logger;
        const Canon::ContentBuildResult result = pipeline.Build(request);

        const std::string expectedFirst = (root / "bin" / "nested/first.xnb").generic_string();
        const std::string expectedSecond = (root / "bin" / "Named/Second.xnb").generic_string();
        EXPECT_EQ(std::string(result.output.bytes.begin(), result.output.bytes.end()),
                  "outer|" + expectedFirst + "|" + expectedFirst + "|" + expectedSecond + "|> SECOND");
        ASSERT_EQ(result.output.additionalOutputs.size(), 2u);
        EXPECT_EQ(result.output.additionalOutputs[0].logicalName, "nested/first");
        EXPECT_EQ(std::string(result.output.additionalOutputs[0].bytes.begin(), result.output.additionalOutputs[0].bytes.end()), "> first");
        EXPECT_EQ(result.output.additionalOutputs[0].rootReaderName, "TestGame.StringReader");
        EXPECT_EQ(result.output.additionalOutputs[0].assetTypeId, 77u);
        EXPECT_EQ(result.output.additionalOutputs[1].logicalName, "Named/Second");
        EXPECT_EQ(std::string(result.output.additionalOutputs[1].bytes.begin(), result.output.additionalOutputs[1].bytes.end()), "> SECOND");

        // The outer node depends on both nested sources (as source files, not primaries) and refers
        // to both nested assets.
        int primaries = 0;
        bool first = false, second = false;
        for (const auto& dependency : result.dependencies)
        {
            if (dependency.kind == Canon::ContentDependencyKind::PrimarySource) { ++primaries; }
            if (dependency.kind == Canon::ContentDependencyKind::SourceFile && dependency.identity.ends_with("nested/first.greet")) { first = true; }
            if (dependency.kind == Canon::ContentDependencyKind::SourceFile && dependency.identity.ends_with("nested/second.greet")) { second = true; }
        }
        EXPECT_EQ(primaries, 1);
        EXPECT_TRUE(first);
        EXPECT_TRUE(second);
        std::vector<std::string> references;
        for (const auto& reference : result.runtimeReferences) { references.push_back(reference.logicalName); }
        EXPECT_EQ(references, (std::vector<std::string>{"Named/Second", "nested/first"}));

        // A collision under one name is refused as a processing failure of the outer node.
        request.processor = "CollidingProcessor";
        request.logicalName = "colliding";
        try
        {
            (void)pipeline.Build(request);
            FAIL() << "expected the colliding nested asset name to be refused";
        }
        catch (const Canon::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Canon::ContentPipelineStage::Process);
            EXPECT_NE(std::string(error.what()).find("Same"), std::string::npos) << error.what();
        }
        std::filesystem::remove_all(root);
    }

    TEST(XnaPipelineBridge, DefaultProcessorIsHonouredByTheCanonicalBuild)
    {
        const std::filesystem::path root = MakeTempRoot("default");
        { std::ofstream(root / "a.greet") << "a\n"; }
        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Xna::ContentImporterAttribute attribute(".greet");
        attribute.setDefaultProcessorProperty("GreetingProcessor");
        Canon::RegisterXnaImporter<GreetingImporter>(*registry, "GreetingImporter", attribute);
        Canon::RegisterXnaProcessor<GreetingProcessor>(*registry, "GreetingProcessor", Xna::ContentProcessorAttribute{});
        // A second processor for the same input type would make default resolution ambiguous
        // without the importer's DefaultProcessor.
        Canon::RegisterXnaProcessorWithoutAttribute<GreetingProcessor>(*registry, "OtherGreetingProcessor");
        Canon::ContentPipeline pipeline(registry);
        Canon::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "a.greet";
        request.logicalName = "a";
        request.outputFormat = Canon::ContentOutputFormat::Xnb;
        // No writer is registered for System.String on this registry, so the build fails at the
        // writer boundary -- after importer and default-processor selection succeeded.
        try
        {
            (void)pipeline.Build(request);
            FAIL() << "expected the writer stage to refuse";
        }
        catch (const Canon::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Canon::ContentPipelineStage::Selection);
            EXPECT_NE(std::string(error.what()).find("writer"), std::string::npos) << error.what();
        }
        std::filesystem::remove_all(root);
    }

    TEST(XnaPipelineComponentScanner, EnumeratesRegisteredXnaComponentsByCatalog)
    {
        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Xna::ContentImporterAttribute attribute({".greet", ".hello"});
        attribute.setDisplayNameProperty("Greeting - TestGame");
        attribute.setDefaultProcessorProperty("GreetingProcessor");
        Canon::RegisterXnaImporter<GreetingImporter>(*registry, "GreetingImporter", attribute, "1", "TestGame.Pipeline");
        Xna::ContentProcessorAttribute processorAttribute;
        processorAttribute.setDisplayNameProperty("Greeting - TestGame");
        Canon::RegisterXnaProcessor<GreetingProcessor>(*registry, "GreetingProcessor", processorAttribute, "1", "TestGame.Pipeline");
        Canon::RegisterXnaProcessorWithoutAttribute<GreetingProcessor>(*registry, "HiddenProcessor", "1", "TestGame.Other");

        Xna::PipelineComponentScanner scanner(registry);
        EXPECT_TRUE(scanner.Update({}));
        EXPECT_EQ(scanner.getImporterNamesProperty(), (std::vector<std::string>{"GreetingImporter"}));
        EXPECT_EQ(scanner.getProcessorNamesProperty(), (std::vector<std::string>{"GreetingProcessor", "HiddenProcessor"}));
        EXPECT_EQ(scanner.getImporterAttributesProperty().at("GreetingImporter").getDefaultProcessorProperty(), "GreetingProcessor");
        EXPECT_EQ(scanner.getImporterOutputTypesProperty().at("GreetingImporter"), "TestGame.Pipeline.GreetingContent");
        EXPECT_EQ(scanner.getProcessorAttributesProperty().count("HiddenProcessor"), 0u);
        EXPECT_EQ(scanner.getProcessorAttributesProperty().at("GreetingProcessor").getDisplayNameProperty(), "Greeting - TestGame");
        EXPECT_EQ(scanner.getProcessorInputTypesProperty().at("GreetingProcessor"), "TestGame.Pipeline.GreetingContent");
        EXPECT_EQ(scanner.getProcessorOutputTypesProperty().at("GreetingProcessor"), "System.String");
        EXPECT_EQ(scanner.getProcessorParametersProperty().at("GreetingProcessor").getCountProperty(), 4);
        EXPECT_TRUE(scanner.getErrorsProperty().empty());

        EXPECT_FALSE(scanner.Update({}));
        EXPECT_TRUE(scanner.Update({"TestGame.Pipeline"}));
        EXPECT_EQ(scanner.getProcessorNamesProperty(), (std::vector<std::string>{"GreetingProcessor"}));
        EXPECT_TRUE(scanner.Update({"Nowhere.Assembly"}, {"TestGame.Other"}));
        EXPECT_TRUE(scanner.getImporterNamesProperty().empty());
        ASSERT_EQ(scanner.getErrorsProperty().size(), 1u);
        EXPECT_NE(scanner.getErrorsProperty()[0].find("Nowhere.Assembly"), std::string::npos);

        Xna::PipelineComponentScanner detached;
        EXPECT_FALSE(detached.Update({}));
        EXPECT_TRUE(detached.getImporterNamesProperty().empty());
    }

    // ---- compile parity: the abstract bases can be derived from and used ------------------------

    class TestImporterContext final : public Xna::ContentImporterContext
    {
    public:
        CollectingLogger logger;
        std::vector<std::string> dependencies;
        std::string getIntermediateDirectoryProperty() const override { return "/tmp/obj"; }
        Xna::ContentBuildLogger& getLoggerProperty() const override { return const_cast<CollectingLogger&>(logger); }
        std::string getOutputDirectoryProperty() const override { return "/tmp/bin"; }
        void AddDependency(const std::string& filename) override { dependencies.push_back(filename); }
    };

    TEST(XnaContentImporter, TypedImportAndUntypedInterfaceAgree)
    {
        const std::filesystem::path root = MakeTempRoot("importer");
        { std::ofstream(root / "x.greet") << "typed\n"; }
        GreetingImporter importer;
        TestImporterContext context;
        std::shared_ptr<GreetingContent> typed = importer.Import((root / "x.greet").string(), context);
        EXPECT_EQ(typed->text, "typed");
        Xna::IContentImporter& untyped = importer;
        Xna::ContentObject boxed = untyped.Import((root / "x.greet").string(), context);
        EXPECT_TRUE(Xna::Holds<GreetingContent>(boxed));
        EXPECT_EQ(context.logger.lines.size(), 2u);
        std::filesystem::remove_all(root);
    }
}
