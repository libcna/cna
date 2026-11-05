// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/TestScratch.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentException.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlReaderSettings.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace I = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using System::Xml::XmlReader;

namespace {
constexpr const char* encodingMessage = "Invalid character in the given encoding.";
// XNA Refresh 4.0.30901 IntermediateSerializer.Deserialize<T> IL_0081..00a5,
// resource XmDeserializelException: "There was an error while deserializing intermediate XML. {0}".
constexpr const char* refreshMessage =
    "There was an error while deserializing intermediate XML. Invalid character in the given encoding.";

std::unique_ptr<XmlReader> OpenUtf8(const std::string& input, int route) {
    if (route == 0) return std::unique_ptr<XmlReader>(XmlReader::CreateFromString(input));
    if (route == 1) return std::unique_ptr<XmlReader>(XmlReader::Create(input));
    if (route == 2)
        return std::unique_ptr<XmlReader>(XmlReader::Create(input, System::Xml::XmlReaderSettings{}));
    const auto path = CNA::TestSupport::FixtureRoot() / "gap22-utf8.xml";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(input.data(), static_cast<std::streamsize>(input.size()));
        if (!file.good()) throw std::runtime_error("could not write GAP-22 fixture");
    }
    const auto name = CNA::Internal::PathToUtf8(path);
    if (route == 3) return std::unique_ptr<XmlReader>(XmlReader::Create(name));
    return std::unique_ptr<XmlReader>(XmlReader::Create(name, System::Xml::XmlReaderSettings{}));
}

void ExpectRefreshRefusal(XmlReader& reader) {
    try {
        (void)I::IntermediateSerializer::Deserialize<std::string>(reader, "");
        ADD_FAILURE() << "invalid UTF-8 must be refused during deserialization";
    } catch (const InvalidContentException& error) {
        EXPECT_EQ(error.getMessageProperty(), refreshMessage);
        // Refresh IntermediateReader.CreateInvalidContentException IL_0080..0095
        // retains the original XmlException rather than replacing it with a generic error.
        const auto inner = error.getInnerExceptionProperty();
        ASSERT_TRUE(static_cast<bool>(inner));
        try {
            std::rethrow_exception(inner);
        } catch (const System::Xml::XmlException& xml) {
            EXPECT_EQ(xml.getMessageProperty(), encodingMessage);
        }
    }
    EXPECT_EQ(reader.getReadStateProperty(), System::Xml::ReadState::Error);
    EXPECT_EQ(reader.getNodeTypeProperty(), System::Xml::XmlNodeType::None);
}
}

TEST(Gap22IntermediateUtf8, Seed9949IsRefusedDuringDeserializeWithRefreshMessage) {
    const auto root = CNA::TestSupport::RepositoryRoot();
    ASSERT_FALSE(root.empty());
    std::ifstream file(root / "tests/fuzz/xna-intermediate/seed-9949.minimized.xml", std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string seed{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // Retained MTH-SAFE-007 minimized witness, not a generated expectation.
    ASSERT_EQ(seed, std::string("<XnaContent><Asset>") + "\xD9" + "</Asset></XnaContent>");
    for (int route = 0; route < 5; ++route) {
        SCOPED_TRACE(route);
        std::unique_ptr<XmlReader> reader;
        ASSERT_NO_THROW(reader = OpenUtf8(seed, route));
        ASSERT_NE(reader, nullptr);
        ExpectRefreshRefusal(*reader);
    }
}

TEST(Gap22IntermediateUtf8, MalformedTextAndIgnoredAttributesAreNotAccepted) {
    // Independent UTF-8 validity: lone continuation, truncation, overlong, surrogate, >10FFFF.
    const std::vector<std::string> invalid{
        "\x80", "\xE2\x82", "\xC0\xAF", "\xE0\x80\xAF",
        "\xF0\x80\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"
    };
    for (const auto& bytes : invalid) {
        for (bool attribute : {false, true}) {
            const std::string xml = attribute
                ? "<XnaContent><Asset Note=\"" + bytes + "\">valid</Asset></XnaContent>"
                : "<XnaContent><Asset>" + bytes + "</Asset></XnaContent>";
            for (int route = 0; route < 5; ++route) {
                SCOPED_TRACE(route);
                SCOPED_TRACE(attribute);
                std::unique_ptr<XmlReader> reader;
                ASSERT_NO_THROW(reader = OpenUtf8(xml, route));
                ASSERT_NE(reader, nullptr);
                ExpectRefreshRefusal(*reader);
            }
        }
    }
}

TEST(Gap22IntermediateUtf8, ValidMultibyteTextAndFileBomRetainExactString) {
    // U+0080,07FF,0800,D7FF,E000,FFFD,10000,10FFFF; valid scalar boundaries.
    const std::string expected = "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF"
                                 "\xEE\x80\x80\xEF\xBF\xBD\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    const std::string xml = "<XnaContent><Asset>" + expected + "</Asset></XnaContent>";
    for (int route = 0; route < 5; ++route) {
        for (bool bom : {false, true}) {
            if (bom && route < 3) continue;
            auto reader = OpenUtf8((bom ? std::string("\xEF\xBB\xBF") : std::string()) + xml, route);
            EXPECT_EQ(I::IntermediateSerializer::Deserialize<std::string>(*reader, ""), expected);
        }
    }
    // Expected value is fixed above; a round trip supplements the independent reader assertion.
    std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
    I::IntermediateSerializer::Serialize<std::string>(*writer, expected, "");
    std::unique_ptr<XmlReader> rereader(XmlReader::CreateFromString(writer->ToString()));
    EXPECT_EQ(I::IntermediateSerializer::Deserialize<std::string>(*rereader, ""), expected);
}

TEST(Gap22IntermediateUtf8, SeedPayloadIsAlsoRefusedByTheWriter) {
    // XML character oracle: a lone D9 cannot represent a Unicode scalar; writer rejects it too.
    std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
    writer->WriteStartElement("r");
    EXPECT_THROW(writer->WriteString("\xD9"), System::ArgumentException);
}
