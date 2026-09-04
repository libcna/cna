// SPDX-License-Identifier: MS-PL

#include "CNA/C/models.h"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsStateDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Content/ObjectDictionaryEXT.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <algorithm>
#include <any>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
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
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelBoneCollection;
using Microsoft::Xna::Framework::Graphics::ModelEffectCollection;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshCollection;
using Microsoft::Xna::Framework::Graphics::ModelMeshPartCollection;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::ApplyBindPoseBoneTransformsEXT;
using Microsoft::Xna::Framework::Graphics::ApplyClipToBonesEXT;
using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;
using Microsoft::Xna::Framework::Graphics::ModelCameraEXT;
using Microsoft::Xna::Framework::Graphics::ModelSkinEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticKindEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticSeverityEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportReportEXT;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;
using Microsoft::Xna::Framework::Graphics::MorphWeightKeyframeEXT;
using Microsoft::Xna::Framework::Graphics::MorphWeightTrackEXT;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;
using Microsoft::Xna::Framework::Graphics::SkinnedModelEXT;
using Microsoft::Xna::Framework::Graphics::SkinningData;

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
    uint64_t activeSkinnedModelReferenceCount = 0U;

    ~PartResource()
    {
        if (value != nullptr) {
            value->setTagProperty(nullptr);
        }
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

struct SkinningDataResource;

struct ModelResource final {
    std::shared_ptr<Model> value;
    // ModelSkinEXT borrows its SkinningData, so the model holds a strong reference beside each
    // skin: without one, destroying the caller's handle would leave that pointer dangling.
    std::vector<std::shared_ptr<SkinningDataResource>> skinSkeletons;
    std::vector<std::shared_ptr<BoneNode>> bones;
    std::vector<std::shared_ptr<MeshResource>> meshes;
    std::shared_ptr<BoneNode> root;
    CNA_ModelTag tag = 0U;
    bool supportsThreeD = true;
    // CBIND-118: a content-loaded model publishes handles for the effects and buffers its parts
    // reference, because the part accessors answer with handles rather than with C++ pointers. The
    // caller never created them, so the model releases them when it is destroyed.
    std::vector<CNA_Handle> contentOwnedHandles;
};

struct SkinnedPartEntry final {
    std::string name;
    std::shared_ptr<PartResource> part;
    CNA_Handle textureHandle = CNA_INVALID_HANDLE;
    std::shared_ptr<Texture2DResource> texture;

    SkinnedPartEntry() = default;
    SkinnedPartEntry(const SkinnedPartEntry&) = delete;
    SkinnedPartEntry& operator=(const SkinnedPartEntry&) = delete;

    SkinnedPartEntry(SkinnedPartEntry&& other) noexcept
        : name(std::move(other.name)),
          part(std::move(other.part)),
          textureHandle(other.textureHandle),
          texture(std::move(other.texture))
    {
        other.textureHandle = CNA_INVALID_HANDLE;
    }

    SkinnedPartEntry& operator=(SkinnedPartEntry&& other) noexcept
    {
        if (this != &other) {
            Reset();
            name = std::move(other.name);
            part = std::move(other.part);
            textureHandle = other.textureHandle;
            texture = std::move(other.texture);
            other.textureHandle = CNA_INVALID_HANDLE;
        }
        return *this;
    }

    ~SkinnedPartEntry()
    {
        Reset();
    }

    void Reset() noexcept
    {
        if (part != nullptr && part->activeSkinnedModelReferenceCount != 0U) {
            --part->activeSkinnedModelReferenceCount;
        }
        if (texture != nullptr && texture->activeModelReferenceCount != 0U) {
            --texture->activeModelReferenceCount;
        }
        part.reset();
        texture.reset();
        textureHandle = CNA_INVALID_HANDLE;
    }
};

struct SkinnedModelResource final {
    std::shared_ptr<SkinnedModelEXT> value = std::make_shared<SkinnedModelEXT>();
    std::vector<SkinnedPartEntry> parts;
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
};

struct SkinningDataResource final {
    std::shared_ptr<SkinningData> value = std::make_shared<SkinningData>();
};

struct AnimationPlayerResource final {
    std::shared_ptr<SkinningDataResource> data;
    std::shared_ptr<AnimationPlayer> value;
    std::string currentClipName;
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
        // Only a part this ABI built has a standalone value to go back to. A part published by
        // MirrorLoadedModel borrows its ModelMeshPart from the Model through an aliasing
        // shared_ptr and never had a detachedValue, so moving one in would replace a live
        // pointer with an empty one -- and ~PartResource would then dereference it.
        if (part->detachedValue != nullptr) {
            part->value = std::move(part->detachedValue);
        }
    }
    if (countedAsGraphicsResource) {
        RemoveOwnedGraphicsResourceFor(parentGame);
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

[[nodiscard]] CNA_Result GetSkinnedModel(
    const CNA_SkinnedModelEXTHandle handle,
    std::shared_ptr<SkinnedModelResource>* const outModel)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::SkinnedModelEXT, outModel);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The SkinnedModelEXT handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateSkinnedModelHandle(
    std::shared_ptr<SkinnedModelResource> model,
    CNA_SkinnedModelEXTHandle* const outModel)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::SkinnedModelEXT, std::move(model), outModel);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned SkinnedModelEXT handle could not be created.");
}

[[nodiscard]] CNA_Result GetSkinningData(
    const CNA_SkinningDataHandle handle,
    std::shared_ptr<SkinningDataResource>* const outData)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::SkinningData, outData);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The SkinningData handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateSkinningDataHandle(
    std::shared_ptr<SkinningDataResource> data,
    CNA_SkinningDataHandle* const outData)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::SkinningData, std::move(data), outData);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The owned SkinningData handle could not be created.");
}

[[nodiscard]] CNA_Result GetAnimationPlayer(
    const CNA_AnimationPlayerHandle handle,
    std::shared_ptr<AnimationPlayerResource>* const outPlayer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::AnimationPlayer, outPlayer);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The AnimationPlayer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateAnimationPlayerHandle(
    std::shared_ptr<AnimationPlayerResource> player,
    CNA_AnimationPlayerHandle* const outPlayer)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::AnimationPlayer, std::move(player), outPlayer);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The owned AnimationPlayer handle could not be created.");
}

void RebuildSkinnedParts(SkinnedModelResource& model)
{
    std::vector<SkinnedModelEXT::PartEXT> rebuilt;
    rebuilt.reserve(model.parts.size());
    for (const SkinnedPartEntry& entry : model.parts) {
        rebuilt.push_back(SkinnedModelEXT::PartEXT{
            entry.name,
            entry.part == nullptr ? nullptr : entry.part->value.get(),
            entry.texture == nullptr ? nullptr : entry.texture->value.get()});
    }
    model.value->Parts = std::move(rebuilt);
}

