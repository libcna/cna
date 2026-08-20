// SPDX-License-Identifier: MS-PL
// plans/plan_gltf.md GLTF-439: a loaded glTF model survives a genuine EasyGL context loss.
//
// This is intentionally not a HEADLESS/STUB lifetime test: their context-loss hook is a no-op.
// EasyGL's desktop hook destroys the SDL GL context, creates a fresh one, then rebuilds registered
// resources from their CPU shadows. The same long-lived Model must therefore draw the same imported
// vertex/index buffers and base-colour texture before and after the loss; reloading the asset after
// recovery would only prove that a second load works and could hide dangling first-load resources.

#include "common/PixelTestGame.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfContextLossTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("mat-basecolor-factor-times-texture");

        if (!ExpectTrue("context-loss fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("context-loss fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        ModelMeshPart* part = mesh->getMeshPartsProperty()[0];
        auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty());
        if (!ExpectTrue("context-loss fixture selects a textured PbrEffect",
                        effect != nullptr && effect->getTextureProperty() != nullptr))
            return;

        // This fixture's independent L7 witness establishes byte 92: sRGB texture byte 128 is
        // decoded to linear, multiplied by the authored 0.5 factor, then encoded to sRGB again.
        effect->setAmbientLightColorProperty(Vector3::One);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);
        effect->setEncodeOutputToSrgbEXTProperty(true);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix world = Matrix::CreateTranslation(-0.25f, -0.25f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);

        device.Clear(Color(0, 255, 0, 255));
        model.Draw(world, view, projection);
        if (!ExpectPixel("loaded glTF before context loss", centre, Color(92, 92, 92, 255), 2))
            return;

        VertexBuffer* const vertexBuffer = part->getVertexBufferProperty();
        IndexBuffer* const indexBuffer = part->getIndexBufferProperty();
        Texture2D* const texture = effect->getTextureProperty();
        device.GetRenderer().DebugSimulateContextLoss();

        if (!ExpectTrue("context recovery preserves the original model resource objects",
                        part->getVertexBufferProperty() == vertexBuffer
                            && part->getIndexBufferProperty() == indexBuffer
                            && effect->getTextureProperty() == texture))
            return;

        device.Clear(Color(0, 255, 0, 255));
        model.Draw(world, view, projection);
        ExpectPixel("same loaded glTF after context loss", centre, Color(92, 92, 92, 255), 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfContextLossTest>();
}
