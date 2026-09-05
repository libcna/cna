// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-150 and 151: MeshBuilder and MeshHelper -- building a
// mesh a triangle at a time, and the operations on a finished one: normals, tangent frames,
// skeletons, merging, ordering and whole-scene transforms -- against what the genuine XNA 4.0
// pipeline does with the same meshes (tests/reference/xna40/graphics/graphics-content-oracle.json,
// cases meshbuilder/* and meshhelper/*).
//
// What the measurements settle, none of which the documentation says: a face normal is the
// clockwise one, so a triangle wound counter-clockwise in the XY plane answers -Z; a vertex normal
// is averaged over the faces meeting at its *position*, so the two vertices of a texture seam come
// out with the same normal; overwriting a normal channel moves it to the end of the channel list;
// and the cache optimization simply takes the triangles in reverse, renumbering the vertices in the
// order the reversed list reaches them.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshBuilder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VectorConverter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Graphics::BoneContent;
using Graphics::GeometryContent;
using Microsoft::Xna::Framework::Content::Pipeline::Box;
using Graphics::MeshBuilder;
using Graphics::MeshContent;
using Graphics::MeshHelper;
using Graphics::NodeContent;
using Graphics::VertexChannel;
using Graphics::VertexChannelBase;
using Graphics::VertexChannelNames;

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

    /** @brief The measurement as recorded, for a case that carries more than one refusal. */
    std::string RawExpected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : found->second;
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

    /** @brief .NET's "R" format for a float: the shortest text that reads back exactly. */
    std::string Number(float value)
    {
        if (std::isnan(value))
        {
            return "NaN";
        }
        std::string spelled;
        for (const int digits : {7, 9})
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text.precision(digits);
            text << value;
            spelled = text.str();
            if (std::stof(spelled) == value)
            {
                break;
            }
        }
        const std::size_t exponent = spelled.find('e');
        if (exponent != std::string::npos)
        {
            spelled[exponent] = 'E';
        }
        return spelled;
    }

    std::string VectorText(const Vector3& value)
    {
        return "(" + Number(value.X) + "," + Number(value.Y) + "," + Number(value.Z) + ")";
    }

    std::string VectorText(const Vector2& value) { return "(" + Number(value.X) + "," + Number(value.Y) + ")"; }

    /** @brief One channel entry, as the oracle's ValueText prints it. */
    std::string EntryText(const std::shared_ptr<VertexChannelBase>& channel, SharpRuntime::intcs index)
    {
        if (const auto vector3 = std::dynamic_pointer_cast<VertexChannel<Vector3>>(channel))
        {
            return VectorText(vector3->At(index));
        }
        if (const auto vector2 = std::dynamic_pointer_cast<VertexChannel<Vector2>>(channel))
        {
            return VectorText(vector2->At(index));
        }
        if (const auto single = std::dynamic_pointer_cast<VertexChannel<float>>(channel))
        {
            return Number(single->At(index));
        }
        return "?";
    }

    /** @brief The .NET short name of a channel's element type, as the oracle prints it. */
    std::string ElementTypeName(const std::shared_ptr<VertexChannelBase>& channel)
    {
        const std::string full = Graphics::VectorConverter::VectorTypeName(channel->getElementTypeProperty());
        return full.empty() ? channel->getElementTypeProperty().getNameProperty()
                            : full.substr(full.rfind('.') + 1);
    }

    /** @brief The oracle's DescribeMeshFull, reproduced. */
    std::string DescribeMeshFull(const std::shared_ptr<MeshContent>& mesh)
    {
        if (mesh == nullptr)
        {
            return "null";
        }
        const auto& positions = static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(
            mesh->getPositionsProperty());
        std::string text = "name=" + (mesh->getNameProperty().empty() ? "null" : mesh->getNameProperty()) +
                           " positions=" + std::to_string(positions.getCountProperty()) + "[";
        for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
        {
            text += (i == 0 ? "" : " ") + VectorText(positions[i]);
        }
        const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<GeometryContent>>&>(mesh->getGeometryProperty());
        text += "] geometry=" + std::to_string(batches.getCountProperty());
        for (SharpRuntime::intcs b = 0; b < batches.getCountProperty(); ++b)
        {
            const std::shared_ptr<GeometryContent>& geometry = batches[b];
            const VertexChannel<SharpRuntime::intcs>& indices =
                geometry->getVerticesProperty().getPositionIndicesProperty();
            text += " {vertices=" + std::to_string(geometry->getVerticesProperty().getVertexCountProperty()) +
                    " positionIndices=[";
            for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
            {
                text += (i == 0 ? "" : ",") + std::to_string(indices.At(i));
            }
            text += "] indices=[";
            const auto& triangles = static_cast<const System::Collections::ObjectModel::Collection<
                SharpRuntime::intcs>&>(geometry->getIndicesProperty());
            for (SharpRuntime::intcs i = 0; i < triangles.getCountProperty(); ++i)
            {
                text += (i == 0 ? "" : ",") + std::to_string(triangles[i]);
            }
            text += "] material=" + (geometry->getMaterialProperty() == nullptr
                                         ? std::string("null")
                                         : geometry->getMaterialProperty()->GetTypeName().substr(
                                               geometry->getMaterialProperty()->GetTypeName().rfind('.') + 1));
            text += " opaque=" + std::to_string(geometry->getOpaqueDataProperty().getCountProperty());
            const Graphics::VertexChannelCollection& channels =
                geometry->getVerticesProperty().getChannelsProperty();
            for (SharpRuntime::intcs c = 0; c < channels.getCountProperty(); ++c)
            {
                const std::shared_ptr<VertexChannelBase>& channel = channels[c];
                text += " channel=" + channel->getNameProperty() + ":" + ElementTypeName(channel) + "[";
                for (SharpRuntime::intcs i = 0; i < channel->getCountProperty(); ++i)
                {
                    text += (i == 0 ? "" : " ") + EntryText(channel, i);
                }
                text += "]";
            }
            text += "}";
        }
        return text;
    }

    /** @brief The oracle's own quad: what its MeshBuilder produced, built here by hand. */
    std::shared_ptr<MeshContent> Quad()
    {
        auto mesh = std::make_shared<MeshContent>();
        mesh->setNameProperty("Quad");
        mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(1, 1, 0));
        mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
        auto geometry = std::make_shared<GeometryContent>();
        mesh->getGeometryProperty().Add(geometry);
        geometry->getVerticesProperty().AddRange({0, 1, 2, 3});
        geometry->getIndicesProperty().AddRange({0, 1, 2, 0, 2, 3});
        geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
            VertexChannelNames::Normal(),
            {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)});
        geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
            VertexChannelNames::TextureCoordinate(0),
            {Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)});
        return mesh;
    }

    /** @brief The oracle's tent: two triangles meeting along an edge at a right angle. */
    std::shared_ptr<MeshContent> Tent()
    {
        auto mesh = std::make_shared<MeshContent>();
        mesh->setNameProperty("Tent");
        mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
        mesh->getPositionsProperty().Add(Vector3(0, 0, 1));
        auto geometry = std::make_shared<GeometryContent>();
        mesh->getGeometryProperty().Add(geometry);
        geometry->getVerticesProperty().AddRange({0, 1, 2, 3});
        geometry->getIndicesProperty().AddRange({0, 1, 2, 0, 3, 1});
        return mesh;
    }

    /** @brief The oracle's grid of quads: a mesh big enough for an ordering to be visible. */
    std::shared_ptr<MeshContent> Grid(int side)
    {
        auto mesh = std::make_shared<MeshContent>();
        mesh->setNameProperty("Grid");
        for (int y = 0; y <= side; ++y)
        {
            for (int x = 0; x <= side; ++x)
            {
                mesh->getPositionsProperty().Add(Vector3(static_cast<float>(x), static_cast<float>(y), 0));
            }
        }
        auto geometry = std::make_shared<GeometryContent>();
        mesh->getGeometryProperty().Add(geometry);
        std::vector<SharpRuntime::intcs> vertices;
        std::vector<SharpRuntime::intcs> indices;
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const int a = y * (side + 1) + x;
                const int b = a + 1;
                const int c = a + side + 1;
                const int d = c + 1;
                for (const int corner : {a, b, d, a, d, c})
                {
                    indices.push_back(static_cast<SharpRuntime::intcs>(vertices.size()));
                    vertices.push_back(corner);
                }
            }
        }
        geometry->getVerticesProperty().AddRange(vertices);
        geometry->getIndicesProperty().AddRange(indices);
        return mesh;
    }

    /**
     * @brief Strips every ".NET Parameter name:" tail, which a run of probes in one string carries
     *        more than once and the shared Normalize only cuts at the first.
     */
    std::string StripParameterNames(const std::string& text)
    {
        std::string out = text;
        for (std::size_t at = out.find("Parameter name:"); at != std::string::npos;
             at = out.find("Parameter name:"))
        {
            std::size_t start = at;
            while (start > 0 && (out[start - 1] == '\n' || out[start - 1] == '\r'))
            {
                --start;
            }
            std::size_t end = out.find(' ', at + 16);
            out = out.substr(0, start) + (end == std::string::npos ? "" : out.substr(end));
        }
        for (std::size_t at = out.find(" (Parameter '"); at != std::string::npos;
             at = out.find(" (Parameter '"))
        {
            const std::size_t end = out.find(')', at);
            out = out.substr(0, at) + (end == std::string::npos ? "" : out.substr(end + 1));
        }
        return out;
    }

    /** @brief The measured refusal probe: every case in one string, as the oracle records it. */
    std::string Probe(const std::string& name, const std::function<void()>& body)
    {
        try
        {
            body();
            return name + "=accepted";
        }
        catch (const System::ArgumentNullException& error)
        {
            return name + "=ArgumentNullException:" + Normalize(error.getMessageProperty());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return name + "=ArgumentOutOfRangeException:" + Normalize(error.getMessageProperty());
        }
        catch (const System::InvalidOperationException& error)
        {
            return name + "=InvalidOperationException:" + Normalize(error.getMessageProperty());
        }
        catch (const System::ArgumentException& error)
        {
            return name + "=ArgumentException:" + Normalize(error.getMessageProperty());
        }
        catch (const InvalidContentException& error)
        {
            return name + "=InvalidContentException:" + Normalize(error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return name + "=Exception:" + Normalize(error.getMessageProperty());
        }
    }
}

