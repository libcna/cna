/**
 * @file cube3d_demo.cpp
 * @brief CNA 3D demo: walkable colored-box scene with first-person camera.
 *
 * Exercises the XNA-like 3D subset (Vector3 / Matrix / VertexPositionColor /
 * VertexBuffer / IndexBuffer / BasicEffect / GraphicsDevice) with the EasyGL
 * backend. SDL_Renderer and bgfx still throw a clear "3D not supported" error.
 *
 * The scene is composed entirely from colored axis-aligned boxes built by the
 * demo-local AddBox / AddGround / AddFence / AddTree / AddPath helpers. No
 * textures, no lighting, no model loading.
 *
 * Controls:
 *  - W / S            : move forward / backward
 *  - A / D            : strafe left / right
 *  - Q / E            : move down / up
 *  - Left/Right arrow : yaw   (look left / right)
 *  - Up/Down arrow    : pitch (look up   / down)
 *  - Esc              : quit
 *
 * @note Collision is intentionally NOT implemented (TODO). The camera flies
 *       through walls; the geometry is laid out so doors/openings exist for
 *       visual navigation.
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

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace {
constexpr float kPi       = 3.14159265358979323846f;
constexpr float kPiOver4  = kPi * 0.25f;
constexpr float kDeg2Rad  = kPi / 180.0f;
} // namespace

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

        // Start outside the house, looking at the front door.
        cameraPosition_ = Vector3(0.0f, 1.7f, 9.0f);
        yaw_   = kPi;          // facing -Z (toward the house)
        pitch_ = -5.0f * kDeg2Rad;
        UpdateCameraTarget();
    }

    void Update(GameTime& gameTime) override {
        Game::Update(gameTime);

        const float dt = static_cast<float>(
            gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

        const auto kb = Keyboard::GetState();
        if (kb.IsKeyDown(Keys::Escape)) { Exit(); }

        // --- Rotation (arrow-key fallback; mouse not wired through CNA yet). --
        const float rotSpeed = 1.6f * dt; // rad/s
        if (kb.IsKeyDown(Keys::Left))  yaw_   += rotSpeed;
        if (kb.IsKeyDown(Keys::Right)) yaw_   -= rotSpeed;
        if (kb.IsKeyDown(Keys::Up))    pitch_ += rotSpeed;
        if (kb.IsKeyDown(Keys::Down))  pitch_ -= rotSpeed;

        // Clamp pitch to avoid gimbal flip.
        const float pitchLimit = 80.0f * kDeg2Rad;
        if (pitch_ >  pitchLimit) pitch_ =  pitchLimit;
        if (pitch_ < -pitchLimit) pitch_ = -pitchLimit;

        // Forward / right derived from yaw+pitch (right-handed, Y-up).
        const float cp = std::cos(pitch_);
        const float sp = std::sin(pitch_);
        const float cy = std::cos(yaw_);
        const float sy = std::sin(yaw_);
        const Vector3 forward(cp * sy, sp, cp * cy * -1.0f);
        Vector3 right = Vector3::Normalize(Vector3::Cross(forward, Vector3::Up));

        // --- Movement ---------------------------------------------------------
        const float speed = 5.0f * dt;
        Vector3 move(0, 0, 0);
        if (kb.IsKeyDown(Keys::W)) move += forward * speed;
        if (kb.IsKeyDown(Keys::S)) move -= forward * speed;
        if (kb.IsKeyDown(Keys::D)) move += right   * speed;
        if (kb.IsKeyDown(Keys::A)) move -= right   * speed;
        if (kb.IsKeyDown(Keys::E)) move += Vector3::Up * speed;
        if (kb.IsKeyDown(Keys::Q)) move -= Vector3::Up * speed;

        // TODO: Collision not implemented. Camera passes freely through walls.
        cameraPosition_ += move;
        UpdateCameraTarget();
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

        effect_->View       = Matrix::CreateLookAt(cameraPosition_, cameraTarget_, Vector3::Up);
        effect_->Projection = Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 300.0f);
        effect_->World      = Matrix::Identity();

        // XNA 4.0-style draw flow.
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

    void UpdateCameraTarget() {
        const float cp = std::cos(pitch_);
        const float sp = std::sin(pitch_);
        const float cy = std::cos(yaw_);
        const float sy = std::sin(yaw_);
        // forward = (cos(pitch)*sin(yaw), sin(pitch), -cos(pitch)*cos(yaw))
        const Vector3 forward(cp * sy, sp, -cp * cy);
        cameraTarget_ = cameraPosition_ + forward;
    }

    /**
     * @brief Adds a colored axis-aligned box centered at @p position.
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
        verts[0]={Vector3(x1,y0,z0),color}; verts[1]={Vector3(x1,y1,z0),color};
        verts[2]={Vector3(x1,y1,z1),color}; verts[3]={Vector3(x1,y0,z1),color};
        // -X
        verts[4]={Vector3(x0,y0,z1),color}; verts[5]={Vector3(x0,y1,z1),color};
        verts[6]={Vector3(x0,y1,z0),color}; verts[7]={Vector3(x0,y0,z0),color};
        // +Y
        verts[8]={Vector3(x0,y1,z0),color}; verts[9]={Vector3(x0,y1,z1),color};
        verts[10]={Vector3(x1,y1,z1),color};verts[11]={Vector3(x1,y1,z0),color};
        // -Y
        verts[12]={Vector3(x0,y0,z1),color};verts[13]={Vector3(x0,y0,z0),color};
        verts[14]={Vector3(x1,y0,z0),color};verts[15]={Vector3(x1,y0,z1),color};
        // +Z
        verts[16]={Vector3(x0,y0,z1),color};verts[17]={Vector3(x1,y0,z1),color};
        verts[18]={Vector3(x1,y1,z1),color};verts[19]={Vector3(x0,y1,z1),color};
        // -Z
        verts[20]={Vector3(x1,y0,z0),color};verts[21]={Vector3(x0,y0,z0),color};
        verts[22]={Vector3(x0,y1,z0),color};verts[23]={Vector3(x1,y1,z0),color};

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

    /** @brief Adds a flat colored ground quad on the Y = 0 plane. */
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

    /** @brief Adds a fence: a row of thin posts between two endpoints. */
    void AddFence(GraphicsDevice& device,
                  const Vector3& from, const Vector3& to,
                  float postSpacing, float postHeight,
                  const Color& postColor, const Color& railColor) {
        const Vector3 d = to - from;
        const float len = std::sqrt(d.X * d.X + d.Z * d.Z);
        if (len < 1e-3f) return;
        const Vector3 dir(d.X / len, 0.0f, d.Z / len);
        const int posts = static_cast<int>(len / postSpacing) + 1;
        for (int i = 0; i < posts; ++i) {
            const float t = static_cast<float>(i) * postSpacing;
            const Vector3 p = from + dir * t;
            AddBox(device,
                   Vector3(p.X, postHeight * 0.5f, p.Z),
                   Vector3(0.1f, postHeight, 0.1f),
                   postColor);
        }
        // Two horizontal rails.
        const Vector3 mid = (from + to) * 0.5f;
        const float yaw = std::atan2(dir.X, dir.Z);
        // Use axis-aligned approximation: only build rails if from/to are axis-aligned.
        const bool axisX = std::abs(d.Z) < 1e-3f;
        const bool axisZ = std::abs(d.X) < 1e-3f;
        const Vector3 railSize = axisX
            ? Vector3(len, 0.05f, 0.05f)
            : axisZ ? Vector3(0.05f, 0.05f, len)
                    : Vector3(0.05f, 0.05f, 0.05f);
        if (axisX || axisZ) {
            AddBox(device, Vector3(mid.X, postHeight * 0.75f, mid.Z), railSize, railColor);
            AddBox(device, Vector3(mid.X, postHeight * 0.35f, mid.Z), railSize, railColor);
        }
        (void)yaw;
    }

    /** @brief Adds a tree (trunk + crown) at base position. */
    void AddTree(GraphicsDevice& device, const Vector3& base,
                 float trunkH, float crownSize) {
        const Color trunkColor(110, 70, 35, 255);
        const Color crownColor( 50,140, 55, 255);
        AddBox(device,
               base + Vector3(0.0f, trunkH * 0.5f, 0.0f),
               Vector3(0.4f, trunkH, 0.4f),
               trunkColor);
        AddBox(device,
               base + Vector3(0.0f, trunkH + crownSize * 0.5f, 0.0f),
               Vector3(crownSize, crownSize, crownSize),
               crownColor);
    }

    /** @brief Adds a flat path tile slightly above the ground. */
    void AddPath(GraphicsDevice& device, const Vector3& center,
                 const Vector3& size, const Color& color) {
        AddBox(device, Vector3(center.X, 0.025f, center.Z),
               Vector3(size.X, 0.05f, size.Z), color);
    }

    void BuildScene(GraphicsDevice& device) {
        // ---- Garden ground ------------------------------------------------
        AddGround(device, 60.0f, Color(70, 140, 70, 255));

        // Subtle terrain variation: a couple of low grass patches.
        AddBox(device, Vector3(-10.0f, 0.05f,  6.0f), Vector3(4.0f, 0.10f, 3.0f), Color(80,150,75,255));
        AddBox(device, Vector3( 12.0f, 0.05f, -8.0f), Vector3(5.0f, 0.10f, 4.0f), Color(85,155,80,255));

        // Colors used across the house.
        const Color wallColor (210, 190, 150, 255);
        const Color innerWall (200, 180, 140, 255);
        const Color floorColor(170, 140, 100, 255);
        const Color doorColor ( 90,  55,  30, 255);
        const Color roofColor (160,  60,  40, 255);
        const Color baseColor (130, 110,  90, 255);
        const Color stoneColor(120, 120, 130, 255);

        // ---- House dimensions --------------------------------------------
        // Footprint: 8 (X) x 6 (Z), centered at origin. Two floors of 2.5 each.
        const float W = 8.0f, D = 6.0f;
        const float floorH = 2.5f;
        const float wallT  = 0.2f;
        const float baseH  = 0.3f;
        const float baseY  = baseH * 0.5f;
        const float f1Y    = baseH + floorH * 0.5f;       // 1st floor wall mid
        const float f2Y    = baseH + floorH * 1.5f;       // 2nd floor wall mid
        const float ceilY  = baseH + floorH;              // 1st floor ceiling top
        const float roofY  = baseH + floorH * 2.0f;       // top of 2nd floor walls

        // Foundation slab.
        AddBox(device, Vector3(0.0f, baseY, 0.0f),
               Vector3(W + 0.4f, baseH, D + 0.4f), baseColor);

        // ---- Basement (a darker pit suggestion under the slab) -----------
        // We can't actually dig into ground geometry, so we expose a basement
        // "stairwell" opening on the back-right corner: a dark sunken box
        // that suggests stairs going down. The player can fly into it.
        AddBox(device, Vector3(W * 0.5f - 1.0f, -0.6f, -D * 0.5f + 1.0f),
               Vector3(1.6f, 1.2f, 1.6f), Color(40, 35, 30, 255));
        // Stone steps descending into the basement opening.
        for (int i = 0; i < 4; ++i) {
            const float y = -0.15f - i * 0.20f;
            const float zOff = -D * 0.5f + 0.4f + i * 0.2f;
            AddBox(device, Vector3(W * 0.5f - 1.0f, y, zOff),
                   Vector3(1.4f, 0.10f, 0.2f), stoneColor);
        }

        // ---- 1st-floor outer walls ---------------------------------------
        // Back wall (-Z).
        AddBox(device, Vector3(0.0f, f1Y, -D * 0.5f + wallT * 0.5f),
               Vector3(W, floorH, wallT), wallColor);
        // Left (-X).
        AddBox(device, Vector3(-W * 0.5f + wallT * 0.5f, f1Y, 0.0f),
               Vector3(wallT, floorH, D), wallColor);
        // Right (+X).
        AddBox(device, Vector3( W * 0.5f - wallT * 0.5f, f1Y, 0.0f),
               Vector3(wallT, floorH, D), wallColor);

        // Front wall (+Z) with door opening centered at X = -1.5.
        const float frontZ = D * 0.5f - wallT * 0.5f;
        const float doorW = 1.0f, doorH = 2.0f;
        const float doorCX = -1.5f;
        const float lintelH = floorH - doorH;
        const float doorCY  = baseH + doorH * 0.5f;
        const float lintelY = baseH + doorH + lintelH * 0.5f;
        const float leftSegW  = (doorCX - doorW * 0.5f) - (-W * 0.5f);
        const float rightSegW = ( W * 0.5f) - (doorCX + doorW * 0.5f);
        // Left segment.
        AddBox(device,
               Vector3(-W * 0.5f + leftSegW * 0.5f, f1Y, frontZ),
               Vector3(leftSegW, floorH, wallT), wallColor);
        // Right segment.
        AddBox(device,
               Vector3( W * 0.5f - rightSegW * 0.5f, f1Y, frontZ),
               Vector3(rightSegW, floorH, wallT), wallColor);
        // Lintel above door.
        AddBox(device, Vector3(doorCX, lintelY, frontZ),
               Vector3(doorW, lintelH, wallT), wallColor);
        // Door panel (open feel: thin recessed dark slab).
        AddBox(device, Vector3(doorCX, doorCY, frontZ - wallT * 0.6f),
               Vector3(doorW * 0.95f, doorH * 0.98f, wallT * 0.5f), doorColor);

        // ---- Interior partition (split 1st floor into 2 rooms) -----------
        // Interior wall along X = 1.0, with an opening (no door) from Z=-0.5..1.0
        // so the player can walk between the rooms.
        const float partX = 1.0f;
        // Segment from -Z wall to opening.
        AddBox(device,
               Vector3(partX, f1Y, (-D * 0.5f + (-0.5f)) * 0.5f),
               Vector3(wallT, floorH, (-0.5f) - (-D * 0.5f)),
               innerWall);
        // Segment from opening to +Z wall.
        AddBox(device,
               Vector3(partX, f1Y, (1.0f + D * 0.5f) * 0.5f),
               Vector3(wallT, floorH, D * 0.5f - 1.0f),
               innerWall);

        // ---- Floor between 1st and 2nd story (with stair hole) ----------
        // We split the ceiling into pieces around the stairwell at X=2..3.5, Z=-2..-0.5.
        // For simplicity build it as 4 surrounding slabs.
        const float fT = 0.10f; // floor thickness
        const float ceilCY = ceilY + fT * 0.5f;
        // Strip in front of stairwell (positive Z side).
        AddBox(device, Vector3(0.0f, ceilCY, ( -0.5f + D * 0.5f) * 0.5f),
               Vector3(W, fT, D * 0.5f - (-0.5f)), floorColor);
        // Strip behind stairwell.
        AddBox(device, Vector3(0.0f, ceilCY, (-2.0f + (-D * 0.5f)) * 0.5f),
               Vector3(W, fT, (-D * 0.5f) - (-2.0f) * -1.0f * 0.0f + ( -2.0f + D * 0.5f )),
               floorColor);
        // Left strip beside stairwell.
        AddBox(device, Vector3((2.0f + (-W * 0.5f)) * 0.5f, ceilCY, -1.25f),
               Vector3(2.0f - (-W * 0.5f), fT, 1.5f), floorColor);
        // Right strip beside stairwell.
        AddBox(device, Vector3((3.5f + W * 0.5f) * 0.5f, ceilCY, -1.25f),
               Vector3(W * 0.5f - 3.5f, fT, 1.5f), floorColor);

        // ---- Stairs to 2nd floor (stacked boxes) -------------------------
        // 8 steps from y=baseH up to y=ceilY, at X around 2.75, Z descending.
        const int   stepCount = 8;
        const float stepRise  = (ceilY - baseH) / stepCount;
        const float stepRun   = 0.30f;
        for (int i = 0; i < stepCount; ++i) {
            const float sy = baseH + stepRise * (static_cast<float>(i) + 0.5f);
            const float sz = -0.5f - stepRun * (static_cast<float>(i) + 0.5f);
            AddBox(device, Vector3(2.75f, sy, sz),
                   Vector3(1.5f, stepRise, stepRun), stoneColor);
        }

        // ---- 2nd-floor outer walls ---------------------------------------
        // Same footprint, with no front-door but a balcony opening at front.
        AddBox(device, Vector3(0.0f, f2Y, -D * 0.5f + wallT * 0.5f),
               Vector3(W, floorH, wallT), wallColor);
        AddBox(device, Vector3(-W * 0.5f + wallT * 0.5f, f2Y, 0.0f),
               Vector3(wallT, floorH, D), wallColor);
        AddBox(device, Vector3( W * 0.5f - wallT * 0.5f, f2Y, 0.0f),
               Vector3(wallT, floorH, D), wallColor);
        // Front wall with a balcony "doorway" centered at X=2.0.
        const float balDoorW = 1.2f, balDoorH = 2.0f;
        const float balCX = 2.0f;
        const float balLintelH = floorH - balDoorH;
        const float balLintelY = baseH + floorH + balDoorH + balLintelH * 0.5f;
        const float balLeftW  = (balCX - balDoorW * 0.5f) - (-W * 0.5f);
        const float balRightW = ( W * 0.5f) - (balCX + balDoorW * 0.5f);
        AddBox(device,
               Vector3(-W * 0.5f + balLeftW * 0.5f, f2Y, frontZ),
               Vector3(balLeftW, floorH, wallT), wallColor);
        AddBox(device,
               Vector3( W * 0.5f - balRightW * 0.5f, f2Y, frontZ),
               Vector3(balRightW, floorH, wallT), wallColor);
        AddBox(device, Vector3(balCX, balLintelY, frontZ),
               Vector3(balDoorW, balLintelH, wallT), wallColor);

        // ---- Roof: 3 stepped slabs ---------------------------------------
        const float layerH = 0.35f;
        for (int i = 0; i < 3; ++i) {
            const float shrink = 0.6f * static_cast<float>(i);
            const float w = W + 0.4f - shrink;
            const float d = D + 0.4f - shrink;
            const float y = roofY + layerH * (static_cast<float>(i) + 0.5f);
            AddBox(device, Vector3(0.0f, y, 0.0f),
                   Vector3(w, layerH, d), roofColor);
        }

        // ---- Balcony (platform + railings outside front wall) ------------
        const float balPlatY = baseH + floorH; // sits on top of 1st-floor ceiling
        AddBox(device,
               Vector3(balCX, balPlatY + 0.05f, frontZ + 0.6f),
               Vector3(2.5f, 0.10f, 1.2f), stoneColor);
        // Railings (front + 2 sides).
        AddBox(device, Vector3(balCX, balPlatY + 0.45f, frontZ + 1.2f),
               Vector3(2.5f, 0.7f, 0.08f), wallColor);
        AddBox(device, Vector3(balCX - 1.25f, balPlatY + 0.45f, frontZ + 0.6f),
               Vector3(0.08f, 0.7f, 1.2f), wallColor);
        AddBox(device, Vector3(balCX + 1.25f, balPlatY + 0.45f, frontZ + 0.6f),
               Vector3(0.08f, 0.7f, 1.2f), wallColor);

        // ---- Sidewalk / path from front door out into the garden ---------
        const Color pathColor(190, 185, 170, 255);
        AddPath(device, Vector3(doorCX, 0.0f, frontZ + 1.5f),
                Vector3(1.4f, 0.0f, 1.6f), pathColor);
        AddPath(device, Vector3(doorCX, 0.0f, frontZ + 3.5f),
                Vector3(1.4f, 0.0f, 2.4f), pathColor);
        AddPath(device, Vector3(doorCX, 0.0f, frontZ + 6.0f),
                Vector3(1.4f, 0.0f, 2.6f), pathColor);

        // ---- Fence around part of the garden -----------------------------
        const Color postCol(140, 110, 70, 255);
        const Color railCol(160, 130, 90, 255);
        // Front-left run (along +X) from x=-12 to x=-3 at z=10.
        AddFence(device, Vector3(-12.0f, 0.0f, 10.0f),
                 Vector3( -3.0f, 0.0f, 10.0f), 0.8f, 1.1f, postCol, railCol);
        // Left side run (along -Z) from z=10 to z=-2 at x=-12.
        AddFence(device, Vector3(-12.0f, 0.0f, 10.0f),
                 Vector3(-12.0f, 0.0f, -2.0f), 0.8f, 1.1f, postCol, railCol);

        // ---- Trees (multiple) --------------------------------------------
        AddTree(device, Vector3( 6.0f, 0.0f,  4.0f), 1.8f, 1.8f);
        AddTree(device, Vector3(-6.0f, 0.0f,  6.5f), 1.6f, 1.6f);
        AddTree(device, Vector3(-9.0f, 0.0f, -3.0f), 2.0f, 2.0f);
        AddTree(device, Vector3( 9.0f, 0.0f, -6.0f), 1.5f, 1.5f);
    }

    std::unique_ptr<BasicEffect> effect_;
    std::vector<Mesh>            meshes_;

    Vector3 cameraPosition_{0.0f, 1.7f, 9.0f};
    Vector3 cameraTarget_  {0.0f, 1.0f, 0.0f};
    float   yaw_   = 3.14159265f; // radians, 0 = looking toward -Z is convention-dependent
    float   pitch_ = 0.0f;
};

int main() {
    Cube3DDemo game;
    game.Run();
    return 0;
}
