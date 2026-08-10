// SPDX-License-Identifier: MS-PL
// Task 438: Pixel test -- a Model with two meshes, each using its own distinct Effect, on EasyGL.
//
// Builds on Task 144's established Model.Draw fixture pattern (a real 2-bone hierarchy, a real
// VertexBuffer/IndexBuffer/BasicEffect chain through DrawIndexedPrimitives), extended to 2
// meshes: a LEFT-half quad (Red, its own BasicEffect) and a RIGHT-half quad (Blue, a SEPARATE
// BasicEffect instance) -- proving Model::Draw iterates every mesh in ModelMeshCollection and
// applies each mesh's OWN Effect, not accidentally sharing/reusing one Effect across meshes or
// only drawing the first/last mesh.
//
// Both meshes' ParentBone is left null (matches Task 144's own convention) -- Model::Draw
// defaults a null ParentBone to bone index 0 (Task 431's audit finding), so both meshes render
// with the single root bone's identity transform.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

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

namespace
{
    const Color kRed (255,   0,   0, 255);
    const Color kBlue(  0,   0, 255, 255);
    const Color kGreen( 0, 255,   0, 255); // background

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }
}

class ModelTwoMeshesEffectsTest : public Game
{
    std::unique_ptr<VertexBuffer> vbLeft_,  vbRight_;
    std::unique_ptr<IndexBuffer>  ibLeft_,  ibRight_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // Left half quad: x in [-1, 0], full height -- Red.
        const VertexPositionColor leftVerts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 0.0f, -1.0f, 0.0f), kRed },
            { Vector3( 0.0f,  1.0f, 0.0f), kRed },
        };
        vbLeft_ = std::make_unique<VertexBuffer>(device, 4);
        vbLeft_->SetData(leftVerts, 4);

        // Right half quad: x in [0, 1], full height -- Blue.
        const VertexPositionColor rightVerts[4] = {
            { Vector3( 0.0f,  1.0f, 0.0f), kBlue },
            { Vector3( 0.0f, -1.0f, 0.0f), kBlue },
            { Vector3( 1.0f, -1.0f, 0.0f), kBlue },
            { Vector3( 1.0f,  1.0f, 0.0f), kBlue },
        };
        vbRight_ = std::make_unique<VertexBuffer>(device, 4);
        vbRight_->SetData(rightVerts, 4);

        const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ibLeft_  = std::make_unique<IndexBuffer>(device, 6);
        ibLeft_->SetData(indices, 6);
        ibRight_ = std::make_unique<IndexBuffer>(device, 6);
        ibRight_->SetData(indices, 6);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(kGreen);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: NDC quad winding is CCW/back-facing under CNA's real default
        // RasterizerState -- needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fxLeft(device);
        fxLeft.VertexColorEnabled = true;
        BasicEffect fxRight(device);
        fxRight.VertexColorEnabled = true;

        ModelBone root(0, "root");

        ModelMeshPart partLeft(vbLeft_.get(), ibLeft_.get(), 4, 2, 0, 0);
        ModelMesh meshLeft(&device, "Left", { &partLeft });
        partLeft.setEffectProperty(&fxLeft);

        ModelMeshPart partRight(vbRight_.get(), ibRight_.get(), 4, 2, 0, 0);
        ModelMesh meshRight(&device, "Right", { &partRight });
        partRight.setEffectProperty(&fxRight);

        Model model(&device, { &root }, { &meshLeft, &meshRight });
        model.Draw(Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty());

        auto sample = [&](int x, int y) {
            Color px(0, 0, 0, 0);
            Rectangle reg(x, y, 1, 1);
            device.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        Color left  = sample(W / 4, H / 2);
        Color right = sample(3 * W / 4, H / 2);

        check(colourMatch(left,  kRed),  "left mesh renders its OWN effect's colour -> Red");
        check(colourMatch(right, kBlue), "right mesh renders its OWN effect's colour -> Blue");

        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    ModelTwoMeshesEffectsTest game;
    game.Run();
    return game.getResult();
}
