// SPDX-License-Identifier: MS-PL
// Task 11.24 / REMED-GFX-008: pixel-readback regression test for AvatarRenderer::PartTintEXT's
// per-part tint routing (Task 11.17) AND for the AvatarRenderer lighting calibration.
//
// Original purpose (Task 11.17): guard against `PartTintEXT` matching `part.Name == "hair"` (exact
// equality against a lowercase literal), which never matched real part names like "CNAAvatarHair" --
// every part, including actual hair, silently rendered in skin color. Two single-bone quads, each
// its own SkinnedModelEXT part with an all-white texture (so tint alone determines the rendered
// color): "CNAAvatarHair" covers NDC x: -1..0, "CNAAvatarShirt" covers NDC x: 0..1.
//
// REMED-GFX-008 strengthening: the pre-fix version drove AmbientLightColor=(1,1,1) AND a full
// head-on DirectionalLight, then asserted the raw tint -- which only held because the SkinnedEffect
// shader bug silently DROPPED the ambient contribution, cancelling the 2x over-lighting back to 1x.
// With the shader corrected, that setup renders 2x tint. This test now uses two lighting scenes
// whose fully-lit result is genuinely 1x tint under the FNA-correct model
// `final = (ambient + keyLight*max(0,N.L)) * tint`, so it can no longer pass by two bugs cancelling:
//
//   Scene 1 "ambient-driven"    : ambient=(1,1,1), key light OFF  -> flat 1x tint.
//       A shader that drops ambient renders this BLACK -> FAIL (this is exactly the GFX-008 bug).
//   Scene 2 "directional-driven": ambient=(0,0,0), key light=(1,1,1) head-on -> 1x tint.
//       A shader that DOUBLES the directional light renders this 2x tint -> FAIL.
//
// Both scenes also verify routing: left quarter must be HairColor, right quarter must be ShirtColor.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "System/TimeSpan.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;

