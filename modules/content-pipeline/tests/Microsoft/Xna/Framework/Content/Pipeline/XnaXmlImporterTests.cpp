// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-230: XmlImporter -- the intermediate serializer reached
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

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/XmlImporter.hpp"
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
            const std::regex pattern(
                "\\{\"case\": \"([^\"]*)\", \"rootType\": \"(?:[^\"\\\\]|\\\\.)*\", \"status\": \"([^\"]*)\", "
                "\"note\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Measurement{match[2], Unescape(match[3])};
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
        return (CorpusDirectory() / (name + ".importer.xml")).string();
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
