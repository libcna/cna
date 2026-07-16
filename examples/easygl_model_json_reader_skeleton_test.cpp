// SPDX-License-Identifier: MS-PL
// Task 941: verify ModelTypeReader::Read() parses the new .model.json "skeleton"/"animations"
// fields (Phase 77 skeletal animation schema, see Task 939) and attaches the resulting
// SkinningData to the loaded Model's own Tag property, and that a mesh whose "effect" is
// "SkinnedEffect" gets a real SkinnedEffect (with its "texture" field bound) instead of the
// default BasicEffect.
//
// Method: a real .model.json + binary vertex/index/skeleton/clip fixture (2-bone rig, one
// "Move" animation clip, one stride-52 GPU-skinned mesh using SkinnedEffect + a texture),
// loaded via Content.Load<Model>(). Pure structural test -- no rendering, no pixel checks --
// mirrors easygl_model_json_reader_bone_hierarchy_test.cpp's own approach.
//
// Checks:
//  - model.getTagProperty() is a real SkinningData* (not null).
//  - SkinningData::BoneCount/SkeletonHierarchy/BindPose/InverseBindPose match the fixture.
//  - SkinningData::AnimationClips["Move"] has the expected Duration and keyframe data.
//  - The mesh's own Effect is a real SkinnedEffect (not BasicEffect).
//  - The SkinnedEffect's Texture is bound (non-null) from the mesh's "texture" field.
//  - The mesh's VertexBuffer got all 3 vertices (stride-52 SetDataRaw path exercised).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"

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

    void AppendInt32(std::vector<std::uint8_t>& out, std::int32_t v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    void AppendDouble(std::vector<std::uint8_t>& out, double v)
    {
        std::uint8_t bytes[8];
        std::memcpy(bytes, &v, 8);
        out.insert(out.end(), bytes, bytes + 8);
    }

    void AppendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        out.insert(out.end(), bytes, bytes + 2);
    }

    void AppendU32BE(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }

    void AppendMatrix(std::vector<std::uint8_t>& out, const Matrix& m)
    {
        AppendFloat(out, m.M11); AppendFloat(out, m.M12); AppendFloat(out, m.M13); AppendFloat(out, m.M14);
        AppendFloat(out, m.M21); AppendFloat(out, m.M22); AppendFloat(out, m.M23); AppendFloat(out, m.M24);
        AppendFloat(out, m.M31); AppendFloat(out, m.M32); AppendFloat(out, m.M33); AppendFloat(out, m.M34);
        AppendFloat(out, m.M41); AppendFloat(out, m.M42); AppendFloat(out, m.M43); AppendFloat(out, m.M44);
    }

    // Minimal QOI encoder: every pixel is a literal QOI_OP_RGBA chunk. Content is irrelevant
    // to this structural test -- only whether the texture ends up bound at all matters.
    void WriteSolidColorQoi(const std::filesystem::path& path, int width, int height,
                            std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        std::vector<std::uint8_t> bytes;
        bytes.push_back('q'); bytes.push_back('o'); bytes.push_back('i'); bytes.push_back('f');
        AppendU32BE(bytes, static_cast<std::uint32_t>(width));
        AppendU32BE(bytes, static_cast<std::uint32_t>(height));
        bytes.push_back(4);
        bytes.push_back(0);
        for (int i = 0; i < width * height; ++i) {
            bytes.push_back(0xFF);
            bytes.push_back(r); bytes.push_back(g); bytes.push_back(b); bytes.push_back(a);
        }
        const std::uint8_t padding[8] = {0, 0, 0, 0, 0, 0, 0, 1};
        bytes.insert(bytes.end(), padding, padding + 8);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

class ModelJsonReaderSkeletonTest : public Game
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
            / ("cna_model_json_reader_skeleton_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // 2-bone skeleton: bone 0 = root (parent -1), bone 1 = child of bone 0. Bind pose for
        // bone 1 offsets it by (0,1,0); inverse bind poses are the exact inverse translation so
        // GetSkinTransforms() reasoning stays simple if extended later.
        std::vector<std::uint8_t> skelBytes;
        AppendInt32(skelBytes, 2); // boneCount
        AppendInt32(skelBytes, -1); // bone 0 parent
        AppendInt32(skelBytes, 0);  // bone 1 parent
        AppendMatrix(skelBytes, Matrix::getIdentityProperty());              // bone 0 bind pose
        AppendMatrix(skelBytes, Matrix::CreateTranslation(Vector3(0, 1, 0))); // bone 1 bind pose
        AppendMatrix(skelBytes, Matrix::getIdentityProperty());                // bone 0 inverse bind
        AppendMatrix(skelBytes, Matrix::CreateTranslation(Vector3(0, -1, 0))); // bone 1 inverse bind
        std::ofstream skelF(root / "rig.skeleton.bin", std::ios::binary);
        skelF.write(reinterpret_cast<const char*>(skelBytes.data()),
                    static_cast<std::streamsize>(skelBytes.size()));
        skelF.close();

        // "Move" clip: single track on bone 0, 2 keyframes moving (0,0,0) -> (2,0,0) over 1s.
        std::vector<std::uint8_t> clipBytes;
        AppendDouble(clipBytes, 1.0); // duration seconds
        AppendInt32(clipBytes, 1);    // trackCount
        AppendInt32(clipBytes, 0);    // track 0 boneIndex
        AppendInt32(clipBytes, 2);    // keyCount
        // key 0: t=0, translation (0,0,0), identity rotation, scale (1,1,1)
        AppendDouble(clipBytes, 0.0);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 1.0f);
        AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f);
        // key 1: t=1, translation (2,0,0), identity rotation, scale (1,1,1)
        AppendDouble(clipBytes, 1.0);
        AppendFloat(clipBytes, 2.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 1.0f);
        AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f);
        std::ofstream clipF(root / "move.clip.bin", std::ios::binary);
        clipF.write(reinterpret_cast<const char*>(clipBytes.data()),
                    static_cast<std::streamsize>(clipBytes.size()));
        clipF.close();

        // Stride-52 GPU-skinned vertex data (pos+normal+texcoord+blendweight+blendindices) for
        // a 3-vertex triangle. Field values are irrelevant to this structural test -- only the
        // byte count (3 * 52) and successful SetDataRaw() upload matter.
        std::vector<std::uint8_t> vertBytes;
        for (int i = 0; i < 3; ++i) {
            AppendFloat(vertBytes, static_cast<float>(i)); AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 0.0f); // pos
            AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 1.0f); // normal
            AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 0.0f); // texcoord
            AppendFloat(vertBytes, 1.0f); AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 0.0f); AppendFloat(vertBytes, 0.0f); // blendweight
            AppendFloat(vertBytes, 0.0f); // blendindices (as float, matches the 52-byte stride)
        }
        std::ofstream vf(root / "rig_verts.bin", std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()),
                 static_cast<std::streamsize>(vertBytes.size()));
        vf.close();

        std::vector<std::uint8_t> idxBytes;
        for (std::uint16_t i : { 0, 1, 2 })
            AppendUint16(idxBytes, i);
        std::ofstream idxf(root / "rig_idx.bin", std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()),
                   static_cast<std::streamsize>(idxBytes.size()));
        idxf.close();

        WriteSolidColorQoi(root / "solid_red.qoi", 2, 2, 255, 0, 0, 255);

        WriteFile(root / "rig.cnb", R"({
  "cnbVersion": 1,
  "type": "Model",
  "skeleton": "rig.skeleton.bin",
  "animations": [
    { "name": "Move", "clip": "move.clip.bin" }
  ],
  "meshes": [
    {
      "name": "Body",
      "vertices": "rig_verts.bin",
      "indices": "rig_idx.bin",
      "vertexStride": 52,
      "effect": "SkinnedEffect",
      "texture": "solid_red.qoi"
    }
  ]
})");

        getContentProperty().setRootDirectoryProperty(root.string());

        Model model = getContentProperty().Load<Model>("rig");

        auto* skinningData = dynamic_cast<SkinningData*>(model.getTagProperty());
        check(skinningData != nullptr, "Model.Tag is a real SkinningData");
        if (skinningData != nullptr)
        {
            check(skinningData->BoneCount == 2, "SkinningData.BoneCount == 2");
            check(skinningData->SkeletonHierarchy.size() == 2
                      && skinningData->SkeletonHierarchy[0] == -1
                      && skinningData->SkeletonHierarchy[1] == 0,
                  "SkeletonHierarchy == {-1, 0}");
            check(skinningData->BindPose.size() == 2
                      && skinningData->BindPose[1].getTranslationProperty().Y == 1.0f,
                  "BindPose[1] translation.Y == 1.0");
            check(skinningData->InverseBindPose.size() == 2
                      && skinningData->InverseBindPose[1].getTranslationProperty().Y == -1.0f,
                  "InverseBindPose[1] translation.Y == -1.0");

            const auto clipIt = skinningData->AnimationClips.find("Move");
            const bool hasClip = clipIt != skinningData->AnimationClips.end();
            check(hasClip, "AnimationClips[\"Move\"] exists");
            if (hasClip)
            {
                const AnimationClip& clip = clipIt->second;
                check(clip.Duration == System::TimeSpan::FromSeconds(1.0), "Move clip Duration == 1s");
                check(clip.Tracks.size() == 1 && clip.Tracks[0].BoneIndex == 0,
                      "Move clip has 1 track on bone 0");
                check(clip.Tracks.size() == 1 && clip.Tracks[0].Keys.size() == 2
                          && clip.Tracks[0].Keys[1].Translation.X == 2.0f,
                      "Move clip's 2nd keyframe translation.X == 2.0");

                // End-to-end: feed this real, Content.Load<Model>()-parsed SkinningData/clip
                // straight into a real AnimationPlayer, matching how game code actually uses
                // this data (see Task 942).
                AnimationPlayer player(*skinningData);
                player.StartClip(clip);
                player.Update(System::TimeSpan::FromSeconds(1.0), false, false);
                check(player.GetWorldTransforms()[0].getTranslationProperty().X == 2.0f,
                      "AnimationPlayer fed with loaded SkinningData/clip reaches bone 0 X == 2.0 at t=1s");
            }
        }

        ModelMesh* bodyMesh = nullptr;
        for (ModelMesh* mesh : model.getMeshesProperty()) {
            if (mesh->getNameProperty() == "Body") bodyMesh = mesh;
        }
        check(bodyMesh != nullptr, "Body mesh exists");
        if (bodyMesh != nullptr)
        {
            auto* part = bodyMesh->getMeshPartsProperty()[0];
            check(part->getNumVerticesProperty() == 3, "Body mesh part has 3 vertices (stride-52 upload)");

            auto* skinnedFx = dynamic_cast<SkinnedEffect*>(part->getEffectProperty());
            check(skinnedFx != nullptr, "Body mesh's Effect is a real SkinnedEffect");
            if (skinnedFx != nullptr)
            {
                check(skinnedFx->getTextureProperty() != nullptr,
                      "SkinnedEffect.Texture is bound from the mesh's \"texture\" field");
            }
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    ModelJsonReaderSkeletonTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ModelJsonReaderSkeletonTest game;
    game.Run();
    return game.getResult();
}
