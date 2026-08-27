// SPDX-License-Identifier: MS-PL

#include "CNA/C/effects.h"
#include "CNA/C/graphics_ext.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <array>
#include <any>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
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
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
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
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::ColorMatrixEffect;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IEffectFog;
using Microsoft::Xna::Framework::Graphics::IEffectLights;
using Microsoft::Xna::Framework::Graphics::IEffectMatrices;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::SpriteEffect;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

constexpr uint32_t StructureVersion = UINT32_C(1);

/// CABI-13: the owner is captured at construction because the count has to be undone against the
/// same owner it was taken against -- an effect on a caller-created device never counted, and must
/// not decrement a game's tally when it goes away.
class EffectOwnership final {
public:
    explicit EffectOwnership(const CNA_Handle owner)
        : owner_(owner)
    {
        AddOwnedGraphicsResourceFor(owner_);
    }

    EffectOwnership(const EffectOwnership&) = delete;
    EffectOwnership& operator=(const EffectOwnership&) = delete;

    ~EffectOwnership()
    {
        RemoveOwnedGraphicsResourceFor(owner_);
    }

private:
    CNA_Handle owner_;
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

    // CBIND-052B: this class overrode Clone() and OnApply() because both were pure virtual and
    // there was nothing to inherit. Both are ordinary virtuals now, and the inherited Clone() is
    // the one that matters: it clones a compiled effect's runtime and copies its parameter values,
    // where the override returned a fresh empty effect and silently dropped both. OnApply()'s base
    // is the same no-op the override was, so what is left here is the two constructors.
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

struct DirectionalLightResource final {
    std::shared_ptr<DirectionalLight> value;
    std::shared_ptr<void> effectOwnership;
};

struct EffectLifetime final {
    /// EffectOwnership is deliberately non-copyable, so the owner is threaded through a
    /// constructor rather than an aggregate initialiser.
    explicit EffectLifetime(const CNA_Handle owner)
        : ownership(owner)
    {
    }

    EffectOwnership ownership;
    std::unordered_map<int, RetainedTextureSlot> shaderTextures;
    RetainedTextureSlot basicTexture;
    RetainedTextureSlot stockTexture0;
    RetainedTextureSlot stockTexture1;
    RetainedTextureSlot environmentMap;
    std::array<RetainedTextureSlot, CNA_PBR_TEXTURE_MAXIMUM + 1U> pbrTextures;
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

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
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
    CNA_EffectHandle* const outEffect,
    const bool disposeAllowed = true)
{
    auto state = std::make_shared<EffectState>();
    state->lifetime = std::make_shared<EffectLifetime>(parentGame);

    state->parameters = std::make_shared<ParameterCollectionState>();
    state->parameters->value = std::shared_ptr<EffectParameterCollection>(
        value, &value->getParametersProperty());
    state->parameters->effectOwnership = state->lifetime;

    state->techniques = std::make_shared<TechniqueCollectionState>();
    state->techniques->value = std::shared_ptr<EffectTechniqueCollection>(
        value, &value->getTechniquesProperty());
    state->techniques->owner = value.get();
    state->techniques->effectOwnership = state->lifetime;

    const auto resource = std::make_shared<EffectResource>(EffectResource{
        std::move(value), parentGame, state, 0U, disposeAllowed});
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

[[nodiscard]] CNA_Result GetBasicEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    BasicEffect** const outBasic)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const basic = dynamic_cast<BasicEffect*>(effect->value.get());
    if (basic == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The Effect handle does not refer to a BasicEffect.");
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outBasic = basic;
    return CNA_RESULT_SUCCESS;
}

template<typename TEffect>
[[nodiscard]] CNA_Result GetStockEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    TEffect** const outStockEffect,
    const char* const typeName)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const stockEffect = dynamic_cast<TEffect*>(effect->value.get());
    if (stockEffect == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            std::string("The Effect handle does not refer to a ") + typeName + ".");
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outStockEffect = stockEffect;
    return CNA_RESULT_SUCCESS;
}

struct PbrEffectView final {
    std::shared_ptr<EffectResource> resource;
    PbrEffect* pbr = nullptr;
    SkinnedPbrEffect* skinned = nullptr;
};

[[nodiscard]] CNA_Result GetPbrEffect(
    const CNA_EffectHandle handle,
    PbrEffectView* const outView)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    PbrEffect* const pbr = dynamic_cast<PbrEffect*>(effect->value.get());
    SkinnedPbrEffect* const skinned =
        dynamic_cast<SkinnedPbrEffect*>(effect->value.get());
    if (pbr == nullptr && skinned == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The Effect handle does not refer to a PbrEffect or SkinnedPbrEffect.");
    }
    *outView = PbrEffectView{std::move(effect), pbr, skinned};
    return CNA_RESULT_SUCCESS;
}

template<typename TInterface>
[[nodiscard]] CNA_Result GetEffectInterface(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    TInterface** const outInterface,
    const char* const failureMessage)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const interfaceValue = dynamic_cast<TInterface*>(effect->value.get());
    if (interfaceValue == nullptr) {
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            failureMessage);
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outInterface = interfaceValue;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetDirectionalLight(
    const CNA_DirectionalLightHandle handle,
    std::shared_ptr<DirectionalLightResource>* const outLight)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::DirectionalLight, outLight);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The DirectionalLight handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateDirectionalLightHandle(
    std::shared_ptr<DirectionalLight> value,
    std::shared_ptr<void> effectOwnership,
    CNA_DirectionalLightHandle* const outLight)
{
    const auto resource = std::make_shared<DirectionalLightResource>(
        DirectionalLightResource{std::move(value), std::move(effectOwnership)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::DirectionalLight, resource, outLight);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The owned DirectionalLight handle could not be created.");
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

[[nodiscard]] CNA_Result GetRetainedEffectTexture(
    const RetainedTextureSlot& retained,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture,
    const char* const nullMessage)
{
    if (outHasTexture == nullptr || outTexture == nullptr) {
        return InvalidArgument(nullMessage);
    }
    *outHasTexture = retained.handle == CNA_INVALID_HANDLE ? CNA_FALSE : CNA_TRUE;
    *outTexture = retained.handle;
    return CNA_RESULT_SUCCESS;
}

template<typename TSetter>
[[nodiscard]] CNA_Result SetRetainedEffectTexture2D(
    const std::shared_ptr<EffectResource>& effect,
    RetainedTextureSlot& retained,
    const CNA_Handle textureHandle,
    TSetter&& setter)
{
    if (textureHandle == CNA_INVALID_HANDLE) {
        std::forward<TSetter>(setter)(std::shared_ptr<Texture2D>{});
        retained.Reset();
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<CNA::C::Detail::Texture2DResource> texture;
    if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEffectTextureOwner(*effect, texture->parentGame);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (texture->value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A disposed Texture2D cannot be assigned to a stock effect.");
    }
    if (texture->activeEffectReferenceCount == UINT64_MAX) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The Texture2D effect-reference count cannot be incremented.");
    }
    std::forward<TSetter>(setter)(texture->value);
    retained.Set(
        textureHandle,
        std::static_pointer_cast<Texture>(texture->value),
        texture,
        &texture->activeEffectReferenceCount);
    return CNA_RESULT_SUCCESS;
}

template<typename TSetter>
[[nodiscard]] CNA_Result SetRetainedEffectTextureCube(
    const std::shared_ptr<EffectResource>& effect,
    RetainedTextureSlot& retained,
    const CNA_Handle textureHandle,
    TSetter&& setter)
{
    if (textureHandle == CNA_INVALID_HANDLE) {
        std::forward<TSetter>(setter)(std::shared_ptr<TextureCube>{});
        retained.Reset();
        return CNA_RESULT_SUCCESS;
    }
    TextureCubeResourceView texture;
    if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEffectTextureOwner(*effect, texture.parentGame);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (texture.value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A disposed TextureCube cannot be assigned to a stock effect.");
    }
    if (texture.activeEffectReferenceCount == nullptr ||
        *texture.activeEffectReferenceCount == UINT64_MAX) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The TextureCube effect-reference count cannot be incremented.");
    }
    std::forward<TSetter>(setter)(texture.value);
    retained.Set(
        textureHandle,
        std::static_pointer_cast<Texture>(texture.value),
        std::move(texture.retentionOwner),
        texture.activeEffectReferenceCount);
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result CreateBorrowedEffect(
    std::shared_ptr<Effect> effect,
    const CNA_Handle parentGame,
    CNA_Handle* const outEffect)
{
    return CreateEffectHandle(
        std::move(effect), parentGame, outEffect, false);
}

} // namespace CNA::C::Detail

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

CNA_Result cna_effect_pass_create_indexed_ext(
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    const uint32_t passIndex,
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
            std::make_shared<EffectPass>(
                nullptr, std::move(copiedName), techniqueIdentity, passIndex),
            nullptr,
            outPass);
    });
}

