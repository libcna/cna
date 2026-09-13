// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-075: the intermediate serializer under hostile input.
// A malformed document has exactly two permitted outcomes -- a value, or an InvalidContentException
// (an XmlException from the parser for text that is not XML) -- never a crash, a hang or another
// exception type. The mutation pass is deterministic (fixed seed) so a failure reproduces.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/Object.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::Pipeline::ContentObject;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;

namespace
{
    struct Chain : System::Object
    {
        static constexpr std::string_view XnaTypeName = "Cna.Hardening.Chain";
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name(XnaTypeName);
            return name;
        }
        std::shared_ptr<Chain> Next;
        std::int32_t Depth = 0;
        static void DescribeContent(Intermediate::ContentTypeDescriptor<Chain>& d)
        {
            d.Field("Next", &Chain::Next);
            d.Field("Depth", &Chain::Depth);
        }
    };

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
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty(); dir = dir.parent_path())
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

    /** @brief Deserializes as several roots; returns false only for an outcome that is not permitted. */
    bool Survives(const std::string& xml, std::string& why)
    {
        const auto attempt = [&](auto tag)
        {
            using T = decltype(tag);
            try
            {
                std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
                (void)Intermediate::IntermediateSerializer::Deserialize<T>(*reader, std::string());
            }
            catch (const InvalidContentException&)
            {
            }
            catch (const System::Xml::XmlException&)
            {
            }
            catch (const std::exception& error)
            {
                why = std::string(typeid(error).name()) + ": " + error.what();
                return false;
            }
            return true;
        };
        return attempt(ContentObject{}) && attempt(std::vector<std::int32_t>{}) && attempt(std::string{}) &&
               attempt(std::shared_ptr<Chain>{});
    }

    struct Rng
    {
        std::uint64_t state;
        std::uint64_t Next()
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        }
    };
}

TEST(XnaIntermediateSerializerHardening, ADocumentNestedBeyondTheCeilingIsRefusedNotOverflowed)
{
    // tinyxml2 refuses element nesting beyond 500 levels while parsing (XML_ELEMENT_DEPTH_EXCEEDED,
    // an XmlException from XmlReader::CreateFromString), so that is the ceiling a document meets
    // first; the reader's own MaxNestingDepth guard stands behind it for flattened members, which
    // add object depth without XML depth.
    const int levels = 550;
    std::string xml = "<XnaContent><Asset Type=\"Cna.Hardening.Chain\">";
    for (int i = 0; i < levels; ++i)
    {
        xml += "<Next>";
    }
    xml += "<Next Null=\"true\" /><Depth>0</Depth>";
    for (int i = 0; i < levels; ++i)
    {
        xml += "</Next><Depth>0</Depth>";
    }
    xml += "</Asset></XnaContent>";
    try
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        (void)Intermediate::IntermediateSerializer::Deserialize<Chain>(*reader, std::string());
        FAIL() << "expected a refusal";
    }
    catch (const System::Xml::XmlException& error)
    {
        EXPECT_NE(std::string(error.what()).find("nesting is too deep"), std::string::npos) << error.what();
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("nests deeper than"), std::string::npos) << error.getMessageProperty();
    }
}

TEST(XnaIntermediateSerializerHardening, ADocumentWithinTheCeilingStillReads)
{
    const int levels = 400;
    std::string xml = "<XnaContent><Asset Type=\"Cna.Hardening.Chain\">";
    for (int i = 0; i < levels; ++i)
    {
        xml += "<Next>";
    }
    xml += "<Next Null=\"true\" /><Depth>0</Depth>";
    for (int i = 0; i < levels; ++i)
    {
        xml += "</Next><Depth>0</Depth>";
    }
    xml += "</Asset></XnaContent>";
    std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
    auto chain = Intermediate::IntermediateSerializer::Deserialize<Chain>(*reader, std::string());
    int counted = 0;
    for (auto node = chain; node; node = node->Next)
    {
        ++counted;
    }
    EXPECT_EQ(counted, levels + 1);
}

TEST(XnaIntermediateSerializerHardening, ACycleThroughAnUnsharedMemberIsRefusedWhenWriting)
{
    auto a = std::make_shared<Chain>();
    auto b = std::make_shared<Chain>();
    a->Next = b;
    b->Next = a;
    System::Xml::XmlWriterSettings settings;
    std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
    try
    {
        Intermediate::IntermediateSerializer::Serialize<Chain>(*writer, a, std::string());
        FAIL() << "expected a refusal";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("not a shared resource"), std::string::npos) << error.getMessageProperty();
    }
}

TEST(XnaIntermediateSerializerHardening, EntityReferencesAndDoctypesAreRefused)
{
    std::string why;
    EXPECT_TRUE(Survives("<!DOCTYPE x [<!ENTITY e SYSTEM \"file:///etc/passwd\">]><XnaContent><Asset Type=\"string\">&e;</Asset></XnaContent>", why)) << why;
    EXPECT_TRUE(Survives("<XnaContent><Asset Type=\"string\">&undefined;</Asset></XnaContent>", why)) << why;
    EXPECT_TRUE(Survives("", why)) << why;
    EXPECT_TRUE(Survives("<", why)) << why;
    EXPECT_TRUE(Survives(std::string("<XnaContent><Asset Type=\"int\">1\0 2</Asset></XnaContent>", 52), why)) << why;
}

TEST(XnaIntermediateSerializerHardening, SeededMutationsOfTheCorpusNeverCrash)
{
    std::vector<std::string> seeds;
    for (const auto& entry : std::filesystem::directory_iterator(CorpusDirectory()))
    {
        if (entry.path().extension() == ".xml")
        {
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            seeds.push_back(buffer.str());
        }
    }
    ASSERT_GE(seeds.size(), 100u);
    Rng random{0x584E41505035ULL};
    static const char* const tokens[] = {"<Item>", "</Item>", " Null=\"true\"", " Type=\"int\"", "#Resource1", "<Resources>",
                                         "</Resources>", "1e39", "NaN", "0x10", "<", "&", "]]>", "<Child>", "</Child>",
                                         " Type=\"Generic:List[int]\"", "<Data Key=\"k\">1</Data>"};
    int survived = 0;
    for (int i = 0; i < 400; ++i)
    {
        std::string candidate = seeds[random.Next() % seeds.size()];
        const int edits = 1 + static_cast<int>(random.Next() % 4);
        for (int e = 0; e < edits && !candidate.empty(); ++e)
        {
            const std::size_t at = random.Next() % candidate.size();
            switch (random.Next() % 5)
            {
            case 0: candidate[at] = static_cast<char>(random.Next()); break;
            case 1: candidate.erase(at, 1 + random.Next() % 8); break;
            case 2: candidate.resize(at); break;
            case 3: candidate.insert(at, tokens[random.Next() % (sizeof(tokens) / sizeof(tokens[0]))]); break;
            default: candidate.insert(random.Next() % (candidate.size() + 1), candidate.substr(at, random.Next() % 40)); break;
            }
        }
        std::string why;
        if (Survives(candidate, why))
        {
            ++survived;
        }
        else
        {
            ADD_FAILURE() << "mutation " << i << " escaped with " << why << "\n" << candidate.substr(0, 400);
        }
    }
    EXPECT_EQ(survived, 400);
}
