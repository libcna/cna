// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <vector>
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;

// Existing private-constructor friend: supply reflected tables without shader compilation.
// The runtime supplies no expected values and no parameter accessor implementation.
namespace CNA::TestSupport
{
    struct CompiledEffectTestAccess
    {
        static std::unique_ptr<Effect> Create(GraphicsDevice& device,
                                              std::unique_ptr<ICompiledEffectRuntime> runtime)
        {
            return std::unique_ptr<Effect>(new Effect(device, std::move(runtime), nullptr));
        }
    };
}

namespace
{
    class TableRuntime final : public ICompiledEffectRuntime
    {
    public:
        CompiledEffectDescription description;
        bool lifecycleStatePass = false;
        std::unique_ptr<ICompiledEffectRuntime> Clone() const override
        { return std::make_unique<TableRuntime>(*this); }
        const CompiledEffectDescription& GetDescription() const override { return description; }
        void SetTechnique(std::uint32_t) override {}
        void SetParameterValue(std::uint32_t, const void*, std::size_t) override {}
        void SetParameterTexture(std::uint32_t, Texture*) override {}
        void ApplyPass(std::uint32_t pass, const CompiledEffectDeviceState&,
                       CompiledEffectPassStateChanges& changes) override
        {
            if (lifecycleStatePass && pass == 1)
            {
                changes.depthStencilChanged = true;
                changes.depthStencil.setDepthBufferEnableProperty(false);
                changes.depthStencil.setDepthBufferWriteEnableProperty(false);
                changes.rasterizerChanged = true;
                changes.rasterizer.setCullModeProperty(CullMode::None);
            }
        }
    };

    CompiledEffectParameterDescription Numeric(EffectParameterClass cls, EffectParameterType type,
                                               int rows, int columns, int elements = 0)
    {
        CompiledEffectParameterDescription p;
        p.name = "Value";
        p.parameterClass = cls;
        p.parameterType = type;
        p.rowCount = rows;
        p.columnCount = columns;
        p.elementCount = elements;
        // Reflected value fixture: each numeric row occupies four 4-byte cells.
        p.rawValue.resize(static_cast<std::size_t>(rows * (elements ? elements : 1) * 16));
        return p;
    }

    std::unique_ptr<Effect> FromTable(GraphicsDevice& device,
                                      CompiledEffectParameterDescription parameter)
    {
        auto runtime = std::make_unique<TableRuntime>();
        runtime->description.parameters.push_back(std::move(parameter));
        runtime->description.techniques.push_back({"First", {}, {{"P0", {}}}});
        runtime->description.techniques.push_back({"Second", {}, {{"P1", {}}}});
        return CNA::TestSupport::CompiledEffectTestAccess::Create(device, std::move(runtime));
    }

    Matrix DistinctMatrix()
    { return Matrix(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16); }
}


// CPU-only reflection witness for the same declarations as CnaConformanceEffect.fx.
// No accessor, clone-value, owner-annotation or precondition behavior is mocked here:
// the public Effect graph and parameter storage implement all those contracts.
namespace CNA::TestSupport
{
    std::unique_ptr<Effect> CreateLifecycleTableEffect(GraphicsDevice& device)
    {
        auto runtime = std::make_unique<TableRuntime>();
        runtime->lifecycleStatePass = true;
        auto annotation = [](const char* name, EffectParameterType type, int value)
        {
            CompiledEffectAnnotationDescription a;
            a.name = name;
            a.parameterType = type;
            a.rowCount = a.columnCount = 1;
            a.rawValue.resize(sizeof(value));
            std::memcpy(a.rawValue.data(), &value, sizeof(value));
            return a;
        };
        auto gain = Numeric(EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1);
        gain.name = "Gain";
        gain.annotations.push_back(annotation("Visible", EffectParameterType::Bool, 1));
        auto weights = Numeric(EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1, 2);
        weights.name = "Weights";
        weights.runtimeIndex = 1;
        auto thresholds = weights;
        thresholds.name = "Thresholds";
        auto lighting = Numeric(EffectParameterClass::Struct, EffectParameterType::Void, 1, 1);
        lighting.rawValue.resize(thresholds.rawValue.size());
        lighting.name = "Lighting";
        lighting.runtimeIndex = 2;
        lighting.structureMembers.push_back(thresholds);
        runtime->description.parameters = {gain, weights, lighting};
        runtime->description.techniques.push_back({"FirstTechnique",
            {annotation("Quality", EffectParameterType::Int32, 7)},
            {{"FirstPass", {}}, {"StatePass", {annotation("PassTag", EffectParameterType::Int32, 3)}}}});
        runtime->description.techniques.push_back({"SecondTechnique", {}, {{"SecondPass", {}}}});
        return CompiledEffectTestAccess::Create(device, std::move(runtime));
    }
}

