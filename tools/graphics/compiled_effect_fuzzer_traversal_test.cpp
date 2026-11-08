// SPDX-License-Identifier: MS-PL
#include "compiled_effect_fuzzer_support.hpp"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <utility>

namespace
{
    using namespace CNA::Tools::FxFuzz;

    // Distinct argument types keep overloaded setters distinguishable without a graphics device.
    struct Matrix { static Matrix getIdentityProperty() { return {}; } };
    struct Quaternion { static const Quaternion Identity; };
    const Quaternion Quaternion::Identity{};
    struct Vector2 { Vector2(float, float) {} };
    struct Vector3 { Vector3(float, float, float) {} };
    struct Vector4 { Vector4(float, float, float, float) {} };
    struct Texture {};
    struct Texture2D : Texture {};
    struct Texture3D : Texture {};
    struct TextureCube : Texture {};

    struct Visits
    {
        std::map<std::string, int> calls;
        bool reject = false;
        bool exhaust = false;
        void Call(const std::string& name)
        {
            ++calls[name];
            if (exhaust) { throw std::bad_alloc{}; }
            if (reject) { throw std::runtime_error("synthetic API rejection"); }
        }
    };

    template<class T> struct Collection
    {
        std::vector<T*> values;
        int getCountProperty() const { return static_cast<int>(values.size()); }
        T* operator[](int index) const { return values.at(index); }
    };

    struct Metadata
    {
        int getNameProperty() const { return 0; }
        int getSemanticProperty() const { return 0; }
        int getParameterClassProperty() const { return 0; }
        int getParameterTypeProperty() const { return 0; }
        int getRowCountProperty() const { return 0; }
        int getColumnCountProperty() const { return 0; }
    };

#define GETTER(name) void name() const { visits->Call(#name); }
#define ARRAY_GETTER(name) void name(int count) const { visits->Call(std::string(#name) + "/" + std::to_string(count)); }
#define SETTER(type, name) void SetValue(type) { visits->Call("SetValue/" name); }
    struct EffectAnnotation : Metadata
    {
        Visits* visits = nullptr;
        GETTER(GetValueBoolean)
        GETTER(GetValueInt32)
        GETTER(GetValueSingle)
        GETTER(GetValueString)
        GETTER(GetValueVector2)
        GETTER(GetValueVector3)
        GETTER(GetValueVector4)
        GETTER(GetValueMatrix)
    };
    using EffectAnnotationCollection = Collection<EffectAnnotation>;

    struct EffectParameter : Metadata
    {
        Visits* visits = nullptr;
        EffectAnnotationCollection annotations;
        Collection<EffectParameter> elements, members;
        const auto& getAnnotationsProperty() const { return annotations; }
        auto& getElementsProperty() { return elements; }
        auto& getStructureMembersProperty() { return members; }
        GETTER(GetValueBoolean)
        GETTER(GetValueInt32)
        GETTER(GetValueSingle)
        GETTER(GetValueString)
        GETTER(GetValueMatrix)
        GETTER(GetValueMatrixTranspose)
        GETTER(GetValueQuaternion)
        GETTER(GetValueVector2)
        GETTER(GetValueVector3)
        GETTER(GetValueVector4)
        GETTER(GetValueTexture2D)
        GETTER(GetValueTexture3D)
        GETTER(GetValueTextureCube)
        ARRAY_GETTER(GetValueBooleanArray)
        ARRAY_GETTER(GetValueInt32Array)
        ARRAY_GETTER(GetValueSingleArray)
        ARRAY_GETTER(GetValueMatrixArray)
        ARRAY_GETTER(GetValueMatrixTransposeArray)
        ARRAY_GETTER(GetValueQuaternionArray)
        ARRAY_GETTER(GetValueVector2Array)
        ARRAY_GETTER(GetValueVector3Array)
        ARRAY_GETTER(GetValueVector4Array)
        SETTER(bool, "Boolean")
        SETTER(std::vector<bool>, "BooleanArray")
        SETTER(int, "Int32")
        SETTER(std::vector<int>, "Int32Array")
        SETTER(float, "Single")
        SETTER(std::vector<float>, "SingleArray")
        SETTER(std::string, "String")
        SETTER(Matrix, "Matrix")
        SETTER(std::vector<Matrix>, "MatrixArray")
        SETTER(Quaternion, "Quaternion")
        SETTER(std::vector<Quaternion>, "QuaternionArray")
        SETTER(Vector2, "Vector2")
        SETTER(std::vector<Vector2>, "Vector2Array")
        SETTER(Vector3, "Vector3")
        SETTER(std::vector<Vector3>, "Vector3Array")
        SETTER(Vector4, "Vector4")
        SETTER(std::vector<Vector4>, "Vector4Array")
        SETTER(Texture*, "Texture")
        SETTER(Texture2D*, "Texture2D")
        SETTER(Texture3D*, "Texture3D")
        SETTER(TextureCube*, "TextureCube")
        void SetValueTranspose(Matrix) { visits->Call("SetValueTranspose/Matrix"); }
        void SetValueTranspose(std::vector<Matrix>) { visits->Call("SetValueTranspose/MatrixArray"); }
    };
#undef GETTER
#undef ARRAY_GETTER
#undef SETTER

#include "compiled_effect_fuzzer_traversal.inc"

