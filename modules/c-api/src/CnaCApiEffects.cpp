// SPDX-License-Identifier: MS-PL

#include "CNA/C/effects.h"
#include "CNA/C/graphics_ext.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
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

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::BorrowGameGraphicsDevice;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyElements;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetOwnedTexture3D;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetOwnedTextureCube;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::IoError;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::RequireIndex;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::TextureCubeResourceView;
using CNA::C::Detail::TextureResourceView;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;
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

[[nodiscard]] CNA_Result GetAnnotation(
    const CNA_EffectAnnotationHandle handle,
    std::shared_ptr<AnnotationResource>* const outAnnotation)
{
    return LookupHandle(handle, ObjectKind::EffectAnnotation, outAnnotation);
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_EffectAnnotationCollectionHandle handle,
    std::shared_ptr<AnnotationCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::EffectAnnotationCollection, outCollection);
}

[[nodiscard]] CNA_Result CreateAnnotationHandle(
    const EffectAnnotation& annotation,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    const auto resource = std::make_shared<AnnotationResource>(
        AnnotationResource{std::make_shared<EffectAnnotation>(annotation)});
    return CreateHandle(ObjectKind::EffectAnnotation, resource, outAnnotation);
}

[[nodiscard]] CNA_Result GetParameter(
    const CNA_EffectParameterHandle handle,
    std::shared_ptr<ParameterResource>* const outParameter)
{
    return LookupHandle(handle, ObjectKind::EffectParameter, outParameter);
}

[[nodiscard]] CNA_Result GetParameterCollection(
    const CNA_EffectParameterCollectionHandle handle,
    std::shared_ptr<ParameterCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::EffectParameterCollection, outCollection);
}

[[nodiscard]] CNA_Result CreateParameterHandle(
    std::shared_ptr<EffectParameter> value,
    std::shared_ptr<ParameterState> state,
    CNA_EffectParameterHandle* const outParameter)
{
    const auto resource = std::make_shared<ParameterResource>(
        ParameterResource{std::move(value), std::move(state)});
    return CreateHandle(ObjectKind::EffectParameter, resource, outParameter);
}

[[nodiscard]] CNA_Result CreateParameterCollectionHandle(
    std::shared_ptr<ParameterCollectionState> state,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<ParameterCollectionResource>(
        ParameterCollectionResource{std::move(state)});
    return CreateHandle(ObjectKind::EffectParameterCollection, resource, outCollection);
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
    if (const CNA_Result result = ReadText(
            createInfo->name, "The EffectParameter name is not valid UTF-8 text.", &name,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            createInfo->semantic, "The EffectParameter semantic is not valid UTF-8 text.",
            &semantic, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
    return CopyElements<TValue>(values, static_cast<TValue*>(destination), capacity, outCount);
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
        return Overflow("The requested EffectParameter value count exceeds the native range.");
    }
    *outCount = static_cast<int>(count);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyEffectName(
    const CNA_StringView name,
    std::string* const outName,
    const char* const failureMessage)
{
    return ReadText(name, failureMessage, outName, EmbeddedNul::Refused);
}

[[nodiscard]] CNA_Result GetPass(
    const CNA_EffectPassHandle handle,
    std::shared_ptr<PassResource>* const outPass)
{
    return LookupHandle(handle, ObjectKind::EffectPass, outPass);
}

[[nodiscard]] CNA_Result GetPassCollection(
    const CNA_EffectPassCollectionHandle handle,
    std::shared_ptr<PassCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::EffectPassCollection, outCollection);
}

[[nodiscard]] CNA_Result GetTechnique(
    const CNA_EffectTechniqueHandle handle,
    std::shared_ptr<TechniqueResource>* const outTechnique)
{
    return LookupHandle(handle, ObjectKind::EffectTechnique, outTechnique);
}

[[nodiscard]] CNA_Result GetTechniqueCollection(
    const CNA_EffectTechniqueCollectionHandle handle,
    std::shared_ptr<TechniqueCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::EffectTechniqueCollection, outCollection);
}

[[nodiscard]] CNA_Result CreatePassHandle(
    std::shared_ptr<EffectPass> value,
    std::shared_ptr<void> effectOwnership,
    CNA_EffectPassHandle* const outPass)
{
    const auto resource = std::make_shared<PassResource>(
        PassResource{std::move(value), std::move(effectOwnership)});
    return CreateHandle(ObjectKind::EffectPass, resource, outPass);
}

[[nodiscard]] CNA_Result CreatePassCollectionHandle(
    std::shared_ptr<PassCollectionState> state,
    CNA_EffectPassCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<PassCollectionResource>(
        PassCollectionResource{std::move(state)});
    return CreateHandle(ObjectKind::EffectPassCollection, resource, outCollection);
}

