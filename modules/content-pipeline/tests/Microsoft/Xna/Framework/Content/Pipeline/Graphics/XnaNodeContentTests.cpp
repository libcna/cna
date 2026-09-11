// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-095, 096, 098: the node graph -- NodeContent,
// BoneContent, MeshContent, GeometryContent and their child collections -- together with
// VertexContent, the vertex channels and the indirect position view, against what the genuine
// XNA 4.0 pipeline does with the same inputs (tests/reference/xna40/graphics/graphics-content-oracle.json,
// cases node/*, mesh/*, geometry/* and vertexcontent/*).
//
// The measurements settle what parenting does (a node refuses a second parent, and removing one
// clears it), what AbsoluteTransform composes, that a channel follows its vertices through every
// insertion and removal, and the exact intermediate form of a mesh -- including that a geometry
// batch's material is a shared resource and that each channel carries its element type as an
// attribute.
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

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Graphics::BasicMaterialContent;
using Graphics::BoneContent;
using Graphics::GeometryContent;
using Graphics::MeshContent;
using Graphics::NodeContent;
using Graphics::VertexChannel;
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
        catch (const System::Collections::Generic::KeyNotFoundException& error)
        {
            return Normalize("throws KeyNotFoundException: " + std::string(error.what()));
        }
        catch (const System::InvalidOperationException& error)
        {
            return Normalize("throws InvalidOperationException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return Normalize("throws ArgumentOutOfRangeException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentException& error)
        {
            return Normalize("throws ArgumentException: " + error.getMessageProperty());
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

    std::string Number(float value)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << value;
        return text.str();
    }

    /** @brief The oracle's Describe(Matrix): its translation, which is what the cases vary. */
    std::string Translation(const Matrix& matrix)
    {
        return "(" + Number(matrix.M41) + "," + Number(matrix.M42) + "," + Number(matrix.M43) + ")";
    }

    std::string VectorText(const Vector2& value)
    {
        return "{X:" + Number(value.X) + " Y:" + Number(value.Y) + "}";
    }

    std::string VectorText(const Vector4& value)
    {
        return "{X:" + Number(value.X) + " Y:" + Number(value.Y) + " Z:" + Number(value.Z) + " W:" +
               Number(value.W) + "}";
    }

    std::string DescribeVertices(const Graphics::VertexContent& vertices)
    {
        std::string indices;
        for (const SharpRuntime::intcs index : vertices.getPositionIndicesProperty().Items())
        {
            if (!indices.empty())
            {
                indices += ' ';
            }
            indices += std::to_string(index);
        }
        return "count=" + std::to_string(vertices.getVertexCountProperty()) + " indices=[" + indices + "] channels=" +
               std::to_string(vertices.getChannelsProperty().getCountProperty());
    }

    std::string Positions(const Graphics::IndirectPositionCollection& positions)
    {
        std::string text;
        for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
        {
            if (!text.empty())
            {
                text += ' ';
            }
            text += Number(positions[i].X);
        }
        return "[" + text + "]";
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

    /** @brief A mesh with positions and one batch over them, the shape most cases want. */
    struct Triangle
    {
        std::shared_ptr<MeshContent> mesh = std::make_shared<MeshContent>();
        std::shared_ptr<GeometryContent> geometry = std::make_shared<GeometryContent>();

        explicit Triangle(const std::vector<Vector3>& positions = {Vector3(0, 0, 0), Vector3(1, 0, 0),
                                                                   Vector3(0, 1, 0)})
        {
            for (const Vector3& position : positions)
            {
                mesh->getPositionsProperty().Add(position);
            }
            mesh->getGeometryProperty().Add(geometry);
        }
    };

    /** @brief The oracle's own four-position mesh for the insert/remove case. */
    std::vector<Vector3> Row(SharpRuntime::intcs count)
    {
        std::vector<Vector3> positions;
        for (SharpRuntime::intcs i = 0; i < count; ++i)
        {
            positions.push_back(Vector3(static_cast<float>(i), 0, 0));
        }
        return positions;
    }
}

TEST(XnaNodeContent, OracleIsPresent)
{
    ASSERT_GE(Oracle().size(), 400u) << CorpusFile();
}

TEST(XnaNodeContent, DefaultsMatchXna)
{
    const NodeContent node;
    EXPECT_EQ("transform=" + Translation(node.getTransformProperty()) + " absolute=" +
                  Translation(node.getAbsoluteTransformProperty()) + " children=" +
                  std::to_string(node.getChildrenProperty().getCountProperty()) + " animations=" +
                  std::to_string(node.getAnimationsProperty().getCountProperty()) + " parent=" +
                  (node.getParentProperty() == nullptr ? "null" : "set") + " name=\"" + node.getNameProperty() + "\"",
              Expected("node/defaults"));
    // The measurement above prints the translation only; the transform itself is the identity,
    // which the serialized form pins (node/serialize writes 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1).
    EXPECT_EQ(node.getTransformProperty(), Matrix::getIdentityProperty());
}

TEST(XnaNodeContent, AbsoluteTransformComposesUpTheChain)
{
    auto root = std::make_shared<NodeContent>();
    root->setTransformProperty(Matrix::CreateTranslation(1, 0, 0));
    auto child = std::make_shared<NodeContent>();
    child->setTransformProperty(Matrix::CreateTranslation(0, 2, 0));
    root->getChildrenProperty().Add(child);
    auto grandchild = std::make_shared<NodeContent>();
    grandchild->setTransformProperty(Matrix::CreateTranslation(0, 0, 3));
    child->getChildrenProperty().Add(grandchild);
    EXPECT_EQ(std::string("child_parent=") + (child->getParentProperty() == root.get() ? "True" : "False") +
                  " child_absolute=" + Translation(child->getAbsoluteTransformProperty()) + " grandchild_absolute=" +
                  Translation(grandchild->getAbsoluteTransformProperty()),
              Expected("node/absolute_transform"));
}

TEST(XnaNodeContent, ParentingMatchesXna)
{
    EXPECT_EQ(Result([]
                     {
                         auto first = std::make_shared<NodeContent>();
                         auto second = std::make_shared<NodeContent>();
                         auto child = std::make_shared<NodeContent>();
                         first->getChildrenProperty().Add(child);
                         second->getChildrenProperty().Add(child);
                         return std::string("reparented");
                     }),
              Expected("node/reparent"));

    EXPECT_EQ(Result([]
                     {
                         NodeContent node;
                         node.getChildrenProperty().Add(nullptr);
                         return "count=" + std::to_string(node.getChildrenProperty().getCountProperty());
                     }),
              Expected("node/add_null_child"));

    auto root = std::make_shared<NodeContent>();
    auto child = std::make_shared<NodeContent>();
    root->getChildrenProperty().Add(child);
    root->getChildrenProperty().Remove(child);
    EXPECT_EQ("count=" + std::to_string(root->getChildrenProperty().getCountProperty()) + " parent=" +
                  (child->getParentProperty() == nullptr ? "null" : "set"),
              Expected("node/remove_child"));

    auto cleared = std::make_shared<NodeContent>();
    auto orphan = std::make_shared<NodeContent>();
    cleared->getChildrenProperty().Add(orphan);
    cleared->getChildrenProperty().Clear();
    EXPECT_EQ("count=" + std::to_string(cleared->getChildrenProperty().getCountProperty()) + " parent=" +
                  (orphan->getParentProperty() == nullptr ? "null" : "set"),
              Expected("node/clear_children"));
}

TEST(XnaNodeContent, MeshAndGeometryMatchXna)
{
    const MeshContent mesh;
    EXPECT_EQ("positions=" + std::to_string(mesh.getPositionsProperty().getCountProperty()) + " geometry=" +
                  std::to_string(mesh.getGeometryProperty().getCountProperty()) + " children=" +
                  std::to_string(mesh.getChildrenProperty().getCountProperty()),
              Expected("mesh/defaults"));

    auto parented = std::make_shared<MeshContent>();
    auto geometry = std::make_shared<GeometryContent>();
    parented->getGeometryProperty().Add(geometry);
    const bool wasParented = geometry->getParentProperty() == parented.get();
    parented->getGeometryProperty().Remove(geometry);
    EXPECT_EQ(std::string("parented=") + (wasParented ? "True" : "False") + " after_remove=" +
                  (geometry->getParentProperty() == nullptr ? "null" : "set") + " count=" +
                  std::to_string(parented->getGeometryProperty().getCountProperty()),
              Expected("mesh/geometry_parent"));

    EXPECT_EQ(Result([]
                     {
                         MeshContent owner;
                         owner.getGeometryProperty().Add(nullptr);
                         return "count=" + std::to_string(owner.getGeometryProperty().getCountProperty());
                     }),
              Expected("mesh/geometry_add_null"));

    GeometryContent material;
    material.setMaterialProperty(std::make_shared<BasicMaterialContent>());
    EXPECT_EQ("material=BasicMaterialContent indices=" +
                  std::to_string(material.getIndicesProperty().getCountProperty()),
              Expected("geometry/material"));
}

TEST(XnaVertexContent, DefaultsAndEditsMatchXna)
{
    const GeometryContent geometry;
    EXPECT_EQ(DescribeVertices(geometry.getVerticesProperty()) + " geometry_indices=" +
                  std::to_string(geometry.getIndicesProperty().getCountProperty()) + " material=" +
                  (geometry.getMaterialProperty() == nullptr ? "null" : "set") + " parent=" +
                  (geometry.getParentProperty() == nullptr ? "null" : "set"),
              Expected("vertexcontent/defaults"));

    Triangle added;
    added.geometry->getVerticesProperty().AddRange({0, 1, 2});
    EXPECT_EQ(DescribeVertices(added.geometry->getVerticesProperty()) + " positions=" +
                  Positions(added.geometry->getVerticesProperty().getPositionsProperty()),
              Expected("vertexcontent/add_positions"));

    Triangle edited(Row(4));
    edited.geometry->getVerticesProperty().AddRange({0, 1, 2});
    edited.geometry->getVerticesProperty().Insert(1, 3);
    edited.geometry->getVerticesProperty().RemoveAt(0);
    edited.geometry->getVerticesProperty().InsertRange(0, {2, 2});
    edited.geometry->getVerticesProperty().RemoveRange(0, 1);
    EXPECT_EQ(DescribeVertices(edited.geometry->getVerticesProperty()) + " positions=" +
                  Positions(edited.geometry->getVerticesProperty().getPositionsProperty()),
              Expected("vertexcontent/insert_and_remove"));
}

TEST(XnaVertexContent, ChannelsFollowTheVertices)
{
    Triangle appended(Row(3));
    appended.geometry->getVerticesProperty().AddRange({0, 1});
    auto channel = appended.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        "Custom0", {Vector2(1, 1), Vector2(2, 2)});
    appended.geometry->getVerticesProperty().Add(2);
    std::string values;
    for (const Vector2& value : channel->Items())
    {
        if (!values.empty())
        {
            values += ' ';
        }
        values += VectorText(value);
    }
    EXPECT_EQ("count=" + std::to_string(appended.geometry->getVerticesProperty().getVertexCountProperty()) +
                  " channel=" + std::to_string(channel->getCountProperty()) + " values=" + values,
              Expected("vertexcontent/add_with_channel"));

    Triangle removed(Row(3));
    removed.geometry->getVerticesProperty().AddRange({0, 1, 2});
    auto shrinking = removed.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        "Custom0", {Vector2(1, 1), Vector2(2, 2), Vector2(3, 3)});
    removed.geometry->getVerticesProperty().RemoveAt(1);
    values.clear();
    for (const Vector2& value : shrinking->Items())
    {
        if (!values.empty())
        {
            values += ' ';
        }
        values += VectorText(value);
    }
    EXPECT_EQ("count=" + std::to_string(removed.geometry->getVerticesProperty().getVertexCountProperty()) +
                  " channel=" + std::to_string(shrinking->getCountProperty()) + " values=" + values,
              Expected("vertexcontent/remove_with_channel"));

    Triangle inserted(Row(3));
    inserted.geometry->getVerticesProperty().AddRange({0, 2});
    auto growing = inserted.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        "Custom0", {Vector2(1, 1), Vector2(3, 3)});
    inserted.geometry->getVerticesProperty().Insert(1, 1);
    values.clear();
    for (const Vector2& value : growing->Items())
    {
        if (!values.empty())
        {
            values += ' ';
        }
        values += VectorText(value);
    }
    EXPECT_EQ("count=" + std::to_string(inserted.geometry->getVerticesProperty().getVertexCountProperty()) +
                  " channel=" + std::to_string(growing->getCountProperty()) + " values=" + values,
              Expected("vertexcontent/insert_with_channel"));
}

