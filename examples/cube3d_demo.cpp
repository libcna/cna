/**
 * @file cube3d_demo.cpp
 * @brief Minimal CNA 3D demo: a tiny blocky scene (ground, house, tree).
 *
 * This example exercises the freshly-added XNA-like 3D subset:
 *  - Vector3 / Matrix
 *  - VertexPositionColor
 *  - VertexBuffer / IndexBuffer
 *  - BasicEffect (matrices only)
 *  - GraphicsDevice 3D entry points
 *
 * It only renders correctly with the EasyGL graphics backend; the SDL_Renderer
 * and bgfx backends throw a clear "3D not supported" error from any 3D call.
 *
 * The scene is composed entirely from colored axis-aligned boxes built via
 * the demo-local `AddBox` / `AddGround` helpers. No textures, lighting, model
 * loading, or physics — just colored geometry.
 *
 * Controls:
 *  - W / S : move camera forward / backward
 *  - A / D : strafe left / right
 *  - Q / E : move down / up
 *  - Esc   : quit
 */

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

class Cube3DDemo final : public Game {
public:
    Cube3DDemo() = default;

    const std::string& GetTypeName() const override {
        static const std::string name = "Cube3DDemo";
        return name;
    }

protected:
    void Initialize() override {
        Game::Initialize();

        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(true);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;

        BuildScene(device);

        cameraPosition_ = Vector3(6.0f, 4.0f, 8.0f);
        cameraTarget_   = Vector3(0.0f, 1.0f, 0.0f);
    }

    void Update(GameTime& gameTime) override {
        Game::Update(gameTime);

        const float dt = static_cast<float>(
            gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

        const auto kb = Keyboard::GetState();
        if (kb.IsKeyDown(Keys::Escape)) { Exit(); }

        // Simple keyboard fly camera.
        Vector3 forward = Vector3::Normalize(cameraTarget_ - cameraPosition_);
        Vector3 right   = Vector3::Normalize(Vector3::Cross(forward, Vector3::Up));
        const float speed = 4.0f * dt;

        Vector3 move(0, 0, 0);
        if (kb.IsKeyDown(Keys::W)) move += forward * speed;
        if (kb.IsKeyDown(Keys::S)) move -= forward * speed;
        if (kb.IsKeyDown(Keys::D)) move += right   * speed;
        if (kb.IsKeyDown(Keys::A)) move -= right   * speed;
        if (kb.IsKeyDown(Keys::E)) move += Vector3::Up * speed;
        if (kb.IsKeyDown(Keys::Q)) move -= Vector3::Up * speed;

        cameraPosition_ += move;
        cameraTarget_   += move;
    }

    void Draw(const GameTime&) override {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(135, 196, 230, 255), 1.0f); // sky blue
        device.SetDepthTestEnabled(true);

        const auto& vp = device.getViewportProperty();
        const float aspect =
            (vp.getHeightProperty() > 0)
                ? static_cast<float>(vp.getWidthProperty()) /
                  static_cast<float>(vp.getHeightProperty())
                : 1.0f;

        effect_->View = Matrix::CreateLookAt(cameraPosition_, cameraTarget_, Vector3::Up);
        constexpr float kPiOver4 = 0.78539816339f;
        effect_->Projection = Matrix::CreatePerspectiveFieldOfView(
            kPiOver4, aspect, 0.1f, 200.0f);

        effect_->World = Matrix::Identity();

        // XNA 4.0-style draw flow:
        //   foreach (var pass in effect.CurrentTechnique.Passes) {
        //       pass.Apply();
        //       foreach (var mesh in meshes) {
        //           graphicsDevice.SetVertexBuffer(mesh.vb);
        //           graphicsDevice.Indices = mesh.ib;
        //           graphicsDevice.DrawIndexedPrimitives(...);
        //       }
        //   }
        for (auto& pass : effect_->CurrentTechnique().Passes()) {
            pass.Apply();
            for (const auto& m : meshes_) {
                device.SetVertexBuffer(m.vb.get());
                device.Indices(m.ib.get()); // XNA: graphicsDevice.Indices = ...
                const int numVertices = m.vb->VertexCount();
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList,
                    /*baseVertex=*/0,
                    /*minVertexIndex=*/0,
                    /*numVertices=*/numVertices,
                    /*startIndex=*/0,
                    /*primitiveCount=*/m.primitiveCount);
            }
        }
    }

