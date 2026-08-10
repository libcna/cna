// SPDX-License-Identifier: MS-PL
// Task 439: Pixel test -- a Model whose CHILD mesh's ParentBone has a non-identity transform,
// proving Model::Draw actually applies that bone's ABSOLUTE transform when positioning the mesh,
// not just always rendering every mesh at the root bone's identity transform.
//
// This test only became possible after a real gap found while designing it: ModelMesh::parentBone_
// (already `friend class Model;` for exactly this purpose) was never actually assigned by ANY
// public construction path -- not even Model's own 3-arg constructor -- so ModelMesh::ParentBone
// was permanently nullptr for every hand-built model, making Model::Draw's
// `mesh->getParentBoneProperty() ? ... : 0` branch dead code. Fixed via a new 4-arg Model
// constructor overload (Model.hpp/.cpp) taking a parallel per-mesh parent-bone vector; see the new
// ModelTest.FourArgConstructor* unit tests in ModelTests.cpp for the isolated, non-GPU coverage of
// that constructor itself.
//
// Fixture: root bone (identity) -> child bone (Translate(0.6, 0, 0)), wired via ModelBone::AddChild
// so CopyAbsoluteBoneTransformsTo genuinely composes child's transform with root's. Two small
// (non-overlapping) quads of the same local shape: meshRoot (Red, ParentBone left null -> defaults
// to root/bone 0, Task 431's finding) and meshChild (Blue, ParentBone explicitly the child bone).
// If Model::Draw correctly resolves each mesh's own ParentBone's absolute transform, meshChild
// renders shifted right by the child bone's translation; if it doesn't (e.g. always used bone 0),
// meshChild renders on top of meshRoot's position instead and the shifted sample stays background.
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
    const Color kRed  (255,   0,   0, 255);
    const Color kBlue (  0,   0, 255, 255);
    const Color kGreen(  0, 255,   0, 255); // background

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }
}

class ModelHierarchyChildMeshTest : public Game
{
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer>  ib_;
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

        // A small quad, -0.25..0.25 in both axes, centred on its own LOCAL origin -- shared shape
        // for both meshes (colour is applied per-vertex, per-mesh, via a distinct VertexBuffer).
        const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);
    }

    std::unique_ptr<VertexBuffer> MakeQuad(GraphicsDevice& device, Color colour)
    {
        const VertexPositionColor verts[4] = {
            { Vector3(-0.25f,  0.25f, 0.0f), colour },
            { Vector3(-0.25f, -0.25f, 0.0f), colour },
            { Vector3( 0.25f, -0.25f, 0.0f), colour },
            { Vector3( 0.25f,  0.25f, 0.0f), colour },
        };
        auto vb = std::make_unique<VertexBuffer>(device, 4);
        vb->SetData(verts, 4);
        return vb;
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

        auto vbRoot  = MakeQuad(device, kRed);
        auto vbChild = MakeQuad(device, kBlue);

        BasicEffect fxRoot(device);
        fxRoot.VertexColorEnabled = true;
        BasicEffect fxChild(device);
        fxChild.VertexColorEnabled = true;

        ModelBone root (0, "root");
        ModelBone child(1, "child");
        // Child bone shifts 0.6 NDC units to the right of root -- far enough that its quad
        // (half-width 0.25) never overlaps root's own quad, also centred at the origin.
        child.setTransformProperty(Matrix::CreateTranslation(0.6f, 0.0f, 0.0f));
        root.AddChild(&child);

        ModelMeshPart partRoot(vbRoot.get(), ib_.get(), 4, 2, 0, 0);
        ModelMesh meshRoot(&device, "Root", { &partRoot });
        partRoot.setEffectProperty(&fxRoot);

        ModelMeshPart partChild(vbChild.get(), ib_.get(), 4, 2, 0, 0);
        ModelMesh meshChild(&device, "Child", { &partChild });
        partChild.setEffectProperty(&fxChild);

        // meshRoot's ParentBone stays null (defaults to root/bone 0, Task 431's finding);
        // meshChild's ParentBone is explicitly the child bone -- proving Model::Draw resolves
        // EACH mesh's own bone, not just always bone 0.
        Model model(&device, { &root, &child }, { &meshRoot, &meshChild }, { nullptr, &child });
        model.Draw(Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty());

        auto sample = [&](float ndcX, float ndcY) {
            const int x = static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(W));
            const int y = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(H));
            Color px(0, 0, 0, 0);
            Rectangle reg(x, y, 1, 1);
            device.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        Color atRoot        = sample(0.0f, 0.0f);
        Color atChildShifted = sample(0.6f, 0.0f);

        check(colourMatch(atRoot, kRed),
              "root mesh renders at its bone's identity transform -> Red at centre");
        check(colourMatch(atChildShifted, kBlue),
              "child mesh renders at its OWN parent bone's absolute transform -> Blue, shifted right");

        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    ModelHierarchyChildMeshTest game;
    game.Run();
    return game.getResult();
}
