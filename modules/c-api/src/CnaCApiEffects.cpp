// SPDX-License-Identifier: MS-PL

#include "CNA/C/effects.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetOwnedTexture3D;
using CNA::C::Detail::GetOwnedTextureCube;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::BorrowGameGraphicsDevice;
using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TextureResourceView;
using CNA::C::Detail::TextureCubeResourceView;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::EffectAnnotation;
using Microsoft::Xna::Framework::Graphics::EffectAnnotationCollection;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::EffectParameter;
using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
using Microsoft::Xna::Framework::Graphics::EffectParameterCollection;
using Microsoft::Xna::Framework::Graphics::EffectParameterType;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::EffectPassCollection;
using Microsoft::Xna::Framework::Graphics::EffectTechnique;
using Microsoft::Xna::Framework::Graphics::EffectTechniqueCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SpriteEffect;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

constexpr uint32_t StructureVersion = UINT32_C(1);

class EffectOwnership final {
public:
    EffectOwnership()
    {
        AddOwnedGraphicsResource();
    }

    EffectOwnership(const EffectOwnership&) = delete;
    EffectOwnership& operator=(const EffectOwnership&) = delete;

    ~EffectOwnership()
    {
        RemoveOwnedGraphicsResource();
    }
};

class CApiEffect final : public Effect {
public:
    explicit CApiEffect(GraphicsDevice& device)
        : Effect(device)
    {
    }

    CApiEffect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode)
        : Effect(device, effectCode)
    {
    }

    [[nodiscard]] Effect* Clone() override
    {
        return new CApiEffect(getGraphicsDeviceInternal());
    }

protected:
    void OnApply() override
    {
    }
};

struct AnnotationResource final {
    std::shared_ptr<EffectAnnotation> value;
};

struct AnnotationCollectionResource final {
    std::shared_ptr<EffectAnnotationCollection> value;
};

struct RetainedTextureSlot final {
    CNA_Handle handle = CNA_INVALID_HANDLE;
    std::shared_ptr<Texture> value;
    std::shared_ptr<void> owner;
    uint64_t* referenceCount = nullptr;

    RetainedTextureSlot() = default;
    RetainedTextureSlot(const RetainedTextureSlot&) = delete;
    RetainedTextureSlot& operator=(const RetainedTextureSlot&) = delete;

    ~RetainedTextureSlot()
    {
        Reset();
    }

    void Reset() noexcept
    {
        if (referenceCount != nullptr) {
            --*referenceCount;
        }
        handle = CNA_INVALID_HANDLE;
        value.reset();
        owner.reset();
        referenceCount = nullptr;
    }

    void Set(
        const CNA_Handle newHandle,
        std::shared_ptr<Texture> newValue,
        std::shared_ptr<void> newOwner,
        uint64_t* const newReferenceCount)
    {
        Reset();
        handle = newHandle;
        value = std::move(newValue);
        owner = std::move(newOwner);
        referenceCount = newReferenceCount;
        if (referenceCount != nullptr) {
            ++*referenceCount;
        }
    }
};

struct ParameterCollectionState;

struct ParameterState final {
    RetainedTextureSlot baseTexture;
    RetainedTextureSlot texture2D;
    RetainedTextureSlot texture3D;
    RetainedTextureSlot textureCube;
    std::shared_ptr<ParameterCollectionState> elements;
    std::shared_ptr<ParameterCollectionState> members;
    std::shared_ptr<void> effectOwnership;
};

struct ParameterCollectionState final {
    std::shared_ptr<EffectParameterCollection> value;
    std::unordered_map<EffectParameter*, std::shared_ptr<ParameterState>> elementStates;
    std::shared_ptr<void> effectOwnership;
};

struct ParameterResource final {
    std::shared_ptr<EffectParameter> value;
    std::shared_ptr<ParameterState> state;
};

struct ParameterCollectionResource final {
    std::shared_ptr<ParameterCollectionState> state;
};

struct PassCollectionState final {
    std::shared_ptr<EffectPassCollection> value;
    Microsoft::Xna::Framework::Graphics::Effect* owner = nullptr;
    std::shared_ptr<void> effectOwnership;
};

struct TechniqueState final {
    std::shared_ptr<PassCollectionState> passes;
    Microsoft::Xna::Framework::Graphics::Effect* owner = nullptr;
    std::shared_ptr<void> effectOwnership;
};

struct TechniqueCollectionState final {
    std::shared_ptr<EffectTechniqueCollection> value;
    Microsoft::Xna::Framework::Graphics::Effect* owner = nullptr;
    std::unordered_map<EffectTechnique*, std::shared_ptr<TechniqueState>> elementStates;
    std::shared_ptr<void> effectOwnership;
};

struct PassResource final {
    std::shared_ptr<EffectPass> value;
    std::shared_ptr<void> effectOwnership;
};

struct PassCollectionResource final {
    std::shared_ptr<PassCollectionState> state;
};

struct TechniqueResource final {
    std::shared_ptr<EffectTechnique> value;
    std::shared_ptr<TechniqueState> state;
};

struct TechniqueCollectionResource final {
    std::shared_ptr<TechniqueCollectionState> state;
};

struct EffectLifetime final {
    EffectOwnership ownership;
    std::unordered_map<int, RetainedTextureSlot> shaderTextures;
};

