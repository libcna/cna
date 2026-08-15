// SPDX-License-Identifier: MS-PL

#include "CNA/C/models.h"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelBoneCollection;
using Microsoft::Xna::Framework::Graphics::ModelEffectCollection;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;
using Microsoft::Xna::Framework::Graphics::MorphWeightKeyframeEXT;
using Microsoft::Xna::Framework::Graphics::MorphWeightTrackEXT;

struct MorphDataResource final {
    std::shared_ptr<MorphTargetDataEXT> value;
};

struct BoneNode final {
    std::shared_ptr<ModelBone> value;
    std::vector<std::shared_ptr<BoneNode>> children;
    std::weak_ptr<BoneNode> parent;
};

struct BoneResource final {
    std::shared_ptr<BoneNode> node;
};

struct BoneCollectionResource final {
    std::shared_ptr<ModelBoneCollection> standalone;
    std::shared_ptr<BoneNode> parent;
    std::vector<std::shared_ptr<BoneNode>> nodes;
};

struct PartRetainedSlot final {
    CNA_Handle handle = CNA_INVALID_HANDLE;
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
    std::shared_ptr<void> owner;
    uint64_t* referenceCount = nullptr;

    PartRetainedSlot() = default;
    PartRetainedSlot(const PartRetainedSlot&) = delete;
    PartRetainedSlot& operator=(const PartRetainedSlot&) = delete;

    ~PartRetainedSlot()
    {
        Reset();
    }

    void Reset() noexcept
    {
        if (referenceCount != nullptr && *referenceCount != 0U) {
            --*referenceCount;
        }
        handle = CNA_INVALID_HANDLE;
        parentGame = CNA_INVALID_HANDLE;
        referenceCount = nullptr;
        owner.reset();
    }

    [[nodiscard]] CNA_Result Set(
        const CNA_Handle newHandle,
        const CNA_Handle newParentGame,
        std::shared_ptr<void> newOwner,
        uint64_t* const newReferenceCount)
    {
        if (newReferenceCount == nullptr ||
            *newReferenceCount == std::numeric_limits<uint64_t>::max()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The ModelMeshPart resource retention count overflowed.");
        }
        Reset();
        handle = newHandle;
        parentGame = newParentGame;
        owner = std::move(newOwner);
        referenceCount = newReferenceCount;
        ++*referenceCount;
        return CNA_RESULT_SUCCESS;
    }
};

struct MeshResource;

struct PartResource final {
    std::shared_ptr<ModelMeshPart> value = std::make_shared<ModelMeshPart>();
    std::shared_ptr<ModelMeshPart> detachedValue;
    PartRetainedSlot effect;
    PartRetainedSlot vertexBuffer;
    PartRetainedSlot indexBuffer;
    CNA_ModelMeshPartTag tag = 0U;
    std::shared_ptr<MorphDataResource> morphData;
    MeshResource* parentMesh = nullptr;

    ~PartResource()
    {
        value->setTagProperty(nullptr);
        if (detachedValue != nullptr) {
            detachedValue->setTagProperty(nullptr);
        }
    }
};

struct PartCollectionResource final {
    std::vector<std::shared_ptr<PartResource>> parts;
    std::shared_ptr<MeshResource> mesh;
};

struct MeshEffectEntry final {
    CNA_EffectHandle handle = CNA_INVALID_HANDLE;
    std::shared_ptr<EffectResource> resource;
};

struct MeshResource final {
    std::shared_ptr<ModelMesh> value;
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
    std::vector<std::shared_ptr<PartResource>> parts;
    std::vector<MeshEffectEntry> effects;
    std::shared_ptr<BoneNode> parentBone;
    CNA_ModelMeshTag tag = 0U;
    bool supportsThreeD = false;
    bool countedAsGraphicsResource = false;

    ~MeshResource();
};

struct MeshCollectionResource final {
    std::vector<std::shared_ptr<MeshResource>> meshes;
};

struct EffectCollectionResource final {
    std::shared_ptr<MeshResource> mesh;
};

struct ModelResource final {
    std::shared_ptr<Model> value;
    std::vector<std::shared_ptr<BoneNode>> bones;
    std::vector<std::shared_ptr<MeshResource>> meshes;
    std::shared_ptr<BoneNode> root;
    CNA_ModelTag tag = 0U;
    bool supportsThreeD = true;
};

[[nodiscard]] const std::vector<std::shared_ptr<PartResource>>& PartList(
    const PartCollectionResource& collection) noexcept
{
    return collection.mesh != nullptr ? collection.mesh->parts : collection.parts;
}

MeshResource::~MeshResource()
{
    for (const MeshEffectEntry& entry : effects) {
        if (entry.resource != nullptr &&
            entry.resource->activeModelReferenceCount != 0U) {
            --entry.resource->activeModelReferenceCount;
        }
    }
    for (const std::shared_ptr<PartResource>& part : parts) {
        if (part->parentMesh != this) {
            continue;
        }
        part->parentMesh = nullptr;
        part->value = std::move(part->detachedValue);
    }
    if (countedAsGraphicsResource) {
        RemoveOwnedGraphicsResource();
    }
}

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] Matrix ToNative(const CNA_Matrix value) noexcept
{
    return Matrix{
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44};
}

[[nodiscard]] CNA_Matrix ToC(const Matrix value) noexcept
{
    return CNA_Matrix{
        value.M11, value.M12, value.M13, value.M14,
        value.M21, value.M22, value.M23, value.M24,
        value.M31, value.M32, value.M33, value.M34,
        value.M41, value.M42, value.M43, value.M44};
}

[[nodiscard]] CNA_Result GetBone(
    const CNA_ModelBoneHandle handle,
    std::shared_ptr<BoneResource>* const outBone)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelBone, outBone);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The ModelBone handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_ModelBoneCollectionHandle handle,
    std::shared_ptr<BoneCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelBoneCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The ModelBoneCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateBoneHandle(
    std::shared_ptr<BoneNode> node,
    CNA_ModelBoneHandle* const outBone)
{
    const auto resource = std::make_shared<BoneResource>(
        BoneResource{std::move(node)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelBone, resource, outBone);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelBone handle could not be created.");
}

[[nodiscard]] CNA_Result CreateCollectionHandle(
    std::shared_ptr<BoneCollectionResource> resource,
    CNA_ModelBoneCollectionHandle* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelBoneCollection, std::move(resource), outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelBoneCollection handle could not be created.");
}

[[nodiscard]] const std::vector<std::shared_ptr<BoneNode>>& CollectionNodes(
    const BoneCollectionResource& collection) noexcept
{
    return collection.parent != nullptr ? collection.parent->children : collection.nodes;
}

[[nodiscard]] CNA_Result CopyBoneName(
    const std::string& name,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The ModelBone name output buffer is invalid.");
    }
    *outByteCount = static_cast<uint64_t>(name.size());
    if (capacity < name.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete ModelBone name.");
    }
    if (!name.empty()) {
        std::memcpy(destination, name.data(), name.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] BoundingSphere ToNative(const CNA_BoundingSphere value) noexcept
{
    return BoundingSphere(
        Microsoft::Xna::Framework::Vector3{
            value.center.x, value.center.y, value.center.z},
        value.radius);
}

[[nodiscard]] CNA_BoundingSphere ToC(const BoundingSphere value) noexcept
{
    return CNA_BoundingSphere{
        {value.Center.X, value.Center.Y, value.Center.Z}, value.Radius};
}

[[nodiscard]] CNA_Result CopyMeshName(
    const std::string& name,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The ModelMesh name output buffer is invalid.");
    }
    *outByteCount = static_cast<uint64_t>(name.size());
    if (capacity < name.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete ModelMesh name.");
    }
    if (!name.empty()) {
        std::memcpy(destination, name.data(), name.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetPart(
    const CNA_ModelMeshPartHandle handle,
    std::shared_ptr<PartResource>* const outPart)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelMeshPart, outPart);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshPart handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetPartCollection(
    const CNA_ModelMeshPartCollectionHandle handle,
    std::shared_ptr<PartCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelMeshPartCollection, outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshPartCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreatePartHandle(
    std::shared_ptr<PartResource> part,
    CNA_ModelMeshPartHandle* const outPart)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelMeshPart, std::move(part), outPart);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelMeshPart handle could not be created.");
}

[[nodiscard]] CNA_Result CreatePartCollectionHandle(
    std::shared_ptr<PartCollectionResource> collection,
    CNA_ModelMeshPartCollectionHandle* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelMeshPartCollection, std::move(collection), outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelMeshPartCollection handle could not be created.");
}

[[nodiscard]] CNA_Result GetMesh(
    const CNA_ModelMeshHandle handle,
    std::shared_ptr<MeshResource>* const outMesh)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelMesh, outMesh);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMesh handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetMeshCollection(
    const CNA_ModelMeshCollectionHandle handle,
    std::shared_ptr<MeshCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelMeshCollection, outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetEffectCollection(
    const CNA_ModelEffectCollectionHandle handle,
    std::shared_ptr<EffectCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelEffectCollection, outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelEffectCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateMeshHandle(
    std::shared_ptr<MeshResource> mesh,
    CNA_ModelMeshHandle* const outMesh)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelMesh, std::move(mesh), outMesh);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelMesh handle could not be created.");
}

[[nodiscard]] CNA_Result CreateMeshCollectionHandle(
    std::shared_ptr<MeshCollectionResource> collection,
    CNA_ModelMeshCollectionHandle* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelMeshCollection, std::move(collection), outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelMeshCollection handle could not be created.");
}

[[nodiscard]] CNA_Result CreateEffectCollectionHandle(
    std::shared_ptr<MeshResource> mesh,
    CNA_ModelEffectCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<EffectCollectionResource>();
    resource->mesh = std::move(mesh);
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ModelEffectCollection, resource, outCollection);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ModelEffectCollection handle could not be created.");
}

