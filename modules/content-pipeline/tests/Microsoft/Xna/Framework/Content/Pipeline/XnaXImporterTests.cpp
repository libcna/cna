// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-220, XNAPP-221: the DirectX `.x` importer, against the
// graph the genuine one answers for the same committed files.
//
// The expectations are tests/reference/xna40/model/model-import-oracle.json, cases `x/*`. The
// comparison is the whole graph -- names, transforms, positions, every vertex channel and its
// values, materials, bone weights and every animation keyframe -- rather than a summary, because
// a modelling importer that gets the shape right and the values wrong is exactly what a summary
// would hide.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VectorConverter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Xna::InvalidContentException;
using Xna::XImporter;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    std::filesystem::path Fixture(const std::string& name)
    {
        return Locate("tests/assets/xna40/model") / name;
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

    std::string Expected(const std::string& name)
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(Locate("tests/reference/xna40/model/model-import-oracle.json"));
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern)) { map[match[1]] = Unescape(match[2]); }
            }
            return map;
        }();
        const auto found = cases.find(name);
        return found == cases.end() ? std::string("<missing case ") + name + ">" : found->second;
    }

    class ImporterContext final : public Xna::ContentImporterContext
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

    /** @brief The oracle's own `0.######` formatting, so the two strings compare verbatim. */
    std::string F(const float value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        std::string text = out.str();
        if (text.find('.') != std::string::npos)
        {
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
        }
        // The oracle prints .NET's "0.######" of a negative zero as "0"; so does this.
        return text == "-0" ? std::string("0") : text;
    }

    std::string Describe(const Matrix& m)
    {
        return "[" + F(m.M11) + " " + F(m.M12) + " " + F(m.M13) + " " + F(m.M14) + " " + F(m.M21) + " " +
               F(m.M22) + " " + F(m.M23) + " " + F(m.M24) + " " + F(m.M31) + " " + F(m.M32) + " " +
               F(m.M33) + " " + F(m.M34) + " " + F(m.M41) + " " + F(m.M42) + " " + F(m.M43) + " " +
               F(m.M44) + "]";
    }

    template<typename T>
    const System::Collections::ObjectModel::Collection<T>& AsCollection(const auto& collection)
    {
        return static_cast<const System::Collections::ObjectModel::Collection<T>&>(collection);
    }

    /** @brief The oracle's own walk over the graph, in the same order and the same words. */
    void Describe(std::string& text, const std::shared_ptr<Graphics::NodeContent>& node,
                  const std::string& path)
    {
        const std::string here = path + "/" +
                                 (node->getNameProperty().empty() ? std::string("<null>")
                                                                  : node->getNameProperty());
        const auto& children =
            AsCollection<std::shared_ptr<Graphics::NodeContent>>(node->getChildrenProperty());
        const std::string type =
            std::dynamic_pointer_cast<Graphics::MeshContent>(node) != nullptr   ? "MeshContent"
            : std::dynamic_pointer_cast<Graphics::BoneContent>(node) != nullptr ? "BoneContent"
                                                                                : "NodeContent";
        text += here + " type=" + type + " transform=" + Describe(node->getTransformProperty()) +
                " absolute=" + Describe(node->getAbsoluteTransformProperty()) +
                " children=" + std::to_string(children.getCountProperty()) +
                " animations=" + std::to_string(node->getAnimationsProperty().getCountProperty()) +
                " opaque=" + std::to_string(node->getOpaqueDataProperty().getCountProperty()) + "\n";
        for (const auto& [name, animation] : node->getAnimationsProperty())
        {
            text += "  animation " + name + " duration=" +
                    std::to_string(animation->getDurationProperty().getTicksProperty()) +
                    " channels=" + std::to_string(animation->getChannelsProperty().getCountProperty()) + "\n";
            for (const auto& [channelName, channel] : animation->getChannelsProperty())
            {
                text += "   channel " + channelName + " keys=" +
                        std::to_string(channel->getCountProperty()) + "\n";
                for (const std::shared_ptr<Graphics::AnimationKeyframe>& key : *channel)
                {
                    text += "    key t=" + std::to_string(key->getTimeProperty().getTicksProperty()) +
                            " " + Describe(key->getTransformProperty()) + "\n";
                }
            }
        }
        if (const auto mesh = std::dynamic_pointer_cast<Graphics::MeshContent>(node); mesh != nullptr)
        {
            const auto& geometry =
                AsCollection<std::shared_ptr<Graphics::GeometryContent>>(mesh->getGeometryProperty());
            text += "  mesh positions=" + std::to_string(mesh->getPositionsProperty().getCountProperty()) +
                    " geometry=" + std::to_string(geometry.getCountProperty()) + "\n";
            for (SharpRuntime::intcs i = 0; i < mesh->getPositionsProperty().getCountProperty(); ++i)
            {
                const Vector3 p = mesh->getPositionsProperty()[i];
                text += "   position " + std::to_string(i) + " (" + F(p.X) + "," + F(p.Y) + "," + F(p.Z) + ")\n";
            }
            for (SharpRuntime::intcs g = 0; g < geometry.getCountProperty(); ++g)
            {
                const std::shared_ptr<Graphics::GeometryContent>& batch = geometry[g];
                const auto& channels = batch->getVerticesProperty().getChannelsProperty();
                text += "   geometry name=" +
                        (batch->getNameProperty().empty() ? std::string("<null>") : batch->getNameProperty()) +
                        " indices=" + std::to_string(batch->getIndicesProperty().getCountProperty()) +
                        " vertices=" + std::to_string(batch->getVerticesProperty().getVertexCountProperty()) +
                        " channels=" + std::to_string(channels.getCountProperty()) + " material=" +
                        (batch->getMaterialProperty() == nullptr
                             ? std::string("null")
                             : std::string("BasicMaterialContent:") +
                                   (batch->getMaterialProperty()->getNameProperty().empty()
                                        ? "<null>"
                                        : batch->getMaterialProperty()->getNameProperty())) + "\n";
                std::string indices;
                for (SharpRuntime::intcs i = 0; i < batch->getIndicesProperty().getCountProperty(); ++i)
                {
                    indices += (indices.empty() ? "" : ",") +
                               std::to_string(batch->getIndicesProperty()[i]);
                }
                text += "    indices " + indices + "\n";
                std::string positionIndices;
                const auto& mapped = batch->getVerticesProperty().getPositionIndicesProperty();
                for (SharpRuntime::intcs i = 0; i < mapped.getCountProperty(); ++i)
                {
                    positionIndices += (positionIndices.empty() ? "" : ",") +
                                       std::to_string(Xna::Unbox<SharpRuntime::intcs>(mapped[i]));
                }
                text += "    positionIndices " + positionIndices + "\n";
                for (SharpRuntime::intcs c = 0; c < channels.getCountProperty(); ++c)
                {
                    const std::shared_ptr<Graphics::VertexChannelBase>& channel = channels[c];
                    const std::string full = Graphics::VectorConverter::VectorTypeName(
                        channel->getElementTypeProperty());
                    std::string elementType =
                        full.empty() ? channel->getElementTypeProperty().getNameProperty()
                                     : full.substr(full.rfind('.') + 1);
                    std::string values;
                    for (SharpRuntime::intcs i = 0; i < channel->getCountProperty(); ++i)
                    {
                        const Xna::ContentObject value = (*channel)[i];
                        std::string one;
                        if (Xna::Holds<Vector3>(value))
                        {
                            const Vector3 v = Xna::Unbox<Vector3>(value);
                            one = "(" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + ")";
                        }
                        else if (Xna::Holds<Vector2>(value))
                        {
                            const Vector2 v = Xna::Unbox<Vector2>(value);
                            one = "(" + F(v.X) + "," + F(v.Y) + ")";
                        }
                        else if (Xna::Holds<Vector4>(value))
                        {
                            const Vector4 v = Xna::Unbox<Vector4>(value);
                            one = "(" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + "," + F(v.W) + ")";
                        }
                        else if (Xna::Holds<Graphics::BoneWeightCollection>(value))
                        {
                            elementType = "BoneWeightCollection";
                            const Graphics::BoneWeightCollection weights =
                                Xna::Unbox<Graphics::BoneWeightCollection>(value);
                            std::string parts;
                            for (SharpRuntime::intcs w = 0; w < weights.getCountProperty(); ++w)
                            {
                                parts += (parts.empty() ? "" : ",") +
                                         weights[w].getBoneNameProperty() + ":" +
                                         F(weights[w].getWeightProperty());
                            }
                            one = "{" + parts + "}";
                        }
                        values += (values.empty() ? "" : " ") + one;
                    }
                    text += "    channel " + channel->getNameProperty() + " type=" + elementType + " " +
                            values + "\n";
                }
                if (batch->getMaterialProperty() != nullptr)
                {
                    for (const auto& [key, value] : batch->getMaterialProperty()->getOpaqueDataProperty())
                    {
                        std::string one;
                        if (Xna::Holds<Vector3>(value))
                        {
                            const Vector3 v = Xna::Unbox<Vector3>(value);
                            one = "(" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + ")";
                        }
                        else if (Xna::Holds<float>(value))
                        {
                            one = F(Xna::Unbox<float>(value));
                        }
                        text += "    materialData " + key + "=" + one + "\n";
                    }
                    for (const auto& [key, reference] : batch->getMaterialProperty()->getTexturesProperty())
                    {
                        // Relative to the fixture directory, as the oracle prints it: a `Texture`
                        // names its file twice and the two can name different *directories*, so
                        // the file name alone would not say which one was taken
                        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-140`).
                        const std::filesystem::path referenced =
                            std::filesystem::path(reference->getFilenameProperty()).lexically_normal();
                        const std::filesystem::path relative =
                            referenced.lexically_relative(Fixture("").lexically_normal());
                        text += "    materialTexture " + key + "=" +
                                (relative.empty() || relative.begin()->string() == ".."
                                     ? referenced.filename().string()
                                     : relative.generic_string()) +
                                "\n";
                    }
                }
            }
        }
        for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
        {
            Describe(text, children[i], here);
        }
    }

    /**
     * @brief The same text with each node's animation blocks in name order.
     *
     * XNA's AnimationContentDictionary is a .NET Dictionary and enumerates in its own hash order,
     * which is not a behaviour to reproduce -- CNA's is a std::map and enumerates by name. Sorting
     * both sides is what makes the comparison about the animations rather than about two hash
     * tables.
     */
    std::string SortAnimations(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) { lines.push_back(line); }
        std::string out;
        for (std::size_t i = 0; i < lines.size();)
        {
            if (lines[i].rfind("  animation ", 0) != 0)
            {
                out += lines[i] + "\n";
                ++i;
                continue;
            }
            // Gather the run of animation blocks and sort them by their first line.
            std::vector<std::string> blocks;
            while (i < lines.size() && lines[i].rfind("  animation ", 0) == 0)
            {
                std::string header = lines[i] + "\n";
                ++i;
                // A channel and the keys under it move together; the channels themselves are a
                // .NET Dictionary on XNA's side and a std::map on CNA's, so they are sorted too.
                std::vector<std::string> channels;
                while (i < lines.size() && lines[i].rfind("   ", 0) == 0 &&
                       lines[i].rfind("  animation ", 0) != 0)
                {
                    std::string channel = lines[i] + "\n";
                    ++i;
                    while (i < lines.size() && lines[i].rfind("    ", 0) == 0)
                    {
                        channel += lines[i] + "\n";
                        ++i;
                    }
                    channels.push_back(std::move(channel));
                }
                std::sort(channels.begin(), channels.end());
                for (const std::string& channel : channels) { header += channel; }
                blocks.push_back(std::move(header));
            }
            std::sort(blocks.begin(), blocks.end());
            for (const std::string& block : blocks) { out += block; }
        }
        return out;
    }

    /**
     * @brief Compares two descriptions, holding every number to a tolerance and the rest exactly.
     *
     * A keyframe's matrix comes out of float trigonometry on both sides -- XNA's own answer for a
     * ninety-degree rotation carries -0.000001 where the exact value is zero -- so comparing the
     * printed digits would be comparing two libraries' rounding rather than the importer.
     */
    void ExpectSame(const std::string& actual, const std::string& expected, const std::string& what)
    {
        const std::regex number("-?[0-9]+(?:\\.[0-9]+)?(?:[eE]-?[0-9]+)?");
        const auto split = [&number](const std::string& text)
        {
            std::vector<std::string> pieces;
            std::sregex_token_iterator it(text.begin(), text.end(), number, {-1, 0});
            for (; it != std::sregex_token_iterator(); ++it) { pieces.push_back(*it); }
            return pieces;
        };
        const std::vector<std::string> left = split(actual);
        const std::vector<std::string> right = split(expected);
        if (left.size() != right.size())
        {
            EXPECT_EQ(actual, expected) << what;
            return;
        }
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (left[i] == right[i]) { continue; }
            char* end = nullptr;
            const double a = std::strtod(left[i].c_str(), &end);
            const bool aNumber = end != nullptr && *end == '\0' && !left[i].empty();
            const double b = std::strtod(right[i].c_str(), &end);
            const bool bNumber = end != nullptr && *end == '\0' && !right[i].empty();
            if (aNumber && bNumber && std::abs(a - b) <= 1e-4)
            {
                continue;
            }
            EXPECT_EQ(actual, expected) << what;
            return;
        }
    }

    /** @brief The whole graph an import answers, in the oracle's own words. */
    std::string Import(const std::string& fixture, ImporterContext& context)
    {
        XImporter importer;
        const std::shared_ptr<Graphics::NodeContent> root =
            importer.Import(Fixture(fixture).string(), context);
        std::string text;
        Describe(text, root, "");
        std::string dependencies;
        for (const std::string& one : context.dependencies)
        {
            dependencies += (dependencies.empty() ? "" : ",") +
                            std::filesystem::path(one).filename().string();
        }
        return text + "dependencies=[" + dependencies + "] log=[]";
    }
}

TEST(XnaXImporter, TheAttributeMatchesXna)
{
    EXPECT_EQ(Expected("attribute/x"),
              "extensions=[.x] displayName=X File - XNA Framework defaultProcessor=ModelProcessor "
              "cacheImportedData=True");
    EXPECT_EQ(XImporter::Attribute().getFileExtensionsProperty(), std::vector<std::string>{".x"});
    EXPECT_EQ(XImporter::Attribute().getDisplayNameProperty(), "X File - XNA Framework");
    EXPECT_EQ(XImporter::Attribute().getDefaultProcessorProperty(), "ModelProcessor");
    EXPECT_TRUE(XImporter::Attribute().getCacheImportedDataProperty());
}

// Every readable file in the corpus, graph for graph.
TEST(XnaXImporter, EveryFileAnswersTheGraphXnaAnswers)
{
    for (const std::string& fixture :
         {"bare_mesh.x", "binary_mesh.x", "generated_normals.x", "hierarchy.x", "missing_texture.x",
          // `MeshNormals` carries its own face list, so a corner's normal is not its position's:
          // this quad's two triangles name one normal each and the genuine importer answers six
          // vertices for four positions (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-144`).
          "normal_per_face.x",
          "oblique_normals.x", "quad_textured.x", "transform_z.x", "two_materials.x",
          "with_templates.x", "zero_power.x"})
    {
        ImporterContext context;
        ExpectSame(SortAnimations(Import(fixture, context)),
                   SortAnimations(Expected("x/" + fixture)), fixture);
    }
}

// The skinning and animation files, which carry everything the simple ones do not.
TEST(XnaXImporter, SkinningAndAnimationAnswerWhatXnaAnswers)
{
    for (const std::string& fixture :
         {"anim_default_rate.x", "two_animations.x", "two_bones_animated.x",
          "skinned_two_animations.x", "skinned_animated.x"})
    {
        ImporterContext context;
        ExpectSame(SortAnimations(Import(fixture, context)),
                   SortAnimations(Expected("x/" + fixture)), fixture);
    }
}

// Every refusal, including the D3DX code the genuine reader appends and which one it picks.
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-122: `Name` is a nullable string in XNA and the two
// empty values reach a built `.xnb` differently -- a null object against a zero-length string. The
// `.x` importer has both: a file with no enclosing frame gets a synthesized root that carries no
// name, while `Frame {` declares one that happens to be empty. Measured on the genuine importer,
// which answers `/<null>` for `bare_mesh.x`'s root and `/glass/` -- a path ending in nothing -- for
// SAMPLE-028 `Car.x`'s unnamed frames.
TEST(XnaXImporter, AnUnnamedFrameAndAnAbsentOneAreDifferentNames)
{
    ImporterContext context;
    Xna::XImporter importer;
    const std::shared_ptr<Graphics::NodeContent> root =
        importer.Import(Fixture("bare_mesh.x").string(), context);
    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(root->getNameIsNullEXT()) << "the synthesized root carries no name";
    EXPECT_TRUE(root->getNameProperty().empty());

    ASSERT_EQ(root->getChildrenProperty().getCountProperty(), 1);
    const std::shared_ptr<Graphics::NodeContent> mesh = root->getChildrenProperty()[0];
    EXPECT_FALSE(mesh->getNameIsNullEXT());
    EXPECT_EQ(mesh->getNameProperty(), "Loose");
}

TEST(XnaXImporter, RefusalsMatchXna)
{
    for (const std::string& fixture :
         {"empty.x", "not_x.x", "bad_version.x", "truncated.x", "index_out_of_range.x"})
    {
        ImporterContext context;
        const std::string record = Expected("x/" + fixture);
        ASSERT_EQ(record.rfind("throws InvalidContentException: ", 0), 0u) << fixture;
        const std::string message = record.substr(std::string("throws InvalidContentException: ").size());
        XImporter importer;
        try
        {
            (void)importer.Import(Fixture(fixture).string(), context);
            ADD_FAILURE() << fixture << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), message) << fixture;
        }
    }
}