TEST(XnaMeshHelper, CalculateNormalsMatchesXna)
{
    std::shared_ptr<MeshContent> mesh = Quad();
    const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<GeometryContent>>&>(mesh->getGeometryProperty());
    EXPECT_TRUE(batches[0]->getVerticesProperty().getChannelsProperty().Remove(VertexChannelNames::Normal()));
    MeshHelper::CalculateNormals(mesh, false);
    EXPECT_EQ(DescribeMeshFull(mesh), Expected("meshhelper/calculate_normals"));

    std::shared_ptr<MeshContent> kept = Quad();
    const auto& keptBatches = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<GeometryContent>>&>(kept->getGeometryProperty());
    const std::shared_ptr<VertexChannel<Vector3>> normals =
        keptBatches[0]->getVerticesProperty().getChannelsProperty().Get<Vector3>(VertexChannelNames::Normal());
    for (SharpRuntime::intcs i = 0; i < normals->getCountProperty(); ++i)
    {
        normals->SetAt(i, Vector3(1, 0, 0));
    }
    MeshHelper::CalculateNormals(kept, false);
    const std::string keptText = DescribeMeshFull(kept);
    MeshHelper::CalculateNormals(kept, true);
    EXPECT_EQ("kept=" + keptText + " overwritten=" + DescribeMeshFull(kept),
              Expected("meshhelper/calculate_normals_overwrite"));

    std::shared_ptr<MeshContent> tent = Tent();
    MeshHelper::CalculateNormals(tent, true);
    EXPECT_EQ(DescribeMeshFull(tent), Expected("meshhelper/calculate_normals_tent"));
}

