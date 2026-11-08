// SPDX-License-Identifier: MS-PL
//
// XNAPP-230: XmlImporter -- the intermediate serializer reached
// through the importer -- against what the genuine XNA 4.0 importer answers for the same
// documents (tests/reference/xna40/intermediate/manifest.json, cases importer_*, whose inputs are
// published beside it as `<case>.importer.xml`).
//
// What the measurements settle: the importer builds whatever the document's Asset Type names, adds
// no dependency to its context and leaves the identity of a content item null; a file that is not
// there is refused with the runtime's own FileNotFoundException rather than a content one, while a
// malformed document, a missing Asset element, an unknown type and an empty file each carry the
// serializer's own message.
#include <gtest/gtest.h>
#include "CNA/Internal/PathUtf8.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/TestSupport/OracleCorpus.hpp"
#include "CNA/TestSupport/TestScratch.hpp"
#include "System/Xml/XmlReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/XmlImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/FontDescriptionImporter.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
using Xna::InvalidContentException;
using Xna::XmlImporter;

namespace
{
    std::filesystem::path CorpusDirectory()
    {
        const std::filesystem::path relative = "tests/reference/xna40/intermediate";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative / "manifest.json"))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative / "manifest.json"))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    /** @brief Reads the JSON escapes the manifest writes back to their characters. */
    std::string Unescape(const std::string& text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    /** @brief One manifest row: the status and the note the genuine importer recorded. */
    struct Measurement
    {
        std::string status;
        std::string note;
    };

    const std::map<std::string, Measurement>& Manifest()
    {
        static const std::map<std::string, Measurement> cases = []
        {
            std::map<std::string, Measurement> map;
            std::ifstream in(CorpusDirectory() / "manifest.json");
            std::string line;
            while (std::getline(in, line))
            {
                // rootType is read and discarded, as the pattern this replaces did with its
                // non-capturing group: the manifest writes it, this fixture does not use it.
                std::vector<std::string> fields;
                if (CNA::TestSupport::ReadOracleFields(
                        line, {{"case", false}, {"rootType", true}, {"status", false}, {"note", true}},
                        fields))
                {
                    map[fields[0]] = Measurement{fields[2], Unescape(fields[3])};
                }
            }
            return map;
        }();
        return cases;
    }

    Measurement Expected(const std::string& name)
    {
        const auto found = Manifest().find(name);
        return found == Manifest().end() ? Measurement{"<missing case>", name} : found->second;
    }

    /** @brief A context that records what it is told, as the oracle's driver does. */
    class ProbeContext final : public Xna::ContentImporterContext
    {
    public:
        std::vector<std::string> dependencies;
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override
        {
            return const_cast<SilentLogger&>(logger_);
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        void AddDependency(const std::string& filename) override { dependencies.push_back(filename); }

    private:
        class SilentLogger final : public Xna::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&, const Xna::ContentIdentity&, const std::string&) override {}
        };

        SilentLogger logger_;
    };

    /** @brief The measured file of one case. */
    std::string Input(const std::string& name)
    {
        return CNA::Internal::PathToUtf8((CorpusDirectory() / (name + ".importer.xml")));
    }
}

TEST(XnaXmlImporter, ImportsWhateverTheDocumentNames)
{
    XmlImporter importer;
    ProbeContext context;
    const Xna::ContentObject item = importer.Import(Input("importer_content_item"), context);
    const auto bone = Xna::Unbox<std::shared_ptr<Xna::Graphics::BoneContent>>(item);
    ASSERT_NE(bone, nullptr);
    EXPECT_EQ(bone->getNameProperty(), "Bone");
    // The importer records nothing and stamps nothing: no dependency, and the identity of what it
    // built is left null (measured, importer_content_item).
    EXPECT_TRUE(context.dependencies.empty());
    EXPECT_TRUE(bone->getIdentityProperty().IsEmpty());
    EXPECT_EQ(Expected("importer_content_item").note.find("dependencies=0 identity=null"),
              Expected("importer_content_item").note.size() - std::string("dependencies=0 identity=null").size());

    const Xna::ContentObject number = importer.Import(Input("importer_int"), context);
    EXPECT_EQ(Xna::Unbox<std::int32_t>(number), 42);

    const Xna::ContentObject list = importer.Import(Input("importer_string_list"), context);
    const std::vector<std::string> strings = Xna::Unbox<std::vector<std::string>>(list);
    ASSERT_EQ(strings.size(), 2u);
    EXPECT_EQ(strings[0], "a");
    EXPECT_EQ(strings[1], "b");
    EXPECT_TRUE(context.dependencies.empty());
}

