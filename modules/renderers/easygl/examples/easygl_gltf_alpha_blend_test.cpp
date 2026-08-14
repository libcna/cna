// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-230 / GLTF-231: a committed BLEND + doubleSided fixture through
// application-owned GPU state.
//
// PbrEffect deliberately outputs straight RGB plus coverage alpha. Consequently glTF BLEND needs
// BlendState::NonPremultiplied (SourceAlpha/InverseSourceAlpha), not XNA's premultiplied
// BlendState::AlphaBlend. Model::Draw must preserve the state selected by the application because
// it neither owns the surfaces behind a primitive nor sorts them back-to-front. The same boundary
// applies to glTF doubleSided: the effect carries the bit and the application resolves it to
// RasterizerState without Model::Draw mutating global device state.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfAlphaBlendTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("mat-factor-only-gold");
        if (!ExpectTrue("BLEND fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("BLEND fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        auto* effect = dynamic_cast<PbrEffect*>(
            mesh->getMeshPartsProperty()[0]->getEffectProperty());
        if (!ExpectTrue("BLEND fixture selects PbrEffect", effect != nullptr))
            return;

        const Vector3 factor = effect->getDiffuseColorProperty();
        if (!ExpectTrue("loader carries straight gold RGB, half alpha, BLEND and doubleSided",
                        effect->getAlphaModeEXTProperty() == AlphaModeEXT::Blend
                            && std::abs(factor.X - 1.0f) < 1e-6f
                            && std::abs(factor.Y - 0.72f) < 1e-6f
                            && std::abs(factor.Z - 0.315f) < 1e-6f
                            && std::abs(effect->getAlphaProperty() - 0.5f) < 1e-6f
                            && effect->getDoubleSidedEXTProperty()))
            return;

        // Ambient=1 makes the fragment exactly the imported straight baseColorFactor. Disabling
        // output encoding keeps the blend arithmetic directly observable as framebuffer bytes.
        effect->setAmbientLightColorProperty(Vector3::One);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->DirectionalLight0.setEnabledProperty(false);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);
        effect->setEncodeOutputToSrgbEXTProperty(false);

        device.SetDepthTestEnabled(false);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix world = Matrix::CreateTranslation(-0.25f, -0.25f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);
        const Color blue(0, 0, 255, 255);

        // Negative control: Model::Draw preserves the caller's Opaque state. Straight source
        // (1,.72,.315,.5) therefore writes RGB bytes (255,184,80), ignoring coverage.
        device.Clear(blue);
        device.setBlendStateProperty(BlendState::Opaque);
        model.Draw(world, view, projection);
        ExpectPixel("application-selected Opaque state remains in force",
                    centre, Color(255, 184, 80, 255), 2);

        // glTF BLEND over blue with straight-alpha factors:
        //   src*.5 + dst*.5 = (.5,.36,.6575) -> approximately (128,92,168).
        // The application explicitly establishes both the existing destination and the state,
        // then issues Model::Draw in its chosen order; CNA performs no hidden material sort.
        device.Clear(blue);
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        model.Draw(world, view, projection);
        ExpectPixel("BLEND fixture composites with straight-alpha state",
                    centre, Color(128, 92, 168, 255), 2);

        // Distinguishing control: AlphaBlend assumes premultiplied RGB and would incorrectly add
        // the full straight source, producing approximately (255,184,208) over the same blue.
        device.Clear(blue);
        device.setBlendStateProperty(BlendState::AlphaBlend);
        model.Draw(world, view, projection);
        ExpectPixel("premultiplied preset is observably wrong for straight glTF output",
                    centre, Color(255, 184, 208, 255), 2);

        // Flip only Y around the triangle's screen-space bounds: the same centre pixel stays
        // strictly inside, but the authored CCW front becomes clockwise. Ignoring doubleSided and
        // keeping the ordinary glTF CullClockwiseFace state must therefore remove it completely.
        // This is the distinguishing control that a front-facing-only double-sided test lacks.
        const Matrix backFacingWorld = Matrix::CreateScale(1.0f, -1.0f, 1.0f)
            * Matrix::CreateTranslation(-1.0f / 3.0f, 1.0f / 3.0f, 0.0f);
        const Color black(0, 0, 0, 255);
        device.Clear(black);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullClockwise);
        model.Draw(backFacingWorld, view, projection);
        ExpectPixel("single-sided control culls the back face",
                    centre, black, 0);

        // The application consumes the imported property at the public boundary. Model::Draw
        // still preserves that state; the back face is now the exact straight gold source colour.
        device.Clear(black);
        device.setRasterizerStateProperty(
            effect->getDoubleSidedEXTProperty()
                ? RasterizerState::CullNone
                : RasterizerState::CullClockwise);
        model.Draw(backFacingWorld, view, projection);
        ExpectPixel("doubleSided selects CullNone and renders the same back face",
                    centre, Color(255, 184, 80, 255), 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfAlphaBlendTest>();
}
