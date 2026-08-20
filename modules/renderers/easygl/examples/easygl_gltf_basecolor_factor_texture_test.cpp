// SPDX-License-Identifier: MS-PL
// plans/plan_gltf.md GLTF-218: a generated corpus material through the whole runtime render path.
//
// The fixture's texture byte 128 decodes to 0.2158605 linear. Its independently authored linear
// baseColorFactor is 0.5, so the shader must encode 0.10793025 and produce byte 92. Ignoring the
// factor gives 128; multiplying after output encoding gives 64; decoding the factor gives 61.

#include "common/PixelTestGame.hpp"

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

class GltfBaseColorFactorTextureTest final : public CNA::Examples::PixelTestGame
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

        if (!ExpectTrue("factor-times-texture fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("factor-times-texture fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        auto* effect = dynamic_cast<PbrEffect*>(
            mesh->getMeshPartsProperty()[0]->getEffectProperty());
        if (!ExpectTrue("textured metallic-roughness primitive selects PbrEffect", effect != nullptr))
            return;

        const Vector3 factor = effect->getDiffuseColorProperty();
        if (!ExpectTrue("loader carries the independent linear baseColorFactor",
                        std::abs(factor.X - 0.5f) < 1e-6f
                            && std::abs(factor.Y - 0.5f) < 1e-6f
                            && std::abs(factor.Z - 0.5f) < 1e-6f
                            && std::abs(effect->getAlphaProperty() - 1.0f) < 1e-6f))
            return;
        if (!ExpectTrue("loader binds the independently-authored baseColorTexture",
                        effect->getTextureProperty() != nullptr
                            && effect->getBaseColorTextureIsSrgbEXTProperty()))
            return;

        // Ambient=1 makes the fragment exactly decodedTexture*factor; all BRDF lights and
        // emissive terms are parked. The imported texture and factor themselves stay untouched.
        effect->setAmbientLightColorProperty(Vector3::One);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);
        effect->setEncodeOutputToSrgbEXTProperty(true);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // Move the source right triangle so NDC centre is strictly inside, not on an edge.
        const Matrix world = Matrix::CreateTranslation(-0.25f, -0.25f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);

        for (int pass = 0; pass < 2; ++pass)
        {
            device.Clear(Color(0, 255, 0, 255));
            model.Draw(world, view, projection);
            ExpectPixel(pass == 0 ? "corpus factor-times-texture first draw"
                                  : "corpus factor-times-texture repeated draw",
                        centre, Color(92, 92, 92, 255), 2);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfBaseColorFactorTextureTest>();
}