TEST(XnaVertexContent, ChannelCollectionMatchesXna)
{
    Triangle two(Row(2));
    two.geometry->getVerticesProperty().AddRange({0, 1});
    auto channel = two.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        VertexChannelNames::TextureCoordinate(0), {Vector2(0, 0), Vector2(1, 1)});
    EXPECT_EQ("name=" + channel->getNameProperty() + " count=" + std::to_string(channel->getCountProperty()) +
                  " element=Vector2 first=" + VectorText(channel->At(0)) + " channels=" +
                  std::to_string(two.geometry->getVerticesProperty().getChannelsProperty().getCountProperty()),
              Expected("vertexcontent/channel_add"));

    EXPECT_EQ(Result([]
                     {
                         Triangle one(Row(1));
                         one.geometry->getVerticesProperty().AddRange({0});
                         one.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
                             "Custom0", {Vector2(0, 0), Vector2(1, 1)});
                         return std::string("accepted");
                     }),
              Expected("vertexcontent/channel_add_wrong_count"));

    GeometryContent empty;
    empty.getVerticesProperty().getChannelsProperty().Add<Vector2>("Custom0", {});
    EXPECT_EQ("count=" + std::to_string(empty.getVerticesProperty().getChannelsProperty().getCountProperty()) +
                  " channel_count=" +
                  std::to_string(empty.getVerticesProperty().getChannelsProperty()[0]->getCountProperty()),
              Expected("vertexcontent/channel_add_null_data"));

    EXPECT_EQ(Result([]
                     {
                         GeometryContent duplicate;
                         duplicate.getVerticesProperty().getChannelsProperty().Add<Vector2>("Custom0", {});
                         duplicate.getVerticesProperty().getChannelsProperty().Add<Vector2>("Custom0", {});
                         return "count=" + std::to_string(
                                    duplicate.getVerticesProperty().getChannelsProperty().getCountProperty());
                     }),
              Expected("vertexcontent/channel_add_duplicate"));
}