[[nodiscard]] CNA_Result GetModel(
    const CNA_ModelHandle handle,
    std::shared_ptr<ModelResource>* const outModel)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::Model, outModel);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The Model handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateModelHandle(
    std::shared_ptr<ModelResource> model,
    CNA_ModelHandle* const outModel)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::Model, std::move(model), outModel);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Model handle could not be created.");
}

[[nodiscard]] CNA_Result GetMorphData(
    const CNA_MorphTargetDataEXTHandle handle,
    std::shared_ptr<MorphDataResource>* const outData)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::MorphTargetDataEXT, outData);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The MorphTargetDataEXT handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateMorphDataHandle(
    std::shared_ptr<MorphDataResource> data,
    CNA_MorphTargetDataEXTHandle* const outData)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::MorphTargetDataEXT, std::move(data), outData);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned MorphTargetDataEXT handle could not be created.");
}

template<typename T>
[[nodiscard]] CNA_Result CopyInputArray(
    const T* const source,
    const uint64_t count,
    std::vector<T>* const destination,
    const char* const message)
{
    std::size_t byteCount = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            source, count, sizeof(T), &byteCount);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    static_cast<void>(byteCount);
    destination->clear();
    if (count != 0U) {
        destination->assign(source, source + static_cast<std::size_t>(count));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] Microsoft::Xna::Framework::Vector3 ToNativeVector3(
    const CNA_Vector3 value) noexcept
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] CNA_Vector3 ToCVector3(
    const Microsoft::Xna::Framework::Vector3 value) noexcept
{
    return {value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Result CopyTrackDescriptor(
    const CNA_MorphWeightTrackEXTDescriptor& descriptor,
    const std::size_t requiredWeightCount,
    const bool enforceRequiredWeightCount,
    MorphWeightTrackEXT* const outTrack)
{
    if ((descriptor.step_interpolation != CNA_FALSE &&
         descriptor.step_interpolation != CNA_TRUE) ||
        (descriptor.cubic_spline != CNA_FALSE &&
         descriptor.cubic_spline != CNA_TRUE)) {
        return InvalidArgument("Morph track interpolation flags must be CNA_TRUE or CNA_FALSE.");
    }
    std::size_t keyframeBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            descriptor.keyframes, descriptor.keyframe_count,
            sizeof(CNA_MorphWeightKeyframeEXTDescriptor), &keyframeBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The MorphWeightTrackEXT keyframe array is invalid or too large.");
    }
    static_cast<void>(keyframeBytes);

    MorphWeightTrackEXT copied;
    copied.StepInterpolation = descriptor.step_interpolation == CNA_TRUE;
    copied.CubicSpline = descriptor.cubic_spline == CNA_TRUE;
    copied.Keys.reserve(static_cast<std::size_t>(descriptor.keyframe_count));
    std::size_t trackWeightCount = requiredWeightCount;
    bool hasTrackWeightCount = enforceRequiredWeightCount;
    double previousTime = 0.0;
    for (uint64_t index = 0U; index < descriptor.keyframe_count; ++index) {
        const CNA_MorphWeightKeyframeEXTDescriptor& key = descriptor.keyframes[index];
        constexpr double MaxTimeSpanSeconds = 922337203685.0;
        if (!std::isfinite(key.time_seconds) ||
            key.time_seconds < -MaxTimeSpanSeconds ||
            key.time_seconds > MaxTimeSpanSeconds) {
            return InvalidArgument("Morph track keyframe times must fit a finite TimeSpan.");
        }
        if (index != 0U && key.time_seconds < previousTime) {
            return InvalidArgument("Morph track keyframe times must be in ascending order.");
        }
        previousTime = key.time_seconds;
        if (!hasTrackWeightCount) {
            if (key.weight_count > static_cast<uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                return Fail(
                    CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                    "The MorphWeightTrackEXT weight count is too large.");
            }
            trackWeightCount = static_cast<std::size_t>(key.weight_count);
            hasTrackWeightCount = true;
        }
        if (key.weight_count != trackWeightCount ||
            (key.in_tangent_count != 0U &&
             key.in_tangent_count != key.weight_count) ||
            (key.out_tangent_count != 0U &&
             key.out_tangent_count != key.weight_count)) {
            return InvalidArgument(
                "Morph track weights and non-empty tangents must have consistent counts.");
        }
        MorphWeightKeyframeEXT copiedKey;
        copiedKey.Time = System::TimeSpan::FromSeconds(key.time_seconds);
        if (const CNA_Result result = CopyInputArray(
                key.weights, key.weight_count, &copiedKey.Weights,
                "The MorphWeightKeyframeEXT weight array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyInputArray(
                key.in_tangents, key.in_tangent_count, &copiedKey.InTangent,
                "The MorphWeightKeyframeEXT incoming tangent array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyInputArray(
                key.out_tangents, key.out_tangent_count, &copiedKey.OutTangent,
                "The MorphWeightKeyframeEXT outgoing tangent array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        copied.Keys.push_back(std::move(copiedKey));
    }
    *outTrack = std::move(copied);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateMorphShape(
    const MorphTargetDataEXT& data,
    const std::vector<float>& weights)
{
    if (data.Stride != 32 && data.Stride != 52 && data.Stride != 56) {
        return InvalidArgument("Morph target stride must be 32, 52, or 56 bytes.");
    }
    if (data.BaseVertexBytes.size() % static_cast<std::size_t>(data.Stride) != 0U) {
        return InvalidArgument("Morph target base bytes must contain complete vertices.");
    }
    if (data.PositionDeltas.size() != data.NormalDeltas.size() ||
        weights.size() != data.PositionDeltas.size()) {
        return InvalidArgument("Morph target arrays and weights must have matching target counts.");
    }
    const std::size_t vertexCount =
        data.BaseVertexBytes.size() / static_cast<std::size_t>(data.Stride);
    for (std::size_t target = 0U; target < data.PositionDeltas.size(); ++target) {
        if (data.PositionDeltas[target].size() != vertexCount ||
            (!data.NormalDeltas[target].empty() &&
             data.NormalDeltas[target].size() != vertexCount)) {
            return InvalidArgument(
                "Every morph target must cover all vertices; normal deltas may be empty.");
        }
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TNative, typename TC, typename TConvert>
[[nodiscard]] CNA_Result CopyOutputValues(
    const std::vector<TNative>& source,
    TC* const destination,
    const uint64_t capacity,
    uint64_t* const outCount,
    TConvert&& convert,
    const char* const invalidMessage,
    const char* const smallMessage)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(invalidMessage);
    }
    *outCount = static_cast<uint64_t>(source.size());
    if (capacity < source.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, smallMessage);
    }
    for (std::size_t index = 0U; index < source.size(); ++index) {
        destination[index] = convert(source[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] auto FindMeshEffect(
    MeshResource& mesh,
    const CNA_EffectHandle handle)
{
    return std::find_if(
        mesh.effects.begin(),
        mesh.effects.end(),
        [handle](const MeshEffectEntry& entry) { return entry.handle == handle; });
}

void RemoveFirstMeshEffect(
    MeshResource& mesh,
    const CNA_EffectHandle handle) noexcept
{
    const auto iterator = FindMeshEffect(mesh, handle);
    if (iterator == mesh.effects.end()) {
        return;
    }
    if (iterator->resource != nullptr &&
        iterator->resource->activeModelReferenceCount != 0U) {
        --iterator->resource->activeModelReferenceCount;
    }
    mesh.effects.erase(iterator);
}

void AddMeshEffect(
    MeshResource& mesh,
    const CNA_EffectHandle handle,
    const std::shared_ptr<EffectResource>& effect)
{
    mesh.effects.push_back(MeshEffectEntry{handle, effect});
    ++effect->activeModelReferenceCount;
}

[[nodiscard]] CNA_Result ValidatePartDevice(
    const PartResource& part,
    const PartRetainedSlot* const replacedSlot,
    const CNA_Handle parentGame)
{
    if (part.parentMesh != nullptr && part.parentMesh->parentGame != parentGame) {
        return InvalidArgument(
            "A ModelMeshPart resource must belong to its parent mesh graphics device.");
    }
    const PartRetainedSlot* const slots[] = {
        &part.effect, &part.vertexBuffer, &part.indexBuffer};
    for (const PartRetainedSlot* const slot : slots) {
        if (slot != replacedSlot && slot->handle != CNA_INVALID_HANDLE &&
            slot->parentGame != parentGame) {
            return InvalidArgument(
                "All ModelMeshPart graphics resources must belong to one graphics device.");
        }
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveEffect(
    const CNA_EffectHandle handle,
    std::shared_ptr<EffectResource>* const outEffect)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::Effect, outEffect);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshPart Effect handle is invalid.");
    }
    if ((*outEffect)->value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A disposed Effect cannot be assigned to a ModelMeshPart.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveVertexBuffer(
    const CNA_VertexBufferHandle handle,
    std::shared_ptr<VertexBufferResource>* const outBuffer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::VertexBuffer, outBuffer);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshPart VertexBuffer handle is invalid.");
    }
    if ((*outBuffer)->value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A disposed VertexBuffer cannot be assigned to a ModelMeshPart.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveIndexBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::IndexBuffer, outBuffer);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMeshPart IndexBuffer handle is invalid.");
    }
    if ((*outBuffer)->value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A disposed IndexBuffer cannot be assigned to a ModelMeshPart.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SetPartEffect(
    const std::shared_ptr<PartResource>& part,
    const CNA_EffectHandle effectHandle)
{
    std::shared_ptr<EffectResource> effect;
    if (effectHandle == part->effect.handle) {
        return CNA_RESULT_SUCCESS;
    }
    if (effectHandle != CNA_INVALID_HANDLE) {
        if (const CNA_Result result = ResolveEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidatePartDevice(
                *part, &part->effect, effect->parentGame);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    MeshResource* const mesh = part->parentMesh;
    const CNA_EffectHandle oldHandle = part->effect.handle;
    bool removeOld = false;
    bool addNew = false;
    if (mesh != nullptr) {
        removeOld = oldHandle != CNA_INVALID_HANDLE;
        if (removeOld) {
            for (const std::shared_ptr<PartResource>& sibling : mesh->parts) {
                if (sibling.get() != part.get() && sibling->effect.handle == oldHandle) {
                    removeOld = false;
                    break;
                }
            }
        }
        addNew = effectHandle != CNA_INVALID_HANDLE &&
            FindMeshEffect(*mesh, effectHandle) == mesh->effects.end();
        if (addNew) {
            mesh->effects.reserve(mesh->effects.size() + 1U);
        }
    }

    if (effect != nullptr) {
        const uint64_t addedReferences = addNew ? 2U : 1U;
        if (effect->activeModelReferenceCount >
            std::numeric_limits<uint64_t>::max() - addedReferences) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The ModelMeshPart Effect retention count overflowed.");
        }
    }

    part->value->setEffectProperty(effect == nullptr ? nullptr : effect->value.get());
    if (part->detachedValue != nullptr) {
        part->detachedValue->setEffectProperty(
            effect == nullptr ? nullptr : effect->value.get());
    }
    part->effect.Reset();
    if (effect != nullptr) {
        const CNA_Result result = part->effect.Set(
            effectHandle, effect->parentGame, effect,
            &effect->activeModelReferenceCount);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }
    if (mesh != nullptr) {
        if (removeOld) {
            RemoveFirstMeshEffect(*mesh, oldHandle);
        }
        if (addNew) {
            AddMeshEffect(*mesh, effectHandle, effect);
        }
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SetPartVertexBuffer(
    const std::shared_ptr<PartResource>& part,
    const CNA_VertexBufferHandle bufferHandle)
{
    if (bufferHandle == CNA_INVALID_HANDLE) {
        part->value->SetVertexBuffer(nullptr);
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetVertexBuffer(nullptr);
        }
        part->vertexBuffer.Reset();
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = ResolveVertexBuffer(bufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidatePartDevice(
            *part, &part->vertexBuffer, buffer->parentGame);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (bufferHandle == part->vertexBuffer.handle) {
        return CNA_RESULT_SUCCESS;
    }
    if (buffer->activeModelReferenceCount == std::numeric_limits<uint64_t>::max()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The ModelMeshPart VertexBuffer retention count overflowed.");
    }
    part->value->SetVertexBuffer(buffer->value.get());
    if (part->detachedValue != nullptr) {
        part->detachedValue->SetVertexBuffer(buffer->value.get());
    }
    return part->vertexBuffer.Set(
        bufferHandle, buffer->parentGame, buffer, &buffer->activeModelReferenceCount);
}

[[nodiscard]] CNA_Result SetPartIndexBuffer(
    const std::shared_ptr<PartResource>& part,
    const CNA_IndexBufferHandle bufferHandle)
{
    if (bufferHandle == CNA_INVALID_HANDLE) {
        part->value->SetIndexBuffer(nullptr);
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetIndexBuffer(nullptr);
        }
        part->indexBuffer.Reset();
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<IndexBufferResource> buffer;
    if (const CNA_Result result = ResolveIndexBuffer(bufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidatePartDevice(
            *part, &part->indexBuffer, buffer->parentGame);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (bufferHandle == part->indexBuffer.handle) {
        return CNA_RESULT_SUCCESS;
    }
    if (buffer->activeModelReferenceCount == std::numeric_limits<uint64_t>::max()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The ModelMeshPart IndexBuffer retention count overflowed.");
    }
    part->value->SetIndexBuffer(buffer->value.get());
    if (part->detachedValue != nullptr) {
        part->detachedValue->SetIndexBuffer(buffer->value.get());
    }
    return part->indexBuffer.Set(
        bufferHandle, buffer->parentGame, buffer, &buffer->activeModelReferenceCount);
}

[[nodiscard]] CNA_Result CreateMesh(
    const CNA_Handle graphicsDeviceHandle,
    const std::string& name,
    const bool named,
    const CNA_ModelMeshPartHandle* const partHandles,
    const uint64_t partCount,
    CNA_ModelMeshHandle* const outMesh)
{
    std::size_t byteCount = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            partHandles, partCount, sizeof(*partHandles), &byteCount);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The ModelMesh part-handle array is invalid or too large.");
    }
    static_cast<void>(byteCount);

    std::shared_ptr<CNA::C::Detail::BorrowedGraphicsDevice> device;
    if (const CNA_Result result = GetBorrowedGraphicsDevice(
            graphicsDeviceHandle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    std::vector<std::shared_ptr<PartResource>> parts;
    std::vector<ModelMeshPart*> nativeParts;
    std::vector<std::shared_ptr<ModelMeshPart>> detachedParts;
    if (partCount > parts.max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The ModelMesh part count is too large.");
    }
    parts.reserve(static_cast<std::size_t>(partCount));
    nativeParts.reserve(static_cast<std::size_t>(partCount));
    detachedParts.reserve(static_cast<std::size_t>(partCount));
    for (uint64_t index = 0U; index < partCount; ++index) {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandles[index], &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (part->parentMesh != nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "A ModelMeshPart cannot belong to more than one live ModelMesh.");
        }
        const PartRetainedSlot* const slots[] = {
            &part->effect, &part->vertexBuffer, &part->indexBuffer};
        for (const PartRetainedSlot* const slot : slots) {
            if (slot->handle != CNA_INVALID_HANDLE &&
                slot->parentGame != device->parentGame) {
                return InvalidArgument(
                    "Every ModelMeshPart resource must belong to the mesh graphics device.");
            }
        }
        auto detached = std::make_shared<ModelMeshPart>(
            part->value->getVertexBufferProperty(),
            part->value->getIndexBufferProperty(),
            part->value->getNumVerticesProperty(),
            part->value->getPrimitiveCountProperty(),
            part->value->getStartIndexProperty(),
            part->value->getVertexOffsetProperty());
        detached->setEffectProperty(part->value->getEffectProperty());
        if (part->morphData != nullptr) {
            detached->setTagProperty(part->morphData->value.get());
        }
        nativeParts.push_back(part->value.get());
        detachedParts.push_back(std::move(detached));
        parts.push_back(std::move(part));
    }

    auto mesh = std::make_shared<MeshResource>();
    mesh->parentGame = device->parentGame;
    mesh->supportsThreeD = device->value->SupportsCapability(
        CNA::GraphicsCapability::ThreeD);
    mesh->parts = parts;
    for (std::size_t index = 0U; index < mesh->parts.size(); ++index) {
        mesh->parts[index]->detachedValue = detachedParts[index];
        mesh->parts[index]->parentMesh = mesh.get();
    }
    mesh->value = named
        ? std::make_shared<ModelMesh>(device->value, name, std::move(nativeParts))
        : std::make_shared<ModelMesh>(device->value, std::move(nativeParts));
    AddOwnedGraphicsResource();
    mesh->countedAsGraphicsResource = true;
    return CreateMeshHandle(std::move(mesh), outMesh);
}

[[nodiscard]] CNA_Result CreateModel(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ModelBoneHandle* const boneHandles,
    const uint64_t boneCount,
    const CNA_ModelMeshHandle* const meshHandles,
    const uint64_t meshCount,
    const CNA_ModelBoneHandle* const meshParentHandles,
    const uint64_t meshParentCount,
    const uint64_t rootBoneIndex,
    const bool explicitParents,
    CNA_ModelHandle* const outModel)
{
    std::size_t ignoredBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            boneHandles, boneCount, sizeof(*boneHandles), &ignoredBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The Model bone-handle array is invalid or too large.");
    }
    if (const CNA_Result result = CheckedElementByteCount(
            meshHandles, meshCount, sizeof(*meshHandles), &ignoredBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The Model mesh-handle array is invalid or too large.");
    }
    if (const CNA_Result result = CheckedElementByteCount(
            meshParentHandles, meshParentCount, sizeof(*meshParentHandles), &ignoredBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The Model mesh-parent array is invalid or too large.");
    }
    if (meshParentCount != 0U && meshParentCount != meshCount) {
        return InvalidArgument(
            "The Model mesh-parent count must be zero or equal the mesh count.");
    }
    if (boneCount != 0U && rootBoneIndex >= boneCount) {
        return InvalidArgument("The Model root-bone index is outside the valid range.");
    }

    std::shared_ptr<CNA::C::Detail::BorrowedGraphicsDevice> device;
    if (const CNA_Result result = GetBorrowedGraphicsDevice(
            graphicsDeviceHandle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    auto model = std::make_shared<ModelResource>();
    if (boneCount > model->bones.max_size() || meshCount > model->meshes.max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The Model collection count is too large.");
    }
    model->bones.reserve(static_cast<std::size_t>(boneCount));
    model->meshes.reserve(static_cast<std::size_t>(meshCount));
    std::vector<ModelBone*> nativeBones;
    std::vector<ModelMesh*> nativeMeshes;
    std::vector<ModelBone*> nativeParents;
    std::vector<std::shared_ptr<BoneNode>> parentNodes;
    nativeBones.reserve(static_cast<std::size_t>(boneCount));
    nativeMeshes.reserve(static_cast<std::size_t>(meshCount));
    nativeParents.reserve(static_cast<std::size_t>(meshParentCount));
    parentNodes.reserve(static_cast<std::size_t>(meshParentCount));

    for (uint64_t index = 0U; index < boneCount; ++index) {
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandles[index], &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        nativeBones.push_back(bone->node->value.get());
        model->bones.push_back(bone->node);
    }
    for (uint64_t index = 0U; index < meshCount; ++index) {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandles[index], &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (mesh->parentGame != device->parentGame) {
            return InvalidArgument(
                "Every Model mesh must belong to the supplied graphics device.");
        }
        nativeMeshes.push_back(mesh->value.get());
        model->meshes.push_back(std::move(mesh));
    }
    for (uint64_t index = 0U; index < meshParentCount; ++index) {
        if (meshParentHandles[index] == CNA_INVALID_HANDLE) {
            nativeParents.push_back(nullptr);
            parentNodes.push_back(nullptr);
            continue;
        }
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(meshParentHandles[index], &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        nativeParents.push_back(bone->node->value.get());
        parentNodes.push_back(bone->node);
    }

    model->supportsThreeD = device->value->SupportsCapability(
        CNA::GraphicsCapability::ThreeD);
    model->value = explicitParents
        ? std::make_shared<Model>(
            device->value,
            std::move(nativeBones),
            std::move(nativeMeshes),
            std::move(nativeParents),
            static_cast<std::size_t>(rootBoneIndex))
        : std::make_shared<Model>(
            device->value, std::move(nativeBones), std::move(nativeMeshes));
    if (!model->bones.empty()) {
        model->root = model->bones[static_cast<std::size_t>(
            explicitParents ? rootBoneIndex : 0U)];
    }
    if (meshParentCount != 0U) {
        for (std::size_t index = 0U; index < model->meshes.size(); ++index) {
            model->meshes[index]->parentBone = parentNodes[index];
        }
    }
    return CreateModelHandle(std::move(model), outModel);
}

[[nodiscard]] CNA_Result CopyMorphDataDescriptor(
    const CNA_MorphTargetDataEXTDescriptor& descriptor,
    MorphTargetDataEXT* const outData)
{
    MorphTargetDataEXT copied;
    copied.Stride = descriptor.stride;
    if (const CNA_Result result = CopyInputArray(
            descriptor.base_vertex_bytes, descriptor.base_vertex_byte_count,
            &copied.BaseVertexBytes,
            "The MorphTargetDataEXT base-vertex array is invalid or too large.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::size_t targetBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            descriptor.targets, descriptor.target_count,
            sizeof(CNA_MorphTargetDeltaEXTDescriptor), &targetBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The MorphTargetDataEXT target array is invalid or too large.");
    }
    static_cast<void>(targetBytes);
    if (descriptor.target_count > copied.PositionDeltas.max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The MorphTargetDataEXT target count is too large.");
    }
    copied.PositionDeltas.reserve(static_cast<std::size_t>(descriptor.target_count));
    copied.NormalDeltas.reserve(static_cast<std::size_t>(descriptor.target_count));
    for (uint64_t target = 0U; target < descriptor.target_count; ++target) {
        const CNA_MorphTargetDeltaEXTDescriptor& input = descriptor.targets[target];
        std::size_t ignoredBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                input.position_deltas, input.position_delta_count,
                sizeof(CNA_Vector3), &ignoredBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "A MorphTargetDataEXT position-delta array is invalid or too large.");
        }
        if (const CNA_Result result = CheckedElementByteCount(
                input.normal_deltas, input.normal_delta_count,
                sizeof(CNA_Vector3), &ignoredBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "A MorphTargetDataEXT normal-delta array is invalid or too large.");
        }
        std::vector<Microsoft::Xna::Framework::Vector3> positions;
        std::vector<Microsoft::Xna::Framework::Vector3> normals;
        positions.reserve(static_cast<std::size_t>(input.position_delta_count));
        normals.reserve(static_cast<std::size_t>(input.normal_delta_count));
        for (uint64_t index = 0U; index < input.position_delta_count; ++index) {
            positions.push_back(ToNativeVector3(input.position_deltas[index]));
        }
        for (uint64_t index = 0U; index < input.normal_delta_count; ++index) {
            normals.push_back(ToNativeVector3(input.normal_deltas[index]));
        }
        copied.PositionDeltas.push_back(std::move(positions));
        copied.NormalDeltas.push_back(std::move(normals));
    }
    if (const CNA_Result result = CopyInputArray(
            descriptor.weights, descriptor.weight_count, &copied.Weights,
            "The MorphTargetDataEXT weight array is invalid or too large.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (descriptor.target_count > static_cast<uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The MorphTargetDataEXT target count is too large.");
    }
    if (const CNA_Result result = CopyTrackDescriptor(
            descriptor.weight_track,
            static_cast<std::size_t>(descriptor.target_count), true,
            &copied.WeightTrack);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateMorphShape(copied, copied.Weights);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outData = std::move(copied);
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_model_bone_create_default(CNA_ModelBoneHandle* const outBone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBone == nullptr) {
            return InvalidArgument("The ModelBone output handle is null.");
        }
        *outBone = CNA_INVALID_HANDLE;
        auto node = std::make_shared<BoneNode>();
        node->value = std::make_shared<ModelBone>();
        return CreateBoneHandle(std::move(node), outBone);
    });
}

CNA_Result cna_model_bone_create(
    const int32_t index,
    const CNA_StringView name,
    CNA_ModelBoneHandle* const outBone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBone == nullptr) {
            return InvalidArgument("The ModelBone output handle is null.");
        }
        *outBone = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelBone name is not valid UTF-8 text.");
        }
        auto node = std::make_shared<BoneNode>();
        node->value = std::make_shared<ModelBone>(
            static_cast<int>(index), std::move(copiedName));
        return CreateBoneHandle(std::move(node), outBone);
    });
}

CNA_Result cna_model_bone_destroy(const CNA_ModelBoneHandle boneHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(boneHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelBone handle could not be released.");
    });
}

CNA_Result cna_model_bone_get_name_byte_count(
    const CNA_ModelBoneHandle boneHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The ModelBone name byte-count output is null.");
        }
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(
            bone->node->value->getNameProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_copy_name(
    const CNA_ModelBoneHandle boneHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBoneName(
            bone->node->value->getNameProperty(),
            destination,
            capacity,
            outByteCount);
    });
}

CNA_Result cna_model_bone_get_index(
    const CNA_ModelBoneHandle boneHandle,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidArgument("The ModelBone index output is null.");
        }
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(bone->node->value->getIndexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_get_transform(
    const CNA_ModelBoneHandle boneHandle,
    CNA_Matrix* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTransform == nullptr) {
            return InvalidArgument("The ModelBone transform output is null.");
        }
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTransform = ToC(bone->node->value->getTransformProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_set_transform(
    const CNA_ModelBoneHandle boneHandle,
    const CNA_Matrix transform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        bone->node->value->setTransformProperty(ToNative(transform));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_get_parent(
    const CNA_ModelBoneHandle boneHandle,
    CNA_Bool* const outHasParent,
    CNA_ModelBoneHandle* const outParent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasParent == nullptr || outParent == nullptr) {
            return InvalidArgument("The ModelBone parent outputs are null.");
        }
        *outHasParent = CNA_FALSE;
        *outParent = CNA_INVALID_HANDLE;
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BoneNode> parent = bone->node->parent.lock();
        if (parent == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateBoneHandle(std::move(parent), outParent);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasParent = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_get_children(
    const CNA_ModelBoneHandle boneHandle,
    CNA_ModelBoneCollectionHandle* const outChildren)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChildren == nullptr) {
            return InvalidArgument("The ModelBone children output handle is null.");
        }
        *outChildren = CNA_INVALID_HANDLE;
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateCollectionHandle(
            std::make_shared<BoneCollectionResource>(
                BoneCollectionResource{nullptr, bone->node}),
            outChildren);
    });
}

CNA_Result cna_model_bone_add_child(
    const CNA_ModelBoneHandle boneHandle,
    const CNA_ModelBoneHandle childHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BoneResource> bone;
        std::shared_ptr<BoneResource> child;
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetBone(childHandle, &child);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        for (std::shared_ptr<BoneNode> ancestor = bone->node;
             ancestor != nullptr;
             ancestor = ancestor->parent.lock()) {
            if (ancestor == child->node) {
                return InvalidArgument(
                    "A ModelBone cannot be added as itself or create an ancestor cycle.");
            }
        }
        bone->node->value->AddChild(child->node->value.get());
        bone->node->children.push_back(child->node);
        child->node->parent = bone->node;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_collection_create(
    CNA_ModelBoneCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The ModelBoneCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        return CreateCollectionHandle(
            std::make_shared<BoneCollectionResource>(BoneCollectionResource{
                std::make_shared<ModelBoneCollection>(), nullptr}),
            outCollection);
    });
}

CNA_Result cna_model_bone_collection_destroy(
    const CNA_ModelBoneCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BoneCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelBoneCollection handle could not be released.");
    });
}

CNA_Result cna_model_bone_collection_get_count(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The ModelBoneCollection count output is null.");
        }
        std::shared_ptr<BoneCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(CollectionNodes(*collection).size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_collection_get_at(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_ModelBoneHandle* const outBone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBone == nullptr) {
            return InvalidArgument("The ModelBoneCollection bone output is null.");
        }
        *outBone = CNA_INVALID_HANDLE;
        std::shared_ptr<BoneCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& nodes = CollectionNodes(*collection);
        if (index >= nodes.size()) {
            return InvalidArgument("The ModelBoneCollection index is outside the valid range.");
        }
        return CreateBoneHandle(
            nodes[static_cast<std::size_t>(index)], outBone);
    });
}

CNA_Result cna_model_bone_collection_find(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_ModelBoneHandle* const outBone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outBone == nullptr) {
            return InvalidArgument("The ModelBoneCollection find outputs are null.");
        }
        *outFound = CNA_FALSE;
        *outBone = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelBone lookup name is not valid UTF-8 text.");
        }
        std::shared_ptr<BoneCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        for (const std::shared_ptr<BoneNode>& node : CollectionNodes(*collection)) {
            if (node->value->getNameProperty() == copiedName) {
                if (const CNA_Result result = CreateBoneHandle(node, outBone);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                *outFound = CNA_TRUE;
                return CNA_RESULT_SUCCESS;
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_bone_collection_contains(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    const CNA_ModelBoneHandle boneHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The ModelBoneCollection contains output is null.");
        }
        std::shared_ptr<BoneCollectionResource> collection;
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetBone(boneHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = std::find(
            CollectionNodes(*collection).begin(),
            CollectionNodes(*collection).end(),
            bone->node) != CollectionNodes(*collection).end() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_create_default(
    CNA_ModelMeshPartHandle* const outPart)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPart == nullptr) {
            return InvalidArgument("The ModelMeshPart output handle is null.");
        }
        *outPart = CNA_INVALID_HANDLE;
        return CreatePartHandle(std::make_shared<PartResource>(), outPart);
    });
}

CNA_Result cna_model_mesh_part_create(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_IndexBufferHandle indexBufferHandle,
    const int32_t numVertices,
    const int32_t primitiveCount,
    const int32_t startIndex,
    const int32_t vertexOffset,
    CNA_ModelMeshPartHandle* const outPart)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPart == nullptr) {
            return InvalidArgument("The ModelMeshPart output handle is null.");
        }
        *outPart = CNA_INVALID_HANDLE;
        const auto part = std::make_shared<PartResource>();
        if (const CNA_Result result = SetPartVertexBuffer(part, vertexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = SetPartIndexBuffer(part, indexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->SetNumVertices(static_cast<int>(numVertices));
        part->value->SetPrimitiveCount(static_cast<int>(primitiveCount));
        part->value->SetStartIndex(static_cast<int>(startIndex));
        part->value->SetVertexOffset(static_cast<int>(vertexOffset));
        return CreatePartHandle(part, outPart);
    });
}

