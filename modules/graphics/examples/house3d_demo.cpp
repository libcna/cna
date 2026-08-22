/**
 * @file house3d_demo.cpp
 * @brief CNA 3D demo: walkable colored-box scene with first-person camera.
 *
 * Exercises the XNA-like 3D subset (Vector3 / Matrix / VertexPositionColor /
 * VertexBuffer / IndexBuffer / BasicEffect / GraphicsDevice) with the EasyGL
 * renderer. SDL_Renderer and bgfx still throw a clear "3D not supported" error.
 *
 * The scene is composed entirely from colored axis-aligned boxes built by the
 * demo-local AddBox / AddGround / AddFence / AddTree / AddPath helpers. No
 * textures, no lighting, no model loading.
 *
 * Two camera / physics modes, toggled with Tab:
 *
 * Game mode (default):
 *  - W / Up arrow     : move forward (horizontal)
 *  - S / Down arrow   : move backward (horizontal)
 *  - A / D            : strafe left / right
 *  - Left/Right arrow : yaw (look left / right)
 *  - Page Up/Down     : pitch (look up / down)
 *  - Space            : jump
 *  - Tab              : switch to Fly mode
 *  - Esc              : quit
 *
 * Fly mode:
 *  - W / S            : move forward / backward (full 3-D, follows pitch)
 *  - A / D            : strafe left / right
 *  - Q / E            : move down / up
 *  - Left/Right arrow : yaw (look left / right)
 *  - Up/Down arrow    : pitch (look up / down)
 *  - Tab              : switch to Game mode
 *  - Esc              : quit
 *
 * @note AABB collision against solid boxes prevents walking through walls in
 *       both modes. In Game mode, gravity and a ground-plane floor are also
 *       applied. Decorative pieces (windows, paths, balcony, ceilings, tree
 *       crowns …) are non-solid; the foundation slab and inner walls carry
 *       solid colliders.
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
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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
constexpr float kPi         = 3.14159265358979323846f;
constexpr float kPiOver4    = kPi * 0.25f;
constexpr float kDeg2Rad    = kPi / 180.0f;
constexpr float kGravity       = 25.0f;
constexpr float kJumpSpeed     =  7.0f;
constexpr float kGroundEyeY    =  1.70f;
constexpr float kMaxStepHeight =  0.55f;
constexpr float kOverlaySeconds = 8.0f; // how long the controls overlay is shown

// ---------------------------------------------------------------------------
// Minimal 8×8 bitmap font — ASCII 0x20..0x7E (95 glyphs, public domain).
// kFont8x8[ch - 0x20][row]: bit 7 = leftmost pixel of that row.
// ---------------------------------------------------------------------------
constexpr std::uint8_t kFont8x8[95][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 0x21 !
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x22 "
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 0x23 #
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 0x24 $
  {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 0x25 %
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 0x26 &
  {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 0x27 '
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 0x28 (
  {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 0x29 )
  {0x00,0x36,0x1C,0x7F,0x1C,0x36,0x00,0x00}, // 0x2A *
  {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 0x2B +
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 0x2C ,
  {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 0x2D -
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 0x2E .
  {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 0x2F /
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 0x30 0
  {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 0x31 1
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 0x32 2
  {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 0x33 3
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 0x34 4
  {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 0x35 5
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 0x36 6
  {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 0x37 7
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 0x38 8
  {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 0x39 9
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 0x3A :
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 0x3B ;
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 0x3C <
  {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 0x3D =
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 0x3E >
  {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 0x3F ?
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 0x40 @
  {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 0x41 A
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 0x42 B
  {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 0x43 C
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 0x44 D
  {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 0x45 E
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 0x46 F
  {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 0x47 G
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 0x48 H
  {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 0x49 I
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 0x4A J
  {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 0x4B K
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 0x4C L
  {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 0x4D M
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 0x4E N
  {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 0x4F O
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 0x50 P
  {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 0x51 Q
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 0x52 R
  {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 0x53 S
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 0x54 T
  {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 0x55 U
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 0x56 V
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 0x57 W
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 0x58 X
  {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 0x59 Y
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 0x5A Z
  {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 0x5B [
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 0x5C backslash
  {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 0x5D ]
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 0x5E ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 0x5F _
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 0x60 `
  {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 0x61 a
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 0x62 b
  {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 0x63 c
  {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 0x64 d
  {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 0x65 e
  {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 0x66 f
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 0x67 g
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 0x68 h
  {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 0x69 i
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 0x6A j
  {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 0x6B k
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 0x6C l
  {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 0x6D m
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 0x6E n
  {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 0x6F o
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 0x70 p
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 0x71 q
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 0x72 r
  {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 0x73 s
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 0x74 t
  {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 0x75 u
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 0x76 v
  {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 0x77 w
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 0x78 x
  {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 0x79 y
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 0x7A z
  {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 0x7B {
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 0x7C |
  {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 0x7D }
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7E ~
};

// Renders ASCII text into an RGBA pixel buffer using the 8×8 bitmap font.
// Characters outside 0x20-0x7E are skipped; '\n' advances to the next line.
void FontDrawText(std::vector<std::uint8_t>& buf, int bufW, int bufH,
                  int x, int y, const char* text,
                  std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { cx = x; y += 10; continue; } // 10 = 8px char + 2px gap
        const int idx = static_cast<unsigned char>(*p) - 0x20;
        if (idx < 0 || idx >= 95) { cx += 8; continue; }
        const auto* g8 = kFont8x8[idx];
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                if (g8[row] & (1u << col)) {
                    const int px = cx + col, py = y + row;
                    if (px < 0 || px >= bufW || py < 0 || py >= bufH) continue;
                    const int off = (py * bufW + px) * 4;
                    buf[off+0] = r; buf[off+1] = g;
                    buf[off+2] = b; buf[off+3] = a;
                }
            }
        }
        cx += 8;
    }
}
} // namespace

class House3DDemo final : public Game {
public:
    House3DDemo()
    {
        static constexpr int FPS = 60;
        Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
    };

    const std::string& GetTypeName() const override {
        static const std::string name = "House3DDemo";
        return name;
    }

protected:
    void Initialize() override {
        Game::Initialize();

        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(true);
        // Task 896 finding: AddBox()'s per-face winding is CCW/back-facing under CNA's real
        // default RasterizerState on every axis-aligned face spot-checked (+X/+Z/-Z) — the
        // whole scene would otherwise render as an empty sky once the correct default lands.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;

        BuildScene(device);

        // Start outside the house, looking at the front door.
        cameraPosition_ = Vector3(0.0f, kGroundEyeY, 9.0f);
        yaw_   = 0.0f;
        pitch_ = -5.0f * kDeg2Rad;
        UpdateCameraTarget();

        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        overlayTexture_ = BuildOverlayTexture(device);
        overlayTimer_   = kOverlaySeconds;
    }

    void Update(GameTime& gameTime) override {
        Game::Update(gameTime);

        if (smokeFramesLeft_ > 0)
        {
            if (--smokeFramesLeft_ == 0) { Exit(); return; }
        }

        const float dt = static_cast<float>(
            gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

        // Countdown for the controls overlay.
        if (overlayTimer_ > 0.0f) overlayTimer_ -= dt;

        const auto kb = Keyboard::GetState();
        if (kb.IsKeyDown(Keys::Escape)) { Exit(); }

        // --- Tab: toggle game / fly mode (edge-triggered) ---------------------
        const bool tabDown = kb.IsKeyDown(Keys::Tab);
        if (tabDown && !tabWasDown_) {
            gameMode_ = !gameMode_;
            if (gameMode_) {
                velocityY_ = 0.0f; // shed any accumulated vertical speed
            }
        }
        tabWasDown_ = tabDown;

        // --- Yaw: same in both modes ------------------------------------------
        const float rotSpeed = 1.6f * dt;
        if (kb.IsKeyDown(Keys::Left))  yaw_ -= rotSpeed;
        if (kb.IsKeyDown(Keys::Right)) yaw_ += rotSpeed;

        // --- Pitch: Page Up/Down in game mode, Up/Down arrows in fly mode -----
        if (gameMode_) {
            if (kb.IsKeyDown(Keys::PageUp))   pitch_ += rotSpeed;
            if (kb.IsKeyDown(Keys::PageDown)) pitch_ -= rotSpeed;
        } else {
            if (kb.IsKeyDown(Keys::Up))   pitch_ += rotSpeed;
            if (kb.IsKeyDown(Keys::Down)) pitch_ -= rotSpeed;
        }
        const float pitchLimit = 80.0f * kDeg2Rad;
        if (pitch_ >  pitchLimit) pitch_ =  pitchLimit;
        if (pitch_ < -pitchLimit) pitch_ = -pitchLimit;

        // Camera vectors derived from current yaw + pitch.
        const float cp = std::cos(pitch_);
        const float sp = std::sin(pitch_);
        const float cy = std::cos(yaw_);
        const float sy = std::sin(yaw_);
        const Vector3 forward(cp * sy, sp, -cp * cy);
        const Vector3 right = Vector3::Normalize(Vector3::Cross(forward, Vector3::Up));

        const float speed = 5.0f * dt;

        if (gameMode_) {
            // ---- Game mode: gravity + jump + step-up stair climbing ----------
            // Horizontal movement vectors (yaw only, no pitch influence).
            // forwardH = (sin(yaw), 0, -cos(yaw)),  rightH = (cos(yaw), 0, sin(yaw))
            const Vector3 forwardH(sy, 0.0f, -cy);
            const Vector3 rightH  (cy, 0.0f,  sy);

            Vector3 move(0.0f, 0.0f, 0.0f);
            if (kb.IsKeyDown(Keys::W) || kb.IsKeyDown(Keys::Up))   move += forwardH * speed;
            if (kb.IsKeyDown(Keys::S) || kb.IsKeyDown(Keys::Down)) move -= forwardH * speed;
            if (kb.IsKeyDown(Keys::D)) move += rightH * speed;
            if (kb.IsKeyDown(Keys::A)) move -= rightH * speed;

            // Jump (only when on solid ground or step).
            if (kb.IsKeyDown(Keys::Space) && onGround_) {
                velocityY_ = kJumpSpeed;
                onGround_  = false;
            }

            // Gravity — only when airborne to avoid oscillation on flat surfaces.
            if (!onGround_) {
                velocityY_ -= kGravity * dt;
            }

            // --- Axis-separated horizontal movement with step-up --------------
            Vector3 candidate = cameraPosition_;

            // X axis
            candidate.X += move.X;
            if (CollidesWithSolid(candidate)) {
                const float stepTop    = GetBlockingColliderTopY(candidate);
                const float stepEyeY   = stepTop + kPlayerEyeToBottom;
                if (stepTop >= 0.0f && stepEyeY <= cameraPosition_.Y + kMaxStepHeight) {
                    candidate.Y = stepEyeY;  // step up
                    velocityY_  = 0.0f;
                    onGround_   = true;
                } else {
                    candidate.X = cameraPosition_.X;  // wall, blocked
                }
            }

            // Z axis
            candidate.Z += move.Z;
            if (CollidesWithSolid(candidate)) {
                const float stepTop  = GetBlockingColliderTopY(candidate);
                const float stepEyeY = stepTop + kPlayerEyeToBottom;
                if (stepTop >= 0.0f && stepEyeY <= candidate.Y + kMaxStepHeight) {
                    candidate.Y = std::max(candidate.Y, stepEyeY);
                    velocityY_  = 0.0f;
                    onGround_   = true;
                } else {
                    candidate.Z = cameraPosition_.Z;  // wall, blocked
                }
            }

            // --- Vertical physics ---------------------------------------------
            candidate.Y += velocityY_ * dt;

            // Floor: highest solid surface (stair step or ground) below feet.
            const float floorEyeY = GetFloorEyeY(candidate);
            if (candidate.Y <= floorEyeY) {
                candidate.Y = floorEyeY;
                velocityY_  = 0.0f;
                onGround_   = true;
            } else {
                onGround_ = false;
            }

            cameraPosition_ = candidate;

        } else {
            // ---- Fly mode: original free-camera movement ---------------------
            Vector3 move(0.0f, 0.0f, 0.0f);
            if (kb.IsKeyDown(Keys::W)) move += forward * speed;
            if (kb.IsKeyDown(Keys::S)) move -= forward * speed;
            if (kb.IsKeyDown(Keys::D)) move += right   * speed;
            if (kb.IsKeyDown(Keys::A)) move -= right   * speed;
            if (kb.IsKeyDown(Keys::E)) move += Vector3::Up * speed;
            if (kb.IsKeyDown(Keys::Q)) move -= Vector3::Up * speed;

            Vector3 candidate = cameraPosition_;
            candidate.X += move.X;
            if (CollidesWithSolid(candidate)) candidate.X = cameraPosition_.X;
            candidate.Z += move.Z;
            if (CollidesWithSolid(candidate)) candidate.Z = cameraPosition_.Z;
            candidate.Y += move.Y;
            cameraPosition_ = candidate;
        }

        UpdateCameraTarget();
    }

    void Draw(const GameTime&) override {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(135, 196, 230, 255), 1.0f); // sky blue

        // SpriteBatch applies its own XNA/FNA default render states and deliberately leaves them
        // active after End(). Restore the complete 3D baseline every frame before drawing the
        // demo's intentionally two-sided box and ground geometry.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const auto& vp = device.getViewportProperty();
        const float aspect =
            (vp.getHeightProperty() > 0)
                ? static_cast<float>(vp.getWidthProperty()) /
                  static_cast<float>(vp.getHeightProperty())
                : 1.0f;

        effect_->View       = Matrix::CreateLookAt(cameraPosition_, cameraTarget_, Vector3::Up);
        effect_->Projection = Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 300.0f);
        effect_->World      = Matrix::getIdentityProperty();

        // XNA 4.0-style draw flow.
        for (auto& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty()) {
            pass.Apply();
            for (const auto& m : meshes_) {
                device.SetVertexBuffer(m.vb.get());
                device.Indices(m.ib.get()); // XNA: graphicsDevice.Indices = ...
                const int numVertices = m.vb->getVertexCountProperty();
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList,
                    /*baseVertex=*/0,
                    /*minVertexIndex=*/0,
                    /*numVertices=*/numVertices,
                    /*startIndex=*/0,
                    /*primitiveCount=*/m.primitiveCount);
            }
            // Overlay black wireframe edges so individual boxes remain readable
            // even when neighbouring boxes share the same fill colour.
            for (const auto& m : edgeMeshes_) {
                device.SetVertexBuffer(m.vb.get());
                device.Indices(m.ib.get());
                const int numVertices = m.vb->getVertexCountProperty();
                device.DrawIndexedPrimitives(
                    PrimitiveType::LineList,
                    /*baseVertex=*/0,
                    /*minVertexIndex=*/0,
                    /*numVertices=*/numVertices,
                    /*startIndex=*/0,
                    /*primitiveCount=*/m.primitiveCount);
            }

            // Transparent glass — rendered last with blending on and
            // depth writes off so the world behind glass shows through.
            device.SetBlendEnabled(true);
            device.SetDepthWriteEnabled(false);
            for (const auto& m : glassMeshes_) {
                device.SetVertexBuffer(m.vb.get());
                device.Indices(m.ib.get());
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList,
                    0, 0, m.vb->getVertexCountProperty(), 0, m.primitiveCount);
            }
            device.SetDepthWriteEnabled(true);
            device.SetBlendEnabled(false);
        }

        // --- Controls overlay (shown for the first kOverlaySeconds) ----------
        if (overlayTimer_ > 0.0f && overlayTexture_.getBoundsProperty().Width > 0) {
            const int sw = vp.getWidthProperty();
            const int sh = vp.getHeightProperty();
            const int ow = static_cast<int>(sw * 0.62f);
            const int oh = static_cast<int>(sh * 0.62f);
            const int ox = (sw - ow) / 2;
            const int oy = (sh - oh) / 2;

            device.SetDepthTestEnabled(false);
            spriteBatch_->Begin();
            spriteBatch_->Draw(
                overlayTexture_,
                Rectangle(ox, oy, ow, oh),
                overlayTexture_.getBoundsProperty(),
                Color(255, 255, 255, 255));
            spriteBatch_->End();
            device.SetDepthTestEnabled(true);
        }
    }

    // Builds a 480×300 RGBA texture with the controls reference card.
    static Texture2D BuildOverlayTexture(GraphicsDevice& device) {
        constexpr int W = 480, H = 300;
        std::vector<std::uint8_t> px(W * H * 4, 0);

        // Dark semi-transparent background.
        for (int i = 0; i < W * H; ++i) {
            px[i*4+0] = 10; px[i*4+1] = 10;
            px[i*4+2] = 20; px[i*4+3] = 210;
        }
        // Inner border.
        for (int x = 2; x < W-2; ++x) {
            for (int y = 2; y < H-2; ++y) {
                if (x == 2 || x == W-3 || y == 2 || y == H-3) {
                    const int off = (y*W+x)*4;
                    px[off+0]=80; px[off+1]=120; px[off+2]=200; px[off+3]=255;
                }
            }
        }

        // Title (yellow).
        FontDrawText(px, W, H, 10, 10, "HOUSE 3D DEMO  -  CONTROLS", 255, 230, 60);
        // Separator line.
        for (int x = 10; x < W-10; ++x) {
            const int off = (22*W+x)*4;
            px[off+0]=100; px[off+1]=150; px[off+2]=220; px[off+3]=255;
        }

        struct Row { const char* key; const char* desc; };
        constexpr Row kRows[] = {
            { "--- GAME MODE (default) ---", "" },
            { "W / Up",        "Move forward" },
            { "S / Down",      "Move backward" },
            { "A / D",         "Strafe left / right" },
            { "Left / Right",  "Turn left / right" },
            { "Page Up/Down",  "Look up / down" },
            { "Space",         "Jump" },
            { "--- FLY MODE (Tab) -------", "" },
            { "W / S",         "Forward / backward (3D)" },
            { "A / D",         "Strafe left / right" },
            { "Q / E",         "Move down / up" },
            { "Left / Right",  "Turn  |  Up/Down: look" },
            { "--- COMMON ----------------", "" },
            { "Tab",           "Toggle Game / Fly mode" },
            { "Esc",           "Quit" },
        };
        constexpr int COL1 = 10, COL2 = 175, Y0 = 28, DY = 18;
        for (int i = 0; i < static_cast<int>(sizeof(kRows)/sizeof(kRows[0])); ++i) {
            const int y = Y0 + i * DY;
            if (kRows[i].key[0] == '-') {
                FontDrawText(px, W, H, COL1, y, kRows[i].key, 80, 200, 220);
            } else {
                FontDrawText(px, W, H, COL1, y, kRows[i].key,  180, 210, 255);
                FontDrawText(px, W, H, COL2, y, kRows[i].desc, 230, 230, 230);
            }
        }
        return Texture2D::CreateFromPixels(device, W, H, px);
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

    /** @brief Demo-local AABB collider (axis-aligned, world-space). */
    struct Collider {
        Vector3 center;
        Vector3 halfSize;
    };

    // Player body approximated as an AABB centered at the camera's eye.
    // Eye height ~1.7m, body half-width 0.30m, body half-height 0.85m
    // (the eye is near the top, so player AABB extends from (eyeY-1.7..eyeY)).
    static constexpr float kPlayerHalfX = 0.30f;
    static constexpr float kPlayerHalfZ = 0.30f;
    static constexpr float kPlayerEyeToTop    = 0.10f; // eye below top
    static constexpr float kPlayerEyeToBottom = 1.60f; // eye above feet

    bool CollidesWithSolid(const Vector3& eye) const {
        const float pxMin = eye.X - kPlayerHalfX;
        const float pxMax = eye.X + kPlayerHalfX;
        const float pyMin = eye.Y - kPlayerEyeToBottom;
        const float pyMax = eye.Y + kPlayerEyeToTop;
        const float pzMin = eye.Z - kPlayerHalfZ;
        const float pzMax = eye.Z + kPlayerHalfZ;
        for (const auto& c : colliders_) {
            const float cxMin = c.center.X - c.halfSize.X;
            const float cxMax = c.center.X + c.halfSize.X;
            const float cyMin = c.center.Y - c.halfSize.Y;
            const float cyMax = c.center.Y + c.halfSize.Y;
            const float czMin = c.center.Z - c.halfSize.Z;
            const float czMax = c.center.Z + c.halfSize.Z;
            if (pxMax > cxMin && pxMin < cxMax &&
                pyMax > cyMin && pyMin < cyMax &&
                pzMax > czMin && pzMin < czMax) {
                return true;
            }
        }
        return false;
    }

    // Returns the highest top-Y of any solid collider that the player AABB
    // at `eye` overlaps. Used for step-up: tells us how high to lift the player.
    // Returns -1 if there is no overlap.
    float GetBlockingColliderTopY(const Vector3& eye) const {
        const float pxMin = eye.X - kPlayerHalfX;
        const float pxMax = eye.X + kPlayerHalfX;
        const float pyMin = eye.Y - kPlayerEyeToBottom;
        const float pyMax = eye.Y + kPlayerEyeToTop;
        const float pzMin = eye.Z - kPlayerHalfZ;
        const float pzMax = eye.Z + kPlayerHalfZ;
        float best = -1.0f;
        for (const auto& c : colliders_) {
            const float cxMin = c.center.X - c.halfSize.X;
            const float cxMax = c.center.X + c.halfSize.X;
            const float cyMin = c.center.Y - c.halfSize.Y;
            const float cyMax = c.center.Y + c.halfSize.Y;
            const float czMin = c.center.Z - c.halfSize.Z;
            const float czMax = c.center.Z + c.halfSize.Z;
            if (pxMax > cxMin && pxMin < cxMax &&
                pyMax > cyMin && pyMin < cyMax &&
                pzMax > czMin && pzMin < czMax) {
                if (cyMax > best) best = cyMax;
            }
        }
        return best;
    }

    // Returns the eye Y at which the player would stand on the highest solid
    // surface at `eye`'s XZ position (surfaces at or just below feet level).
    // Falls back to kGroundEyeY (Y=0 ground plane) when no solid surface found.
    float GetFloorEyeY(const Vector3& eye) const {
        const float pxMin  = eye.X - kPlayerHalfX;
        const float pxMax  = eye.X + kPlayerHalfX;
        const float pzMin  = eye.Z - kPlayerHalfZ;
        const float pzMax  = eye.Z + kPlayerHalfZ;
        const float feetY  = eye.Y - kPlayerEyeToBottom;
        float best = 0.0f; // absolute ground at Y=0
        for (const auto& c : colliders_) {
            const float cyMax = c.center.Y + c.halfSize.Y;
            if (cyMax <= feetY + 0.05f && cyMax > best) {
                const float cxMin = c.center.X - c.halfSize.X;
                const float cxMax = c.center.X + c.halfSize.X;
                const float czMin = c.center.Z - c.halfSize.Z;
                const float czMax = c.center.Z + c.halfSize.Z;
                if (pxMax > cxMin && pxMin < cxMax &&
                    pzMax > czMin && pzMin < czMax) {
                    best = cyMax;
                }
            }
        }
        return best + kPlayerEyeToBottom;
    }

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
                const Color& color,
                bool solid = true) {
        if (solid) {
            colliders_.push_back(Collider{
                position,
                Vector3(size.X * 0.5f, size.Y * 0.5f, size.Z * 0.5f)
            });
        }
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

        solid_builder_.append(verts, 24, indices, 36, 12);

        AddBoxEdges(device, x0, y0, z0, x1, y1, z1);
    }

    /**
     * @brief Builds a black wireframe (12 edges) overlay for an axis-aligned
     *        box, pushed into @ref edgeMeshes_.
     */
    void AddBoxEdges(GraphicsDevice& device,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1) {
        const Color edge(0, 0, 0, 255);
        VertexPositionColor verts[8] = {
            { Vector3(x0, y0, z0), edge },
            { Vector3(x1, y0, z0), edge },
            { Vector3(x1, y0, z1), edge },
            { Vector3(x0, y0, z1), edge },
            { Vector3(x0, y1, z0), edge },
            { Vector3(x1, y1, z0), edge },
            { Vector3(x1, y1, z1), edge },
            { Vector3(x0, y1, z1), edge },
        };
        // 12 edges => 24 indices (line list).
        std::uint16_t indices[24] = {
            0,1, 1,2, 2,3, 3,0,   // bottom
            4,5, 5,6, 6,7, 7,4,   // top
            0,4, 1,5, 2,6, 3,7    // verticals
        };

        edge_builder_.append(verts, 8, indices, 24, 12);
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

        solid_builder_.append(verts, 4, indices, 6, 2);
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
               trunkColor, /*solid=*/true);
        AddBox(device,
               base + Vector3(0.0f, trunkH + crownSize * 0.5f, 0.0f),
               Vector3(crownSize, crownSize, crownSize),
               crownColor, /*solid=*/false);
    }

    /** @brief Adds a small bush (a single low green box). */
    void AddBush(GraphicsDevice& device, const Vector3& base, float size = 0.7f) {
        AddBox(device,
               base + Vector3(0.0f, size * 0.5f, 0.0f),
               Vector3(size, size, size),
               Color(60, 130, 60, 255), /*solid=*/false);
    }

    /** @brief Adds a flat path tile slightly above the ground (decorative). */
    void AddPath(GraphicsDevice& device, const Vector3& center,
                 const Vector3& size, const Color& color) {
        AddBox(device, Vector3(center.X, 0.025f, center.Z),
               Vector3(size.X, 0.05f, size.Z), color, /*solid=*/false);
    }

    /**
     * @brief Adds a window (glass pane + frame + crossbar) on a wall.
     *
     * @param wallAxis 'Z' = window faces +Z (front wall), 'z' = -Z (back),
     *                 'X' = +X (right), 'x' = -X (left).
     * @param wallCoord The coordinate of the wall's outer face on the
     *                  perpendicular axis.
     */
    void AddWindow(GraphicsDevice& device,
                   char wallAxis, float wallCoord,
                   const Vector3& center, float w, float h) {
        const Color glass(70, 110, 160, 255);
        const Color frame(80, 65, 50, 255);
        const float frameT = 0.06f;     // frame thickness (perpendicular to wall)
        const float frameW = 0.08f;     // frame strip width
        const float offset = 0.05f;     // small offset from wall surface

        // Compute outward normal sign and perpendicular axis index (0=X,2=Z).
        const bool axisZ = (wallAxis == 'Z' || wallAxis == 'z');
        const float sign = (wallAxis == 'Z' || wallAxis == 'X') ? +1.0f : -1.0f;

        Vector3 c = center;
        if (axisZ) c.Z = wallCoord + sign * offset;
        else       c.X = wallCoord + sign * offset;

        // Glass pane.
        Vector3 glassSize = axisZ
            ? Vector3(w, h, frameT)
            : Vector3(frameT, h, w);
        AddBox(device, c, glassSize, glass, /*solid=*/false);

        // Frame strips (top/bottom/left/right) just outside the glass.
        Vector3 fc = c;
        if (axisZ) fc.Z += sign * 0.005f; else fc.X += sign * 0.005f;

        if (axisZ) {
            // top/bottom horizontal strips
            AddBox(device, Vector3(fc.X, fc.Y + h * 0.5f, fc.Z),
                   Vector3(w + frameW, frameW, frameT), frame, false);
            AddBox(device, Vector3(fc.X, fc.Y - h * 0.5f, fc.Z),
                   Vector3(w + frameW, frameW, frameT), frame, false);
            // left/right vertical strips
            AddBox(device, Vector3(fc.X - w * 0.5f, fc.Y, fc.Z),
                   Vector3(frameW, h, frameT), frame, false);
            AddBox(device, Vector3(fc.X + w * 0.5f, fc.Y, fc.Z),
                   Vector3(frameW, h, frameT), frame, false);
            // crossbar (vertical)
            AddBox(device, Vector3(fc.X, fc.Y, fc.Z),
                   Vector3(frameW * 0.6f, h, frameT), frame, false);
        } else {
            AddBox(device, Vector3(fc.X, fc.Y + h * 0.5f, fc.Z),
                   Vector3(frameT, frameW, w + frameW), frame, false);
            AddBox(device, Vector3(fc.X, fc.Y - h * 0.5f, fc.Z),
                   Vector3(frameT, frameW, w + frameW), frame, false);
            AddBox(device, Vector3(fc.X, fc.Y, fc.Z - w * 0.5f),
                   Vector3(frameT, h, frameW), frame, false);
            AddBox(device, Vector3(fc.X, fc.Y, fc.Z + w * 0.5f),
                   Vector3(frameT, h, frameW), frame, false);
            AddBox(device, Vector3(fc.X, fc.Y, fc.Z),
                   Vector3(frameT, h, frameW * 0.6f), frame, false);
        }
    }

    /**
     * @brief Adds a door visual: outer frame strips + open door panel + handle.
     *        The door is rendered swung fully open (hinged on the left side,
     *        opening outward), so the doorway is passable. The open panel is
     *        solid so the player cannot walk through it.
     */
    void AddDoor(GraphicsDevice& device,
                 float wallZ, float sign,
                 float cx, float cyBase, float w, float h) {
        const Color frame(70, 50, 30, 255);
        const Color door (120, 75, 40, 255);
        const Color handle(220, 200, 60, 255);
        const float frameT = 0.06f;
        const float frameW = 0.10f;
        const float panelT = 0.05f;
        const float off    = 0.05f;

        const float zOuter = wallZ + sign * off;
        const float cy     = cyBase + h * 0.5f;

        // Frame: top + 2 sides (no bottom, that's the threshold).
        AddBox(device, Vector3(cx, cyBase + h, zOuter),
               Vector3(w + 2 * frameW, frameW, frameT), frame, false);
        AddBox(device, Vector3(cx - w * 0.5f - frameW * 0.5f, cy, zOuter),
               Vector3(frameW, h, frameT), frame, false);
        AddBox(device, Vector3(cx + w * 0.5f + frameW * 0.5f, cy, zOuter),
               Vector3(frameW, h, frameT), frame, false);

        // Door panel swung open on the left hinge (outward, toward +Z for sign=+1).
        // When open, the panel lies flat against the wall beside the frame.
        const float hingeX  = cx - w * 0.5f;           // left edge of door opening
        const float panelCZ = wallZ + sign * w * 0.5f; // panel extends outward
        AddBox(device, Vector3(hingeX, cy, panelCZ),
               Vector3(panelT, h * 0.97f, w * 0.95f), door, /*solid=*/true);

        // Handle near the free edge of the open door.
        AddBox(device, Vector3(hingeX, cyBase + h * 0.45f, wallZ + sign * (w - 0.15f)),
               Vector3(0.08f, 0.08f, 0.08f), handle, false);
    }

    /** @brief Adds a small flight of entrance steps in front of a door. */
    void AddEntranceSteps(GraphicsDevice& device,
                          float cx, float frontZ, float baseY) {
        const Color stoneColor(150, 145, 135, 255);
        const int steps = 3;
        const float stepRise = baseY / steps;
        const float stepRun  = 0.30f;
        for (int i = 0; i < steps; ++i) {
            const float y = stepRise * (static_cast<float>(i) + 0.5f);
            const float z = frontZ + 0.10f + stepRun * (static_cast<float>(steps - i) - 0.5f);
            AddBox(device, Vector3(cx, y, z),
                   Vector3(1.6f, stepRise, stepRun), stoneColor, /*solid=*/false);
        }
    }

    void BuildScene(GraphicsDevice& device) {
        // ---- Garden ground ------------------------------------------------
        AddGround(device, 60.0f, Color(70, 140, 70, 255));

        // Subtle terrain variation: a couple of low grass patches (decorative).
        AddBox(device, Vector3(-10.0f, 0.05f,  6.0f), Vector3(4.0f, 0.10f, 3.0f), Color(80,150,75,255), /*solid=*/false);
        AddBox(device, Vector3( 12.0f, 0.05f, -8.0f), Vector3(5.0f, 0.10f, 4.0f), Color(85,155,80,255), /*solid=*/false);

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

        // Foundation slab (decorative; would otherwise block all interior horizontal motion).
        AddBox(device, Vector3(0.0f, baseY, 0.0f),
               Vector3(W + 0.4f, baseH, D + 0.4f), baseColor, /*solid=*/false);

        // ---- Basement (a darker pit suggestion under the slab) -----------
        // We can't actually dig into ground geometry, so we expose a basement
        // "stairwell" opening on the back-right corner: a dark sunken box
        // that suggests stairs going down. The player can fly into it.
        AddBox(device, Vector3(W * 0.5f - 1.0f, -0.6f, -D * 0.5f + 1.0f),
               Vector3(1.6f, 1.2f, 1.6f), Color(40, 35, 30, 255), /*solid=*/false);
        // Stone steps descending into the basement opening (decorative).
        for (int i = 0; i < 4; ++i) {
            const float y = -0.15f - i * 0.20f;
            const float zOff = -D * 0.5f + 0.4f + i * 0.2f;
            AddBox(device, Vector3(W * 0.5f - 1.0f, y, zOff),
                   Vector3(1.4f, 0.10f, 0.2f), stoneColor, /*solid=*/false);
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
        // Door visual (frame + panel + handle); opening itself is passable.
        (void)doorColor;
        (void)doorCY;
        AddDoor(device, frontZ + wallT * 0.5f, +1.0f, doorCX, baseH, doorW, doorH);

        // Small entrance steps in front of the door.
        AddEntranceSteps(device, doorCX, frontZ + wallT * 0.5f, baseH);

        // Front-window pair on the 1st-floor front wall (left segment).
        AddWindow(device, 'Z', frontZ + wallT * 0.5f,
                  Vector3(-3.5f, baseH + 1.4f, 0.0f), 0.9f, 1.0f);
        AddWindow(device, 'Z', frontZ + wallT * 0.5f,
                  Vector3( 1.8f, baseH + 1.4f, 0.0f), 0.9f, 1.0f);
        // Side window on the right (-X normal? No, +X wall).
        AddWindow(device, 'X',  W * 0.5f - wallT * 0.5f,
                  Vector3(0.0f, baseH + 1.4f, 1.5f), 0.9f, 1.0f);
        // Side window on the left wall.
        AddWindow(device, 'x', -W * 0.5f + wallT * 0.5f,
                  Vector3(0.0f, baseH + 1.4f, -1.5f), 0.9f, 1.0f);

        // ---- Interior partition (split 1st floor into 2 rooms) -----------
        // Interior wall along X = 1.0, with an opening (no door) from Z=-0.5..1.0
        // so the player can walk between the rooms.
        const float partX = 1.0f;
        // Segment from -Z wall to opening.
        AddBox(device,
               Vector3(partX, f1Y, (-D * 0.5f + (-0.5f)) * 0.5f),
               Vector3(wallT, floorH, (-0.5f) - (-D * 0.5f)),
               innerWall, /*solid=*/true);
        // Segment from opening to +Z wall.
        AddBox(device,
               Vector3(partX, f1Y, (1.0f + D * 0.5f) * 0.5f),
               Vector3(wallT, floorH, D * 0.5f - 1.0f),
               innerWall, /*solid=*/true);

        // ---- Floor between 1st and 2nd story (with stair hole) ----------
        // Split into 4 solid slabs around the stairwell opening at X=2..3.5, Z=-2..-0.5.
        // Solid so the player can stand on the 2nd floor without falling through.
        const float fT = 0.10f; // floor/ceiling slab thickness
        const float ceilCY = ceilY + fT * 0.5f;
        // Front slab: covers Z from -0.5 to +D/2 (in front of stairwell).
        AddBox(device, Vector3(0.0f, ceilCY, (-0.5f + D * 0.5f) * 0.5f),
               Vector3(W, fT, D * 0.5f + 0.5f), floorColor, /*solid=*/true);
        // Back slab: covers Z from -D/2 to -2.0 (behind stairwell).
        AddBox(device, Vector3(0.0f, ceilCY, (-D * 0.5f + (-2.0f)) * 0.5f),
               Vector3(W, fT, D * 0.5f - 2.0f), floorColor, /*solid=*/true);
        // Left slab: covers X from -W/2 to 2.0, Z in stairwell column (-2..-0.5).
        AddBox(device, Vector3((-W * 0.5f + 2.0f) * 0.5f, ceilCY, -1.25f),
               Vector3(W * 0.5f + 2.0f, fT, 1.5f), floorColor, /*solid=*/true);
        // Right slab: covers X from 3.5 to +W/2, Z in stairwell column.
        AddBox(device, Vector3((3.5f + W * 0.5f) * 0.5f, ceilCY, -1.25f),
               Vector3(W * 0.5f - 3.5f, fT, 1.5f), floorColor, /*solid=*/true);

        // ---- Stairs to 2nd floor (stacked boxes) -------------------------
        // 8 steps from y=baseH up to y=ceilY, at X around 2.75, Z descending.
        const int   stepCount = 8;
        const float stepRise  = (ceilY - baseH) / stepCount;
        const float stepRun   = 0.30f;
        for (int i = 0; i < stepCount; ++i) {
            const float sy = baseH + stepRise * (static_cast<float>(i) + 0.5f);
            const float sz = -0.5f - stepRun * (static_cast<float>(i) + 0.5f);
            AddBox(device, Vector3(2.75f, sy, sz),
                   Vector3(1.5f, stepRise, stepRun), stoneColor, /*solid=*/true);
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

        // 2nd-floor side windows (one per side wall).
        AddWindow(device, 'X',  W * 0.5f - wallT * 0.5f,
                  Vector3(0.0f, baseH + floorH + 1.4f,  1.5f), 0.9f, 1.0f);
        AddWindow(device, 'x', -W * 0.5f + wallT * 0.5f,
                  Vector3(0.0f, baseH + floorH + 1.4f, -1.5f), 0.9f, 1.0f);
        // Front 2nd-floor window next to the balcony doorway.
        AddWindow(device, 'Z', frontZ + wallT * 0.5f,
                  Vector3(-2.5f, baseH + floorH + 1.4f, 0.0f), 1.0f, 1.0f);

        // ---- Roof: cleaner stepped pyramid (5 thinner layers, smoother shrink). --
        const int   roofLayers = 5;
        const float roofTotalH = 1.6f;
        const float layerH = roofTotalH / roofLayers;
        for (int i = 0; i < roofLayers; ++i) {
            const float t = static_cast<float>(i + 1) / roofLayers;
            const float w = (W + 0.6f) * (1.0f - 0.85f * t * t) + 0.2f;
            const float d = (D + 0.6f) * (1.0f - 0.85f * t * t) + 0.2f;
            const float y = roofY + layerH * (static_cast<float>(i) + 0.5f);
            const Color rc(160 - i * 6, 60 - i * 4, 40, 255);
            AddBox(device, Vector3(0.0f, y, 0.0f),
                   Vector3(w, layerH, d), rc, /*solid=*/false);
        }
        // Roof ridge cap.
        AddBox(device, Vector3(0.0f, roofY + roofTotalH + 0.10f, 0.0f),
               Vector3(0.6f, 0.20f, 0.6f), Color(110, 40, 30, 255), /*solid=*/false);

        // ---- Balcony (platform + railings + balusters outside front wall) ------
        const float balPlatY = baseH + floorH; // sits on top of 1st-floor ceiling
        AddBox(device,
               Vector3(balCX, balPlatY + 0.05f, frontZ + 0.6f),
               Vector3(2.5f, 0.10f, 1.2f), stoneColor, /*solid=*/true);
        // Top + lower rail + corner posts.
        AddBox(device, Vector3(balCX, balPlatY + 0.85f, frontZ + 1.2f),
               Vector3(2.6f, 0.10f, 0.10f), wallColor);
        AddBox(device, Vector3(balCX, balPlatY + 0.30f, frontZ + 1.2f),
               Vector3(2.6f, 0.05f, 0.05f), wallColor);
        AddBox(device, Vector3(balCX - 1.25f, balPlatY + 0.45f, frontZ + 0.6f),
               Vector3(0.10f, 0.85f, 1.2f), wallColor);
        AddBox(device, Vector3(balCX + 1.25f, balPlatY + 0.45f, frontZ + 0.6f),
               Vector3(0.10f, 0.85f, 1.2f), wallColor);
        // Balusters spaced along the front railing.
        for (int i = -2; i <= 2; ++i) {
            const float bx = balCX + i * 0.5f;
            AddBox(device, Vector3(bx, balPlatY + 0.45f, frontZ + 1.2f),
                   Vector3(0.05f, 0.7f, 0.05f), wallColor, /*solid=*/false);
        }

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
        // Tighter spacing -> more posts/rails.
        const float postSpacing = 0.6f;
        // Front run.
        AddFence(device, Vector3(-12.0f, 0.0f, 10.0f),
                 Vector3( -3.0f, 0.0f, 10.0f), postSpacing, 1.1f, postCol, railCol);
        // Left side run.
        AddFence(device, Vector3(-12.0f, 0.0f, 10.0f),
                 Vector3(-12.0f, 0.0f, -2.0f), postSpacing, 1.1f, postCol, railCol);
        // Right side run (mirror).
        AddFence(device, Vector3(  3.0f, 0.0f, 10.0f),
                 Vector3( 12.0f, 0.0f, 10.0f), postSpacing, 1.1f, postCol, railCol);
        AddFence(device, Vector3( 12.0f, 0.0f, 10.0f),
                 Vector3( 12.0f, 0.0f, -2.0f), postSpacing, 1.1f, postCol, railCol);

        // ---- Trees (multiple) --------------------------------------------
        AddTree(device, Vector3( 6.0f, 0.0f,  4.0f), 1.8f, 1.8f);
        AddTree(device, Vector3(-6.0f, 0.0f,  6.5f), 1.6f, 1.6f);
        AddTree(device, Vector3(-9.0f, 0.0f, -3.0f), 2.0f, 2.0f);
        AddTree(device, Vector3( 9.0f, 0.0f, -6.0f), 1.5f, 1.5f);
        AddTree(device, Vector3( 8.0f, 0.0f,  7.5f), 1.4f, 1.4f);
        AddTree(device, Vector3(-7.5f, 0.0f, -7.0f), 1.7f, 1.7f);

        // ---- Garden bushes (decorative) ----------------------------------
        AddBush(device, Vector3(-3.0f, 0.0f, 7.0f), 0.7f);
        AddBush(device, Vector3( 0.5f, 0.0f, 7.5f), 0.6f);
        AddBush(device, Vector3( 4.0f, 0.0f, 7.0f), 0.7f);
        AddBush(device, Vector3(-5.5f, 0.0f, 5.0f), 0.5f);
        AddBush(device, Vector3( 5.5f, 0.0f, 5.5f), 0.6f);

        // Merge all accumulated geometry into one VBO/IBO per category.
        FinalizeBuilders(device);
    }

    // CPU-side geometry accumulator used during BuildScene().
    // All AddBox/AddGround/... calls append here; FinalizeBuilders() then
    // uploads exactly one VertexBuffer+IndexBuffer per category to the GPU.
    struct MeshBuilder
    {
        std::vector<VertexPositionColor> verts;
        std::vector<std::uint16_t>       indices;
        int prim_count = 0;

        void append(const VertexPositionColor* v, int vc,
                    const std::uint16_t* idx,    int ic, int pc)
        {
            const auto base = static_cast<std::uint16_t>(verts.size());
            for (int i = 0; i < vc; ++i) verts.push_back(v[i]);
            for (int i = 0; i < ic; ++i) indices.push_back(idx[i] + base);
            prim_count += pc;
        }

        [[nodiscard]] bool empty() const { return verts.empty(); }
    };

    MeshBuilder solid_builder_;
    MeshBuilder edge_builder_;
    MeshBuilder glass_builder_;

    void FinalizeBuilders(GraphicsDevice& device)
    {
        auto upload = [&](MeshBuilder& b, std::vector<Mesh>& out)
        {
            if (b.empty()) return;
            Mesh m;
            m.vb = std::make_unique<VertexBuffer>(device, static_cast<int>(b.verts.size()));
            m.vb->SetData(b.verts.data(), static_cast<int>(b.verts.size()));
            m.ib = std::make_unique<IndexBuffer>(device, static_cast<int>(b.indices.size()));
            m.ib->SetData(b.indices.data(), static_cast<int>(b.indices.size()));
            m.primitiveCount = b.prim_count;
            out.push_back(std::move(m));
            b = {};  // free CPU memory
        };
        upload(solid_builder_, meshes_);
        upload(edge_builder_,  edgeMeshes_);
        upload(glass_builder_, glassMeshes_);
        // Benchmark (EasyGL renderer, desktop, 60 FPS, metagl METAGLDEBUG counter):
        //   Before merge: ~700 000 GL calls / 5 s  (~400 meshes x ~6 calls x 60 FPS x 5 s)
        //   After  merge: ~15 000 GL calls / 5 s   (~46x reduction)
    }

    std::unique_ptr<BasicEffect> effect_;
    std::vector<Mesh>            meshes_;
    std::vector<Mesh>            edgeMeshes_;
    std::vector<Collider>        colliders_;

    Vector3 cameraPosition_{0.0f, kGroundEyeY, 9.0f};
    Vector3 cameraTarget_  {0.0f, 1.0f, 0.0f};
    float   yaw_   = 0.0f;  // 0 = facing -Z (toward the house)
    float   pitch_ = 0.0f;

    // Mode state
    bool  gameMode_   = true;
    bool  tabWasDown_ = false;
    float velocityY_  = 0.0f;
    bool  onGround_   = true;

    std::vector<Mesh>            glassMeshes_; // transparent — rendered last

    // Controls overlay
    std::unique_ptr<SpriteBatch> spriteBatch_;
    Texture2D                    overlayTexture_;
    float                        overlayTimer_ = 0.0f;

    // When >= 0, the demo exits after this many more frames (smoke-test mode).
    int smokeFramesLeft_ = -1;

public:
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }
};

int main(int argc, char* argv[]) {
    House3DDemo game;

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--smoke" && i + 1 < argc)
            game.SetSmokeFrames(std::stoi(argv[++i]));
        else if (std::string(argv[i]) == "--smoke")
            game.SetSmokeFrames(3);
    }

    game.Run();
    return 0;
}