TEST(XnaVertexContent, ChannelLookupMatchesXna)
{
    GeometryContent geometry;
    auto& channels = geometry.getVerticesProperty().getChannelsProperty();
    channels.Add<Vector2>("Custom0", {});
    EXPECT_EQ(std::string("same=") + (channels["Custom0"] == channels[0] ? "True" : "False") + " contains=" +
                  (channels.Contains("Custom0") ? "True" : "False") + " indexof=" +
                  std::to_string(channels.IndexOf("Custom0")) + " missing=" +
                  (channels.Contains("None") ? "True" : "False") + " indexof_missing=" +
                  std::to_string(channels.IndexOf("None")),
              Expected("vertexcontent/channel_lookup"));

    EXPECT_EQ(Result([]
                     {
                         GeometryContent missing;
                         return missing.getVerticesProperty().getChannelsProperty()["None"]->getNameProperty();
                     }),
              Expected("vertexcontent/channel_lookup_missing"));

    EXPECT_EQ("name=" + channels.Get<Vector2>("Custom0")->getNameProperty() + " element=Vector2",
              Expected("vertexcontent/channel_get_typed"));

    EXPECT_EQ(Result([&channels] { return channels.Get<Vector3>("Custom0")->getNameProperty(); }),
              Expected("vertexcontent/channel_get_wrong_type"));
}

