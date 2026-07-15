// SPDX-License-Identifier: MS-PL
// Task 942: verify AnimationPlayer's GetSkinTransforms() output, fed into
// SkinnedEffect::SetBoneTransforms() before Model.Draw(), actually deforms a real
// Content.Load<Model>()-loaded skinned mesh over time -- the real Skinned Model Sample's own
// wiring pattern, using only already-existing public APIs (SkinnedEffect::SetBoneTransforms(),
// ModelMesh::Draw() via Model::Draw(), AnimationPlayer::Update()/GetSkinTransforms()). No engine
// change is needed for this task (see Task 939's own correction of this task's original,
// factually-wrong premise about Model.Draw() having a bone-transform parameter) -- what matters
// is proving the pieces compose correctly end-to-end.
//
// Method: a real .model.json + binary fixture (1-bone rig, "Move" clip translating bone 0 from
// (0,0,0) at t=0s to (+0.5,0,0) at t=1s) with a single stride-52 GPU-skinned quad mesh (all
// vertices 100% bound to bone 0), "effect": "SkinnedEffect", loaded via Content.Load<Model>().
// The quad covers NDC x: -1..0 (left half) in bind pose. A real AnimationPlayer is bound to the
// loaded Model's own Tag (SkinningData) and driven to two different times; before each Draw,
// AnimationPlayer::GetSkinTransforms() is fed into the mesh's real SkinnedEffect via
// SetBoneTransforms() (the actual "wiring" this task is about), then Model.Draw() renders it.
//
// A single screen point at NDC x=-0.75 is sampled at both times:
//  - t=0s (bone at bind pose, no translation): quad still covers x:-1..0 -> point is INSIDE -> Red.
//  - t=1s (bone translated +0.5 on X): quad now covers x:-0.5..0.5 -> point is OUTSIDE -> background.
// The same screen point changing color between the two Draw calls is only possible if the bone
// transform produced by AnimationPlayer::Update() at each different time actually reached the GPU
// and displaced the mesh -- proving the full AnimationPlayer -> SkinnedEffect -> Draw pipeline.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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

    // Minimal QOI encoder: every pixel is a literal QOI_OP_RGBA chunk -- SkinnedEffect's real
    // XNA shader is always textured (no TextureEnabled toggle to leave it off), so every
    // SkinnedEffect example/test in this codebase binds one (Task 402/407's own convention).
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

    void AppendMatrix(std::vector<std::uint8_t>& out, const Matrix& m)
    {
        AppendFloat(out, m.M11); AppendFloat(out, m.M12); AppendFloat(out, m.M13); AppendFloat(out, m.M14);
        AppendFloat(out, m.M21); AppendFloat(out, m.M22); AppendFloat(out, m.M23); AppendFloat(out, m.M24);
        AppendFloat(out, m.M31); AppendFloat(out, m.M32); AppendFloat(out, m.M33); AppendFloat(out, m.M34);
        AppendFloat(out, m.M41); AppendFloat(out, m.M42); AppendFloat(out, m.M43); AppendFloat(out, m.M44);
    }

    // GPU-compact skinned vertex, matching ModelTypeReader::Read()'s stride-52 branch and the
    // established SkinnedGpuVertex layout (Task 407): pos(12)+normal(12)+uv(8)+weights(16)+
    // indices(4). All vertices below are 100% bound to bone 0.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");
}

class ModelSkinnedAnimationPlaybackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_model_skinned_animation_playback_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // 1-bone skeleton: bone 0 = root, identity bind/inverse-bind pose.
        std::vector<std::uint8_t> skelBytes;
        AppendInt32(skelBytes, 1); // boneCount
        AppendInt32(skelBytes, -1); // bone 0 parent
        AppendMatrix(skelBytes, Matrix::getIdentityProperty()); // bind pose
        AppendMatrix(skelBytes, Matrix::getIdentityProperty()); // inverse bind pose
        std::ofstream skelF(root / "rig.skeleton.bin", std::ios::binary);
        skelF.write(reinterpret_cast<const char*>(skelBytes.data()),
                    static_cast<std::streamsize>(skelBytes.size()));
        skelF.close();

        // "Move" clip: bone 0 translates (0,0,0) at t=0s -> (+0.5,0,0) at t=1s.
        std::vector<std::uint8_t> clipBytes;
        AppendDouble(clipBytes, 1.0); // duration seconds
        AppendInt32(clipBytes, 1);    // trackCount
        AppendInt32(clipBytes, 0);    // track 0 boneIndex
        AppendInt32(clipBytes, 2);    // keyCount
        AppendDouble(clipBytes, 0.0);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 1.0f);
        AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f);
        AppendDouble(clipBytes, 1.0);
        AppendFloat(clipBytes, 0.5f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f);
        AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 0.0f); AppendFloat(clipBytes, 1.0f);
        AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f); AppendFloat(clipBytes, 1.0f);
        std::ofstream clipF(root / "move.clip.bin", std::ios::binary);
        clipF.write(reinterpret_cast<const char*>(clipBytes.data()),
                    static_cast<std::streamsize>(clipBytes.size()));
        clipF.close();

        // Quad covering NDC x: -1..0, y: -1..1 (left half), all vertices 100% bound to bone 0 --
        // exactly Task 407's own established geometry/binding, so this test's expected pixel
        // math directly mirrors that already-proven-correct GPU skinning scenario.
        const SkinnedGpuVertex verts[4] = {
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            {  0,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
        };
        std::vector<std::uint8_t> vertBytes(sizeof(verts));
        std::memcpy(vertBytes.data(), verts, sizeof(verts));
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
      "name": "Quad",
      "vertices": "quad_verts.bin",
      "indices": "quad_idx.bin",
      "vertexStride": 52,
      "effect": "SkinnedEffect",
      "texture": "solid_red.qoi"
    }
  ]
})");

        getContentProperty().setRootDirectoryProperty(root.string());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        Model model = getContentProperty().Load<Model>("rig");

        auto* skinningData = dynamic_cast<SkinningData*>(model.getTagProperty());
        if (skinningData == nullptr)
        {
            std::printf("[FAIL] Model.Tag is not a real SkinningData\n");
            Exit();
            return;
        }

        ModelMesh* quadMesh = nullptr;
        for (ModelMesh* mesh : model.getMeshesProperty()) {
            if (mesh->getNameProperty() == "Quad") quadMesh = mesh;
        }
        auto* skinnedFx = quadMesh != nullptr
            ? dynamic_cast<SkinnedEffect*>(quadMesh->getMeshPartsProperty()[0]->getEffectProperty())
            : nullptr;
        if (skinnedFx == nullptr)
        {
            std::printf("[FAIL] Quad mesh's Effect is not a real SkinnedEffect\n");
            Exit();
            return;
        }

        skinnedFx->EnableDefaultLighting();

        AnimationPlayer player(*skinningData);
        player.StartClip(skinningData->AnimationClips["Move"]);

        const Rectangle sampleReg(W / 8, H / 2, 1, 1); // NDC x ~ -0.75, y = 0
        auto sampleAtTime = [&](double seconds) {
            player.Update(System::TimeSpan::FromSeconds(seconds), false, false);
            // The actual Task 942 wiring: AnimationPlayer's per-frame skin transforms feed
            // directly into the mesh's own SkinnedEffect before drawing.
            skinnedFx->SetBoneTransforms(player.GetSkinTransforms());

            device.Clear(Color(0, 255, 0, 255)); // Green background.
            model.Draw(Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty());

            Color px(0, 0, 0, 0);
            device.GetBackBufferData(&sampleReg, &px, 0, 1);
            return px;
        };

        const Color atBindPose = sampleAtTime(0.0);
        const bool bindPoseOk = atBindPose.getRProperty() >= 100 && atBindPose.getGProperty() <= 100;
        std::printf("[%s] t=0s (bind pose, quad still left): (%d,%d,%d) expected textured/lit (red-dominant)\n",
                    bindPoseOk ? "PASS" : "FAIL",
                    atBindPose.getRProperty(), atBindPose.getGProperty(), atBindPose.getBProperty());

        const Color atEnd = sampleAtTime(1.0);
        const bool endOk = atEnd.getGProperty() > atEnd.getRProperty();
        std::printf("[%s] t=1s (bone translated, quad shifted away): (%d,%d,%d) expected green background\n",
                    endOk ? "PASS" : "FAIL",
                    atEnd.getRProperty(), atEnd.getGProperty(), atEnd.getBProperty());

        const int passCount = (bindPoseOk ? 1 : 0) + (endOk ? 1 : 0);
        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    ModelSkinnedAnimationPlaybackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return result_; }
};

int main()
{
    ModelSkinnedAnimationPlaybackTest game;
    game.Run();
    return game.getResult();
}