TEST(XnaXImporter, DisposeIsIdempotentAndAMissingFileIsTheRuntimesOwnRefusal)
{
    EXPECT_EQ(Expected("x/dispose_twice"), "accepted");
    XImporter importer;
    importer.Dispose();
    importer.Dispose();

    ImporterContext context;
    XImporter another;
    EXPECT_THROW((void)another.Import(Fixture("no_such_model.x").string(), context),
                 System::IO::FileNotFoundException);
}

// ---- XNAPP-216: the FBX importer -------------------------------------------------------------
//
// The same corpus discipline as the .x side, and the same oracle file. What FBX and .x differ on
// is measured, not inferred: FBX is right-handed so nothing is converted, the winding IS reversed,
// a texture coordinate's V is flipped, the channel order differs, and a colour is not quantized.

namespace
{
    /** @brief The whole graph an FBX import answers, in the oracle's own words. */
    std::string ImportFbx(const std::string& fixture, ImporterContext& context)
    {
        Xna::FbxImporter importer;
        const std::shared_ptr<Graphics::NodeContent> root =
            importer.Import(Fixture(fixture).string(), context);
        std::string text;
        Describe(text, root, "");
        std::string dependencies;
        for (const std::string& one : context.dependencies)
        {
            dependencies += (dependencies.empty() ? "" : ",") +
                            std::filesystem::path(one).filename().string();
        }
        return text + "dependencies=[" + dependencies + "] log=[]";
    }