[[nodiscard]] CNA_Result CreateTechniqueHandle(
    std::shared_ptr<EffectTechnique> value,
    std::shared_ptr<TechniqueState> state,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    const auto resource = std::make_shared<TechniqueResource>(
        TechniqueResource{std::move(value), std::move(state)});
    return CreateHandle(ObjectKind::EffectTechnique, resource, outTechnique);
}

[[nodiscard]] CNA_Result CreateTechniqueCollectionHandle(
    std::shared_ptr<TechniqueCollectionState> state,
    CNA_EffectTechniqueCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<TechniqueCollectionResource>(
        TechniqueCollectionResource{std::move(state)});
    return CreateHandle(ObjectKind::EffectTechniqueCollection, resource, outCollection);
}

[[nodiscard]] CNA_Result CreateAnnotationCollectionAlias(
    std::shared_ptr<void> owner,
    EffectAnnotationCollection* const value,
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<AnnotationCollectionResource>(
        AnnotationCollectionResource{
            std::shared_ptr<EffectAnnotationCollection>(std::move(owner), value)});
    return CreateHandle(ObjectKind::EffectAnnotationCollection, resource, outCollection);
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
    return LookupHandle(handle, ObjectKind::Effect, outEffect);
}

[[nodiscard]] std::shared_ptr<EffectState> GetEffectState(
    const std::shared_ptr<EffectResource>& effect)
{
    return std::static_pointer_cast<EffectState>(effect->adapterState);
}

[[nodiscard]] std::shared_ptr<EffectState> MakeEffectState(
    const std::shared_ptr<Effect>& value,
    const CNA_Handle parentGame)
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
    return state;
}

[[nodiscard]] CNA_Result CreateEffectHandle(
    std::shared_ptr<Effect> value,
    const CNA_Handle parentGame,
    CNA_EffectHandle* const outEffect,
    const bool disposeAllowed = true)
{
    auto state = MakeEffectState(value, parentGame);
    const auto resource = std::make_shared<EffectResource>(EffectResource{
        std::move(value), parentGame, state, 0U, disposeAllowed});
    return CreateHandle(ObjectKind::Effect, resource, outEffect);
}

// The class or interface name an Effect lookup names when the effect is not one.
template<typename TEffect>
constexpr const char* EffectTypeName = nullptr;
template<>
constexpr const char* EffectTypeName<AlphaTestEffect> = "AlphaTestEffect";
template<>
constexpr const char* EffectTypeName<BasicEffect> = "BasicEffect";
template<>
constexpr const char* EffectTypeName<ColorMatrixEffect> = "ColorMatrixEffect";
template<>
constexpr const char* EffectTypeName<DualTextureEffect> = "DualTextureEffect";
template<>
constexpr const char* EffectTypeName<EnvironmentMapEffect> = "EnvironmentMapEffect";
template<>
constexpr const char* EffectTypeName<ShaderEffect> = "ShaderEffect";
template<>
constexpr const char* EffectTypeName<SkinnedEffect> = "SkinnedEffect";
template<>
constexpr const char* EffectTypeName<SkinnedPbrEffect> = "SkinnedPbrEffect";
template<>
constexpr const char* EffectTypeName<IEffectFog> = "IEffectFog";
template<>
constexpr const char* EffectTypeName<IEffectLights> = "IEffectLights";
template<>
constexpr const char* EffectTypeName<IEffectMatrices> = "IEffectMatrices";

/// The effect behind a handle as one concrete effect class; INVALID_HANDLE when it is another.
template<typename TEffect>
[[nodiscard]] CNA_Result GetStockEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    TEffect** const outStockEffect)
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
            std::string("The Effect handle does not refer to a ") + EffectTypeName<TEffect> + ".");
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outStockEffect = stockEffect;
    return CNA_RESULT_SUCCESS;
}

/// The two-argument form the generated routes use.
template<typename TEffect>
[[nodiscard]] CNA_Result GetStockEffect(const CNA_EffectHandle handle, TEffect** const outEffect)
{
    return GetStockEffect(handle, nullptr, outEffect);
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

/// The effect behind a handle as one effect interface; NOT_SUPPORTED when it does not implement it.
template<typename TInterface>
[[nodiscard]] CNA_Result GetEffectInterface(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outResource,
    TInterface** const outInterface)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const interfaceValue = dynamic_cast<TInterface*>(effect->value.get());
    if (interfaceValue == nullptr) {
        return NotSupported(
            std::string("The Effect does not implement ") + EffectTypeName<TInterface> + ".");
    }
    if (outResource != nullptr) {
        *outResource = std::move(effect);
    }
    *outInterface = interfaceValue;
    return CNA_RESULT_SUCCESS;
}

/// The two-argument form the generated routes use.
template<typename TInterface>
[[nodiscard]] CNA_Result GetEffectInterface(
    const CNA_EffectHandle handle,
    TInterface** const outInterface)
{
    return GetEffectInterface(handle, nullptr, outInterface);
}