struct EffectState final {
    std::shared_ptr<EffectLifetime> lifetime;
    std::shared_ptr<ParameterCollectionState> parameters;
    std::shared_ptr<TechniqueCollectionState> techniques;
};

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result GetAnnotation(
    const CNA_EffectAnnotationHandle handle,
    std::shared_ptr<AnnotationResource>* const outAnnotation)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectAnnotation, outAnnotation);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectAnnotation handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_EffectAnnotationCollectionHandle handle,
    std::shared_ptr<AnnotationCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectAnnotationCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectAnnotationCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateAnnotationHandle(
    const EffectAnnotation& annotation,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    const auto resource = std::make_shared<AnnotationResource>(
        AnnotationResource{std::make_shared<EffectAnnotation>(annotation)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectAnnotation, resource, outAnnotation);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned EffectAnnotation handle could not be created.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result GetStringByteCount(
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr) {
        return InvalidArgument("The EffectAnnotation string byte-count output is null.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The EffectAnnotation string output is invalid.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete EffectAnnotation string.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Vector2 ToC(const Vector2 value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3 value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Vector4 ToC(const Vector4 value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] CNA_Matrix ToC(const Matrix value) noexcept
{
    return CNA_Matrix{
        value.M11, value.M12, value.M13, value.M14,
        value.M21, value.M22, value.M23, value.M24,
        value.M31, value.M32, value.M33, value.M34,
        value.M41, value.M42, value.M43, value.M44};
}

[[nodiscard]] CNA_Quaternion ToC(const Quaternion value) noexcept
{
    return CNA_Quaternion{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] Matrix ToNative(const CNA_Matrix value) noexcept
{
    return Matrix{
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44};
}

[[nodiscard]] Quaternion ToNative(const CNA_Quaternion value) noexcept
{
    return Quaternion{value.x, value.y, value.z, value.w};
}

[[nodiscard]] Vector2 ToNative(const CNA_Vector2 value) noexcept
{
    return Vector2{value.x, value.y};
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value) noexcept
{
    return Vector3{value.x, value.y, value.z};
}

[[nodiscard]] Vector4 ToNative(const CNA_Vector4 value) noexcept
{
    return Vector4{value.x, value.y, value.z, value.w};
}

[[nodiscard]] CNA_Result GetParameter(
    const CNA_EffectParameterHandle handle,
    std::shared_ptr<ParameterResource>* const outParameter)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectParameter, outParameter);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectParameter handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetParameterCollection(
    const CNA_EffectParameterCollectionHandle handle,
    std::shared_ptr<ParameterCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectParameterCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectParameterCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateParameterHandle(
    std::shared_ptr<EffectParameter> value,
    std::shared_ptr<ParameterState> state,
    CNA_EffectParameterHandle* const outParameter)
{
    const auto resource = std::make_shared<ParameterResource>(
        ParameterResource{std::move(value), std::move(state)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectParameter, resource, outParameter);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned EffectParameter handle could not be created.");
}

[[nodiscard]] CNA_Result CreateParameterCollectionHandle(
    std::shared_ptr<ParameterCollectionState> state,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<ParameterCollectionResource>(
        ParameterCollectionResource{std::move(state)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectParameterCollection, resource, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned EffectParameterCollection handle could not be created.");
}

[[nodiscard]] CNA_Result CreateNativeParameter(
    const CNA_EffectParameterCreateInfo* const createInfo,
    std::unique_ptr<EffectParameter>* const outParameter)
{
    if (createInfo == nullptr || outParameter == nullptr ||
        createInfo->struct_size < sizeof(CNA_EffectParameterCreateInfo) ||
        createInfo->struct_version != StructureVersion ||
        createInfo->parameter_class > CNA_EFFECT_PARAMETER_CLASS_STRUCT ||
        createInfo->parameter_type > CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE) {
        return InvalidArgument("The EffectParameter creation configuration is invalid.");
    }
    std::string name;
    std::string semantic;
    if (const CNA_Result result = CopyStringView(createInfo->name, true, &name);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The EffectParameter name is not valid UTF-8 text.");
    }
    if (const CNA_Result result = CopyStringView(createInfo->semantic, true, &semantic);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The EffectParameter semantic is not valid UTF-8 text.");
    }
    *outParameter = std::make_unique<EffectParameter>(
        std::move(name),
        std::move(semantic),
        createInfo->row_count,
        createInfo->column_count,
        static_cast<EffectParameterClass>(createInfo->parameter_class),
        static_cast<EffectParameterType>(createInfo->parameter_type));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] std::shared_ptr<ParameterState> GetElementState(
    const std::shared_ptr<ParameterCollectionState>& collection,
    EffectParameter* const parameter)
{
    auto [iterator, inserted] = collection->elementStates.try_emplace(parameter);
    if (inserted || iterator->second == nullptr) {
        iterator->second = std::make_shared<ParameterState>();
        iterator->second->effectOwnership = collection->effectOwnership;
    }
    return iterator->second;
}

[[nodiscard]] CNA_Result CreateCollectionElementHandle(
    const std::shared_ptr<ParameterCollectionState>& collection,
    EffectParameter* const parameter,
    CNA_EffectParameterHandle* const outParameter)
{
    return CreateParameterHandle(
        std::shared_ptr<EffectParameter>(collection->value, parameter),
        GetElementState(collection, parameter),
        outParameter);
}

template<typename TValue>
void LoadValue(const void* const source, TValue* const outValue) noexcept
{
    std::memcpy(outValue, source, sizeof(TValue));
}

template<typename TValue>
[[nodiscard]] CNA_Result LoadValues(
    const void* const source,
    const uint64_t count,
    std::vector<TValue>* const outValues)
{
    std::size_t byteCount = 0U;
    const CNA_Result result = CheckedElementByteCount(
        source, count, sizeof(TValue), &byteCount);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The EffectParameter value array is invalid.");
    }
    outValues->resize(static_cast<std::size_t>(count));
    if (byteCount != 0U) {
        std::memcpy(outValues->data(), source, byteCount);
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TValue>
[[nodiscard]] CNA_Result CopyValues(
    const std::vector<TValue>& values,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The EffectParameter value-array output is invalid.");
    }
    *outCount = static_cast<uint64_t>(values.size());
    if (capacity < values.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete EffectParameter value array.");
    }
    if (!values.empty()) {
        std::memcpy(destination, values.data(), values.size() * sizeof(TValue));
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TNative, typename TC, typename TConvert>
[[nodiscard]] CNA_Result CopyConvertedValues(
    const std::vector<TNative>& nativeValues,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outCount,
    TConvert&& convert)
{
    std::vector<TC> values;
    values.reserve(nativeValues.size());
    for (const TNative& value : nativeValues) {
        values.push_back(std::forward<TConvert>(convert)(value));
    }
    return CopyValues(values, destination, capacity, outCount);
}

[[nodiscard]] CNA_Result RequestedCountToInt(
    const uint64_t count,
    int* const outCount)
{
    if (count > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The requested EffectParameter value count exceeds the native range.");
    }
    *outCount = static_cast<int>(count);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyEffectName(
    const CNA_StringView name,
    std::string* const outName,
    const char* const failureMessage)
{
    const CNA_Result result = CopyStringView(name, true, outName);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(result, ErrorCategoryForResult(result), failureMessage);
}

[[nodiscard]] CNA_Result GetPass(
    const CNA_EffectPassHandle handle,
    std::shared_ptr<PassResource>* const outPass)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectPass, outPass);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The EffectPass handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetPassCollection(
    const CNA_EffectPassCollectionHandle handle,
    std::shared_ptr<PassCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectPassCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The EffectPassCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetTechnique(
    const CNA_EffectTechniqueHandle handle,
    std::shared_ptr<TechniqueResource>* const outTechnique)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectTechnique, outTechnique);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The EffectTechnique handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetTechniqueCollection(
    const CNA_EffectTechniqueCollectionHandle handle,
    std::shared_ptr<TechniqueCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectTechniqueCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The EffectTechniqueCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreatePassHandle(
    std::shared_ptr<EffectPass> value,
    std::shared_ptr<void> effectOwnership,
    CNA_EffectPassHandle* const outPass)
{
    const auto resource = std::make_shared<PassResource>(
        PassResource{std::move(value), std::move(effectOwnership)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectPass, resource, outPass);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned EffectPass handle could not be created.");
}

[[nodiscard]] CNA_Result CreatePassCollectionHandle(
    std::shared_ptr<PassCollectionState> state,
    CNA_EffectPassCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<PassCollectionResource>(
        PassCollectionResource{std::move(state)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectPassCollection, resource, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned EffectPassCollection handle could not be created.");
}

[[nodiscard]] CNA_Result CreateTechniqueHandle(
    std::shared_ptr<EffectTechnique> value,
    std::shared_ptr<TechniqueState> state,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    const auto resource = std::make_shared<TechniqueResource>(
        TechniqueResource{std::move(value), std::move(state)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectTechnique, resource, outTechnique);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned EffectTechnique handle could not be created.");
}

[[nodiscard]] CNA_Result CreateTechniqueCollectionHandle(
    std::shared_ptr<TechniqueCollectionState> state,
    CNA_EffectTechniqueCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<TechniqueCollectionResource>(
        TechniqueCollectionResource{std::move(state)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectTechniqueCollection, resource, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned EffectTechniqueCollection handle could not be created.");
}

[[nodiscard]] CNA_Result CreateAnnotationCollectionAlias(
    std::shared_ptr<void> owner,
    EffectAnnotationCollection* const value,
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<AnnotationCollectionResource>(
        AnnotationCollectionResource{
            std::shared_ptr<EffectAnnotationCollection>(std::move(owner), value)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectAnnotationCollection, resource, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The effect annotation-collection view could not be created.");
}

[[nodiscard]] std::shared_ptr<TechniqueState> GetTechniqueElementState(
    const std::shared_ptr<TechniqueCollectionState>& collection,
    EffectTechnique* const technique)
{
    auto [iterator, inserted] = collection->elementStates.try_emplace(technique);
    if (inserted || iterator->second == nullptr) {
        iterator->second = std::make_shared<TechniqueState>();
        iterator->second->owner = collection->owner;
        iterator->second->effectOwnership = collection->effectOwnership;
    }
    return iterator->second;
}

[[nodiscard]] CNA_Result CreateTechniqueElementHandle(
    const std::shared_ptr<TechniqueCollectionState>& collection,
    EffectTechnique* const technique,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CreateTechniqueHandle(
        std::shared_ptr<EffectTechnique>(collection->value, technique),
        GetTechniqueElementState(collection, technique),
        outTechnique);
}

[[nodiscard]] CNA_Result GetEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outEffect)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::Effect, outEffect);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The Effect handle is invalid for this call.");
}

[[nodiscard]] std::shared_ptr<EffectState> GetEffectState(
    const std::shared_ptr<EffectResource>& effect)
{
    return std::static_pointer_cast<EffectState>(effect->adapterState);
}

[[nodiscard]] CNA_Result CreateEffectHandle(
    std::shared_ptr<Effect> value,
    const CNA_Handle parentGame,
    CNA_EffectHandle* const outEffect)
{
    auto state = std::make_shared<EffectState>();
    state->lifetime = std::make_shared<EffectLifetime>();

    state->parameters = std::make_shared<ParameterCollectionState>();
    state->parameters->value = std::shared_ptr<EffectParameterCollection>(
        value, &value->getParametersProperty());
    state->parameters->effectOwnership = state->lifetime;

    state->techniques = std::make_shared<TechniqueCollectionState>();
    state->techniques->value = std::shared_ptr<EffectTechniqueCollection>(
        value, &value->getTechniquesProperty());
    state->techniques->owner = value.get();
    state->techniques->effectOwnership = state->lifetime;

    const auto resource = std::make_shared<EffectResource>(
        EffectResource{std::move(value), parentGame, state});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::Effect, resource, outEffect);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned Effect handle could not be created.");
}

[[nodiscard]] CNA_Result GetShaderEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    ShaderEffect** const outShader)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const shader = dynamic_cast<ShaderEffect*>(effect->value.get());
    if (shader == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The Effect handle does not refer to a ShaderEffect.");
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outShader = shader;
    return CNA_RESULT_SUCCESS;
}

template<typename TGetter>
[[nodiscard]] CNA_Result GetEffectStringByteCount(
    const CNA_EffectHandle handle,
    uint64_t* const outByteCount,
    TGetter&& getter)
{
    if (outByteCount == nullptr) {
        return InvalidArgument("The Effect string byte-count output is null.");
    }
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outByteCount = static_cast<uint64_t>(
        std::forward<TGetter>(getter)(*effect->value).size());
    return CNA_RESULT_SUCCESS;
}

template<typename TGetter>
[[nodiscard]] CNA_Result CopyEffectString(
    const CNA_EffectHandle handle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TGetter&& getter)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The Effect string output buffer is invalid.");
    }
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string& text = std::forward<TGetter>(getter)(*effect->value);
    *outByteCount = static_cast<uint64_t>(text.size());
    if (capacity < text.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete Effect string.");
    }
    if (!text.empty()) {
        std::memcpy(destination, text.data(), text.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyUniformName(
    const CNA_StringView name,
    std::string* const outName)
{
    const CNA_Result result = CopyStringView(name, true, outName);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The shader uniform name is not valid UTF-8 text.");
}

[[nodiscard]] CNA_Result ValidateEffectTextureOwner(
    const EffectResource& effect,
    const CNA_Handle textureParentGame)
{
    if (textureParentGame == effect.parentGame) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        CNA_RESULT_INVALID_ARGUMENT,
        CNA_ERROR_CATEGORY_ARGUMENT,
        "The Effect and texture belong to different graphics devices.");
}

} // namespace

CNA_Result cna_effect_annotation_create(
    const CNA_EffectAnnotationCreateInfo* const createInfo,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotation output handle is null.");
        }
        *outAnnotation = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_EffectAnnotationCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            createInfo->parameter_class > CNA_EFFECT_PARAMETER_CLASS_STRUCT ||
            createInfo->parameter_type > CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE) {
            return InvalidArgument("The EffectAnnotation creation configuration is invalid.");
        }
        std::size_t dataByteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                createInfo->data,
                createInfo->data_count,
                sizeof(float),
                &dataByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation data buffer is invalid.");
        }
        std::string name;
        std::string semantic;
        std::string cachedString;
        if (const CNA_Result result = CopyStringView(createInfo->name, true, &name);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation name is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                createInfo->semantic, true, &semantic);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation semantic is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                createInfo->cached_string, true, &cachedString);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation cached string is not valid UTF-8 text.");
        }
        std::vector<float> data;
        if (dataByteCount != 0U) {
            data.assign(
                createInfo->data,
                createInfo->data + static_cast<std::size_t>(createInfo->data_count));
        }
        const EffectAnnotation native(
            std::move(name),
            std::move(semantic),
            createInfo->row_count,
            createInfo->column_count,
            static_cast<EffectParameterClass>(createInfo->parameter_class),
            static_cast<EffectParameterType>(createInfo->parameter_type),
            std::move(data),
            std::move(cachedString));
        return CreateAnnotationHandle(native, outAnnotation);
    });
}

CNA_Result cna_effect_annotation_destroy(const CNA_EffectAnnotationHandle annotationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(annotationHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotation handle could not be released.");
    });
}

CNA_Result cna_effect_annotation_get_info(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_EffectAnnotationInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr ||
            outInfo->struct_size < sizeof(CNA_EffectAnnotationInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The EffectAnnotation info output structure is invalid.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_EffectAnnotationInfo{
            sizeof(CNA_EffectAnnotationInfo),
            StructureVersion,
            annotation->value->getRowCountProperty(),
            annotation->value->getColumnCountProperty(),
            static_cast<CNA_EffectParameterClass>(
                annotation->value->getParameterClassProperty()),
            static_cast<CNA_EffectParameterType>(
                annotation->value->getParameterTypeProperty())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_name_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_annotation_copy_name(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_annotation_get_semantic_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_annotation_copy_semantic(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_annotation_get_value_boolean(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Boolean output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = annotation->value->GetValueBoolean() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_int32(
    const CNA_EffectAnnotationHandle annotationHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Int32 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(annotation->value->GetValueInt32());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_single(
    const CNA_EffectAnnotationHandle annotationHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Single output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = annotation->value->GetValueSingle();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_string_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_annotation_copy_value_string(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_annotation_get_value_vector2(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector2* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector2 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector2());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_vector3(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector3 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector3());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_vector4(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector4 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector4());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_matrix(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Matrix output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueMatrix());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_create(
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<AnnotationCollectionResource>(
            AnnotationCollectionResource{std::make_shared<EffectAnnotationCollection>()});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::EffectAnnotationCollection, resource, outCollection);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotationCollection handle could not be created.");
    });
}

CNA_Result cna_effect_annotation_collection_destroy(
    const CNA_EffectAnnotationCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotationCollection handle could not be released.");
    });
}

CNA_Result cna_effect_annotation_collection_get_count(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection count output is null.");
        }
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_add(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const CNA_EffectAnnotationHandle annotationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Add(*annotation->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_get_at(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotation output handle is null.");
        }
        *outAnnotation = CNA_INVALID_HANDLE;
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(collection->value->getCountProperty()) ||
            index > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The EffectAnnotationCollection index is outside the collection.");
        }
        return CreateAnnotationHandle(
            (*collection->value)[static_cast<int>(index)], outAnnotation);
    });
}

CNA_Result cna_effect_annotation_collection_find(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outAnnotation = CNA_INVALID_HANDLE;
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation lookup name is not valid UTF-8 text.");
        }
        const EffectAnnotation* const annotation =
            static_cast<const EffectAnnotationCollection&>(*collection->value)[copiedName];
        if (annotation == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreateAnnotationHandle(*annotation, outAnnotation);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}

CNA_Result cna_effect_parameter_create(
    const CNA_EffectParameterCreateInfo* const createInfo,
    CNA_EffectParameterHandle* const outParameter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outParameter == nullptr) {
            return InvalidArgument("The EffectParameter output handle is null.");
        }
        *outParameter = CNA_INVALID_HANDLE;
        std::unique_ptr<EffectParameter> parameter;
        if (const CNA_Result result = CreateNativeParameter(createInfo, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateParameterHandle(
            std::shared_ptr<EffectParameter>(std::move(parameter)),
            std::make_shared<ParameterState>(),
            outParameter);
    });
}

CNA_Result cna_effect_parameter_destroy(const CNA_EffectParameterHandle parameterHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(parameterHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectParameter handle could not be released.");
    });
}

CNA_Result cna_effect_parameter_get_info(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectParameterInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr ||
            outInfo->struct_size < sizeof(CNA_EffectParameterInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The EffectParameter info output structure is invalid.");
        }
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_EffectParameterInfo{
            sizeof(CNA_EffectParameterInfo),
            StructureVersion,
            parameter->value->getRowCountProperty(),
            parameter->value->getColumnCountProperty(),
            static_cast<CNA_EffectParameterClass>(
                parameter->value->getParameterClassProperty()),
            static_cast<CNA_EffectParameterType>(
                parameter->value->getParameterTypeProperty())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_parameter_get_name_byte_count(
    const CNA_EffectParameterHandle parameterHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return parameter->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_parameter_copy_name(
    const CNA_EffectParameterHandle parameterHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return parameter->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_parameter_get_semantic_byte_count(
    const CNA_EffectParameterHandle parameterHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return parameter->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_parameter_copy_semantic(
    const CNA_EffectParameterHandle parameterHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return parameter->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_parameter_get_elements(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectParameter elements output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (parameter->state->elements == nullptr) {
            auto state = std::make_shared<ParameterCollectionState>();
            state->value = std::shared_ptr<EffectParameterCollection>(
                parameter->value, &parameter->value->getElementsProperty());
            state->effectOwnership = parameter->state->effectOwnership;
            parameter->state->elements = std::move(state);
        }
        return CreateParameterCollectionHandle(parameter->state->elements, outCollection);
    });
}

CNA_Result cna_effect_parameter_get_structure_members(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectParameter members output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (parameter->state->members == nullptr) {
            auto state = std::make_shared<ParameterCollectionState>();
            state->value = std::shared_ptr<EffectParameterCollection>(
                parameter->value, &parameter->value->getStructureMembersProperty());
            state->effectOwnership = parameter->state->effectOwnership;
            parameter->state->members = std::move(state);
        }
        return CreateParameterCollectionHandle(parameter->state->members, outCollection);
    });
}

CNA_Result cna_effect_parameter_get_annotations(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectParameter annotations output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<AnnotationCollectionResource>(
            AnnotationCollectionResource{std::shared_ptr<EffectAnnotationCollection>(
                parameter->value, &parameter->value->getAnnotationsProperty())});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::EffectAnnotationCollection, resource, outCollection);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The EffectParameter annotation-collection view could not be created.");
    });
}

CNA_Result cna_effect_parameter_get_value(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    void* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectParameter scalar output is null.");
        }
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        switch (valueType) {
        case CNA_EFFECT_VALUE_BOOLEAN: {
            const CNA_Bool value = parameter->value->GetValueBoolean() ? CNA_TRUE : CNA_FALSE;
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_INT32: {
            const int32_t value = static_cast<int32_t>(parameter->value->GetValueInt32());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_SINGLE: {
            const float value = parameter->value->GetValueSingle();
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_MATRIX: {
            const CNA_Matrix value = ToC(parameter->value->GetValueMatrix());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_MATRIX_TRANSPOSE: {
            const CNA_Matrix value = ToC(parameter->value->GetValueMatrixTranspose());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_QUATERNION: {
            const CNA_Quaternion value = ToC(parameter->value->GetValueQuaternion());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR2: {
            const CNA_Vector2 value = ToC(parameter->value->GetValueVector2());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR3: {
            const CNA_Vector3 value = ToC(parameter->value->GetValueVector3());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR4: {
            const CNA_Vector4 value = ToC(parameter->value->GetValueVector4());
            std::memcpy(outValue, &value, sizeof(value));
            return CNA_RESULT_SUCCESS;
        }
        default:
            return InvalidArgument("The EffectParameter scalar value type is invalid.");
        }
    });
}

CNA_Result cna_effect_parameter_get_values(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const uint64_t requestedCount,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The EffectParameter value-array output is invalid.");
        }
        *outCount = 0U;
        int nativeCount = 0;
        if (const CNA_Result result = RequestedCountToInt(requestedCount, &nativeCount);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        switch (valueType) {
        case CNA_EFFECT_VALUE_BOOLEAN: {
            const std::vector<bool> nativeValues =
                parameter->value->GetValueBooleanArray(nativeCount);
            std::vector<CNA_Bool> values;
            values.reserve(nativeValues.size());
            for (const bool value : nativeValues) {
                values.push_back(value ? CNA_TRUE : CNA_FALSE);
            }
            return CopyValues(values, destination, capacity, outCount);
        }
        case CNA_EFFECT_VALUE_INT32:
            return CopyConvertedValues<int, int32_t>(
                parameter->value->GetValueInt32Array(nativeCount),
                destination,
                capacity,
                outCount,
                [](const int value) { return static_cast<int32_t>(value); });
        case CNA_EFFECT_VALUE_SINGLE:
            return CopyValues(
                parameter->value->GetValueSingleArray(nativeCount),
                destination,
                capacity,
                outCount);
        case CNA_EFFECT_VALUE_MATRIX:
            return CopyConvertedValues<Matrix, CNA_Matrix>(
                parameter->value->GetValueMatrixArray(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Matrix& value) { return ToC(value); });
        case CNA_EFFECT_VALUE_MATRIX_TRANSPOSE:
            return CopyConvertedValues<Matrix, CNA_Matrix>(
                parameter->value->GetValueMatrixTransposeArray(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Matrix& value) { return ToC(value); });
        case CNA_EFFECT_VALUE_QUATERNION:
            return CopyConvertedValues<Quaternion, CNA_Quaternion>(
                parameter->value->GetValueQuaternionArray(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Quaternion& value) { return ToC(value); });
        case CNA_EFFECT_VALUE_VECTOR2:
            return CopyConvertedValues<Vector2, CNA_Vector2>(
                parameter->value->GetValueVector2Array(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Vector2& value) { return ToC(value); });
        case CNA_EFFECT_VALUE_VECTOR3:
            return CopyConvertedValues<Vector3, CNA_Vector3>(
                parameter->value->GetValueVector3Array(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Vector3& value) { return ToC(value); });
        case CNA_EFFECT_VALUE_VECTOR4:
            return CopyConvertedValues<Vector4, CNA_Vector4>(
                parameter->value->GetValueVector4Array(nativeCount),
                destination,
                capacity,
                outCount,
                [](const Vector4& value) { return ToC(value); });
        default:
            return InvalidArgument("The EffectParameter array value type is invalid.");
        }
    });
}

CNA_Result cna_effect_parameter_set_value(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const void* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return InvalidArgument("The EffectParameter scalar input is null.");
        }
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        switch (valueType) {
        case CNA_EFFECT_VALUE_BOOLEAN: {
            CNA_Bool copied = CNA_FALSE;
            LoadValue(value, &copied);
            if (copied != CNA_FALSE && copied != CNA_TRUE) {
                return InvalidArgument("The EffectParameter Boolean value is invalid.");
            }
            parameter->value->SetValue(copied == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_INT32: {
            int32_t copied = 0;
            LoadValue(value, &copied);
            parameter->value->SetValue(static_cast<int>(copied));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_SINGLE: {
            float copied = 0.0F;
            LoadValue(value, &copied);
            parameter->value->SetValue(copied);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_MATRIX:
        case CNA_EFFECT_VALUE_MATRIX_TRANSPOSE: {
            CNA_Matrix copied{};
            LoadValue(value, &copied);
            const Matrix native = ToNative(copied);
            if (valueType == CNA_EFFECT_VALUE_MATRIX) {
                parameter->value->SetValue(native);
            } else {
                parameter->value->SetValueTranspose(native);
            }
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_QUATERNION: {
            CNA_Quaternion copied{};
            LoadValue(value, &copied);
            parameter->value->SetValue(ToNative(copied));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR2: {
            CNA_Vector2 copied{};
            LoadValue(value, &copied);
            parameter->value->SetValue(ToNative(copied));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR3: {
            CNA_Vector3 copied{};
            LoadValue(value, &copied);
            parameter->value->SetValue(ToNative(copied));
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR4: {
            CNA_Vector4 copied{};
            LoadValue(value, &copied);
            parameter->value->SetValue(ToNative(copied));
            return CNA_RESULT_SUCCESS;
        }
        default:
            return InvalidArgument("The EffectParameter scalar value type is invalid.");
        }
    });
}

CNA_Result cna_effect_parameter_set_values(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const void* const values,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        switch (valueType) {
        case CNA_EFFECT_VALUE_BOOLEAN: {
            std::vector<CNA_Bool> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<bool> native;
            native.reserve(copied.size());
            for (const CNA_Bool value : copied) {
                if (value != CNA_FALSE && value != CNA_TRUE) {
                    return InvalidArgument(
                        "The EffectParameter Boolean value array is invalid.");
                }
                native.push_back(value == CNA_TRUE);
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_INT32: {
            std::vector<int32_t> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<int> native;
            native.reserve(copied.size());
            for (const int32_t value : copied) {
                native.push_back(static_cast<int>(value));
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_SINGLE: {
            std::vector<float> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            parameter->value->SetValue(copied);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_MATRIX:
        case CNA_EFFECT_VALUE_MATRIX_TRANSPOSE: {
            std::vector<CNA_Matrix> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<Matrix> native;
            native.reserve(copied.size());
            for (const CNA_Matrix value : copied) {
                native.push_back(ToNative(value));
            }
            if (valueType == CNA_EFFECT_VALUE_MATRIX) {
                parameter->value->SetValue(native);
            } else {
                parameter->value->SetValueTranspose(native);
            }
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_QUATERNION: {
            std::vector<CNA_Quaternion> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<Quaternion> native;
            native.reserve(copied.size());
            for (const CNA_Quaternion value : copied) {
                native.push_back(ToNative(value));
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR2: {
            std::vector<CNA_Vector2> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<Vector2> native;
            native.reserve(copied.size());
            for (const CNA_Vector2 value : copied) {
                native.push_back(ToNative(value));
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR3: {
            std::vector<CNA_Vector3> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<Vector3> native;
            native.reserve(copied.size());
            for (const CNA_Vector3 value : copied) {
                native.push_back(ToNative(value));
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        case CNA_EFFECT_VALUE_VECTOR4: {
            std::vector<CNA_Vector4> copied;
            if (const CNA_Result result = LoadValues(values, count, &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<Vector4> native;
            native.reserve(copied.size());
            for (const CNA_Vector4 value : copied) {
                native.push_back(ToNative(value));
            }
            parameter->value->SetValue(native);
            return CNA_RESULT_SUCCESS;
        }
        default:
            return InvalidArgument("The EffectParameter array value type is invalid.");
        }
    });
}

CNA_Result cna_effect_parameter_get_value_string_byte_count(
    const CNA_EffectParameterHandle parameterHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return parameter->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_parameter_copy_value_string(
    const CNA_EffectParameterHandle parameterHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return parameter->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_parameter_set_value_string(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_StringView value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copied;
        if (const CNA_Result result = CopyStringView(value, true, &copied);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectParameter string value is not valid UTF-8 text.");
        }
        parameter->value->SetValue(copied);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_parameter_get_value_texture(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectTextureType textureType,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The EffectParameter texture output is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        switch (textureType) {
        case CNA_EFFECT_TEXTURE_2D:
            if (parameter->value->GetValueTexture2D() != nullptr) {
                *outTexture = parameter->state->texture2D.handle;
            }
            return CNA_RESULT_SUCCESS;
        case CNA_EFFECT_TEXTURE_3D:
            if (parameter->value->GetValueTexture3D() != nullptr) {
                *outTexture = parameter->state->texture3D.handle;
            }
            return CNA_RESULT_SUCCESS;
        case CNA_EFFECT_TEXTURE_CUBE:
            if (parameter->value->GetValueTextureCube() != nullptr) {
                *outTexture = parameter->state->textureCube.handle;
            }
            return CNA_RESULT_SUCCESS;
        case CNA_EFFECT_TEXTURE_BASE:
            return InvalidArgument(
                "The base Texture EffectParameter overload has no native getter.");
        default:
            return InvalidArgument("The EffectParameter texture type is invalid.");
        }
    });
}

CNA_Result cna_effect_parameter_set_value_texture(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectTextureType textureType,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterResource> parameter;
        if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (textureType > CNA_EFFECT_TEXTURE_CUBE) {
            return InvalidArgument("The EffectParameter texture type is invalid.");
        }
        RetainedTextureSlot* slot = nullptr;
        switch (textureType) {
        case CNA_EFFECT_TEXTURE_BASE:
            slot = &parameter->state->baseTexture;
            break;
        case CNA_EFFECT_TEXTURE_2D:
            slot = &parameter->state->texture2D;
            break;
        case CNA_EFFECT_TEXTURE_3D:
            slot = &parameter->state->texture3D;
            break;
        case CNA_EFFECT_TEXTURE_CUBE:
            slot = &parameter->state->textureCube;
            break;
        default:
            break;
        }
        if (textureHandle == CNA_INVALID_HANDLE) {
            switch (textureType) {
            case CNA_EFFECT_TEXTURE_BASE:
                parameter->value->SetValue(static_cast<Texture*>(nullptr));
                break;
            case CNA_EFFECT_TEXTURE_2D:
                parameter->value->SetValue(static_cast<Texture2D*>(nullptr));
                break;
            case CNA_EFFECT_TEXTURE_3D:
                parameter->value->SetValue(static_cast<Texture3D*>(nullptr));
                break;
            case CNA_EFFECT_TEXTURE_CUBE:
                parameter->value->SetValue(static_cast<TextureCube*>(nullptr));
                break;
            default:
                break;
            }
            slot->Reset();
            return CNA_RESULT_SUCCESS;
        }

        std::shared_ptr<Texture> texture;
        std::shared_ptr<void> owner;
        uint64_t* referenceCount = nullptr;
        switch (textureType) {
        case CNA_EFFECT_TEXTURE_BASE: {
            TextureResourceView view;
            if (const CNA_Result result = GetOwnedTexture(textureHandle, &view);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            texture = std::move(view.value);
            owner = std::move(view.retentionOwner);
            referenceCount = view.activeEffectReferenceCount;
            break;
        }
        case CNA_EFFECT_TEXTURE_2D: {
            std::shared_ptr<CNA::C::Detail::Texture2DResource> resource;
            if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            texture = std::static_pointer_cast<Texture>(resource->value);
            owner = resource;
            referenceCount = &resource->activeEffectReferenceCount;
            break;
        }
        case CNA_EFFECT_TEXTURE_3D: {
            std::shared_ptr<CNA::C::Detail::Texture3DResource> resource;
            if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            texture = std::static_pointer_cast<Texture>(resource->value);
            owner = resource;
            referenceCount = &resource->activeEffectReferenceCount;
            break;
        }
        case CNA_EFFECT_TEXTURE_CUBE: {
            TextureCubeResourceView view;
            if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &view);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            texture = std::static_pointer_cast<Texture>(view.value);
            owner = std::move(view.retentionOwner);
            referenceCount = view.activeEffectReferenceCount;
            break;
        }
        default:
            break;
        }
        if (texture->getIsDisposedProperty()) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "A disposed texture cannot be assigned to an EffectParameter.");
        }
        if (referenceCount == nullptr || *referenceCount == UINT64_MAX) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The EffectParameter texture retention count cannot be increased.");
        }

        switch (textureType) {
        case CNA_EFFECT_TEXTURE_BASE:
            parameter->value->SetValue(texture.get());
            break;
        case CNA_EFFECT_TEXTURE_2D:
            parameter->value->SetValue(static_cast<Texture2D*>(texture.get()));
            break;
        case CNA_EFFECT_TEXTURE_3D:
            parameter->value->SetValue(static_cast<Texture3D*>(texture.get()));
            break;
        case CNA_EFFECT_TEXTURE_CUBE:
            parameter->value->SetValue(static_cast<TextureCube*>(texture.get()));
            break;
        default:
            break;
        }
        slot->Set(textureHandle, std::move(texture), std::move(owner), referenceCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_parameter_collection_create(
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectParameterCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        auto state = std::make_shared<ParameterCollectionState>();
        state->value = std::make_shared<EffectParameterCollection>();
        return CreateParameterCollectionHandle(std::move(state), outCollection);
    });
}

CNA_Result cna_effect_parameter_collection_destroy(
    const CNA_EffectParameterCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectParameterCollection handle could not be released.");
    });
}

CNA_Result cna_effect_parameter_collection_get_count(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The EffectParameterCollection count output is null.");
        }
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->state->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_parameter_collection_add_create(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_EffectParameterCreateInfo* const createInfo,
    CNA_EffectParameterHandle* const outParameter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outParameter == nullptr) {
            return InvalidArgument("The EffectParameter element output handle is null.");
        }
        *outParameter = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::unique_ptr<EffectParameter> parameter;
        if (const CNA_Result result = CreateNativeParameter(createInfo, &parameter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->state->value->Add(std::move(*parameter));
        const int index = collection->state->value->getCountProperty() - 1;
        EffectParameter* const added = &(*collection->state->value)[index];
        return CreateCollectionElementHandle(collection->state, added, outParameter);
    });
}

CNA_Result cna_effect_parameter_collection_get_at(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectParameterHandle* const outParameter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outParameter == nullptr) {
            return InvalidArgument("The EffectParameter element output handle is null.");
        }
        *outParameter = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(collection->state->value->getCountProperty()) ||
            index > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The EffectParameterCollection index is outside the collection.");
        }
        EffectParameter* const parameter =
            &(*collection->state->value)[static_cast<int>(index)];
        return CreateCollectionElementHandle(
            collection->state, parameter, outParameter);
    });
}

CNA_Result cna_effect_parameter_collection_find_name(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectParameterHandle* const outParameter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outParameter == nullptr) {
            return InvalidArgument("The EffectParameterCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outParameter = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectParameter lookup name is not valid UTF-8 text.");
        }
        EffectParameter* const parameter = (*collection->state->value)[copiedName];
        if (parameter == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreateCollectionElementHandle(
            collection->state, parameter, outParameter);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}

CNA_Result cna_effect_parameter_collection_find_semantic(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_StringView semantic,
    CNA_Bool* const outFound,
    CNA_EffectParameterHandle* const outParameter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outParameter == nullptr) {
            return InvalidArgument("The EffectParameterCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outParameter = CNA_INVALID_HANDLE;
        std::shared_ptr<ParameterCollectionResource> collection;
        if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedSemantic;
        if (const CNA_Result result = CopyStringView(semantic, true, &copiedSemantic);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectParameter lookup semantic is not valid UTF-8 text.");
        }
        EffectParameter* const parameter =
            collection->state->value->GetParameterBySemantic(copiedSemantic);
        if (parameter == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreateCollectionElementHandle(
            collection->state, parameter, outParameter);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}

CNA_Result cna_effect_pass_create(
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    CNA_EffectPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return InvalidArgument("The EffectPass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectPass name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreatePassHandle(
            std::make_shared<EffectPass>(nullptr, std::move(copiedName), techniqueIdentity),
            nullptr,
            outPass);
    });
}

CNA_Result cna_effect_pass_destroy(const CNA_EffectPassHandle passHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(passHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(result, ErrorCategoryForResult(result),
                   "The owned EffectPass handle could not be released.");
    });
}

CNA_Result cna_effect_pass_get_name_byte_count(
    const CNA_EffectPassHandle passHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return pass->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_pass_copy_name(
    const CNA_EffectPassHandle passHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return pass->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_pass_get_annotations(
    const CNA_EffectPassHandle passHandle,
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectPass annotations output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateAnnotationCollectionAlias(
            pass->value, &pass->value->getAnnotationsProperty(), outCollection);
    });
}

CNA_Result cna_effect_pass_apply(const CNA_EffectPassHandle passHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        pass->value->Apply();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_pass_collection_create(
    CNA_EffectPassCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectPassCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        auto state = std::make_shared<PassCollectionState>();
        state->value = std::make_shared<EffectPassCollection>();
        return CreatePassCollectionHandle(std::move(state), outCollection);
    });
}

CNA_Result cna_effect_pass_collection_destroy(
    const CNA_EffectPassCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PassCollectionResource> collection;
        if (const CNA_Result result = GetPassCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(result, ErrorCategoryForResult(result),
                   "The owned EffectPassCollection handle could not be released.");
    });
}

CNA_Result cna_effect_pass_collection_get_count(
    const CNA_EffectPassCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The EffectPassCollection count output is null.");
        }
        std::shared_ptr<PassCollectionResource> collection;
        if (const CNA_Result result = GetPassCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->state->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_pass_collection_add_create(
    const CNA_EffectPassCollectionHandle collectionHandle,
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    CNA_EffectPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return InvalidArgument("The EffectPass element output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<PassCollectionResource> collection;
        if (const CNA_Result result = GetPassCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectPass name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->state->value->Add(
            EffectPass(collection->state->owner, std::move(copiedName), techniqueIdentity));
        EffectPass* const pass = &(*collection->state->value)[
            collection->state->value->getCountProperty() - 1];
        return CreatePassHandle(
            std::shared_ptr<EffectPass>(collection->state->value, pass),
            collection->state->effectOwnership,
            outPass);
    });
}

CNA_Result cna_effect_pass_collection_get_at(
    const CNA_EffectPassCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return InvalidArgument("The EffectPass element output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<PassCollectionResource> collection;
        if (const CNA_Result result = GetPassCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(collection->state->value->getCountProperty()) ||
            index > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE,
                "The EffectPassCollection index is outside the collection.");
        }
        EffectPass* const pass = &(*collection->state->value)[static_cast<int>(index)];
        return CreatePassHandle(
            std::shared_ptr<EffectPass>(collection->state->value, pass),
            collection->state->effectOwnership,
            outPass);
    });
}

CNA_Result cna_effect_pass_collection_find(
    const CNA_EffectPassCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outPass == nullptr) {
            return InvalidArgument("The EffectPassCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<PassCollectionResource> collection;
        if (const CNA_Result result = GetPassCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectPass lookup name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        EffectPass* const pass = (*collection->state->value)[copiedName];
        if (pass == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreatePassHandle(
            std::shared_ptr<EffectPass>(collection->state->value, pass),
            collection->state->effectOwnership,
            outPass);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}

CNA_Result cna_effect_technique_create_default(
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        return CreateTechniqueHandle(
            std::make_shared<EffectTechnique>(),
            std::make_shared<TechniqueState>(),
            outTechnique);
    });
}

CNA_Result cna_effect_technique_create_named(
    const CNA_StringView name,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectTechnique name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTechniqueHandle(
            std::make_shared<EffectTechnique>(nullptr, std::move(copiedName)),
            std::make_shared<TechniqueState>(),
            outTechnique);
    });
}

CNA_Result cna_effect_technique_destroy(
    const CNA_EffectTechniqueHandle techniqueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(techniqueHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(result, ErrorCategoryForResult(result),
                   "The owned EffectTechnique handle could not be released.");
    });
}

CNA_Result cna_effect_technique_get_name_byte_count(
    const CNA_EffectTechniqueHandle techniqueHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return technique->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_technique_copy_name(
    const CNA_EffectTechniqueHandle techniqueHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return technique->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_technique_get_identity(
    const CNA_EffectTechniqueHandle techniqueHandle,
    uint64_t* const outIdentity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIdentity == nullptr) {
            return InvalidArgument("The EffectTechnique identity output is null.");
        }
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIdentity = technique->value->getIdInternal();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_technique_get_passes(
    const CNA_EffectTechniqueHandle techniqueHandle,
    CNA_EffectPassCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectTechnique passes output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (technique->state->passes == nullptr) {
            auto state = std::make_shared<PassCollectionState>();
            state->value = std::shared_ptr<EffectPassCollection>(
                technique->value, &technique->value->getPassesProperty());
            state->owner = technique->state->owner;
            state->effectOwnership = technique->state->effectOwnership;
            technique->state->passes = std::move(state);
        }
        return CreatePassCollectionHandle(technique->state->passes, outCollection);
    });
}

CNA_Result cna_effect_technique_get_annotations(
    const CNA_EffectTechniqueHandle techniqueHandle,
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectTechnique annotations output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateAnnotationCollectionAlias(
            technique->value,
            &technique->value->getAnnotationsProperty(),
            outCollection);
    });
}

CNA_Result cna_effect_technique_collection_create(
    CNA_EffectTechniqueCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectTechniqueCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        auto state = std::make_shared<TechniqueCollectionState>();
        state->value = std::make_shared<EffectTechniqueCollection>();
        return CreateTechniqueCollectionHandle(std::move(state), outCollection);
    });
}

CNA_Result cna_effect_technique_collection_destroy(
    const CNA_EffectTechniqueCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(result, ErrorCategoryForResult(result),
                   "The owned EffectTechniqueCollection handle could not be released.");
    });
}

CNA_Result cna_effect_technique_collection_get_count(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The EffectTechniqueCollection count output is null.");
        }
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->state->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_technique_collection_add_default(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique element output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->state->value->Add(EffectTechnique());
        EffectTechnique* const technique = &(*collection->state->value)[
            collection->state->value->getCountProperty() - 1];
        return CreateTechniqueElementHandle(
            collection->state, technique, outTechnique);
    });
}

CNA_Result cna_effect_technique_collection_add_named(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique element output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectTechnique name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->state->value->Add(
            EffectTechnique(nullptr, std::move(copiedName)));
        EffectTechnique* const technique = &(*collection->state->value)[
            collection->state->value->getCountProperty() - 1];
        return CreateTechniqueElementHandle(
            collection->state, technique, outTechnique);
    });
}

