// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-189: the three generated out-of-range UV fixtures at the real sampler.
//
// At source position (13/15,14/15), the shared authored UV is (1.05,1.6). CLAMP therefore samples
// the reference image's bottom-right yellow quadrant, REPEAT its bottom-left blue quadrant, and
// MIRRORED_REPEAT its top-right green quadrant. The chosen point is inside the quad and every
// resolved coordinate is well away from a quadrant boundary and the white numeral glyphs.

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
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfSamplerWrapTest final : public CNA::Examples::PixelTestGame
{
    void RunFixture(const char* fixture,
                    TextureAddressMode expectedMode,
                    const Color& expectedPixel)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        Model model = getContentProperty().Load<Model>(fixture);
        if (!ExpectTrue("sampler fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("sampler fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        ModelMeshPart* part = mesh->getMeshPartsProperty()[0];
        auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty());
        if (!ExpectTrue("sampler fixture selects textured PbrEffect", effect != nullptr
                        && effect->getTextureProperty() != nullptr))
            return;

        // GLTF-208 deliberately carries sampler state on the part without making Model::Draw
        // overwrite application-owned device state. Exercise that public CNAEXT boundary exactly:
        // the application selects the imported base-colour slot immediately before the draw.
        const SamplerState& sampler = part->getSamplerStatesEXTProperty()[0];
        if (!ExpectTrue("fixture carries linear filtering and the expected U/V address mode",
                        sampler.getFilterProperty() == TextureFilter::Linear
                            && sampler.getAddressUProperty() == expectedMode
                            && sampler.getAddressVProperty() == expectedMode))
            return;
        device.getSamplerStatesProperty()[0] = sampler;

        // Ambient=1 isolates the texture sample. Decode+encode round-trips the reference PNG's
        // sRGB bytes, so the expected values below are the generator's authored quadrant colours.
        effect->setAmbientLightColorProperty(Vector3::One);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->setMetallicFactorProperty(0.0f);
        effect->setRoughnessFactorProperty(1.0f);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);
        effect->setEncodeOutputToSrgbEXTProperty(true);

        device.Clear(Color(0, 255, 0, 255));
        const Matrix world = Matrix::CreateTranslation(-13.0f / 15.0f, -14.0f / 15.0f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);
        model.Draw(world, view, projection);
        ExpectPixel(fixture, centre, expectedPixel, 3);
    }

protected:
    void RunTest() override
    {
        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        RunFixture("uv-out-of-range-clamp", TextureAddressMode::Clamp,
                   Color(230, 190, 20, 255));
        RunFixture("uv-out-of-range-wrap", TextureAddressMode::Wrap,
                   Color(40, 70, 220, 255));
        RunFixture("uv-out-of-range-mirror", TextureAddressMode::Mirror,
                   Color(30, 160, 60, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfSamplerWrapTest>();
}
