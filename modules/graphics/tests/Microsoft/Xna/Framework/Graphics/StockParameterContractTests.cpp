// SPDX-License-Identifier: MS-PL
// ISSUE-034/035: genuine XNA scalar guards and shared bone parameter views.
#include <memory>
#include <cmath>
#include <limits>
#include <vector>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/InvalidCastException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class StockParameterDeviceTest : public testing::Test
{
protected:
    GraphicsDevice device;
    void SetUp() override
    {
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    }
};

template<class T> class StockParameterShapeTest : public StockParameterDeviceTest {};
using StockEffects = testing::Types<BasicEffect, AlphaTestEffect, DualTextureEffect,
                                   EnvironmentMapEffect, SkinnedEffect>;
TYPED_TEST_SUITE(StockParameterShapeTest, StockEffects);

void ExpectScalarRefusal(Effect& effect)
{
    for (const char* name : {"DiffuseColor", "WorldViewProj"})
    {
        SCOPED_TRACE(name);
        auto* parameter = effect.getParametersProperty()[name];
        ASSERT_NE(parameter, nullptr);
        EXPECT_THROW(static_cast<void>(parameter->GetValueSingle()), System::InvalidCastException);
        EXPECT_THROW(static_cast<void>(parameter->GetValueInt32()), System::InvalidCastException);
        EXPECT_THROW(static_cast<void>(parameter->GetValueBoolean()), System::InvalidCastException);
    }
}

TYPED_TEST(StockParameterShapeTest, VectorAndMatrixRejectAllScalarGetters)
{
    TypeParam effect(this->device);
    ExpectScalarRefusal(effect);
}

TYPED_TEST(StockParameterShapeTest, CloneRetainsScalarGetterGuards)
{
    TypeParam effect(this->device);
    std::unique_ptr<Effect> clone(effect.Clone());
    ExpectScalarRefusal(*clone);
}

TYPED_TEST(StockParameterShapeTest, ValidScalarRetainsValue)
{
    TypeParam effect(this->device);
    auto* shader = effect.getParametersProperty()["ShaderIndex"];
    ASSERT_NE(shader, nullptr);
    shader->SetValue(7);
    EXPECT_EQ(shader->GetValueInt32(), 7);
}

TEST_F(StockParameterDeviceTest, StandaloneStorageRetainsItsExtensionContract)
{
    EffectParameter parameter("standalone", "", 1, 4,
        EffectParameterClass::Vector, EffectParameterType::Single);
    parameter.SetValue(Vector4(2, 3, 4, 5));
    EXPECT_EQ(parameter.GetValueSingle(), 2);
    EXPECT_EQ(parameter.GetValueInt32(), 0);
    EXPECT_FALSE(parameter.GetValueBoolean());
    const std::vector<Vector4> values{Vector4(1, 2, 3, 4), Vector4(5, 6, 7, 8)};
    parameter.SetValue(values);
    EXPECT_EQ(parameter.GetValueVector4Array(2), values);
    EXPECT_EQ(parameter.getElementsProperty().getCountProperty(), 0);
}

class StockBoneParameterTest : public StockParameterDeviceTest {};

Matrix AffineTransform()
{
    return Matrix::CreateScale(2, 3, 4) * Matrix::CreateTranslation(5, 6, 7);
}

TEST_F(StockBoneParameterTest, ReflectsSeventyTwoFourByThreeMatrixViews)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    ASSERT_NE(bones, nullptr);
    EXPECT_EQ(bones->getRowCountProperty(), 4);
    EXPECT_EQ(bones->getColumnCountProperty(), 3);
    auto& elements = bones->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    for (int i = 0; i < 72; ++i)
    {
        SCOPED_TRACE(i);
        ASSERT_NE(elements[i], nullptr);
        EXPECT_EQ(elements[i]->getParameterClassProperty(), EffectParameterClass::Matrix);
        EXPECT_EQ(elements[i]->getParameterTypeProperty(), EffectParameterType::Single);
        EXPECT_EQ(elements[i]->getRowCountProperty(), 4);
        EXPECT_EQ(elements[i]->getColumnCountProperty(), 3);
    }
}