CNA_Result cna_effect_pass_get_index_ext(
    const CNA_EffectPassHandle passHandle,
    uint32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidArgument("The EffectPass index output is null.");
        }
        std::shared_ptr<PassResource> pass;
        if (const CNA_Result result = GetPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = pass->value->getIndexInternal();
        return CNA_RESULT_SUCCESS;
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

CNA_Result cna_effect_technique_create_reflected_ext(
    const CNA_StringView name,
    const uint32_t techniqueIndex,
    const CNA_Bool addDefaultPass,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTechnique == nullptr) {
            return InvalidArgument("The EffectTechnique output handle is null.");
        }
        *outTechnique = CNA_INVALID_HANDLE;
        if (addDefaultPass != CNA_FALSE && addDefaultPass != CNA_TRUE) {
            return InvalidArgument("The default-pass flag is not a CNA_Bool value.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyEffectName(
                name, &copiedName, "The EffectTechnique name is not valid UTF-8 text.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTechniqueHandle(
            std::make_shared<EffectTechnique>(
                nullptr, std::move(copiedName), techniqueIndex, addDefaultPass == CNA_TRUE),
            std::make_shared<TechniqueState>(),
            outTechnique);
    });
}

CNA_Result cna_effect_technique_get_index_ext(
    const CNA_EffectTechniqueHandle techniqueHandle,
    uint32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidArgument("The EffectTechnique index output is null.");
        }
        std::shared_ptr<TechniqueResource> technique;
        if (const CNA_Result result = GetTechnique(techniqueHandle, &technique);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = technique->value->getIndexInternal();
        return CNA_RESULT_SUCCESS;
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

CNA_Result cna_content_manager_load_effect(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The loaded Effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::string assetNameCopy;
        if (const CNA_Result result = CopyStringView(assetName, true, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The content asset name is not valid UTF-8.");
        }
        if (assetNameCopy.empty()) {
            return InvalidArgument("The content asset name must not be empty.");
        }
        CNA::C::Detail::BorrowedContentManager contentManager;
        if (const CNA_Result result = CNA::C::Detail::BorrowContentManager(
                contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        try {
            std::shared_ptr<Effect> loaded =
                contentManager.value->Load<std::shared_ptr<Effect>>(assetNameCopy);
            if (loaded == nullptr) {
                return Fail(
                    CNA_RESULT_IO,
                    CNA_ERROR_CATEGORY_IO,
                    "The Effect asset loaded as a null effect.");
            }
            return CreateEffectHandle(
                std::move(loaded), contentManager.parentGame, outEffect);
        } catch (const Microsoft::Xna::Framework::Content::ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // The asset's root reader produced something that is not an Effect. Reporting the
            // mismatch beats the exception barrier's catch-all calling it an internal fault.
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader did not produce an Effect.");
        }
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
        // CBIND-075: an effect with no source at all is the caller's mistake, and renderers
        // disagreed about it -- one threw, which the barrier reported as CNA_RESULT_INTERNAL and so
        // blamed CNA for the caller's input, and another accepted it and handed back an effect that
        // could never draw. Refused here so the answer is the same whichever renderer is active.
        if (vertex.empty() && fragment.empty()) {
            return InvalidArgument(
                "A ShaderEffect needs source: the vertex and fragment sources are both empty.");
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
        if (effect->activeModelReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Effect is retained by a ModelMeshPart.");
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
        if (const CNA_Result result = CreateEffectHandle(
                std::move(clone), effect->parentGame, outClone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EffectResource> clonedEffect;
        if (const CNA_Result result = GetEffect(*outClone, &clonedEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::shared_ptr<EffectLifetime> sourceLifetime =
            GetEffectState(effect)->lifetime;
        const std::shared_ptr<EffectLifetime> cloneLifetime =
            GetEffectState(clonedEffect)->lifetime;
        const auto copyRetained = [](const RetainedTextureSlot& source,
                                     RetainedTextureSlot& destination) {
            if (source.handle != CNA_INVALID_HANDLE) {
                destination.Set(
                    source.handle,
                    source.value,
                    source.owner,
                    source.referenceCount);
            }
        };
        copyRetained(sourceLifetime->basicTexture, cloneLifetime->basicTexture);
        copyRetained(sourceLifetime->stockTexture0, cloneLifetime->stockTexture0);
        copyRetained(sourceLifetime->stockTexture1, cloneLifetime->stockTexture1);
        copyRetained(sourceLifetime->environmentMap, cloneLifetime->environmentMap);
        for (std::size_t index = 0U; index < sourceLifetime->pbrTextures.size(); ++index) {
            copyRetained(
                sourceLifetime->pbrTextures[index],
                cloneLifetime->pbrTextures[index]);
        }
        return CNA_RESULT_SUCCESS;
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
        if (effect->activeModelReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Effect is retained by a ModelMeshPart.");
        }
        if (!effect->disposeAllowed) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Effect is a borrowed factory-owned view and cannot be disposed.");
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

CNA_Result cna_effect_get_is_compiled_ext(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outIsCompiled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsCompiled == nullptr) {
            return InvalidArgument("The Effect compiled-runtime output is null.");
        }
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The runtime itself is renderer-owned implementation, so its presence is what crosses.
        *outIsCompiled = effect->value->GetCompiledRuntimePtr() != nullptr ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
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

CNA_Result cna_shader_effect_declare_uniform_block_ext(
    const CNA_EffectHandle effectHandle,
    const int32_t blockSizeBytes,
    const CNA_StringView* const names,
    const int32_t* const offsets,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (blockSizeBytes < 0) {
            return InvalidArgument("The uniform block size must not be negative.");
        }
        std::size_t nameBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                names, count, sizeof(CNA_StringView), &nameBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The uniform block member-name array is invalid.");
        }
        std::size_t offsetBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                offsets, count, sizeof(int32_t), &offsetBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The uniform block member-offset array is invalid.");
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
        // The canonical signature takes `const char* const*`, so the views must become owned
        // NUL-terminated strings first -- a CNA_StringView carries a length, not a terminator, and
        // may point into the middle of a larger buffer.
        std::vector<std::string> copiedNames;
        std::vector<const char*> namePointers;
        std::vector<int> nativeOffsets;
        copiedNames.reserve(static_cast<std::size_t>(nativeCount));
        namePointers.reserve(static_cast<std::size_t>(nativeCount));
        nativeOffsets.reserve(static_cast<std::size_t>(nativeCount));
        for (int index = 0; index < nativeCount; ++index) {
            std::string copied;
            if (const CNA_Result result = CopyUniformName(names[index], &copied);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            copiedNames.push_back(std::move(copied));
            nativeOffsets.push_back(static_cast<int>(offsets[index]));
        }
        for (const std::string& name : copiedNames) {
            namePointers.push_back(name.c_str());
        }
        shader->DeclareUniformBlockEXT(
            static_cast<int>(blockSizeBytes),
            nativeCount == 0 ? nullptr : namePointers.data(),
            nativeCount == 0 ? nullptr : nativeOffsets.data(),
            nativeCount);
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

CNA_Result cna_directional_light_create(
    CNA_DirectionalLightHandle* const outLight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLight == nullptr) {
            return InvalidArgument("The DirectionalLight output handle is null.");
        }
        *outLight = CNA_INVALID_HANDLE;
        return CreateDirectionalLightHandle(
            std::make_shared<DirectionalLight>(), nullptr, outLight);
    });
}

CNA_Result cna_directional_light_destroy(
    const CNA_DirectionalLightHandle lightHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(lightHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned DirectionalLight handle could not be released.");
    });
}

CNA_Result cna_directional_light_get_diffuse_color(
    const CNA_DirectionalLightHandle lightHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DirectionalLight diffuse-color output is null.");
        }
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(light->value->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_set_diffuse_color(
    const CNA_DirectionalLightHandle lightHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        light->value->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_get_direction(
    const CNA_DirectionalLightHandle lightHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DirectionalLight direction output is null.");
        }
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(light->value->getDirectionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_set_direction(
    const CNA_DirectionalLightHandle lightHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        light->value->setDirectionProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_get_specular_color(
    const CNA_DirectionalLightHandle lightHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DirectionalLight specular-color output is null.");
        }
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(light->value->getSpecularColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_set_specular_color(
    const CNA_DirectionalLightHandle lightHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        light->value->setSpecularColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_get_enabled(
    const CNA_DirectionalLightHandle lightHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DirectionalLight enabled output is null.");
        }
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = light->value->getEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_directional_light_set_enabled(
    const CNA_DirectionalLightHandle lightHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The DirectionalLight enabled value is not a CNA_Bool.");
        }
        std::shared_ptr<DirectionalLightResource> light;
        if (const CNA_Result result = GetDirectionalLight(lightHandle, &light);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        light->value->setEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The BasicEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<BasicEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_effect_matrices_get_world(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect world-matrix output is null.");
        }
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(matrices->getWorldProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_matrices_set_world(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        matrices->setWorldProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_matrices_get_view(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect view-matrix output is null.");
        }
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(matrices->getViewProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_matrices_set_view(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        matrices->setViewProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_matrices_get_projection(
    const CNA_EffectHandle effectHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect projection-matrix output is null.");
        }
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(matrices->getProjectionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_matrices_set_projection(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectMatrices* matrices = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &matrices,
                "The Effect does not implement IEffectMatrices.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        matrices->setProjectionProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_get_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect fog-color output is null.");
        }
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(fog->getFogColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_set_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        fog->setFogColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_get_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect fog-enabled output is null.");
        }
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = fog->getFogEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_set_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The fog-enabled value is not a CNA_Bool.");
        }
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        fog->setFogEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_get_start(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect fog-start output is null.");
        }
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = fog->getFogStartProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_set_start(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        fog->setFogStartProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_get_end(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect fog-end output is null.");
        }
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = fog->getFogEndProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_fog_set_end(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectFog* fog = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &fog, "The Effect does not implement IEffectFog.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        fog->setFogEndProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_lights_get_ambient_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect ambient-light output is null.");
        }
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(lights->getAmbientLightColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_lights_set_ambient_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        lights->setAmbientLightColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_lights_get_directional_light(
    const CNA_EffectHandle effectHandle,
    const uint32_t index,
    CNA_DirectionalLightHandle* const outLight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLight == nullptr) {
            return InvalidArgument("The effect directional-light output is null.");
        }
        *outLight = CNA_INVALID_HANDLE;
        if (index > 2U) {
            return InvalidArgument("The directional-light index is outside zero through two.");
        }
        std::shared_ptr<EffectResource> effect;
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, &effect, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        DirectionalLight* light = nullptr;
        if (index == 0U) {
            light = &lights->getDirectionalLight0Property();
        } else if (index == 1U) {
            light = &lights->getDirectionalLight1Property();
        } else {
            light = &lights->getDirectionalLight2Property();
        }
        return CreateDirectionalLightHandle(
            std::shared_ptr<DirectionalLight>(effect->value, light),
            GetEffectState(effect)->lifetime,
            outLight);
    });
}

CNA_Result cna_effect_lights_get_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The effect lighting-enabled output is null.");
        }
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = lights->getLightingEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_lights_set_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The lighting-enabled value is not a CNA_Bool.");
        }
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (value == CNA_FALSE &&
            (dynamic_cast<EnvironmentMapEffect*>(lights) != nullptr ||
             dynamic_cast<SkinnedEffect*>(lights) != nullptr ||
             dynamic_cast<PbrEffect*>(lights) != nullptr ||
             dynamic_cast<SkinnedPbrEffect*>(lights) != nullptr)) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "This stock effect's lighting cannot be disabled.");
        }
        lights->setLightingEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_lights_enable_default(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        IEffectLights* lights = nullptr;
        if (const CNA_Result result = GetEffectInterface(
                effectHandle, nullptr, &lights,
                "The Effect does not implement IEffectLights.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        lights->EnableDefaultLighting();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect vertex-color output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = basic->VertexColorEnabled ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The vertex-color value is not a CNA_Bool.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->VertexColorEnabled = value == CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_prefer_per_pixel_lighting(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect per-pixel-lighting output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = basic->getPreferPerPixelLightingProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_prefer_per_pixel_lighting(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The per-pixel-lighting value is not a CNA_Bool.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setPreferPerPixelLightingProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect diffuse-color output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(basic->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_emissive_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect emissive-color output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(basic->getEmissiveColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_emissive_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setEmissiveColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_specular_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect specular-color output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(basic->getSpecularColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_specular_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setSpecularColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_specular_power(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect specular-power output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = basic->getSpecularPowerProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_specular_power(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setSpecularPowerProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect alpha output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = basic->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setAlphaProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_texture_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The BasicEffect texture-enabled output is null.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = basic->getTextureEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_texture_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The texture-enabled value is not a CNA_Bool.");
        }
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, nullptr, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        basic->setTextureEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("The BasicEffect texture outputs are null.");
        }
        *outHasTexture = CNA_FALSE;
        *outTexture = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectResource> effect;
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, &effect, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)basic;
        const RetainedTextureSlot& texture =
            GetEffectState(effect)->lifetime->basicTexture;
        if (texture.handle != CNA_INVALID_HANDLE) {
            *outHasTexture = CNA_TRUE;
            *outTexture = texture.handle;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_basic_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        BasicEffect* basic = nullptr;
        if (const CNA_Result result = GetBasicEffect(effectHandle, &effect, &basic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        RetainedTextureSlot& retained =
            GetEffectState(effect)->lifetime->basicTexture;
        if (textureHandle == CNA_INVALID_HANDLE) {
            basic->SetOwnedTexture(nullptr);
            retained.Reset();
            return CNA_RESULT_SUCCESS;
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
        basic->SetOwnedTexture(texture->value);
        retained.Set(
            textureHandle,
            std::static_pointer_cast<Texture>(texture->value),
            texture,
            &texture->activeEffectReferenceCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The AlphaTestEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<AlphaTestEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_alpha_test_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The AlphaTestEffect diffuse-color output is null.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(alphaTest->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        alphaTest->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The AlphaTestEffect alpha output is null.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = alphaTest->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        alphaTest->setAlphaProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("The AlphaTestEffect texture outputs are null.");
        }
        std::shared_ptr<EffectResource> effect;
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)alphaTest;
        return GetRetainedEffectTexture(
            GetEffectState(effect)->lifetime->stockTexture0,
            outHasTexture,
            outTexture,
            "The AlphaTestEffect texture outputs are null.");
    });
}

CNA_Result cna_alpha_test_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetRetainedEffectTexture2D(
            effect,
            GetEffectState(effect)->lifetime->stockTexture0,
            textureHandle,
            [alphaTest](std::shared_ptr<Texture2D> texture) {
                alphaTest->SetOwnedTexture(std::move(texture));
            });
    });
}

CNA_Result cna_alpha_test_effect_get_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The AlphaTestEffect vertex-color output is null.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = alphaTest->getVertexColorEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_set_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The AlphaTestEffect vertex-color value is not a CNA_Bool.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        alphaTest->setVertexColorEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_get_alpha_function(
    const CNA_EffectHandle effectHandle,
    CNA_CompareFunction* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The AlphaTestEffect comparison output is null.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<CNA_CompareFunction>(
            alphaTest->getAlphaFunctionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_set_alpha_function(
    const CNA_EffectHandle effectHandle,
    const CNA_CompareFunction value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value > CNA_COMPARE_NOT_EQUAL) {
            return InvalidArgument("The AlphaTestEffect comparison function is invalid.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        alphaTest->setAlphaFunctionProperty(static_cast<CompareFunction>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_get_reference_alpha(
    const CNA_EffectHandle effectHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The AlphaTestEffect reference-alpha output is null.");
        }
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(alphaTest->getReferenceAlphaProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_alpha_test_effect_set_reference_alpha(
    const CNA_EffectHandle effectHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AlphaTestEffect* alphaTest = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &alphaTest, "AlphaTestEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        alphaTest->setReferenceAlphaProperty(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The DualTextureEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<DualTextureEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_dual_texture_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DualTextureEffect diffuse-color output is null.");
        }
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(dualTexture->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dualTexture->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DualTextureEffect alpha output is null.");
        }
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = dualTexture->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dualTexture->setAlphaProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    const uint32_t textureIndex,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("The DualTextureEffect texture outputs are null.");
        }
        *outHasTexture = CNA_FALSE;
        *outTexture = CNA_INVALID_HANDLE;
        if (textureIndex > 1U) {
            return InvalidArgument("The DualTextureEffect texture index is not zero or one.");
        }
        std::shared_ptr<EffectResource> effect;
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)dualTexture;
        const std::shared_ptr<EffectLifetime> lifetime = GetEffectState(effect)->lifetime;
        return GetRetainedEffectTexture(
            textureIndex == 0U ? lifetime->stockTexture0 : lifetime->stockTexture1,
            outHasTexture,
            outTexture,
            "The DualTextureEffect texture outputs are null.");
    });
}

CNA_Result cna_dual_texture_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const uint32_t textureIndex,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (textureIndex > 1U) {
            return InvalidArgument("The DualTextureEffect texture index is not zero or one.");
        }
        std::shared_ptr<EffectResource> effect;
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::shared_ptr<EffectLifetime> lifetime = GetEffectState(effect)->lifetime;
        RetainedTextureSlot& retained = textureIndex == 0U
            ? lifetime->stockTexture0
            : lifetime->stockTexture1;
        return SetRetainedEffectTexture2D(
            effect,
            retained,
            textureHandle,
            [dualTexture, textureIndex](std::shared_ptr<Texture2D> texture) {
                if (textureIndex == 0U) {
                    dualTexture->SetOwnedTexture(std::move(texture));
                } else {
                    dualTexture->SetOwnedTexture2(std::move(texture));
                }
            });
    });
}

CNA_Result cna_dual_texture_effect_get_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The DualTextureEffect vertex-color output is null.");
        }
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = dualTexture->getVertexColorEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dual_texture_effect_set_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The DualTextureEffect vertex-color value is not a CNA_Bool.");
        }
        DualTextureEffect* dualTexture = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &dualTexture, "DualTextureEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dualTexture->setVertexColorEnabledProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<EnvironmentMapEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_environment_map_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect diffuse-color output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(environmentMap->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_get_emissive_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect emissive-color output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(environmentMap->getEmissiveColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_emissive_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setEmissiveColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect alpha output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = environmentMap->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setAlphaProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect texture outputs are null.");
        }
        std::shared_ptr<EffectResource> effect;
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)environmentMap;
        return GetRetainedEffectTexture(
            GetEffectState(effect)->lifetime->stockTexture0,
            outHasTexture,
            outTexture,
            "The EnvironmentMapEffect texture outputs are null.");
    });
}

