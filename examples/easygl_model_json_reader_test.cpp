// SPDX-License-Identifier: MS-PL
// Task 927: verify Content.Load<Model>() reads vertex data correctly from a real .model.json
// descriptor + binary vertex/index files -- exercising ModelTypeReader::Read() itself, not a
// hand-built-in-C++ Model (that path is already covered by easygl_model_draw_test.cpp, Task 144).
//
// Confirmed bug (found while porting ../cna-samples, DEFERRED.md item #26): every CNA vertex
// struct publicly inherits the polymorphic IVertexType (a vtable pointer XNA's own C# interface
// never carried), inflating sizeof() past the "clean" size the .model.json format declares
// (e.g. sizeof(VertexPositionNormalTexture) == 40, not 32). ModelTypeReader::Read() used to
// compare the declared "vertexStride" against sizeof(...) directly, then reinterpret_cast the
// tightly-packed file bytes as an array of those inflated structs -- silently reading every
// vertex's fields from the wrong byte offset. This is very likely the true root cause of a
// long-tracked "near-plane-clipping"/invisible-model symptom family across several ../cna-samples
// ports (CameraShake, LensFlare, Graphics3D, PickingSample, InverseKinematics, ChaseCamera).
//
// Method: write a real .model.json + binary _verts.bin/_idx.bin fixture (stride-32
// VertexPositionNormalTexture, matching every model asset in ../cna-samples) describing a flat
// quad covering NDC x/y in [-0.5, 0.5], load it via Content.Load<Model>(), render with identity
// World/View/Projection (object space == NDC) and BasicEffect defaults (LightingEnabled=false,
// DiffuseColor=white, untextured) so a correctly-positioned quad renders solid white. If vertex
// positions are corrupted (reading the wrong file byte offsets), the quad would render at some
// other, unpredictable location instead.
//
// Check A — screen centre (inside the quad): must be White.
// Check B — a point outside the quad's own bounds (must NOT also be covered by a mispositioned
//   quad): must be the clear colour, not White.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

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
}

class ModelJsonReaderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        // Scratch content root, unique per process run.
        const auto root = std::filesystem::temp_directory_path()
            / ("cna_model_json_reader_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // Stride-32 VertexPositionNormalTexture: pos[12] + normal[12] + texcoord[8], matching
        // every model asset ../cna-samples' own conversion tools produce. A flat quad in the
        // XY plane, covering NDC x/y in [-0.5, 0.5] -- deliberately smaller than the full
        // screen, so a corrupted/scrambled position is very unlikely to still land here.
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

        WriteFile(root / "quad.model.json", R"({
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

        const Color background(0, 0, 255, 255); // Blue -- clearly distinct from a white quad.
        device.Clear(background);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto model = getContentProperty().Load<Model>("quad");

        // Bind BasicEffect defaults (LightingEnabled=false, DiffuseColor=white, untextured) to
        // every mesh part -- ModelTypeReader already does this at load time, this just confirms
        // the loaded model's own effect renders as expected.
        model.Draw(Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty());

        auto sample = [&](float ndcX, float ndcY) {
            const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(W));
            const int py = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(H));
            Rectangle reg(px, py, 1, 1);
            Color c(0, 0, 0, 0);
            device.GetBackBufferData(&reg, &c, 0, 1);
            return c;
        };

        const auto isWhite = [](const Color& c) {
            return c.getRProperty() >= 200 && c.getGProperty() >= 200 && c.getBProperty() >= 200;
        };
        const auto isBackground = [&](const Color& c) {
            return c.getRProperty() <= 30 && c.getGProperty() <= 30 && c.getBProperty() >= 200;
        };

        int passCount = 0;
        const Color centre = sample(0.0f, 0.0f);
        const bool centreOk = isWhite(centre);
        std::printf("[%s] centre (inside quad): (%d,%d,%d) expected White\n",
                    centreOk ? "PASS" : "FAIL",
                    centre.getRProperty(), centre.getGProperty(), centre.getBProperty());
        if (centreOk) ++passCount;

        const Color outside = sample(0.8f, 0.8f);
        const bool outsideOk = isBackground(outside);
        std::printf("[%s] outside quad bounds: (%d,%d,%d) expected Blue background\n",
                    outsideOk ? "PASS" : "FAIL",
                    outside.getRProperty(), outside.getGProperty(), outside.getBProperty());
        if (outsideOk) ++passCount;

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    ModelJsonReaderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    ModelJsonReaderTest game;
    game.Run();
    return game.getResult();
}
