// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-54: real-device Model/ModelMesh/ModelMeshPart/ModelBone orchestration
// test, mirroring D3D11's own DX-128 and D3D12's DX-148 (examples/directx12_smoke_test.cpp, Check KK6).
//
// Drives ModelMesh::Draw()'s REAL orchestration (SetVertexBuffer + setIndicesProperty +
// DrawIndexedPrimitives + EffectPass::Apply through a bone-transformed world matrix), not a raw
// VertexBuffer draw wearing a Model label -- which would prove nothing new. A real 2-bone hierarchy
// (root -> child) is built so Model::Draw() genuinely exercises the bone path, not just a
// single-identity-bone no-op. D3D12's own version of this exact test found a real crash (calling
// through several previously-unimplemented state-setter stubs) that nothing else in that renderer's
// test suite happened to exercise, so this is a real "does the orchestration code path actually
// work end to end" check, not a formality.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DiligentModelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        const Color red(255, 0, 0, 255);
        const VertexPositionColor verts[4] = {
            {Vector3(-1.0f, 1.0f, 0.0f), red},
            {Vector3(-1.0f, -1.0f, 0.0f), red},
            {Vector3(1.0f, -1.0f, 0.0f), red},
            {Vector3(1.0f, 1.0f, 0.0f), red},
        };
        VertexBuffer vb(device, 4);
        vb.SetData(verts, 4);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer ib(device, 6);
        ib.SetData(indices, 6);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;

        // A real 2-bone hierarchy: root -> child. Model::Draw multiplies the mesh's absolute bone
        // transform into the world matrix, so this genuinely exercises the bone path.
        ModelBone bone0(0, "root");
        ModelBone bone1(1, "child");
        bone0.AddChild(&bone1);

        ModelMeshPart part(&vb, &ib, /*numVertices=*/4, /*primitiveCount=*/2, /*startIndex=*/0,
                           /*vertexOffset=*/0);
        ModelMesh mesh(&device, {&part});
        part.setEffectProperty(&effect);
        Model model(&device, {&bone0, &bone1}, {&mesh});

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        model.Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                  Matrix::getIdentityProperty());

        const auto& viewport = device.getViewportProperty();
        const Rectangle region(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1,
                               1);
        Color centre(0, 0, 0, 0);
        device.GetBackBufferData(&region, &centre, 0, 1);

        const bool ok = centre.getRProperty() >= 200 && centre.getGProperty() <= 60 &&
                        centre.getBProperty() <= 60;
        std::printf("[%s] Model::Draw() -> ModelMesh::Draw()'s real orchestration (bone transform + "
                    "SetVertexBuffer + setIndices + DrawIndexedPrimitives + EffectPass::Apply) "
                    "paints the mesh's exact red over the green clear: centre=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", centre.getRProperty(), centre.getGProperty(),
                    centre.getBProperty());

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    DiligentModelTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentModelTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
