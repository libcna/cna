// SPDX-License-Identifier: MS-PL
// Task 813: Pixel test -- a Model whose CHILD mesh's ParentBone has a non-identity transform,
// proving Model::Draw actually applies that bone's ABSOLUTE transform when positioning the mesh,
// not just always rendering every mesh at the root bone's identity transform. Bgfx counterpart of
// Task 812 above, closing the Phase 72 Model row group (812-813).
//
// Bgfx-specific adaptation of examples/easygl_model_hierarchy_child_mesh_test.cpp (Task 439,
// already reused verbatim on Vulkan). Not a verbatim reuse for the same two reasons as Task 812's
// file: (1) SetDepthTestEnabled(false) throws on Bgfx -- replaced with the equivalent
// DepthStencilState-based call; (2) both sample points are read after a single Draw() call in one
// rendered frame, incompatible with Bgfx's GetBackBufferData "first read per rendered frame" quirk
// (Task 406) -- restructured into one separately-read RunCheck() pass per sample point, each redoing
// Clear() + model.Draw() from scratch.
//
// Fixture: root bone (identity) -> child bone (Translate(0.6, 0, 0)), wired via ModelBone::AddChild
// so CopyAbsoluteBoneTransformsTo genuinely composes child's transform with root's. Two small
// (non-overlapping) quads of the same local shape: meshRoot (Red, ParentBone left null -> defaults
// to root/bone 0, Task 431's finding) and meshChild (Blue, ParentBone explicitly the child bone).
// If Model::Draw correctly resolves each mesh's own ParentBone's absolute transform, meshChild
// renders shifted right by the child bone's translation; if it doesn't (e.g. always used bone 0),
// meshChild renders on top of meshRoot's position instead and the shifted sample stays background.
//
// Exit code 0 = all 2 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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

static constexpr int kSize = 64;

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

class BgfxModelHierarchyChildMeshTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<IndexBuffer> ib_;
    bool done_   = false;
    int  result_ = 1;

    static std::unique_ptr<VertexBuffer> MakeQuad(GraphicsDevice& device, Color colour)
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

    Color RunCheck(GraphicsDevice& dev, float ndcX, float ndcY)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kGreen);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding: NDC quad winding is CCW/back-facing under CNA's real default
            // RasterizerState -- needs CullNone.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            auto vbRoot  = MakeQuad(dev, kRed);
            auto vbChild = MakeQuad(dev, kBlue);

            BasicEffect fxRoot(dev);
            fxRoot.VertexColorEnabled = true;
            BasicEffect fxChild(dev);
            fxChild.VertexColorEnabled = true;

            ModelBone root (0, "root");
            ModelBone child(1, "child");
            // Child bone shifts 0.6 NDC units to the right of root -- far enough that its quad
            // (half-width 0.25) never overlaps root's own quad, also centred at the origin.
            child.setTransformProperty(Matrix::CreateTranslation(0.6f, 0.0f, 0.0f));
            root.AddChild(&child);

            ModelMeshPart partRoot(vbRoot.get(), ib_.get(), 4, 2, 0, 0);
            ModelMesh meshRoot(&dev, "Root", { &partRoot });
            partRoot.setEffectProperty(&fxRoot);

            ModelMeshPart partChild(vbChild.get(), ib_.get(), 4, 2, 0, 0);
            ModelMesh meshChild(&dev, "Child", { &partChild });
            partChild.setEffectProperty(&fxChild);

            // meshRoot's ParentBone stays null (defaults to root/bone 0, Task 431's finding);
            // meshChild's ParentBone is explicitly the child bone -- proving Model::Draw resolves
            // EACH mesh's own bone, not just always bone 0.
            Model model(&dev, { &root, &child }, { &meshRoot, &meshChild }, { nullptr, &child });
            model.Draw(Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty());

            const auto& vp = dev.getViewportProperty();
            const int W = vp.getWidthProperty();
            const int H = vp.getHeightProperty();
            const int x = static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(W));
            const int y = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(H));

            const Rectangle reg(x, y, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
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

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        int passCount = 0;
        const int totalChecks = 2;

        const Color atRoot = RunCheck(dev, 0.0f, 0.0f);
        const bool rootOk = colourMatch(atRoot, kRed);
        std::printf("[%s] root mesh renders at its bone's identity transform -> Red at centre: got=(%d,%d,%d)\n",
                    rootOk ? "PASS" : "FAIL",
                    atRoot.getRProperty(), atRoot.getGProperty(), atRoot.getBProperty());
        if (rootOk) ++passCount;

        const Color atChildShifted = RunCheck(dev, 0.6f, 0.0f);
        const bool childOk = colourMatch(atChildShifted, kBlue);
        std::printf("[%s] child mesh renders at its OWN parent bone's absolute transform -> Blue, shifted right: got=(%d,%d,%d)\n",
                    childOk ? "PASS" : "FAIL",
                    atChildShifted.getRProperty(), atChildShifted.getGProperty(), atChildShifted.getBProperty());
        if (childOk) ++passCount;

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    BgfxModelHierarchyChildMeshTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxModelHierarchyChildMeshTest game;
    game.Run();
    return game.getResult();
}
