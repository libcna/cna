// SPDX-License-Identifier: MS-PL
// Task 883: SpriteEffect::Clone() had no dedicated unit test file before this
// task's Effect::Clone() base-class contract work (only exercised implicitly
// via SpriteBatch's internal use of SpriteEffect, never Clone() itself).
//
// TQ-035 (owner decision 2026-11-04): XNA 4.0 has no public SpriteEffect, so CNA's public class is
// FNA's. Oracle: FNA src/Graphics/Effect/StockEffects/SpriteEffect.cs -- the parameter is
// `Parameters["MatrixTransform"]` (SpriteEffect.fx: `float4x4 MatrixTransform`), OnApply sets it
// to CreateTranslation(-0.5, -0.5, 0) * CreateOrthographicOffCenter(0, W, H, 0, 0, 1) for the
// device viewport, and the clone constructor is `: base(cloneSource)`, which keeps the value.
//
// The expected matrix, by hand (XNA/FNA CreateOrthographicOffCenter with left 0, right W,
// bottom H, top 0, near 0, far 1): M11 = 2/W, M22 = 2/(0-H) = -2/H, M33 = 1/(0-1) = -1,
// M41 = (0+W)/(0-W) = -1, M42 = (0+H)/(H-0) = 1, M43 = 0, M44 = 1. Pre-multiplying the half-pixel
// translation changes only row 4: M41 = -0.5*(2/W) - 1 = -1 - 1/W, M42 = -0.5*(-2/H) + 1 = 1 + 1/H.
// 320x200: M11 = 0.00625, M22 = -0.01, M41 = -1.003125, M42 = 1.005.
// 160x100: M11 = 0.0125,  M22 = -0.02, M41 = -1.00625,  M42 = 1.01.

#include <gtest/gtest.h>

#include <memory>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectParameter;
using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
using Microsoft::Xna::Framework::Graphics::EffectParameterType;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SpriteEffect;
using Microsoft::Xna::Framework::Graphics::Viewport;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    class SpriteEffectMatrixTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
                          PresentationParameters::CreateWindowedEXT(800, 480)};

        static void ApplyFirstPass(Effect& effect)
        {
            effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        }

        static Matrix MatrixOf(const Effect& effect)
        {
            const EffectParameter* parameter = effect.getParametersProperty()["MatrixTransform"];
            EXPECT_NE(parameter, nullptr);
            return parameter != nullptr ? parameter->GetValueMatrix() : Matrix();
        }

        // Every element of FNA's sprite matrix for a W x H viewport, worked out in the header.
        static void ExpectSpriteMatrix(const Matrix& m, float m11, float m22, float m41, float m42)
        {
            EXPECT_FLOAT_EQ(m.M11, m11);
            EXPECT_EQ(m.M12, 0.0f);
            EXPECT_EQ(m.M13, 0.0f);
            EXPECT_EQ(m.M14, 0.0f);
            EXPECT_EQ(m.M21, 0.0f);
            EXPECT_FLOAT_EQ(m.M22, m22);
            EXPECT_EQ(m.M23, 0.0f);
            EXPECT_EQ(m.M24, 0.0f);
            EXPECT_EQ(m.M31, 0.0f);
            EXPECT_EQ(m.M32, 0.0f);
            EXPECT_FLOAT_EQ(m.M33, -1.0f) << "FNA's orthographic depth, not XNA SpriteBatch's +1";
            EXPECT_EQ(m.M34, 0.0f);
            EXPECT_FLOAT_EQ(m.M41, m41);
            EXPECT_FLOAT_EQ(m.M42, m42);
            EXPECT_EQ(m.M43, 0.0f);
            EXPECT_FLOAT_EQ(m.M44, 1.0f);
        }
    };
}

TEST(SpriteEffectTest, CloneReturnsIndependentSpriteEffect)
{
    GraphicsDevice gd;
    SpriteEffect fx(gd);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<SpriteEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    ASSERT_NE(clone->getParametersProperty()["MatrixTransform"], nullptr);
    EXPECT_NE(clone->getParametersProperty()["MatrixTransform"],
              fx.getParametersProperty()["MatrixTransform"])
        << "the clone must own its parameter, not share the source's";
}

TEST_F(SpriteEffectMatrixTest, ExposesFnasMatrixTransformParameter)
{
    SpriteEffect fx(gd);
    const EffectParameter* parameter = fx.getParametersProperty()["MatrixTransform"];
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->getNameProperty(), "MatrixTransform");
    EXPECT_EQ(parameter->getParameterClassProperty(), EffectParameterClass::Matrix);
    EXPECT_EQ(parameter->getParameterTypeProperty(), EffectParameterType::Single);
    EXPECT_EQ(parameter->getRowCountProperty(), 4);
    EXPECT_EQ(parameter->getColumnCountProperty(), 4);
}