TEST(XnaMeshHelper, ANormalIsAveragedOverThePositionNotTheVertex)
{
    auto mesh = std::make_shared<MeshContent>();
    mesh->setNameProperty("Seam");
    mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
    mesh->getPositionsProperty().Add(Vector3(0, 0, 1));
    auto geometry = std::make_shared<GeometryContent>();
    mesh->getGeometryProperty().Add(geometry);
    geometry->getVerticesProperty().AddRange({0, 1, 2, 0, 3, 1});
    geometry->getIndicesProperty().AddRange({0, 1, 2, 3, 4, 5});
    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        VertexChannelNames::TextureCoordinate(0),
        {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(7, 7), Vector2(8, 8), Vector2(9, 9)});
    MeshHelper::CalculateNormals(mesh, true);
    EXPECT_EQ(DescribeMeshFull(mesh), Expected("meshhelper/calculate_normals_shared_positions"));
}

TEST(XnaMeshHelper, CalculateTangentFramesMatchesXna)
{
    std::shared_ptr<MeshContent> mesh = Quad();
    MeshHelper::CalculateTangentFrames(mesh, VertexChannelNames::TextureCoordinate(0),
                                       VertexChannelNames::Tangent(0), VertexChannelNames::Binormal(0));
    // The frame's own numbers carry the extended-precision intermediates the x86 .NET Framework
    // evaluated the measurement in -- its binormal has a 4.4e-08 X where the cross product answers
    // zero -- so the channel layout is compared as text and the values as numbers.
    const std::string described = DescribeMeshFull(mesh);
    const std::string expected = Expected("meshhelper/calculate_tangent_frames");
    EXPECT_EQ(described.substr(0, described.find(" channel=Tangent0")),
              expected.substr(0, expected.find(" channel=Tangent0")));
    const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<GeometryContent>>&>(mesh->getGeometryProperty());
    const Graphics::VertexChannelCollection& channels = batches[0]->getVerticesProperty().getChannelsProperty();
    ASSERT_TRUE(channels.Contains(VertexChannelNames::Tangent(0)));
    ASSERT_TRUE(channels.Contains(VertexChannelNames::Binormal(0)));
    const std::shared_ptr<VertexChannel<Vector3>> tangents = channels.Get<Vector3>(VertexChannelNames::Tangent(0));
    const std::shared_ptr<VertexChannel<Vector3>> binormals =
        channels.Get<Vector3>(VertexChannelNames::Binormal(0));
    for (SharpRuntime::intcs i = 0; i < tangents->getCountProperty(); ++i)
    {
        EXPECT_NEAR(tangents->At(i).X, 1.0f, 1e-6f);
        EXPECT_NEAR(tangents->At(i).Y, 0.0f, 1e-6f);
        EXPECT_NEAR(tangents->At(i).Z, 0.0f, 1e-6f);
        EXPECT_NEAR(binormals->At(i).X, 0.0f, 1e-6f);
        EXPECT_NEAR(binormals->At(i).Y, 1.0f, 1e-6f);
        EXPECT_NEAR(binormals->At(i).Z, 0.0f, 1e-6f);
    }
}