[[nodiscard]] CNA_Result GetDirectionalLight(
    const CNA_DirectionalLightHandle handle,
    std::shared_ptr<DirectionalLightResource>* const outLight)
{
    return LookupHandle(handle, ObjectKind::DirectionalLight, outLight);
}

[[nodiscard]] CNA_Result CreateDirectionalLightHandle(
    std::shared_ptr<DirectionalLight> value,
    std::shared_ptr<void> effectOwnership,
    CNA_DirectionalLightHandle* const outLight)
{
    const auto resource = std::make_shared<DirectionalLightResource>(
        DirectionalLightResource{std::move(value), std::move(effectOwnership)});
    return CreateHandle(ObjectKind::DirectionalLight, resource, outLight);
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
        return BufferTooSmall("The destination cannot hold the complete Effect string.");
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
    return ReadText(
        name, "The shader uniform name is not valid UTF-8 text.", outName, EmbeddedNul::Refused);
}

[[nodiscard]] CNA_Result ValidateEffectTextureOwner(
    const EffectResource& effect,
    const CNA_Handle textureParentGame)
{
    if (textureParentGame == effect.parentGame) {
        return CNA_RESULT_SUCCESS;
    }
    return InvalidArgument("The Effect and texture belong to different graphics devices.");
}

[[nodiscard]] CNA_Result GetRetainedEffectTexture(
    const RetainedTextureSlot& retained,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
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
        return InvalidState("A disposed Texture2D cannot be assigned to a stock effect.");
    }
    if (texture->activeEffectReferenceCount == UINT64_MAX) {
        return Overflow("The Texture2D effect-reference count cannot be incremented.");
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
        return InvalidState("A disposed TextureCube cannot be assigned to a stock effect.");
    }
    if (texture.activeEffectReferenceCount == nullptr ||
        *texture.activeEffectReferenceCount == UINT64_MAX) {
        return Overflow("The TextureCube effect-reference count cannot be incremented.");
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

std::shared_ptr<void> CreateEffectAdapterState(const std::shared_ptr<Effect>& effect)
{
    // No counted owner: the content that loaded the effect owns it, not the game's C children.
    return MakeEffectState(effect, CNA_INVALID_HANDLE);
}

CNA_Result CreateBorrowedEffect(
    std::shared_ptr<Effect> effect,
    const CNA_Handle parentGame,
    CNA_Handle* const outEffect)
{
    return CreateEffectHandle(
        std::move(effect), parentGame, outEffect, false);
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result EffectAnnotationCreate(
    const CNA_EffectAnnotationCreateInfo* const createInfo,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    if (!IsVersionedStructure(createInfo) ||
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
    if (const CNA_Result result = ReadText(
            createInfo->name, "The EffectAnnotation name is not valid UTF-8 text.", &name,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            createInfo->semantic, "The EffectAnnotation semantic is not valid UTF-8 text.",
            &semantic, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            createInfo->cached_string,
            "The EffectAnnotation cached string is not valid UTF-8 text.", &cachedString,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result EffectAnnotationCollectionFind(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    *outFound = CNA_FALSE;
    *outAnnotation = CNA_INVALID_HANDLE;
    std::shared_ptr<AnnotationCollectionResource> collection;
    if (const CNA_Result result = GetCollection(collectionHandle, &collection);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The EffectAnnotation lookup name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result EffectParameterGetElements(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
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
}

[[nodiscard]] CNA_Result EffectParameterGetStructureMembers(
    const CNA_EffectParameterHandle parameterHandle,
    CNA_EffectParameterCollectionHandle* const outCollection)
{
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
}

[[nodiscard]] CNA_Result EffectParameterGetValue(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    void* const outValue)
{
    std::shared_ptr<ParameterResource> parameter;
    if (const CNA_Result result = GetParameter(parameterHandle, &parameter);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    switch (valueType) {
    case CNA_EFFECT_VALUE_BOOLEAN: {
        const CNA_Bool value = ToCBool(parameter->value->GetValueBoolean());
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
}

[[nodiscard]] CNA_Result EffectParameterGetValues(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const uint64_t requestedCount,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
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
            values.push_back(ToCBool(value));
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
}

[[nodiscard]] CNA_Result EffectParameterSetValue(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const void* const value)
{
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
}

[[nodiscard]] CNA_Result EffectParameterSetValues(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectValueType valueType,
    const void* const values,
    const uint64_t count)
{
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
}

[[nodiscard]] CNA_Result EffectParameterGetValueTexture(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectTextureType textureType,
    CNA_Handle* const outTexture)
{
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
}

[[nodiscard]] CNA_Result EffectParameterSetValueTexture(
    const CNA_EffectParameterHandle parameterHandle,
    const CNA_EffectTextureType textureType,
    const CNA_Handle textureHandle)
{
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
        return InvalidState("A disposed texture cannot be assigned to an EffectParameter.");
    }
    if (referenceCount == nullptr || *referenceCount == UINT64_MAX) {
        return Overflow("The EffectParameter texture retention count cannot be increased.");
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
}

[[nodiscard]] CNA_Result EffectParameterCollectionCreate(
    CNA_EffectParameterCollectionHandle* const outCollection)
{
    auto state = std::make_shared<ParameterCollectionState>();
    state->value = std::make_shared<EffectParameterCollection>();
    return CreateParameterCollectionHandle(std::move(state), outCollection);
}

[[nodiscard]] CNA_Result EffectParameterCollectionAddCreate(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_EffectParameterCreateInfo* const createInfo,
    CNA_EffectParameterHandle* const outParameter)
{
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
    // Non-null by construction: Add() above left the collection holding at least this element.
    EffectParameter* const added = (*collection->state->value)[index];
    return CreateCollectionElementHandle(collection->state, added, outParameter);
}

[[nodiscard]] CNA_Result EffectParameterCollectionFindName(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectParameterHandle* const outParameter)
{
    *outFound = CNA_FALSE;
    *outParameter = CNA_INVALID_HANDLE;
    std::shared_ptr<ParameterCollectionResource> collection;
    if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The EffectParameter lookup name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result EffectParameterCollectionFindSemantic(
    const CNA_EffectParameterCollectionHandle collectionHandle,
    const CNA_StringView semantic,
    CNA_Bool* const outFound,
    CNA_EffectParameterHandle* const outParameter)
{
    *outFound = CNA_FALSE;
    *outParameter = CNA_INVALID_HANDLE;
    std::shared_ptr<ParameterCollectionResource> collection;
    if (const CNA_Result result = GetParameterCollection(collectionHandle, &collection);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string copiedSemantic;
    if (const CNA_Result result = ReadText(
            semantic, "The EffectParameter lookup semantic is not valid UTF-8 text.",
            &copiedSemantic, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result EffectPassCreate(
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    CNA_EffectPassHandle* const outPass)
{
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
}

[[nodiscard]] CNA_Result EffectPassCreateIndexedExt(
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    const uint32_t passIndex,
    CNA_EffectPassHandle* const outPass)
{
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
}

[[nodiscard]] CNA_Result EffectPassCollectionCreate(
    CNA_EffectPassCollectionHandle* const outCollection)
{
    auto state = std::make_shared<PassCollectionState>();
    state->value = std::make_shared<EffectPassCollection>();
    return CreatePassCollectionHandle(std::move(state), outCollection);
}

[[nodiscard]] CNA_Result EffectPassCollectionAddCreate(
    const CNA_EffectPassCollectionHandle collectionHandle,
    const CNA_StringView name,
    const uint64_t techniqueIdentity,
    CNA_EffectPassHandle* const outPass)
{
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
    // Non-null by construction: Add() above left the collection holding at least this element.
    EffectPass* const pass = (*collection->state->value)[
        collection->state->value->getCountProperty() - 1];
    return CreatePassHandle(
        std::shared_ptr<EffectPass>(collection->state->value, pass),
        collection->state->effectOwnership,
        outPass);
}

[[nodiscard]] CNA_Result EffectPassCollectionFind(
    const CNA_EffectPassCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectPassHandle* const outPass)
{
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
}

[[nodiscard]] CNA_Result EffectTechniqueCreateNamed(
    const CNA_StringView name,
    CNA_EffectTechniqueHandle* const outTechnique)
{
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
}

[[nodiscard]] CNA_Result EffectTechniqueCreateReflectedExt(
    const CNA_StringView name,
    const uint32_t techniqueIndex,
    const CNA_Bool addDefaultPass,
    CNA_EffectTechniqueHandle* const outTechnique)
{
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
}

[[nodiscard]] CNA_Result EffectTechniqueGetPasses(
    const CNA_EffectTechniqueHandle techniqueHandle,
    CNA_EffectPassCollectionHandle* const outCollection)
{
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
}

[[nodiscard]] CNA_Result EffectTechniqueCollectionCreate(
    CNA_EffectTechniqueCollectionHandle* const outCollection)
{
    auto state = std::make_shared<TechniqueCollectionState>();
    state->value = std::make_shared<EffectTechniqueCollection>();
    return CreateTechniqueCollectionHandle(std::move(state), outCollection);
}

[[nodiscard]] CNA_Result EffectTechniqueCollectionAddDefault(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    CNA_EffectTechniqueHandle* const outTechnique)
{
    std::shared_ptr<TechniqueCollectionResource> collection;
    if (const CNA_Result result = GetTechniqueCollection(collectionHandle, &collection);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // Owned by the collection's effect, so Effect::setCurrentTechniqueProperty accepts it
    // (RUST-UPSTREAM-030); a standalone collection has no owner and passes null.
    collection->state->value->Add(
        EffectTechnique(collection->state->owner, std::string(), 0U, false));
    // Non-null by construction: Add() above left the collection holding at least this element.
    EffectTechnique* const technique = (*collection->state->value)[
        collection->state->value->getCountProperty() - 1];
    return CreateTechniqueElementHandle(
        collection->state, technique, outTechnique);
}

[[nodiscard]] CNA_Result EffectTechniqueCollectionAddNamed(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_EffectTechniqueHandle* const outTechnique)
{
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
        EffectTechnique(collection->state->owner, std::move(copiedName)));
    // Non-null by construction: Add() above left the collection holding at least this element.
    EffectTechnique* const technique = (*collection->state->value)[
        collection->state->value->getCountProperty() - 1];
    return CreateTechniqueElementHandle(
        collection->state, technique, outTechnique);
}

[[nodiscard]] CNA_Result EffectTechniqueCollectionFind(
    const CNA_EffectTechniqueCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectTechniqueHandle* const outTechnique)
{
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
}

[[nodiscard]] CNA_Result EffectCreateCompiled(
    const CNA_Handle graphicsDeviceHandle,
    const uint8_t* const effectCode,
    const uint64_t effectCodeCount,
    CNA_EffectHandle* const outEffect)
{
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
}

[[nodiscard]] CNA_Result ContentManagerLoadEffect(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_EffectHandle* const outEffect)
{
    std::string assetNameCopy;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameCopy,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
            return IoError("The Effect asset loaded as a null effect.");
        }
        return CreateEffectHandle(
            std::move(loaded), contentManager.parentGame, outEffect);
    } catch (const Microsoft::Xna::Framework::Content::ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // The asset's root reader produced something that is not an Effect. Reporting the
        // mismatch beats the exception barrier's catch-all calling it an internal fault.
        return IoError("The asset's root type reader did not produce an Effect.");
    }
}

[[nodiscard]] CNA_Result ShaderEffectCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView vertexSource,
    const CNA_StringView fragmentSource,
    CNA_EffectHandle* const outEffect)
{
    std::string vertex;
    std::string fragment;
    if (const CNA_Result result = ReadText(
            vertexSource, "The ShaderEffect vertex source is not valid UTF-8 text.", &vertex,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            fragmentSource, "The ShaderEffect fragment source is not valid UTF-8 text.",
            &fragment, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result EffectClone(
    const CNA_EffectHandle effectHandle,
    CNA_EffectHandle* const outClone)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<Effect> clone(effect->value->Clone());
    if (clone == nullptr) {
        return InternalError("The native Effect clone operation returned null.");
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
}

[[nodiscard]] CNA_Result EffectGetCurrentTechnique(
    const CNA_EffectHandle effectHandle,
    CNA_EffectTechniqueHandle* const outTechnique)
{
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
}

[[nodiscard]] CNA_Result EffectSetCurrentTechnique(
    const CNA_EffectHandle effectHandle,
    const CNA_EffectTechniqueHandle techniqueHandle)
{
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
}

[[nodiscard]] CNA_Result EffectGetGraphicsDevice(
    const CNA_EffectHandle effectHandle,
    CNA_Handle* const outGraphicsDevice)
{
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
        return InternalError("The Effect and borrowed graphics-device owners disagree.");
    }
    *outGraphicsDevice = borrowed;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ShaderEffectSetUniformMatrix(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Matrix value)
{
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectDeclareUniformBlockExt(
    const CNA_EffectHandle effectHandle,
    const int32_t blockSizeBytes,
    const CNA_StringView* const names,
    const int32_t* const offsets,
    const uint64_t count)
{
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
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetUniformFloatArray(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const values,
    const uint64_t count)
{
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
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetUniformVector2Array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const CNA_Vector2* const values,
    const uint64_t count)
{
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
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetTexture2d(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    if (unit < 0) {
        return InvalidArgument("The ShaderEffect texture unit is negative.");
    }
    std::shared_ptr<EffectResource> effect;
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, &effect, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetTextureCube(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    if (unit < 0) {
        return InvalidArgument("The ShaderEffect texture unit is negative.");
    }
    std::shared_ptr<EffectResource> effect;
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, &effect, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetTexture3d(
    const CNA_EffectHandle effectHandle,
    const int32_t unit,
    const CNA_Handle textureHandle)
{
    if (unit < 0) {
        return InvalidArgument("The ShaderEffect texture unit is negative.");
    }
    std::shared_ptr<EffectResource> effect;
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, &effect, &shader);
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
}

[[nodiscard]] CNA_Result EffectLightsGetDirectionalLight(
    const CNA_EffectHandle effectHandle,
    const uint32_t index,
    CNA_DirectionalLightHandle* const outLight)
{
    if (index > 2U) {
        return InvalidArgument("The directional-light index is outside zero through two.");
    }
    std::shared_ptr<EffectResource> effect;
    IEffectLights* lights = nullptr;
    if (const CNA_Result result = GetEffectInterface(
            effectHandle, &effect, &lights);
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
}

[[nodiscard]] CNA_Result EffectLightsSetEnabled(
    const CNA_EffectHandle effectHandle,
    const CNA_Bool value)
{
    IEffectLights* lights = nullptr;
    if (const CNA_Result result = GetEffectInterface(
            effectHandle, nullptr, &lights);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (value == CNA_FALSE &&
        (dynamic_cast<EnvironmentMapEffect*>(lights) != nullptr ||
         dynamic_cast<SkinnedEffect*>(lights) != nullptr ||
         dynamic_cast<PbrEffect*>(lights) != nullptr ||
         dynamic_cast<SkinnedPbrEffect*>(lights) != nullptr)) {
        return InvalidState("This stock effect's lighting cannot be disabled.");
    }
    lights->setLightingEnabledProperty(value == CNA_TRUE);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BasicEffectGetTexture(
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    *outHasTexture = CNA_FALSE;
    *outTexture = CNA_INVALID_HANDLE;
    std::shared_ptr<EffectResource> effect;
    BasicEffect* basic = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, &effect, &basic);
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
}

[[nodiscard]] CNA_Result BasicEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<EffectResource> effect;
    BasicEffect* basic = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, &effect, &basic);
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
}

[[nodiscard]] CNA_Result AlphaTestEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<EffectResource> effect;
    AlphaTestEffect* alphaTest = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &alphaTest);
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
}

[[nodiscard]] CNA_Result DualTextureEffectGetTexture(
    const CNA_EffectHandle effectHandle,
    const uint32_t textureIndex,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    *outHasTexture = CNA_FALSE;
    *outTexture = CNA_INVALID_HANDLE;
    if (textureIndex > 1U) {
        return InvalidArgument("The DualTextureEffect texture index is not zero or one.");
    }
    std::shared_ptr<EffectResource> effect;
    DualTextureEffect* dualTexture = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &dualTexture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    (void)dualTexture;
    const std::shared_ptr<EffectLifetime> lifetime = GetEffectState(effect)->lifetime;
    return GetRetainedEffectTexture(
        textureIndex == 0U ? lifetime->stockTexture0 : lifetime->stockTexture1,
        outHasTexture,
        outTexture);
}

[[nodiscard]] CNA_Result DualTextureEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const uint32_t textureIndex,
    const CNA_Handle textureHandle)
{
    if (textureIndex > 1U) {
        return InvalidArgument("The DualTextureEffect texture index is not zero or one.");
    }
    std::shared_ptr<EffectResource> effect;
    DualTextureEffect* dualTexture = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &dualTexture);
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
}

[[nodiscard]] CNA_Result EnvironmentMapEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<EffectResource> effect;
    EnvironmentMapEffect* environmentMap = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &environmentMap);
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
}

[[nodiscard]] CNA_Result EnvironmentMapEffectSetEnvironmentMap(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle environmentMapHandle)
{
    std::shared_ptr<EffectResource> effect;
    EnvironmentMapEffect* environmentMap = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &environmentMap);
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
}

[[nodiscard]] CNA_Result SkinnedEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<EffectResource> effect;
    SkinnedEffect* skinned = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, &effect, &skinned);
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
}

[[nodiscard]] CNA_Result SkinnedEffectSetBoneTransforms(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix* const transforms,
    const uint64_t transformCount)
{
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
            effectHandle, nullptr, &skinned);
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
}

[[nodiscard]] CNA_Result SkinnedEffectCopyBoneTransforms(
    const CNA_EffectHandle effectHandle,
    const uint64_t requestedCount,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U) || requestedCount == 0U ||
        requestedCount > CNA_SKINNED_EFFECT_MAX_BONES) {
        return InvalidArgument("The SkinnedEffect bone-transform copy request is invalid.");
    }
    *outCount = requestedCount;
    if (capacity < requestedCount) {
        return BufferTooSmall("The destination cannot hold the requested bone transforms.");
    }
    SkinnedEffect* skinned = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, nullptr, &skinned);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<Matrix> bones = skinned->GetBoneTransforms(
        static_cast<int>(requestedCount));
    for (uint64_t index = 0U; index < requestedCount; ++index) {
        destination[index] = ToC(bones[static_cast<std::size_t>(index)]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PbrEffectSetTexture(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_Handle textureHandle)
{
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
}

// The three colour-carrying slots. Everything else a PBR material samples -- the normal map's
// tangent-space vectors, the packed metallic-roughness channels, the occlusion factor -- is linear
// data by definition, so an sRGB question about those slots has no answer rather than a false one.
[[nodiscard]] bool IsColorTextureSlot(const CNA_PbrTextureSlot slot) noexcept
{
    return slot == CNA_PBR_TEXTURE_BASE_COLOR || slot == CNA_PBR_TEXTURE_EMISSIVE ||
        slot == CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT;
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
    return IsVersionedStructure(value);
}

[[nodiscard]] CNA_Result PbrEffectGetTextureCoordinateSetExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    int32_t* const outValue)
{
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
}

[[nodiscard]] CNA_Result PbrEffectSetTextureCoordinateSetExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const int32_t value)
{
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
}

[[nodiscard]] CNA_Result PbrEffectGetTextureTransformExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    CNA_TextureTransformEXT* const outTransform)
{
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
}

[[nodiscard]] CNA_Result PbrEffectSetTextureTransformExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_TextureTransformEXT* const transform)
{
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
}

[[nodiscard]] CNA_Result PbrEffectGetTextureIsSrgbExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    CNA_Bool* const outValue)
{
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
    *outValue = ToCBool(view.pbr != nullptr ? read(*view.pbr) : read(*view.skinned));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PbrEffectSetTextureIsSrgbExt(
    const CNA_EffectHandle effectHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_Bool value)
{
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
}

[[nodiscard]] CNA_Result PbrEffectSetAlphaModeExt(
    const CNA_EffectHandle effectHandle,
    const CNA_AlphaModeEXT value)
{
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
}

[[nodiscard]] CNA_Result SkinnedPbrEffectSetBoneTransforms(
    const CNA_EffectHandle effectHandle,
    const CNA_Matrix* const transforms,
    const uint64_t transformCount)
{
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
            effectHandle, nullptr, &skinned);
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
}

[[nodiscard]] CNA_Result SkinnedPbrEffectCopyBoneTransforms(
    const CNA_EffectHandle effectHandle,
    const uint64_t requestedCount,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U) || requestedCount == 0U ||
        requestedCount > CNA_SKINNED_PBR_EFFECT_MAX_BONES) {
        return InvalidArgument("The SkinnedPbrEffect bone-transform copy request is invalid.");
    }
    *outCount = requestedCount;
    if (capacity < requestedCount) {
        return BufferTooSmall("The destination cannot hold the requested bone transforms.");
    }
    SkinnedPbrEffect* skinned = nullptr;
    if (const CNA_Result result = GetStockEffect(
            effectHandle, nullptr, &skinned);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<Matrix> bones = skinned->GetBoneTransforms(
        static_cast<int>(requestedCount));
    for (uint64_t index = 0U; index < requestedCount; ++index) {
        destination[index] = ToC(bones[static_cast<std::size_t>(index)]);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

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
    std::shared_ptr<EffectResource>* const outResource,
    TEffect** const outEffect,
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
    std::shared_ptr<EffectResource>* const outResource,
    CNA::Graphics::CRTEffect** const outEffect)
{
    return GetExtensionEffect(
        effectHandle, outResource, outEffect, "The Effect is not a CRTEffect.");
}

[[nodiscard]] CNA_Result GetDepthEffect(
    const CNA_EffectHandle effectHandle,
    std::shared_ptr<EffectResource>* const outResource,
    CNA::Graphics::DepthEffect** const outEffect)
{
    return GetExtensionEffect(
        effectHandle, outResource, outEffect, "The Effect is not a DepthEffect.");
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
        if (const CNA_Result result = GetCrtEffect(effectHandle, &resource, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::forward<TCallable>(callable)(*effect, value);
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result CrtEffectSetScanlineIntensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setScanlineIntensity(v);
    });
}

[[nodiscard]] CNA_Result CrtEffectSetCurvature(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setCurvature(v);
    });
}

[[nodiscard]] CNA_Result CrtEffectSetVignetteIntensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setVignetteIntensity(v);
    });
}