// FNA's SpriteEffect.fx declares Texture (Texture2D) beside MatrixTransform. FNA's Apply binds a
// non-null effect texture to its sampler (Effect.cs:719-727), and a clone -- its own Clone or an
// EffectMaterial -- copies the reference (:303-305).
TEST_F(SpriteEffectMatrixTest, TheTextureParameterIsBoundAtApplyAndCarriedByClones)
{
    SpriteEffect fx(gd);
    EffectParameter* texture = fx.getParametersProperty()["Texture"];
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getParameterTypeProperty(), EffectParameterType::Texture2D);
    Texture2D red(gd, 1, 1);
    Texture2D blue(gd, 1, 1);
    gd.getTexturesProperty()(0, &blue);
    ApplyFirstPass(fx);
    EXPECT_EQ(gd.getTexturesProperty()[0], &blue) << "a null effect texture left s0";
    texture->SetValue(&red);
    ApplyFirstPass(fx);
    EXPECT_EQ(gd.getTexturesProperty()[0], &red);

    std::unique_ptr<Effect> clone(fx.Clone());
    EffectMaterial material(fx);
    for (Effect* copy : {clone.get(), static_cast<Effect*>(&material)})
    {
        ASSERT_NE(copy->getParametersProperty()["Texture"], nullptr);
        EXPECT_EQ(copy->getParametersProperty()["Texture"]->GetValueTexture2D(), &red);
        gd.getTexturesProperty()(0, &blue);
        ApplyFirstPass(*copy);
        EXPECT_EQ(gd.getTexturesProperty()[0], &red);
    }
}

// FNA's clone copies the held reference (Effect.cs:303-305) without the checks of a new
// SetValue: a texture that is now the active render target, which SetValue would refuse
// (XNA SetValue(Texture) IL_0002-0059), is still carried by the clone.
TEST_F(SpriteEffectMatrixTest, CloneCarriesATextureThatIsNowTheActiveRenderTarget)
{
    SpriteEffect fx(gd);
    RenderTarget2D target(gd, 4, 4);
    fx.getParametersProperty()["Texture"]->SetValue(&target);
    gd.SetRenderTarget(&target);
    std::unique_ptr<Effect> clone;
    EXPECT_NO_THROW(clone.reset(fx.Clone()));
    std::unique_ptr<EffectMaterial> material;
    EXPECT_NO_THROW(material = std::make_unique<EffectMaterial>(fx));
    gd.SetRenderTarget(nullptr);
    ASSERT_NE(clone, nullptr);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(clone->getParametersProperty()["Texture"]->GetValueTexture2D(), &target);
    EXPECT_EQ(material->getParametersProperty()["Texture"]->GetValueTexture2D(), &target);
}

// FNA binds the stored Texture without a cast (Effect.cs:719-727), so a cube set through the base
// overload is what the pass binds -- by the effect, its clone and an EffectMaterial alike.
TEST_F(SpriteEffectMatrixTest, ApplyBindsATextureOfAnotherTypeSetThroughTheBaseOverload)
{
    SpriteEffect fx(gd);
    TextureCube cube(gd, 4, false, SurfaceFormat::Color);
    Texture2D blue(gd, 1, 1);
    fx.getParametersProperty()["Texture"]->SetValue(static_cast<Texture*>(&cube));
    EXPECT_EQ(fx.getParametersProperty()["Texture"]->GetValueTexture2D(), nullptr);

    std::unique_ptr<Effect> clone(fx.Clone());
    EffectMaterial material(fx);
    for (Effect* effect : {static_cast<Effect*>(&fx), clone.get(), static_cast<Effect*>(&material)})
    {
        gd.getTexturesProperty()(0, &blue);
        ApplyFirstPass(*effect);
        EXPECT_EQ(gd.getTexturesProperty()[0], &cube);
    }
}

TEST_F(SpriteEffectMatrixTest, ApplySetsTheViewportMatrixEveryTime)
{
    SpriteEffect fx(gd);
    gd.setViewportProperty(Viewport(0, 0, 320, 200));
    ApplyFirstPass(fx);
    ExpectSpriteMatrix(MatrixOf(fx), 0.00625f, -0.01f, -1.003125f, 1.005f);

    // Recomputed from the current viewport on each Apply, not cached from the first.
    gd.setViewportProperty(Viewport(0, 0, 160, 100));
    ApplyFirstPass(fx);
    ExpectSpriteMatrix(MatrixOf(fx), 0.0125f, -0.02f, -1.00625f, 1.01f);
}

// Replaces the former EXPECT_NO_THROW-only CloneAndOriginalApplyIndependently, which a no-op
// OnApply passed.
TEST_F(SpriteEffectMatrixTest, CloneKeepsTheCurrentMatrixAndAppliesIndependently)
{
    SpriteEffect fx(gd);
    gd.setViewportProperty(Viewport(0, 0, 320, 200));
    ApplyFirstPass(fx);

    std::unique_ptr<Effect> clone(fx.Clone());
    ASSERT_NE(clone, nullptr);
    // FNA's clone is `: base(cloneSource)`: the value is there before the clone applies.
    ExpectSpriteMatrix(MatrixOf(*clone), 0.00625f, -0.01f, -1.003125f, 1.005f);

    gd.setViewportProperty(Viewport(0, 0, 160, 100));
    ApplyFirstPass(*clone);
    ExpectSpriteMatrix(MatrixOf(*clone), 0.0125f, -0.02f, -1.00625f, 1.01f);
    ExpectSpriteMatrix(MatrixOf(fx), 0.00625f, -0.01f, -1.003125f, 1.005f);
}
