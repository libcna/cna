// SPDX-License-Identifier: MS-PL
// plan_headless.md HEADLESS-51/64: exercises AlphaTestEffect, DualTextureEffect,
// EnvironmentMapEffect, SkinnedEffect, and Model.Draw() end-to-end against the HEADLESS backend
// (through real GraphicsDevice::DrawPrimitives/DrawIndexedPrimitives calls, not just constructing
// the effect and calling Apply() in isolation). Previously these were only an inference from
// "they share the same DrawPrimitivesEx/DrawIndexedPrimitivesEx path already proven for
// BasicEffect", not individually verified.
//
// Each of DualTextureEffect/EnvironmentMapEffect/SkinnedEffect unconditionally sets
// TextureEnabled (plus its own extra flag) in FillGpuDrawParams() regardless of whether a texture
// was actually assigned -- so HeadlessGraphicsBackend::DrawPrimitivesEx's HEADLESS-22 validation
// (Require(!(textureEnabled && texture0==nullptr), ...) etc.) genuinely trips if the game forgot
// to set one, and does not trip once it's set. AlphaTestEffect only sets TextureEnabled when a
// texture was actually assigned, so it degrades gracefully either way.
//
// Check A -- AlphaTestEffect draws without a texture set: does not throw (texture is optional).
// Check B -- AlphaTestEffect draws with a texture set: does not throw.
// Check C -- DualTextureEffect draws without setTexture2Property(): throws under
//   HeadlessValidation (TextureEnabled/DualTexture are unconditionally true but texture0/texture1
//   are still null).
// Check D -- DualTextureEffect draws with both setTextureProperty()/setTexture2Property() set:
//   does not throw.
// Check E -- EnvironmentMapEffect draws without setEnvironmentMapProperty(): throws.
// Check F -- EnvironmentMapEffect draws with both setTextureProperty()/setEnvironmentMapProperty()
//   set: does not throw.
// Check G -- SkinnedEffect draws without setTextureProperty(): throws (bone count is fine by
//   default -- the constructor seeds 72 identity bone transforms -- but TextureEnabled is still
//   unconditionally true).
// Check H -- SkinnedEffect draws with a texture set: does not throw.
// Check I -- a procedurally-built Model (2 ModelBones, 1 ModelMesh, 1 ModelMeshPart, BasicEffect)
//   draws via Model::Draw() -> ModelMesh::Draw() -> GraphicsDevice::DrawIndexedPrimitives() without
//   throwing, and drawCallCount increases by exactly 1.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Headless;

class HeadlessEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static bool ThrowsOnDraw(GraphicsDevice& dev, VertexBuffer& vb)
    {
        bool threw = false;
        try
        {
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
        catch (const HeadlessValidationException&) { threw = true; }
        dev.SetVertexBuffer(nullptr);
        return threw;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<HeadlessGraphicsBackend&>(dev.GetBackend());
        dev.Clear(Color::Black);

        Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});

        // Checks A/B: AlphaTestEffect -- texture is optional.
        {
            const VertexPositionTexture verts[3] = {
                { Vector3(0.0f, 1.0f, 0.0f), Vector2(0.5f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            VertexBuffer vbNoTex(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbNoTex.SetData(verts, 3);
            AlphaTestEffect fxNoTex(dev);
            fxNoTex.Apply();
            check(!ThrowsOnDraw(dev, vbNoTex), "AlphaTestEffect draws without a texture set (optional)");

            VertexBuffer vbTex(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbTex.SetData(verts, 3);
            AlphaTestEffect fxTex(dev);
            fxTex.setTextureProperty(&tex);
            fxTex.Apply();
            check(!ThrowsOnDraw(dev, vbTex), "AlphaTestEffect draws with a texture set");
        }

        // Checks C/D: DualTextureEffect -- both textures are effectively mandatory.
        {
            const VertexPositionTexture verts[3] = {
                { Vector3(0.0f, 1.0f, 0.0f), Vector2(0.5f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            VertexBuffer vbMissing(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbMissing.SetData(verts, 3);
            DualTextureEffect fxMissing(dev);
            fxMissing.setTextureProperty(&tex);
            // setTexture2Property() deliberately not called.
            fxMissing.Apply();
            check(ThrowsOnDraw(dev, vbMissing),
                  "DualTextureEffect draws without Texture2 set throws under HeadlessValidation");

            VertexBuffer vbBoth(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbBoth.SetData(verts, 3);
            DualTextureEffect fxBoth(dev);
            fxBoth.setTextureProperty(&tex);
            fxBoth.setTexture2Property(&tex);
            fxBoth.Apply();
            check(!ThrowsOnDraw(dev, vbBoth), "DualTextureEffect draws with both textures set");
        }

        // Checks E/F: EnvironmentMapEffect -- texture + environment cube map are both mandatory.
        {
            const VertexPositionNormalTexture verts[3] = {
                { Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.5f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f) },
            };
            VertexBuffer vbMissing(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbMissing.SetData(verts, 3);
            EnvironmentMapEffect fxMissing(dev);
            fxMissing.setTextureProperty(&tex);
            // setEnvironmentMapProperty() deliberately not called.
            fxMissing.Apply();
            check(ThrowsOnDraw(dev, vbMissing),
                  "EnvironmentMapEffect draws without an environment map set throws under HeadlessValidation");

            TextureCube cube(dev, 4, false, SurfaceFormat::Color);
            VertexBuffer vbBoth(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vbBoth.SetData(verts, 3);
            EnvironmentMapEffect fxBoth(dev);
            fxBoth.setTextureProperty(&tex);
            fxBoth.setEnvironmentMapProperty(&cube);
            fxBoth.Apply();
            check(!ThrowsOnDraw(dev, vbBoth), "EnvironmentMapEffect draws with texture and environment map set");
        }

        // Checks G/H: SkinnedEffect -- bone transforms default to 72 identities (never the
        // problem), but the texture is still effectively mandatory (TextureEnabled unconditional).
        {
            const Vector4 fullWeight(1.0f, 0.0f, 0.0f, 0.0f);
            const std::array<std::uint8_t, 4> boneZero{0, 0, 0, 0};
            const VertexPositionNormalTextureSkinned verts[3] = {
                { Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.5f, 0.0f), fullWeight, boneZero },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f), fullWeight, boneZero },
                { Vector3(1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f), fullWeight, boneZero },
            };
            VertexBuffer vbMissing(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 3,
                                   BufferUsage::None);
            vbMissing.SetData(verts, 3);
            SkinnedEffect fxMissing(dev);
            // setTextureProperty() deliberately not called.
            fxMissing.Apply();
            check(ThrowsOnDraw(dev, vbMissing),
                  "SkinnedEffect draws without a texture set throws under HeadlessValidation");

            VertexBuffer vbTex(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 3,
                               BufferUsage::None);
            vbTex.SetData(verts, 3);
            SkinnedEffect fxTex(dev);
            fxTex.setTextureProperty(&tex);
            fxTex.Apply();
            check(!ThrowsOnDraw(dev, vbTex), "SkinnedEffect draws with a texture set (default bone transforms are fine)");
        }

        // Check I: procedurally-built Model -> Model::Draw() -> ModelMesh::Draw() ->
        // GraphicsDevice::DrawIndexedPrimitives(), matching examples/easygl_model_draw_test.cpp's
        // own proven 2-bone/1-mesh/1-part shape.
        {
            const Color red(255, 0, 0, 255);
            const VertexPositionColor quadVerts[4] = {
                { Vector3(-1.0f, 1.0f, 0.0f), red },
                { Vector3(-1.0f, -1.0f, 0.0f), red },
                { Vector3(1.0f, -1.0f, 0.0f), red },
                { Vector3(1.0f, 1.0f, 0.0f), red },
            };
            VertexBuffer vb(dev, 4);
            vb.SetData(quadVerts, 4);
            const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
            IndexBuffer ib(dev, 6);
            ib.SetData(indices, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;

            ModelBone bone0(0, "root");
            ModelBone bone1(1, "child");
            bone0.AddChild(&bone1);

            ModelMeshPart part(&vb, &ib, 4, 2, 0, 0);
            ModelMesh mesh(&dev, {&part});
            part.setEffectProperty(&fx);

            Model model(&dev, {&bone0, &bone1}, {&mesh});

            const std::uint64_t before = backend.GetStatistics().drawCallCount;
            bool threw = false;
            try
            {
                model.Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), Matrix::getIdentityProperty());
            }
            catch (const HeadlessValidationException&) { threw = true; }
            const std::uint64_t after = backend.GetStatistics().drawCallCount;
            check(!threw && after == before + 1,
                  "a procedurally-built Model draws via Model::Draw() with exactly 1 draw call, no throw");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 9);
        result_ = (passCount_ == 9) ? 0 : 1;
        Exit();
    }

public:
    HeadlessEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessEffectsTest game;
    game.Run();
    return game.getResult();
}