CNA_Result cna_effect_technique_collection_get_at(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique element output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(collection->state->value->getCountProperty()) ||
            index > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE,
                "The EffectTechniqueCollection index is outside the collection.");
        }
        EffectTechnique* const technique =
            &(*collection->state->value)[static_cast<int>(index)];
        return CreateTechniqueElementHandle(
            collection->state, technique, outTechnique);
    });
}

CNA_Result cna_effect_technique_collection_find(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outTechnique == nullptr) {
            return InvalidArgument("The EffectTechniqueCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outTechnique = CNA_INVALID_HANDLE;
        std::shared_ptr<TechniqueCollectionResource> collection;
        if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectTechnique lookup name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        EffectTechnique* const technique = (*collection->state->value)[copiedName];
        if (technique == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreateTechniqueElementHandle(
            collection->state, technique, outTechnique);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}

CNA_Result cna_effect_create_empty(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The Effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<CApiEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_effect_create_compiled(
    const CNA_Handle graphicsDeviceHandle,
    const uint8_t* const effectCode,
    const uint64_t effectCodeCount,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The compiled Effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                effectCode, effectCodeCount, sizeof(uint8_t), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The compiled Effect bytecode buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> code(byteCount);
        if (byteCount != 0U) {
            std::memcpy(code.data(), effectCode, byteCount);
        }
        return CreateEffectHandle(
            std::make_shared<CApiEffect>(*graphicsDevice->value, code),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_shader_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView vertexSource,
    const CNA_StringView fragmentSource,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The ShaderEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::string vertex;
        std::string fragment;
        if (const CNA_Result result = CopyStringView(vertexSource, true, &vertex);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The ShaderEffect vertex source is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(fragmentSource, true, &fragment);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The ShaderEffect fragment source is not valid UTF-8 text.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<ShaderEffect>(
                *graphicsDevice->value, std::move(vertex), std::move(fragment)),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_effect_material_create(
    const CNA_EffectHandle cloneSourceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The EffectMaterial output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> source;
        if (const CNA_Result result = GetEffect(cloneSourceHandle, &source);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<EffectMaterial>(*source->value),
            source->parentGame,
            outEffect);
    });
}

CNA_Result cna_sprite_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The SpriteEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<SpriteEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_effect_destroy(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(effectHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned Effect handle could not be released.");
    });
}

CNA_Result cna_effect_clone(
    const CNA_EffectHandle effectHandle,
    CNA_EffectHandle* const outClone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outClone == nullptr) {
            return InvalidArgument("The Effect clone output handle is null.");
        }
        *outClone = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Effect> clone(effect->value->Clone());
        if (clone == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native Effect clone operation returned null.");
        }
        return CreateEffectHandle(std::move(clone), effect->parentGame, outClone);
    });
}