private:
    // ------------------------------------------------------------------
    // Demo-local mesh container and helpers.
    // ------------------------------------------------------------------

    struct Mesh {
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<IndexBuffer>  ib;
        int primitiveCount = 0;
    };

    /**
     * @brief Adds a colored axis-aligned box centered at @p position.
     *
     * The box has its color baked into the vertices, so a single shared
     * `BasicEffect` with `World = Identity` renders every part of the scene.
     *
     * @param device    Graphics device to allocate the GPU buffers on.
     * @param position  World-space center of the box.
     * @param size      Full extents along X, Y, Z (not half-extents).
     * @param color     Solid color applied to all 6 faces.
     */
    void AddBox(GraphicsDevice& device,
                const Vector3& position,
                const Vector3& size,
                const Color& color) {
        const float hx = size.X * 0.5f;
        const float hy = size.Y * 0.5f;
        const float hz = size.Z * 0.5f;
        const float x0 = position.X - hx, x1 = position.X + hx;
        const float y0 = position.Y - hy, y1 = position.Y + hy;
        const float z0 = position.Z - hz, z1 = position.Z + hz;

        VertexPositionColor verts[24];
        // +X
        verts[0]  = { Vector3(x1, y0, z0), color };
        verts[1]  = { Vector3(x1, y1, z0), color };
        verts[2]  = { Vector3(x1, y1, z1), color };
        verts[3]  = { Vector3(x1, y0, z1), color };
        // -X
        verts[4]  = { Vector3(x0, y0, z1), color };
        verts[5]  = { Vector3(x0, y1, z1), color };
        verts[6]  = { Vector3(x0, y1, z0), color };
        verts[7]  = { Vector3(x0, y0, z0), color };
        // +Y
        verts[8]  = { Vector3(x0, y1, z0), color };
        verts[9]  = { Vector3(x0, y1, z1), color };
        verts[10] = { Vector3(x1, y1, z1), color };
        verts[11] = { Vector3(x1, y1, z0), color };
        // -Y
        verts[12] = { Vector3(x0, y0, z1), color };
        verts[13] = { Vector3(x0, y0, z0), color };
        verts[14] = { Vector3(x1, y0, z0), color };
        verts[15] = { Vector3(x1, y0, z1), color };
        // +Z
        verts[16] = { Vector3(x0, y0, z1), color };
        verts[17] = { Vector3(x1, y0, z1), color };
        verts[18] = { Vector3(x1, y1, z1), color };
        verts[19] = { Vector3(x0, y1, z1), color };
        // -Z
        verts[20] = { Vector3(x1, y0, z0), color };
        verts[21] = { Vector3(x0, y0, z0), color };
        verts[22] = { Vector3(x0, y1, z0), color };
        verts[23] = { Vector3(x1, y1, z0), color };

        std::uint16_t indices[36];
        for (std::uint16_t f = 0; f < 6; ++f) {
            const std::uint16_t b = static_cast<std::uint16_t>(f * 4);
            indices[f * 6 + 0] = b;
            indices[f * 6 + 1] = static_cast<std::uint16_t>(b + 1);
            indices[f * 6 + 2] = static_cast<std::uint16_t>(b + 2);
            indices[f * 6 + 3] = b;
            indices[f * 6 + 4] = static_cast<std::uint16_t>(b + 2);
            indices[f * 6 + 5] = static_cast<std::uint16_t>(b + 3);
        }

        Mesh mesh;
        mesh.vb = std::make_unique<VertexBuffer>(device, 24);
        mesh.vb->SetData(verts, 24);
        mesh.ib = std::make_unique<IndexBuffer>(device, 36);
        mesh.ib->SetData(indices, 36);
        mesh.primitiveCount = 12;
        meshes_.push_back(std::move(mesh));
    }

    /**
     * @brief Adds a flat colored ground quad on the Y = 0 plane.
     *
     * @param device  Graphics device to allocate the GPU buffers on.
     * @param size    Full edge length of the (square) ground tile.
     * @param color   Solid color of the ground.
     */
    void AddGround(GraphicsDevice& device, float size, const Color& color) {
        const float h = size * 0.5f;
        VertexPositionColor verts[4] = {
            { Vector3(-h, 0.0f, -h), color },
            { Vector3( h, 0.0f, -h), color },
            { Vector3( h, 0.0f,  h), color },
            { Vector3(-h, 0.0f,  h), color },
        };
        std::uint16_t indices[6] = { 0, 2, 1, 0, 3, 2 };

        Mesh mesh;
        mesh.vb = std::make_unique<VertexBuffer>(device, 4);
        mesh.vb->SetData(verts, 4);
        mesh.ib = std::make_unique<IndexBuffer>(device, 6);
        mesh.ib->SetData(indices, 6);
        mesh.primitiveCount = 2;
        meshes_.push_back(std::move(mesh));
    }

    void BuildScene(GraphicsDevice& device) {
        // Ground.
        AddGround(device, 24.0f, Color(70, 140, 70, 255));

        // -- House --------------------------------------------------------
        // House footprint: 4 x 3 (X x Z), walls 2.5 high, centered at origin.
        const Color wallColor (210, 190, 150, 255); // sand / plaster
        const Color doorColor ( 90,  55,  30, 255); // dark brown
        const Color roofColor (160,  60,  40, 255); // terracotta
        const Color baseColor (130, 110,  90, 255); // foundation

        const float houseW = 4.0f;   // along X
        const float houseD = 3.0f;   // along Z
        const float wallH  = 2.5f;
        const float wallT  = 0.2f;   // wall thickness
        const float baseH  = 0.2f;
        const float baseY  = baseH * 0.5f;
        const float wallY  = baseH + wallH * 0.5f;

        // Foundation slab: a hair larger than the footprint.
        AddBox(device,
               Vector3(0.0f, baseY, 0.0f),
               Vector3(houseW + 0.4f, baseH, houseD + 0.4f),
               baseColor);

        // Back wall (-Z, full).
        AddBox(device,
               Vector3(0.0f, wallY, -houseD * 0.5f + wallT * 0.5f),
               Vector3(houseW, wallH, wallT),
               wallColor);
        // Left wall (-X, full).
        AddBox(device,
               Vector3(-houseW * 0.5f + wallT * 0.5f, wallY, 0.0f),
               Vector3(wallT, wallH, houseD),
               wallColor);
        // Right wall (+X, full).
        AddBox(device,
               Vector3( houseW * 0.5f - wallT * 0.5f, wallY, 0.0f),
               Vector3(wallT, wallH, houseD),
               wallColor);

        // Front wall (+Z) with a door opening in the middle.
        // Door is 0.9 wide, 1.8 high; built as left segment, right segment,
        // and lintel above the opening. The opening itself is suggested by
        // a darker "door" box recessed slightly behind the wall plane.
        const float frontZ    = houseD * 0.5f - wallT * 0.5f;
        const float doorW     = 0.9f;
        const float doorH     = 1.8f;
        const float sideW     = (houseW - doorW) * 0.5f;
        const float lintelH   = wallH - doorH;
        const float doorCenterY = baseH + doorH * 0.5f;
        const float lintelY     = baseH + doorH + lintelH * 0.5f;

        // Left front segment.
        AddBox(device,
               Vector3(-houseW * 0.5f + sideW * 0.5f, wallY, frontZ),
               Vector3(sideW, wallH, wallT),
               wallColor);
        // Right front segment.
        AddBox(device,
               Vector3( houseW * 0.5f - sideW * 0.5f, wallY, frontZ),
               Vector3(sideW, wallH, wallT),
               wallColor);
        // Lintel above the door.
        AddBox(device,
               Vector3(0.0f, lintelY, frontZ),
               Vector3(doorW, lintelH, wallT),
               wallColor);
        // Door panel (recessed slightly toward the interior).
        AddBox(device,
               Vector3(0.0f, doorCenterY, frontZ - wallT * 0.6f),
               Vector3(doorW * 0.95f, doorH * 0.98f, wallT * 0.5f),
               doorColor);

        // -- Roof: stepped boxes approximating a gable -----------------
        // Three stacked, progressively narrower slabs along Z give a simple
        // "pyramid-ish" silhouette without needing a real prism.
        const float roofBaseY = baseH + wallH;
        const float step      = 0.35f;
        const float layerH    = 0.35f;
        for (int i = 0; i < 3; ++i) {
            const float shrink = step * static_cast<float>(i);
            const float w = houseW + 0.3f - shrink * 2.0f;
            const float d = houseD + 0.3f - shrink * 2.0f;
            const float y = roofBaseY + layerH * (static_cast<float>(i) + 0.5f);
            AddBox(device,
                   Vector3(0.0f, y, 0.0f),
                   Vector3(w, layerH, d),
                   roofColor);
        }

        // -- Tree --------------------------------------------------------
        // Trunk (brown box) + crown (green box) off to the side of the house.
        const Color trunkColor(110,  70,  35, 255);
        const Color crownColor( 50, 140,  55, 255);
        const Vector3 treeBase(4.5f, 0.0f, 1.5f);
        const float trunkH = 1.6f;
        AddBox(device,
               treeBase + Vector3(0.0f, trunkH * 0.5f, 0.0f),
               Vector3(0.4f, trunkH, 0.4f),
               trunkColor);
        AddBox(device,
               treeBase + Vector3(0.0f, trunkH + 0.7f, 0.0f),
               Vector3(1.6f, 1.4f, 1.6f),
               crownColor);
    }

    std::unique_ptr<BasicEffect> effect_;
    std::vector<Mesh>            meshes_;

    Vector3 cameraPosition_{0.0f, 2.0f, 5.0f};
    Vector3 cameraTarget_  {0.0f, 0.0f, 0.0f};
};

int main() {
    Cube3DDemo game;
    game.Run();
    return 0;
}