CNA_Result cna_model_mesh_part_destroy(const CNA_ModelMeshPartHandle partHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(partHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelMeshPart handle could not be released.");
    });
}

CNA_Result cna_model_mesh_part_get_num_vertices(
    const CNA_ModelMeshPartHandle partHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMeshPart vertex-count output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(part->value->getNumVerticesProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_num_vertices(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->SetNumVertices(static_cast<int>(value));
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetNumVertices(static_cast<int>(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_primitive_count(
    const CNA_ModelMeshPartHandle partHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMeshPart primitive-count output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(part->value->getPrimitiveCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_primitive_count(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->SetPrimitiveCount(static_cast<int>(value));
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetPrimitiveCount(static_cast<int>(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_start_index(
    const CNA_ModelMeshPartHandle partHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMeshPart start-index output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(part->value->getStartIndexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_start_index(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->SetStartIndex(static_cast<int>(value));
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetStartIndex(static_cast<int>(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_vertex_offset(
    const CNA_ModelMeshPartHandle partHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMeshPart vertex-offset output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(part->value->getVertexOffsetProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_vertex_offset(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->SetVertexOffset(static_cast<int>(value));
        if (part->detachedValue != nullptr) {
            part->detachedValue->SetVertexOffset(static_cast<int>(value));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_effect(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasEffect,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasEffect == nullptr || outEffect == nullptr) {
            return InvalidArgument("The ModelMeshPart Effect outputs are null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasEffect = part->effect.handle == CNA_INVALID_HANDLE ? CNA_FALSE : CNA_TRUE;
        *outEffect = part->effect.handle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_effect(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetPartEffect(part, effectHandle);
    });
}

CNA_Result cna_model_mesh_part_get_vertex_buffer(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasBuffer,
    CNA_VertexBufferHandle* const outBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasBuffer == nullptr || outBuffer == nullptr) {
            return InvalidArgument("The ModelMeshPart VertexBuffer outputs are null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasBuffer = part->vertexBuffer.handle == CNA_INVALID_HANDLE
            ? CNA_FALSE
            : CNA_TRUE;
        *outBuffer = part->vertexBuffer.handle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_vertex_buffer(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_VertexBufferHandle vertexBufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetPartVertexBuffer(part, vertexBufferHandle);
    });
}

CNA_Result cna_model_mesh_part_get_index_buffer(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasBuffer,
    CNA_IndexBufferHandle* const outBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasBuffer == nullptr || outBuffer == nullptr) {
            return InvalidArgument("The ModelMeshPart IndexBuffer outputs are null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasBuffer = part->indexBuffer.handle == CNA_INVALID_HANDLE
            ? CNA_FALSE
            : CNA_TRUE;
        *outBuffer = part->indexBuffer.handle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_index_buffer(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_IndexBufferHandle indexBufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SetPartIndexBuffer(part, indexBufferHandle);
    });
}

CNA_Result cna_model_mesh_part_get_tag(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_ModelMeshPartTag* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return InvalidArgument("The ModelMeshPart tag output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTag = part->tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_tag(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_ModelMeshPartTag tag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->tag = tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_collection_create(
    const CNA_ModelMeshPartHandle* const partHandles,
    const uint64_t partCount,
    CNA_ModelMeshPartCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The ModelMeshPartCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                partHandles, partCount, sizeof(*partHandles), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelMeshPart handle array is invalid or too large.");
        }
        static_cast<void>(byteCount);
        auto collection = std::make_shared<PartCollectionResource>();
        if (partCount > collection->parts.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The ModelMeshPart collection is too large.");
        }
        collection->parts.reserve(static_cast<std::size_t>(partCount));
        for (uint64_t index = 0U; index < partCount; ++index) {
            std::shared_ptr<PartResource> part;
            if (const CNA_Result result = GetPart(partHandles[index], &part);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            collection->parts.push_back(std::move(part));
        }
        return CreatePartCollectionHandle(std::move(collection), outCollection);
    });
}

CNA_Result cna_model_mesh_part_collection_destroy(
    const CNA_ModelMeshPartCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartCollectionResource> collection;
        if (const CNA_Result result = GetPartCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelMeshPartCollection handle could not be released.");
    });
}

CNA_Result cna_model_mesh_part_collection_get_count(
    const CNA_ModelMeshPartCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The ModelMeshPartCollection count output is null.");
        }
        std::shared_ptr<PartCollectionResource> collection;
        if (const CNA_Result result = GetPartCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(PartList(*collection).size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_collection_get_at(
    const CNA_ModelMeshPartCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_ModelMeshPartHandle* const outPart)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPart == nullptr) {
            return InvalidArgument("The ModelMeshPartCollection part output is null.");
        }
        *outPart = CNA_INVALID_HANDLE;
        std::shared_ptr<PartCollectionResource> collection;
        if (const CNA_Result result = GetPartCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& parts = PartList(*collection);
        if (index >= parts.size()) {
            return InvalidArgument(
                "The ModelMeshPartCollection index is outside the valid range.");
        }
        return CreatePartHandle(
            parts[static_cast<std::size_t>(index)], outPart);
    });
}

CNA_Result cna_model_mesh_create(
    const CNA_Handle graphicsDevice,
    const CNA_ModelMeshPartHandle* const parts,
    const uint64_t partCount,
    CNA_ModelMeshHandle* const outMesh)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMesh == nullptr) {
            return InvalidArgument("The ModelMesh output handle is null.");
        }
        *outMesh = CNA_INVALID_HANDLE;
        return CreateMesh(
            graphicsDevice, std::string{}, false, parts, partCount, outMesh);
    });
}

CNA_Result cna_model_mesh_create_named(
    const CNA_Handle graphicsDevice,
    const CNA_StringView name,
    const CNA_ModelMeshPartHandle* const parts,
    const uint64_t partCount,
    CNA_ModelMeshHandle* const outMesh)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMesh == nullptr) {
            return InvalidArgument("The ModelMesh output handle is null.");
        }
        *outMesh = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelMesh name is not valid UTF-8 text.");
        }
        return CreateMesh(
            graphicsDevice, copiedName, true, parts, partCount, outMesh);
    });
}

CNA_Result cna_model_mesh_destroy(const CNA_ModelMeshHandle meshHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(meshHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelMesh handle could not be released.");
    });
}

CNA_Result cna_model_mesh_get_bounding_sphere(
    const CNA_ModelMeshHandle meshHandle,
    CNA_BoundingSphere* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMesh bounding-sphere output is null.");
        }
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(mesh->value->getBoundingSphereProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_set_bounding_sphere(
    const CNA_ModelMeshHandle meshHandle,
    const CNA_BoundingSphere value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        mesh->value->setBoundingSphereProperty(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_get_mesh_parts(
    const CNA_ModelMeshHandle meshHandle,
    CNA_ModelMeshPartCollectionHandle* const outParts)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outParts == nullptr) {
            return InvalidArgument("The ModelMesh part-collection output is null.");
        }
        *outParts = CNA_INVALID_HANDLE;
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto collection = std::make_shared<PartCollectionResource>();
        collection->mesh = std::move(mesh);
        return CreatePartCollectionHandle(std::move(collection), outParts);
    });
}

CNA_Result cna_model_mesh_get_effects(
    const CNA_ModelMeshHandle meshHandle,
    CNA_ModelEffectCollectionHandle* const outEffects)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffects == nullptr) {
            return InvalidArgument("The ModelMesh effect-collection output is null.");
        }
        *outEffects = CNA_INVALID_HANDLE;
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateEffectCollectionHandle(std::move(mesh), outEffects);
    });
}

CNA_Result cna_model_mesh_get_name_byte_count(
    const CNA_ModelMeshHandle meshHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The ModelMesh name byte-count output is null.");
        }
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(mesh->value->getNameProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_copy_name(
    const CNA_ModelMeshHandle meshHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyMeshName(
            mesh->value->getNameProperty(), destination, capacity, outByteCount);
    });
}

