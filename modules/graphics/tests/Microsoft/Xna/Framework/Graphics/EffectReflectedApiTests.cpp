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