CNA_Result cna_environment_map_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetRetainedEffectTexture2D(
            effect,
            GetEffectState(effect)->lifetime->stockTexture0,
            textureHandle,
            [environmentMap](std::shared_ptr<Texture2D> texture) {
                environmentMap->SetOwnedTexture(std::move(texture));
            });
    });
}

CNA_Result cna_environment_map_effect_get_environment_map(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasEnvironmentMap,
    CNA_Handle* const outEnvironmentMap)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasEnvironmentMap == nullptr || outEnvironmentMap == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect cube-map outputs are null.");
        }
        std::shared_ptr<EffectResource> effect;
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)environmentMap;
        return GetRetainedEffectTexture(
            GetEffectState(effect)->lifetime->environmentMap,
            outHasEnvironmentMap,
            outEnvironmentMap,
            "The EnvironmentMapEffect cube-map outputs are null.");
    });
}

CNA_Result cna_environment_map_effect_set_environment_map(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle environmentMapHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetRetainedEffectTextureCube(
            effect,
            GetEffectState(effect)->lifetime->environmentMap,
            environmentMapHandle,
            [environmentMap](std::shared_ptr<TextureCube> texture) {
                environmentMap->SetOwnedEnvironmentMap(std::move(texture));
            });
    });
}

CNA_Result cna_environment_map_effect_get_amount(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect amount output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = environmentMap->getEnvironmentMapAmountProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_amount(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setEnvironmentMapAmountProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_get_specular(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect specular output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(environmentMap->getEnvironmentMapSpecularProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_specular(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setEnvironmentMapSpecularProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_get_fresnel_factor(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EnvironmentMapEffect Fresnel-factor output is null.");
        }
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = environmentMap->getFresnelFactorProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_environment_map_effect_set_fresnel_factor(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        EnvironmentMapEffect* environmentMap = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &environmentMap, "EnvironmentMapEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        environmentMap->setFresnelFactorProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The SkinnedEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<SkinnedEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_skinned_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect diffuse-color output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(skinned->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setDiffuseColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_emissive_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect emissive-color output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(skinned->getEmissiveColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_emissive_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setEmissiveColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_specular_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect specular-color output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(skinned->getSpecularColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_specular_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setSpecularColorProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_specular_power(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect specular-power output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = skinned->getSpecularPowerProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_specular_power(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setSpecularPowerProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect alpha output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = skinned->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setAlphaProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_prefer_per_pixel_lighting(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect per-pixel-lighting output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = skinned->getPreferPerPixelLightingProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_prefer_per_pixel_lighting(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The SkinnedEffect per-pixel-lighting value is not a CNA_Bool.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setPreferPerPixelLightingProperty(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("The SkinnedEffect texture outputs are null.");
        }
        std::shared_ptr<EffectResource> effect;
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (void)skinned;
        return GetRetainedEffectTexture(
            GetEffectState(effect)->lifetime->stockTexture0,
            outHasTexture,
            outTexture,
            "The SkinnedEffect texture outputs are null.");
    });
}

CNA_Result cna_skinned_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, &effect, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetRetainedEffectTexture2D(
            effect,
            GetEffectState(effect)->lifetime->stockTexture0,
            textureHandle,
            [skinned](std::shared_ptr<Texture2D> texture) {
                skinned->SetOwnedTexture(std::move(texture));
            });
    });
}

