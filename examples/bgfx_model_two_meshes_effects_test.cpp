// SPDX-License-Identifier: MS-PL
// Task 812: Pixel test -- a Model with two meshes, each using its own distinct Effect, on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_model_two_meshes_effects_test.cpp (Task 438, already
// reused verbatim on Vulkan). Not a verbatim reuse for two reasons: (1) that file uses
// GraphicsDevice::SetDepthTestEnabled(false), which throws on Bgfx ("SetDepthTestEnabled / SetBlend*
// are not yet wired into bgfx state flags" -- a pre-existing, deliberate stub) -- replaced with the
// equivalent DepthStencilState-based call, matching Tasks 752-755's established substitution; (2)
// that file samples 2 spatially-separate points after a SINGLE Draw() call in one rendered frame,
// incompatible with Bgfx's GetBackBufferData "first read per rendered frame" quirk (Task 406) --
// restructured into one separately-read RunCheck() pass per sample point, each redoing Clear() +
// model.Draw() from scratch, mirroring Tasks 759-811's established Bgfx pattern.
//
// Proves Model::Draw iterates every mesh in ModelMeshCollection and applies each mesh's OWN Effect,
// not accidentally sharing/reusing one Effect across meshes or only drawing the first/last mesh.
// Both meshes' ParentBone is left null (Model::Draw defaults a null ParentBone to bone index 0,
// Task 431's audit finding), so both meshes render with the single root bone's identity transform.
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

class BgfxModelTwoMeshesEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vbLeft_,  vbRight_;
    std::unique_ptr<IndexBuffer>  ibLeft_,  ibRight_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int x, int y)
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

            BasicEffect fxLeft(dev);
            fxLeft.VertexColorEnabled = true;
            BasicEffect fxRight(dev);
            fxRight.VertexColorEnabled = true;

            ModelBone root(0, "root");

            ModelMeshPart partLeft(vbLeft_.get(), ibLeft_.get(), 4, 2, 0, 0);
            ModelMesh meshLeft(&dev, "Left", { &partLeft });
            partLeft.setEffectProperty(&fxLeft);

            ModelMeshPart partRight(vbRight_.get(), ibRight_.get(), 4, 2, 0, 0);
            ModelMesh meshRight(&dev, "Right", { &partRight });
            partRight.setEffectProperty(&fxRight);

            Model model(&dev, { &root }, { &meshLeft, &meshRight });
            model.Draw(Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty());

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

        auto& dev = getGraphicsDeviceProperty();

        int passCount = 0;
        const int totalChecks = 2;

        const Color left  = RunCheck(dev, kSize / 4, kSize / 2);
        const bool leftOk = colourMatch(left, kRed);
        std::printf("[%s] left mesh renders its OWN effect's colour -> Red: got=(%d,%d,%d)\n",
                    leftOk ? "PASS" : "FAIL",
                    left.getRProperty(), left.getGProperty(), left.getBProperty());
        if (leftOk) ++passCount;

        const Color right  = RunCheck(dev, 3 * kSize / 4, kSize / 2);
        const bool rightOk = colourMatch(right, kBlue);
        std::printf("[%s] right mesh renders its OWN effect's colour -> Blue: got=(%d,%d,%d)\n",
                    rightOk ? "PASS" : "FAIL",
                    right.getRProperty(), right.getGProperty(), right.getBProperty());
        if (rightOk) ++passCount;

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    BgfxModelTwoMeshesEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxModelTwoMeshesEffectsTest game;
    game.Run();
    return game.getResult();
}