[[nodiscard]] CNA_Result CopySkinnedName(
    const std::string& name,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    const char* const invalidMessage,
    const char* const smallMessage)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(invalidMessage);
    }
    *outByteCount = static_cast<uint64_t>(name.size());
    if (capacity < name.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, smallMessage);
    }
    if (!name.empty()) {
        std::memcpy(destination, name.data(), name.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] std::vector<std::string> SortedClipNames(
    const SkinnedModelEXT& model)
{
    std::vector<std::string> names;
    names.reserve(model.Clips.size());
    for (const auto& [name, ignored] : model.Clips) {
        static_cast<void>(ignored);
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] std::vector<std::string> SortedClipNames(
    const SkinningData& data)
{
    std::vector<std::string> names;
    names.reserve(data.AnimationClips.size());
    for (const auto& [name, ignored] : data.AnimationClips) {
        static_cast<void>(ignored);
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

constexpr double MaxTimeSpanSeconds = 922337203685.0;

[[nodiscard]] bool IsValidTimeSpanSeconds(const double value) noexcept
{
    return std::isfinite(value) && value >= -MaxTimeSpanSeconds &&
        value <= MaxTimeSpanSeconds;
}

[[nodiscard]] CNA_Result CopyAnimationClipDescriptor(
    const CNA_AnimationClipEXTDescriptor& descriptor,
    AnimationClipEXT* const outClip)
{
    if (!IsValidTimeSpanSeconds(descriptor.duration_seconds)) {
        return InvalidArgument("The AnimationClipEXT duration must fit a finite TimeSpan.");
    }
    std::size_t trackBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            descriptor.tracks, descriptor.track_count,
            sizeof(CNA_BoneTrackEXTDescriptor), &trackBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The AnimationClipEXT track array is invalid or too large.");
    }
    static_cast<void>(trackBytes);

    AnimationClipEXT copied;
    copied.Duration = System::TimeSpan::FromSeconds(descriptor.duration_seconds);
    if (descriptor.track_count > copied.Tracks.max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The AnimationClipEXT track count is too large.");
    }
    copied.Tracks.reserve(static_cast<std::size_t>(descriptor.track_count));
    for (uint64_t trackIndex = 0U; trackIndex < descriptor.track_count; ++trackIndex) {
        const CNA_BoneTrackEXTDescriptor& sourceTrack = descriptor.tracks[trackIndex];
        if (sourceTrack.reserved != 0U) {
            return InvalidArgument("CNA_BoneTrackEXTDescriptor.reserved must be zero.");
        }
        std::size_t keyframeBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                sourceTrack.keyframes, sourceTrack.keyframe_count,
                sizeof(CNA_KeyframeEXT), &keyframeBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The BoneTrackEXT keyframe array is invalid or too large.");
        }
        static_cast<void>(keyframeBytes);
        BoneTrackEXT copiedTrack;
        copiedTrack.BoneIndex = sourceTrack.bone_index;
        if (sourceTrack.keyframe_count > copiedTrack.Keys.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The BoneTrackEXT keyframe count is too large.");
        }
        copiedTrack.Keys.reserve(static_cast<std::size_t>(sourceTrack.keyframe_count));
        double previousTime = 0.0;
        for (uint64_t keyIndex = 0U; keyIndex < sourceTrack.keyframe_count; ++keyIndex) {
            const CNA_KeyframeEXT& sourceKey = sourceTrack.keyframes[keyIndex];
            if (!IsValidTimeSpanSeconds(sourceKey.time_seconds)) {
                return InvalidArgument("BoneTrackEXT keyframe times must fit a finite TimeSpan.");
            }
            if (keyIndex != 0U && sourceKey.time_seconds < previousTime) {
                return InvalidArgument("BoneTrackEXT keyframe times must be in ascending order.");
            }
            previousTime = sourceKey.time_seconds;
            KeyframeEXT copiedKey;
            copiedKey.Time = System::TimeSpan::FromSeconds(sourceKey.time_seconds);
            copiedKey.Translation = {
                sourceKey.translation.x, sourceKey.translation.y,
                sourceKey.translation.z};
            copiedKey.Rotation = {
                sourceKey.rotation.x, sourceKey.rotation.y,
                sourceKey.rotation.z, sourceKey.rotation.w};
            copiedKey.Scale = {
                sourceKey.scale.x, sourceKey.scale.y, sourceKey.scale.z};
            copiedTrack.Keys.push_back(copiedKey);
        }
        copied.Tracks.push_back(std::move(copiedTrack));
    }
    *outClip = std::move(copied);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopySkeleton(
    const int32_t boneCount,
    const int32_t* const parentBoneIndices,
    const CNA_Matrix* const bindPoseLocal,
    const CNA_Matrix* const inverseBindPoseGlobal,
    std::vector<int>* const outParents,
    std::vector<Matrix>* const outBindPose,
    std::vector<Matrix>* const outInverseBindPose)
{
    if (boneCount < 0) {
        return InvalidArgument("The SkinnedModelEXT bone count cannot be negative.");
    }
    const uint64_t count = static_cast<uint64_t>(boneCount);
    std::size_t ignoredBytes = 0U;
    const struct {
        const void* pointer;
        std::size_t elementSize;
        const char* message;
    } arrays[] = {
        {parentBoneIndices, sizeof(*parentBoneIndices),
         "The SkinnedModelEXT parent-index array is invalid or too large."},
        {bindPoseLocal, sizeof(*bindPoseLocal),
         "The SkinnedModelEXT bind-pose array is invalid or too large."},
        {inverseBindPoseGlobal, sizeof(*inverseBindPoseGlobal),
         "The SkinnedModelEXT inverse-bind-pose array is invalid or too large."}};
    for (const auto& array : arrays) {
        if (const CNA_Result result = CheckedElementByteCount(
                array.pointer, count, array.elementSize, &ignoredBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(result, ErrorCategoryForResult(result), array.message);
        }
    }

    std::vector<int> copiedParents;
    std::vector<Matrix> copiedBindPose;
    std::vector<Matrix> copiedInverseBindPose;
    copiedParents.reserve(static_cast<std::size_t>(count));
    copiedBindPose.reserve(static_cast<std::size_t>(count));
    copiedInverseBindPose.reserve(static_cast<std::size_t>(count));
    for (int32_t index = 0; index < boneCount; ++index) {
        const int32_t parent = parentBoneIndices[index];
        if (parent < -1 || parent >= index) {
            return InvalidArgument(
                "SkinnedModelEXT parent indices must be -1 or precede their child.");
        }
        copiedParents.push_back(parent);
        copiedBindPose.push_back(ToNative(bindPoseLocal[index]));
        copiedInverseBindPose.push_back(ToNative(inverseBindPoseGlobal[index]));
    }
    *outParents = std::move(copiedParents);
    *outBindPose = std::move(copiedBindPose);
    *outInverseBindPose = std::move(copiedInverseBindPose);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_KeyframeEXT ToCKeyframe(const KeyframeEXT& key) noexcept
{
    return CNA_KeyframeEXT{
        key.Time.getTotalSecondsProperty(),
        {key.Translation.X, key.Translation.Y, key.Translation.Z},
        {key.Rotation.X, key.Rotation.Y, key.Rotation.Z, key.Rotation.W},
        {key.Scale.X, key.Scale.Y, key.Scale.Z}};
}

[[nodiscard]] CNA_Result CopySkinningDataDescriptor(
    const CNA_SkinningDataDescriptor& descriptor,
    SkinningData* const outData)
{
    if (descriptor.reserved != 0U) {
        return InvalidArgument("CNA_SkinningDataDescriptor.reserved must be zero.");
    }
    if (descriptor.skeleton_root_prefix_count != 0U &&
        (descriptor.bone_count < 0 || descriptor.skeleton_root_prefix_count !=
            static_cast<uint64_t>(descriptor.bone_count))) {
        return InvalidArgument(
            "The SkinningData root-prefix count must be zero or equal the bone count.");
    }
    std::vector<int> hierarchy;
    std::vector<Matrix> bindPose;
    std::vector<Matrix> inverseBindPose;
    if (const CNA_Result result = CopySkeleton(
            descriptor.bone_count, descriptor.skeleton_hierarchy,
            descriptor.bind_pose, descriptor.inverse_bind_pose,
            &hierarchy, &bindPose, &inverseBindPose);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::size_t prefixBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            descriptor.skeleton_root_prefix,
            descriptor.skeleton_root_prefix_count,
            sizeof(CNA_Matrix), &prefixBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The SkinningData root-prefix array is invalid or too large.");
    }
    static_cast<void>(prefixBytes);
    std::size_t clipBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            descriptor.clips, descriptor.clip_count,
            sizeof(CNA_NamedAnimationClipEXTDescriptor), &clipBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The SkinningData clip array is invalid or too large.");
    }
    static_cast<void>(clipBytes);

    SkinningData copied;
    copied.BoneCount = descriptor.bone_count;
    copied.SkeletonHierarchy = std::move(hierarchy);
    copied.BindPose = std::move(bindPose);
    copied.InverseBindPose = std::move(inverseBindPose);
    copied.SkeletonRootPrefix.reserve(
        static_cast<std::size_t>(descriptor.skeleton_root_prefix_count));
    for (uint64_t index = 0U; index < descriptor.skeleton_root_prefix_count; ++index) {
        copied.SkeletonRootPrefix.push_back(ToNative(descriptor.skeleton_root_prefix[index]));
    }
    if (descriptor.clip_count > copied.AnimationClips.max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The SkinningData clip count is too large.");
    }
    copied.AnimationClips.reserve(static_cast<std::size_t>(descriptor.clip_count));
    for (uint64_t index = 0U; index < descriptor.clip_count; ++index) {
        const CNA_NamedAnimationClipEXTDescriptor& source = descriptor.clips[index];
        std::string name;
        if (const CNA_Result result = CopyStringView(source.name, true, &name);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "A SkinningData clip name is not valid UTF-8 text.");
        }
        if (copied.AnimationClips.contains(name)) {
            return InvalidArgument("SkinningData construction clip names must be unique.");
        }
        AnimationClipEXT clip;
        if (const CNA_Result result = CopyAnimationClipDescriptor(source.clip, &clip);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        copied.AnimationClips.emplace(std::move(name), std::move(clip));
    }
    *outData = std::move(copied);
    return CNA_RESULT_SUCCESS;
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
    // plans/plan_gltf.md GLTF-278 taught this lesson once already, in BlendMorphTargetsEXT: a
    // restated stride list goes stale against the canonical table it copies. This one had, and
    // the three it still admitted were exactly the layouts carrying no tangent, so every
    // PBR morph target -- 48 unskinned, 68 skinned -- was refused outright. Ask the table.
    if (!CNA::Internal::Graphics::InferredLayoutForStride(
             data.Stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt)
             .known) {
        return InvalidArgument("Morph target stride does not name a known vertex layout.");
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
    if (part->activeSkinnedModelReferenceCount != 0U &&
        bufferHandle != part->vertexBuffer.handle) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A SkinnedModelEXT-owned part cannot replace its VertexBuffer.");
    }
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
    if (part->activeSkinnedModelReferenceCount != 0U &&
        bufferHandle != part->indexBuffer.handle) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A SkinnedModelEXT-owned part cannot replace its IndexBuffer.");
    }
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
        if (part->activeSkinnedModelReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "A SkinnedModelEXT-owned part cannot also belong to a ModelMesh.");
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
    AddOwnedGraphicsResourceFor(mesh->parentGame);
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

namespace CNA::C::Detail {

CNA_Result GetOwnedSkinnedModelValue(
    const CNA_Handle handle,
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT>* const outModel)
{
    if (outModel == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The SkinnedModelEXT output is null.");
    }
    outModel->reset();
    std::shared_ptr<SkinnedModelResource> model;
    if (const CNA_Result result = GetSkinnedModel(handle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outModel = model->value;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail


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

namespace CNA::C::Detail {

// CBIND-092B. Declared in CnaCApiGraphicsDetail.hpp; defined here because PartResource is private
// to this adapter.
CNA_Result GetOwnedModelMeshPartValue(
    const CNA_Handle handle,
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::ModelMeshPart>* const outPart)
{
    std::shared_ptr<PartResource> resource;
    if (const CNA_Result result = GetPart(handle, &resource); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outPart = resource->value;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

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
        // CBIND-118: taken before the release, because the resource record is what holds them.
        const std::vector<CNA_Handle> published = model->contentOwnedHandles;
        const CNA_Result result = GetRuntimeHandles().Release(modelHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Model handle could not be released.");
        }
        // A content-loaded model published these; the caller never created them, so releasing the
        // model is what ends them. Each release is best-effort: a caller that already released one
        // by hand must not make destroying the model fail.
        for (const CNA_Handle handle : published) {
            static_cast<void>(GetRuntimeHandles().Release(handle));
        }
        return CNA_RESULT_SUCCESS;
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

namespace {

constexpr uint32_t ReportStructureVersion = UINT32_C(1);

/* The report and its diagnostics are read, never handed out as a handle: the whole object belongs
   to the Model and lives exactly as long as it does, so an index-based read cannot outlive what it
   read from. The strings come back through the count/copy pairs used everywhere else in this ABI,
   because a string of unbounded length never goes in a fixed structure. */

[[nodiscard]] CNA_Result GetImportDiagnostic(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    std::shared_ptr<ModelResource>* const outModel,
    const GltfImportDiagnosticEXT** const outDiagnostic)
{
    if (const CNA_Result result = GetModel(modelHandle, outModel);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<GltfImportDiagnosticEXT>& diagnostics =
        (*outModel)->value->getGltfImportReportEXTProperty().Diagnostics;
    if (index >= diagnostics.size()) {
        return InvalidArgument("The glTF import diagnostic index is outside the valid range.");
    }
    *outDiagnostic = &diagnostics[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyDiagnosticString(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The glTF import diagnostic output buffer is invalid.");
    }
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete glTF import diagnostic string.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetDiagnosticStringByteCount(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount,
    const std::string& (*select)(const GltfImportDiagnosticEXT&))
{
    if (outByteCount == nullptr) {
        return InvalidArgument("The glTF import diagnostic byte-count output is null.");
    }
    std::shared_ptr<ModelResource> model;
    const GltfImportDiagnosticEXT* diagnostic = nullptr;
    if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outByteCount = static_cast<uint64_t>(select(*diagnostic).size());
    return CNA_RESULT_SUCCESS;
}

const std::string& SelectCode(const GltfImportDiagnosticEXT& value) { return value.Code; }
const std::string& SelectSubject(const GltfImportDiagnosticEXT& value) { return value.Subject; }
const std::string& SelectMessage(const GltfImportDiagnosticEXT& value) { return value.Message; }

} // namespace

CNA_Result cna_model_get_gltf_import_report_ext(
    const CNA_ModelHandle modelHandle,
    CNA_GltfImportReportEXT* const outReport)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReport == nullptr || outReport->struct_size < sizeof(CNA_GltfImportReportEXT) ||
            outReport->struct_version != ReportStructureVersion) {
            return InvalidArgument("The glTF import report output is null or malformed.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const GltfImportReportEXT& report = model->value->getGltfImportReportEXTProperty();
        *outReport = CNA_GltfImportReportEXT{
            .struct_size = sizeof(CNA_GltfImportReportEXT),
            .struct_version = ReportStructureVersion,
            .node_count = static_cast<uint64_t>(report.NodeCount),
            .mesh_instance_count = static_cast<uint64_t>(report.MeshInstanceCount),
            .distinct_mesh_count = static_cast<uint64_t>(report.DistinctMeshCount),
            .shared_mesh_count = static_cast<uint64_t>(report.SharedMeshCount),
            .max_node_depth = static_cast<uint64_t>(report.MaxNodeDepth),
            .camera_node_count = static_cast<uint64_t>(report.CameraNodeCount),
            .light_node_count = static_cast<uint64_t>(report.LightNodeCount),
            .imported_light_count = static_cast<uint64_t>(report.ImportedLightCount),
            .primitive_count = static_cast<uint64_t>(report.PrimitiveCount),
            .skin_count = static_cast<uint64_t>(report.SkinCount),
            .animation_count = static_cast<uint64_t>(report.AnimationCount),
            .clip_count = static_cast<uint64_t>(report.ClipCount),
            .diagnostic_count = static_cast<uint64_t>(report.Diagnostics.size()),
            .warning_count = static_cast<uint64_t>(report.getWarningCountProperty()),
            .dropped_feature_count =
                static_cast<uint64_t>(report.getDroppedFeatureCountProperty()),
            .approximation_count = static_cast<uint64_t>(report.getApproximationCountProperty()),
            .anything_lost = report.AnythingLost() ? CNA_TRUE : CNA_FALSE};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_set_gltf_import_report_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_GltfImportReportEXT* const report)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (report == nullptr || report->struct_size < sizeof(CNA_GltfImportReportEXT) ||
            report->struct_version != ReportStructureVersion) {
            return InvalidArgument("The glTF import report is null or malformed.");
        }
        // The derived values are answers the report computes from its diagnostics, not state a
        // caller owns. Refusing a non-zero one is deliberate: silently dropping it would let a
        // caller believe it had recorded a warning count that the next read contradicts.
        if (report->diagnostic_count != 0U || report->warning_count != 0U ||
            report->dropped_feature_count != 0U || report->approximation_count != 0U ||
            report->anything_lost != CNA_FALSE) {
            return InvalidArgument(
                "The glTF import report's derived values are outputs and must be zero.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GltfImportReportEXT native;
        native.NodeCount = static_cast<std::size_t>(report->node_count);
        native.MeshInstanceCount = static_cast<std::size_t>(report->mesh_instance_count);
        native.DistinctMeshCount = static_cast<std::size_t>(report->distinct_mesh_count);
        native.SharedMeshCount = static_cast<std::size_t>(report->shared_mesh_count);
        native.MaxNodeDepth = static_cast<std::size_t>(report->max_node_depth);
        native.CameraNodeCount = static_cast<std::size_t>(report->camera_node_count);
        native.LightNodeCount = static_cast<std::size_t>(report->light_node_count);
        native.ImportedLightCount = static_cast<std::size_t>(report->imported_light_count);
        native.PrimitiveCount = static_cast<std::size_t>(report->primitive_count);
        native.SkinCount = static_cast<std::size_t>(report->skin_count);
        native.AnimationCount = static_cast<std::size_t>(report->animation_count);
        native.ClipCount = static_cast<std::size_t>(report->clip_count);
        model->value->setGltfImportReportEXTProperty(std::move(native));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_add_gltf_import_diagnostic_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_GltfImportDiagnosticDescriptorEXT* const descriptor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (descriptor == nullptr) {
            return InvalidArgument("The glTF import diagnostic descriptor is null.");
        }
        if (descriptor->severity > CNA_GLTF_IMPORT_SEVERITY_MAXIMUM_EXT) {
            return InvalidArgument("The glTF import diagnostic severity is not a defined identity.");
        }
        if (descriptor->kind > CNA_GLTF_IMPORT_KIND_MAXIMUM_EXT) {
            return InvalidArgument("The glTF import diagnostic kind is not a defined identity.");
        }
        if (descriptor->details == nullptr && descriptor->detail_count != 0U) {
            return InvalidArgument("The glTF import diagnostic detail array is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GltfImportDiagnosticEXT entry;
        if (const CNA_Result result = CopyStringView(descriptor->code, true, &entry.Code);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The glTF import diagnostic code is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(descriptor->subject, true, &entry.Subject);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The glTF import diagnostic subject is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(descriptor->message, true, &entry.Message);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The glTF import diagnostic message is not valid UTF-8 text.");
        }
        entry.Details.reserve(static_cast<std::size_t>(descriptor->detail_count));
        for (uint64_t index = 0U; index < descriptor->detail_count; ++index) {
            std::string detail;
            if (const CNA_Result result =
                    CopyStringView(descriptor->details[index], true, &detail);
                result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result,
                    ErrorCategoryForResult(result),
                    "A glTF import diagnostic detail is not valid UTF-8 text.");
            }
            entry.Details.push_back(std::move(detail));
        }
        entry.Severity =
            static_cast<GltfImportDiagnosticSeverityEXT>(descriptor->severity);
        entry.Kind = static_cast<GltfImportDiagnosticKindEXT>(descriptor->kind);
        entry.Count = static_cast<std::size_t>(descriptor->count);
        entry.WorstMagnitude = descriptor->worst_magnitude;

        // The canonical report exposes its diagnostics as a const reference, so appending means
        // reading the whole report, extending it and setting it back. Copies of this size happen
        // once per diagnostic at build time and never on a render path.
        GltfImportReportEXT updated = model->value->getGltfImportReportEXTProperty();
        updated.Diagnostics.push_back(std::move(entry));
        model->value->setGltfImportReportEXTProperty(std::move(updated));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_gltf_import_diagnostic_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_GltfImportDiagnosticEXT* const outDiagnostic)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDiagnostic == nullptr ||
            outDiagnostic->struct_size < sizeof(CNA_GltfImportDiagnosticEXT) ||
            outDiagnostic->struct_version != ReportStructureVersion) {
            return InvalidArgument("The glTF import diagnostic output is null or malformed.");
        }
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDiagnostic = CNA_GltfImportDiagnosticEXT{
            .struct_size = sizeof(CNA_GltfImportDiagnosticEXT),
            .struct_version = ReportStructureVersion,
            .severity = static_cast<CNA_GltfImportDiagnosticSeverityEXT>(diagnostic->Severity),
            .kind = static_cast<CNA_GltfImportDiagnosticKindEXT>(diagnostic->Kind),
            .count = static_cast<uint64_t>(diagnostic->Count),
            .worst_magnitude = diagnostic->WorstMagnitude,
            .detail_count = static_cast<uint64_t>(diagnostic->Details.size())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_gltf_import_diagnostic_code_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDiagnosticStringByteCount(modelHandle, index, outByteCount, SelectCode);
    });
}

CNA_Result cna_model_copy_gltf_import_diagnostic_code_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyDiagnosticString(diagnostic->Code, destination, capacity, outByteCount);
    });
}

CNA_Result cna_model_get_gltf_import_diagnostic_subject_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDiagnosticStringByteCount(modelHandle, index, outByteCount, SelectSubject);
    });
}

CNA_Result cna_model_copy_gltf_import_diagnostic_subject_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyDiagnosticString(diagnostic->Subject, destination, capacity, outByteCount);
    });
}

CNA_Result cna_model_get_gltf_import_diagnostic_message_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDiagnosticStringByteCount(modelHandle, index, outByteCount, SelectMessage);
    });
}

CNA_Result cna_model_copy_gltf_import_diagnostic_message_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyDiagnosticString(diagnostic->Message, destination, capacity, outByteCount);
    });
}

