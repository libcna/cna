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

// XNA SetValue(Vector3) IL_0000..0030 and SetValue(Matrix) IL_0000..0014
// reject mismatched reflected shapes before writing.
// Same contract as EffectTest.CompiledTypedValueSettersValidateReflectedShape, with
// non-default data proving that rejected writes preserve the old values on stock effects.
TYPED_TEST(StockParameterShapeTest, RejectedShapeWritesPreserveVectorAndMatrixValues)
{
    TypeParam effect(this->device);
    auto* diffuse = effect.getParametersProperty()["DiffuseColor"];
    auto* matrix = effect.getParametersProperty()["WorldViewProj"];
    ASSERT_NE(diffuse, nullptr);
    ASSERT_NE(matrix, nullptr);
    const Vector4 vectorValue(2, 3, 5, 7);
    const Matrix matrixValue(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    diffuse->SetValue(vectorValue);
    matrix->SetValue(matrixValue);
    EXPECT_THROW(diffuse->SetValue(Vector2(11, 13)), System::InvalidCastException);
    EXPECT_EQ(diffuse->GetValueVector4(), vectorValue);
    EXPECT_THROW(diffuse->SetValue(Vector3(11, 13, 17)), System::InvalidCastException);
    EXPECT_EQ(diffuse->GetValueVector4(), vectorValue);
    EXPECT_THROW(diffuse->SetValue(matrixValue), System::InvalidCastException);
    EXPECT_EQ(diffuse->GetValueVector4(), vectorValue);
    EXPECT_THROW(diffuse->SetValueTranspose(matrixValue), System::InvalidCastException);
    EXPECT_EQ(diffuse->GetValueVector4(), vectorValue);
    EXPECT_THROW(matrix->SetValue(Quaternion(19, 23, 29, 31)), System::InvalidCastException);
    EXPECT_EQ(matrix->GetValueMatrix(), matrixValue);
    EXPECT_THROW(matrix->SetValue(Vector4(19, 23, 29, 31)), System::InvalidCastException);
    EXPECT_EQ(matrix->GetValueMatrix(), matrixValue);
    // These failures must not affect a subsequent correctly shaped write either.
    diffuse->SetValue(Vector4(11, 13, 17, 19));
    EXPECT_EQ(diffuse->GetValueVector4(), Vector4(11, 13, 17, 19));
}

TYPED_TEST(StockParameterShapeTest, CloneRetainsScalarGetterGuards)
{
    TypeParam effect(this->device);
    std::unique_ptr<Effect> clone(effect.Clone());
    ExpectScalarRefusal(*clone);
}

TYPED_TEST(StockParameterShapeTest, NonStringDeclarationsUseDefaultStringCastRefusal)
{
    TypeParam effect(this->device);
    // Refresh GetValueString IL_000d..0012 / SetValue(string) IL_000e..0013
    // apply the String type gate to stock-effect parameters as well.
    for (const char* name : {"ShaderIndex", "DiffuseColor", "WorldViewProj", "Texture"})
    {
        SCOPED_TRACE(name);
        auto* p = effect.getParametersProperty()[name];
        ASSERT_NE(p, nullptr);
        const auto check = [](auto operation)
        {
            try { operation(); FAIL() << "Expected InvalidCastException"; }
            catch (const System::InvalidCastException& e)
            { EXPECT_EQ(e.getMessageProperty(), "Specified cast is not valid."); }
        };
        check([&] { (void)p->GetValueString(); });
        check([&] { p->SetValue(std::string("rejected")); });
    }
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

TEST_F(StockBoneParameterTest, RectangularElementTypedArraysUseLogicalRowsWithoutFourthColumn)
{
    SkinnedEffect effect(device);
    auto* bones = effect.getParametersProperty()["Bones"];
    ASSERT_NE(bones, nullptr);
    effect.SetBoneTransforms({Matrix(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)});
    auto* element = bones->getElementsProperty()[0];
    ASSERT_NE(element, nullptr);
    // Refresh typed arrays IL_0051..0062 delegate Matrix to GetValueSingleArray;
    // its IL_0063..00c9 walks RowCount=4, ColumnCount=3: omit M14/M24/M34/M44.
    EXPECT_EQ(element->GetValueVector2Array(7),
        (std::vector<Vector2>{{1,2},{3,5},{6,7},{9,10},{11,13},{14,15},{0,0}}));
    EXPECT_EQ(element->GetValueVector3Array(5),
        (std::vector<Vector3>{{1,2,3},{5,6,7},{9,10,11},{13,14,15},{0,0,0}}));
    EXPECT_EQ(element->GetValueVector4Array(4),
        (std::vector<Vector4>{{1,2,3,5},{6,7,9,10},{11,13,14,15},{0,0,0,0}}));
    EXPECT_EQ(element->GetValueQuaternionArray(4),
        (std::vector<Quaternion>{{1,2,3,5},{6,7,9,10},{11,13,14,15},{0,0,0,0}}));
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

TYPED_TEST(StockParameterShapeTest, MatrixNumericArraysUseLogicalRowsAndPreservePartialWrites)
{
    TypeParam effect(this->device);
    auto* p = effect.getParametersProperty()["WorldViewProj"];
    ASSERT_NE(p, nullptr);
    const Matrix initial(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
    p->SetValue(initial);
    // Small integers and halves are exactly representable in binary32 (zero tolerance).
    // Refresh GetValueSingleArray IL_0043..00d1 traverses Matrix.M11 row-first;
    // BooleanArray IL_009a..00af compares against zero; Int32Array IL_009e..00a0 truncates.
    EXPECT_EQ(p->GetValueSingleArray(18),
        (std::vector<float>{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,0,0}));
    EXPECT_EQ(p->GetValueInt32Array(3), (std::vector<int>{1,2,3}));
    EXPECT_EQ(p->GetValueBooleanArray(3), (std::vector<bool>{true,true,true}));
    // Refresh numeric array setters read the old matrix, replace a row-major prefix,
    // and SetValue(matrix) (float[] IL_005b..00e4). Unwritten fields must survive.
    p->SetValue(std::vector<float>{-2.5f,0});
    EXPECT_EQ(p->GetValueMatrix(), Matrix(-2.5f,0,3,4,5,6,7,8,9,10,11,12,13,14,15,16));
    EXPECT_EQ(p->GetValueInt32Array(3), (std::vector<int>{-2,0,3}));
    EXPECT_EQ(p->GetValueBooleanArray(3), (std::vector<bool>{true,false,true}));
    p->SetValue(std::vector<int>{7,8,9});
    EXPECT_EQ(p->GetValueMatrix(), Matrix(7,8,9,4,5,6,7,8,9,10,11,12,13,14,15,16));
    p->SetValue(std::vector<bool>{false,true});
    EXPECT_EQ(p->GetValueMatrix(), Matrix(0,1,9,4,5,6,7,8,9,10,11,12,13,14,15,16));
    p->SetValue(std::vector<float>{});
    p->SetValue(std::vector<int>{});
    p->SetValue(std::vector<bool>{});
    EXPECT_EQ(p->GetValueMatrix(), Matrix(0,1,9,4,5,6,7,8,9,10,11,12,13,14,15,16));
    p->SetValue(initial);
    EXPECT_EQ(p->GetValueVector2Array(2), (std::vector<Vector2>{{1,2},{3,4}}));
    EXPECT_EQ(p->GetValueVector3Array(2), (std::vector<Vector3>{{1,2,3},{4,5,6}}));
    EXPECT_EQ(p->GetValueVector4Array(1), (std::vector<Vector4>{{1,2,3,4}}));
    EXPECT_EQ(p->GetValueQuaternionArray(1), (std::vector<Quaternion>{{1,2,3,4}}));
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
TYPED_TEST(StockParameterShapeTest, TypedGettersAndArraySettersRespectLeafShapes)
{
    TypeParam effect(this->device);
    auto* vector = effect.getParametersProperty()["DiffuseColor"];
    auto* matrix = effect.getParametersProperty()["WorldViewProj"];
    ASSERT_NE(vector, nullptr); ASSERT_NE(matrix, nullptr);
    const Vector4 value(2,3,5,7);
    const Matrix m(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
    vector->SetValue(value); matrix->SetValue(m);
    // XNA typed getters: Vector2 IL_005c..007f, Vector3 IL_0064..0087,
    // Vector4/Quaternion IL_006f..0092, Matrix/Transpose IL_00d2..00dd.
    EXPECT_THROW((void)vector->GetValueVector2(), System::InvalidCastException);
    EXPECT_THROW((void)vector->GetValueVector3(), System::InvalidCastException);
    EXPECT_THROW((void)vector->GetValueMatrix(), System::InvalidCastException);
    EXPECT_THROW((void)vector->GetValueMatrixTranspose(), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueVector2(), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueVector3(), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueVector4(), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueQuaternion(), System::InvalidCastException);
    // Typed array setters require Elements.Count > 0: vector families IL_0011..001c,
    // Matrix/Transpose IL_0022..002d; array getters IL_0016..0021 have the same constraint.
    EXPECT_THROW(vector->SetValue(std::vector<Vector2>{{11,13}}), System::InvalidCastException);
    EXPECT_THROW(vector->SetValue(std::vector<Vector3>{{11,13,17}}), System::InvalidCastException);
    EXPECT_THROW(vector->SetValue(std::vector<Vector4>{{11,13,17,19}}), System::InvalidCastException);
    EXPECT_THROW(vector->SetValue(std::vector<Quaternion>{{11,13,17,19}}), System::InvalidCastException);
    EXPECT_THROW(matrix->SetValue(std::vector<Matrix>{m}), System::InvalidCastException);
    EXPECT_THROW(matrix->SetValueTranspose(std::vector<Matrix>{m}), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueMatrixArray(1), System::InvalidCastException);
    EXPECT_THROW((void)matrix->GetValueMatrixTransposeArray(1), System::InvalidCastException);
    EXPECT_EQ(vector->GetValueVector4(), value);
    EXPECT_EQ(matrix->GetValueMatrix(), m);
}

}