CNA_Result cna_effect_dispose(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_apply(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->value->Apply();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_get_parameters(
    const CNA_EffectHandle effectHandle,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The Effect parameter-collection output is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateParameterCollectionHandle(
            GetEffectState(effect)->parameters, outCollection);
    });
}

CNA_Result cna_effect_get_techniques(
    const CNA_EffectHandle effectHandle,
    CNA_EffectTechniqueCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The Effect technique-collection output is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTechniqueCollectionHandle(
            GetEffectState(effect)->techniques, outCollection);
    });
}

CNA_Result cna_effect_get_current_technique(
    const CNA_EffectHandle effectHandle,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The Effect current-technique output is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        EffectTechnique* const technique =
            effect->value->getCurrentTechniqueProperty();
        if (technique == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        return CreateTechniqueElementHandle(
            GetEffectState(effect)->techniques, technique, outTechnique);
    });
}

CNA_Result cna_effect_set_current_technique(
    const CNA_EffectHandle effectHandle,
    const CNA_EffectTechniqueHandle techniqueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (techniqueHandle == CNA_INVALID_HANDLE) {
            effect->value->setCurrentTechniqueProperty(nullptr);
            return CNA_RESULT_SUCCESS;
        }
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (technique->state->owner != effect->value.get()) {
            return InvalidArgument(
                "The selected technique does not belong to this Effect.");
        }
        effect->value->setCurrentTechniqueProperty(technique->value.get());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_get_graphics_device(
    const CNA_EffectHandle effectHandle,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGraphicsDevice == nullptr) {
            return InvalidArgument("The Effect graphics-device output is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA_Handle borrowed = CNA_INVALID_HANDLE;
        if (const CNA_Result result = BorrowGameGraphicsDevice(
                effect->parentGame, &borrowed);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        const CNA_Result lookupResult = GetRuntimeHandles().Get(
            borrowed, ObjectKind::GraphicsDevice, &graphicsDevice);
        if (lookupResult != CNA_RESULT_SUCCESS ||
            graphicsDevice->value != &effect->value->getGraphicsDeviceInternal()) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The Effect and borrowed graphics-device owners disagree.");
        }
        *outGraphicsDevice = borrowed;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_get_type_name_byte_count(
    const CNA_EffectHandle effectHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetEffectStringByteCount(
            effectHandle, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetTypeName(); });
    });
}