TEST(XnaMeshHelper, TangentFrameRefusalsMatchXna)
{
    std::string text = Probe("noTexCoords",
                             []
                             {
                                 std::shared_ptr<MeshContent> mesh = Quad();
                                 const auto& batches =
                                     static_cast<const System::Collections::ObjectModel::Collection<
                                         std::shared_ptr<GeometryContent>>&>(mesh->getGeometryProperty());
                                 (void)batches[0]->getVerticesProperty().getChannelsProperty().Remove(
                                     VertexChannelNames::TextureCoordinate(0));
                                 MeshHelper::CalculateTangentFrames(
                                     mesh, VertexChannelNames::TextureCoordinate(0),
                                     VertexChannelNames::Tangent(0), VertexChannelNames::Binormal(0));
                             });
    text += " " + Probe("nullTangentAndBinormal",
                        []
                        {
                            MeshHelper::CalculateTangentFrames(Quad(), VertexChannelNames::TextureCoordinate(0),
                                                               "", "");
                        });
    text += " " + Probe("nullMesh",
                        []
                        {
                            MeshHelper::CalculateTangentFrames(nullptr, VertexChannelNames::TextureCoordinate(0),
                                                               VertexChannelNames::Tangent(0),
                                                               VertexChannelNames::Binormal(0));
                        });
    EXPECT_EQ(text, Expected("meshhelper/calculate_tangent_frames_refusals"));
}

TEST(XnaMeshHelper, FindsAndFlattensASkeleton)
{
    auto root = std::make_shared<NodeContent>();
    root->setNameProperty("Root");
    auto skeleton = std::make_shared<BoneContent>();
    skeleton->setNameProperty("Skeleton");
    auto childA = std::make_shared<BoneContent>();
    childA->setNameProperty("A");
    auto childB = std::make_shared<BoneContent>();
    childB->setNameProperty("B");
    auto grandChild = std::make_shared<BoneContent>();
    grandChild->setNameProperty("A1");
    root->getChildrenProperty().Add(skeleton);
    skeleton->getChildrenProperty().Add(childA);
    skeleton->getChildrenProperty().Add(childB);
    childA->getChildrenProperty().Add(grandChild);
    const std::shared_ptr<BoneContent> found = MeshHelper::FindSkeleton(root);
    std::string order;
    for (const std::shared_ptr<BoneContent>& bone : MeshHelper::FlattenSkeleton(skeleton))
    {
        order += (order.empty() ? "" : ",") + bone->getNameProperty();
    }
    const std::shared_ptr<BoneContent> fromGrandChild = MeshHelper::FindSkeleton(grandChild);
    EXPECT_EQ("found=" + (found == nullptr ? std::string("null") : found->getNameProperty()) + " flattened=[" +
                  order + "] fromGrandChild=" +
                  (fromGrandChild == nullptr ? std::string("null") : fromGrandChild->getNameProperty()) +
                  " fromEmpty=" +
                  (MeshHelper::FindSkeleton(std::make_shared<NodeContent>()) == nullptr ? "null" : "found"),
              Expected("meshhelper/skeleton"));
}

