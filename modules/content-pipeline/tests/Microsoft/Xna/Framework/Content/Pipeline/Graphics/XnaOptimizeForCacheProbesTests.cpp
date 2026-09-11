// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149: CNA's `MeshHelper.OptimizeForCache` against
// genuine XNA 4.0's, on every probe the campaign measured.
//
// The probe files under tests/reference/xna40/optimize/ are the generator's own output and the
// answers beside them came back from `MeshHelper.OptimizeForCache` running under Wine against the
// real Microsoft assemblies (tools/xna-pipeline-oracle/optimize/OptimizeForCacheOracle.cs). This
// suite rebuilds each probe exactly as that driver builds it -- one `GeometryContent`, a vertex per
// distinct position for a shared mesh and one per corner otherwise -- runs CNA's method over it and
// compares the vertex order and the index buffer that come out, number for number.
//
// Three files, three questions. `probes-designed` is the family the campaign designed to separate
// candidate rules; `probes-heldout` was generated after the rule was settled, so a disagreement was
// possible; `nonmanifold-probes` puts three and four faces on one edge, which is the one thing the
// designed family could not decide and the only place the readings differ.
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
using Microsoft::Xna::Framework::Vector3;
using Graphics::GeometryContent;
using Graphics::MeshContent;
using Graphics::MeshHelper;

namespace
{
    std::filesystem::path ReferenceDirectory()
    {
        const std::filesystem::path relative = "tests/reference/xna40/optimize";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
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
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path();
             !dir.empty(); dir = dir.parent_path())
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

    struct Probe
    {
        std::string name;
        bool shared = false;
        std::vector<Vector3> positions;
        std::vector<std::array<int, 3>> faces;
    };

    std::vector<Probe> ReadProbes(const std::filesystem::path& path)
    {
        std::vector<Probe> probes;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream fields(line);
            std::string tag;
            fields >> tag;
            if (tag == "P")
            {
                Probe probe;
                int shared = 0, positionCount = 0, faceCount = 0;
                fields >> probe.name >> shared >> positionCount >> faceCount;
                probe.shared = shared != 0;
                probe.positions.reserve(static_cast<std::size_t>(positionCount));
                probe.faces.reserve(static_cast<std::size_t>(faceCount));
                probes.push_back(std::move(probe));
            }
            else if (tag == "V" && !probes.empty())
            {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                fields >> x >> y >> z;
                probes.back().positions.emplace_back(x, y, z);
            }
            else if (tag == "F" && !probes.empty())
            {
                std::array<int, 3> face{};
                fields >> face[0] >> face[1] >> face[2];
                probes.back().faces.push_back(face);
            }
        }
        return probes;
    }

    /** @brief `name -> "<position indices>|<index buffer>"`, as the oracle wrote it. */
    std::map<std::string, std::string> ReadAnswers(const std::filesystem::path& path)
    {
        std::map<std::string, std::string> answers;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            {
                line.pop_back();
            }
            const std::size_t bar = line.find('|');
            if (bar != std::string::npos)
            {
                answers[line.substr(0, bar)] = line.substr(bar + 1);
            }
        }
        return answers;
    }

    /** @brief The mesh the genuine oracle's driver builds from a probe, corner for corner. */
    std::shared_ptr<MeshContent> Build(const Probe& probe)
    {
        auto mesh = std::make_shared<MeshContent>();
        mesh->setNameProperty(probe.name);
        for (const Vector3& position : probe.positions)
        {
            mesh->getPositionsProperty().Add(position);
        }
        auto geometry = std::make_shared<GeometryContent>();
        mesh->getGeometryProperty().Add(geometry);
        std::vector<SharpRuntime::intcs> vertices;
        std::vector<SharpRuntime::intcs> indices;
        std::map<int, int> vertexOf;
        for (const std::array<int, 3>& face : probe.faces)
        {
            for (const int corner : face)
            {
                int vertex;
                if (probe.shared)
                {
                    const auto found = vertexOf.find(corner);
                    if (found == vertexOf.end())
                    {
                        vertex = static_cast<int>(vertices.size());
                        vertexOf[corner] = vertex;
                        vertices.push_back(corner);
                    }
                    else
                    {
                        vertex = found->second;
                    }
                }
                else
                {
                    vertex = static_cast<int>(vertices.size());
                    vertices.push_back(corner);
                }
                indices.push_back(vertex);
            }
        }
        geometry->getVerticesProperty().AddRange(vertices);
        geometry->getIndicesProperty().AddRange(indices);
        return mesh;
    }

    /** @brief The same string the oracle's `Describe` writes: positions, a bar, then the indices. */
    std::string Describe(const MeshContent& mesh)
    {
        const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<GeometryContent>>&>(mesh.getGeometryProperty());
        const std::shared_ptr<GeometryContent> geometry = batches[0];
        std::ostringstream text;
        const auto& positions = geometry->getVerticesProperty().getPositionIndicesProperty();
        for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
        {
            if (i != 0)
            {
                text << ',';
            }
            text << positions.At(i);
        }
        text << '|';
        const auto& indices = static_cast<const System::Collections::ObjectModel::Collection<
            SharpRuntime::intcs>&>(geometry->getIndicesProperty());
        for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
        {
            if (i != 0)
            {
                text << ',';
            }
            text << indices[i];
        }
        return text.str();
    }

    void RunFamily(const std::string& probeFile, const std::string& answerFile, std::size_t expected)
    {
        const std::filesystem::path directory = ReferenceDirectory();
        const std::vector<Probe> probes = ReadProbes(directory / probeFile);
        const std::map<std::string, std::string> answers = ReadAnswers(directory / answerFile);
        ASSERT_EQ(probes.size(), expected) << probeFile;
        ASSERT_EQ(answers.size(), expected) << answerFile;
        std::size_t matched = 0;
        std::vector<std::string> differing;
        for (const Probe& probe : probes)
        {
            const auto answer = answers.find(probe.name);
            ASSERT_NE(answer, answers.end()) << probe.name;
            const std::shared_ptr<MeshContent> mesh = Build(probe);
            MeshHelper::OptimizeForCache(mesh);
            if (Describe(*mesh) == answer->second)
            {
                ++matched;
            }
            else if (differing.size() < 8)
            {
                differing.push_back(probe.name);
            }
        }
        std::string named;
        for (const std::string& name : differing)
        {
            named += " " + name;
        }
        EXPECT_EQ(matched, expected) << "differ:" << named;
    }
}

TEST(XnaOptimizeForCacheProbes, TheDesignedFamilyAnswersWhatGenuineXnaAnswers)
{
    RunFamily("probes-designed.txt", "answers-designed.txt", 376);
}

TEST(XnaOptimizeForCacheProbes, TheHeldOutFamilyAnswersWhatGenuineXnaAnswers)
{
    RunFamily("probes-heldout.txt", "heldout-answers.txt", 240);
}

TEST(XnaOptimizeForCacheProbes, ANonManifoldEdgeAnswersWhatGenuineXnaAnswers)
{
    RunFamily("nonmanifold-probes.txt", "nonmanifold-answers.txt", 594);
}