CNA_Result cna_effect_copy_type_name(
    const CNA_EffectHandle effectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyEffectString(
            effectHandle, destination, capacity, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetTypeName(); });
    });
}

CNA_Result cna_effect_get_vertex_source_byte_count(
    const CNA_EffectHandle effectHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetEffectStringByteCount(
            effectHandle, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetVertexSource(); });
    });
}

CNA_Result cna_effect_copy_vertex_source(
    const CNA_EffectHandle effectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyEffectString(
            effectHandle, destination, capacity, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetVertexSource(); });
    });
}

CNA_Result cna_effect_get_fragment_source_byte_count(
    const CNA_EffectHandle effectHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetEffectStringByteCount(
            effectHandle, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetFragmentSource(); });
    });
}

CNA_Result cna_effect_copy_fragment_source(
    const CNA_EffectHandle effectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyEffectString(
            effectHandle, destination, capacity, outByteCount,
            [](Effect& effect) -> const std::string& { return effect.GetFragmentSource(); });
    });
}

CNA_Result cna_effect_has_renderer(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasRenderer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasRenderer == nullptr) {
            return InvalidArgument("The Effect renderer-state output is null.");
        }
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasRenderer = effect->value->GetEffectRendererPtr() != nullptr
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_is_exact_stock_sprite_effect(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outIsExact)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsExact == nullptr) {
            return InvalidArgument("The stock SpriteEffect identity output is null.");
        }
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsExact = effect->value->IsExactStockSpriteEffectEXT()
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_is_valid(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outIsValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsValid == nullptr) {
            return InvalidArgument("The ShaderEffect validity output is null.");
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsValid = shader->IsEffectValid() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_has_renderer(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasRenderer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasRenderer == nullptr) {
            return InvalidArgument("The ShaderEffect renderer-state output is null.");
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasRenderer = shader->HasRenderer() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_matrix(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const float matrix[16] = {
            value.m11, value.m12, value.m13, value.m14,
            value.m21, value.m22, value.m23, value.m24,
            value.m31, value.m32, value.m33, value.m34,
            value.m41, value.m42, value.m43, value.m44};
        shader->SetUniformMat4(copiedName.c_str(), matrix);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_vector4(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Vector4 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformVec4(copiedName.c_str(), value.x, value.y, value.z, value.w);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_vector3(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformVec3(copiedName.c_str(), value.x, value.y, value.z);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_vector2(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Vector2 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformVec2(copiedName.c_str(), value.x, value.y);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_float(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformFloat(copiedName.c_str(), value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_int32(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformInt(copiedName.c_str(), value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_float_array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const values,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                values, count, sizeof(float), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The shader float-uniform array is invalid.");
        }
        int nativeCount = 0;
        if (const CNA_Result result = RequestedCountToInt(count, &nativeCount);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformFloatArray(copiedName.c_str(), values, nativeCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_vector2_array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Vector2* const values,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                values, count, sizeof(CNA_Vector2), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The shader vec2-uniform array is invalid.");
        }
        int nativeCount = 0;
        if (const CNA_Result result = RequestedCountToInt(count, &nativeCount);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyUniformName(name, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<float> copiedValues;
        copiedValues.reserve(static_cast<std::size_t>(count) * 2U);
        for (uint64_t index = 0U; index < count; ++index) {
            copiedValues.push_back(values[index].x);
            copiedValues.push_back(values[index].y);
        }
        shader->SetUniformVec2Array(
            copiedName.c_str(), copiedValues.data(), nativeCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_texture2d(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (unit < 0) {
            return InvalidArgument("The ShaderEffect texture unit is negative.");
        }
        std::shared_ptr<EffectResource> effect;
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, &effect, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CNA::C::Detail::Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateEffectTextureOwner(
                *effect, texture->parentGame);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetTexture(unit, *texture->value);
        GetEffectState(effect)->lifetime->shaderTextures[unit].Set(
            textureHandle,
            std::static_pointer_cast<Texture>(texture->value),
            texture,
            &texture->activeEffectReferenceCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_texture_cube(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (unit < 0) {
            return InvalidArgument("The ShaderEffect texture unit is negative.");
        }
        std::shared_ptr<EffectResource> effect;
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, &effect, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateEffectTextureOwner(
                *effect, texture.parentGame);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetTexture(unit, *texture.value);
        GetEffectState(effect)->lifetime->shaderTextures[unit].Set(
            textureHandle,
            std::static_pointer_cast<Texture>(texture.value),
            std::move(texture.retentionOwner),
            texture.activeEffectReferenceCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_texture3d(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (unit < 0) {
            return InvalidArgument("The ShaderEffect texture unit is negative.");
        }
        std::shared_ptr<EffectResource> effect;
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, &effect, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CNA::C::Detail::Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateEffectTextureOwner(
                *effect, texture->parentGame);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetTexture(unit, *texture->value);
        GetEffectState(effect)->lifetime->shaderTextures[unit].Set(
            textureHandle,
            std::static_pointer_cast<Texture>(texture->value),
            texture,
            &texture->activeEffectReferenceCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_get_world(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ShaderEffect world-matrix output is null.");
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(shader->getWorldProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_world(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->setWorldProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_get_view(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ShaderEffect view-matrix output is null.");
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(shader->getViewProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_view(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->setViewProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_get_projection(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ShaderEffect projection-matrix output is null.");
        }
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(shader->getProjectionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_projection(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->setProjectionProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}
