// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-184/336: distinct base-colour and normal-map transforms at the real sampler.
//
// At authored UV (.25,.75), the base transform maps to (-.25,.875), which PointClamp resolves to
// the reference texture's blue bottom-left quadrant. The normal transform maps to
// (1.553,.913), its +Z bottom-right quadrant. Reusing the base transform for the normal map instead
// samples its +X bottom-left quadrant, turning the front-lit second pass nearly dark.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfTextureTransformPerMapTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("texture-transform-per-map");
        if (!ExpectTrue("per-map transform fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("per-map transform fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        auto* effect = dynamic_cast<PbrEffect*>(
            mesh->getMeshPartsProperty()[0]->getEffectProperty());
        if (!ExpectTrue("fixture binds independent base-colour and normal textures",
                        effect != nullptr && effect->getTextureProperty() != nullptr
                            && effect->getNormalMapProperty() != nullptr))
            return;

        Texture2D* const importedNormalMap = effect->getNormalMapProperty();
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        effect->setDiffuseColorProperty(Vector3::One);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->setMetallicFactorProperty(0.0f);
        effect->setRoughnessFactorProperty(1.0f);
        effect->setEncodeOutputToSrgbEXTProperty(true);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);

        // Put source position/UV (.25,.75) at the framebuffer centre. Ambient=1 and no normal map
        // isolate the base-colour sample; linear decode plus output encode round-trips its bytes.
        const Matrix world = Matrix::CreateTranslation(-0.25f, -0.75f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);
        effect->setNormalMapProperty(nullptr);
        effect->setAmbientLightColorProperty(Vector3::One);
        device.Clear(Color(0, 255, 0, 255));
        model.Draw(world, view, projection);
        ExpectPixel("base-colour map applies its own transform", centre,
                    Color(40, 70, 220, 255), 2);

        // Remove the coloured base operand and restore the imported normal map. Its own transform
        // selects +Z, whose front-lit white dielectric produces byte 151 on this established PBR
        // witness. A shared/base transform selects +X instead and cannot satisfy this value.
        effect->setTextureProperty(nullptr);
        effect->setNormalMapProperty(importedNormalMap);
        effect->setNormalScaleEXTProperty(1.0f);
        effect->setAmbientLightColorProperty(Vector3::Zero);
        effect->DirectionalLight0.setEnabledProperty(true);
        effect->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect->DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        device.Clear(Color(0, 255, 0, 255));
        model.Draw(world, view, projection);
        ExpectPixel("normal map applies its different transform", centre,
                    Color(151, 151, 151, 255), 3);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfTextureTransformPerMapTest>();
}
