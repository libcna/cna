// SPDX-License-Identifier: MS-PL
// Task 10.14: AvatarRenderer::EnableRealRenderingEXT/DrawRealEXT — real GPU-skinned mesh
// deformation driven through the NOXNA real-rendering Avatar extension, exercising the full
// SkinnedModelEXT -> ComputeBoneTransformsEXT -> SkinnedEffect -> GraphicsDevice draw path.
//
// Mirrors examples/skinned_effect_integration_test.cpp's synthetic-fixture approach (no real
// MakeHuman/Mixamo asset required) so this test has no dependency on Phase 10b's asset
// pipeline having been run: a quad spanning the left half of the screen (NDC x: -1..0), all
// vertices bound 100% to bone 0. A single-keyframe "Test" clip translates bone 0 by
// (+0.5, 0, 0), which shifts the quad into the centre of the screen (NDC x: -0.5..0.5).
// Background is cleared to green.
//
// Expected results after GPU skinning:
//   - Pixel at left quarter (NDC ~ -0.75) -> green  (quad moved away)
//   - Pixel at centre       (NDC ~  0.00) -> red    (quad now covers centre)
//   - Pixel at right-most   (NDC ~ +0.75) -> green  (quad doesn't reach there)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "System/TimeSpan.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;

namespace
{
    std::shared_ptr<SkinnedModelEXT> BuildOneBoneQuadModel(GraphicsDevice& device, Texture2D texture)
    {
        auto model = std::make_shared<SkinnedModelEXT>();
        model->BoneCount = 1;
        model->ParentBoneIndices = {-1};
        model->BindPoseLocal = {Matrix::getIdentityProperty()};
        model->InverseBindPoseGlobal = {Matrix::getIdentityProperty()};

        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::Zero;
        BoneTrackEXT track;
        track.BoneIndex = 0;
        track.Keys.push_back(KeyframeEXT{System::TimeSpan::Zero, Vector3(0.5f, 0.0f, 0.0f)});
        clip.Tracks.push_back(track);
        model->Clips["Test"] = clip;

        // Quad covering NDC x: -1..0, y: -1..1, all vertices bound 100% to bone 0.
        const VertexPositionNormalTextureSkinned verts[6] = {
            {Vector3(-1,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector2(0, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3( 0, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(-1,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3( 0, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3( 0,  1, 0), Vector3(0, 0, 1), Vector2(1, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

        auto vb = std::make_unique<VertexBuffer>(device, 6);
        vb->SetData(verts, 6);
        auto ib = std::make_unique<IndexBuffer>(device, 6);
        ib->SetData(indices, 6);
        auto part = std::make_unique<ModelMeshPart>(vb.get(), ib.get(), 6, 2, 0, 0);

        model->AddPartEXT("body", std::move(vb), std::move(ib), std::move(part), std::move(texture));
        return model;
    }
}

class AvatarRealRenderIntegrationTest : public Game
{
    Texture2D tex_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> px = {255, 0, 0, 255};
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        auto model = BuildOneBoneQuadModel(device, tex_);

        AvatarRenderer renderer(nullptr);
        renderer.EnableRealRenderingEXT(device, model);
        // AvatarRenderer's LightColor/LightDirection/AmbientLightColor default to black,
        // matching the real (never-drawing) XNA implementation's untouched value-type
        // defaults; a real caller configures these to match their scene before drawing,
        // same as this test does here.
        renderer.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        renderer.setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        renderer.setLightDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        renderer.DrawRealEXT("Test", System::TimeSpan::Zero, false);

        const Rectangle leftReg(W / 8,     H / 2, 1, 1);
        const Rectangle centReg(W / 2,     H / 2, 1, 1);
        const Rectangle rightReg(7 * W / 8, H / 2, 1, 1);
        Color leftPx(0, 0, 0, 0), centPx(0, 0, 0, 0), rightPx(0, 0, 0, 0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&centReg,  &centPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        const bool leftOk  = (leftPx.getGProperty()  > leftPx.getRProperty());
        const bool centOk  = (centPx.getRProperty()  > centPx.getGProperty() && centPx.getRProperty() > 50);
        const bool rightOk = (rightPx.getGProperty() > rightPx.getRProperty());

        if (leftOk && centOk && rightOk)
        {
            std::printf("[PASS] AvatarRealRenderIntegration: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] AvatarRealRenderIntegration: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=green, centre=red, right=green\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    AvatarRealRenderIntegrationTest game;
    game.Run();
    return game.getResult();
}