TEST(XnaXmlImporter, RefusalsMatchXna)
{
    XmlImporter importer;
    ProbeContext context;
    // A file that is not there is the runtime's own refusal, not a content one.
    EXPECT_EQ(Expected("importer_missing_file").note.substr(0, std::string("FileNotFoundException").size()),
              "FileNotFoundException");
    EXPECT_THROW((void)importer.Import(Input("importer_absent"), context),
                 System::IO::FileNotFoundException);

    const auto message = [&importer, &context](const std::string& name)
    {
        try
        {
            (void)importer.Import(Input(name), context);
            return std::string("accepted");
        }
        catch (const InvalidContentException& error)
        {
            return "InvalidContentException: " + error.getMessageProperty();
        }
    };
    // The serializer's own refusals carry XNA's message word for word.
    for (const std::string& name : {"importer_no_asset", "importer_unknown_type"})
    {
        EXPECT_EQ(message(name), Expected(name).note) << name;
    }
    // The two the XML parser refuses carry XNA's sentence and this parser's own reason, which is a
    // recorded divergence: .NET says "Data at the root level is invalid. Line 1, position 1." and
    // "Root element is missing." where CNA's parser names its own error.
    const std::string sentence = "InvalidContentException: There was an error while deserializing intermediate XML. ";
    for (const std::string& name : {"importer_not_xml", "importer_empty_document"})
    {
        EXPECT_EQ(message(name).substr(0, sentence.size()), sentence) << name;
        EXPECT_EQ(Expected(name).note.substr(0, sentence.size()), sentence) << name;
    }
    EXPECT_TRUE(context.dependencies.empty());
}

TEST(XnaXmlImporter, XmlAndFontRefusalsCarrySourceFilenameAndNodeLocation)
{
    // Refresh IntermediateReader.CreateInvalidContentException(Exception,string,object[])
    // IL_0006-0033 chooses relocation LocalPath or Xml.BaseURI; IL_0047-007b formats
    // IXmlLineInfo as "line,column". ReadObjectInternal IL_00ac-00bf selects Type (column20 in the retained input).
    ProbeContext context;
    const auto filename = Input("importer_unknown_type");
    // Refresh's relocation Uri.LocalPath is an absolute native path, not the
    // caller's mixed '/' and '\\' spelling (CreateInvalidContentException IL_0006-0033).
    // Build the oracle with filesystem path syntax, independently of the importer.
    const auto nativePath = CNA::Internal::PathFromUtf8(filename).lexically_normal().make_preferred();
    const auto expectedFilename = CNA::Internal::PathToUtf8(nativePath);
    for (const auto& spelling : {filename, expectedFilename, CNA::Internal::PathToGenericUtf8(nativePath)})
    {
        SCOPED_TRACE(spelling);
        for (bool font : {false, true})
        {
            SCOPED_TRACE(font);
            try
            {
                if (font) (void)Xna::FontDescriptionImporter().Import(spelling, context);
                else (void)XmlImporter().Import(spelling, context);
                FAIL() << "unknown XML type accepted";
            }
            catch (const InvalidContentException& error)
            {
                const auto& identity = error.getContentIdentityProperty();
                EXPECT_EQ(identity.getSourceFilenameProperty(), expectedFilename);
                EXPECT_EQ(identity.getFragmentIdentifierProperty(), "1,20");
                EXPECT_TRUE(identity.getSourceToolProperty().empty());
            }
        }
    }
}

