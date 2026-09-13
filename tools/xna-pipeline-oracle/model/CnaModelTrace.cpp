// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-170: CNA's side of the model trace.
//
// `ModelImportOracle.cs` prints what the genuine XNA importer answers for a directory of models;
// this prints what CNA's importer answers for the same directory, in the same words and the same
// order, so that the two can be diffed line for line. Both have a `BITS` mode that prints every
// float as its raw IEEE-754 bits, which is the only form that can show a sign of zero or settle a
// single ulp without a decimal round trip in the way.
//
// It is a build-time tool and links `cna_content_pipeline`; nothing here is production behaviour.
//
//   build: tools/xna-pipeline-oracle/model/build-cna-model-trace.sh
//   usage: CnaModelTrace <fixture directory> <out.txt>
//   env:   CNA_MODEL_ORACLE_BITS=1   print floats as 0xXXXXXXXX (default: "0.######")
//          CNA_MODEL_ORACLE_EXACT=1  print floats at round-trip precision
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Pipeline::Graphics;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

namespace
{
    bool g_bits = false;
    bool g_exact = false;

    std::string F(float value)
    {
        if (g_bits)
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            std::ostringstream out;
            out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << raw;
            return out.str();
        }
        std::ostringstream out;
        if (g_exact)
        {
            out << std::setprecision(9) << value;
        }
        else
        {
            out << std::setprecision(7) << value;
        }
        return out.str();
    }

    std::string Describe(const Matrix& m)
    {
        return "[" + F(m.M11) + " " + F(m.M12) + " " + F(m.M13) + " " + F(m.M14) + " " +
               F(m.M21) + " " + F(m.M22) + " " + F(m.M23) + " " + F(m.M24) + " " +
               F(m.M31) + " " + F(m.M32) + " " + F(m.M33) + " " + F(m.M34) + " " +
               F(m.M41) + " " + F(m.M42) + " " + F(m.M43) + " " + F(m.M44) + "]";
    }

    template<typename T>
    const System::Collections::ObjectModel::Collection<T>& AsCollection(const auto& collection)
    {
        return static_cast<const System::Collections::ObjectModel::Collection<T>&>(collection);
    }

    std::string DescribeChannel(const std::shared_ptr<Graphics::VertexChannelBase>& channel)
    {
        std::string text;
        if (const auto v3 = std::dynamic_pointer_cast<Graphics::VertexChannel<Vector3>>(channel))
        {
            for (SharpRuntime::intcs i = 0; i < v3->getCountProperty(); ++i)
            {
                const Vector3 v = v3->At(i);
                text += (text.empty() ? "" : " ") + std::string("(") + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + ")";
            }
            return "Vector3 " + text;
        }
        if (const auto v2 = std::dynamic_pointer_cast<Graphics::VertexChannel<Vector2>>(channel))
        {
            for (SharpRuntime::intcs i = 0; i < v2->getCountProperty(); ++i)
            {
                const Vector2 v = v2->At(i);
                text += (text.empty() ? "" : " ") + std::string("(") + F(v.X) + "," + F(v.Y) + ")";
            }
            return "Vector2 " + text;
        }
        if (const auto v4 = std::dynamic_pointer_cast<Graphics::VertexChannel<Vector4>>(channel))
        {
            for (SharpRuntime::intcs i = 0; i < v4->getCountProperty(); ++i)
            {
                const Vector4 v = v4->At(i);
                text += (text.empty() ? "" : " ") + std::string("(") + F(v.X) + "," + F(v.Y) + "," +
                        F(v.Z) + "," + F(v.W) + ")";
            }
            return "Vector4 " + text;
        }
        return "<other>";
    }

    void Describe(std::string& text, const std::shared_ptr<Graphics::NodeContent>& node,
                  const std::string& path)
    {
        const std::string here =
            path + "/" + (node->getNameProperty().empty() ? std::string("<null>") : node->getNameProperty());
        const auto& children =
            AsCollection<std::shared_ptr<Graphics::NodeContent>>(node->getChildrenProperty());
        text += here + " transform=" + Describe(node->getTransformProperty()) +
                " absolute=" + Describe(node->getAbsoluteTransformProperty()) +
                " children=" + std::to_string(children.getCountProperty()) + "\n";
        if (const auto mesh = std::dynamic_pointer_cast<Graphics::MeshContent>(node); mesh != nullptr)
        {
            const auto& geometry =
                AsCollection<std::shared_ptr<Graphics::GeometryContent>>(mesh->getGeometryProperty());
            text += "  mesh positions=" +
                    std::to_string(mesh->getPositionsProperty().getCountProperty()) +
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
                text += "   geometry indices=" +
                        std::to_string(batch->getIndicesProperty().getCountProperty()) +
                        " vertices=" +
                        std::to_string(batch->getVerticesProperty().getVertexCountProperty()) +
                        " channels=" + std::to_string(channels.getCountProperty()) + "\n";
                std::string indices;
                for (SharpRuntime::intcs i = 0; i < batch->getIndicesProperty().getCountProperty(); ++i)
                {
                    indices += (indices.empty() ? "" : ",") + std::to_string(batch->getIndicesProperty()[i]);
                }
                text += "    indices " + indices + "\n";
                std::string positionIndices;
                const auto& mapped = batch->getVerticesProperty().getPositionIndicesProperty();
                for (SharpRuntime::intcs i = 0; i < mapped.getCountProperty(); ++i)
                {
                    positionIndices += (positionIndices.empty() ? "" : ",") +
                                       std::to_string(mapped.At(i));
                }
                text += "    positionIndices " + positionIndices + "\n";
                for (SharpRuntime::intcs c = 0; c < channels.getCountProperty(); ++c)
                {
                    const std::shared_ptr<Graphics::VertexChannelBase>& channel = channels[c];
                    text += "    channel " + channel->getNameProperty() + " type=" +
                            DescribeChannel(channel) + "\n";
                }
            }
        }
        for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
        {
            Describe(text, children[i], here);
        }
    }

    class Logger final : public Pipeline::ContentBuildLogger
    {
    public:
        void LogMessage(const std::string&) override {}
        void LogImportantMessage(const std::string&) override {}
        void LogWarning(const std::string&, const Pipeline::ContentIdentity&,
                        const std::string&) override {}
    };

    class Context final : public Pipeline::ContentImporterContext
    {
    public:
        [[nodiscard]] Pipeline::ContentBuildLogger& getLoggerProperty() const override
        {
            return logger_;
        }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return {}; }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return {}; }
        void AddDependency(const std::string&) override {}
    private:
        mutable Logger logger_;
    };
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: CnaModelTrace <fixture directory> <out.txt>\n");
        return 2;
    }
    const char* bits = std::getenv("CNA_MODEL_ORACLE_BITS");
    const char* exact = std::getenv("CNA_MODEL_ORACLE_EXACT");
    g_bits = bits != nullptr && std::strcmp(bits, "1") == 0;
    g_exact = exact != nullptr && std::strcmp(exact, "1") == 0;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(argv[1]))
    {
        if (!entry.is_regular_file()) { continue; }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".fbx" || extension == ".x") { files.push_back(entry.path()); }
    }
    std::sort(files.begin(), files.end());

    std::ofstream out(argv[2]);
    for (const std::filesystem::path& file : files)
    {
        Context context;
        std::string text;
        try
        {
            std::shared_ptr<Graphics::NodeContent> root;
            std::string extension = file.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".fbx")
            {
                root = Pipeline::FbxImporter().Import(file.string(), context);
            }
            else
            {
                root = Pipeline::XImporter().Import(file.string(), context);
            }
            Describe(text, root, "");
        }
        catch (const std::exception& error)
        {
            text = std::string("ERROR ") + error.what() + "\n";
        }
        out << "=== " << file.filename().string() << "\n" << text;
    }
    return 0;
}