CNA_Result cna_skinned_effect_get_weights_per_vertex(
    const CNA_EffectHandle effectHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect weights-per-vertex output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(skinned->getWeightsPerVertexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_weights_per_vertex(
    const CNA_EffectHandle effectHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value != 1 && value != 2 && value != 4) {
            return InvalidArgument("SkinnedEffect weights per vertex must be one, two, or four.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setWeightsPerVertexProperty(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_bone_transforms(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix* const transforms,
    const uint64_t transformCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t byteCount = 0U;
        if (transformCount == 0U || transformCount > CNA_SKINNED_EFFECT_MAX_BONES ||
            CheckedElementByteCount(
                transforms, transformCount, sizeof(CNA_Matrix), &byteCount) !=
                CNA_RESULT_SUCCESS) {
            return InvalidArgument(
                "The SkinnedEffect bone-transform array must contain one through 72 matrices.");
        }
        (void)byteCount;
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Matrix> copied;
        copied.reserve(static_cast<std::size_t>(transformCount));
        for (uint64_t index = 0U; index < transformCount; ++index) {
            copied.push_back(ToNative(transforms[index]));
        }
        skinned->SetBoneTransforms(copied);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_copy_bone_transforms(
    const CNA_EffectHandle effectHandle,
    const uint64_t requestedCount,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U) ||
            requestedCount == 0U || requestedCount > CNA_SKINNED_EFFECT_MAX_BONES) {
            return InvalidArgument("The SkinnedEffect bone-transform copy request is invalid.");
        }
        *outCount = requestedCount;
        if (capacity < requestedCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the requested bone transforms.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<Matrix> bones = skinned->GetBoneTransforms(
            static_cast<int>(requestedCount));
        for (uint64_t index = 0U; index < requestedCount; ++index) {
            destination[index] = ToC(bones[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_get_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedEffect vertex-color output is null.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = skinned->VertexColorEnabled ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_effect_set_vertex_color_enabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The SkinnedEffect vertex-color value is not a CNA_Bool.");
        }
        SkinnedEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->VertexColorEnabled = value == CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The ColorMatrixEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<ColorMatrixEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_color_matrix_effect_get_matrix(
    const CNA_EffectHandle effectHandle,
    CNA_ColorMatrix4x4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ColorMatrixEffect matrix output is null.");
        }
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::array<float, 16>& native = colorMatrix->GetColorMatrix();
        std::copy(native.begin(), native.end(), std::begin(outValue->values));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_set_matrix(
    const CNA_EffectHandle effectHandle,
    const CNA_ColorMatrix4x4 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::array<float, 16> native{};
        std::copy(std::begin(value.values), std::end(value.values), native.begin());
        colorMatrix->SetColorMatrix(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_get_offset(
    const CNA_EffectHandle effectHandle,
    CNA_Vector4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ColorMatrixEffect offset output is null.");
        }
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(colorMatrix->GetColorOffset());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_set_offset(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector4 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        colorMatrix->SetColorOffset(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_set_grayscale(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        colorMatrix->SetGrayscale();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_matrix_effect_reset(const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ColorMatrixEffect* colorMatrix = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &colorMatrix, "ColorMatrixEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        colorMatrix->Reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The PbrEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<PbrEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_skinned_pbr_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The SkinnedPbrEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<SkinnedPbrEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_pbr_effect_get_diffuse_color(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR diffuse-color output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(view.pbr != nullptr
            ? view.pbr->getDiffuseColorProperty()
            : view.skinned->getDiffuseColorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_diffuse_color(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setDiffuseColorProperty(ToNative(value));
        } else {
            view.skinned->setDiffuseColorProperty(ToNative(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_alpha(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR alpha output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getAlphaProperty()
            : view.skinned->getAlphaProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_alpha(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setAlphaProperty(value);
        } else {
            view.skinned->setAlphaProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetRetainedEffectTexture(
            GetEffectState(view.resource)->lifetime->pbrTextures[slot],
            outHasTexture,
            outTexture,
            "The PBR texture outputs are null.");
    });
}

CNA_Result cna_pbr_effect_set_texture(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetRetainedEffectTexture2D(
            view.resource,
            GetEffectState(view.resource)->lifetime->pbrTextures[slot],
            textureHandle,
            [&view, slot](std::shared_ptr<Texture2D> texture) {
                if (view.pbr != nullptr) {
                    switch (slot) {
                    case CNA_PBR_TEXTURE_BASE_COLOR: view.pbr->SetOwnedTexture(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_NORMAL: view.pbr->SetOwnedNormalMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_METALLIC_ROUGHNESS: view.pbr->SetOwnedMetallicRoughnessMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_EMISSIVE: view.pbr->SetOwnedEmissiveMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_OCCLUSION: view.pbr->SetOwnedOcclusionMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_SPECULAR_EXT: view.pbr->SetOwnedSpecularMapEXT(std::move(texture)); break;
                    default: view.pbr->SetOwnedSpecularColorMapEXT(std::move(texture)); break;
                    }
                } else {
                    switch (slot) {
                    case CNA_PBR_TEXTURE_BASE_COLOR: view.skinned->SetOwnedTexture(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_NORMAL: view.skinned->SetOwnedNormalMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_METALLIC_ROUGHNESS: view.skinned->SetOwnedMetallicRoughnessMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_EMISSIVE: view.skinned->SetOwnedEmissiveMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_OCCLUSION: view.skinned->SetOwnedOcclusionMap(std::move(texture)); break;
                    case CNA_PBR_TEXTURE_SPECULAR_EXT: view.skinned->SetOwnedSpecularMapEXT(std::move(texture)); break;
                    default: view.skinned->SetOwnedSpecularColorMapEXT(std::move(texture)); break;
                    }
                }
            });
    });
}

CNA_Result cna_pbr_effect_get_metallic_factor(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR metallic-factor output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getMetallicFactorProperty()
            : view.skinned->getMetallicFactorProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_metallic_factor(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setMetallicFactorProperty(value);
        } else {
            view.skinned->setMetallicFactorProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_roughness_factor(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR roughness-factor output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getRoughnessFactorProperty()
            : view.skinned->getRoughnessFactorProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_roughness_factor(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setRoughnessFactorProperty(value);
        } else {
            view.skinned->setRoughnessFactorProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_emissive_factor(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR emissive-factor output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(view.pbr != nullptr
            ? view.pbr->getEmissiveFactorProperty()
            : view.skinned->getEmissiveFactorProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_emissive_factor(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setEmissiveFactorProperty(ToNative(value));
        } else {
            view.skinned->setEmissiveFactorProperty(ToNative(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

// The three colour-carrying slots. Everything else a PBR material samples -- the normal map's
// tangent-space vectors, the packed metallic-roughness channels, the occlusion factor -- is linear
// data by definition, so an sRGB question about those slots has no answer rather than a false one.
[[nodiscard]] bool IsColorTextureSlot(const CNA_PbrTextureSlot slot) noexcept
{
    return slot == CNA_PBR_TEXTURE_BASE_COLOR || slot == CNA_PBR_TEXTURE_EMISSIVE ||
        slot == CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT;
}

[[nodiscard]] CNA_Bool ToCBoolean(const bool value) noexcept
{
    return value ? CNA_TRUE : CNA_FALSE;
}

[[nodiscard]] CNA_TextureTransformEXT ToC(const TextureTransformEXT& value) noexcept
{
    return CNA_TextureTransformEXT{
        .struct_size = sizeof(CNA_TextureTransformEXT),
        .struct_version = StructureVersion,
        .offset = ToC(value.Offset),
        .scale = ToC(value.Scale),
        .rotation = value.Rotation};
}

[[nodiscard]] TextureTransformEXT ToNative(const CNA_TextureTransformEXT& value) noexcept
{
    return TextureTransformEXT{
        .Offset = ToNative(value.offset),
        .Scale = ToNative(value.scale),
        .Rotation = value.rotation};
}

[[nodiscard]] bool IsTextureTransform(const CNA_TextureTransformEXT* const value) noexcept
{
    return value != nullptr && value->struct_size >= sizeof(CNA_TextureTransformEXT) &&
        value->struct_version == StructureVersion;
}

} // namespace

CNA_Result cna_texture_transform_ext_init(CNA_TextureTransformEXT* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTransform == nullptr) {
            return InvalidArgument("The texture transform output is null.");
        }
        *outTransform = ToC(TextureTransformEXT{});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_transform_ext_equals(
    const CNA_TextureTransformEXT* const left,
    const CNA_TextureTransformEXT* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The texture transform comparison output is null.");
        }
        if (!IsTextureTransform(left) || !IsTextureTransform(right)) {
            return InvalidArgument("A texture transform operand is null or malformed.");
        }
        *outEqual = ToCBoolean(ToNative(*left) == ToNative(*right));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_ior_ext(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR index-of-refraction output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getIorEXTProperty()
            : view.skinned->getIorEXTProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_vertex_color_enabled_ext(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnabled == nullptr) {
            return InvalidArgument("The vertex-colour-enabled output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const bool enabled = view.pbr != nullptr
            ? view.pbr->VertexColorEnabledEXT
            : view.skinned->VertexColorEnabledEXT;
        *outEnabled = enabled ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_vertex_color_enabled_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool enabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(enabled)) {
            return InvalidArgument("The vertex-colour-enabled value is not a CNA_Bool.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->VertexColorEnabledEXT = enabled == CNA_TRUE;
        } else {
            view.skinned->VertexColorEnabledEXT = enabled == CNA_TRUE;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_ior_ext(const CNA_EffectHandle effectHandle, const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setIorEXTProperty(value);
        } else {
            view.skinned->setIorEXTProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_specular_factor_ext(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR specular-factor output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getSpecularFactorEXTProperty()
            : view.skinned->getSpecularFactorEXTProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_specular_factor_ext(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setSpecularFactorEXTProperty(value);
        } else {
            view.skinned->setSpecularFactorEXTProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_specular_color_factor_ext(
    const CNA_EffectHandle effectHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR specular-colour-factor output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(view.pbr != nullptr
            ? view.pbr->getSpecularColorFactorEXTProperty()
            : view.skinned->getSpecularColorFactorEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_specular_color_factor_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setSpecularColorFactorEXTProperty(ToNative(value));
        } else {
            view.skinned->setSpecularColorFactorEXTProperty(ToNative(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_normal_scale_ext(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR normal-scale output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getNormalScaleEXTProperty()
            : view.skinned->getNormalScaleEXTProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_normal_scale_ext(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setNormalScaleEXTProperty(value);
        } else {
            view.skinned->setNormalScaleEXTProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_occlusion_strength_ext(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR occlusion-strength output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getOcclusionStrengthEXTProperty()
            : view.skinned->getOcclusionStrengthEXTProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_occlusion_strength_ext(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setOcclusionStrengthEXTProperty(value);
        } else {
            view.skinned->setOcclusionStrengthEXTProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_texture_coordinate_set_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR texture-coordinate-set output is null.");
        }
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto read = [slot](const auto& effect) -> int {
            switch (slot) {
            case CNA_PBR_TEXTURE_SPECULAR_EXT:
                return effect.getSpecularTextureCoordinateSetEXTProperty();
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                return effect.getSpecularColorTextureCoordinateSetEXTProperty();
            default:
                return effect.getTextureCoordinateSetsEXTProperty()[slot];
            }
        };
        *outValue = view.pbr != nullptr ? read(*view.pbr) : read(*view.skinned);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_texture_coordinate_set_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        if (value != 0 && value != 1) {
            return InvalidArgument("The packed UV channel must be 0 or 1.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto write = [slot, value](auto& effect) {
            switch (slot) {
            case CNA_PBR_TEXTURE_SPECULAR_EXT:
                effect.setSpecularTextureCoordinateSetEXTProperty(value);
                break;
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                effect.setSpecularColorTextureCoordinateSetEXTProperty(value);
                break;
            default:
                effect.setTextureCoordinateSetEXTProperty(static_cast<int>(slot), value);
                break;
            }
        };
        if (view.pbr != nullptr) {
            write(*view.pbr);
        } else {
            write(*view.skinned);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_texture_transform_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    CNA_TextureTransformEXT* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsTextureTransform(outTransform)) {
            return InvalidArgument("The PBR texture-transform output is null or malformed.");
        }
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto read = [slot](const auto& effect) -> TextureTransformEXT {
            switch (slot) {
            case CNA_PBR_TEXTURE_SPECULAR_EXT:
                return effect.getSpecularTextureTransformEXTProperty();
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                return effect.getSpecularColorTextureTransformEXTProperty();
            default:
                return effect.getTextureTransformsEXTProperty()[slot];
            }
        };
        *outTransform = ToC(view.pbr != nullptr ? read(*view.pbr) : read(*view.skinned));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_texture_transform_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_TextureTransformEXT* const transform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsTextureTransform(transform)) {
            return InvalidArgument("The PBR texture transform is null or malformed.");
        }
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const TextureTransformEXT native = ToNative(*transform);
        const auto write = [slot, &native](auto& effect) {
            switch (slot) {
            case CNA_PBR_TEXTURE_SPECULAR_EXT:
                effect.setSpecularTextureTransformEXTProperty(native);
                break;
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                effect.setSpecularColorTextureTransformEXTProperty(native);
                break;
            default:
                effect.setTextureTransformEXTProperty(static_cast<int>(slot), native);
                break;
            }
        };
        if (view.pbr != nullptr) {
            write(*view.pbr);
        } else {
            write(*view.skinned);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_texture_is_srgb_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR sRGB output is null.");
        }
        if (!IsColorTextureSlot(slot)) {
            return InvalidArgument("Only the base-colour, emissive and specular-colour slots "
                                   "carry an sRGB encoding flag.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto read = [slot](const auto& effect) -> bool {
            switch (slot) {
            case CNA_PBR_TEXTURE_EMISSIVE:
                return effect.getEmissiveTextureIsSrgbEXTProperty();
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                return effect.getSpecularColorTextureIsSrgbEXTProperty();
            default:
                return effect.getBaseColorTextureIsSrgbEXTProperty();
            }
        };
        *outValue = ToCBoolean(view.pbr != nullptr ? read(*view.pbr) : read(*view.skinned));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_texture_is_srgb_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The boolean argument is neither CNA_TRUE nor CNA_FALSE.");
        }
        if (!IsColorTextureSlot(slot)) {
            return InvalidArgument("Only the base-colour, emissive and specular-colour slots "
                                   "carry an sRGB encoding flag.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const bool native = (value == CNA_TRUE);
        const auto write = [slot, native](auto& effect) {
            switch (slot) {
            case CNA_PBR_TEXTURE_EMISSIVE:
                effect.setEmissiveTextureIsSrgbEXTProperty(native);
                break;
            case CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT:
                effect.setSpecularColorTextureIsSrgbEXTProperty(native);
                break;
            default:
                effect.setBaseColorTextureIsSrgbEXTProperty(native);
                break;
            }
        };
        if (view.pbr != nullptr) {
            write(*view.pbr);
        } else {
            write(*view.skinned);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_encode_output_to_srgb_ext(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR output-encoding output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToCBoolean(view.pbr != nullptr
            ? view.pbr->getEncodeOutputToSrgbEXTProperty()
            : view.skinned->getEncodeOutputToSrgbEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_encode_output_to_srgb_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The boolean argument is neither CNA_TRUE nor CNA_FALSE.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setEncodeOutputToSrgbEXTProperty((value == CNA_TRUE));
        } else {
            view.skinned->setEncodeOutputToSrgbEXTProperty((value == CNA_TRUE));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_alpha_mode_ext(
    const CNA_EffectHandle effectHandle,
    CNA_AlphaModeEXT* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR alpha-mode output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<CNA_AlphaModeEXT>(view.pbr != nullptr
            ? view.pbr->getAlphaModeEXTProperty()
            : view.skinned->getAlphaModeEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_alpha_mode_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_AlphaModeEXT value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value > CNA_ALPHA_MODE_MAXIMUM_EXT) {
            return InvalidArgument("The alpha mode is not a defined identity.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto native = static_cast<AlphaModeEXT>(value);
        if (view.pbr != nullptr) {
            view.pbr->setAlphaModeEXTProperty(native);
        } else {
            view.skinned->setAlphaModeEXTProperty(native);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_alpha_cutoff_ext(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR alpha-cutoff output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = view.pbr != nullptr
            ? view.pbr->getAlphaCutoffEXTProperty()
            : view.skinned->getAlphaCutoffEXTProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_alpha_cutoff_ext(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setAlphaCutoffEXTProperty(value);
        } else {
            view.skinned->setAlphaCutoffEXTProperty(value);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_get_double_sided_ext(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The PBR double-sided output is null.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToCBoolean(view.pbr != nullptr
            ? view.pbr->getDoubleSidedEXTProperty()
            : view.skinned->getDoubleSidedEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_set_double_sided_ext(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsBool(value)) {
            return InvalidArgument("The boolean argument is neither CNA_TRUE nor CNA_FALSE.");
        }
        PbrEffectView view;
        if (const CNA_Result result = GetPbrEffect(effectHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.pbr != nullptr) {
            view.pbr->setDoubleSidedEXTProperty((value == CNA_TRUE));
        } else {
            view.skinned->setDoubleSidedEXTProperty((value == CNA_TRUE));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_pbr_effect_get_weights_per_vertex(
    const CNA_EffectHandle effectHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinnedPbrEffect weights-per-vertex output is null.");
        }
        SkinnedPbrEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedPbrEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(skinned->getWeightsPerVertexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_pbr_effect_set_weights_per_vertex(
    const CNA_EffectHandle effectHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value != 1 && value != 2 && value != 4) {
            return InvalidArgument(
                "SkinnedPbrEffect weights per vertex must be one, two, or four.");
        }
        SkinnedPbrEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedPbrEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        skinned->setWeightsPerVertexProperty(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_pbr_effect_set_bone_transforms(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix* const transforms,
    const uint64_t transformCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t byteCount = 0U;
        if (transformCount == 0U || transformCount > CNA_SKINNED_PBR_EFFECT_MAX_BONES ||
            CheckedElementByteCount(
                transforms, transformCount, sizeof(CNA_Matrix), &byteCount) !=
                CNA_RESULT_SUCCESS) {
            return InvalidArgument(
                "The SkinnedPbrEffect bone-transform array must contain one through 72 matrices.");
        }
        (void)byteCount;
        SkinnedPbrEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedPbrEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Matrix> copied;
        copied.reserve(static_cast<std::size_t>(transformCount));
        for (uint64_t index = 0U; index < transformCount; ++index) {
            copied.push_back(ToNative(transforms[index]));
        }
        skinned->SetBoneTransforms(copied);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_pbr_effect_copy_bone_transforms(
    const CNA_EffectHandle effectHandle,
    const uint64_t requestedCount,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U) ||
            requestedCount == 0U || requestedCount > CNA_SKINNED_PBR_EFFECT_MAX_BONES) {
            return InvalidArgument(
                "The SkinnedPbrEffect bone-transform copy request is invalid.");
        }
        *outCount = requestedCount;
        if (capacity < requestedCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the requested bone transforms.");
        }
        SkinnedPbrEffect* skinned = nullptr;
        if (const CNA_Result result = GetStockEffect(
                effectHandle, nullptr, &skinned, "SkinnedPbrEffect");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<Matrix> bones = skinned->GetBoneTransforms(
            static_cast<int>(requestedCount));
        for (uint64_t index = 0U; index < requestedCount; ++index) {
            destination[index] = ToC(bones[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

// ---------------------------------------------------------------------------------------------
// CNA extended graphics layer: CRTEffect and DepthEffect are ordinary ShaderEffect descendants,
// so they are created and observed through the same owned CNA_EffectHandle as every other effect.
// Their declarations live in graphics_ext.h; without the extension layer the routes report the
// same NOT_SUPPORTED result the rest of that family does, so the exported ABI never changes shape.
// ---------------------------------------------------------------------------------------------

#ifdef CNA_CNAEXT

#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/DepthEffect.hpp"

#include <cmath>

namespace {

template<typename TEffect>
[[nodiscard]] CNA_Result GetExtensionEffect(
    const CNA_EffectHandle effectHandle,
    TEffect** const outEffect,
    std::shared_ptr<EffectResource>* const outResource,
    const char* const message)
{
    if (const CNA_Result result = GetEffect(effectHandle, outResource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outEffect = dynamic_cast<TEffect*>((*outResource)->value.get());
    if (*outEffect == nullptr) {
        return Fail(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE, message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetCrtEffect(
    const CNA_EffectHandle effectHandle,
    CNA::Graphics::CRTEffect** const outEffect,
    std::shared_ptr<EffectResource>* const outResource)
{
    return GetExtensionEffect(
        effectHandle, outEffect, outResource, "The Effect is not a CRTEffect.");
}

[[nodiscard]] CNA_Result GetDepthEffect(
    const CNA_EffectHandle effectHandle,
    CNA::Graphics::DepthEffect** const outEffect,
    std::shared_ptr<EffectResource>* const outResource)
{
    return GetExtensionEffect(
        effectHandle, outEffect, outResource, "The Effect is not a DepthEffect.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result CrtQuery(
    const CNA_EffectHandle effectHandle,
    float* const outValue,
    TCallable&& callable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The CRT effect output is null.");
        }
        CNA::Graphics::CRTEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetCrtEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = std::forward<TCallable>(callable)(*effect);
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result CrtCommand(
    const CNA_EffectHandle effectHandle,
    const float value,
    TCallable&& callable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(value)) {
            return InvalidArgument("The CRT effect value is not finite.");
        }
        CNA::Graphics::CRTEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetCrtEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::forward<TCallable>(callable)(*effect, value);
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_crt_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The CRTEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<CNA::Graphics::CRTEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_crt_effect_get_scanline_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CrtQuery(effectHandle, outValue, [](const CNA::Graphics::CRTEffect& effect) {
        return effect.getScanlineIntensity();
    });
}

CNA_Result cna_crt_effect_set_scanline_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setScanlineIntensity(v);
    });
}

CNA_Result cna_crt_effect_get_curvature(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CrtQuery(effectHandle, outValue, [](const CNA::Graphics::CRTEffect& effect) {
        return effect.getCurvature();
    });
}

CNA_Result cna_crt_effect_set_curvature(const CNA_EffectHandle effectHandle, const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setCurvature(v);
    });
}

CNA_Result cna_crt_effect_get_vignette_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CrtQuery(effectHandle, outValue, [](const CNA::Graphics::CRTEffect& effect) {
        return effect.getVignetteIntensity();
    });
}

CNA_Result cna_crt_effect_set_vignette_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setVignetteIntensity(v);
    });
}

CNA_Result cna_crt_effect_get_mask_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    return CrtQuery(effectHandle, outValue, [](const CNA::Graphics::CRTEffect& effect) {
        return effect.getMaskIntensity();
    });
}

CNA_Result cna_crt_effect_set_mask_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setMaskIntensity(v);
    });
}

CNA_Result cna_crt_effect_get_mask_type(
    const CNA_EffectHandle effectHandle,
    CNA_CRTMaskType* const outMaskType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMaskType == nullptr) {
            return InvalidArgument("The CRT mask-type output is null.");
        }
        CNA::Graphics::CRTEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetCrtEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMaskType = static_cast<CNA_CRTMaskType>(effect->getMaskType());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_crt_effect_set_mask_type(
    const CNA_EffectHandle effectHandle,
    const CNA_CRTMaskType maskType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (maskType > CNA_CRT_MASK_TYPE_SHADOW_MASK) {
            return InvalidArgument("The CRT mask type is not recognized.");
        }
        CNA::Graphics::CRTEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetCrtEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->setMaskType(static_cast<CNA::Graphics::CRTMaskType>(maskType));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The DepthEffect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectHandle(
            std::make_shared<CNA::Graphics::DepthEffect>(*graphicsDevice->value),
            graphicsDevice->parentGame,
            outEffect);
    });
}

CNA_Result cna_depth_effect_get_mode(
    const CNA_EffectHandle effectHandle,
    CNA_DepthEffectMode* const outMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMode == nullptr) {
            return InvalidArgument("The depth-effect mode output is null.");
        }
        CNA::Graphics::DepthEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetDepthEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMode = static_cast<CNA_DepthEffectMode>(effect->getMode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_effect_set_mode(
    const CNA_EffectHandle effectHandle,
    const CNA_DepthEffectMode mode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (mode > CNA_DEPTH_EFFECT_MODE_PALETTE_16) {
            return InvalidArgument("The depth-effect mode is not recognized.");
        }
        CNA::Graphics::DepthEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetDepthEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->setMode(static_cast<CNA::Graphics::DepthEffectMode>(mode));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_effect_get_dither_mode(
    const CNA_EffectHandle effectHandle,
    CNA_DitherMode* const outDitherMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDitherMode == nullptr) {
            return InvalidArgument("The depth-effect dither output is null.");
        }
        CNA::Graphics::DepthEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetDepthEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDitherMode = static_cast<CNA_DitherMode>(effect->getDitherMode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_effect_set_dither_mode(
    const CNA_EffectHandle effectHandle,
    const CNA_DitherMode ditherMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (ditherMode > CNA_DITHER_MODE_BAYER_8X8) {
            return InvalidArgument("The dither mode is not recognized.");
        }
        CNA::Graphics::DepthEffect* effect = nullptr;
        std::shared_ptr<EffectResource> resource;
        if (const CNA_Result result = GetDepthEffect(effectHandle, &effect, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->setDitherMode(static_cast<CNA::Graphics::DitherMode>(ditherMode));
        return CNA_RESULT_SUCCESS;
    });
}

#else // CNA_CNAEXT

namespace {

[[nodiscard]] CNA_Result ExtensionEffectUnavailable()
{
    return Fail(
        CNA_RESULT_NOT_SUPPORTED,
        CNA_ERROR_CATEGORY_NOT_SUPPORTED,
        "This CNA build does not contain the extended graphics layer.");
}

} // namespace

CNA_Result cna_crt_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    (void)graphicsDeviceHandle;
    if (outEffect != nullptr) {
        *outEffect = CNA_INVALID_HANDLE;
    }
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_get_scanline_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    (void)effectHandle;
    (void)outValue;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_set_scanline_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    (void)effectHandle;
    (void)value;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_get_curvature(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    (void)effectHandle;
    (void)outValue;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_set_curvature(const CNA_EffectHandle effectHandle, const float value)
{
    (void)effectHandle;
    (void)value;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_get_vignette_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    (void)effectHandle;
    (void)outValue;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_set_vignette_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    (void)effectHandle;
    (void)value;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_get_mask_intensity(
    const CNA_EffectHandle effectHandle,
    float* const outValue)
{
    (void)effectHandle;
    (void)outValue;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_set_mask_intensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    (void)effectHandle;
    (void)value;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_get_mask_type(
    const CNA_EffectHandle effectHandle,
    CNA_CRTMaskType* const outMaskType)
{
    (void)effectHandle;
    (void)outMaskType;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_crt_effect_set_mask_type(
    const CNA_EffectHandle effectHandle,
    const CNA_CRTMaskType maskType)
{
    (void)effectHandle;
    (void)maskType;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_depth_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_EffectHandle* const outEffect)
{
    (void)graphicsDeviceHandle;
    if (outEffect != nullptr) {
        *outEffect = CNA_INVALID_HANDLE;
    }
    return ExtensionEffectUnavailable();
}

CNA_Result cna_depth_effect_get_mode(
    const CNA_EffectHandle effectHandle,
    CNA_DepthEffectMode* const outMode)
{
    (void)effectHandle;
    (void)outMode;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_depth_effect_set_mode(
    const CNA_EffectHandle effectHandle,
    const CNA_DepthEffectMode mode)
{
    (void)effectHandle;
    (void)mode;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_depth_effect_get_dither_mode(
    const CNA_EffectHandle effectHandle,
    CNA_DitherMode* const outDitherMode)
{
    (void)effectHandle;
    (void)outDitherMode;
    return ExtensionEffectUnavailable();
}

CNA_Result cna_depth_effect_set_dither_mode(
    const CNA_EffectHandle effectHandle,
    const CNA_DitherMode ditherMode)
{
    (void)effectHandle;
    (void)ditherMode;
    return ExtensionEffectUnavailable();
}

#endif // CNA_CNAEXT

// CBIND-093. The last three ShaderEffect members: the compile error and the two array uniforms.
// They live here with the rest of the shader-effect surface rather than in the engine layer,
// because ShaderEffect is an always-compiled graphics type and these work in every build.

CNA_Result cna_shader_effect_copy_compile_error_ext(
    const CNA_EffectHandle effectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ShaderEffect* shader = nullptr;
        if (const CNA_Result result = GetShaderEffect(effectHandle, nullptr, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The Effect string output buffer is invalid.");
        }
        // The canonical accessor returns by value, unlike the other string getters here, so the
        // text is held rather than referenced while it is measured and copied.
        const std::string text = shader->GetCompileErrorEXT();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete compile error.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

// Both array setters take a tightly packed float array and a count. The count is refused when
// negative rather than treated as zero: a negative count is a caller mistake, and reading it as
// "none" would hide it.
[[nodiscard]] CNA_Result RequireUniformArray(
    const float* const values, const int32_t count, const char* const what)
{
    if (count < INT32_C(0)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The count is negative.");
    }
    if (values == nullptr && count != INT32_C(0)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, what);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_shader_effect_set_uniform_vec3_array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const values,
    const int32_t count)
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
        if (const CNA_Result result =
                RequireUniformArray(values, count, "The vector array is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformVec3Array(copiedName.c_str(), values, static_cast<int>(count));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_set_uniform_mat4_array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const matrices,
    const int32_t count)
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
        if (const CNA_Result result =
                RequireUniformArray(matrices, count, "The matrix array is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->SetUniformMat4Array(copiedName.c_str(), matrices, static_cast<int>(count));
        return CNA_RESULT_SUCCESS;
    });
}