// TQ-082: XNA Refresh GetValueVector2/3/4Array and GetValueQuaternionArray
// IL_0051..0062 call GetValueSingleArray for Matrix, then copy a flat prefix.
TEST(EffectReflectedApiTest, MatrixTypedArraysUseLogicalRowsAndZeroFillTheRequestedSuffix)
{
    GraphicsDevice device;
    for (int elements : {0, 2})
    {
        SCOPED_TRACE(elements);
        auto desc = Numeric(EffectParameterClass::Matrix, EffectParameterType::Single, 4, 4, elements);
        // Four column registers encode the row-major matrix 1..16. No CNA setter supplies
        // the oracle. A second matrix is 21..36; every float is an exact small integer.
        const float columns[] = {1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16,
                                 21,25,29,33,22,26,30,34,23,27,31,35,24,28,32,36};
        std::memcpy(desc.rawValue.data(), columns, desc.rawValue.size());
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        const int cells = elements ? 32 : 16;
        const auto expectedCell = [cells](int i) -> float
        { return i >= cells ? 0.0f : static_cast<float>(i < 16 ? i + 1 : i + 5); };
        for (int count : {1, 3, 9, 17})
        {
            SCOPED_TRACE(count);
            const auto v2 = p.GetValueVector2Array(count);
            const auto v3 = p.GetValueVector3Array(count);
            const auto v4 = p.GetValueVector4Array(count);
            const auto q = p.GetValueQuaternionArray(count);
            ASSERT_EQ(v2.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(v3.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(v4.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(q.size(), static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                EXPECT_EQ(v2[i], Vector2(expectedCell(2*i), expectedCell(2*i+1)));
                EXPECT_EQ(v3[i], Vector3(expectedCell(3*i), expectedCell(3*i+1), expectedCell(3*i+2)));
                EXPECT_EQ(v4[i], Vector4(expectedCell(4*i), expectedCell(4*i+1),
                                        expectedCell(4*i+2), expectedCell(4*i+3)));
                EXPECT_EQ(q[i], Quaternion(expectedCell(4*i), expectedCell(4*i+1),
                                          expectedCell(4*i+2), expectedCell(4*i+3)));
            }
        }
    }
}

TEST(EffectReflectedApiTest, VectorArrayWritesFlattenAcrossReflectedWidths)
{
    GraphicsDevice device;
    // XNA SetValue(Vector2/3/4[] and Quaternion[]) IL_0000..001c checks only Vector + Elements;
    // IL_004c..0054 sends length * source width to native SetFloatArray, not one register/vector.
    for (int width : {2, 3, 4})
    {
        SCOPED_TRACE(width);
        auto effect = FromTable(device, Numeric(EffectParameterClass::Vector,
            EffectParameterType::Single, 1, width, 4));
        auto& p = *effect->getParametersProperty()[0];
        p.SetValue(std::vector<float>(4 * width, -9));
        p.SetValue(std::vector<Vector2>{{1,2},{3,4}});
        std::vector<float> expected(4 * width, -9);
        expected[0]=1; expected[1]=2; expected[2]=3; expected[3]=4;
        EXPECT_EQ(p.GetValueSingleArray(4 * width), expected);
        p.SetValue(std::vector<float>(4 * width, -9));
        p.SetValue(std::vector<Vector3>{{1,2,3},{4,5,6}});
        for (int i=0; i<6; ++i) expected[i]=static_cast<float>(i+1);
        EXPECT_EQ(p.GetValueSingleArray(4 * width), expected);
        p.SetValue(std::vector<float>(4 * width, -9));
        p.SetValue(std::vector<Vector4>{{1,2,3,4},{5,6,7,8}});
        for (int i=0; i<4*width; ++i) expected[i]=i<8 ? static_cast<float>(i+1) : -9;
        EXPECT_EQ(p.GetValueSingleArray(4 * width), expected);
        p.SetValue(std::vector<float>(4 * width, -9));
        p.SetValue(std::vector<Quaternion>{{1,2,3,4},{5,6,7,8}});
        EXPECT_EQ(p.GetValueSingleArray(4 * width), expected);
        p.SetValue(std::vector<Vector2>{});
        p.SetValue(std::vector<Vector3>{});
        p.SetValue(std::vector<Vector4>{});
        p.SetValue(std::vector<Quaternion>{});
        EXPECT_EQ(p.GetValueSingleArray(4 * width), expected);
    }
}

TEST(EffectReflectedApiTest, NumericPartialArrayWritesPreserveTailAndElementViews)
{
    GraphicsDevice device;
    // XNA numeric array setters accept Scalar/Vector/Matrix (IL_0000..001e).
    // Prefix assignment leaves unspecified cells untouched; integer/boolean inputs convert.
    for (auto type : {EffectParameterType::Single, EffectParameterType::Int32, EffectParameterType::Bool})
    {
        SCOPED_TRACE(static_cast<int>(type));
        auto effect = FromTable(device, Numeric(EffectParameterClass::Scalar, type, 1, 1, 3));
        auto& p = *effect->getParametersProperty()[0];
        p.SetValue(std::vector<int>{1,0,1});
        p.SetValue(std::vector<bool>{false,true});
        EXPECT_EQ(p.GetValueInt32Array(3), (std::vector<int>{0,1,1}));
        EXPECT_EQ(p.GetValueBooleanArray(4), (std::vector<bool>{false,true,true,false}));
        EXPECT_EQ(p.GetValueSingleArray(4), (std::vector<float>{0,1,1,0}));
        EXPECT_FLOAT_EQ(p.getElementsProperty()[2]->GetValueSingle(), 1);
        p.SetValue(std::vector<float>{1});
        p.SetValue(std::vector<int>{});
        p.SetValue(std::vector<float>{});
        p.SetValue(std::vector<bool>{});
        EXPECT_EQ(p.GetValueSingleArray(3), (std::vector<float>{1,1,1}));
        EXPECT_THROW(p.SetValue(4), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(4.0f), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(false), System::InvalidCastException);
        EXPECT_EQ(p.GetValueSingleArray(3), (std::vector<float>{1,1,1}));
    }
}

TEST(EffectReflectedApiTest, ManagedObjectShapeRefusalsUseDefaultInvalidCastMessage)
{
    GraphicsDevice device;
    // Refresh managed class/shape guards construct parameterless InvalidCastException:
    // scalar getters IL_0019..001e; scalar setters IL_000d..0012;
    // vector setters IL_000d..0012; numeric array setters IL_0019..001e.
    // Positive-count primitive/vector array GETTERS go through native D3DX; Matrix arrays have a managed guard.
    for (auto type : {EffectParameterType::String, EffectParameterType::Texture,
                      EffectParameterType::Texture1D, EffectParameterType::Texture2D,
                      EffectParameterType::Texture3D, EffectParameterType::TextureCube})
    {
        auto desc = Numeric(EffectParameterClass::Object, type, 1, 1);
        desc.rawValue.resize(4);
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        const auto check = [](auto operation)
        {
            try { operation(); FAIL() << "Expected InvalidCastException"; }
            catch (const System::InvalidCastException& e)
            { EXPECT_EQ(e.getMessageProperty(), "Specified cast is not valid."); }
        };
        check([&] { p.SetValue(true); });
        check([&] { p.SetValue(3); });
        check([&] { p.SetValue(3.0f); });
        check([&] { p.SetValue(Vector2(1,2)); });
        check([&] { p.SetValue(Vector3(1,2,3)); });
        check([&] { p.SetValue(Vector4(1,2,3,4)); });
        check([&] { p.SetValue(Quaternion(1,2,3,4)); });
        check([&] { p.SetValue(DistinctMatrix()); });
        check([&] { p.SetValueTranspose(DistinctMatrix()); });
        check([&] { p.SetValue(std::vector<bool>{}); });
        check([&] { p.SetValue(std::vector<int>{}); });
        check([&] { p.SetValue(std::vector<float>{}); });
        check([&] { p.SetValue(std::vector<Vector2>{}); });
        check([&] { p.SetValue(std::vector<Vector3>{}); });
        check([&] { p.SetValue(std::vector<Vector4>{}); });
        check([&] { p.SetValue(std::vector<Quaternion>{}); });
        check([&] { p.SetValue(std::vector<Matrix>{}); });
        check([&] { p.SetValueTranspose(std::vector<Matrix>{}); });
        check([&] { (void)p.GetValueBoolean(); });
        check([&] { (void)p.GetValueInt32(); });
        check([&] { (void)p.GetValueSingle(); });
        check([&] { (void)p.GetValueVector2(); });
        check([&] { (void)p.GetValueVector3(); });
        check([&] { (void)p.GetValueVector4(); });
        check([&] { (void)p.GetValueQuaternion(); });
        check([&] { (void)p.GetValueMatrix(); });
        check([&] { (void)p.GetValueMatrixTranspose(); });
        check([&] { (void)p.GetValueMatrixArray(1); });
        check([&] { (void)p.GetValueMatrixTransposeArray(1); });
    }
}

TEST(EffectReflectedApiTest, ObjectParametersRejectEveryNumericOverloadAndCountWins)
{
    GraphicsDevice device;
    for (auto type : {EffectParameterType::String, EffectParameterType::Texture,
                      EffectParameterType::Texture1D, EffectParameterType::Texture2D,
                      EffectParameterType::Texture3D, EffectParameterType::TextureCube})
    {
        SCOPED_TRACE(static_cast<int>(type));
        auto desc = Numeric(EffectParameterClass::Object, type, 1, 1);
        desc.rawValue.resize(4); desc.stringValue = "unchanged";
        auto effect = FromTable(device, desc);
        auto& p = *effect->getParametersProperty()[0];
        // EffectParameter scalar/typed shape guards and native object numeric refusal;
        // array getters IL_0000..0009 validate count before native/type access.
        EXPECT_THROW(p.SetValue(true), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(3), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(3.0f), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(Vector2(1,2)), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(Vector3(1,2,3)), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(Vector4(1,2,3,4)), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(Quaternion(1,2,3,4)), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(DistinctMatrix()), System::InvalidCastException);
        EXPECT_THROW(p.SetValueTranspose(DistinctMatrix()), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<bool>{true}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<int>{1}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<float>{1}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<Vector2>{{1,2}}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<Vector3>{{1,2,3}}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<Vector4>{{1,2,3,4}}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<Quaternion>{{1,2,3,4}}), System::InvalidCastException);
        EXPECT_THROW(p.SetValue(std::vector<Matrix>{DistinctMatrix()}), System::InvalidCastException);
        EXPECT_THROW(p.SetValueTranspose(std::vector<Matrix>{DistinctMatrix()}), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueBoolean(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueInt32(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueSingle(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueVector2(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueVector3(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueVector4(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueQuaternion(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueMatrix(), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueMatrixTranspose(), System::InvalidCastException);
        const auto checkArray = [](auto getter)
        {
            EXPECT_THROW((void)getter(1), System::InvalidCastException);
            for (int count : {0, -1})
                EXPECT_THROW((void)getter(count), System::ArgumentOutOfRangeException);
        };
        checkArray([&](int count) { return p.GetValueBooleanArray(count); });
        checkArray([&](int count) { return p.GetValueInt32Array(count); });
        checkArray([&](int count) { return p.GetValueSingleArray(count); });
        checkArray([&](int count) { return p.GetValueVector2Array(count); });
        checkArray([&](int count) { return p.GetValueVector3Array(count); });
        checkArray([&](int count) { return p.GetValueVector4Array(count); });
        checkArray([&](int count) { return p.GetValueQuaternionArray(count); });
        checkArray([&](int count) { return p.GetValueMatrixArray(count); });
        checkArray([&](int count) { return p.GetValueMatrixTransposeArray(count); });
        if (type == EffectParameterType::String)
            EXPECT_EQ(p.GetValueString(), "unchanged");
        else
        {
            p.SetValue(static_cast<Texture*>(nullptr));
            EXPECT_THROW((void)p.GetValueString(), System::InvalidCastException);
        }
    }
}

TEST(EffectReflectedApiTest, MatrixArrayCountsAndTransposeHaveIndependentLiteralOracle)
{
    GraphicsDevice device;
    auto effect = FromTable(device, Numeric(EffectParameterClass::Matrix,
        EffectParameterType::Single, 4, 4, 2));
    auto& p = *effect->getParametersProperty()[0];
    const Matrix a = DistinctMatrix();
    const Matrix transpose(1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16);
    const Matrix b(21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36);
    // XNA GetValueMatrix[Transpose]Array IL_0026..0058 clamps copied cells,
    // IL_0091..00b6 allocates requested count; the untouched result suffix is all zero.
    p.SetValue(std::vector<Matrix>{a,b});
    EXPECT_EQ(p.GetValueMatrixArray(1), (std::vector<Matrix>{a}));
    EXPECT_EQ(p.GetValueMatrixArray(3), (std::vector<Matrix>{a,b,Matrix()}));
    p.SetValueTranspose(std::vector<Matrix>{a});
    EXPECT_EQ(p.GetValueMatrixArray(2), (std::vector<Matrix>{transpose,b}));
    EXPECT_EQ(p.GetValueMatrixTransposeArray(1), (std::vector<Matrix>{a}));
    // SetValue[Transpose](Matrix[]) IL_0014..002d rejects overflow before any write.
    EXPECT_THROW(p.SetValue(std::vector<Matrix>{b,b,b}), System::InvalidCastException);
    EXPECT_THROW(p.SetValueTranspose(std::vector<Matrix>{b,b,b}), System::InvalidCastException);
    EXPECT_EQ(p.GetValueMatrixArray(2), (std::vector<Matrix>{transpose,b}));
    EXPECT_EQ(p.getElementsProperty()[1]->GetValueMatrix(), b);
}

TEST(EffectReflectedApiTest, StructureArrayMembersBelongToElementsOnly)
{
    GraphicsDevice device;
    auto member = Numeric(EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1);
    member.name = "Amount";
    auto structure = Numeric(EffectParameterClass::Struct, EffectParameterType::Void, 1, 1, 2);
    structure.structureMembers.push_back(member);
    const float first = 11, second = 22;
    std::memcpy(structure.rawValue.data(), &first, sizeof(first));
    std::memcpy(structure.rawValue.data()+16, &second, sizeof(second));
    auto effect = FromTable(device, structure);
    auto& p = *effect->getParametersProperty()[0];
    // XNA EffectParameter..ctor IL_00dd..0100 passes zero member count for array parents.
    EXPECT_EQ(p.getStructureMembersProperty().getCountProperty(), 0);
    EXPECT_EQ(p.getStructureMembersProperty()[0], nullptr);
    EXPECT_EQ(p.getStructureMembersProperty()["Amount"], nullptr);
    ASSERT_EQ(p.getElementsProperty().getCountProperty(), 2);
    auto& firstMembers = p.getElementsProperty()[0]->getStructureMembersProperty();
    auto& secondMembers = p.getElementsProperty()[1]->getStructureMembersProperty();
    ASSERT_EQ(firstMembers.getCountProperty(), 1);
    ASSERT_EQ(secondMembers.getCountProperty(), 1);
    ASSERT_NE(firstMembers["Amount"], nullptr);
    ASSERT_NE(secondMembers["Amount"], nullptr);
    EXPECT_FLOAT_EQ(firstMembers["Amount"]->GetValueSingle(), 11);
    EXPECT_FLOAT_EQ(secondMembers["Amount"]->GetValueSingle(), 22);
    firstMembers[0]->SetValue(31.0f);
    EXPECT_FLOAT_EQ(firstMembers[0]->GetValueSingle(), 31);
    EXPECT_FLOAT_EQ(secondMembers[0]->GetValueSingle(), 22);
    // XNA collection get_Item(int) IL_0000..0020 returns null outside the collection.
    EXPECT_EQ(p.getElementsProperty()[-1], nullptr);
    EXPECT_EQ(p.getElementsProperty()[2], nullptr);
    EXPECT_EQ(firstMembers[-1], nullptr);
    EXPECT_EQ(firstMembers[1], nullptr);
    const auto& c = p;
    EXPECT_EQ(c.getElementsProperty()[2], nullptr);
    EXPECT_EQ(c.getStructureMembersProperty()[0], nullptr);
}

TEST(EffectReflectedApiTest, StringAccessorsUseTheDefaultInvalidCastMessage)
{
    GraphicsDevice device;
    // Refresh GetValueString IL_0000..0012 and SetValue(string) IL_0000..0013
    // construct parameterless InvalidCastException: English .NET message below.
    for (auto type : {EffectParameterType::Bool, EffectParameterType::Int32,
                      EffectParameterType::Single, EffectParameterType::Texture,
                      EffectParameterType::Texture1D, EffectParameterType::Texture2D,
                      EffectParameterType::Texture3D, EffectParameterType::TextureCube})
    {
        auto desc = Numeric(type <= EffectParameterType::Single ? EffectParameterClass::Scalar
                                                                : EffectParameterClass::Object,
                            type, 1, 1);
        if (desc.parameterClass == EffectParameterClass::Object) desc.rawValue.resize(4);
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        const auto check = [](auto operation)
        {
            try { operation(); FAIL() << "Expected InvalidCastException"; }
            catch (const System::InvalidCastException& e)
            { EXPECT_EQ(e.getMessageProperty(), "Specified cast is not valid."); }
        };
        check([&] { (void)p.GetValueString(); });
        check([&] { p.SetValue(std::string("rejected")); });
    }
}

TEST(EffectReflectedApiTest, StringValuesAreIndependentAcrossClones)
{
    GraphicsDevice device;
    auto desc = Numeric(EffectParameterClass::Object, EffectParameterType::String, 1, 1);
    desc.rawValue.resize(4); desc.stringValue = "initial";
    auto source = FromTable(device, desc);
    auto& p = *source->getParametersProperty()[0];
    // XNA SetValue(string) IL_0000..0013, GetValueString IL_0000..0012;
    // Effect.Clone IL_0000..0006 delegates to native clone constructor IL_0048..007b.
    p.SetValue(std::string("source-before-clone"));
    std::unique_ptr<Effect> clone(source->Clone());
    auto& cp = *clone->getParametersProperty()[0];
    EXPECT_EQ(cp.GetValueString(), "source-before-clone");
    cp.SetValue(std::string("clone-only"));
    EXPECT_EQ(p.GetValueString(), "source-before-clone");
    p.SetValue(std::string(""));
    EXPECT_EQ(p.GetValueString(), "");
    EXPECT_EQ(cp.GetValueString(), "clone-only");
    EXPECT_THROW(cp.SetValue(static_cast<Texture*>(nullptr)), System::InvalidCastException);
    EXPECT_EQ(cp.GetValueString(), "clone-only");
}

TEST(EffectReflectedApiTest, ATextureReleasedByItsDevicesDisposalIsRefused)
{
    // HND-TQ-47 R2: Refresh SetValue(Texture) IL_0005-0015 calls CheckDisposed(value,
    // value.pStateTracker), which Texture2D::ReleaseNativeObject IL_0000-0018 cleared when the
    // texture's device was disposed; the texture itself is not disposed.
    GraphicsDevice released;
    Texture2D texture(released, 1, 1);
    released.Dispose();
    ASSERT_FALSE(texture.getIsDisposedProperty());

    GraphicsDevice device;
    auto desc = Numeric(EffectParameterClass::Object, EffectParameterType::Texture2D, 1, 1);
    desc.rawValue.resize(4);
    auto effect = FromTable(device, std::move(desc));
    auto& p = *effect->getParametersProperty()[0];
    EXPECT_THROW(p.SetValue(static_cast<Texture*>(&texture)), System::ObjectDisposedException);
    EXPECT_THROW(p.SetValue(&texture), System::ObjectDisposedException);
}

TEST(EffectReflectedApiTest, ActiveTargetRefusalUsesRefreshMessageAndPrecedesParameterType)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 1, 1);
    Texture2D previous(device, 1, 1);
    // Refresh SetValue(Texture) IL_001a..002c: MustResolveRenderTarget is tested
    // before the declaration type gate IL_002d..0059. Literal read from Framework.resources.
    for (auto type : {EffectParameterType::Single, EffectParameterType::Texture2D})
    {
        auto desc = Numeric(type == EffectParameterType::Single ? EffectParameterClass::Scalar
                                                               : EffectParameterClass::Object,
                            type, 1, 1);
        if (type == EffectParameterType::Texture2D) desc.rawValue.resize(4);
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        if (type == EffectParameterType::Texture2D) p.SetValue(&previous);
        device.SetRenderTarget(&target);
        const auto check = [](auto operation)
        {
            try { operation(); FAIL() << "Expected InvalidOperationException"; }
            catch (const System::InvalidOperationException& e)
            {
                EXPECT_EQ(e.getMessageProperty(),
                    "The render target must not be set on the device when it is used as a texture.");
            }
        };
        check([&] { p.SetValue(static_cast<Texture*>(&target)); });
        check([&] { p.SetValue(static_cast<Texture2D*>(&target)); });
        device.SetRenderTarget(nullptr);
        if (type == EffectParameterType::Texture2D)
        {
            EXPECT_EQ(p.GetValueTexture2D(), &previous);
            p.SetValue(static_cast<Texture*>(&target));
            EXPECT_EQ(p.GetValueTexture2D(), &target);
        }
        else
        {
            // Once unbound, the later type gate is visible again.
            EXPECT_THROW(p.SetValue(static_cast<Texture*>(&target)), System::InvalidCastException);
            EXPECT_EQ(p.GetValueSingle(), 0.0f);
        }
    }
}

TEST(EffectReflectedApiTest, TextureGetterDimensionsAndRejectedLifetimePreserveBinding)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1), disposed(device, 1, 1);
    disposed.Dispose();
    // XNA getter type gates: 2D/3D IL_0000..0019, Cube IL_0000..001a;
    // setter accepts all texture declarations, validates resource lifetime first IL_0002..0059.
    for (auto type : {EffectParameterType::Texture, EffectParameterType::Texture1D,
                      EffectParameterType::Texture2D, EffectParameterType::Texture3D,
                      EffectParameterType::TextureCube})
    {
        SCOPED_TRACE(static_cast<int>(type));
        auto desc = Numeric(EffectParameterClass::Object, type, 1, 1);
        desc.rawValue.resize(4);
        auto source = FromTable(device, desc);
        auto& p = *source->getParametersProperty()[0];
        p.SetValue(static_cast<Texture*>(nullptr));
        if (type == EffectParameterType::Texture || type == EffectParameterType::Texture2D)
            EXPECT_EQ(p.GetValueTexture2D(), nullptr);
        else
            EXPECT_THROW((void)p.GetValueTexture2D(), System::InvalidCastException);
        if (type == EffectParameterType::Texture || type == EffectParameterType::Texture3D)
            EXPECT_EQ(p.GetValueTexture3D(), nullptr);
        else
            EXPECT_THROW((void)p.GetValueTexture3D(), System::InvalidCastException);
        if (type == EffectParameterType::Texture || type == EffectParameterType::TextureCube)
            EXPECT_EQ(p.GetValueTextureCube(), nullptr);
        else
            EXPECT_THROW((void)p.GetValueTextureCube(), System::InvalidCastException);
    }
    auto desc = Numeric(EffectParameterClass::Object, EffectParameterType::Texture2D, 1, 1);
    desc.rawValue.resize(4);
    auto source = FromTable(device, desc);
    auto& p = *source->getParametersProperty()[0];
    p.SetValue(&texture);
    EXPECT_THROW(p.SetValue(&disposed), System::ObjectDisposedException);
    EXPECT_EQ(p.GetValueTexture2D(), &texture);
    EXPECT_THROW(p.SetValue(std::string("bad")), System::InvalidCastException);
    EXPECT_EQ(p.GetValueTexture2D(), &texture);
    std::unique_ptr<Effect> clone(source->Clone());
    EXPECT_EQ(clone->getParametersProperty()[0]->GetValueTexture2D(), &texture);
    clone->getParametersProperty()[0]->SetValue(static_cast<Texture*>(nullptr));
    EXPECT_EQ(p.GetValueTexture2D(), &texture);
    EXPECT_EQ(clone->getParametersProperty()[0]->GetValueTexture2D(), nullptr);
}

TEST(EffectReflectedApiTest, NumericShapeRefusalsPreserveAllComponents)
{
    GraphicsDevice device;
    // Managed XNA guards are independent of Bool/Int32/Single: SetValue(VectorN)
    // IL_0000..0030, Matrix IL_0000..0014, numeric scalars IL_0000..0012.
    // Audit every realizable numeric shape: Scalar 1x1; Vector 1x2/3/4;
    // Matrix 1..4 x 1..4; either a leaf or a two-element array parent.
    for (auto type : {EffectParameterType::Bool, EffectParameterType::Int32, EffectParameterType::Single})
    for (auto cls : {EffectParameterClass::Scalar, EffectParameterClass::Vector, EffectParameterClass::Matrix})
    for (int rows = 1; rows <= (cls == EffectParameterClass::Matrix ? 4 : 1); ++rows)
    for (int cols = (cls == EffectParameterClass::Vector ? 2 : 1);
         cols <= (cls == EffectParameterClass::Scalar ? 1 : 4); ++cols)
    for (int elements : {0, 2})
    {
        SCOPED_TRACE(testing::Message() << static_cast<int>(type) << "/" << static_cast<int>(cls)
                     << "/" << rows << "x" << cols << "/" << elements);
        auto effect = FromTable(device, Numeric(cls, type, rows, cols, elements));
        auto& p = *effect->getParametersProperty()[0];
        const int count = rows * cols * (elements ? elements : 1);
        const std::vector<float> expected(count, 1);
        p.SetValue(expected);
        if (elements || cls != EffectParameterClass::Vector || cols != 2)
            EXPECT_THROW(p.SetValue(Vector2(5,7)), System::InvalidCastException);
        if (elements || cls != EffectParameterClass::Vector || cols != 3)
            EXPECT_THROW(p.SetValue(Vector3(5,7,11)), System::InvalidCastException);
        if (elements || cls != EffectParameterClass::Vector || cols != 4)
        {
            EXPECT_THROW(p.SetValue(Vector4(5,7,11,13)), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(Quaternion(5,7,11,13)), System::InvalidCastException);
        }
        if (elements || cls != EffectParameterClass::Matrix)
        {
            EXPECT_THROW(p.SetValue(DistinctMatrix()), System::InvalidCastException);
            EXPECT_THROW(p.SetValueTranspose(DistinctMatrix()), System::InvalidCastException);
        }
        if (!elements || cls != EffectParameterClass::Vector)
        {
            EXPECT_THROW(p.SetValue(std::vector<Vector2>{{5,7}}), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(std::vector<Vector3>{{5,7,11}}), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(std::vector<Vector4>{{5,7,11,13}}), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(std::vector<Quaternion>{{5,7,11,13}}), System::InvalidCastException);
        }
        if (!elements || cls != EffectParameterClass::Matrix)
        {
            EXPECT_THROW(p.SetValue(std::vector<Matrix>{DistinctMatrix()}), System::InvalidCastException);
            EXPECT_THROW(p.SetValueTranspose(std::vector<Matrix>{DistinctMatrix()}), System::InvalidCastException);
            EXPECT_THROW((void)p.GetValueMatrixArray(1), System::InvalidCastException);
            EXPECT_THROW((void)p.GetValueMatrixTransposeArray(1), System::InvalidCastException);
        }
        if (elements)
        {
            EXPECT_THROW(p.SetValue(5.0f), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(5), System::InvalidCastException);
            EXPECT_THROW(p.SetValue(false), System::InvalidCastException);
        }
        else if (cls != EffectParameterClass::Scalar)
        {
            // Scalar getters IL_0000..001e permit Scalar OR Elements.Count > 0.
            EXPECT_THROW((void)p.GetValueBoolean(), System::InvalidCastException);
            EXPECT_THROW((void)p.GetValueInt32(), System::InvalidCastException);
            EXPECT_THROW((void)p.GetValueSingle(), System::InvalidCastException);
        }
        if (!elements && cls != EffectParameterClass::Scalar)
        {
            if (cls != EffectParameterClass::Vector || cols != 2)
                EXPECT_THROW((void)p.GetValueVector2(), System::InvalidCastException);
            if (cls != EffectParameterClass::Vector || cols != 3)
                EXPECT_THROW((void)p.GetValueVector3(), System::InvalidCastException);
            if (cls != EffectParameterClass::Vector || cols != 4)
            {
                EXPECT_THROW((void)p.GetValueVector4(), System::InvalidCastException);
                EXPECT_THROW((void)p.GetValueQuaternion(), System::InvalidCastException);
            }
            if (cls != EffectParameterClass::Matrix)
            {
                EXPECT_THROW((void)p.GetValueMatrix(), System::InvalidCastException);
                EXPECT_THROW((void)p.GetValueMatrixTranspose(), System::InvalidCastException);
            }
        }
        EXPECT_THROW(p.SetValue(std::string("invalid")), System::InvalidCastException);
        EXPECT_THROW((void)p.GetValueString(), System::InvalidCastException);
        EXPECT_EQ(p.GetValueSingleArray(count), expected);
    }
}

TEST(EffectReflectedApiTest, NumericMatrixArraysTraverseRowsAndPreservePartialSuffix)
{
    GraphicsDevice device;
    auto effect = FromTable(device, Numeric(EffectParameterClass::Matrix,
        EffectParameterType::Single, 4, 4));
    auto& p = *effect->getParametersProperty()[0];
    p.SetValue(DistinctMatrix());
    // XNA GetValueSingleArray IL_0043..00d1 traverses Matrix.M11 with row*4+column.
    // Numeric array setters IL_005b..00e4 read the old matrix and replace that same prefix.
    const std::vector<float> rows{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    EXPECT_EQ(p.GetValueSingleArray(16), rows);
    EXPECT_EQ(p.GetValueInt32Array(16), (std::vector<int>{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}));
    p.SetValue(std::vector<float>{17,19});
    EXPECT_EQ(p.GetValueMatrix(), Matrix(17,19,3,4,5,6,7,8,9,10,11,12,13,14,15,16));
    p.SetValue(std::vector<int>{23,29,31});
    EXPECT_EQ(p.GetValueMatrix(), Matrix(23,29,31,4,5,6,7,8,9,10,11,12,13,14,15,16));
    p.SetValue(std::vector<bool>{false,true,false});
    EXPECT_EQ(p.GetValueMatrix(), Matrix(0,1,0,4,5,6,7,8,9,10,11,12,13,14,15,16));
    EXPECT_EQ(p.GetValueBooleanArray(4), (std::vector<bool>{false,true,false,true}));
}

TEST(EffectReflectedApiTest, ScalarGettersBroadcastConvertedValuesToEveryTypedLeafGetter)
{
    GraphicsDevice device;
    // Refresh typed getters read GetFloat for Scalar and explicitly fill every component:
    // Vector2 IL_0022..005b, Vector3 IL_0022..0063, Vector4/Quaternion IL_0022..006e,
    // Matrix/Transpose IL_0028..00d1. GetInt truncates; GetBool compares with zero.
    for (auto type : {EffectParameterType::Single, EffectParameterType::Int32, EffectParameterType::Bool})
    {
        auto desc = Numeric(EffectParameterClass::Scalar, type, 1, 1);
        // Literal native cells; integers and -3.5 are exact binary32 values (zero tolerance).
        // Truncation toward zero gives -3; canonical Bool 1 reads true.
        const float single = -3.5f;
        const int integer = type == EffectParameterType::Bool ? 1 : -3;
        if (type == EffectParameterType::Single)
            std::memcpy(desc.rawValue.data(), &single, 4);
        else
            std::memcpy(desc.rawValue.data(), &integer, 4);
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        const float expected = type == EffectParameterType::Single ? -3.5f
                             : type == EffectParameterType::Int32 ? -3.0f : 1.0f;
        EXPECT_TRUE(p.GetValueBoolean());
        EXPECT_EQ(p.GetValueInt32(), type == EffectParameterType::Bool ? 1 : -3);
        EXPECT_EQ(p.GetValueSingle(), expected);
        EXPECT_EQ(p.GetValueVector2(), Vector2(expected, expected));
        EXPECT_EQ(p.GetValueVector3(), Vector3(expected, expected, expected));
        EXPECT_EQ(p.GetValueVector4(), Vector4(expected, expected, expected, expected));
        EXPECT_EQ(p.GetValueQuaternion(), Quaternion(expected, expected, expected, expected));
        const Matrix broadcast(expected,expected,expected,expected, expected,expected,expected,expected,
                               expected,expected,expected,expected, expected,expected,expected,expected);
        EXPECT_EQ(p.GetValueMatrix(), broadcast);
        EXPECT_EQ(p.GetValueMatrixTranspose(), broadcast);
    }
}

TEST(EffectReflectedApiTest, NumericTypedArraysFlattenAcrossWidthsWithoutReadingRegisterPadding)
{
    GraphicsDevice device;
    // Refresh typed array getters clamp to descriptor.Bytes/4 and allocate requested count
    // (Vector2 IL_0037..0050, IL_00b0..00d7); SetFloatArray/GetFloatArray use logical cells.
    for (auto type : {EffectParameterType::Single, EffectParameterType::Int32, EffectParameterType::Bool})
    for (int width : {1, 2, 3, 4})
    for (int elements : {0, 2})
    {
        SCOPED_TRACE(testing::Message() << static_cast<int>(type) << "/" << width << "/" << elements);
        auto desc = Numeric(width == 1 ? EffectParameterClass::Scalar : EffectParameterClass::Vector,
                            type, 1, width, elements);
        const int rows = elements ? elements : 1;
        for (int row = 0; row < rows; ++row)
        for (int col = 0; col < 4; ++col)
        {
            // Padding 123 must never leak into a typed result. Bool uses 0,1,0,1,... .
            const int logical = row * width + col;
            const int cell = col >= width ? 123 : type == EffectParameterType::Bool ? logical % 2 : logical + 1;
            if (type == EffectParameterType::Single)
            {
                const float value = static_cast<float>(cell);
                std::memcpy(desc.rawValue.data() + row*16 + col*4, &value, 4);
            }
            else std::memcpy(desc.rawValue.data() + row*16 + col*4, &cell, 4);
        }
        auto effect = FromTable(device, std::move(desc));
        auto& p = *effect->getParametersProperty()[0];
        const int cells = width * rows;
        const auto value = [=](int index) -> float
        { return index >= cells ? 0.0f : static_cast<float>(type == EffectParameterType::Bool ? index % 2 : index + 1); };
        for (int count : {1, 3, 5})
        {
            const auto v2 = p.GetValueVector2Array(count);
            const auto v3 = p.GetValueVector3Array(count);
            const auto v4 = p.GetValueVector4Array(count);
            const auto q = p.GetValueQuaternionArray(count);
            ASSERT_EQ(v2.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(v3.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(v4.size(), static_cast<std::size_t>(count));
            ASSERT_EQ(q.size(), static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                EXPECT_EQ(v2[i], Vector2(value(2*i), value(2*i+1)));
                EXPECT_EQ(v3[i], Vector3(value(3*i), value(3*i+1), value(3*i+2)));
                EXPECT_EQ(v4[i], Vector4(value(4*i), value(4*i+1), value(4*i+2), value(4*i+3)));
                EXPECT_EQ(q[i], Quaternion(value(4*i), value(4*i+1), value(4*i+2), value(4*i+3)));
            }
        }
    }
}

TEST(EffectReflectedApiTest, EmptyNumericAndTypedArrayWritesStillValidateStructureClass)
{
    GraphicsDevice device;
    auto desc = Numeric(EffectParameterClass::Struct, EffectParameterType::Void, 1, 1);
    auto member = Numeric(EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1);
    member.name = "Amount";
    desc.structureMembers.push_back(member);
    const float initial = 17;
    std::memcpy(desc.rawValue.data(), &initial, 4);
    auto effect = FromTable(device, std::move(desc));
    auto& p = *effect->getParametersProperty()[0];
    // Refresh numeric array setters IL_0000..001e reject Struct BEFORE testing length.
    // Vector/Quaternion arrays IL_0000..001c, Matrix arrays IL_0000..002d do likewise.
    const auto cast = [](auto operation)
    {
        try { operation(); FAIL() << "Expected InvalidCastException"; }
        catch (const System::InvalidCastException& e)
        { EXPECT_EQ(e.getMessageProperty(), "Specified cast is not valid."); }
    };
    cast([&] { p.SetValue(std::vector<bool>{}); });
    cast([&] { p.SetValue(std::vector<int>{}); });
    cast([&] { p.SetValue(std::vector<float>{}); });
    cast([&] { p.SetValue(std::vector<Vector2>{}); });
    cast([&] { p.SetValue(std::vector<Vector3>{}); });
    cast([&] { p.SetValue(std::vector<Vector4>{}); });
    cast([&] { p.SetValue(std::vector<Quaternion>{}); });
    cast([&] { p.SetValue(std::vector<Matrix>{}); });
    cast([&] { p.SetValueTranspose(std::vector<Matrix>{}); });
    cast([&] { p.SetValue(2.0f); });
    cast([&] { p.SetValue(2); });
    cast([&] { p.SetValue(true); });
    cast([&] { (void)p.GetValueSingle(); });
    cast([&] { (void)p.GetValueInt32(); });
    cast([&] { (void)p.GetValueBoolean(); });
    cast([&] { (void)p.GetValueVector2(); });
    cast([&] { (void)p.GetValueVector3(); });
    cast([&] { (void)p.GetValueVector4(); });
    cast([&] { (void)p.GetValueQuaternion(); });
    cast([&] { (void)p.GetValueMatrix(); });
    cast([&] { (void)p.GetValueMatrixTranspose(); });
    auto& members = p.getStructureMembersProperty();
    ASSERT_EQ(members.getCountProperty(), 1);
    ASSERT_NE(members["Amount"], nullptr);
    EXPECT_EQ(members["Amount"]->GetValueSingle(), 17);
    members["Amount"]->SetValue(23.0f);
    EXPECT_EQ(members[0]->GetValueSingle(), 23);
    EXPECT_EQ(p.getElementsProperty().getCountProperty(), 0);
}

TEST(EffectReflectedApiTest, ParameterAnnotationsStayWithTheirOwnerAndHaveNoElementCopies)
{
    GraphicsDevice device;
    auto desc = Numeric(EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1, 2);
    CompiledEffectAnnotationDescription a;
    a.name = "Label"; a.parameterClass = EffectParameterClass::Object;
    a.parameterType = EffectParameterType::String; a.rowCount = a.columnCount = 1;
    a.stringValue = "parent label"; a.rawValue.resize(4);
    desc.annotations.push_back(a);
    auto effect = FromTable(device, std::move(desc));
    auto& p = *effect->getParametersProperty()[0];
    // Refresh .ctor IL_00c0..00d8 constructs annotations for the supplied handle;
    // array elements use their own handles. HLSL annotations belong to the declaration.
    auto& annotations = p.getAnnotationsProperty();
    ASSERT_EQ(annotations.getCountProperty(), 1);
    ASSERT_NE(annotations["Label"], nullptr);
    EXPECT_EQ(annotations["Label"]->GetValueString(), "parent label");
    EXPECT_THROW((void)annotations["Label"]->GetValueVector2(), System::InvalidCastException);
    ASSERT_EQ(p.getElementsProperty().getCountProperty(), 2);
    for (int i : {0, 1})
        EXPECT_EQ(p.getElementsProperty()[i]->getAnnotationsProperty().getCountProperty(), 0);
    const auto& c = p;
    EXPECT_EQ(c.getAnnotationsProperty()["Label"], annotations["Label"]);
    std::unique_ptr<Effect> clone(effect->Clone());
    auto& cloned = clone->getParametersProperty()[0]->getAnnotationsProperty();
    ASSERT_EQ(cloned.getCountProperty(), 1);
    EXPECT_NE(cloned["Label"], annotations["Label"]);
    EXPECT_EQ(cloned["Label"]->GetValueString(), "parent label");
}