TEST(XnaVertexContent, ChannelConversionMatchesXna)
{
    Triangle one(Row(1));
    one.geometry->getVerticesProperty().AddRange({0});
    auto& channels = one.geometry->getVerticesProperty().getChannelsProperty();
    channels.Add<Vector2>("Custom0", {Vector2(0.25f, 0.5f)});
    auto converted = channels.ConvertChannelContent<Vector4>("Custom0");
    EXPECT_EQ("element=Vector4 value=" + VectorText(converted->At(0)) + " channels=" +
                  std::to_string(channels.getCountProperty()) + " same_name=" + converted->getNameProperty(),
              Expected("vertexcontent/channel_convert"));

    Triangle read(Row(1));
    read.geometry->getVerticesProperty().AddRange({0});
    read.geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>("Custom0", {Vector2(0.25f, 0.5f)});
    std::string values;
    for (const Vector4& value :
         read.geometry->getVerticesProperty().getChannelsProperty()[0]->ReadConvertedContent<Vector4>())
    {
        values += VectorText(value);
    }
    EXPECT_EQ("values=" + values, Expected("vertexcontent/channel_read_converted"));
}

TEST(XnaVertexContent, ChannelEditsMatchXna)
{
    GeometryContent geometry;
    auto& channels = geometry.getVerticesProperty().getChannelsProperty();
    channels.Add<Vector2>("A0", {});
    channels.Add<Vector2>("B0", {});
    const bool removed = channels.Remove("A0");
    const bool missing = channels.Remove("None");
    channels.RemoveAt(0);
    EXPECT_EQ(std::string("removed=") + (removed ? "True" : "False") + " missing=" + (missing ? "True" : "False") +
                  " count=" + std::to_string(channels.getCountProperty()),
              Expected("vertexcontent/channel_remove"));

    GeometryContent ordered;
    auto& inserted = ordered.getVerticesProperty().getChannelsProperty();
    inserted.Add<Vector2>("A0", {});
    inserted.Insert<Vector3>(0, "B0", {});
    std::string names;
    for (const std::shared_ptr<Graphics::VertexChannelBase>& channel : inserted)
    {
        if (!names.empty())
        {
            names += ' ';
        }
        names += channel->getNameProperty();
    }
    inserted.Clear();
    EXPECT_EQ("names=" + names + " clear_then=" + std::to_string(inserted.getCountProperty()),
              Expected("vertexcontent/channel_insert"));
}