    /**
     * @brief The same text with each triangle rotated to start at its lowest corner.
     *
     * A triangle is a cycle: (2,1,0), (1,0,2) and (0,2,1) are the same face wound the same way,
     * and XNA's FBX SDK picks its own starting corner when it triangulates a polygon -- a quad
     * answers (2,1,0) then (0,3,2) where the fan would give (3,2,0). Rotating both sides is what
     * makes the comparison about the winding and the vertices rather than about a triangulator's
     * bookkeeping.
     */
    std::string NormalizeTriangles(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) { lines.push_back(line); }
        std::string out;
        for (std::string& one : lines)
        {
            const std::string prefix = "    indices ";
            if (one.rfind(prefix, 0) != 0)
            {
                out += one + "\n";
                continue;
            }
            std::vector<int> indices;
            std::istringstream values(one.substr(prefix.size()));
            std::string value;
            while (std::getline(values, value, ',')) { indices.push_back(std::stoi(value)); }
            std::string rebuilt;
            for (std::size_t i = 0; i + 2 < indices.size() + 1 && i + 3 <= indices.size(); i += 3)
            {
                std::array<int, 3> triangle{indices[i], indices[i + 1], indices[i + 2]};
                const std::size_t lowest = static_cast<std::size_t>(
                    std::min_element(triangle.begin(), triangle.end()) - triangle.begin());
                for (std::size_t c = 0; c < 3; ++c)
                {
                    rebuilt += (rebuilt.empty() ? "" : ",") +
                               std::to_string(triangle[(lowest + c) % 3]);
                }
            }
            out += prefix + rebuilt + "\n";
        }
        return out;
    }
}