[[nodiscard]] CNA_Result CrtEffectSetMaskIntensity(
    const CNA_EffectHandle effectHandle,
    const float value)
{
    return CrtCommand(effectHandle, value, [](CNA::Graphics::CRTEffect& effect, const float v) {
        effect.setMaskIntensity(v);
    });
}

[[nodiscard]] CNA_Result CrtEffectSetMaskType(
    const CNA_EffectHandle effectHandle,
    const CNA_CRTMaskType maskType)
{
    if (maskType > CNA_CRT_MASK_TYPE_SHADOW_MASK) {
        return InvalidArgument("The CRT mask type is not recognized.");
    }
    CNA::Graphics::CRTEffect* effect = nullptr;
    std::shared_ptr<EffectResource> resource;
    if (const CNA_Result result = GetCrtEffect(effectHandle, &resource, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    effect->setMaskType(static_cast<CNA::Graphics::CRTMaskType>(maskType));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DepthEffectSetMode(
    const CNA_EffectHandle effectHandle,
    const CNA_DepthEffectMode mode)
{
    if (mode > CNA_DEPTH_EFFECT_MODE_PALETTE_16) {
        return InvalidArgument("The depth-effect mode is not recognized.");
    }
    CNA::Graphics::DepthEffect* effect = nullptr;
    std::shared_ptr<EffectResource> resource;
    if (const CNA_Result result = GetDepthEffect(effectHandle, &resource, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    effect->setMode(static_cast<CNA::Graphics::DepthEffectMode>(mode));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DepthEffectSetDitherMode(
    const CNA_EffectHandle effectHandle,
    const CNA_DitherMode ditherMode)
{
    if (ditherMode > CNA_DITHER_MODE_BAYER_8X8) {
        return InvalidArgument("The dither mode is not recognized.");
    }
    CNA::Graphics::DepthEffect* effect = nullptr;
    std::shared_ptr<EffectResource> resource;
    if (const CNA_Result result = GetDepthEffect(effectHandle, &resource, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    effect->setDitherMode(static_cast<CNA::Graphics::DitherMode>(ditherMode));
    return CNA_RESULT_SUCCESS;
}

} // namespace

#else // CNA_CNAEXT

namespace {

[[nodiscard]] CNA_Result ExtensionEffectUnavailable()
{
    return NotSupported("This CNA build does not contain the extended graphics layer.");
}

} // namespace

#endif // CNA_CNAEXT

// CBIND-093. The last three ShaderEffect members: the compile error and the two array uniforms.
// They live here with the rest of the shader-effect surface rather than in the engine layer,
// because ShaderEffect is an always-compiled graphics type and these work in every build.

namespace {

// Both array setters take a tightly packed float array and a count. The count is refused when
// negative rather than treated as zero: a negative count is a caller mistake, and reading it as
// "none" would hide it.
[[nodiscard]] CNA_Result RequireUniformArray(
    const float* const values, const int32_t count, const char* const what)
{
    if (count < INT32_C(0)) {
        return InvalidArgument("The count is negative.");
    }
    if (values == nullptr && count != INT32_C(0)) {
        return InvalidArgument(what);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ShaderEffectSetUniformVec3Array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const values,
    const int32_t count)
{
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

[[nodiscard]] CNA_Result ShaderEffectSetUniformMat4Array(
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const float* const matrices,
    const int32_t count)
{
    ShaderEffect* shader = nullptr;
    if (const CNA_Result result = GetStockEffect(effectHandle, nullptr, &shader);
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
}

} // namespace

/* --- CBIND-104: the EffectMaterial retained-texture contract ----------------------------------- */

namespace {

/*
 * An EffectMaterial has no handle of its own -- it is an Effect handle whose object happens to be
 * one, the same way `cna_effect_material_create` hands one back. So these two routes have to say
 * which they got, rather than trusting the caller's word for it.
 */
[[nodiscard]] CNA_Result GetEffectMaterial(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outEffect,
    EffectMaterial** const outMaterial)
{
    if (const CNA_Result result = GetEffect(handle, outEffect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const material = dynamic_cast<EffectMaterial*>((*outEffect)->value.get());
    if (material == nullptr) {
        return InvalidArgument("The Effect handle does not hold an EffectMaterial.");
    }
    *outMaterial = material;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result EffectMaterialRetainParameterTextureExt(
    const CNA_EffectHandle effectHandle,
    const CNA_EffectTextureType textureType,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<EffectResource> effect;
    EffectMaterial* material = nullptr;
    if (const CNA_Result result = GetEffectMaterial(effectHandle, &effect, &material);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (textureType > CNA_EFFECT_TEXTURE_CUBE) {
        return InvalidArgument("The texture type is not a CNA_EFFECT_TEXTURE_* family.");
    }
    // The canonical method ignores a null pointer; the null handle is its C spelling, and
    // treating it as an error would make the two disagree about the same input.
    if (textureHandle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }

    std::shared_ptr<Texture> texture;
    switch (textureType) {
    case CNA_EFFECT_TEXTURE_BASE: {
        TextureResourceView view;
        if (const CNA_Result result = GetOwnedTexture(textureHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        texture = std::move(view.value);
        break;
    }
    case CNA_EFFECT_TEXTURE_2D: {
        std::shared_ptr<CNA::C::Detail::Texture2DResource> resource;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        texture = std::static_pointer_cast<Texture>(resource->value);
        break;
    }
    case CNA_EFFECT_TEXTURE_3D: {
        std::shared_ptr<CNA::C::Detail::Texture3DResource> resource;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        texture = std::static_pointer_cast<Texture>(resource->value);
        break;
    }
    default: {
        TextureCubeResourceView view;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        texture = std::static_pointer_cast<Texture>(view.value);
        break;
    }
    }

    // Only the canonical texture is handed over, not the C resource wrapper: retaining is what
    // makes the material own the object outright, so the caller's handle may be released
    // immediately afterwards without the parameter dangling.
    material->RetainParameterTextureEXT(std::move(texture));
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/Effects.toml.
#include "generated/Effects.inc"
