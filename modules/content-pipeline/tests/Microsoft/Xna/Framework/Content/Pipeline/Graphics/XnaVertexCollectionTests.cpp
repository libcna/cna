// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-096, 098: VertexChannelNames, BoneWeight,
// BoneWeightCollection, IndexCollection and PositionCollection against what the genuine XNA 4.0
// pipeline does with the same inputs (tests/reference/xna40/graphics/graphics-content-oracle.json,
// cases vertexnames/*, boneweight/*, indexcollection/* and positioncollection/*).
//
// The measurements settle three things worth naming: a bone weight is a value type whose weight
// must lie in [0, 1] and whose name may not be empty; NormalizeWeights sorts largest first, keeps
// the largest ones, then scales them to sum to one, refusing a total of zero with its own message;
// and the blend weights channel is spelled "Weights", not by its VertexElementUsage name.
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

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Graphics::BoneWeight;
using Graphics::BoneWeightCollection;
using Graphics::IndexCollection;
using Graphics::PositionCollection;
using Graphics::VertexChannelNames;
using Intermediate::IntermediateSerializer;

namespace
{
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/graphics/graphics-content-oracle.json";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
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
            if (std::filesystem::exists(dir / relative))
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

    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t parameter = text.find("Parameter name:");
        if (parameter != std::string::npos)
        {
            std::size_t cut = parameter;
            while (cut > 0 && (text[cut - 1] == '\n' || text[cut - 1] == '\r'))
            {
                --cut;
            }
            text = text.substr(0, cut);
        }
        const std::size_t core = text.find(" (Parameter '");
        if (core != std::string::npos)
        {
            const std::size_t end = text.find(')', core);
            text = text.substr(0, core) + (end == std::string::npos ? "" : text.substr(end + 1));
        }
        return text;
    }

    const std::map<std::string, std::string>& Oracle()
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(CorpusFile());
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        return cases;
    }

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return Normalize(body());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return Normalize("throws ArgumentOutOfRangeException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const InvalidContentException& error)
        {
            return Normalize("throws InvalidContentException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    /** @brief .NET's "R" format for a float: the shortest text that reads back exactly. */
    std::string RoundTrip(float value)
    {
        if (std::isnan(value))
        {
            return "NaN";
        }
        // .NET Framework's "R" tries 7 significant digits and falls back to 9, never 8.
        for (const int digits : {7, 9})
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text.precision(digits);
            text << value;
            if (std::stof(text.str()) == value)
            {
                return text.str();
            }
        }
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text.precision(9);
        text << value;
        return text.str();
    }

    std::string Weights(const BoneWeightCollection& weights)
    {
        std::string text = "count=" + std::to_string(weights.getCountProperty()) + " [";
        for (SharpRuntime::intcs i = 0; i < weights.getCountProperty(); ++i)
        {
            if (i > 0)
            {
                text += ' ';
            }
            const BoneWeight& weight =
                static_cast<const System::Collections::ObjectModel::Collection<BoneWeight>&>(weights)[i];
            text += weight.getBoneNameProperty() + "=" + RoundTrip(weight.getWeightProperty());
        }
        return text + "]";
    }

    template<typename T>
    std::string Serialize(const std::shared_ptr<T>& value)
    {
        System::Xml::XmlWriterSettings settings;
        settings.Indent = true;
        settings.NewLineChars = "\r\n";
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        IntermediateSerializer::Serialize<std::shared_ptr<T>>(*writer, value, std::string());
        std::string xml = writer->ToString();
        const std::size_t cut = xml.find("?>");
        if (cut != std::string::npos)
        {
            xml = xml.substr(cut + 2);
        }
        while (!xml.empty() && (xml.front() == '\r' || xml.front() == '\n'))
        {
            xml.erase(xml.begin());
        }
        return xml;
    }

    template<typename T>
    std::shared_ptr<T> Deserialize(const std::string& xml)
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        return IntermediateSerializer::Deserialize<std::shared_ptr<T>>(*reader, std::string());
    }
}

TEST(XnaVertexChannelNames, EncodedNamesMatchXna)
{
    EXPECT_EQ(VertexChannelNames::Normal() + "|" + VertexChannelNames::Normal(1) + "|" +
                  VertexChannelNames::Binormal(0) + "|" + VertexChannelNames::Color(2) + "|" +
                  VertexChannelNames::Tangent(3) + "|" + VertexChannelNames::TextureCoordinate(0) + "|" +
                  VertexChannelNames::Weights() + "|" + VertexChannelNames::Weights(4),
              Expected("vertexnames/standard"));

    EXPECT_EQ(VertexChannelNames::EncodeName(VertexElementUsage::Position, 0) + "|" +
                  VertexChannelNames::EncodeName(VertexElementUsage::TextureCoordinate, 7) + "|" +
                  VertexChannelNames::EncodeName(VertexElementUsage::BlendIndices, 1),
              Expected("vertexnames/encode_usage"));

    EXPECT_EQ(VertexChannelNames::EncodeName("Custom", 0) + "|" + VertexChannelNames::EncodeName("Custom", 12),
              Expected("vertexnames/encode_string"));
}