TEST(XnaVertexContent, IndirectPositionsReadThroughTheMesh)
{
    Triangle mesh({Vector3(7, 0, 0), Vector3(8, 0, 0)});
    mesh.geometry->getVerticesProperty().AddRange({1, 0, 1});
    const Graphics::IndirectPositionCollection& positions =
        mesh.geometry->getVerticesProperty().getPositionsProperty();
    EXPECT_EQ("count=" + std::to_string(positions.getCountProperty()) + " items=" + Positions(positions) +
                  " contains=" + (positions.Contains(Vector3(7, 0, 0)) ? "True" : "False") + " indexof=" +
                  std::to_string(positions.IndexOf(Vector3(7, 0, 0))) + " missing=" +
                  std::to_string(positions.IndexOf(Vector3(9, 0, 0))),
              Expected("vertexcontent/indirect_positions"));
}

TEST(XnaNodeContent, SerializesAsXnaSerializes)
{
    auto root = std::make_shared<NodeContent>();
    root->setNameProperty("Root");
    root->setTransformProperty(Matrix::CreateTranslation(1, 2, 3));
    auto bone = std::make_shared<BoneContent>();
    bone->setNameProperty("Bone");
    root->getChildrenProperty().Add(bone);
    EXPECT_EQ(Serialize(root), Expected("node/serialize"));

    auto mesh = std::make_shared<MeshContent>();
    mesh->setNameProperty("Mesh");
    mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
    auto geometry = std::make_shared<GeometryContent>();
    mesh->getGeometryProperty().Add(geometry);
    geometry->getVerticesProperty().AddRange({0, 1, 2});
    geometry->getIndicesProperty().AddRange({0, 1, 2});
    geometry->setMaterialProperty(std::make_shared<BasicMaterialContent>());
    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        VertexChannelNames::TextureCoordinate(0), {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)});
    EXPECT_EQ(Serialize(mesh), Expected("mesh/serialize"));
}

