// SPDX-License-Identifier: MS-PL
// Task 927: verify Content.Load<Model>() reads vertex data correctly from a real .model.json
// descriptor + binary vertex/index files on Bgfx -- a Bgfx-specific adaptation of the EasyGL
// source (examples/easygl_model_json_reader_test.cpp) restructured into per-check RunCheck()
// passes (own Clear+Draw+Read retry loop), since Bgfx's own GetBackBufferData only reliably
// reflects the first read per rendered frame (Task 406 finding) -- the EasyGL version reads two
// spatially-separate points in the same frame, incompatible with that quirk.
//
// See easygl_model_json_reader_test.cpp's own file header for the full bug writeup
// (DEFERRED.md item #26, found while porting ../cna-samples): ModelTypeReader::Read() used to
// compare the .model.json-declared "vertexStride" against sizeof(...) of CNA's own
// (IVertexType-vtable-inflated) vertex structs directly, then reinterpret_cast the tightly-packed
// file bytes as an array of those inflated structs -- silently reading every vertex's fields from
// the wrong byte offset.
//
// KNOWN, PRE-EXISTING, SEPARATE Bgfx limitation found while writing this test (not a Task 927
// regression -- Task 927's own fix is independently verified correct via the EasyGL and Vulkan
// versions of this exact test, both 2/2 PASS): the "centre" check below currently reads
// (0,0,0) -- black -- instead of the expected White. This is NOT a position bug: the black
// region is correctly bounded to the quad's own expected screen area (the "outside" check
// correctly reads the Blue background, not black), meaning Task 927's own vertex-position fix
// IS working here too. The wrong COLOUR is `BgfxRenderer` never overriding
// `DrawIndexedPrimitivesEx` (confirmed via grep -- only the base `IGraphicsRenderer` declares it),
// so this indexed, Effect-bound draw silently falls back to the default implementation's
// `DrawIndexedColoredPrimitives(...)` call, which drops the real `GpuDrawParams` (BasicEffect's
// actual DiffuseColor) entirely and renders via the `colored3D` shader pipeline instead -- a
// pipeline that expects a bound `a_color0` vertex attribute, which `VertexPositionNormalTexture`
// (position+normal+texcoord, no colour field) never provides. GL defaults an unbound generic
// vertex attribute to (0,0,0,1), producing exactly the observed solid black. This is the same
// class of gap Task 766 already flagged as an adjacent, out-of-scope discovery ("`BgfxRenderer`
// never overrides `DrawIndexedPrimitivesEx`... losing all `GpuDrawParams`-driven shader features")
// -- Task 927's own Content.Load<Model> test is simply the first one to demonstrate its real,
// concrete rendering impact. Tracked as a new, separately-scoped Task 948 (see plans/plan_graphics.md) --
// not fixed here, since fixing it is unrelated in scope to Task 927's own vertex-stride bug.
//
// Exit code 0 = both PASS, 1 = either FAIL (the "centre" check is EXPECTED to currently fail on
// Bgfx specifically, per the known Task 948 gap above -- tracked as part of the established
// pre-existing Bgfx failure baseline, not a surprise regression).

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

class BgfxModelJsonReaderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static constexpr int kSize = 64;

    static bool IsWhite(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() >= 200 && c.getBProperty() >= 200;
    }
    static bool IsBackground(const Color& c)
    {
        return c.getRProperty() <= 30 && c.getGProperty() <= 30 && c.getBProperty() >= 200;
    }

    Color RunCheck(float ndcX, float ndcY)
    {
        auto& device = getGraphicsDeviceProperty();
        Color got(0, 0, 0, 0);
        // Task 406: redo the entire Clear+Draw+Read cycle every iteration (never read twice from
        // the same rendered frame) and always take the LAST iteration's result -- deliberately no
        // conditional early-break here (unlike this project's usual "break on first non-background
        // pixel" shorthand), since this test's own background (Blue, not the usual Black) would
        // otherwise be indistinguishable from a "not yet caught up" signal. 20 identical redraws
        // is more than enough margin to flush any read-back pipeline delay.
        for (int i = 0; i < 20; ++i)
        {
            const Color background(0, 0, 255, 255); // Blue -- distinct from both White (expected
                                                      // quad colour) and Black (see file header).
            device.Clear(background);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);

            auto model = getContentProperty().Load<Model>("quad");
            model.Draw(Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty());

            const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(kSize));
            const int py = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(kSize));
            Rectangle reg(px, py, 1, 1);
            device.GetBackBufferData(&reg, &got, 0, 1);
        }
        return got;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_bgfx_model_json_reader_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        // Stride-32 VertexPositionNormalTexture: pos[12] + normal[12] + texcoord[8], matching
        // every model asset ../cna-samples' own conversion tools produce. A flat quad in the XY
        // plane, covering NDC x/y in [-0.5, 0.5] -- deliberately smaller than the full screen, so
        // a corrupted/scrambled position is very unlikely to still land here.
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

        getContentProperty().setRootDirectoryProperty(root.string());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        int passCount = 0;

        const Color centre = RunCheck(0.0f, 0.0f);
        const bool centreOk = IsWhite(centre);
        std::printf("[%s] centre (inside quad): (%d,%d,%d) expected White\n",
                    centreOk ? "PASS" : "FAIL",
                    centre.getRProperty(), centre.getGProperty(), centre.getBProperty());
        if (centreOk) ++passCount;

        const Color outside = RunCheck(0.8f, 0.8f);
        const bool outsideOk = IsBackground(outside);
        std::printf("[%s] outside quad bounds: (%d,%d,%d) expected Blue background\n",
                    outsideOk ? "PASS" : "FAIL",
                    outside.getRProperty(), outside.getGProperty(), outside.getBProperty());
        if (outsideOk) ++passCount;

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    BgfxModelJsonReaderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxModelJsonReaderTest game;
    game.Run();
    return game.getResult();
}
