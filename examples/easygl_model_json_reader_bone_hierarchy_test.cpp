// SPDX-License-Identifier: MS-PL
// Task 937: verify ModelTypeReader::Read() gives each mesh its own real ModelBone (a child of
// the model's Root, named after the mesh) instead of leaving every mesh's ParentBone null.
//
// Confirmed gap (DEFERRED.md item #34 addendum / Phase 76): ModelTypeReader::Read() previously
// created exactly one synthetic "Root" ModelBone for the whole model; every mesh parsed from
// "meshes" kept ParentBone == nullptr. Real game code that looks up a named per-part bone (e.g.
// SplitScreen/TankOnAHeightMap's wheel/turret/cannon/hatch lookups via Model.Bones["PartName"])
// had no real per-mesh bone to find.
//
// Method: a real .model.json + binary vertex/index fixture with 2 distinctly-named meshes
// ("Wheel", "Turret"), loaded via Content.Load<Model>(). This is a pure structural test -- no
// rendering, no pixel checks -- verifying the Model's own Bones collection and each ModelMesh's
// ParentBone directly.
//
// Checks:
//  - Bones.Count == 3 (Root + one bone per mesh).
//  - Bones["Wheel"] and Bones["Turret"] both resolve to real, distinct, non-null bones.
//  - Each resolved bone's Parent == the model's Root bone.
//  - Each ModelMesh's own ParentBone matches its corresponding named bone exactly.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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

    // Writes a minimal, valid stride-16 VertexPositionColor triangle (3 verts) + index (3
    // indices) fixture pair. Vertex/pixel content is irrelevant to this structural test.
    void WriteTriangleFixture(const std::filesystem::path& root, const std::string& baseName)
    {
        std::vector<std::uint8_t> vertBytes;
        for (int i = 0; i < 3; ++i) {
            AppendFloat(vertBytes, static_cast<float>(i)); // x
            AppendFloat(vertBytes, 0.0f);                  // y
            AppendFloat(vertBytes, 0.0f);                  // z
            vertBytes.push_back(255); vertBytes.push_back(255);
            vertBytes.push_back(255); vertBytes.push_back(255); // RGBA white
        }
        std::ofstream vf(root / (baseName + "_verts.bin"), std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()),
                 static_cast<std::streamsize>(vertBytes.size()));

        std::vector<std::uint8_t> idxBytes;
        for (std::uint16_t i : { 0, 1, 2 })
            AppendUint16(idxBytes, i);
        std::ofstream idxf(root / (baseName + "_idx.bin"), std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()),
                   static_cast<std::streamsize>(idxBytes.size()));
    }
}

class ModelJsonReaderBoneHierarchyTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_model_json_reader_bone_hierarchy_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteTriangleFixture(root, "wheel");
        WriteTriangleFixture(root, "turret");

        WriteFile(root / "rig.cnb", R"({
  "cnbVersion": 1,
  "type": "Model",
  "meshes": [
    {
      "name": "Wheel",
      "vertices": "wheel_verts.bin",
      "indices": "wheel_idx.bin",
      "vertexStride": 16,
      "effect": "BasicEffect"
    },
    {
      "name": "Turret",
      "vertices": "turret_verts.bin",
      "indices": "turret_idx.bin",
      "vertexStride": 16,
      "effect": "BasicEffect"
    }
  ]
})");

        getContentProperty().setRootDirectoryProperty(root.string());

        Model model = getContentProperty().Load<Model>("rig");

        check(model.getBonesProperty().getCountProperty() == 3,
              "Bones.Count == 3 (Root + one bone per mesh)");

        ModelBone* wheelBone = model.getBonesProperty()["Wheel"];
        ModelBone* turretBone = model.getBonesProperty()["Turret"];
        check(wheelBone != nullptr, "Bones[\"Wheel\"] resolves to a real bone");
        check(turretBone != nullptr, "Bones[\"Turret\"] resolves to a real bone");
        check(wheelBone != turretBone, "Wheel and Turret each get their own distinct bone");

        ModelBone* root_bone = model.getRootProperty();
        if (wheelBone)
            check(wheelBone->getParentProperty() == root_bone, "Wheel bone's Parent is the model's Root");
        if (turretBone)
            check(turretBone->getParentProperty() == root_bone, "Turret bone's Parent is the model's Root");

        ModelMesh* wheelMesh = nullptr;
        ModelMesh* turretMesh = nullptr;
        for (ModelMesh* mesh : model.getMeshesProperty()) {
            if (mesh->getNameProperty() == "Wheel")  wheelMesh  = mesh;
            if (mesh->getNameProperty() == "Turret") turretMesh = mesh;
        }
        check(wheelMesh != nullptr && wheelMesh->getParentBoneProperty() == wheelBone,
              "Wheel mesh's own ParentBone matches Bones[\"Wheel\"]");
        check(turretMesh != nullptr && turretMesh->getParentBoneProperty() == turretBone,
              "Turret mesh's own ParentBone matches Bones[\"Turret\"]");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    ModelJsonReaderBoneHierarchyTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ModelJsonReaderBoneHierarchyTest game;
    game.Run();
    return game.getResult();
}
