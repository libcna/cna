// SPDX-License-Identifier: MS-PL
// Task 728: Verify Model::Draw throws correctly on SDL_Renderer.
//
// Model::Draw requires real 3D primitives to reach any throw: Model -> ModelMesh::Draw ->
// GraphicsDevice::SetVertexBuffer/DrawIndexedPrimitives, per mesh part with an assigned Effect
// and a positive PrimitiveCount (parts with no Effect or PrimitiveCount<=0 are silently skipped,
// matching FNA's own ModelMesh.Draw). Since VertexBuffer/IndexBuffer construction itself throws
// immediately on this renderer (Tasks 720-723), a real GPU-backed VertexBuffer/IndexBuffer can
// never exist here -- so this test mirrors the EasyGL integration test's structure
// (examples/easygl_model_draw_test.cpp: 2-bone hierarchy, 1 mesh, 1 part, BasicEffect) but passes
// nullptr for the part's VertexBuffer*/IndexBuffer* (ModelMeshPart accepts raw pointers, no
// construction involved) -- ModelMesh::Draw's own `SetVertexBuffer(nullptr)` is safe (guarded),
// so the call chain reaches GraphicsDevice::DrawIndexedPrimitives with no vertex buffer bound,
// throwing the SAME shared (not SDL_Renderer-specific) "no vertex buffer is bound" message
// established by Task 720. Confirmed zero existing SDL_Renderer test coverage of Model::Draw
// (ModelMeshTests.cpp never calls Draw() at all, always passes a null GraphicsDevice), so this is
// safe new coverage with no blast radius, unlike Task 725's situation.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlModelDrawThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        // 2-bone hierarchy: root -> child (mirrors easygl_model_draw_test.cpp's structure).
        ModelBone bone0(0, "root");
        ModelBone bone1(1, "child");
        bone0.AddChild(&bone1);

        // nullptr VertexBuffer/IndexBuffer: a real one can never be constructed on this renderer
        // (Tasks 720-723). numVertices=4, primitiveCount=2 (> 0, so this part is NOT skipped).
        ModelMeshPart part(nullptr, nullptr, 4, 2, 0, 0);
        ModelMesh mesh(&dev, { &part });
        part.setEffectProperty(&fx);

        Model model(&dev, { &bone0, &bone1 }, { &mesh });

        std::string message;
        try
        {
            model.Draw(Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty(),
                       Matrix::getIdentityProperty());
        }
        catch (const std::exception& e) { message = e.what(); }
        check(message == "GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound.",
              "Model::Draw throws the exact expected message when its mesh part has no bound vertex buffer");

        // --- The device must remain fully usable after the throw above. ---
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after the throw above");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlModelDrawThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlModelDrawThrowsTest game;
    game.Run();
    return game.getResult();
}