TEST(XnaFbxImporter, TheAttributeMatchesXna)
{
    EXPECT_EQ(Expected("attribute/fbx"),
              "extensions=[.fbx] displayName=Autodesk FBX - XNA Framework "
              "defaultProcessor=ModelProcessor cacheImportedData=True");
    EXPECT_EQ(Xna::FbxImporter::Attribute().getFileExtensionsProperty(),
              std::vector<std::string>{".fbx"});
    EXPECT_EQ(Xna::FbxImporter::Attribute().getDisplayNameProperty(), "Autodesk FBX - XNA Framework");
    EXPECT_EQ(Xna::FbxImporter::Attribute().getDefaultProcessorProperty(), "ModelProcessor");
    EXPECT_TRUE(Xna::FbxImporter::Attribute().getCacheImportedDataProperty());
}

TEST(XnaFbxImporter, EveryFileAnswersTheGraphXnaAnswers)
{
    for (const std::string& fixture :
         {"fbx_bare_mesh.fbx", "fbx_cameras.fbx", "fbx_hierarchy.fbx",
          "fbx_material_factor_texture.fbx", "fbx_material_legacy.fbx",
          "fbx_oblique.fbx", "fbx_prerotation_units.fbx",
          "fbx_quad_polygon.fbx", "fbx_quad_textured.fbx", "fbx_split_vertices.fbx",
          // A `Texture` names its file twice and the two can name different directories; the
          // reference XNA writes is whichever one resolves. These two separate the branches:
          // the first's `FileName` names a directory that is there and its `RelativeFilename`
          // one that is not, the second's the other way round
          // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-140`).
          "fbx_texture_path_filename.fbx", "fbx_texture_path_relative.fbx",
          // Which texture a batch gets is its polygons' own `TextureId`, and this one's first
          // polygon carries -1: the genuine importer answers a material with no texture at all,
          // where taking the first *non-negative* id would have given it one
          // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-141`).
          "fbx_texture_second_batch.fbx",
          // A UV set's channel index is its `Layer` block's own number, and two sets in one block
          // take consecutive indices: the first of these declares its only UV in `Layer: 1` and
          // answers `TextureCoordinate1`, the second declares a `LayerElementUV` and a
          // `LayerElementReflectionUV` in `Layer: 0` and answers both indices
          // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-143`).
          "fbx_uv_layer_one.fbx", "fbx_uv_two_sets.fbx",
          "fbx_two_materials.fbx"})
    {
        ImporterContext context;
        ExpectSame(NormalizeTriangles(SortAnimations(ImportFbx(fixture, context))),
                   NormalizeTriangles(SortAnimations(Expected("fbx/" + fixture))), fixture);
    }
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-121: the oracle's own record prints a transform
// rounded, so `EveryFileAnswersTheGraphXnaAnswers` above cannot tell -4.4e-08 from 6.1e-17 -- both
// print as `0`. This is the one that can. A quarter turn is where a float and a double `cos` part
// company, and XNA's `Cube.xnb` (SAMPLE-003, `PreRotation -90` under `UnitScaleFactor 2.54`)
// carries 2.54 * 6.123233995736766e-17 = 1.555301383669155e-16 on the two entries the turn zeroes.
// A float rotation puts -1.11e-07 there instead, seven orders of magnitude away, and every model
// standing on a Z-up exporter's -90 carried it.
TEST(XnaFbxImporter, AQuarterTurnIsComposedTheWayXnasIsAndNotInFloat)
{
    ImporterContext context;
    Xna::FbxImporter importer;
    const std::shared_ptr<Graphics::NodeContent> node =
        importer.Import(Fixture("fbx_prerotation_units.fbx").string(), context);
    ASSERT_NE(node, nullptr);
    const Matrix transform = node->getTransformProperty();

    // The scale is exact in both, and it is what the near-zero entries carry.
    EXPECT_FLOAT_EQ(transform.M11, 2.54f);
    // cos(pi/2) in double, times that scale. Written as the literal XNA's own file holds.
    EXPECT_EQ(transform.M22, 1.555301383669155e-16f);
    EXPECT_EQ(transform.M33, 1.555301383669155e-16f);
    // And not what a float `cos` answers, which is where this used to be.
    EXPECT_NE(transform.M22, 2.54f * std::cos(1.5707964f));
}

TEST(XnaFbxImporter, RefusalsMatchXna)
{
    for (const std::string& fixture : {"fbx_empty.fbx", "fbx_not_fbx.fbx", "fbx_not_fbx_large.fbx"})
    {
        ImporterContext context;
        const std::string record = Expected("fbx/" + fixture);
        ASSERT_EQ(record.rfind("throws InvalidContentException: ", 0), 0u) << fixture;
        const std::string message = record.substr(std::string("throws InvalidContentException: ").size());
        Xna::FbxImporter importer;
        try
        {
            (void)importer.Import(Fixture(fixture).string(), context);
            ADD_FAILURE() << fixture << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), message) << fixture;
        }
    }
    // A `.x` handed to the FBX importer is refused for what it is, with its own sentence.
    {
        ImporterContext context;
        Xna::FbxImporter importer;
        try
        {
            (void)importer.Import(Fixture("bare_mesh.x").string(), context);
            ADD_FAILURE() << "a .x file was accepted as FBX";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ("throws InvalidContentException: " + error.getMessageProperty(),
                      Expected("fbx/an_x_file"));
        }
    }
    // A missing file is the runtime's own refusal, and XNA's sentence names the path.
    {
        ImporterContext context;
        Xna::FbxImporter importer;
        EXPECT_NE(Expected("fbx/missing.fbx").find("Cannot import the specified mesh."),
                  std::string::npos);
        EXPECT_THROW((void)importer.Import(Fixture("no_such_model.fbx").string(), context),
                     System::IO::FileNotFoundException);
    }
}