CNA_Result cna_model_mesh_get_parent_bone(
    const CNA_ModelMeshHandle meshHandle,
    CNA_Bool* const outHasParent,
    CNA_ModelBoneHandle* const outParent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasParent == nullptr || outParent == nullptr) {
            return InvalidArgument("The ModelMesh parent-bone outputs are null.");
        }
        *outHasParent = CNA_FALSE;
        *outParent = CNA_INVALID_HANDLE;
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (mesh->parentBone == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateBoneHandle(mesh->parentBone, outParent);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasParent = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_set_parent_bone(
    const CNA_ModelMeshHandle meshHandle,
    const CNA_ModelBoneHandle parentHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (parentHandle == CNA_INVALID_HANDLE) {
            mesh->value->setParentBoneProperty(nullptr);
            mesh->parentBone.reset();
            return CNA_RESULT_SUCCESS;
        }
        std::shared_ptr<BoneResource> bone;
        if (const CNA_Result result = GetBone(parentHandle, &bone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        mesh->value->setParentBoneProperty(bone->node->value.get());
        mesh->parentBone = bone->node;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_get_tag(
    const CNA_ModelMeshHandle meshHandle,
    CNA_ModelMeshTag* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return InvalidArgument("The ModelMesh tag output is null.");
        }
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTag = mesh->tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_set_tag(
    const CNA_ModelMeshHandle meshHandle,
    const CNA_ModelMeshTag tag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        mesh->tag = tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_draw(const CNA_ModelMeshHandle meshHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!mesh->supportsThreeD) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "ModelMesh drawing requires a renderer with 3D support.");
        }
        mesh->value->Draw();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_collection_create(
    const CNA_ModelMeshHandle* const meshHandles,
    const uint64_t meshCount,
    CNA_ModelMeshCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The ModelMeshCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                meshHandles, meshCount, sizeof(*meshHandles), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelMesh handle array is invalid or too large.");
        }
        static_cast<void>(byteCount);
        auto collection = std::make_shared<MeshCollectionResource>();
        if (meshCount > collection->meshes.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The ModelMesh collection is too large.");
        }
        collection->meshes.reserve(static_cast<std::size_t>(meshCount));
        for (uint64_t index = 0U; index < meshCount; ++index) {
            std::shared_ptr<MeshResource> mesh;
            if (const CNA_Result result = GetMesh(meshHandles[index], &mesh);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            collection->meshes.push_back(std::move(mesh));
        }
        return CreateMeshCollectionHandle(std::move(collection), outCollection);
    });
}

CNA_Result cna_model_mesh_collection_destroy(
    const CNA_ModelMeshCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MeshCollectionResource> collection;
        if (const CNA_Result result = GetMeshCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelMeshCollection handle could not be released.");
    });
}

