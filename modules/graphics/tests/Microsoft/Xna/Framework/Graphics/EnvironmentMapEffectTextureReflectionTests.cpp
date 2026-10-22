// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
class InspectableTextureCube : public TextureCube
{
  public:
    using TextureCube::GetRendererRaw;
    using TextureCube::TextureCube;
};

class EnvironmentMapTextureReflectionTest : public ::testing::Test
{
  protected:
    GraphicsDevice device;
    EnvironmentMapEffect effect{device};
    EffectParameter* texture = effect.getParametersProperty()["Texture"];
    EffectParameter* cube = effect.getParametersProperty()["EnvironmentMap"];
};
} // namespace

TEST_F(EnvironmentMapTextureReflectionTest, ReflectsTypedTextureParameters)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(texture->getParameterClassProperty(), EffectParameterClass::Object);
    EXPECT_EQ(cube->getParameterClassProperty(), EffectParameterClass::Object);
    EXPECT_EQ(texture->getParameterTypeProperty(), EffectParameterType::Texture2D);
    EXPECT_EQ(cube->getParameterTypeProperty(), EffectParameterType::TextureCube);
    EXPECT_EQ(texture->GetValueTexture2D(), nullptr);
    EXPECT_EQ(cube->GetValueTextureCube(), nullptr);
}

TEST_F(EnvironmentMapTextureReflectionTest, PropertyWritesReachParameters)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    Texture2D image(device, 2, 2);
    TextureCube environment(device, 2, false, SurfaceFormat::Color);
    effect.setTextureProperty(&image);
    effect.setEnvironmentMapProperty(&environment);
    EXPECT_EQ(texture->GetValueTexture2D(), &image);
    EXPECT_EQ(cube->GetValueTextureCube(), &environment);
}

TEST_F(EnvironmentMapTextureReflectionTest, ParameterWritesReachPropertiesAndDrawBindings)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    Texture2D image(device, 2, 2);
    InspectableTextureCube environment(device, 2, false, SurfaceFormat::Color);
    texture->SetValue(&image);
    cube->SetValue(&environment);
    EXPECT_EQ(effect.getTextureProperty(), &image);
    EXPECT_EQ(effect.getEnvironmentMapProperty(), &environment);
    effect.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &image);
    EXPECT_EQ(device.getTexturesProperty()[1], &environment);
    CNA::Internal::Renderers::GpuDrawParams draw;
    effect.FillGpuDrawParams(draw);
    EXPECT_EQ(draw.texture0, &image.GetRenderer());
    EXPECT_EQ(draw.envMap, environment.GetRendererRaw());
}

TEST_F(EnvironmentMapTextureReflectionTest, NullParameterWritesUnbindBothTextures)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    Texture2D image(device, 2, 2);
    TextureCube environment(device, 2, false, SurfaceFormat::Color);
    effect.setTextureProperty(&image);
    effect.setEnvironmentMapProperty(&environment);
    effect.Apply();
    texture->SetValue(static_cast<Texture2D*>(nullptr));
    cube->SetValue(static_cast<TextureCube*>(nullptr));
    effect.Apply();
    EXPECT_EQ(effect.getTextureProperty(), nullptr);
    EXPECT_EQ(effect.getEnvironmentMapProperty(), nullptr);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    EXPECT_EQ(device.getTexturesProperty()[1], nullptr);
}

TEST_F(EnvironmentMapTextureReflectionTest, CloneCopiesCurrentParameterValuesIndependently)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    Texture2D first(device, 2, 2), second(device, 2, 2);
    TextureCube environment(device, 2, false, SurfaceFormat::Color);
    effect.setTextureProperty(&first);
    texture->SetValue(&second);
    cube->SetValue(&environment);
    std::unique_ptr<Effect> cloned(effect.Clone());
    auto* clone = dynamic_cast<EnvironmentMapEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);
    auto* cloneTexture = clone->getParametersProperty()["Texture"];
    auto* cloneCube = clone->getParametersProperty()["EnvironmentMap"];
    ASSERT_NE(cloneTexture, nullptr);
    ASSERT_NE(cloneCube, nullptr);
    EXPECT_NE(cloneTexture, texture);
    EXPECT_NE(cloneCube, cube);
    EXPECT_EQ(clone->getTextureProperty(), &second);
    EXPECT_EQ(clone->getEnvironmentMapProperty(), &environment);
    cloneTexture->SetValue(&first);
    cloneCube->SetValue(static_cast<TextureCube*>(nullptr));
    EXPECT_EQ(effect.getTextureProperty(), &second);
    EXPECT_EQ(effect.getEnvironmentMapProperty(), &environment);
}

TEST_F(EnvironmentMapTextureReflectionTest, OwnedTexturesAreVisibleThroughParametersAndSharedByClone)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(cube, nullptr);
    auto image = std::make_shared<Texture2D>(device, 2, 2);
    auto environment = std::make_shared<TextureCube>(device, 2, false, SurfaceFormat::Color);
    effect.SetOwnedTexture(image);
    effect.SetOwnedEnvironmentMap(environment);
    EXPECT_EQ(texture->GetValueTexture2D(), image.get());
    EXPECT_EQ(cube->GetValueTextureCube(), environment.get());
    std::unique_ptr<Effect> cloned(effect.Clone());
    ASSERT_NE(cloned->getParametersProperty()["Texture"], nullptr);
    ASSERT_NE(cloned->getParametersProperty()["EnvironmentMap"], nullptr);
    EXPECT_EQ(cloned->getParametersProperty()["Texture"]->GetValueTexture2D(), image.get());
    EXPECT_EQ(cloned->getParametersProperty()["EnvironmentMap"]->GetValueTextureCube(), environment.get());
    std::weak_ptr<Texture2D> imageLifetime = image;
    std::weak_ptr<TextureCube> cubeLifetime = environment;
    image.reset();
    environment.reset();
    effect.SetOwnedTexture(nullptr);
    effect.SetOwnedEnvironmentMap(nullptr);
    EXPECT_FALSE(imageLifetime.expired());
    EXPECT_FALSE(cubeLifetime.expired());
    cloned.reset();
    EXPECT_TRUE(imageLifetime.expired());
    EXPECT_TRUE(cubeLifetime.expired());
}