TEST(XnaVertexChannelNames, RefusalsMatchXna)
{
    EXPECT_EQ(Result([] { return VertexChannelNames::EncodeName(std::string(), 0); }),
              Expected("vertexnames/encode_null"));
    EXPECT_EQ(Result([] { return VertexChannelNames::EncodeName("Custom", -1); }),
              Expected("vertexnames/encode_negative"));
    EXPECT_EQ(Result([] { return VertexChannelNames::DecodeBaseName(std::string()); }),
              Expected("vertexnames/decode_null"));
    EXPECT_EQ(Result([] { return std::to_string(VertexChannelNames::DecodeUsageIndex(std::string())); }),
              Expected("vertexnames/decode_usage_index_null"));
    EXPECT_EQ(Result([]
                     {
                         VertexElementUsage usage = VertexElementUsage::Position;
                         return std::to_string(static_cast<int>(
                             VertexChannelNames::TryDecodeUsage(std::string(), usage)));
                     }),
              Expected("vertexnames/try_decode_null"));
}

TEST(XnaVertexChannelNames, DecodingMatchesXna)
{
    EXPECT_EQ(VertexChannelNames::DecodeBaseName("TextureCoordinate0") + "|" +
                  std::to_string(VertexChannelNames::DecodeUsageIndex("TextureCoordinate0")) + "|" +
                  VertexChannelNames::DecodeBaseName("Custom12") + "|" +
                  std::to_string(VertexChannelNames::DecodeUsageIndex("Custom12")) + "|" +
                  VertexChannelNames::DecodeBaseName("NoDigits") + "|" +
                  std::to_string(VertexChannelNames::DecodeUsageIndex("NoDigits")),
              Expected("vertexnames/decode"));

    VertexElementUsage normal = VertexElementUsage::Position;
    const bool normalOk = VertexChannelNames::TryDecodeUsage("Normal0", normal);
    VertexElementUsage custom = VertexElementUsage::Normal;
    const bool customOk = VertexChannelNames::TryDecodeUsage("Custom0", custom);
    VertexElementUsage bare = VertexElementUsage::Position;
    const bool bareOk = VertexChannelNames::TryDecodeUsage("Normal", bare);
    const auto name = [](VertexElementUsage usage)
    { return VertexChannelNames::DecodeBaseName(VertexChannelNames::EncodeName(usage, 0)); };
    EXPECT_EQ(std::string("normal=") + (normalOk ? "True" : "False") + "," + name(normal) + " custom=" +
                  (customOk ? "True" : "False") + "," + name(custom) + " bare=" + (bareOk ? "True" : "False") + "," +
                  name(bare),
              Expected("vertexnames/try_decode"));
}

TEST(XnaBoneWeight, MembersMatchXna)
{
    const BoneWeight weight("Bone1", 0.25f);
    EXPECT_EQ("name=" + weight.getBoneNameProperty() + " weight=" + RoundTrip(weight.getWeightProperty()) +
                  " tostring=" + weight.ToString(),
              Expected("boneweight/members"));

    const BoneWeight unset;
    EXPECT_EQ("name=" + std::string(unset.getBoneNameProperty().empty() ? "null" : unset.getBoneNameProperty()) +
                  " weight=" + RoundTrip(unset.getWeightProperty()),
              Expected("boneweight/default_value"));
}

TEST(XnaBoneWeight, RefusalsMatchXna)
{
    EXPECT_EQ(Result([] { return BoneWeight(std::string(), 1.0f).getBoneNameProperty(); }),
              Expected("boneweight/null_name"));
    EXPECT_EQ(Result([] { return "name=\"" + BoneWeight(std::string(), 1.0f).getBoneNameProperty() + "\""; }),
              Expected("boneweight/empty_name"));
    EXPECT_EQ(Result([] { return RoundTrip(BoneWeight("Bone1", -1.0f).getWeightProperty()); }),
              Expected("boneweight/negative_weight"));
}