TEST_F(StockBoneParameterTest, RawDefaultOmitsFourthColumnButBoneGetterRestoresM44)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    const auto raw = bones->GetValueMatrixArray(1);
    ASSERT_EQ(raw.size(), 1u);
    Matrix expected = Matrix::getIdentityProperty();
    expected.M44 = 0;
    EXPECT_EQ(raw[0], expected);
    EXPECT_EQ(effect.GetBoneTransforms(1)[0], Matrix::getIdentityProperty());
}

TEST_F(StockBoneParameterTest, ParentWriteReachesElementWithoutFourthColumn)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    ASSERT_EQ(bones->getElementsProperty().getCountProperty(), 72);
    effect.SetBoneTransforms({AffineTransform()});
    Matrix expected = AffineTransform();
    expected.M44 = 0;
    EXPECT_EQ(bones->GetValueMatrixArray(1)[0], expected);
    EXPECT_EQ(bones->getElementsProperty()[0]->GetValueMatrix(), expected);
    EXPECT_EQ(effect.GetBoneTransforms(1)[0], AffineTransform());
}

TEST_F(StockBoneParameterTest, PartialWritePreservesArrayCapacityAndUntouchedTail)
{
    SkinnedEffect effect(device);
    effect.SetBoneTransforms({AffineTransform(), Matrix::getIdentityProperty()});
    const auto values = effect.GetBoneTransforms(72);
    ASSERT_EQ(values.size(), 72u);
    EXPECT_EQ(values[0], AffineTransform());
    for (int i = 1; i < 72; ++i)
    {
        EXPECT_EQ(values[i], Matrix::getIdentityProperty());
    }
}

TEST_F(StockBoneParameterTest, ElementWriteReachesParent)
{
    SkinnedEffect effect(device);
    auto& elements = effect.getParametersProperty()["Bones"]->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    elements[0]->SetValue(AffineTransform());
    EXPECT_EQ(effect.GetBoneTransforms(1)[0], AffineTransform());
}

TEST_F(StockBoneParameterTest, TransposeUsesTheReflectedFourByThreeExtent)
{
    SkinnedEffect effect(device);
    auto& elements = effect.getParametersProperty()["Bones"]->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    effect.SetBoneTransforms({AffineTransform()});
    Matrix raw = AffineTransform();
    raw.M44 = 0;
    EXPECT_EQ(elements[0]->GetValueMatrixTranspose(), Matrix::Transpose(raw));
    const Matrix asymmetric(1, 2, 3, 4, 5, 6, 7, 8,
                            9, 10, 11, 12, 13, 14, 15, 16);
    elements[0]->SetValueTranspose(asymmetric);
    EXPECT_EQ(elements[0]->GetValueMatrix(),
              Matrix(1, 5, 9, 0, 2, 6, 10, 0, 3, 7, 11, 0, 4, 8, 12, 0));
}

TEST_F(StockBoneParameterTest, NumericArrayReadsUseRowMajorReflectedComponents)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    ASSERT_EQ(bones->getElementsProperty().getCountProperty(), 72);
    effect.SetBoneTransforms({AffineTransform()});
    const std::vector<float> expected{2, 0, 0, 0, 3, 0, 0, 0, 4, 5, 6, 7, 1, 0, 0, 0};
    EXPECT_EQ(bones->GetValueSingleArray(16), expected);
    EXPECT_EQ(bones->getElementsProperty()[0]->GetValueSingleArray(16),
              (std::vector<float>{2, 0, 0, 0, 3, 0, 0, 0, 4, 5, 6, 7, 0, 0, 0, 0}));
    EXPECT_EQ(bones->GetValueInt32Array(16),
              (std::vector<int>{2, 0, 0, 0, 3, 0, 0, 0, 4, 5, 6, 7, 1, 0, 0, 0}));
    EXPECT_EQ(bones->GetValueBooleanArray(16),
              (std::vector<bool>{true, false, false, false, true, false, false, false,
                                 true, true, true, true, true, false, false, false}));
}