TEST(XnaMeshHelper, MergesDuplicatePositionsAsXnaDoes)
{
    std::shared_ptr<MeshContent> mesh = Quad();
    MeshHelper::MergeDuplicatePositions(mesh, 0.0f);
    const std::string exact = DescribeMeshFull(mesh);
    std::shared_ptr<MeshContent> loose = Quad();
    loose->getPositionsProperty().setItem(1, Vector3(1.001f, 0, 0));
    MeshHelper::MergeDuplicatePositions(loose, 0.01f);
    EXPECT_EQ("exact=" + exact + " loose=" + DescribeMeshFull(loose),
              Expected("meshhelper/merge_duplicate_positions"));

    auto real = std::make_shared<MeshContent>();
    real->setNameProperty("Mesh");
    real->getPositionsProperty().Add(Vector3(0, 0, 0));
    real->getPositionsProperty().Add(Vector3(1, 0, 0));
    real->getPositionsProperty().Add(Vector3(0.0005f, 0, 0));
    real->getPositionsProperty().Add(Vector3(0, 1, 0));
    auto geometry = std::make_shared<GeometryContent>();
    real->getGeometryProperty().Add(geometry);
    geometry->getVerticesProperty().AddRange({0, 1, 2, 3});
    geometry->getIndicesProperty().AddRange({0, 1, 2, 1, 2, 3});
    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        VertexChannelNames::TextureCoordinate(0),
        {Vector2(0, 0), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1)});
    std::shared_ptr<MeshContent> tent = Tent();
    MeshHelper::MergeDuplicatePositions(real, 0.001f);
    const std::string merged = DescribeMeshFull(real);
    MeshHelper::MergeDuplicatePositions(tent, 0.0f);
    EXPECT_EQ("merged=" + merged + " tent=" + DescribeMeshFull(tent),
              Expected("meshhelper/merge_duplicate_positions_real"));
}

TEST(XnaMeshHelper, MergesDuplicateVerticesAsXnaDoes)
{
    std::shared_ptr<MeshContent> mesh = Quad();
    const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<GeometryContent>>&>(mesh->getGeometryProperty());
    MeshHelper::MergeDuplicateVertices(batches[0]);
    const std::string one = DescribeMeshFull(mesh);
    std::shared_ptr<MeshContent> whole = Quad();
    MeshHelper::MergeDuplicateVertices(whole);
    EXPECT_EQ("geometry=" + one + " mesh=" + DescribeMeshFull(whole),
              Expected("meshhelper/merge_duplicate_vertices"));

    auto real = std::make_shared<MeshContent>();
    real->setNameProperty("Mesh");
    real->getPositionsProperty().Add(Vector3(0, 0, 0));
    real->getPositionsProperty().Add(Vector3(1, 0, 0));
    real->getPositionsProperty().Add(Vector3(0, 1, 0));
    auto geometry = std::make_shared<GeometryContent>();
    real->getGeometryProperty().Add(geometry);
    geometry->getVerticesProperty().AddRange({0, 1, 2, 0, 1, 2});
    geometry->getIndicesProperty().AddRange({0, 1, 2, 3, 4, 5});
    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
        VertexChannelNames::TextureCoordinate(0),
        {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(0, 0), Vector2(1, 0), Vector2(9, 9)});
    MeshHelper::MergeDuplicateVertices(geometry);
    EXPECT_EQ(DescribeMeshFull(real), Expected("meshhelper/merge_duplicate_vertices_real"));
}

TEST(XnaMeshHelper, OptimizeForCacheReversesTheTriangles)
{
    std::shared_ptr<MeshContent> quad = Quad();
    MeshHelper::OptimizeForCache(quad);
    EXPECT_EQ(DescribeMeshFull(quad), Expected("meshhelper/optimize_for_cache"));

    std::shared_ptr<MeshContent> grid = Grid(3);
    MeshHelper::OptimizeForCache(grid);
    EXPECT_EQ(DescribeMeshFull(grid), Expected("meshhelper/optimize_for_cache_grid"));

    std::shared_ptr<MeshContent> shuffled = Grid(3);
    const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<GeometryContent>>&>(shuffled->getGeometryProperty());
    const auto& triangles = static_cast<const System::Collections::ObjectModel::Collection<
        SharpRuntime::intcs>&>(batches[0]->getIndicesProperty());
    const std::vector<int> order = {5, 0, 11, 3, 8, 14, 1, 17, 6, 12, 2, 9, 16, 4, 10, 7, 15, 13};
    std::vector<SharpRuntime::intcs> indices;
    for (const int triangle : order)
    {
        for (int i = 0; i < 3; ++i)
        {
            indices.push_back(triangles[triangle * 3 + i]);
        }
    }
    batches[0]->getIndicesProperty().Clear();
    batches[0]->getIndicesProperty().AddRange(indices);
    MeshHelper::OptimizeForCache(shuffled);
    EXPECT_EQ(DescribeMeshFull(shuffled), Expected("meshhelper/optimize_for_cache_shuffled"));
}

