/**
 * @file cube3d_demo.cpp
 * @brief Minimal CNA 3D demo: a colored cube on a flat ground plane.
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

        BuildCube(device);
        BuildGround(device);

        cameraPosition_ = Vector3(3.5f, 2.5f, 5.0f);
        cameraTarget_   = Vector3(0.0f, 0.0f, 0.0f);
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

        rotation_ += dt * 0.7f;
    }

    void Draw(const GameTime&) override {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(40, 80, 120, 255), 1.0f);
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
            kPiOver4, aspect, 0.1f, 100.0f);

        // Ground.
        effect_->World = Matrix::Identity();
        device.DrawIndexedPrimitives(*effect_, *groundVB_, *groundIB_,
                                     PrimitiveType::TriangleList, 2);

        // Spinning cube on top of the ground.
        effect_->World =
            Matrix::CreateRotationY(rotation_) *
            Matrix::CreateTranslation(0.0f, 0.5f, 0.0f);
        device.DrawIndexedPrimitives(*effect_, *cubeVB_, *cubeIB_,
                                     PrimitiveType::TriangleList, 12);
    }

private:
    void BuildCube(GraphicsDevice& device) {
        const Color faces[6] = {
            Color(255,  64,  64, 255), // +X red
            Color( 64, 255,  64, 255), // +Y green
            Color( 64,  64, 255, 255), // +Z blue
            Color(255, 255,  64, 255), // -X yellow
            Color(255,  64, 255, 255), // -Y magenta
            Color( 64, 255, 255, 255), // -Z cyan
        };
        // 6 faces * 4 verts = 24 vertices, with the face color baked in.
        VertexPositionColor verts[24];
        // +X
        verts[0] = { Vector3( 0.5f,-0.5f,-0.5f), faces[0] };
        verts[1] = { Vector3( 0.5f, 0.5f,-0.5f), faces[0] };
        verts[2] = { Vector3( 0.5f, 0.5f, 0.5f), faces[0] };
        verts[3] = { Vector3( 0.5f,-0.5f, 0.5f), faces[0] };
        // -X
        verts[4] = { Vector3(-0.5f,-0.5f, 0.5f), faces[3] };
        verts[5] = { Vector3(-0.5f, 0.5f, 0.5f), faces[3] };
        verts[6] = { Vector3(-0.5f, 0.5f,-0.5f), faces[3] };
        verts[7] = { Vector3(-0.5f,-0.5f,-0.5f), faces[3] };
        // +Y
        verts[8]  = { Vector3(-0.5f, 0.5f,-0.5f), faces[1] };
        verts[9]  = { Vector3(-0.5f, 0.5f, 0.5f), faces[1] };
        verts[10] = { Vector3( 0.5f, 0.5f, 0.5f), faces[1] };
        verts[11] = { Vector3( 0.5f, 0.5f,-0.5f), faces[1] };
        // -Y
        verts[12] = { Vector3(-0.5f,-0.5f, 0.5f), faces[4] };
        verts[13] = { Vector3(-0.5f,-0.5f,-0.5f), faces[4] };
        verts[14] = { Vector3( 0.5f,-0.5f,-0.5f), faces[4] };
        verts[15] = { Vector3( 0.5f,-0.5f, 0.5f), faces[4] };
        // +Z
        verts[16] = { Vector3(-0.5f,-0.5f, 0.5f), faces[2] };
        verts[17] = { Vector3( 0.5f,-0.5f, 0.5f), faces[2] };
        verts[18] = { Vector3( 0.5f, 0.5f, 0.5f), faces[2] };
        verts[19] = { Vector3(-0.5f, 0.5f, 0.5f), faces[2] };
        // -Z
        verts[20] = { Vector3( 0.5f,-0.5f,-0.5f), faces[5] };
        verts[21] = { Vector3(-0.5f,-0.5f,-0.5f), faces[5] };
        verts[22] = { Vector3(-0.5f, 0.5f,-0.5f), faces[5] };
        verts[23] = { Vector3( 0.5f, 0.5f,-0.5f), faces[5] };

        std::uint16_t indices[36];
        for (std::uint16_t f = 0; f < 6; ++f) {
            const std::uint16_t b = static_cast<std::uint16_t>(f * 4);
            indices[f*6 + 0] = b;
            indices[f*6 + 1] = static_cast<std::uint16_t>(b + 1);
            indices[f*6 + 2] = static_cast<std::uint16_t>(b + 2);
            indices[f*6 + 3] = b;
            indices[f*6 + 4] = static_cast<std::uint16_t>(b + 2);
            indices[f*6 + 5] = static_cast<std::uint16_t>(b + 3);
        }

        cubeVB_ = std::make_unique<VertexBuffer>(device, 24);
        cubeVB_->SetData(verts, 24);
        cubeIB_ = std::make_unique<IndexBuffer>(device, 36);
        cubeIB_->SetData(indices, 36);
    }

    void BuildGround(GraphicsDevice& device) {
        const Color colA(80, 80, 80, 255);
        const Color colB(120, 120, 120, 255);

        const float s = 6.0f;
        VertexPositionColor verts[4] = {
            { Vector3(-s, 0.0f, -s), colA },
            { Vector3( s, 0.0f, -s), colB },
            { Vector3( s, 0.0f,  s), colA },
            { Vector3(-s, 0.0f,  s), colB },
        };
        std::uint16_t indices[6] = { 0, 2, 1, 0, 3, 2 };

        groundVB_ = std::make_unique<VertexBuffer>(device, 4);
        groundVB_->SetData(verts, 4);
        groundIB_ = std::make_unique<IndexBuffer>(device, 6);
        groundIB_->SetData(indices, 6);
    }

    std::unique_ptr<BasicEffect>  effect_;
    std::unique_ptr<VertexBuffer> cubeVB_;
    std::unique_ptr<IndexBuffer>  cubeIB_;
    std::unique_ptr<VertexBuffer> groundVB_;
    std::unique_ptr<IndexBuffer>  groundIB_;

    Vector3 cameraPosition_{0.0f, 2.0f, 5.0f};
    Vector3 cameraTarget_  {0.0f, 0.0f, 0.0f};
    float   rotation_ = 0.0f;
};

int main() {
    Cube3DDemo game;
    game.Run();
    return 0;
}
