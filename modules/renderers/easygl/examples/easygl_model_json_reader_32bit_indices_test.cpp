// SPDX-License-Identifier: MS-PL
// Task 931: verify ModelTypeReader::Read() correctly loads a mesh whose vertex count exceeds
// 65535, which requires 32-bit indices (real XNA's stock ModelProcessor auto-selects
// IndexElementSize.ThirtyTwoBits in exactly this situation) -- exercising a mesh index buffer
// whose index VALUES also exceed 65535, not just its vertex count.
//
// Confirmed bug (DEFERRED.md item #6 addendum, found while auditing ../cna-samples):
// ModelTypeReader::Read() unconditionally read the index file as std::uint16_t and always built a
// 16-bit IndexBuffer, regardless of vertex count. For a mesh with more than 65535 vertices this
// silently mis-decodes the index buffer -- both the wrong element count (idxBytes.size() divided
// by the wrong stride) and, for any index value >= 65536, byte-for-byte corruption (a 4-byte index
// value gets split across two bogus "indices" instead of read as one).
//
// Method: write a real .model.json + binary vertex/index fixture with 65540 VertexPositionNormalTexture
// vertices (stride 32) -- the first 65536 are inert filler (zeroed, never referenced by any index),
// and the last 4 (indices 65536-65539, each requiring a full 32 bits to represent) form the same
// NDC [-0.5, 0.5] quad as the Task 927 test. Load via Content.Load<Model>() and render with identity
// World/View/Projection + BasicEffect defaults (untextured, DiffuseColor=white). If the index buffer
// is mis-decoded (wrong element count and/or values truncated to 16 bits), the two index-buffer
// triangles either read garbage/out-of-range vertex data or collapse onto the inert filler vertices
// at (0,0,0) -- either way the quad will NOT render correctly at its expected on-screen position.
//
// Check A — screen centre (inside the quad): must be White.
// Check B — a point outside the quad's own bounds: must be the Blue clear colour, not White.
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

    void PutFloatAt(std::vector<std::uint8_t>& out, std::size_t offset, float v)
    {
        std::memcpy(out.data() + offset, &v, 4);
    }

    void AppendUint32(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }
}

class ModelJsonReader32BitIndicesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_model_json_reader_32bit_indices_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // 65540 VertexPositionNormalTexture vertices (stride 32): the first 65536 are inert
        // zeroed filler (never referenced by the index buffer, present purely to push the mesh's
        // vertex count past the 65535 IndexElementSize.SixteenBits ceiling); the last 4 (vertex
        // indices 65536-65539) are the same NDC [-0.5, 0.5] quad corners as the Task 927 test.
        constexpr int kNumVertices = 65540;
        constexpr int kStride = 32;
        std::vector<std::uint8_t> vertBytes(static_cast<std::size_t>(kNumVertices) * kStride, 0);

        struct V { float x, y, z, nx, ny, nz, u, v; };
        const V corners[4] = {
            { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
            { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
            {  0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
            {  0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        };
        constexpr int kFirstCornerVertex = 65536;
        for (int i = 0; i < 4; ++i) {
            const std::size_t o = static_cast<std::size_t>(kFirstCornerVertex + i) * kStride;
            const V& v = corners[i];
            PutFloatAt(vertBytes, o,      v.x);  PutFloatAt(vertBytes, o + 4,  v.y);
            PutFloatAt(vertBytes, o + 8,  v.z);
            PutFloatAt(vertBytes, o + 12, v.nx); PutFloatAt(vertBytes, o + 16, v.ny);
            PutFloatAt(vertBytes, o + 20, v.nz);
            PutFloatAt(vertBytes, o + 24, v.u);  PutFloatAt(vertBytes, o + 28, v.v);
        }
        std::ofstream vf(root / "bigquad_verts.bin", std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()),
                 static_cast<std::streamsize>(vertBytes.size()));
        vf.close();

        // Every index here requires the full 32 bits to represent (>= 65536) -- if the reader
        // still assumes 16-bit indices, these values truncate/misalign entirely.
        std::vector<std::uint8_t> idxBytes;
        for (std::uint32_t i : { 65536u, 65537u, 65538u, 65536u, 65538u, 65539u })
            AppendUint32(idxBytes, i);
        std::ofstream idxf(root / "bigquad_idx.bin", std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()),
                   static_cast<std::streamsize>(idxBytes.size()));
        idxf.close();

        WriteFile(root / "bigquad.cnj", R"({
  "cnjVersion": 1,
  "type": "Model",
  "meshes": [
    {
      "name": "BigQuad",
      "vertices": "bigquad_verts.bin",
      "indices": "bigquad_idx.bin",
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

        auto model = getContentProperty().Load<Model>("bigquad");

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
    ModelJsonReader32BitIndicesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    ModelJsonReader32BitIndicesTest game;
    game.Run();
    return game.getResult();
}