TEST(XnaMeshHelper, SwapWindingOrderReversesEachTriangle)
{
    std::shared_ptr<MeshContent> mesh = Quad();
    MeshHelper::SwapWindingOrder(mesh);
    EXPECT_EQ(DescribeMeshFull(mesh), Expected("meshhelper/swap_winding_order"));
}

TEST(XnaMeshHelper, TransformSceneMovesTheGeometryAndReExpressesTheNodes)
{
    auto root = std::make_shared<NodeContent>();
    root->setNameProperty("Root");
    root->setTransformProperty(Matrix::CreateTranslation(1, 0, 0));
    auto bone = std::make_shared<BoneContent>();
    bone->setNameProperty("Bone");
    bone->setTransformProperty(Matrix::CreateTranslation(0, 3, 0));
    root->getChildrenProperty().Add(bone);
    std::shared_ptr<MeshContent> mesh = Quad();
    bone->getChildrenProperty().Add(mesh);
    MeshHelper::TransformScene(root, Matrix::CreateRotationY(
                                         MathHelper::ToRadians(90)) *
                                         Matrix::CreateScale(2));
    const auto matrix = [](const Matrix& m)
    {
        const std::array<float, 16> values = {m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24,
                                              m.M31, m.M32, m.M33, m.M34, m.M41, m.M42, m.M43, m.M44};
        std::string out = "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            out += (i == 0 ? "" : ",") + Number(values[i]);
        }
        return out + "]";
    };
    EXPECT_EQ("root=" + matrix(root->getTransformProperty()) + " bone=" + matrix(bone->getTransformProperty()) +
                  " " + DescribeMeshFull(mesh),
              Expected("meshhelper/transform_scene"));
}

TEST(XnaMeshHelper, NullAndRangeRefusalsMatchXna)
{
    std::string text = Probe("normalsNull", [] { MeshHelper::CalculateNormals(nullptr, true); });
    text += " " + Probe("mergePositionsNull", [] { MeshHelper::MergeDuplicatePositions(nullptr, 0.0f); });
    text += " " + Probe("mergePositionsNegative", [] { MeshHelper::MergeDuplicatePositions(Tent(), -1.0f); });
    text += " " + Probe("mergeVerticesNullGeometry",
                        [] { MeshHelper::MergeDuplicateVertices(std::shared_ptr<GeometryContent>()); });
    text += " " + Probe("mergeVerticesNullMesh",
                        [] { MeshHelper::MergeDuplicateVertices(std::shared_ptr<MeshContent>()); });
    text += " " + Probe("optimizeNull", [] { MeshHelper::OptimizeForCache(nullptr); });
    text += " " + Probe("swapNull", [] { MeshHelper::SwapWindingOrder(nullptr); });
    text += " " + Probe("transformNull", [] { MeshHelper::TransformScene(nullptr, Matrix::getIdentityProperty()); });
    text += " " + Probe("findSkeletonNull", [] { (void)MeshHelper::FindSkeleton(nullptr); });
    text += " " + Probe("flattenNull", [] { (void)MeshHelper::FlattenSkeleton(nullptr); });
    EXPECT_EQ(StripParameterNames(text), StripParameterNames(RawExpected("meshhelper/null_and_range_refusals")));
}


namespace
{
    /** @brief The oracle's own BuiltQuad: the same builder calls in the same order. */
    std::shared_ptr<MeshContent> BuiltQuad(bool merge, bool swap)
    {
        const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Quad");
        builder->setMergeDuplicatePositionsProperty(merge);
        builder->setSwapWindingOrderProperty(swap);
        const SharpRuntime::intcs normals = builder->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
        const SharpRuntime::intcs coords =
            builder->CreateVertexChannel<Vector2>(VertexChannelNames::TextureCoordinate(0));
        const SharpRuntime::intcs a = builder->CreatePosition(0, 0, 0);
        const SharpRuntime::intcs b = builder->CreatePosition(1, 0, 0);
        const SharpRuntime::intcs c = builder->CreatePosition(1, 1, 0);
        const SharpRuntime::intcs d = builder->CreatePosition(0, 1, 0);
        const std::vector<SharpRuntime::intcs> corners = {a, b, c, a, c, d};
        const std::vector<Vector2> uv = {Vector2(0, 0), Vector2(1, 0), Vector2(1, 1),
                                         Vector2(0, 0), Vector2(1, 1), Vector2(0, 1)};
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            builder->SetVertexChannelData(normals, Box<Vector3>(Vector3(0, 0, 1)));
            builder->SetVertexChannelData(coords, Box<Vector2>(uv[i]));
            builder->AddTriangleVertex(corners[i]);
        }
        return builder->FinishMesh();
    }
}