TEST_F(StockBoneParameterTest, NumericArrayWritesPreserveUnwrittenMatrixComponents)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    const std::vector<float> values{1, 2, 3, 4, 5, 6, 7, 8,
                                    9, 10, 11, 12, 13, 14, 15, 16};
    const Matrix first(1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 0);
    const Matrix second(13, 14, 15, 0, 16, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    for (bool integer : {false, true})
    {
        effect.SetBoneTransforms(std::vector<Matrix>(72, Matrix::getIdentityProperty()));
        if (integer)
            bones->SetValue(std::vector<int>(values.begin(), values.end()));
        else
            bones->SetValue(values);
        const auto matrices = bones->GetValueMatrixArray(2);
        ASSERT_EQ(matrices.size(), 2u);
        EXPECT_EQ(matrices[0], first);
        EXPECT_EQ(matrices[1], second);
    }
    effect.SetBoneTransforms(std::vector<Matrix>(72, Matrix::getIdentityProperty()));
    bones->SetValue(std::vector<bool>{true, false, true, false, true, false,
                                     true, false, true, false, true, false});
    EXPECT_EQ(bones->GetValueMatrixArray(1)[0],
              Matrix(1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0));
    EXPECT_EQ(effect.GetBoneTransforms(72)[71], Matrix::getIdentityProperty());
}

TEST_F(StockBoneParameterTest, ScalarBroadcastStaysInsideItsElementView)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    auto& elements = bones->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    EXPECT_THROW(bones->SetValue(1.0f), System::InvalidCastException);
    for (int index : {0, 71})
        for (int kind = 0; kind < 3; ++kind)
        {
            SCOPED_TRACE(testing::Message() << "element=" << index << " kind=" << kind);
            effect.SetBoneTransforms(std::vector<Matrix>(72, Matrix::getIdentityProperty()));
            const float value = kind == 0 ? 7.0f : kind == 1 ? 5.0f : 1.0f;
            if (kind == 0) elements[index]->SetValue(7.0f);
            else if (kind == 1) elements[index]->SetValue(5);
            else elements[index]->SetValue(true);
            EXPECT_EQ(elements[index]->GetValueMatrix(),
                      Matrix(value, value, value, 0, value, value, value, 0,
                             value, value, value, 0, value, value, value, 0));
            const auto all = effect.GetBoneTransforms(72);
            ASSERT_EQ(all.size(), 72u);
            for (int other = 0; other < 72; ++other)
            {
                if (other != index)
                {
                    EXPECT_EQ(all[other], Matrix::getIdentityProperty());
                }
            }
        }
}

TEST_F(StockBoneParameterTest, FloatingBroadcastPreservesNonfiniteValuesWithoutIntegerConversion)
{
    SkinnedEffect effect(device);
    auto& elements = effect.getParametersProperty()["Bones"]->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    for (float value : {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()})
    {
        elements[0]->SetValue(value);
        const auto components = elements[0]->GetValueSingleArray(12);
        ASSERT_EQ(components.size(), 12u);
        for (float actual : components)
        {
            if (std::isnan(value))
            {
                EXPECT_TRUE(std::isnan(actual));
            }
            else
            {
                EXPECT_EQ(actual, value);
            }
        }
        EXPECT_EQ(elements[0]->GetValueMatrix().M44, 0);
        EXPECT_EQ(effect.GetBoneTransforms(2)[1], Matrix::getIdentityProperty());
    }
}

TEST_F(StockBoneParameterTest, CloneHasIndependentCoherentElementViews)
{
    SkinnedEffect effect(device);
    std::unique_ptr<Effect> cloneBase(effect.Clone());
    auto* clone = dynamic_cast<SkinnedEffect*>(cloneBase.get());
    ASSERT_NE(clone, nullptr);
    auto& elements = clone->getParametersProperty()["Bones"]->getElementsProperty();
    ASSERT_EQ(elements.getCountProperty(), 72);
    elements[1]->SetValue(AffineTransform());
    EXPECT_EQ(clone->GetBoneTransforms(2)[1], AffineTransform());
    EXPECT_EQ(effect.GetBoneTransforms(2)[1], Matrix::getIdentityProperty());
}
}