CNA_Result cna_model_mesh_collection_get_count(
    const CNA_ModelMeshCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The ModelMeshCollection count output is null.");
        }
        std::shared_ptr<MeshCollectionResource> collection;
        if (const CNA_Result result = GetMeshCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->meshes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_collection_get_at(
    const CNA_ModelMeshCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_ModelMeshHandle* const outMesh)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMesh == nullptr) {
            return InvalidArgument("The ModelMeshCollection mesh output is null.");
        }
        *outMesh = CNA_INVALID_HANDLE;
        std::shared_ptr<MeshCollectionResource> collection;
        if (const CNA_Result result = GetMeshCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= collection->meshes.size()) {
            return InvalidArgument("The ModelMeshCollection index is outside the valid range.");
        }
        return CreateMeshHandle(
            collection->meshes[static_cast<std::size_t>(index)], outMesh);
    });
}

CNA_Result cna_model_mesh_collection_find(
    const CNA_ModelMeshCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_ModelMeshHandle* const outMesh)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outMesh == nullptr) {
            return InvalidArgument("The ModelMeshCollection find outputs are null.");
        }
        *outFound = CNA_FALSE;
        *outMesh = CNA_INVALID_HANDLE;
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ModelMesh lookup name is not valid UTF-8 text.");
        }
        std::shared_ptr<MeshCollectionResource> collection;
        if (const CNA_Result result = GetMeshCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto iterator = std::find_if(
            collection->meshes.begin(),
            collection->meshes.end(),
            [&copiedName](const std::shared_ptr<MeshResource>& mesh) {
                return mesh->value->getNameProperty() == copiedName;
            });
        if (iterator == collection->meshes.end()) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateMeshHandle(*iterator, outMesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFound = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_collection_contains(
    const CNA_ModelMeshCollectionHandle collectionHandle,
    const CNA_ModelMeshHandle meshHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The ModelMeshCollection contains output is null.");
        }
        std::shared_ptr<MeshCollectionResource> collection;
        std::shared_ptr<MeshResource> mesh;
        if (const CNA_Result result = GetMeshCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetMesh(meshHandle, &mesh);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = std::find(
            collection->meshes.begin(), collection->meshes.end(), mesh) !=
                collection->meshes.end()
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_effect_collection_destroy(
    const CNA_ModelEffectCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectCollectionResource> collection;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ModelEffectCollection handle could not be released.");
    });
}

CNA_Result cna_model_effect_collection_get_count(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The ModelEffectCollection count output is null.");
        }
        std::shared_ptr<EffectCollectionResource> collection;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->mesh->effects.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_effect_collection_get_at(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The ModelEffectCollection effect output is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<EffectCollectionResource> collection;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= collection->mesh->effects.size()) {
            return InvalidArgument("The ModelEffectCollection index is outside the valid range.");
        }
        *outEffect = collection->mesh->effects[static_cast<std::size_t>(index)].handle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_effect_collection_contains(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const CNA_EffectHandle effectHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The ModelEffectCollection contains output is null.");
        }
        std::shared_ptr<EffectCollectionResource> collection;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ResolveEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = FindMeshEffect(*collection->mesh, effectHandle) !=
                collection->mesh->effects.end()
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_effect_collection_add(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectCollectionResource> collection;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ResolveEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (effect->parentGame != collection->mesh->parentGame) {
            return InvalidArgument(
                "A ModelEffectCollection Effect must belong to its mesh graphics device.");
        }
        if (effect->activeModelReferenceCount == std::numeric_limits<uint64_t>::max()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The ModelEffectCollection retention count overflowed.");
        }
        collection->mesh->effects.reserve(collection->mesh->effects.size() + 1U);
        collection->mesh->value->getEffectsPropertyMutable().Add(effect->value.get());
        AddMeshEffect(*collection->mesh, effectHandle, effect);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_effect_collection_remove(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectCollectionResource> collection;
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffectCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ResolveEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->mesh->value->getEffectsPropertyMutable().Remove(effect->value.get());
        RemoveFirstMeshEffect(*collection->mesh, effectHandle);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_create_default(CNA_ModelHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The Model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        auto model = std::make_shared<ModelResource>();
        model->value = std::make_shared<Model>();
        return CreateModelHandle(std::move(model), outModel);
    });
}

CNA_Result cna_model_create(
    const CNA_Handle graphicsDevice,
    const CNA_ModelBoneHandle* const bones,
    const uint64_t boneCount,
    const CNA_ModelMeshHandle* const meshes,
    const uint64_t meshCount,
    CNA_ModelHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The Model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        return CreateModel(
            graphicsDevice, bones, boneCount, meshes, meshCount,
            nullptr, 0U, 0U, false, outModel);
    });
}

