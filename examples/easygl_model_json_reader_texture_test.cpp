// SPDX-License-Identifier: MS-PL
// Task 932: verify ModelTypeReader::Read() honors a per-mesh "texture" field in .model.json,
// binding it to the mesh's BasicEffect (TextureEnabled=true, Texture=<loaded texture>) -- mirrors
// SkinnedModelTypeReader's own already-working per-part texture loading, extended to the plain
// (non-skinned) Content.Load<Model>() path.
//
// Confirmed gap (DEFERRED.md item #6 addendum, found while auditing ../cna-samples):
// ModelTypeReader::Read()'s mesh-parsing loop extracted "name"/"vertices"/"indices"/
// "vertexStride"/"effect" per mesh but never looked for a "texture" field, so every
// Content.Load<Model>()-loaded mesh rendered with BasicEffect.TextureEnabled == false regardless
// of what the source asset's own real material texture was.
//
// Method: write a real .model.json + binary vertex/index fixture (the same NDC [-0.5, 0.5] quad,
// stride-32 VertexPositionNormalTexture, as the Task 927 test) plus a tiny hand-encoded 2x2 solid
// Red QOI texture file, and a "texture" field naming it. Render with identity World/View/Projection
// and BasicEffect defaults otherwise (LightingEnabled=false, DiffuseColor=white -- white * texture
// color is a no-op multiply, so a correctly bound texture renders the quad exactly Red). If the
// "texture" field is not wired up, the quad renders plain White instead (BasicEffect's untextured
// default), which is clearly distinguishable from both Red and the Blue background.
//
// Check A — screen centre (inside the quad): must be Red (texture bound and sampled).
// Check B — a point outside the quad's own bounds: must be the Blue clear colour.
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

    void AppendU32BE(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }

    // Minimal QOI encoder: every pixel is emitted as a literal QOI_OP_RGBA chunk (tag 0xFF
    // followed by R,G,B,A). Always spec-valid regardless of pixel history -- no run-length/
    // index/diff optimization needed for a tiny fixed-color test fixture.
    void WriteSolidColorQoi(const std::filesystem::path& path, int width, int height,
                            std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(14 + static_cast<std::size_t>(width) * height * 5 + 8);

        bytes.push_back('q'); bytes.push_back('o'); bytes.push_back('i'); bytes.push_back('f');
        AppendU32BE(bytes, static_cast<std::uint32_t>(width));
        AppendU32BE(bytes, static_cast<std::uint32_t>(height));
        bytes.push_back(4); // channels: RGBA
        bytes.push_back(0); // colorspace: sRGB with linear alpha

        for (int i = 0; i < width * height; ++i) {
            bytes.push_back(0xFF); // QOI_OP_RGBA
            bytes.push_back(r); bytes.push_back(g); bytes.push_back(b); bytes.push_back(a);
        }

        const std::uint8_t padding[8] = {0, 0, 0, 0, 0, 0, 0, 1};
        bytes.insert(bytes.end(), padding, padding + 8);

        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
}

class ModelJsonReaderTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_model_json_reader_texture_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // Same NDC [-0.5, 0.5] stride-32 VertexPositionNormalTexture quad as the Task 927 test.
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
        std::ofstream vf(root / "texquad_verts.bin", std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()),
                 static_cast<std::streamsize>(vertBytes.size()));
        vf.close();

        std::vector<std::uint8_t> idxBytes;
        for (std::uint16_t i : { 0, 1, 2, 0, 2, 3 })
            AppendUint16(idxBytes, i);
        std::ofstream idxf(root / "texquad_idx.bin", std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()),
                   static_cast<std::streamsize>(idxBytes.size()));
        idxf.close();

        // 2x2 solid Red texture -- distinguishable from both White (untextured fallback) and
        // Blue (clear/background), so a mis-wired "texture" field is unambiguous.
        WriteSolidColorQoi(root / "solid_red.qoi", 2, 2, 255, 0, 0, 255);

        WriteFile(root / "texquad.model.json", R"({
  "meshes": [
    {
      "name": "TexQuad",
      "vertices": "texquad_verts.bin",
      "indices": "texquad_idx.bin",
      "vertexStride": 32,
      "effect": "BasicEffect",
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

        const Color background(0, 0, 255, 255); // Blue.
        device.Clear(background);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto model = getContentProperty().Load<Model>("texquad");

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

        const auto isRed = [](const Color& c) {
            return c.getRProperty() >= 200 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
        };
        const auto isBackground = [&](const Color& c) {
            return c.getRProperty() <= 30 && c.getGProperty() <= 30 && c.getBProperty() >= 200;
        };

        int passCount = 0;
        const Color centre = sample(0.0f, 0.0f);
        const bool centreOk = isRed(centre);
        std::printf("[%s] centre (inside textured quad): (%d,%d,%d) expected Red\n",
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
    ModelJsonReaderTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    ModelJsonReaderTextureTest game;
    game.Run();
    return game.getResult();
}