TEST(XnaMeshBuilder, DefaultsMatchXna)
{
    const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Mesh");
    EXPECT_EQ("MergeDuplicatePositions=" +
                  std::string(builder->getMergeDuplicatePositionsProperty() ? "True" : "False") +
                  " MergePositionTolerance=" + Number(builder->getMergePositionToleranceProperty()) +
                  " Name=\"" + builder->getNameProperty() + "\" SwapWindingOrder=" +
                  (builder->getSwapWindingOrderProperty() ? "True" : "False"),
              Expected("meshbuilder/defaults"));
}

TEST(XnaMeshBuilder, BuildsAQuadAsXnaDoes)
{
    EXPECT_EQ(DescribeMeshFull(BuiltQuad(false, false)), Expected("meshbuilder/quad"));
    EXPECT_EQ(DescribeMeshFull(BuiltQuad(true, false)), Expected("meshbuilder/quad_merged"));
    EXPECT_EQ(DescribeMeshFull(BuiltQuad(false, true)), Expected("meshbuilder/quad_swapped"));
}

TEST(XnaMeshBuilder, MergesPositionsAtTheEndNotAtCreation)
{
    const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Mesh");
    builder->setMergeDuplicatePositionsProperty(true);
    builder->setMergePositionToleranceProperty(0.01f);
    const SharpRuntime::intcs a = builder->CreatePosition(Vector3(0, 0, 0));
    const SharpRuntime::intcs b = builder->CreatePosition(Vector3(0, 0, 0));
    const SharpRuntime::intcs c = builder->CreatePosition(Vector3(0.005f, 0, 0));
    const SharpRuntime::intcs d = builder->CreatePosition(Vector3(1, 0, 0));
    builder->AddTriangleVertex(a);
    builder->AddTriangleVertex(d);
    builder->AddTriangleVertex(c);
    EXPECT_EQ("a=" + std::to_string(a) + " b=" + std::to_string(b) + " c=" + std::to_string(c) + " d=" +
                  std::to_string(d) + " " + DescribeMeshFull(builder->FinishMesh()),
              Expected("meshbuilder/duplicate_positions"));
}

TEST(XnaMeshBuilder, CarriesTheMaterialTheOpaqueDataAndTheGeneratedNormals)
{
    const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Mesh");
    auto material = std::make_shared<Graphics::BasicMaterialContent>();
    material->setAlphaProperty(0.5f);
    builder->SetMaterial(material);
    Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary data;
    data.SetValue<SharpRuntime::intcs>("Key", 7);
    builder->SetOpaqueData(&data);
    builder->CreatePosition(0, 0, 0);
    builder->CreatePosition(1, 0, 0);
    builder->CreatePosition(0, 1, 0);
    builder->AddTriangleVertex(0);
    builder->AddTriangleVertex(1);
    builder->AddTriangleVertex(2);
    EXPECT_EQ(DescribeMeshFull(builder->FinishMesh()), Expected("meshbuilder/material_and_opaque_data"));
}

TEST(XnaMeshBuilder, ChannelDataIsCarriedIntoTheCornersThatFollow)
{
    const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Mesh");
    const SharpRuntime::intcs normals = builder->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
    const SharpRuntime::intcs coords =
        builder->CreateVertexChannel<Vector2>(VertexChannelNames::TextureCoordinate(0));
    builder->CreatePosition(0, 0, 0);
    builder->CreatePosition(1, 0, 0);
    builder->CreatePosition(0, 1, 0);
    builder->SetVertexChannelData(normals, Box<Vector3>(Vector3(0, 0, 1)));
    builder->SetVertexChannelData(coords, Box<Vector2>(Vector2(5, 6)));
    builder->AddTriangleVertex(0);
    builder->AddTriangleVertex(1);
    builder->SetVertexChannelData(coords, Box<Vector2>(Vector2(7, 8)));
    builder->AddTriangleVertex(2);
    EXPECT_EQ(DescribeMeshFull(builder->FinishMesh()), Expected("meshbuilder/channel_data_persistence"));
}

