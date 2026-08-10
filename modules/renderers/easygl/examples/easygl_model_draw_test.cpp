// SPDX-License-Identifier: MS-PL
// Task 144: EasyGL integration test — Model.Draw with a hard-coded 2-bone model.
//
// Exercises the full Model → ModelMesh → ModelMeshPart → DrawIndexedPrimitives chain:
//   - 2 ModelBones: root (idx 0, identity) → child (idx 1, identity)
//   - 1 ModelMesh with 1 ModelMeshPart; parentBone is nullptr → boneIdx=0
//   - CopyAbsoluteBoneTransformsTo: both slots get identity
//   - Model::Draw multiplies boneTransform[0] * worldArg = identity
//   - BasicEffect: VertexColorEnabled=true, no texture, no lighting → colored3D pipeline
//   - Vertex color = red (255,0,0,255), full-screen NDC quad
//   - Expected centre pixel: red (R>=200, G<=50, B<=50)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class ModelDrawTest : public Game
{
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer>  ib_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const Color red(255, 0, 0, 255);
        const VertexPositionColor verts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), red },
            { Vector3(-1.0f, -1.0f, 0.0f), red },
            { Vector3( 1.0f, -1.0f, 0.0f), red },
            { Vector3( 1.0f,  1.0f, 0.0f), red },
        };
        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);

        const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);
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

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;

        // 2-bone hierarchy: root → child (both identity → absolute transforms = identity)
        ModelBone bone0(0, "root");
        ModelBone bone1(1, "child");
        bone0.AddChild(&bone1);

        // numVertices=4, primitiveCount=2, startIndex=0, vertexOffset=0
        ModelMeshPart part(vb_.get(), ib_.get(), 4, 2, 0, 0);
        ModelMesh mesh(&device, { &part });
        part.setEffectProperty(&fx);

        Model model(&device, { &bone0, &bone1 }, { &mesh });
        model.Draw(Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty());

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);
        if (pass)
        {
            std::printf("[PASS] Model.Draw: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] Model.Draw: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    ModelDrawTest game;
    game.Run();
    return game.getResult();
}