TEST(XnaNodeContent, DeserializesAsXnaDeserializes)
{
    const auto root = Deserialize<NodeContent>(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n"
        "  <Asset Type=\"Graphics:NodeContent\">\r\n"
        "    <Name>Root</Name>\r\n"
        "    <Transform>1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1</Transform>\r\n"
        "    <Children>\r\n"
        "      <Child Type=\"Graphics:BoneContent\">\r\n"
        "        <Name>Bone</Name>\r\n"
        "        <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n"
        "      </Child>\r\n"
        "    </Children>\r\n"
        "  </Asset>\r\n"
        "</XnaContent>\r\n");
    ASSERT_NE(root, nullptr);
    const std::shared_ptr<NodeContent>& child =
        static_cast<const System::Collections::ObjectModel::Collection<std::shared_ptr<NodeContent>>&>(
            root->getChildrenProperty())[0];
    EXPECT_EQ("name=\"" + root->getNameProperty() + "\" children=" +
                  std::to_string(root->getChildrenProperty().getCountProperty()) + " child=" +
                  (std::dynamic_pointer_cast<BoneContent>(child) != nullptr ? "BoneContent" : "NodeContent") +
                  " child_parent=" + (child->getParentProperty() == root.get() ? "True" : "False") + " transform=" +
                  Translation(root->getTransformProperty()),
              Expected("node/deserialize_minimal"));

    const auto mesh = Deserialize<MeshContent>(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\" "
        "xmlns:Framework=\"Microsoft.Xna.Framework\">\r\n"
        "  <Asset Type=\"Graphics:MeshContent\">\r\n"
        "    <Name>Mesh</Name>\r\n"
        "    <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n"
        "    <Positions>0 0 0 1 0 0 0 1 0</Positions>\r\n"
        "    <Geometry>\r\n"
        "      <Batch>\r\n"
        "        <Indices>0 1 2</Indices>\r\n"
        "        <Vertices>\r\n"
        "          <PositionIndices>0 1 2</PositionIndices>\r\n"
        "          <Channels>\r\n"
        "            <VertexChannel Name=\"TextureCoordinate0\" ElementType=\"Framework:Vector2\">0 0 1 0 0 1"
        "</VertexChannel>\r\n"
        "          </Channels>\r\n"
        "        </Vertices>\r\n"
        "      </Batch>\r\n"
        "    </Geometry>\r\n"
        "  </Asset>\r\n"
        "</XnaContent>\r\n");
    ASSERT_NE(mesh, nullptr);
    const std::shared_ptr<GeometryContent>& batch =
        static_cast<const System::Collections::ObjectModel::Collection<std::shared_ptr<GeometryContent>>&>(
            mesh->getGeometryProperty())[0];
    EXPECT_EQ("positions=" + std::to_string(mesh->getPositionsProperty().getCountProperty()) + " geometry=" +
                  std::to_string(mesh->getGeometryProperty().getCountProperty()) + " indices=" +
                  std::to_string(batch->getIndicesProperty().getCountProperty()) + " vertices=" +
                  std::to_string(batch->getVerticesProperty().getVertexCountProperty()) + " channels=" +
                  std::to_string(batch->getVerticesProperty().getChannelsProperty().getCountProperty()) +
                  " channel=" + batch->getVerticesProperty().getChannelsProperty()[0]->getNameProperty() +
                  " element=Vector2 parent=" + (batch->getParentProperty() == mesh.get() ? "True" : "False"),
              Expected("mesh/deserialize"));
}

TEST(XnaNodeContent, ToStringIsTheFullTypeName)
{
    EXPECT_EQ(GeometryContent().getVerticesProperty().ToString() + "|" +
                  GeometryContent().getVerticesProperty().getChannelsProperty().ToString() + "|" +
                  GeometryContent().getVerticesProperty().getPositionIndicesProperty().ToString(),
              Expected("vertexcontent/tostring"));
    EXPECT_EQ(BoneContent().ToString(), Expected("node/bone_is_a_node").substr(
                                            Expected("node/bone_is_a_node").find("tostring=") + 9));
}
