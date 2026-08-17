// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-268: first focused corpus L7 witness for the combined skinned-PBR path.
//
// Unlike the effect-only EasyGL tests, this loads the generated
// tests/assets/gltf/skin-nonuniform-joint-scale.gltf through ContentManager. The fixture already
// has independent L1-L6 oracles: its three vertices pack to the 68-byte skinned-PBR layout and its
// bind-pose palette contains joint S=[1,2,1]. This test exercises the missing end-to-end half:
// ContentManager.Load<Model> -> SkinnedPbrEffect -> automatically-applied bind pose -> Model.Draw
// -> the real EasyGL vertex/fragment programs -> framebuffer readback.
//
// The fixture normal is (0,.6,.8). Inverse-transpose(S) makes it normalize(0,.3,.8) =
// (0,.351123,.936329). With L=+Y, V=+Z, white rough dielectric material and sRGB output, the
// production BRDF gives byte 93. The pre-GLTF-264 direct joint 3x3 gave byte 139, while an identity
// palette gives a third value; therefore neither a dropped skin nor the old normal bug can pass.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GltfSkinnedPbrNonUniformJointTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);

        getContentProperty().setRootDirectoryProperty("tests/assets/gltf");
        Model model = getContentProperty().Load<Model>("skin-nonuniform-joint-scale");

        if (!ExpectTrue("corpus fixture loads exactly one mesh",
                        model.getMeshesProperty().getCountProperty() == 1))
            return;
        ModelMesh* mesh = model.getMeshesProperty()[0];
        if (!ExpectTrue("corpus fixture loads exactly one mesh part",
                        mesh != nullptr && mesh->getMeshPartsProperty().getCountProperty() == 1))
            return;
        auto* effect = dynamic_cast<SkinnedPbrEffect*>(
            mesh->getMeshPartsProperty()[0]->getEffectProperty());
        if (!ExpectTrue("stride-68 corpus primitive selects SkinnedPbrEffect", effect != nullptr))
            return;

        const std::vector<Matrix> bindPose = effect->GetBoneTransforms(1);
        if (!ExpectTrue("runtime loader applies the non-uniform bind-pose joint palette",
                        bindPose.size() == 1 && std::abs(bindPose[0].M11 - 1.0f) < 1e-6f
                            && std::abs(bindPose[0].M22 - 2.0f) < 1e-6f
                            && std::abs(bindPose[0].M33 - 1.0f) < 1e-6f))
            return;

        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, whitePixel);
        effect->setTextureProperty(&white);
        effect->setNormalMapProperty(nullptr);
        effect->setMetallicRoughnessMapProperty(nullptr);
        effect->setEmissiveMapProperty(nullptr);
        effect->setOcclusionMapProperty(nullptr);
        effect->setDiffuseColorProperty(Vector3::One);
        effect->setAmbientLightColorProperty(Vector3::Zero);
        effect->setEmissiveFactorProperty(Vector3::Zero);
        effect->setMetallicFactorProperty(0.0f);
        effect->setRoughnessFactorProperty(1.0f);
        effect->setEncodeOutputToSrgbEXTProperty(true);
        effect->DirectionalLight0.setEnabledProperty(true);
        effect->DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        effect->DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect->DirectionalLight1.setEnabledProperty(false);
        effect->DirectionalLight2.setEnabledProperty(false);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // After skinning, the triangle spans (0,0), (1,0), (0,2). This translation puts NDC
        // centre strictly inside it (at barycentric source position (.25,.5)), not on an edge.
        const Matrix world = Matrix::CreateTranslation(-0.25f, -0.5f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f);

        for (int pass = 0; pass < 2; ++pass)
        {
            device.Clear(Color(0, 255, 0, 255));
            model.Draw(world, view, projection);
            ExpectPixel(pass == 0 ? "corpus skinned-PBR first draw"
                                  : "corpus skinned-PBR repeated draw",
                        centre, Color(93, 93, 93, 255), 2);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GltfSkinnedPbrNonUniformJointTest>();
}