namespace
{
    std::shared_ptr<SkinnedModelEXT> BuildTwoPartModel(GraphicsDevice& device, Texture2D whiteTex)
    {
        auto model = std::make_shared<SkinnedModelEXT>();
        model->BoneCount = 1;
        model->ParentBoneIndices = {-1};
        model->BindPoseLocal = {Matrix::getIdentityProperty()};
        model->InverseBindPoseGlobal = {Matrix::getIdentityProperty()};

        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::Zero;
        BoneTrackEXT track;
        track.BoneIndex = 0;
        track.Keys.push_back(KeyframeEXT{System::TimeSpan::Zero, Vector3(0.0f, 0.0f, 0.0f)});
        clip.Tracks.push_back(track);
        model->Clips["Test"] = clip;

        auto addQuad = [&](const std::string& partName, float xMin, float xMax, Texture2D tex)
        {
            const VertexPositionNormalTextureSkinned verts[6] = {
                {Vector3(xMin,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
                {Vector3(xMin, -1, 0), Vector3(0, 0, 1), Vector2(0, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
                {Vector3(xMax, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
                {Vector3(xMin,  1, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
                {Vector3(xMax, -1, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
                {Vector3(xMax,  1, 0), Vector3(0, 0, 1), Vector2(1, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
            };
            const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

            auto vb = std::make_unique<VertexBuffer>(device, 6);
            vb->SetData(verts, 6);
            auto ib = std::make_unique<IndexBuffer>(device, 6);
            ib->SetData(indices, 6);
            auto part = std::make_unique<ModelMeshPart>(vb.get(), ib.get(), 6, 2, 0, 0);
            model->AddPartEXT(partName, std::move(vb), std::move(ib), std::move(part), std::move(tex));
        };

        // Real part names as produced by tools/avatar_asset_pipeline/convert_avatar.py --
        // NOT the lowercase "hair"/"shirt" literals the pre-Task-11.17 bug compared against.
        addQuad("CNAAvatarHair", -1.0f, 0.0f, whiteTex);
        addQuad("CNAAvatarShirt", 0.0f, 1.0f, whiteTex);
        return model;
    }

    bool ColorCloseTo(Color actual, Color expected, int tolerance)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance
            && std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance
            && std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance;
    }
}

class AvatarTintRoutingIntegrationTest : public Game
{
    Texture2D whiteTex_;
    bool done_ = false;
    int result_ = 0;

    // Renders the two-part avatar with the given lighting, samples the left/right quarter pixels,
    // and checks both routing (left=Hair, right=Shirt) and the absolute 1x-tint calibration.
    void runScene(GraphicsDevice& device, const std::shared_ptr<SkinnedModelEXT>& model,
                  const AvatarAppearanceEXT& appearance, const Color& hairColor, const Color& shirtColor,
                  const char* label, const Vector3& ambient, const Vector3& lightColor,
                  const Vector3& lightDir)
    {
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 128, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState -- needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        AvatarRenderer renderer(nullptr);
        renderer.EnableRealRenderingEXT(device, model);
        renderer.SetAppearanceEXT(appearance);
        renderer.setAmbientLightColorProperty(ambient);
        renderer.setLightColorProperty(lightColor);
        renderer.setLightDirectionProperty(lightDir);
        renderer.DrawRealEXT("Test", System::TimeSpan::Zero, false);

        const Rectangle leftReg(W / 4, H / 2, 1, 1);
        const Rectangle rightReg(3 * W / 4, H / 2, 1, 1);
        Color leftPx(0, 0, 0, 0), rightPx(0, 0, 0, 0);
        device.GetBackBufferData(&leftReg, &leftPx, 0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        // Tolerance of 20 absorbs ordinary shading/rounding noise while staying far tighter than the
        // ~100+ channel swing a routing bug (both parts collapsing to one color), an ambient-drop
        // bug (part goes black), or a doubled-light bug (2x tint) would produce.
        const bool leftOk = ColorCloseTo(leftPx, hairColor, 20);
        const bool rightOk = ColorCloseTo(rightPx, shirtColor, 20);

        if (leftOk && rightOk)
        {
            std::printf("[PASS] %-18s left=(%d,%d,%d) right=(%d,%d,%d)\n", label,
                        leftPx.getRProperty(), leftPx.getGProperty(), leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
        }
        else
        {
            std::printf("[FAIL] %-18s left=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=HairColor(%d,%d,%d), right=ShirtColor(%d,%d,%d)\n", label,
                        leftPx.getRProperty(), leftPx.getGProperty(), leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty(),
                        hairColor.getRProperty(), hairColor.getGProperty(), hairColor.getBProperty(),
                        shirtColor.getRProperty(), shirtColor.getGProperty(), shirtColor.getBProperty());
            result_ = 1;
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, std::vector<uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const Color hairColor(40, 25, 15, 255);
        const Color shirtColor(20, 90, 155, 255);
        AvatarAppearanceEXT appearance;
        appearance.setHairColorProperty(hairColor);
        appearance.setShirtColorProperty(shirtColor);

        auto model = BuildTwoPartModel(device, whiteTex_);

        // Scene 1: ambient (1,1,1), key light OFF. Fully lit = (1 + 0)*tint = 1x tint.
        // The GFX-008 ambient-drop bug renders this BLACK.
        runScene(device, model, appearance, hairColor, shirtColor, "ambient-driven",
                 Vector3(1.0f, 1.0f, 1.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f));

        // Scene 2: ambient (0,0,0), key light (1,1,1) head-on (N=(0,0,1), L=(0,0,-1) so N.(-L)=1).
        // Fully lit = (0 + 1)*tint = 1x tint. A doubled-directional bug renders this 2x tint.
        runScene(device, model, appearance, hairColor, shirtColor, "directional-driven",
                 Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f));

        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    AvatarTintRoutingIntegrationTest game;
    game.Run();
    return game.getResult();
}