// XNA's own SDK refuses a modern FBX; CNA reads one. The divergence is deliberate and measured.
// The wrapping every Autodesk exporter uses and nothing written here did.
TEST(XnaFbxImporter, AValueListWrappedAcrossLinesReadsAsTheSameMesh)
{
    // FBX 6 ASCII breaks a long value list across lines and writes the comma that separates the
    // last value on one line from the first on the next at the *start* of the next line. A reader
    // that ends a list at the newline reads every fixture written here and none of the 149 real
    // ones in the public samples, which is exactly what happened until XNAPP-242 built them.
    ImporterContext context;
    Xna::FbxImporter importer;
    const std::shared_ptr<Graphics::NodeContent> wrapped =
        importer.Import(Fixture("fbx_wrapped_values.fbx").string(), context);
    const std::shared_ptr<Graphics::NodeContent> plain =
        importer.Import(Fixture("fbx_bare_mesh.fbx").string(), context);
    ASSERT_NE(wrapped, nullptr);
    ASSERT_NE(plain, nullptr);

    // The two documents are the same mesh; only the line breaks differ, so the graphs must be
    // identical rather than merely both readable.
    std::string wrappedText;
    std::string plainText;
    Describe(wrappedText, wrapped, "");
    Describe(plainText, plain, "");
    EXPECT_EQ(wrappedText, plainText);
}

TEST(XnaFbxImporter, AModernBinaryFbxIsReadWhereXnasSdkRefusesIt)
{
    // The genuine importer's answer for this exact file is recorded, and it is a refusal: its FBX
    // SDK 2011.3.1 does not read version 7500, which is what every current exporter writes.
    EXPECT_NE(Expected("fbx/fbx_binary_modern.fbx").find("encountered when importing the scene"),
              std::string::npos)
        << "the recorded divergence assumes XNA refuses this file";

    ImporterContext context;
    Xna::FbxImporter importer;
    const std::shared_ptr<Graphics::NodeContent> root =
        importer.Import(Fixture("fbx_binary_modern.fbx").string(), context);
    ASSERT_NE(root, nullptr);
    // The document is the two-material quad, written binary with deflated arrays, so reading it
    // exercises the record stream, the property types and the decompression at once.
    std::string text;
    Describe(text, root, "");
    EXPECT_NE(text.find("MeshContent"), std::string::npos) << text;
    EXPECT_NE(text.find("position 0 "), std::string::npos) << text;
    EXPECT_NE(text.find("channel Normal0"), std::string::npos) << text;
}