CNA_Result cna_model_create_with_parents(
    const CNA_Handle graphicsDevice,
    const CNA_ModelBoneHandle* const bones,
    const uint64_t boneCount,
    const CNA_ModelMeshHandle* const meshes,
    const uint64_t meshCount,
    const CNA_ModelBoneHandle* const meshParents,
    const uint64_t meshParentCount,
    const uint64_t rootBoneIndex,
    CNA_ModelHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The Model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        return CreateModel(
            graphicsDevice, bones, boneCount, meshes, meshCount,
            meshParents, meshParentCount, rootBoneIndex, true, outModel);
    });
}

CNA_Result cna_model_destroy(const CNA_ModelHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(modelHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Model handle could not be released.");
    });
}

CNA_Result cna_model_get_bones(
    const CNA_ModelHandle modelHandle,
    CNA_ModelBoneCollectionHandle* const outBones)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBones == nullptr) {
            return InvalidArgument("The Model bone-collection output is null.");
        }
        *outBones = CNA_INVALID_HANDLE;
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto collection = std::make_shared<BoneCollectionResource>();
        collection->nodes = model->bones;
        return CreateCollectionHandle(std::move(collection), outBones);
    });
}

CNA_Result cna_model_get_meshes(
    const CNA_ModelHandle modelHandle,
    CNA_ModelMeshCollectionHandle* const outMeshes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMeshes == nullptr) {
            return InvalidArgument("The Model mesh-collection output is null.");
        }
        *outMeshes = CNA_INVALID_HANDLE;
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto collection = std::make_shared<MeshCollectionResource>();
        collection->meshes = model->meshes;
        return CreateMeshCollectionHandle(std::move(collection), outMeshes);
    });
}

