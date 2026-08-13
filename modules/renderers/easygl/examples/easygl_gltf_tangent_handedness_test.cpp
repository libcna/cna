// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-175: the committed opposite-TANGENT.w fixture through the real PBR shader.
//
// Its constant normal map decodes to tangent-space approximately +Y. With T=+X and N=+Z, the
// left primitive's w=+1 reconstructs B=+Y and faces a +Y light; the right primitive's w=-1
// reconstructs B=-Y and faces away. Dropping/defaulting/inverting the sign cannot produce the
// required bright-left/black-right pair.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfTangentHandednessTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("tangent-handedness");
        if (!ExpectTrue("tangent-handedness fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("fixture keeps its two independently packed primitives",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 2))
            return;

        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, whitePixel);
        for (int i = 0; i < 2; ++i)
        {
            auto* effect = dynamic_cast<PbrEffect*>(
                mesh->getMeshPartsProperty()[i]->getEffectProperty());
            if (!ExpectTrue("each handedness primitive selects PbrEffect with its normal map",
                            effect != nullptr && effect->getNormalMapProperty() != nullptr))
                return;

            effect->setTextureProperty(&white);
            effect->setDiffuseColorProperty(Vector3::One);
            effect->setAmbientLightColorProperty(Vector3::Zero);
            effect->setEmissiveFactorProperty(Vector3::Zero);
            effect->setMetallicFactorProperty(0.0f);
            effect->setRoughnessFactorProperty(1.0f);
            effect->setNormalScaleEXTProperty(1.0f);
            effect->setEncodeOutputToSrgbEXTProperty(true);
            effect->DirectionalLight0.setEnabledProperty(true);
            effect->DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
            effect->DirectionalLight0.setDiffuseColorProperty(Vector3::One);
            effect->DirectionalLight1.setEnabledProperty(false);
            effect->DirectionalLight2.setEnabledProperty(false);
        }

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 100.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 200.0f);

        // Source (-.75,.25) is strictly inside the left triangle. Moving it to the framebuffer
        // centre also makes V exactly +Z, giving analytic byte 151 for the rough dielectric BRDF.
        device.Clear(Color(0, 255, 0, 255));
        model.Draw(Matrix::CreateTranslation(0.75f, -0.25f, 0.0f), view, projection);
        ExpectPixel("positive tangent handedness reconstructs +Y bitangent",
                    centre, Color(151, 151, 151, 255), 3);

        // The symmetric source point (.75,.25) lies inside the right triangle. Its w=-1 turns
        // the same tangent-space +Y sample into world -Y, so NdotL is zero and the result is black.
        device.Clear(Color(0, 255, 0, 255));
        model.Draw(Matrix::CreateTranslation(-0.75f, -0.25f, 0.0f), view, projection);
        ExpectPixel("negative tangent handedness reconstructs -Y bitangent",
                    centre, Color(0, 0, 0, 255), 0);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfTangentHandednessTest>();
}