CNA_Result cna_model_get_gltf_import_diagnostic_detail_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    const uint64_t detailIndex,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The glTF import diagnostic byte-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (detailIndex >= diagnostic->Details.size()) {
            return InvalidArgument(
                "The glTF import diagnostic detail index is outside the valid range.");
        }
        *outByteCount = static_cast<uint64_t>(
            diagnostic->Details[static_cast<std::size_t>(detailIndex)].size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_gltf_import_diagnostic_detail_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    const uint64_t detailIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const GltfImportDiagnosticEXT* diagnostic = nullptr;
        if (const CNA_Result result = GetImportDiagnostic(modelHandle, index, &model, &diagnostic);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (detailIndex >= diagnostic->Details.size()) {
            return InvalidArgument(
                "The glTF import diagnostic detail index is outside the valid range.");
        }
        return CopyDiagnosticString(
            diagnostic->Details[static_cast<std::size_t>(detailIndex)],
            destination,
            capacity,
            outByteCount);
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

CNA_Result cna_skinned_model_ext_create_default(
    CNA_SkinnedModelEXTHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The SkinnedModelEXT output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        return CreateSkinnedModelHandle(
            std::make_shared<SkinnedModelResource>(), outModel);
    });
}

CNA_Result cna_skinned_model_ext_create(
    const CNA_SkinnedModelEXTDescriptor* const descriptor,
    CNA_SkinnedModelEXTHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (descriptor == nullptr || outModel == nullptr) {
            return InvalidArgument("The SkinnedModelEXT descriptor or output is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        if (descriptor->reserved != 0U) {
            return InvalidArgument("CNA_SkinnedModelEXTDescriptor.reserved must be zero.");
        }
        std::vector<int> parents;
        std::vector<Matrix> bindPose;
        std::vector<Matrix> inverseBindPose;
        if (const CNA_Result result = CopySkeleton(
                descriptor->bone_count, descriptor->parent_bone_indices,
                descriptor->bind_pose_local, descriptor->inverse_bind_pose_global,
                &parents, &bindPose, &inverseBindPose);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::size_t clipBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                descriptor->clips, descriptor->clip_count,
                sizeof(CNA_NamedAnimationClipEXTDescriptor), &clipBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinnedModelEXT clip array is invalid or too large.");
        }
        static_cast<void>(clipBytes);

        auto resource = std::make_shared<SkinnedModelResource>();
        resource->value->BoneCount = descriptor->bone_count;
        resource->value->ParentBoneIndices = std::move(parents);
        resource->value->BindPoseLocal = std::move(bindPose);
        resource->value->InverseBindPoseGlobal = std::move(inverseBindPose);
        if (descriptor->clip_count > resource->value->Clips.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The SkinnedModelEXT clip count is too large.");
        }
        resource->value->Clips.reserve(static_cast<std::size_t>(descriptor->clip_count));
        for (uint64_t index = 0U; index < descriptor->clip_count; ++index) {
            const CNA_NamedAnimationClipEXTDescriptor& source = descriptor->clips[index];
            std::string name;
            if (const CNA_Result result = CopyStringView(source.name, true, &name);
                result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result, ErrorCategoryForResult(result),
                    "A SkinnedModelEXT clip name is not valid UTF-8 text.");
            }
            if (resource->value->Clips.contains(name)) {
                return InvalidArgument("SkinnedModelEXT construction clip names must be unique.");
            }
            AnimationClipEXT clip;
            if (const CNA_Result result = CopyAnimationClipDescriptor(source.clip, &clip);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            resource->value->Clips.emplace(std::move(name), std::move(clip));
        }
        return CreateSkinnedModelHandle(std::move(resource), outModel);
    });
}

CNA_Result cna_skinned_model_ext_destroy(
    const CNA_SkinnedModelEXTHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(modelHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned SkinnedModelEXT handle could not be released.");
    });
}

CNA_Result cna_skinned_model_ext_create_move(
    const CNA_SkinnedModelEXTHandle sourceHandle,
    CNA_SkinnedModelEXTHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The moved SkinnedModelEXT output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        std::shared_ptr<SkinnedModelResource> source;
        if (const CNA_Result result = GetSkinnedModel(sourceHandle, &source);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto moved = std::make_shared<SkinnedModelResource>();
        moved->value = std::make_shared<SkinnedModelEXT>(std::move(*source->value));
        moved->parts = std::move(source->parts);
        moved->parentGame = source->parentGame;
        RebuildSkinnedParts(*moved);
        source->value = std::make_shared<SkinnedModelEXT>();
        source->parts.clear();
        source->parentGame = CNA_INVALID_HANDLE;
        return CreateSkinnedModelHandle(std::move(moved), outModel);
    });
}

CNA_Result cna_skinned_model_ext_move_assign(
    const CNA_SkinnedModelEXTHandle destinationHandle,
    const CNA_SkinnedModelEXTHandle sourceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destinationHandle == sourceHandle) {
            return InvalidArgument("A SkinnedModelEXT cannot be move-assigned to itself.");
        }
        std::shared_ptr<SkinnedModelResource> destination;
        std::shared_ptr<SkinnedModelResource> source;
        if (const CNA_Result result = GetSkinnedModel(destinationHandle, &destination);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetSkinnedModel(sourceHandle, &source);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *destination->value = std::move(*source->value);
        destination->parts = std::move(source->parts);
        destination->parentGame = source->parentGame;
        RebuildSkinnedParts(*destination);
        source->value = std::make_shared<SkinnedModelEXT>();
        source->parts.clear();
        source->parentGame = CNA_INVALID_HANDLE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_set_skeleton(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const int32_t boneCount,
    const int32_t* const parentBoneIndices,
    const CNA_Matrix* const bindPoseLocal,
    const CNA_Matrix* const inverseBindPoseGlobal)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::vector<int> parents;
        std::vector<Matrix> bindPose;
        std::vector<Matrix> inverseBindPose;
        if (const CNA_Result result = CopySkeleton(
                boneCount, parentBoneIndices, bindPoseLocal, inverseBindPoseGlobal,
                &parents, &bindPose, &inverseBindPose);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->BoneCount = boneCount;
        model->value->ParentBoneIndices = std::move(parents);
        model->value->BindPoseLocal = std::move(bindPose);
        model->value->InverseBindPoseGlobal = std::move(inverseBindPose);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_bone_count(
    const CNA_SkinnedModelEXTHandle modelHandle,
    uint64_t* const outBoneCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBoneCount == nullptr) {
            return InvalidArgument("The SkinnedModelEXT bone-count output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBoneCount = static_cast<uint64_t>(model->value->BoneCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_copy_parent_bone_indices(
    const CNA_SkinnedModelEXTHandle modelHandle,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            model->value->ParentBoneIndices, destination, capacity, outCount,
            [](const int value) noexcept { return static_cast<int32_t>(value); },
            "The SkinnedModelEXT parent-index output is invalid.",
            "The destination cannot hold all SkinnedModelEXT parent indices.");
    });
}