CNA_Result cna_model_get_root(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasRoot,
    CNA_ModelBoneHandle* const outRoot)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasRoot == nullptr || outRoot == nullptr) {
            return InvalidArgument("The Model root outputs are null.");
        }
        *outHasRoot = CNA_FALSE;
        *outRoot = CNA_INVALID_HANDLE;
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (model->root == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateBoneHandle(model->root, outRoot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasRoot = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_tag(
    const CNA_ModelHandle modelHandle,
    CNA_ModelTag* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return InvalidArgument("The Model tag output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTag = model->tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_set_tag(
    const CNA_ModelHandle modelHandle,
    const CNA_ModelTag tag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->tag = tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_set_owned_resources(
    const CNA_ModelHandle modelHandle,
    void* const context,
    const CNA_ModelOwnedResourcesReleaseCallback release)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if ((context == nullptr) != (release == nullptr)) {
            return InvalidArgument(
                "Model owned resources require both context and release callback, or neither.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<void> owner;
        if (context != nullptr) {
            owner = std::shared_ptr<void>(
                context,
                [release](void* const value) noexcept { release(value); });
        }
        model->value->setOwnedResources(std::move(owner));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_bone_transform_count(
    const CNA_ModelHandle modelHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The Model bone-transform count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(model->bones.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_absolute_bone_transforms(
    const CNA_ModelHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The Model absolute-transform output is invalid.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(model->bones.size());
        if (capacity < model->bones.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all absolute Model bone transforms.");
        }
        std::vector<Matrix> transforms(model->bones.size());
        model->value->CopyAbsoluteBoneTransformsTo(transforms);
        for (std::size_t index = 0U; index < transforms.size(); ++index) {
            destination[index] = ToC(transforms[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_set_bone_transforms(
    const CNA_ModelHandle modelHandle,
    const CNA_Matrix* const source,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t byteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                source, count, sizeof(*source), &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The Model source-transform array is invalid or too large.");
        }
        static_cast<void>(byteCount);
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (count < model->bones.size()) {
            return InvalidArgument(
                "The source array does not cover all Model bone transforms.");
        }
        std::vector<Matrix> transforms;
        transforms.reserve(model->bones.size());
        for (std::size_t index = 0U; index < model->bones.size(); ++index) {
            transforms.push_back(ToNative(source[index]));
        }
        model->value->CopyBoneTransformsFrom(transforms);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_bone_transforms(
    const CNA_ModelHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The Model local-transform output is invalid.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(model->bones.size());
        if (capacity < model->bones.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all local Model bone transforms.");
        }
        std::vector<Matrix> transforms(model->bones.size());
        model->value->CopyBoneTransformsTo(transforms);
        for (std::size_t index = 0U; index < transforms.size(); ++index) {
            destination[index] = ToC(transforms[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_draw(
    const CNA_ModelHandle modelHandle,
    const CNA_Matrix world,
    const CNA_Matrix view,
    const CNA_Matrix projection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!model->meshes.empty() && !model->supportsThreeD) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "Model drawing requires a renderer with 3D support.");
        }
        model->value->Draw(
            ToNative(world), ToNative(view), ToNative(projection));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_create(
    const CNA_MorphTargetDataEXTDescriptor* const descriptor,
    CNA_MorphTargetDataEXTHandle* const outData)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (descriptor == nullptr || outData == nullptr) {
            return InvalidArgument("The MorphTargetDataEXT descriptor or output is null.");
        }
        *outData = CNA_INVALID_HANDLE;
        MorphTargetDataEXT copied;
        if (const CNA_Result result = CopyMorphDataDescriptor(*descriptor, &copied);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<MorphDataResource>();
        resource->value = std::make_shared<MorphTargetDataEXT>(std::move(copied));
        return CreateMorphDataHandle(std::move(resource), outData);
    });
}

CNA_Result cna_morph_target_data_ext_destroy(
    const CNA_MorphTargetDataEXTHandle dataHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(dataHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned MorphTargetDataEXT handle could not be released.");
    });
}

CNA_Result cna_morph_target_data_ext_get_type_name_byte_count(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The MorphTargetDataEXT type-name count output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(data->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_type_name(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The MorphTargetDataEXT type-name output is invalid.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string& name = data->value->GetTypeName();
        *outByteCount = static_cast<uint64_t>(name.size());
        if (capacity < name.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the MorphTargetDataEXT type name.");
        }
        if (!name.empty()) {
            std::memcpy(destination, name.data(), name.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_get_stride(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    int32_t* const outStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outStride == nullptr) {
            return InvalidArgument("The MorphTargetDataEXT stride output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outStride = data->value->Stride;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_get_base_vertex_byte_count(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The MorphTargetDataEXT base-byte count output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(data->value->BaseVertexBytes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_base_vertex_bytes(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The MorphTargetDataEXT base-byte output is invalid.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& bytes = data->value->BaseVertexBytes;
        *outByteCount = static_cast<uint64_t>(bytes.size());
        if (capacity < bytes.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all MorphTargetDataEXT base bytes.");
        }
        if (!bytes.empty()) {
            std::memcpy(destination, bytes.data(), bytes.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_get_target_count(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint64_t* const outTargetCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTargetCount == nullptr) {
            return InvalidArgument("The MorphTargetDataEXT target-count output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTargetCount = static_cast<uint64_t>(data->value->PositionDeltas.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_position_deltas(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t targetIndex,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outDeltaCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (targetIndex >= data->value->PositionDeltas.size()) {
            return InvalidArgument("The MorphTargetDataEXT target index is outside the valid range.");
        }
        return CopyOutputValues(
            data->value->PositionDeltas[static_cast<std::size_t>(targetIndex)],
            destination, capacity, outDeltaCount, ToCVector3,
            "The MorphTargetDataEXT position-delta output is invalid.",
            "The destination cannot hold all morph position deltas.");
    });
}

CNA_Result cna_morph_target_data_ext_copy_normal_deltas(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t targetIndex,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outDeltaCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (targetIndex >= data->value->NormalDeltas.size()) {
            return InvalidArgument("The MorphTargetDataEXT target index is outside the valid range.");
        }
        return CopyOutputValues(
            data->value->NormalDeltas[static_cast<std::size_t>(targetIndex)],
            destination, capacity, outDeltaCount, ToCVector3,
            "The MorphTargetDataEXT normal-delta output is invalid.",
            "The destination cannot hold all morph normal deltas.");
    });
}

CNA_Result cna_morph_target_data_ext_set_weights(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const float* const weights,
    const uint64_t weightCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::vector<float> copied;
        if (const CNA_Result result = CopyInputArray(
                weights, weightCount, &copied,
                "The MorphTargetDataEXT weight array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (copied.size() != data->value->PositionDeltas.size()) {
            return InvalidArgument("The MorphTargetDataEXT weight count must equal its target count.");
        }
        data->value->Weights = std::move(copied);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_weights(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outWeightCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->Weights, destination, capacity, outWeightCount,
            [](const float value) noexcept { return value; },
            "The MorphTargetDataEXT weight output is invalid.",
            "The destination cannot hold all morph weights.");
    });
}

CNA_Result cna_morph_target_data_ext_set_weight_track(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const CNA_MorphWeightTrackEXTDescriptor* const track)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (track == nullptr) {
            return InvalidArgument("The MorphWeightTrackEXT descriptor is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        MorphWeightTrackEXT copied;
        if (const CNA_Result result = CopyTrackDescriptor(
                *track, data->value->PositionDeltas.size(), true, &copied);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        data->value->WeightTrack = std::move(copied);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_get_weight_track_info(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint64_t* const outKeyframeCount,
    CNA_Bool* const outStepInterpolation,
    CNA_Bool* const outCubicSpline)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKeyframeCount == nullptr || outStepInterpolation == nullptr ||
            outCubicSpline == nullptr) {
            return InvalidArgument("A MorphWeightTrackEXT info output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outKeyframeCount = static_cast<uint64_t>(data->value->WeightTrack.Keys.size());
        *outStepInterpolation = data->value->WeightTrack.StepInterpolation
            ? CNA_TRUE : CNA_FALSE;
        *outCubicSpline = data->value->WeightTrack.CubicSpline
            ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_weight_keyframe(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t keyframeIndex,
    double* const outTimeSeconds,
    float* const weights,
    const uint64_t weightCapacity,
    uint64_t* const outWeightCount,
    float* const inTangents,
    const uint64_t inTangentCapacity,
    uint64_t* const outInTangentCount,
    float* const outTangents,
    const uint64_t outTangentCapacity,
    uint64_t* const outOutTangentCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTimeSeconds == nullptr || outWeightCount == nullptr ||
            outInTangentCount == nullptr || outOutTangentCount == nullptr ||
            (weights == nullptr && weightCapacity != 0U) ||
            (inTangents == nullptr && inTangentCapacity != 0U) ||
            (outTangents == nullptr && outTangentCapacity != 0U)) {
            return InvalidArgument("A MorphWeightKeyframeEXT output is invalid.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (keyframeIndex >= data->value->WeightTrack.Keys.size()) {
            return InvalidArgument("The MorphWeightKeyframeEXT index is outside the valid range.");
        }
        const MorphWeightKeyframeEXT& key =
            data->value->WeightTrack.Keys[static_cast<std::size_t>(keyframeIndex)];
        *outWeightCount = static_cast<uint64_t>(key.Weights.size());
        *outInTangentCount = static_cast<uint64_t>(key.InTangent.size());
        *outOutTangentCount = static_cast<uint64_t>(key.OutTangent.size());
        if (weightCapacity < key.Weights.size() ||
            inTangentCapacity < key.InTangent.size() ||
            outTangentCapacity < key.OutTangent.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "A destination cannot hold the complete MorphWeightKeyframeEXT.");
        }
        *outTimeSeconds = key.Time.getTotalSecondsProperty();
        if (!key.Weights.empty()) {
            std::copy(key.Weights.begin(), key.Weights.end(), weights);
        }
        if (!key.InTangent.empty()) {
            std::copy(key.InTangent.begin(), key.InTangent.end(), inTangents);
        }
        if (!key.OutTangent.empty()) {
            std::copy(key.OutTangent.begin(), key.OutTangent.end(), outTangents);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_blend(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const float* const weights,
    const uint64_t weightCount,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The MorphTargetDataEXT blend output is invalid.");
        }
        std::vector<float> copiedWeights;
        if (const CNA_Result result = CopyInputArray(
                weights, weightCount, &copiedWeights,
                "The morph blend weight array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateMorphShape(*data->value, copiedWeights);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(data->value->BaseVertexBytes.size());
        if (capacity < data->value->BaseVertexBytes.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all blended morph bytes.");
        }
        const std::vector<std::uint8_t> blended =
            Microsoft::Xna::Framework::Graphics::BlendMorphTargetsEXT(
                *data->value, copiedWeights);
        if (!blended.empty()) {
            std::memcpy(destination, blended.data(), blended.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_weight_track_ext_evaluate(
    const CNA_MorphWeightTrackEXTDescriptor* const track,
    const double timeSeconds,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outWeightCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (track == nullptr || outWeightCount == nullptr ||
            (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The MorphWeightTrackEXT evaluation input or output is invalid.");
        }
        if (!std::isfinite(timeSeconds)) {
            return InvalidArgument("The morph evaluation time must be finite.");
        }
        MorphWeightTrackEXT copied;
        if (const CNA_Result result = CopyTrackDescriptor(
                *track, 0U, false, &copied);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<float> evaluated =
            Microsoft::Xna::Framework::Graphics::EvaluateMorphWeightsEXT(
                copied, timeSeconds);
        *outWeightCount = static_cast<uint64_t>(evaluated.size());
        if (capacity < evaluated.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all evaluated morph weights.");
        }
        if (!evaluated.empty()) {
            std::copy(evaluated.begin(), evaluated.end(), destination);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_morph_target_data_ext(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_MorphTargetDataEXTHandle dataHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (dataHandle == CNA_INVALID_HANDLE) {
            part->value->setTagProperty(nullptr);
            if (part->detachedValue != nullptr) {
                part->detachedValue->setTagProperty(nullptr);
            }
            part->morphData.reset();
            return CNA_RESULT_SUCCESS;
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->morphData = std::move(data);
        part->value->setTagProperty(part->morphData->value.get());
        if (part->detachedValue != nullptr) {
            part->detachedValue->setTagProperty(part->morphData->value.get());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_morph_target_data_ext(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasData,
    CNA_MorphTargetDataEXTHandle* const outData)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasData == nullptr || outData == nullptr) {
            return InvalidArgument("A ModelMeshPart morph-data output is null.");
        }
        *outHasData = CNA_FALSE;
        *outData = CNA_INVALID_HANDLE;
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (part->morphData == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateMorphDataHandle(part->morphData, outData);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasData = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_morph_weights_ext(
    const CNA_ModelMeshPartHandle partHandle,
    const float* const weights,
    const uint64_t weightCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::vector<float> copiedWeights;
        if (const CNA_Result result = CopyInputArray(
                weights, weightCount, &copiedWeights,
                "The ModelMeshPart morph weight array is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (part->morphData == nullptr ||
            part->value->getTagProperty() != part->morphData->value.get()) {
            return Fail(
                CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE,
                "The ModelMeshPart has no attached MorphTargetDataEXT.");
        }
        if (part->value->getVertexBufferProperty() == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE,
                "The ModelMeshPart has no vertex buffer for morph upload.");
        }
        if (const CNA_Result result = ValidateMorphShape(
                *part->morphData->value, copiedWeights);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::size_t vertexCount = part->morphData->value->BaseVertexBytes.size() /
            static_cast<std::size_t>(part->morphData->value->Stride);
        const int bufferVertexCount =
            part->value->getVertexBufferProperty()->getVertexCountProperty();
        if (bufferVertexCount < 0 ||
            vertexCount > static_cast<std::size_t>(bufferVertexCount)) {
            return InvalidArgument(
                "The ModelMeshPart vertex buffer cannot hold all morphed vertices.");
        }
        Microsoft::Xna::Framework::Graphics::SetMorphWeightsEXT(
            *part->value, copiedWeights);
        return CNA_RESULT_SUCCESS;
    });
}
