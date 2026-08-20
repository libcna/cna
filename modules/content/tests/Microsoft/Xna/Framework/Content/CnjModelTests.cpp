// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-19/CNB-20: first-ever gtest coverage for ModelTypeReader, migrated from
// .model.json to .cnj (CNB-17). Prior exercise of this reader was example-program only (7
// examples/*model_json*.cpp files, migrated to .cnj by CNB-18) -- no gtest coverage existed
// before this, per docs/model-content-pipeline-support.md's own flagged gap.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelMesh;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_model_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void AppendFloat(std::vector<std::uint8_t>& out, float v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    void AppendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        out.insert(out.end(), bytes, bytes + 2);
    }

    // Writes a minimal stride-32 VertexPositionNormalTexture quad fixture (verts + indices +
    // .cnj), matching modules/renderers/easygl/examples/easygl_model_json_reader_test.cpp's own fixture shape.
    void WriteQuadModelFixture(const std::filesystem::path& root)
    {
        struct V { float x, y, z, nx, ny, nz, u, v; };
        const V verts[4] = {
            { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
            { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
            {  0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
            {  0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        };
        std::vector<std::uint8_t> vertBytes;
        for (const auto& v : verts) {
            AppendFloat(vertBytes, v.x);  AppendFloat(vertBytes, v.y);  AppendFloat(vertBytes, v.z);
            AppendFloat(vertBytes, v.nx); AppendFloat(vertBytes, v.ny); AppendFloat(vertBytes, v.nz);
            AppendFloat(vertBytes, v.u);  AppendFloat(vertBytes, v.v);
        }
        std::ofstream vf(root / "quad_verts.bin", std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()),
                 static_cast<std::streamsize>(vertBytes.size()));
        vf.close();

        std::vector<std::uint8_t> idxBytes;
        for (std::uint16_t i : { 0, 1, 2, 0, 2, 3 })
            AppendUint16(idxBytes, i);
        std::ofstream idxf(root / "quad_idx.bin", std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()),
                   static_cast<std::streamsize>(idxBytes.size()));
        idxf.close();

        WriteFile(root / "quad.cnj", R"({
  "cnjVersion": 1,
  "type": "Model",
  "meshes": [
    {
      "name": "Quad",
      "vertices": "quad_verts.bin",
      "indices": "quad_idx.bin",
      "vertexStride": 32,
      "effect": "BasicEffect"
    }
  ]
})");
    }
}

class CnjModelTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjModelTest, LoadsRealCnjFixture)
{
    // Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline rejects it
    // before the mesh/mesh-part structure this test actually exercises is ever reached. Guarded
    // per-test (not in a fixture-wide SetUp()) so MismatchedTypeThrowsContentLoadException below,
    // whose throw happens earlier for an unrelated reason (a type mismatch), keeps running
    // everywhere.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ScratchContentRoot root;
    WriteQuadModelFixture(root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("quad");

    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->getNameProperty(), "Quad");
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);
}

TEST_F(CnjModelTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "wrong.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Model>("wrong"), ContentLoadException);
}