    // Independent oracle: L14's traversal contract requires each public typed operation once
    // per visited node; every array getter is attempted separately for counts 0, 1 and 4.
    // These inventories are handwritten, never extracted from the production operation list.
    std::map<std::string, int> ParameterCalls(int nodes = 1)
    {
        std::map<std::string, int> expected;
        for (const auto* type : {"Boolean", "Int32", "Single", "String", "Matrix", "MatrixTranspose",
                               "Quaternion", "Vector2", "Vector3", "Vector4", "Texture2D", "Texture3D", "TextureCube"})
        {
            expected[std::string("GetValue") + type] = nodes;
        }
        for (const auto* type : {"Boolean", "Int32", "Single", "Matrix", "MatrixTranspose",
                               "Quaternion", "Vector2", "Vector3", "Vector4"})
        {
            for (int count : {0, 1, 4})
            {
                expected[std::string("GetValue") + type + "Array/" + std::to_string(count)] = nodes;
            }
        }
        for (const auto* type : {"Boolean", "BooleanArray", "Int32", "Int32Array", "Single", "SingleArray", "String",
                               "Matrix", "MatrixArray", "Quaternion", "QuaternionArray", "Vector2", "Vector2Array",
                               "Vector3", "Vector3Array", "Vector4", "Vector4Array", "Texture", "Texture2D", "Texture3D", "TextureCube"})
        {
            expected[std::string("SetValue/") + type] = nodes;
        }
        expected["SetValueTranspose/Matrix"] = nodes;
        expected["SetValueTranspose/MatrixArray"] = nodes;
        return expected;
    }

    std::map<std::string, int> AnnotationCalls(int nodes = 1)
    {
        std::map<std::string, int> expected;
        for (const auto* type : {"Boolean", "Int32", "Single", "String", "Vector2", "Vector3", "Vector4", "Matrix"})
        {
            expected[std::string("GetValue") + type] = nodes;
        }
        return expected;
    }

    void ExpectCalls(const Visits& actual, const std::map<std::string, int>& expected)
    {
        for (const auto& [name, count] : expected)
        {
            const auto found = actual.calls.find(name);
            EXPECT_EQ(found == actual.calls.end() ? 0 : found->second, count) << name;
        }
        EXPECT_EQ(actual.calls.size(), expected.size()) << "unexpected typed operation";
    }

    TEST(CompiledEffectFuzzerTraversal, EveryParameterOperationIsIndependentlyAttempted)
    {
        for (bool reject : {false, true})
        {
            SCOPED_TRACE(reject ? "each operation rejects" : "all operations accept");
            stats = {};
            Visits visits;
            visits.reject = reject;
            EffectParameter parameter;
            parameter.visits = &visits;
            Budget budget;
            budget.nodes = 1;
            WalkParameter(parameter, 0, budget);
            ExpectCalls(visits, ParameterCalls());
            EXPECT_EQ(budget.nodes, 0);
            EXPECT_EQ(stats.parameters, 1u);
            // 13 scalar getters + 9 array getters * 3 counts + 23 setters = 63.
            EXPECT_EQ(stats.rejections, reject ? 63u : 0u);
        }
    }

    TEST(CompiledEffectFuzzerTraversal, EveryAnnotationGetterIsIndependentlyAttempted)
    {
        for (bool reject : {false, true})
        {
            SCOPED_TRACE(reject);
            stats = {};
            Visits visits;
            visits.reject = reject;
            EffectAnnotation annotation;
            annotation.visits = &visits;
            Budget budget;
            budget.annotations = 1;
            WalkAnnotations(EffectAnnotationCollection{{&annotation}}, budget);
            ExpectCalls(visits, AnnotationCalls());
            EXPECT_EQ(budget.annotations, 0);
            EXPECT_EQ(stats.annotations, 1u);
            EXPECT_EQ(stats.rejections, reject ? 8u : 0u);
        }
    }

    TEST(CompiledEffectFuzzerTraversal, ElementsMembersAndAnnotationsHaveIndependentBudgets)
    {
        Visits visits, annotations;
        EffectAnnotation annotation;
        annotation.visits = &annotations;
        EffectParameter root, element, member, excluded;
        for (auto* parameter : {&root, &element, &member, &excluded})
        {
            parameter->visits = &visits;
            parameter->annotations.values = {nullptr, &annotation, &annotation};
        }
        root.elements.values = {nullptr, &element};
        root.members.values = {&member, &excluded};
        Budget budget;
        budget.nodes = 3;
        budget.annotations = 2;
        WalkParameter(root, 0, budget);
        ExpectCalls(visits, ParameterCalls(3));
        // Null annotation consumes one slot, then exactly one non-null annotation is visited.
        ExpectCalls(annotations, AnnotationCalls());
        EXPECT_EQ(budget.nodes, 0);
        EXPECT_EQ(budget.annotations, 0);
    }

    TEST(CompiledEffectFuzzerTraversal, DepthAndEmptyBudgetsPreventAccessAndAllocationFailureEscapes)
    {
        Visits visits;
        EffectParameter parameter;
        parameter.visits = &visits;
        Budget budget;
        WalkParameter(parameter, 9, budget);
        EXPECT_TRUE(visits.calls.empty());
        budget.nodes = 0;
        WalkParameter(parameter, 0, budget);
        EXPECT_TRUE(visits.calls.empty());
        budget.nodes = 1;
        visits.exhaust = true;
        EXPECT_THROW(WalkParameter(parameter, 0, budget), std::bad_alloc);
        EXPECT_EQ(visits.calls.size(), 1u);
        EXPECT_EQ(visits.calls.at("GetValueBoolean"), 1);
    }
}