TEST(XnaMeshBuilder, FinishingTwiceAnswersTheSameMesh)
{
    const std::shared_ptr<MeshBuilder> builder = MeshBuilder::StartMesh("Mesh");
    builder->CreatePosition(0, 0, 0);
    builder->CreatePosition(1, 0, 0);
    builder->CreatePosition(0, 1, 0);
    builder->AddTriangleVertex(0);
    builder->AddTriangleVertex(1);
    builder->AddTriangleVertex(2);
    const std::shared_ptr<MeshContent> first = builder->FinishMesh();
    const std::shared_ptr<MeshContent> second = builder->FinishMesh();
    EXPECT_EQ("same=" + std::string(first == second ? "True" : "False") + " first=" + DescribeMeshFull(first) +
                  " second=" + DescribeMeshFull(second),
              Expected("meshbuilder/finish_twice"));

    const std::shared_ptr<MeshBuilder> empty = MeshBuilder::StartMesh("Empty");
    empty->CreatePosition(0, 0, 0);
    EXPECT_EQ(DescribeMeshFull(empty->FinishMesh()), Expected("meshbuilder/no_triangles"));
}

TEST(XnaMeshBuilder, RefusalsMatchXna)
{
    std::string text = Probe("nullName", [] { (void)MeshBuilder::StartMesh(""); });
    text += " " + Probe("channelAfterVertex",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            one->CreatePosition(0, 0, 0);
                            one->AddTriangleVertex(0);
                            (void)one->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
                        });
    text += " " + Probe("badVertexIndex",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            one->CreatePosition(0, 0, 0);
                            one->AddTriangleVertex(4);
                        });
    text += " " + Probe("wrongChannelType",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            const SharpRuntime::intcs channel =
                                one->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
                            one->CreatePosition(0, 0, 0);
                            one->SetVertexChannelData(channel, Box<Vector2>(Vector2(1, 2)));
                            one->AddTriangleVertex(0);
                            (void)one->FinishMesh();
                        });
    text += " " + Probe("badChannelIndex",
                        []
                        {
                            MeshBuilder::StartMesh("Mesh")->SetVertexChannelData(
                                3, Box<Vector3>(Vector3(0, 0, 1)));
                        });
    text += " " + Probe("unfinishedTriangle",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            one->CreatePosition(0, 0, 0);
                            one->AddTriangleVertex(0);
                            (void)one->FinishMesh();
                        });
    text += " " + Probe("nullMaterial", [] { MeshBuilder::StartMesh("Mesh")->SetMaterial(nullptr); });
    text += " " + Probe("nullOpaqueData", [] { MeshBuilder::StartMesh("Mesh")->SetOpaqueData(nullptr); });
    EXPECT_EQ(StripParameterNames(text), StripParameterNames(RawExpected("meshbuilder/refusals")));
}

TEST(XnaMeshBuilder, ChannelRefusalsMatchXna)
{
    std::string text = Probe("nullChannelName",
                             [] { (void)MeshBuilder::StartMesh("Mesh")->CreateVertexChannel<Vector3>(""); });
    text += " " + Probe("duplicateChannel",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            (void)one->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
                            (void)one->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
                        });
    text += " " + Probe("intChannel",
                        []
                        {
                            (void)MeshBuilder::StartMesh("Mesh")->CreateVertexChannel<SharpRuntime::intcs>(
                                "Custom0");
                        });
    text += " " + Probe("stringChannel",
                        [] { (void)MeshBuilder::StartMesh("Mesh")->CreateVertexChannel<std::string>("Custom0"); });
    text += " " + Probe("dataBeforePosition",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            const SharpRuntime::intcs channel =
                                one->CreateVertexChannel<Vector3>(VertexChannelNames::Normal());
                            one->SetVertexChannelData(channel, Box<Vector3>(Vector3(0, 0, 1)));
                        });
    text += " " + Probe("finishTwice",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Mesh");
                            one->CreatePosition(0, 0, 0);
                            one->CreatePosition(1, 0, 0);
                            one->CreatePosition(0, 1, 0);
                            one->AddTriangleVertex(0);
                            one->AddTriangleVertex(1);
                            one->AddTriangleVertex(2);
                            (void)one->FinishMesh();
                            (void)one->FinishMesh();
                        });
    text += " " + Probe("nameAfterStart",
                        []
                        {
                            const std::shared_ptr<MeshBuilder> one = MeshBuilder::StartMesh("Given");
                            one->setNameProperty("Renamed");
                            one->CreatePosition(0, 0, 0);
                            one->CreatePosition(1, 0, 0);
                            one->CreatePosition(0, 1, 0);
                            one->AddTriangleVertex(0);
                            one->AddTriangleVertex(1);
                            one->AddTriangleVertex(2);
                            if (one->FinishMesh()->getNameProperty() != "Renamed")
                            {
                                throw System::Exception("name=" + one->FinishMesh()->getNameProperty());
                            }
                        });
    EXPECT_EQ(StripParameterNames(text), StripParameterNames(RawExpected("meshbuilder/channel_refusals")));
}
