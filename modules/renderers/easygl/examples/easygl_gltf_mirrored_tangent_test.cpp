// SPDX-License-Identifier: MS-PL
// plans/plan_gltf.md GLTF-176: negative-determinant placement must correct tangent handedness per draw.
//
// The fixture shares one local (+X,+1) tangent buffer between an ordinary node and
// T(3,0,0)*S(-1,1,1). The mirrored draw transforms T to -X and must also multiply tangent.w by
// sign(det(world))=-1, keeping the reconstructed world bitangent at +Y. Without that correction
// its common tangent-space +Y normal map instead faces -Y and the mirrored sample is black.

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

class GltfMirroredTangentTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("tangent-mirrored");
        if (!ExpectTrue("two node placements become two drawable meshes",
                        model.getMeshesProperty().getCountProperty() == 2))
            return;

        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, whitePixel);
        int configuredParts = 0;
        for (ModelMesh* mesh : model.getMeshesProperty())
        {
            if (mesh == nullptr) { continue; }
            for (ModelMeshPart* part : mesh->getMeshPartsProperty())
            {
                auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty());
                if (!ExpectTrue("each mirrored-tangent placement selects PbrEffect", effect != nullptr
                                && effect->getNormalMapProperty() != nullptr))
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
                ++configuredParts;
            }
        }
        if (!ExpectTrue("both placements expose one configured part", configuredParts == 2))
            return;

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 200.0f);

        const auto DrawAtCentre = [&](const char* label, const Vector3& sample) {
            const Matrix view = Matrix::CreateLookAt(
                sample + Vector3(0.0f, 0.0f, 100.0f), sample, Vector3(0.0f, 1.0f, 0.0f));
            device.Clear(Color(0, 255, 0, 255));
            model.Draw(Matrix::getIdentityProperty(), view, projection);
            ExpectPixel(label, centre, Color(151, 151, 151, 255), 3);
        };

        // Both points are strictly inside their respective triangles. Moving each to framebuffer
        // centre also makes V exactly +Z, matching GLTF-175's analytic rough-dielectric byte 151.
        DrawAtCentre("ordinary placement reconstructs +Y bitangent",
                     Vector3(0.25f, 0.25f, 0.0f));
        DrawAtCentre("mirrored placement preserves +Y via determinant sign",
                     Vector3(2.75f, 0.25f, 0.0f));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfMirroredTangentTest>();
}
