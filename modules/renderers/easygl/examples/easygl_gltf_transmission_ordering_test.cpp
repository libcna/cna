// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-340: the opaque dial is visible through nearer approximated glass only when
// the application draws opaque first and transmission second.
//
// The committed material-variant fixture supplies both source-authored states without adding an
// ad-hoc test asset: opaque red is the dial and blue KHR_materials_transmission=0.5 is the glass.
// CNA intentionally approximates transmission as straight alpha. Drawing glass first also writes
// its nearer depth, so the later dial is rejected; that is the exact ordering failure this test
// keeps visibly distinct from a correct back-to-front composite.

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

class GltfTransmissionOrderingTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("mat-material-variants");
        if (!ExpectTrue("layering fixture loads one mesh and one part",
                        model.getMeshesProperty().getCountProperty() == 1
                            && model.getMeshesProperty()[0] != nullptr
                            && model.getMeshesProperty()[0]->getMeshPartsProperty()
                                   .getCountProperty() == 1))
            return;

        ModelMeshPart* part = model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
        const auto SelectLayer = [&](int variant, const char* label) -> PbrEffect* {
            model.setMaterialVariantEXTProperty(variant);
            auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty());
            if (!ExpectTrue(label, effect != nullptr)) { return nullptr; }
            effect->setAmbientLightColorProperty(Vector3::One);
            effect->setEmissiveFactorProperty(Vector3::Zero);
            effect->DirectionalLight0.setEnabledProperty(false);
            effect->DirectionalLight1.setEnabledProperty(false);
            effect->DirectionalLight2.setEnabledProperty(false);
            effect->setEncodeOutputToSrgbEXTProperty(false);
            return effect;
        };

        PbrEffect* dial = SelectLayer(-1, "default variant supplies the opaque PBR dial");
        if (dial == nullptr) { return; }
        if (!ExpectTrue("dial is source-authored opaque red",
                        dial->getAlphaModeEXTProperty() == AlphaModeEXT::Opaque
                            && std::abs(dial->getAlphaProperty() - 1.0f) < 1e-6f))
            return;

        PbrEffect* glass = SelectLayer(0, "variant zero supplies the PBR glass");
        if (glass == nullptr) { return; }
        if (!ExpectTrue("transmission becomes source-carried straight half alpha",
                        glass->getAlphaModeEXTProperty() == AlphaModeEXT::Blend
                            && std::abs(glass->getAlphaProperty() - 0.5f) < 1e-6f))
            return;

        device.SetDepthTestEnabled(true);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);
        const auto DrawLayer = [&](int variant, float z, const BlendState& blend) {
            PbrEffect* effect = SelectLayer(variant, "selected layer remains PBR");
            if (effect == nullptr) { return; }
            device.setBlendStateProperty(blend);
            model.Draw(Matrix::CreateTranslation(-0.25f, -0.25f, z), view, projection);
        };

        // Wrong order: the nearer glass writes depth first. The opaque dial is then rejected and
        // blue (.05,.2,.9) at alpha .5 is composited only over black: (.025,.1,.45).
        device.Clear(Color(0, 0, 0, 255));
        DrawLayer(0, 0.1f, BlendState::NonPremultiplied);
        DrawLayer(-1, 0.0f, BlendState::Opaque);
        ExpectPixel("front-to-back order hides the dial behind glass depth",
                    centre, Color(6, 26, 115, 255), 2);

        // Correct order: opaque red dial (.6,.1,.1) first, then straight-alpha blue glass:
        // .5*(.05,.2,.9) + .5*(.6,.1,.1) = (.325,.15,.5).
        device.Clear(Color(0, 0, 0, 255));
        DrawLayer(-1, 0.0f, BlendState::Opaque);
        DrawLayer(0, 0.1f, BlendState::NonPremultiplied);
        ExpectPixel("back-to-front order keeps the dial visible through glass",
                    centre, Color(83, 38, 128, 255), 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfTransmissionOrderingTest>();
}