CNA_Result cna_skinned_model_ext_copy_bind_pose_local(
    const CNA_SkinnedModelEXTHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            model->value->BindPoseLocal, destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The SkinnedModelEXT bind-pose output is invalid.",
            "The destination cannot hold all SkinnedModelEXT bind-pose matrices.");
    });
}

CNA_Result cna_skinned_model_ext_copy_inverse_bind_pose_global(
    const CNA_SkinnedModelEXTHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            model->value->InverseBindPoseGlobal, destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The SkinnedModelEXT inverse-bind-pose output is invalid.",
            "The destination cannot hold all SkinnedModelEXT inverse-bind-pose matrices.");
    });
}

CNA_Result cna_skinned_model_ext_set_clip(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const CNA_AnimationClipEXTDescriptor* const clip)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (clip == nullptr) {
            return InvalidArgument("The AnimationClipEXT descriptor is null.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationClipEXT name is not valid UTF-8 text.");
        }
        AnimationClipEXT copiedClip;
        if (const CNA_Result result = CopyAnimationClipDescriptor(*clip, &copiedClip);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->Clips.insert_or_assign(std::move(copiedName), std::move(copiedClip));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_remove_clip(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationClipEXT name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->Clips.erase(copiedName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_clip_count(
    const CNA_SkinnedModelEXTHandle modelHandle,
    uint64_t* const outClipCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outClipCount == nullptr) {
            return InvalidArgument("The AnimationClipEXT count output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outClipCount = static_cast<uint64_t>(model->value->Clips.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_clip_name_byte_count_at(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The AnimationClipEXT name-size output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*model->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The AnimationClipEXT index is outside the valid range.");
        }
        *outByteCount = static_cast<uint64_t>(names[static_cast<std::size_t>(clipIndex)].size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_copy_clip_name_at(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*model->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The AnimationClipEXT index is outside the valid range.");
        }
        return CopySkinnedName(
            names[static_cast<std::size_t>(clipIndex)], destination, capacity,
            outByteCount, "The AnimationClipEXT name output is invalid.",
            "The destination cannot hold the complete AnimationClipEXT name.");
    });
}

CNA_Result cna_skinned_model_ext_get_clip_info(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outDurationSeconds == nullptr || outTrackCount == nullptr) {
            return InvalidArgument("An AnimationClipEXT info output is null.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationClipEXT name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto iterator = model->value->Clips.find(copiedName);
        if (iterator == model->value->Clips.end()) {
            *outFound = CNA_FALSE;
            *outDurationSeconds = 0.0;
            *outTrackCount = 0U;
            return CNA_RESULT_SUCCESS;
        }
        *outFound = CNA_TRUE;
        *outDurationSeconds = iterator->second.Duration.getTotalSecondsProperty();
        *outTrackCount = static_cast<uint64_t>(iterator->second.Tracks.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_copy_clip_track(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const uint64_t trackIndex,
    int32_t* const outBoneIndex,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBoneIndex == nullptr || outKeyframeCount == nullptr ||
            (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("A BoneTrackEXT output is invalid.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationClipEXT name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto clip = model->value->Clips.find(copiedName);
        if (clip == model->value->Clips.end()) {
            return InvalidArgument("The AnimationClipEXT name is unknown.");
        }
        if (trackIndex >= clip->second.Tracks.size()) {
            return InvalidArgument("The BoneTrackEXT index is outside the valid range.");
        }
        const BoneTrackEXT& track = clip->second.Tracks[static_cast<std::size_t>(trackIndex)];
        *outKeyframeCount = static_cast<uint64_t>(track.Keys.size());
        if (capacity < track.Keys.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all BoneTrackEXT keyframes.");
        }
        *outBoneIndex = static_cast<int32_t>(track.BoneIndex);
        for (std::size_t index = 0U; index < track.Keys.size(); ++index) {
            destination[index] = ToCKeyframe(track.Keys[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_compute_bone_transforms(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView clipName,
    const double positionSeconds,
    const CNA_Bool loop,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outBoneCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBoneCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The SkinnedModelEXT transform output is invalid.");
        }
        if (loop != CNA_FALSE && loop != CNA_TRUE) {
            return InvalidArgument("The SkinnedModelEXT loop value is not a canonical C boolean.");
        }
        if (!IsValidTimeSpanSeconds(positionSeconds)) {
            return InvalidArgument("The SkinnedModelEXT playback position must fit a finite TimeSpan.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(clipName, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationClipEXT name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!model->value->Clips.contains(copiedName)) {
            return InvalidArgument("The AnimationClipEXT name is unknown.");
        }
        *outBoneCount = static_cast<uint64_t>(model->value->BoneCount);
        if (capacity < static_cast<uint64_t>(model->value->BoneCount)) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all computed bone transforms.");
        }
        std::vector<Matrix> computed;
        model->value->ComputeBoneTransformsEXT(
            copiedName, System::TimeSpan::FromSeconds(positionSeconds),
            loop == CNA_TRUE, computed);
        for (std::size_t index = 0U; index < computed.size(); ++index) {
            destination[index] = ToC(computed[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_add_part(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinnedModelEXT part name is not valid UTF-8 text.");
        }
        if (vertexBufferHandle == CNA_INVALID_HANDLE ||
            indexBufferHandle == CNA_INVALID_HANDLE) {
            return InvalidArgument("SkinnedModelEXT parts require vertex and index buffers.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        std::shared_ptr<VertexBufferResource> vertexBuffer;
        std::shared_ptr<IndexBufferResource> indexBuffer;
        std::shared_ptr<PartResource> part;
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ResolveVertexBuffer(vertexBufferHandle, &vertexBuffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ResolveIndexBuffer(indexBufferHandle, &indexBuffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (textureHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = CNA::C::Detail::GetOwnedTexture2D(
                    textureHandle, &texture);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (texture->value->getIsDisposedProperty()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE,
                    "A disposed Texture2D cannot be added to a SkinnedModelEXT.");
            }
        }
        const CNA_Handle parentGame = vertexBuffer->parentGame;
        if (indexBuffer->parentGame != parentGame ||
            (texture != nullptr && texture->parentGame != parentGame)) {
            return InvalidArgument(
                "All SkinnedModelEXT part resources must belong to one graphics device.");
        }
        if (model->parentGame != CNA_INVALID_HANDLE && model->parentGame != parentGame) {
            return InvalidArgument(
                "All SkinnedModelEXT parts must belong to one graphics device.");
        }
        if (part->parentMesh != nullptr || part->activeSkinnedModelReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE,
                "A ModelMeshPart cannot belong to more than one live model.");
        }
        if (part->activeSkinnedModelReferenceCount ==
                std::numeric_limits<uint64_t>::max() ||
            (texture != nullptr && texture->activeModelReferenceCount ==
                std::numeric_limits<uint64_t>::max()) ||
            (vertexBufferHandle != part->vertexBuffer.handle &&
             vertexBuffer->activeModelReferenceCount ==
                std::numeric_limits<uint64_t>::max()) ||
            (indexBufferHandle != part->indexBuffer.handle &&
             indexBuffer->activeModelReferenceCount ==
                std::numeric_limits<uint64_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "A SkinnedModelEXT resource retention count overflowed.");
        }
        if (model->parts.size() == model->parts.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The SkinnedModelEXT part count is too large.");
        }
        model->parts.reserve(model->parts.size() + 1U);
        if (const CNA_Result result = SetPartVertexBuffer(part, vertexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = SetPartIndexBuffer(part, indexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        SkinnedPartEntry entry;
        entry.name = std::move(copiedName);
        entry.part = std::move(part);
        entry.textureHandle = textureHandle;
        entry.texture = std::move(texture);
        ++entry.part->activeSkinnedModelReferenceCount;
        if (entry.texture != nullptr) {
            ++entry.texture->activeModelReferenceCount;
        }
        model->parts.push_back(std::move(entry));
        model->parentGame = parentGame;
        RebuildSkinnedParts(*model);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_attach_parts(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_SkinnedModelEXTHandle otherHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (modelHandle == otherHandle) {
            return InvalidArgument("A SkinnedModelEXT cannot attach its own parts.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        std::shared_ptr<SkinnedModelResource> other;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetSkinnedModel(otherHandle, &other);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (model->value->BoneCount != other->value->BoneCount) {
            return InvalidArgument(
                "Attached SkinnedModelEXT instances must have equal bone counts.");
        }
        if (!model->parts.empty() && !other->parts.empty() &&
            model->parentGame != other->parentGame) {
            return InvalidArgument(
                "Attached SkinnedModelEXT parts must belong to one graphics device.");
        }
        if (other->parts.empty()) {
            return CNA_RESULT_SUCCESS;
        }
        if (other->parts.size() > model->parts.max_size() - model->parts.size()) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The attached SkinnedModelEXT part count is too large.");
        }
        model->parts.reserve(model->parts.size() + other->parts.size());
        for (SkinnedPartEntry& incoming : other->parts) {
            model->parts.erase(
                std::remove_if(
                    model->parts.begin(), model->parts.end(),
                    [&](const SkinnedPartEntry& current) {
                        return current.name == incoming.name;
                    }),
                model->parts.end());
            model->parts.push_back(std::move(incoming));
        }
        other->parts.clear();
        if (model->parentGame == CNA_INVALID_HANDLE) {
            model->parentGame = other->parentGame;
        }
        other->parentGame = CNA_INVALID_HANDLE;
        RebuildSkinnedParts(*model);
        RebuildSkinnedParts(*other);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_remove_part(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinnedModelEXT part name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->parts.erase(
            std::remove_if(
                model->parts.begin(), model->parts.end(),
                [&](const SkinnedPartEntry& entry) { return entry.name == copiedName; }),
            model->parts.end());
        if (model->parts.empty()) {
            model->parentGame = CNA_INVALID_HANDLE;
        }
        RebuildSkinnedParts(*model);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_part_count(
    const CNA_SkinnedModelEXTHandle modelHandle,
    uint64_t* const outPartCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPartCount == nullptr) {
            return InvalidArgument("The SkinnedModelEXT part-count output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPartCount = static_cast<uint64_t>(model->parts.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_part_name_byte_count_at(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t partIndex,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The SkinnedModelEXT part-name size output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (partIndex >= model->parts.size()) {
            return InvalidArgument("The SkinnedModelEXT part index is outside the valid range.");
        }
        *outByteCount = static_cast<uint64_t>(
            model->parts[static_cast<std::size_t>(partIndex)].name.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_copy_part_name_at(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t partIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (partIndex >= model->parts.size()) {
            return InvalidArgument("The SkinnedModelEXT part index is outside the valid range.");
        }
        return CopySkinnedName(
            model->parts[static_cast<std::size_t>(partIndex)].name,
            destination, capacity, outByteCount,
            "The SkinnedModelEXT part-name output is invalid.",
            "The destination cannot hold the complete SkinnedModelEXT part name.");
    });
}

CNA_Result cna_skinned_model_ext_get_part_at(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t partIndex,
    CNA_ModelMeshPartHandle* const outPart,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPart == nullptr || outHasTexture == nullptr || outTexture == nullptr) {
            return InvalidArgument("A SkinnedModelEXT part output is null.");
        }
        *outPart = CNA_INVALID_HANDLE;
        *outHasTexture = CNA_FALSE;
        *outTexture = CNA_INVALID_HANDLE;
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (partIndex >= model->parts.size()) {
            return InvalidArgument("The SkinnedModelEXT part index is outside the valid range.");
        }
        const SkinnedPartEntry& entry = model->parts[static_cast<std::size_t>(partIndex)];
        if (const CNA_Result result = CreatePartHandle(entry.part, outPart);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (entry.texture != nullptr) {
            *outHasTexture = CNA_TRUE;
            *outTexture = entry.textureHandle;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinned_model_ext_get_owned_resource_counts(
    const CNA_SkinnedModelEXTHandle modelHandle,
    uint64_t* const outVertexBuffers,
    uint64_t* const outIndexBuffers,
    uint64_t* const outParts,
    uint64_t* const outTextures)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVertexBuffers == nullptr || outIndexBuffers == nullptr ||
            outParts == nullptr || outTextures == nullptr) {
            return InvalidArgument("A SkinnedModelEXT owned-resource count output is null.");
        }
        std::shared_ptr<SkinnedModelResource> model;
        if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t partCount = static_cast<uint64_t>(model->parts.size());
        uint64_t textureCount = 0U;
        for (const SkinnedPartEntry& entry : model->parts) {
            if (entry.texture != nullptr) {
                ++textureCount;
            }
        }
        *outVertexBuffers = partCount;
        *outIndexBuffers = partCount;
        *outParts = partCount;
        *outTextures = textureCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_create(
    const CNA_SkinningDataDescriptor* const descriptor,
    CNA_SkinningDataHandle* const outData)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (descriptor == nullptr || outData == nullptr) {
            return InvalidArgument("The SkinningData descriptor or output is null.");
        }
        *outData = CNA_INVALID_HANDLE;
        SkinningData copied;
        if (const CNA_Result result = CopySkinningDataDescriptor(*descriptor, &copied);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SkinningDataResource>();
        resource->value = std::make_shared<SkinningData>(std::move(copied));
        return CreateSkinningDataHandle(std::move(resource), outData);
    });
}

CNA_Result cna_skinning_data_destroy(const CNA_SkinningDataHandle dataHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(dataHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned SkinningData handle could not be released.");
    });
}

CNA_Result cna_skinning_data_get_type_name_byte_count(
    const CNA_SkinningDataHandle dataHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The SkinningData type-name size output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(data->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_copy_type_name(
    const CNA_SkinningDataHandle dataHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopySkinnedName(
            data->value->GetTypeName(), destination, capacity, outByteCount,
            "The SkinningData type-name output is invalid.",
            "The destination cannot hold the complete SkinningData type name.");
    });
}

CNA_Result cna_skinning_data_get_bone_count(
    const CNA_SkinningDataHandle dataHandle,
    uint64_t* const outBoneCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBoneCount == nullptr) {
            return InvalidArgument("The SkinningData bone-count output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBoneCount = static_cast<uint64_t>(data->value->BoneCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_copy_skeleton_hierarchy(
    const CNA_SkinningDataHandle dataHandle,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->SkeletonHierarchy, destination, capacity, outCount,
            [](const int value) noexcept { return static_cast<int32_t>(value); },
            "The SkinningData hierarchy output is invalid.",
            "The destination cannot hold the complete SkinningData hierarchy.");
    });
}

CNA_Result cna_skinning_data_copy_bind_pose(
    const CNA_SkinningDataHandle dataHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->BindPose, destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The SkinningData bind-pose output is invalid.",
            "The destination cannot hold all SkinningData bind-pose matrices.");
    });
}

CNA_Result cna_skinning_data_copy_inverse_bind_pose(
    const CNA_SkinningDataHandle dataHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->InverseBindPose, destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The SkinningData inverse-bind-pose output is invalid.",
            "The destination cannot hold all SkinningData inverse-bind-pose matrices.");
    });
}

CNA_Result cna_skinning_data_copy_skeleton_root_prefix(
    const CNA_SkinningDataHandle dataHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->SkeletonRootPrefix, destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The SkinningData root-prefix output is invalid.",
            "The destination cannot hold all SkinningData root-prefix matrices.");
    });
}

CNA_Result cna_skinning_data_get_clip_count(
    const CNA_SkinningDataHandle dataHandle,
    uint64_t* const outClipCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outClipCount == nullptr) {
            return InvalidArgument("The SkinningData clip-count output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outClipCount = static_cast<uint64_t>(data->value->AnimationClips.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_get_clip_name_byte_count_at(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The SkinningData clip-name size output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*data->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The SkinningData clip index is outside the valid range.");
        }
        *outByteCount = static_cast<uint64_t>(names[static_cast<std::size_t>(clipIndex)].size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_copy_clip_name_at(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*data->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The SkinningData clip index is outside the valid range.");
        }
        return CopySkinnedName(
            names[static_cast<std::size_t>(clipIndex)], destination, capacity,
            outByteCount, "The SkinningData clip-name output is invalid.",
            "The destination cannot hold the complete SkinningData clip name.");
    });
}

CNA_Result cna_skinning_data_get_clip_info(
    const CNA_SkinningDataHandle dataHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outDurationSeconds == nullptr || outTrackCount == nullptr) {
            return InvalidArgument("A SkinningData clip-info output is null.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinningData clip name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto clip = data->value->AnimationClips.find(copiedName);
        if (clip == data->value->AnimationClips.end()) {
            *outFound = CNA_FALSE;
            *outDurationSeconds = 0.0;
            *outTrackCount = 0U;
            return CNA_RESULT_SUCCESS;
        }
        *outFound = CNA_TRUE;
        *outDurationSeconds = clip->second.Duration.getTotalSecondsProperty();
        *outTrackCount = static_cast<uint64_t>(clip->second.Tracks.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_copy_clip_track(
    const CNA_SkinningDataHandle dataHandle,
    const CNA_StringView name,
    const uint64_t trackIndex,
    int32_t* const outBoneIndex,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBoneIndex == nullptr || outKeyframeCount == nullptr ||
            (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("A SkinningData BoneTrack output is invalid.");
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinningData clip name is not valid UTF-8 text.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto clip = data->value->AnimationClips.find(copiedName);
        if (clip == data->value->AnimationClips.end()) {
            return InvalidArgument("The SkinningData clip name is unknown.");
        }
        if (trackIndex >= clip->second.Tracks.size()) {
            return InvalidArgument("The SkinningData track index is outside the valid range.");
        }
        const BoneTrackEXT& track = clip->second.Tracks[static_cast<std::size_t>(trackIndex)];
        *outKeyframeCount = static_cast<uint64_t>(track.Keys.size());
        if (capacity < track.Keys.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all SkinningData keyframes.");
        }
        *outBoneIndex = static_cast<int32_t>(track.BoneIndex);
        for (std::size_t index = 0U; index < track.Keys.size(); ++index) {
            destination[index] = ToCKeyframe(track.Keys[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_create(
    const CNA_SkinningDataHandle dataHandle,
    CNA_AnimationPlayerHandle* const outPlayer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayer == nullptr) {
            return InvalidArgument("The AnimationPlayer output handle is null.");
        }
        *outPlayer = CNA_INVALID_HANDLE;
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<AnimationPlayerResource>();
        resource->data = std::move(data);
        resource->value = std::make_shared<AnimationPlayer>(*resource->data->value);
        return CreateAnimationPlayerHandle(std::move(resource), outPlayer);
    });
}

CNA_Result cna_animation_player_destroy(
    const CNA_AnimationPlayerHandle playerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(playerHandle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The owned AnimationPlayer handle could not be released.");
    });
}

CNA_Result cna_animation_player_start_clip(
    const CNA_AnimationPlayerHandle playerHandle,
    const CNA_StringView clipName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(clipName, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The AnimationPlayer clip name is not valid UTF-8 text.");
        }
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto clip = player->data->value->AnimationClips.find(copiedName);
        if (clip == player->data->value->AnimationClips.end()) {
            return InvalidArgument("The AnimationPlayer clip name is unknown.");
        }
        player->value->StartClip(clip->second);
        player->currentClipName = std::move(copiedName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_update(
    const CNA_AnimationPlayerHandle playerHandle,
    const double timeSeconds,
    const CNA_Bool relativeToCurrentTime,
    const CNA_Bool loop)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsValidTimeSpanSeconds(timeSeconds)) {
            return InvalidArgument("The AnimationPlayer time must fit a finite TimeSpan.");
        }
        if ((relativeToCurrentTime != CNA_FALSE && relativeToCurrentTime != CNA_TRUE) ||
            (loop != CNA_FALSE && loop != CNA_TRUE)) {
            return InvalidArgument("AnimationPlayer flags must be canonical C booleans.");
        }
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        player->value->Update(
            System::TimeSpan::FromSeconds(timeSeconds),
            relativeToCurrentTime == CNA_TRUE,
            loop == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_get_current_position(
    const CNA_AnimationPlayerHandle playerHandle,
    double* const outPositionSeconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPositionSeconds == nullptr) {
            return InvalidArgument("The AnimationPlayer position output is null.");
        }
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPositionSeconds =
            player->value->getCurrentPositionProperty().getTotalSecondsProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_get_current_clip_info(
    const CNA_AnimationPlayerHandle playerHandle,
    CNA_Bool* const outHasClip,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasClip == nullptr || outDurationSeconds == nullptr || outTrackCount == nullptr) {
            return InvalidArgument("An AnimationPlayer current-clip output is null.");
        }
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const AnimationClipEXT* const clip = player->value->getCurrentClipProperty();
        if (clip == nullptr) {
            *outHasClip = CNA_FALSE;
            *outDurationSeconds = 0.0;
            *outTrackCount = 0U;
            return CNA_RESULT_SUCCESS;
        }
        *outHasClip = CNA_TRUE;
        *outDurationSeconds = clip->Duration.getTotalSecondsProperty();
        *outTrackCount = static_cast<uint64_t>(clip->Tracks.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_get_current_clip_name_byte_count(
    const CNA_AnimationPlayerHandle playerHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The AnimationPlayer clip-name size output is null.");
        }
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(player->currentClipName.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_animation_player_copy_current_clip_name(
    const CNA_AnimationPlayerHandle playerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopySkinnedName(
            player->currentClipName, destination, capacity, outByteCount,
            "The AnimationPlayer clip-name output is invalid.",
            "The destination cannot hold the complete AnimationPlayer clip name.");
    });
}

CNA_Result cna_animation_player_copy_bone_transforms(
    const CNA_AnimationPlayerHandle playerHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            player->value->GetBoneTransforms(), destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The AnimationPlayer bone-transform output is invalid.",
            "The destination cannot hold all AnimationPlayer bone transforms.");
    });
}

CNA_Result cna_animation_player_copy_world_transforms(
    const CNA_AnimationPlayerHandle playerHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            player->value->GetWorldTransforms(), destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The AnimationPlayer world-transform output is invalid.",
            "The destination cannot hold all AnimationPlayer world transforms.");
    });
}

CNA_Result cna_animation_player_copy_skin_transforms(
    const CNA_AnimationPlayerHandle playerHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnimationPlayerResource> player;
        if (const CNA_Result result = GetAnimationPlayer(playerHandle, &player);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            player->value->GetSkinTransforms(), destination, capacity, outCount,
            [](const Matrix value) noexcept { return ToC(value); },
            "The AnimationPlayer skin-transform output is invalid.",
            "The destination cannot hold all AnimationPlayer skin transforms.");
    });
}

CNA_Result cna_model_mesh_part_get_primitive_type_ext(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_PrimitiveType* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The ModelMeshPart primitive-type output is null.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<CNA_PrimitiveType>(part->value->getPrimitiveTypeEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_primitive_type_ext(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_PrimitiveType value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value > CNA_PRIMITIVE_POINT_LIST_EXT) {
            return InvalidArgument("The primitive type is not a defined identity.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        part->value->setPrimitiveTypeEXTProperty(static_cast<PrimitiveType>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_get_sampler_state_ext(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_PbrTextureSlot slot,
    CNA_SamplerState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidArgument("The ModelMeshPart sampler-state output is null.");
        }
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const SamplerState& state = slot < CNA_PBR_TEXTURE_SPECULAR_EXT
            ? part->value->getSamplerStatesEXTProperty()[slot]
            : part->value->getSpecularSamplerStatesEXTProperty()
                  [slot - CNA_PBR_TEXTURE_SPECULAR_EXT];
        CNA::C::Detail::ToCSamplerState(state, outState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_mesh_part_set_sampler_state_ext(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_SamplerState* const state)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (slot > CNA_PBR_TEXTURE_MAXIMUM) {
            return InvalidArgument("The PBR texture slot is outside the valid range.");
        }
        std::shared_ptr<PartResource> part;
        if (const CNA_Result result = GetPart(partHandle, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SamplerState native;
        if (const CNA_Result result = CNA::C::Detail::ToNativeSamplerState(state, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (slot < CNA_PBR_TEXTURE_SPECULAR_EXT) {
            part->value->setSamplerStateEXTProperty(static_cast<int>(slot), native);
        } else {
            part->value->setSpecularSamplerStateEXTProperty(
                static_cast<int>(slot - CNA_PBR_TEXTURE_SPECULAR_EXT), native);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_get_clip_target_space_ext(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    CNA_ClipTargetSpaceEXT* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinningData clip target-space output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*data->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The SkinningData clip index is outside the valid range.");
        }
        *outValue = static_cast<CNA_ClipTargetSpaceEXT>(
            data->value->AnimationClips.at(names[static_cast<std::size_t>(clipIndex)])
                .TargetSpace);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_set_clip_target_space_ext(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    const CNA_ClipTargetSpaceEXT value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value > CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT) {
            return InvalidArgument("The clip target space is not a defined identity.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> names = SortedClipNames(*data->value);
        if (clipIndex >= names.size()) {
            return InvalidArgument("The SkinningData clip index is outside the valid range.");
        }
        data->value->AnimationClips.at(names[static_cast<std::size_t>(clipIndex)]).TargetSpace =
            static_cast<ClipTargetSpaceEXT>(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_tangent_deltas(
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
        // The target count is the position array's length, which is what the matching setter
        // bounds against and why: TangentDeltas is grown to cover the targets only when a tangent
        // is first written, so bounding against it refused every target of data that has none --
        // and said the index was out of range, which was never the reason.
        if (targetIndex >= data->value->PositionDeltas.size()) {
            return InvalidArgument(
                "The MorphTargetDataEXT target index is outside the valid range.");
        }
        static const std::vector<Microsoft::Xna::Framework::Vector3> kNoTangentDeltas;
        const std::vector<Microsoft::Xna::Framework::Vector3>& tangents =
            targetIndex < data->value->TangentDeltas.size()
                ? data->value->TangentDeltas[static_cast<std::size_t>(targetIndex)]
                : kNoTangentDeltas;
        return CopyOutputValues(
            tangents,
            destination, capacity, outDeltaCount, ToCVector3,
            "The MorphTargetDataEXT tangent-delta output is invalid.",
            "The destination cannot hold all morph tangent deltas.");
    });
}

CNA_Result cna_morph_target_data_ext_get_recompute_flat_normals_ext(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    CNA_Bool* const outRecompute)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRecompute == nullptr) {
            return InvalidArgument(
                "The MorphTargetDataEXT flat-normal output is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRecompute = data->value->RecomputeFlatNormalsEXT ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_set_recompute_flat_normals_ext(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const CNA_Bool recompute)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (recompute != CNA_FALSE && recompute != CNA_TRUE) {
            return InvalidArgument(
                "The MorphTargetDataEXT flat-normal value is not a canonical C boolean.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        data->value->RecomputeFlatNormalsEXT = recompute == CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_copy_triangle_indices_ext(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outIndexCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyOutputValues(
            data->value->TriangleIndicesEXT,
            destination, capacity, outIndexCount,
            [](const std::uint32_t value) { return static_cast<uint32_t>(value); },
            "The MorphTargetDataEXT triangle-index output is invalid.",
            "The destination cannot hold all morph triangle indices.");
    });
}

CNA_Result cna_morph_target_data_ext_set_triangle_indices_ext(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint32_t* const indices,
    const uint64_t indexCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (indices == nullptr && indexCount != 0U) {
            return InvalidArgument("The morph triangle-index array is null.");
        }
        // Three indices per face: a count that is not a multiple of three describes no triangle
        // list at all, and the recomputation would read past the last complete face.
        if ((indexCount % 3U) != 0U) {
            return InvalidArgument(
                "The morph triangle-index count must be a multiple of three.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<std::uint32_t>& target = data->value->TriangleIndicesEXT;
        target.clear();
        target.reserve(static_cast<std::size_t>(indexCount));
        for (uint64_t index = 0U; index < indexCount; ++index) {
            target.push_back(static_cast<std::uint32_t>(indices[index]));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_morph_target_data_ext_set_tangent_deltas(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t targetIndex,
    const CNA_Vector3* const deltas,
    const uint64_t deltaCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (deltas == nullptr && deltaCount != 0U) {
            return InvalidArgument("The morph tangent-delta array is null.");
        }
        std::shared_ptr<MorphDataResource> data;
        if (const CNA_Result result = GetMorphData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // TangentDeltas is sized per target like the position and normal arrays, and a target
        // without a TANGENT delta keeps an empty inner vector rather than being absent, so the
        // outer array is grown to match the targets the data already has.
        if (targetIndex >= data->value->PositionDeltas.size()) {
            return InvalidArgument(
                "The MorphTargetDataEXT target index is outside the valid range.");
        }
        const std::size_t vertexCount =
            data->value->PositionDeltas[static_cast<std::size_t>(targetIndex)].size();
        if (deltaCount != 0U && static_cast<std::size_t>(deltaCount) != vertexCount) {
            return InvalidArgument(
                "The morph tangent-delta count must be zero or the target's vertex count.");
        }
        data->value->TangentDeltas.resize(data->value->PositionDeltas.size());
        std::vector<Microsoft::Xna::Framework::Vector3>& target =
            data->value->TangentDeltas[static_cast<std::size_t>(targetIndex)];
        target.clear();
        target.reserve(static_cast<std::size_t>(deltaCount));
        for (uint64_t index = 0U; index < deltaCount; ++index) {
            target.push_back(ToNativeVector3(deltas[index]));
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_ModelCameraEXT ToC(const ModelCameraEXT& value) noexcept
{
    return CNA_ModelCameraEXT{
        .struct_size = sizeof(CNA_ModelCameraEXT),
        .struct_version = ReportStructureVersion,
        .scene_node_index = value.SceneNodeIndex,
        .is_perspective = value.IsPerspective ? CNA_TRUE : CNA_FALSE,
        .has_infinite_far_plane = value.HasInfiniteFarPlane ? CNA_TRUE : CNA_FALSE,
        .has_authored_aspect_ratio = value.HasAuthoredAspectRatio ? CNA_TRUE : CNA_FALSE,
        .projection = ToC(value.Projection),
        .world_transform = ToC(value.WorldTransform),
        .aspect_ratio = value.AspectRatio,
        .field_of_view = value.FieldOfView,
        .near_plane_distance = value.NearPlaneDistance,
        .far_plane_distance = value.FarPlaneDistance};
}

[[nodiscard]] bool IsModelCamera(const CNA_ModelCameraEXT* const value) noexcept
{
    return value != nullptr && value->struct_size >= sizeof(CNA_ModelCameraEXT) &&
        value->struct_version == ReportStructureVersion;
}

[[nodiscard]] CNA_Result GetModelCamera(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    std::shared_ptr<ModelResource>* const outModel,
    const ModelCameraEXT** const outCamera)
{
    if (const CNA_Result result = GetModel(modelHandle, outModel);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<ModelCameraEXT>& cameras = (*outModel)->value->getCamerasEXTProperty();
    if (index >= cameras.size()) {
        return InvalidArgument("The Model camera index is outside the valid range.");
    }
    *outCamera = &cameras[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetModelSkin(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    std::shared_ptr<ModelResource>* const outModel,
    const ModelSkinEXT** const outSkin)
{
    if (const CNA_Result result = GetModel(modelHandle, outModel);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<ModelSkinEXT>& skins = (*outModel)->value->getSkinsEXTProperty();
    if (index >= skins.size()) {
        return InvalidArgument("The Model skin index is outside the valid range.");
    }
    *outSkin = &skins[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyModelString(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    const char* const invalidMessage,
    const char* const smallMessage)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(invalidMessage);
    }
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, smallMessage);
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_model_get_camera_count_ext(
    const CNA_ModelHandle modelHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The Model camera-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(model->value->getCamerasEXTProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_camera_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_ModelCameraEXT* const outCamera)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsModelCamera(outCamera)) {
            return InvalidArgument("The Model camera output is null or malformed.");
        }
        std::shared_ptr<ModelResource> model;
        const ModelCameraEXT* camera = nullptr;
        if (const CNA_Result result = GetModelCamera(modelHandle, index, &model, &camera);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCamera = ToC(*camera);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_camera_name_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The Model camera-name byte-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        const ModelCameraEXT* camera = nullptr;
        if (const CNA_Result result = GetModelCamera(modelHandle, index, &model, &camera);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(camera->Name.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_camera_name_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const ModelCameraEXT* camera = nullptr;
        if (const CNA_Result result = GetModelCamera(modelHandle, index, &model, &camera);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyModelString(
            camera->Name, destination, capacity, outByteCount,
            "The Model camera-name output is invalid.",
            "The destination cannot hold the complete Model camera name.");
    });
}

CNA_Result cna_model_clear_cameras_ext(const CNA_ModelHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->setCamerasEXTProperty({});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_add_camera_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_ModelCameraDescriptorEXT* const descriptor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (descriptor == nullptr || !IsModelCamera(&descriptor->camera)) {
            return InvalidArgument("The Model camera descriptor is null or malformed.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ModelCameraEXT camera;
        if (const CNA_Result result = CopyStringView(descriptor->name, true, &camera.Name);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The Model camera name is not valid UTF-8 text.");
        }
        camera.SceneNodeIndex = descriptor->camera.scene_node_index;
        camera.Projection = ToNative(descriptor->camera.projection);
        camera.WorldTransform = ToNative(descriptor->camera.world_transform);
        camera.IsPerspective = descriptor->camera.is_perspective == CNA_TRUE;
        camera.HasInfiniteFarPlane = descriptor->camera.has_infinite_far_plane == CNA_TRUE;
        camera.AspectRatio = descriptor->camera.aspect_ratio;
        camera.HasAuthoredAspectRatio = descriptor->camera.has_authored_aspect_ratio == CNA_TRUE;
        camera.FieldOfView = descriptor->camera.field_of_view;
        camera.NearPlaneDistance = descriptor->camera.near_plane_distance;
        camera.FarPlaneDistance = descriptor->camera.far_plane_distance;

        std::vector<ModelCameraEXT> cameras = model->value->getCamerasEXTProperty();
        cameras.push_back(std::move(camera));
        model->value->setCamerasEXTProperty(std::move(cameras));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_skin_count_ext(
    const CNA_ModelHandle modelHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The Model skin-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(model->value->getSkinsEXTProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_skin_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_Bool* const outHasData,
    uint64_t* const outMeshCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasData == nullptr || outMeshCount == nullptr) {
            return InvalidArgument("The Model skin outputs are null.");
        }
        std::shared_ptr<ModelResource> model;
        const ModelSkinEXT* skin = nullptr;
        if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasData = skin->Data != nullptr ? CNA_TRUE : CNA_FALSE;
        *outMeshCount = static_cast<uint64_t>(skin->Meshes.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_create_skin_skeleton_handle_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_SkinningDataHandle* const outData)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outData == nullptr) {
            return InvalidArgument("The Model skin skeleton output handle is null.");
        }
        *outData = CNA_INVALID_HANDLE;
        std::shared_ptr<ModelResource> model;
        const ModelSkinEXT* skin = nullptr;
        if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (skin->Data == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Model skin names no skeleton.");
        }
        const std::size_t skinIndex = static_cast<std::size_t>(index);
        if (skinIndex >= model->skinSkeletons.size()) {
            model->skinSkeletons.resize(skinIndex + 1U);
        }
        if (model->skinSkeletons[skinIndex] == nullptr) {
            // A skin this ABI built has its skeleton in skinSkeletons already. One that arrived
            // with a loaded Model -- a glTF import, say -- does not, and used to be refused with
            // "the skeleton was not created through the C API": a refusal models.h does not
            // document, on a skin cna_model_get_skin_ext had just reported as having data. The
            // skeleton was reachable the whole time.
            //
            // Model.hpp's own guarantee is what makes publishing it safe: "Data and every mesh
            // pointer are owned by the Model's content resources and remain valid for the Model's
            // lifetime." So the handle aliases the Model, the way every other borrowed piece of a
            // loaded Model does, and keeps it alive for as long as it exists. Cached, so a second
            // read answers the same resource rather than minting a rival for one skeleton.
            auto adopted = std::make_shared<SkinningDataResource>();
            adopted->value = std::shared_ptr<SkinningData>(model->value, skin->Data);
            model->skinSkeletons[skinIndex] = std::move(adopted);
        }
        return CreateSkinningDataHandle(model->skinSkeletons[skinIndex], outData);
    });
}

CNA_Result cna_model_get_skin_name_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The Model skin-name byte-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        const ModelSkinEXT* skin = nullptr;
        if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(skin->Name.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_skin_name_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        const ModelSkinEXT* skin = nullptr;
        if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyModelString(
            skin->Name, destination, capacity, outByteCount,
            "The Model skin-name output is invalid.",
            "The destination cannot hold the complete Model skin name.");
    });
}

CNA_Result cna_model_get_skin_mesh_index_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    const uint64_t meshIndex,
    uint64_t* const outModelMeshIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModelMeshIndex == nullptr) {
            return InvalidArgument("The Model skin mesh-index output is null.");
        }
        std::shared_ptr<ModelResource> model;
        const ModelSkinEXT* skin = nullptr;
        if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (meshIndex >= skin->Meshes.size()) {
            return InvalidArgument("The Model skin mesh index is outside the valid range.");
        }
        const ModelMesh* const mesh = skin->Meshes[static_cast<std::size_t>(meshIndex)];
        for (std::size_t candidate = 0U; candidate < model->meshes.size(); ++candidate) {
            if (model->meshes[candidate]->value.get() == mesh) {
                *outModelMeshIndex = static_cast<uint64_t>(candidate);
                return CNA_RESULT_SUCCESS;
            }
        }
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The Model skin names a mesh this Model does not own.");
    });
}

CNA_Result cna_model_clear_skins_ext(const CNA_ModelHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->setSkinsEXTProperty({});
        model->skinSkeletons.clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_add_skin_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_StringView name,
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t* const meshIndices,
    const uint64_t meshIndexCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (meshIndices == nullptr && meshIndexCount != 0U) {
            return InvalidArgument("The Model skin mesh-index array is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SkinningDataResource> data;
        if (dataHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = GetSkinningData(dataHandle, &data);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        ModelSkinEXT skin;
        if (const CNA_Result result = CopyStringView(name, true, &skin.Name);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The Model skin name is not valid UTF-8 text.");
        }
        skin.Meshes.reserve(static_cast<std::size_t>(meshIndexCount));
        for (uint64_t entry = 0U; entry < meshIndexCount; ++entry) {
            if (meshIndices[entry] >= model->meshes.size()) {
                return InvalidArgument("A Model skin mesh index is outside the valid range.");
            }
            skin.Meshes.push_back(
                model->meshes[static_cast<std::size_t>(meshIndices[entry])]->value.get());
        }
        // The skeleton is borrowed by the canonical type, so the model holds a strong reference
        // beside it: without one, destroying the caller's handle would leave ModelSkinEXT::Data
        // pointing at freed memory the next reader would follow.
        skin.Data = data != nullptr ? data->value.get() : nullptr;

        std::vector<ModelSkinEXT> skins = model->value->getSkinsEXTProperty();
        skins.push_back(std::move(skin));
        model->value->setSkinsEXTProperty(std::move(skins));
        model->skinSkeletons.push_back(std::move(data));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_bounding_sphere_ext(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasValue,
    CNA_BoundingSphere* const outSphere)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasValue == nullptr || outSphere == nullptr) {
            return InvalidArgument("The Model bounding-sphere outputs are null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::optional<BoundingSphere> sphere =
            model->value->getBoundingSphereEXTProperty();
        *outHasValue = sphere.has_value() ? CNA_TRUE : CNA_FALSE;
        *outSphere = sphere.has_value() ? ToC(*sphere) : CNA_BoundingSphere{};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_material_variant_count_ext(
    const CNA_ModelHandle modelHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The Model material-variant count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount =
            static_cast<uint64_t>(model->value->getMaterialVariantNamesEXTProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_material_variant_name_byte_count_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The Model material-variant byte-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string>& names =
            model->value->getMaterialVariantNamesEXTProperty();
        if (index >= names.size()) {
            return InvalidArgument("The Model material-variant index is outside the valid range.");
        }
        *outByteCount = static_cast<uint64_t>(names[static_cast<std::size_t>(index)].size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_copy_material_variant_name_ext(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string>& names =
            model->value->getMaterialVariantNamesEXTProperty();
        if (index >= names.size()) {
            return InvalidArgument("The Model material-variant index is outside the valid range.");
        }
        return CopyModelString(
            names[static_cast<std::size_t>(index)], destination, capacity, outByteCount,
            "The Model material-variant name output is invalid.",
            "The destination cannot hold the complete Model material-variant name.");
    });
}

CNA_Result cna_model_get_material_variant_ext(
    const CNA_ModelHandle modelHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The Model material-variant output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(model->value->getMaterialVariantEXTProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_set_material_variant_ext(
    const CNA_ModelHandle modelHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->setMaterialVariantEXTProperty(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_matrix_create_infinite_perspective_field_of_view_ext(
    const float fieldOfView,
    const float aspectRatio,
    const float nearPlaneDistance,
    CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMatrix == nullptr) {
            return InvalidArgument("The infinite-perspective matrix output is null.");
        }
        *outMatrix = ToC(Microsoft::Xna::Framework::Graphics::
                                   CreateInfinitePerspectiveFieldOfViewEXT(
                                       fieldOfView, aspectRatio, nearPlaneDistance));
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

struct ModelAnimationsResource final {
    std::shared_ptr<ModelAnimationsEXT> value = std::make_shared<ModelAnimationsEXT>();
};

[[nodiscard]] std::vector<std::string> SortedAnimationClipNames(
    const ModelAnimationsEXT& animations)
{
    std::vector<std::string> names;
    names.reserve(animations.Clips.size());
    for (const auto& [name, ignored] : animations.Clips) {
        static_cast<void>(ignored);
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] CNA_Result GetModelAnimations(
    const CNA_ModelAnimationsEXTHandle handle,
    std::shared_ptr<ModelAnimationsResource>* const outAnimations)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ModelAnimationsEXT, outAnimations);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The ModelAnimationsEXT handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetAnimationClipAt(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    std::shared_ptr<ModelAnimationsResource>* const outAnimations,
    std::string* const outName)
{
    if (const CNA_Result result = GetModelAnimations(handle, outAnimations);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<std::string> names = SortedAnimationClipNames(*(*outAnimations)->value);
    if (clipIndex >= names.size()) {
        return InvalidArgument("The ModelAnimationsEXT clip index is outside the valid range.");
    }
    *outName = names[static_cast<std::size_t>(clipIndex)];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_model_animations_ext_create(
    const CNA_NamedAnimationClipEXTDescriptor* const clips,
    const uint64_t clipCount,
    CNA_ModelAnimationsEXTHandle* const outAnimations)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnimations == nullptr) {
            return InvalidArgument("The ModelAnimationsEXT output handle is null.");
        }
        *outAnimations = CNA_INVALID_HANDLE;
        if (clips == nullptr && clipCount != 0U) {
            return InvalidArgument("The ModelAnimationsEXT clip array is null.");
        }
        auto resource = std::make_shared<ModelAnimationsResource>();
        if (clipCount > resource->value->Clips.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The ModelAnimationsEXT clip count is too large.");
        }
        resource->value->Clips.reserve(static_cast<std::size_t>(clipCount));
        for (uint64_t index = 0U; index < clipCount; ++index) {
            const CNA_NamedAnimationClipEXTDescriptor& source = clips[index];
            std::string name;
            if (const CNA_Result result = CopyStringView(source.name, true, &name);
                result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result, ErrorCategoryForResult(result),
                    "A ModelAnimationsEXT clip name is not valid UTF-8 text.");
            }
            if (resource->value->Clips.contains(name)) {
                return InvalidArgument("ModelAnimationsEXT clip names must be unique.");
            }
            AnimationClipEXT clip;
            if (const CNA_Result result = CopyAnimationClipDescriptor(source.clip, &clip);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            resource->value->Clips.emplace(std::move(name), std::move(clip));
        }
        return GetRuntimeHandles().Create(
            ObjectKind::ModelAnimationsEXT, std::move(resource), outAnimations);
    });
}

CNA_Result cna_model_animations_ext_destroy(const CNA_ModelAnimationsEXTHandle handle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelAnimationsResource> animations;
        if (const CNA_Result result = GetModelAnimations(handle, &animations);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(handle);
        return result == CNA_RESULT_SUCCESS
            ? CNA_RESULT_SUCCESS
            : Fail(
                result, ErrorCategoryForResult(result),
                "The ModelAnimationsEXT handle could not be released.");
    });
}

CNA_Result cna_model_animations_ext_get_type_name_byte_count(
    const CNA_ModelAnimationsEXTHandle handle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The ModelAnimationsEXT type-name byte-count output is null.");
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        if (const CNA_Result result = GetModelAnimations(handle, &animations);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(animations->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_animations_ext_copy_type_name(
    const CNA_ModelAnimationsEXTHandle handle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelAnimationsResource> animations;
        if (const CNA_Result result = GetModelAnimations(handle, &animations);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyModelString(
            animations->value->GetTypeName(), destination, capacity, outByteCount,
            "The ModelAnimationsEXT type-name output is invalid.",
            "The destination cannot hold the complete ModelAnimationsEXT type name.");
    });
}

CNA_Result cna_model_animations_ext_get_clip_count(
    const CNA_ModelAnimationsEXTHandle handle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The ModelAnimationsEXT clip-count output is null.");
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        if (const CNA_Result result = GetModelAnimations(handle, &animations);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(animations->value->Clips.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_animations_ext_get_clip_name_byte_count_at(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The ModelAnimationsEXT clip-name byte-count output is null.");
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        std::string name;
        if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(name.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_animations_ext_copy_clip_name_at(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelAnimationsResource> animations;
        std::string name;
        if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyModelString(
            name, destination, capacity, outByteCount,
            "The ModelAnimationsEXT clip-name output is invalid.",
            "The destination cannot hold the complete ModelAnimationsEXT clip name.");
    });
}

CNA_Result cna_model_animations_ext_get_clip_info_at(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount,
    CNA_ClipTargetSpaceEXT* const outTargetSpace)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDurationSeconds == nullptr || outTrackCount == nullptr ||
            outTargetSpace == nullptr) {
            return InvalidArgument("The ModelAnimationsEXT clip-info outputs are null.");
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        std::string name;
        if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const AnimationClipEXT& clip = animations->value->Clips.at(name);
        *outDurationSeconds = clip.Duration.getTotalSecondsProperty();
        *outTrackCount = static_cast<uint64_t>(clip.Tracks.size());
        *outTargetSpace = static_cast<CNA_ClipTargetSpaceEXT>(clip.TargetSpace);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_animations_ext_set_clip_target_space_at(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    const CNA_ClipTargetSpaceEXT value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value > CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT) {
            return InvalidArgument("The clip target space is not a defined identity.");
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        std::string name;
        if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        animations->value->Clips.at(name).TargetSpace =
            static_cast<ClipTargetSpaceEXT>(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_apply_clip_to_bones_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_ModelAnimationsEXTHandle animationsHandle,
    const uint64_t clipIndex,
    const double timeSeconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ModelAnimationsResource> animations;
        std::string name;
        if (const CNA_Result result =
                GetAnimationClipAt(animationsHandle, clipIndex, &animations, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!IsValidTimeSpanSeconds(timeSeconds)) {
            return InvalidArgument("The clip evaluation time is not a valid TimeSpan.");
        }
        const System::TimeSpan time = System::TimeSpan::FromSeconds(timeSeconds);
        // A joint-palette clip's indices name palette slots, so applying them to Model::Bones
        // would pose the wrong bones without saying so. The canonical helper throws; the barrier
        // turns that into INVALID_ARGUMENT rather than letting it pass as success.
        ApplyClipToBonesEXT(*model->value, animations->value->Clips.at(name), time);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_apply_bind_pose_bone_transforms_ext(
    const CNA_ModelHandle modelHandle,
    const CNA_SkinningDataHandle dataHandle,
    uint64_t* const outPosedCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPosedCount == nullptr) {
            return InvalidArgument("The bind-pose posed-count output is null.");
        }
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPosedCount = static_cast<uint64_t>(
            ApplyBindPoseBoneTransformsEXT(*model->value, *data->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_get_skeleton_root_node_index_ext(
    const CNA_SkinningDataHandle dataHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The SkinningData skeleton-root index output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(data->value->SkeletonRootNodeIndexEXT);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_set_skeleton_root_node_index_ext(
    const CNA_SkinningDataHandle dataHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        data->value->SkeletonRootNodeIndexEXT = static_cast<int>(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_get_skeleton_root_name_byte_count_ext(
    const CNA_SkinningDataHandle dataHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The SkinningData skeleton-root name output is null.");
        }
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(data->value->SkeletonRootNameEXT.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_skinning_data_copy_skeleton_root_name_ext(
    const CNA_SkinningDataHandle dataHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyModelString(
            data->value->SkeletonRootNameEXT, destination, capacity, outByteCount,
            "The SkinningData skeleton-root name output is invalid.",
            "The destination cannot hold the complete SkinningData skeleton-root name.");
    });
}

CNA_Result cna_skinning_data_set_skeleton_root_name_ext(
    const CNA_SkinningDataHandle dataHandle,
    const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SkinningDataResource> data;
        if (const CNA_Result result = GetSkinningData(dataHandle, &data);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copied;
        if (const CNA_Result result = CopyStringView(name, true, &copied);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The SkinningData skeleton-root name is not valid UTF-8 text.");
        }
        data->value->SkeletonRootNameEXT = std::move(copied);
        return CNA_RESULT_SUCCESS;
    });
}


/* --- CBIND-118: loading a Model from content -------------------------------------------------- */

namespace {

/// Borrows a pointer that lives inside the loaded model's owned resources.
///
/// `Model` is a lightweight copy-constructible handle whose real contents sit behind one
/// `shared_ptr<void>` its copies share, and every collection it exposes hands out raw pointers into
/// that bundle. The aliasing constructor is the exact fit: the result points at the borrowed object
/// and keeps the model that owns it alive, which is what lets a part's effect or buffer become a
/// handle without anyone taking a second ownership of it.
template <typename T>
[[nodiscard]] std::shared_ptr<T> BorrowFromModel(
    const std::shared_ptr<Model>& owner,
    T* const pointer)
{
    return pointer == nullptr ? std::shared_ptr<T>() : std::shared_ptr<T>(owner, pointer);
}

/// Publishes one borrowed graphics resource as a handle, once per distinct object.
///
/// Two parts of one mesh routinely share an effect, and the canonical model expects the mesh's
/// effect collection to hold each one once; publishing per part would hand out two handles for one
/// object and make that collection wrong. The map is keyed by the object address for that reason,
/// not as an optimization.
template <typename TResource, typename TValue>
[[nodiscard]] CNA_Result PublishModelResource(
    const std::shared_ptr<Model>& owner,
    TValue* const pointer,
    const CNA_Handle parentGame,
    const ObjectKind kind,
    std::map<const void*, CNA_Handle>& published,
    std::vector<CNA_Handle>& ownedHandles,
    CNA_Handle* const outHandle)
{
    *outHandle = CNA_INVALID_HANDLE;
    if (pointer == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    const auto existing = published.find(static_cast<const void*>(pointer));
    if (existing != published.end()) {
        *outHandle = existing->second;
        return CNA_RESULT_SUCCESS;
    }
    auto resource = std::make_shared<TResource>();
    resource->value = BorrowFromModel(owner, pointer);
    resource->parentGame = parentGame;
    CNA_Handle handle = CNA_INVALID_HANDLE;
    if (const CNA_Result result = GetRuntimeHandles().Create(kind, resource, &handle);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "A loaded Model resource could not be published as a handle.");
    }
    published.emplace(static_cast<const void*>(pointer), handle);
    ownedHandles.push_back(handle);
    *outHandle = handle;
    return CNA_RESULT_SUCCESS;
}

/// Rebuilds the C resource graph over a model the content pipeline produced.
///
/// The bone and mesh accessors answer from this graph rather than from the canonical model, so a
/// loaded model that skipped it would report no bones and no meshes while looking valid. Every node
/// borrows: nothing here copies a bone, a mesh or a part, and the loaded model stays the owner.
[[nodiscard]] CNA_Result MirrorLoadedModel(
    std::shared_ptr<Model> loaded,
    const CNA_Handle parentGame,
    const bool supportsThreeD,
    std::shared_ptr<ModelResource>* const outModel)
{
    auto model = std::make_shared<ModelResource>();
    model->value = std::move(loaded);
    model->supportsThreeD = supportsThreeD;

    const ModelBoneCollection& bones = model->value->getBonesProperty();
    std::map<const ModelBone*, std::shared_ptr<BoneNode>> boneNodes;
    model->bones.reserve(bones.getCountProperty() < 0
        ? 0U
        : static_cast<std::size_t>(bones.getCountProperty()));
    for (int index = 0; index < bones.getCountProperty(); ++index) {
        ModelBone* const bone = bones[index];
        auto node = std::make_shared<BoneNode>();
        node->value = BorrowFromModel(model->value, bone);
        boneNodes.emplace(bone, node);
        model->bones.push_back(std::move(node));
    }
    // A second pass, because a bone's parent and children are other bones and the first pass is
    // what makes every one of them findable.
    for (const std::shared_ptr<BoneNode>& node : model->bones) {
        const ModelBone* const bone = node->value.get();
        const auto parent = boneNodes.find(bone->getParentProperty());
        if (parent != boneNodes.end()) {
            node->parent = parent->second;
        }
        const ModelBoneCollection& children = bone->getChildrenProperty();
        node->children.reserve(children.getCountProperty() < 0
            ? 0U
            : static_cast<std::size_t>(children.getCountProperty()));
        for (int index = 0; index < children.getCountProperty(); ++index) {
            const auto child = boneNodes.find(children[index]);
            if (child != boneNodes.end()) {
                node->children.push_back(child->second);
            }
        }
    }
    if (const auto root = boneNodes.find(model->value->getRootProperty());
        root != boneNodes.end()) {
        model->root = root->second;
    }

    std::map<const void*, CNA_Handle> published;
    const ModelMeshCollection& meshes = model->value->getMeshesProperty();
    model->meshes.reserve(meshes.getCountProperty() < 0
        ? 0U
        : static_cast<std::size_t>(meshes.getCountProperty()));
    for (int meshIndex = 0; meshIndex < meshes.getCountProperty(); ++meshIndex) {
        ModelMesh* const nativeMesh = meshes[meshIndex];
        auto mesh = std::make_shared<MeshResource>();
        mesh->value = BorrowFromModel(model->value, nativeMesh);
        mesh->parentGame = parentGame;
        mesh->supportsThreeD = supportsThreeD;
        if (const auto parent = boneNodes.find(nativeMesh->getParentBoneProperty());
            parent != boneNodes.end()) {
            mesh->parentBone = parent->second;
        }

        const ModelMeshPartCollection& parts = nativeMesh->getMeshPartsProperty();
        mesh->parts.reserve(parts.getCountProperty() < 0
            ? 0U
            : static_cast<std::size_t>(parts.getCountProperty()));
        for (int partIndex = 0; partIndex < parts.getCountProperty(); ++partIndex) {
            ModelMeshPart* const nativePart = parts[partIndex];
            auto part = std::make_shared<PartResource>();
            part->value = BorrowFromModel(model->value, nativePart);
            part->parentMesh = mesh.get();
            mesh->parts.push_back(std::move(part));
        }
        model->meshes.push_back(std::move(mesh));
    }

    // The retained slots are filled through the ordinary setters, so a loaded part reaches exactly
    // the state a hand-built one reaches -- including the mesh effect collection each one maintains.
    // Re-setting the pointer the part already holds is a no-op on the canonical side.
    for (const std::shared_ptr<MeshResource>& mesh : model->meshes) {
        for (const std::shared_ptr<PartResource>& part : mesh->parts) {
            ModelMeshPart* const nativePart = part->value.get();
            CNA_Handle handle = CNA_INVALID_HANDLE;
            if (const CNA_Result result = PublishModelResource<EffectResource>(
                    model->value, nativePart->getEffectProperty(), parentGame,
                    ObjectKind::Effect, published, model->contentOwnedHandles, &handle);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (handle != CNA_INVALID_HANDLE) {
                std::shared_ptr<EffectResource> effect;
                if (const CNA_Result result = ResolveEffect(handle, &effect);
                    result == CNA_RESULT_SUCCESS) {
                    // A model-owned effect is not the caller's to dispose.
                    effect->disposeAllowed = false;
                }
                if (const CNA_Result result = SetPartEffect(part, handle);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
            }
            if (const CNA_Result result = PublishModelResource<VertexBufferResource>(
                    model->value, nativePart->getVertexBufferProperty(), parentGame,
                    ObjectKind::VertexBuffer, published, model->contentOwnedHandles, &handle);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (handle != CNA_INVALID_HANDLE) {
                if (const CNA_Result result = SetPartVertexBuffer(part, handle);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
            }
            if (const CNA_Result result = PublishModelResource<IndexBufferResource>(
                    model->value, nativePart->getIndexBufferProperty(), parentGame,
                    ObjectKind::IndexBuffer, published, model->contentOwnedHandles, &handle);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (handle != CNA_INVALID_HANDLE) {
                if (const CNA_Result result = SetPartIndexBuffer(part, handle);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
            }
        }
    }

    *outModel = std::move(model);
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_content_manager_load_model(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_ModelHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The loaded Model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        std::string assetNameText;
        if (const CNA_Result result = CopyStringView(assetName, true, &assetNameText);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The content asset name is not valid UTF-8.");
        }
        if (assetNameText.empty()) {
            return InvalidArgument("The content asset name must not be empty.");
        }
        CNA::C::Detail::BorrowedContentManager contentManager;
        if (const CNA_Result result = CNA::C::Detail::BorrowContentManager(
                contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (contentManager.value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_STATE,
                "The content manager handle does not name a manager.");
        }

        std::shared_ptr<Model> loaded;
        bool supportsThreeD = false;
        try {
            supportsThreeD = contentManager.value->getGraphicsDeviceInternal()
                .SupportsCapability(CNA::GraphicsCapability::ThreeD);
            loaded = std::make_shared<Model>(contentManager.value->Load<Model>(assetNameText));
        } catch (const Microsoft::Xna::Framework::Content::ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // The asset's root reader produced something that is not a Model. Reporting the
            // mismatch beats the exception barrier's catch-all calling it an internal fault.
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader did not produce a Model.");
        }

        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = MirrorLoadedModel(
                std::move(loaded), contentManager.parentGame, supportsThreeD, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateModelHandle(std::move(model), outModel);
    });
}

/* --- CBIND-118: the Tag the content pipeline wrote --------------------------------------------- */

CNA_Result cna_model_get_content_tag_dictionary_ext(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasTag,
    CNA_ObjectDictionaryHandle* const outDictionary)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTag == nullptr || outDictionary == nullptr) {
            return InvalidArgument("The Model content-tag outputs are null.");
        }
        *outHasTag = CNA_FALSE;
        *outDictionary = CNA_INVALID_HANDLE;
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const tag = dynamic_cast<CNA::Content::ObjectDictionaryEXT*>(
            model->value->getTagProperty());
        if (tag == nullptr) {
            // No tag, or one of another shape. Not an error: XNA's unset Tag is null, and a caller
            // asking the wrong question about a present tag learns that from out_has_tag.
            return CNA_RESULT_SUCCESS;
        }
        // Aliased onto the model, so the handle keeps the loaded asset alive by itself and a caller
        // that destroys the model first is left with a dictionary rather than with a dangling one.
        if (const CNA_Result result = CNA::C::Detail::PublishObjectDictionary(
                std::shared_ptr<CNA::Content::ObjectDictionaryEXT>(model->value, tag),
                outDictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasTag = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_model_get_content_tag_foreign_object_ext(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasTag,
    void** const outObject)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasTag == nullptr || outObject == nullptr) {
            return InvalidArgument("The Model content-tag outputs are null.");
        }
        *outHasTag = CNA_FALSE;
        *outObject = nullptr;
        std::shared_ptr<ModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        void* object = nullptr;
        if (!CNA::C::Detail::TryGetForeignReferenceObject(
                model->value->getTagProperty(), &object)) {
            return CNA_RESULT_SUCCESS;
        }
        *outObject = object;
        *outHasTag = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}