TEST(XnaXmlImporter, ReaderTracksUtf16ColumnsAcrossAttributesTextAndClosingTags)
{
    // .NET XmlTextReaderImpl.cs (local snapshot): node LineInfo :4315 (element), :5418
    // (text), :6217 (PI), and IXmlLineInfo :1906-1921. Columns count UTF-16 units:
    // line2's e-acute is one unit and U+1F600 is two; the closing n is column19.
    using System::Xml::XmlNodeType;
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(
        "<r a=\">\">\r\n  <n b=\"é😀\">é😀</n>\r\n  <!-- x -->\r\n  <![CDATA[y]]><?pi z?>\r\n</r>"));
    EXPECT_TRUE(reader->getBaseURIProperty().empty());
    EXPECT_EQ(reader->getLineNumberProperty(), 0);
    EXPECT_EQ(reader->getLinePositionProperty(), 0);
    ASSERT_TRUE(reader->Read());
    EXPECT_EQ(reader->getLinePositionProperty(), 2);
    ASSERT_TRUE(reader->MoveToFirstAttribute());
    EXPECT_EQ(reader->getLinePositionProperty(), 4);
    reader->MoveToElement(); reader->Read(); reader->MoveToContent();
    EXPECT_EQ(reader->getNameProperty(), "n");
    EXPECT_EQ(reader->getLineNumberProperty(), 2);
    EXPECT_EQ(reader->getLinePositionProperty(), 4);
    ASSERT_TRUE(reader->MoveToFirstAttribute());
    EXPECT_EQ(reader->getLineNumberProperty(), 2);
    EXPECT_EQ(reader->getLinePositionProperty(), 6);
    reader->MoveToElement();
    const struct { XmlNodeType type; int line; int column; } expected[] = {
        {XmlNodeType::Text, 2, 14}, {XmlNodeType::EndElement, 2, 19},
        {XmlNodeType::Comment, 3, 7}, {XmlNodeType::CDATA, 4, 12},
        {XmlNodeType::ProcessingInstruction, 4, 18}, {XmlNodeType::EndElement, 5, 3}};
    for (const auto& node : expected)
    {
        ASSERT_TRUE(reader->Read());
        EXPECT_EQ(reader->getNodeTypeProperty(), node.type);
        EXPECT_EQ(reader->getLineNumberProperty(), node.line);
        EXPECT_EQ(reader->getLinePositionProperty(), node.column);
    }
    reader->Close();
    EXPECT_EQ(reader->getLineNumberProperty(), 0);
    EXPECT_EQ(reader->getLinePositionProperty(), 0);
}

TEST(XnaXmlImporter, FileBaseUriAndSerializerFallbackRetainEscapedSourceIdentity)
{
    // Refresh CreateInvalidContentException IL_0027-0033 uses Xml.BaseURI when there
    // is no relocation path. File URI escaping is derived from the literal space/hash.
    const auto file = CNA::TestSupport::FixtureRoot() / "xml source#identity.xml";
    { std::ofstream out(file); out << "<XnaContent>\n    <Asset Type=\"Cna.NoSuchType\" />\n</XnaContent>"; }
    const auto filename = CNA::Internal::PathToUtf8(file);
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::Create(filename));
    const auto expectedUri = reader->getBaseURIProperty();
    EXPECT_TRUE(expectedUri.starts_with("file://"));
    EXPECT_TRUE(expectedUri.ends_with("/xml%20source%23identity.xml"));
    using Xna::Serialization::Intermediate::IntermediateSerializer;
    try
    {
        (void)IntermediateSerializer::DeserializeObject(*reader,
            IntermediateSerializer::TypeSerializerFor<System::Object>(), "");
        FAIL() << "unknown Type accepted";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_EQ(error.getContentIdentityProperty().getSourceFilenameProperty(), expectedUri);
        EXPECT_EQ(error.getContentIdentityProperty().getFragmentIdentifierProperty(), "2,12");
    }
    ProbeContext context;
    for (bool font : {false, true})
    {
        try
        {
            if (font) (void)Xna::FontDescriptionImporter().Import(filename, context);
            else (void)XmlImporter().Import(filename, context);
            FAIL() << "unknown Type accepted by importer";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getContentIdentityProperty().getSourceFilenameProperty(), filename);
            EXPECT_EQ(error.getContentIdentityProperty().getFragmentIdentifierProperty(), "2,12");
        }
    }
    reader->Close();
    EXPECT_TRUE(reader->getBaseURIProperty().empty());
}

TEST(XnaXmlImporter, MissingTypeRefusalCarriesElementPosition)
{
    // Refresh ReadObjectInternal IL_010b-0133 refuses an untyped object while still
    // on its element. Four spaces followed by '<' place Asset at column6.
    const auto file = CNA::TestSupport::FixtureRoot() / "xml-missing-type.xml";
    { std::ofstream out(file); out << "<XnaContent>\n    <Asset />\n</XnaContent>"; }
    const auto filename = CNA::Internal::PathToUtf8(file);
    ProbeContext context;
    try
    {
        (void)XmlImporter().Import(filename, context);
        FAIL() << "missing Type accepted";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_EQ(error.getMessageProperty(), "XML is missing a \"Type\" attribute.");
        EXPECT_EQ(error.getContentIdentityProperty().getSourceFilenameProperty(), filename);
        EXPECT_EQ(error.getContentIdentityProperty().getFragmentIdentifierProperty(), "2,6");
    }
}
