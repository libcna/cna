// SPDX-License-Identifier: MS-PL
// Task 11.21: SkinnedModelEXT::AttachPartEXT -- proves a second, independently-built
// SkinnedModelEXT's part can be attached onto a first model at runtime and both parts then
// render correctly through one SkinnedEffect pass over the host's parts, exercising the
// buffer-ownership transfer an attached part relies on.
//
// Two single-bone quads, each built as its own SkinnedModelEXT (mirroring
// avatar_real_render_integration_test.cpp's synthetic-fixture approach): "host" covers
// NDC x: -1..0 (red), "wardrobe" covers NDC x: 0..1 (blue). wardrobe is attached onto
// host via AttachPartEXT, then host alone is drawn.
//
// Expected results after GPU skinning:
//   - Pixel at left quarter  (NDC ~ -0.5) -> red  (host's own original quad)
//   - Pixel at right quarter (NDC ~ +0.5) -> blue (wardrobe's quad, now owned by host)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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

namespace
{
    std::shared_ptr<SkinnedModelEXT> BuildOneBoneQuadModel(
        GraphicsDevice& device, Texture2D texture, const std::string& partName, float ndcXMin, float ndcXMax)
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
        track.Keys.push_back(KeyframeEXT{System::TimeSpan::Zero, Vector3(0.0f, 0.0f, 0.0f)});
        clip.Tracks.push_back(track);
        model->Clips["Test"] = clip;

        const VertexPositionNormalTextureSkinned verts[6] = {
            {Vector3(ndcXMin,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(ndcXMin, -1, 0), Vector3(0, 0, 1), Vector2(0, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(ndcXMax, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(ndcXMin,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(ndcXMax, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            {Vector3(ndcXMax,  1, 0), Vector3(0, 0, 1), Vector2(1, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

        auto vb = std::make_unique<VertexBuffer>(device, 6);
        vb->SetData(verts, 6);
        auto ib = std::make_unique<IndexBuffer>(device, 6);
        ib->SetData(indices, 6);
        auto part = std::make_unique<ModelMeshPart>(vb.get(), ib.get(), 6, 2, 0, 0);

        model->AddPartEXT(partName, std::move(vb), std::move(ib), std::move(part), std::move(texture));
        return model;
    }
}

class SkinnedModelAttachPartIntegrationTest : public Game
{
    Texture2D redTex_;
    Texture2D blueTex_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        redTex_ = Texture2D::CreateFromPixels(device, 1, 1, std::vector<uint8_t>{255, 0, 0, 255});
        blueTex_ = Texture2D::CreateFromPixels(device, 1, 1, std::vector<uint8_t>{0, 0, 255, 255});
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
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone (missed by Task 896's own file audit).
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto host = BuildOneBoneQuadModel(device, redTex_, "host", -1.0f, 0.0f);
        auto wardrobe = BuildOneBoneQuadModel(device, blueTex_, "wardrobe", 0.0f, 1.0f);

        // The actual runtime-attach path under test (Task 11.21): wardrobe's part is
        // moved into host, taking ownership of its buffers; wardrobe itself is left empty.
        host->AttachPartEXT(std::move(*wardrobe));

        std::vector<Matrix> bones;
        host->ComputeBoneTransformsEXT("Test", System::TimeSpan::Zero, false, bones);
        SkinnedEffect effect(device);
        effect.SetBoneTransforms(bones);
        effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        for (const auto& part : host->Parts)
        {
            effect.setTextureProperty(part.Texture);
            effect.Apply();
            device.SetVertexBuffer(part.Part->getVertexBufferProperty());
            device.SetIndexBuffer(part.Part->getIndexBufferProperty());
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, part.Part->getVertexOffsetProperty(), 0,
                                         part.Part->getNumVerticesProperty(), part.Part->getStartIndexProperty(),
                                         part.Part->getPrimitiveCountProperty());
        }

        const Rectangle leftReg(W / 4, H / 2, 1, 1);
        const Rectangle rightReg(3 * W / 4, H / 2, 1, 1);
        Color leftPx(0, 0, 0, 0), rightPx(0, 0, 0, 0);
        device.GetBackBufferData(&leftReg, &leftPx, 0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        const bool leftOk = (leftPx.getRProperty() > leftPx.getGProperty()
                              && leftPx.getRProperty() > leftPx.getBProperty());
        const bool rightOk = (rightPx.getBProperty() > rightPx.getRProperty()
                               && rightPx.getBProperty() > rightPx.getGProperty());
        const bool partCountOk = (host->Parts.size() == 2);

        if (leftOk && rightOk && partCountOk)
        {
            std::printf("[PASS] SkinnedModelAttachPartIntegration: left=(%d,%d,%d) right=(%d,%d,%d) parts=%zu\n",
                        leftPx.getRProperty(), leftPx.getGProperty(), leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty(),
                        host->Parts.size());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedModelAttachPartIntegration: left=(%d,%d,%d) right=(%d,%d,%d) parts=%zu\n"
                        "       expected: left=red, right=blue, parts=2\n",
                        leftPx.getRProperty(), leftPx.getGProperty(), leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty(),
                        host->Parts.size());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    SkinnedModelAttachPartIntegrationTest game;
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    game.Run();
    return game.getResult();
}