TEST(XnaBoneWeight, WeightRangeMatchesXna)
{
    std::string results;
    for (const float probe : {0.0f, 0.0001f, 0.5f, 1.0f, 1.0001f, 2.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        if (!results.empty())
        {
            results += ' ';
        }
        try
        {
            results += RoundTrip(probe) + "=" + RoundTrip(BoneWeight("A", probe).getWeightProperty());
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            results += RoundTrip(probe) + "=ArgumentOutOfRangeException";
        }
    }
    EXPECT_EQ(results, Expected("boneweight/weight_range"));
}

TEST(XnaBoneWeightCollection, NormalizeWeightsMatchesXna)
{
    BoneWeightCollection normalized;
    normalized.Add(BoneWeight("A", 0.25f));
    normalized.Add(BoneWeight("B", 0.75f));
    normalized.NormalizeWeights();
    EXPECT_EQ(Weights(normalized), Expected("boneweight/collection_normalize"));

    BoneWeightCollection unnormalized;
    unnormalized.Add(BoneWeight("A", 0.5f));
    unnormalized.Add(BoneWeight("B", 0.25f));
    unnormalized.NormalizeWeights();
    EXPECT_EQ(Weights(unnormalized), Expected("boneweight/collection_normalize_unnormalized"));

    BoneWeightCollection capped;
    capped.Add(BoneWeight("A", 0.2f));
    capped.Add(BoneWeight("B", 0.5f));
    capped.Add(BoneWeight("C", 0.3f));
    capped.NormalizeWeights(2);
    EXPECT_EQ(Weights(capped), Expected("boneweight/collection_normalize_max"));

    BoneWeightCollection ties;
    ties.Add(BoneWeight("A", 0.25f));
    ties.Add(BoneWeight("B", 0.25f));
    ties.Add(BoneWeight("C", 0.5f));
    ties.NormalizeWeights(2);
    EXPECT_EQ(Weights(ties), Expected("boneweight/collection_normalize_ties"));

    BoneWeightCollection spare;
    spare.Add(BoneWeight("A", 1.0f));
    spare.NormalizeWeights(4);
    EXPECT_EQ(Weights(spare), Expected("boneweight/collection_normalize_more_than_count"));
}

TEST(XnaBoneWeightCollection, NormalizeRefusalsMatchXna)
{
    EXPECT_EQ(Result([]
                     {
                         BoneWeightCollection weights;
                         weights.NormalizeWeights();
                         return Weights(weights);
                     }),
              Expected("boneweight/collection_normalize_empty"));

    EXPECT_EQ(Result([]
                     {
                         BoneWeightCollection weights;
                         weights.NormalizeWeights();
                         return Weights(weights);
                     }),
              Expected("boneweight/collection_normalize_zero_total"));

    EXPECT_EQ(Result([]
                     {
                         BoneWeightCollection weights;
                         weights.Add(BoneWeight("A", 1.0f));
                         weights.NormalizeWeights(-1);
                         return Weights(weights);
                     }),
              Expected("boneweight/collection_normalize_negative_max"));

    EXPECT_EQ(Result([]
                     {
                         BoneWeightCollection weights;
                         weights.Add(BoneWeight("A", 1.0f));
                         weights.NormalizeWeights(0);
                         return Weights(weights);
                     }),
              Expected("boneweight/collection_normalize_zero_max"));
}

TEST(XnaVertexCollections, IndexAndPositionCollectionsMatchXna)
{
    IndexCollection indices;
    indices.AddRange({3, 1, 2});
    indices.Add(4);
    std::string items;
    for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
    {
        if (!items.empty())
        {
            items += ',';
        }
        items += std::to_string(
            static_cast<const System::Collections::ObjectModel::Collection<SharpRuntime::intcs>&>(indices)[i]);
    }
    EXPECT_EQ("count=" + std::to_string(indices.getCountProperty()) + " items=" + items,
              Expected("indexcollection/addrange"));

    PositionCollection positions;
    positions.Add(Vector3(1, 2, 3));
    positions.Add(Vector3(4, 5, 6));
    const Vector3& first =
        static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(positions)[0];
    std::ostringstream firstText;
    firstText.imbue(std::locale::classic());
    firstText << "{X:" << first.X << " Y:" << first.Y << " Z:" << first.Z << "}";
    EXPECT_EQ("count=" + std::to_string(positions.getCountProperty()) + " first=" + firstText.str() + " contains=" +
                  (positions.Contains(Vector3(4, 5, 6)) ? "True" : "False") + " indexof=" +
                  std::to_string(positions.IndexOf(Vector3(4, 5, 6))),
              Expected("positioncollection/basics"));
}

TEST(XnaVertexCollections, SerializeAsXnaSerializes)
{
    auto indices = std::make_shared<IndexCollection>();
    indices->AddRange({0, 1, 2});
    EXPECT_EQ(Serialize(indices), Expected("indexcollection/serialize"));

    auto positions = std::make_shared<PositionCollection>();
    positions->Add(Vector3(1, 2, 3));
    EXPECT_EQ(Serialize(positions), Expected("positioncollection/serialize"));

    auto weights = std::make_shared<BoneWeightCollection>();
    weights->Add(BoneWeight("A", 0.25f));
    weights->Add(BoneWeight("B", 0.75f));
    EXPECT_EQ(Serialize(weights), Expected("boneweight/serialize"));
}

TEST(XnaVertexCollections, DeserializeAsXnaDeserializes)
{
    const std::shared_ptr<BoneWeightCollection> weights = Deserialize<BoneWeightCollection>(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n"
        "  <Asset Type=\"Graphics:BoneWeightCollection\">\r\n    <Item />\r\n  </Asset>\r\n"
        "</XnaContent>\r\n");
    ASSERT_NE(weights, nullptr);
    EXPECT_EQ(Weights(*weights), Expected("boneweight/deserialize"));
}
