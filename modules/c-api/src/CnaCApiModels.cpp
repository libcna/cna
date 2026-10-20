// SPDX-License-Identifier: MS-PL

#include "CNA/C/models.h"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
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

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidArgumentIf;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IoError;
using CNA::C::Detail::IsValidTimeSpanSeconds;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupportedIf;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToCSamplerState;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;
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
            return Overflow("The ModelMeshPart resource retention count overflowed.");
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

[[nodiscard]] CNA_Result GetBone(
    const CNA_ModelBoneHandle handle,
    std::shared_ptr<BoneResource>* const outBone)
{
    return LookupHandle(handle, ObjectKind::ModelBone, outBone);
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_ModelBoneCollectionHandle handle,
    std::shared_ptr<BoneCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::ModelBoneCollection, outCollection);
}

[[nodiscard]] CNA_Result CreateBoneHandle(
    std::shared_ptr<BoneNode> node,
    CNA_ModelBoneHandle* const outBone)
{
    const auto resource = std::make_shared<BoneResource>(
        BoneResource{std::move(node)});
    return CreateHandle(ObjectKind::ModelBone, resource, outBone);
}

[[nodiscard]] CNA_Result CreateCollectionHandle(
    std::shared_ptr<BoneCollectionResource> resource,
    CNA_ModelBoneCollectionHandle* const outCollection)
{
    return CreateHandle(ObjectKind::ModelBoneCollection, std::move(resource), outCollection);
}

[[nodiscard]] const std::vector<std::shared_ptr<BoneNode>>& CollectionNodes(
    const BoneCollectionResource& collection) noexcept
{
    return collection.parent != nullptr ? collection.parent->children : collection.nodes;
}

[[nodiscard]] CNA_Result GetPart(
    const CNA_ModelMeshPartHandle handle,
    std::shared_ptr<PartResource>* const outPart)
{
    return LookupHandle(handle, ObjectKind::ModelMeshPart, outPart);
}

[[nodiscard]] CNA_Result GetPartCollection(
    const CNA_ModelMeshPartCollectionHandle handle,
    std::shared_ptr<PartCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::ModelMeshPartCollection, outCollection);
}

[[nodiscard]] CNA_Result CreatePartHandle(
    std::shared_ptr<PartResource> part,
    CNA_ModelMeshPartHandle* const outPart)
{
    return CreateHandle(ObjectKind::ModelMeshPart, std::move(part), outPart);
}

[[nodiscard]] CNA_Result CreatePartCollectionHandle(
    std::shared_ptr<PartCollectionResource> collection,
    CNA_ModelMeshPartCollectionHandle* const outCollection)
{
    return CreateHandle(ObjectKind::ModelMeshPartCollection, std::move(collection), outCollection);
}

[[nodiscard]] CNA_Result GetMesh(
    const CNA_ModelMeshHandle handle,
    std::shared_ptr<MeshResource>* const outMesh)
{
    return LookupHandle(handle, ObjectKind::ModelMesh, outMesh);
}

[[nodiscard]] CNA_Result GetMeshCollection(
    const CNA_ModelMeshCollectionHandle handle,
    std::shared_ptr<MeshCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::ModelMeshCollection, outCollection);
}

[[nodiscard]] CNA_Result GetEffectCollection(
    const CNA_ModelEffectCollectionHandle handle,
    std::shared_ptr<EffectCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::ModelEffectCollection, outCollection);
}

[[nodiscard]] CNA_Result CreateMeshHandle(
    std::shared_ptr<MeshResource> mesh,
    CNA_ModelMeshHandle* const outMesh)
{
    return CreateHandle(ObjectKind::ModelMesh, std::move(mesh), outMesh);
}

[[nodiscard]] CNA_Result CreateMeshCollectionHandle(
    std::shared_ptr<MeshCollectionResource> collection,
    CNA_ModelMeshCollectionHandle* const outCollection)
{
    return CreateHandle(ObjectKind::ModelMeshCollection, std::move(collection), outCollection);
}

[[nodiscard]] CNA_Result CreateEffectCollectionHandle(
    std::shared_ptr<MeshResource> mesh,
    CNA_ModelEffectCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<EffectCollectionResource>();
    resource->mesh = std::move(mesh);
    return CreateHandle(ObjectKind::ModelEffectCollection, resource, outCollection);
}

[[nodiscard]] CNA_Result GetModel(
    const CNA_ModelHandle handle,
    std::shared_ptr<ModelResource>* const outModel)
{
    return LookupHandle(handle, ObjectKind::Model, outModel);
}

[[nodiscard]] CNA_Result CreateModelHandle(
    std::shared_ptr<ModelResource> model,
    CNA_ModelHandle* const outModel)
{
    return CreateHandle(ObjectKind::Model, std::move(model), outModel);
}

[[nodiscard]] CNA_Result GetMorphData(
    const CNA_MorphTargetDataEXTHandle handle,
    std::shared_ptr<MorphDataResource>* const outData)
{
    return LookupHandle(handle, ObjectKind::MorphTargetDataEXT, outData);
}

[[nodiscard]] CNA_Result CreateMorphDataHandle(
    std::shared_ptr<MorphDataResource> data,
    CNA_MorphTargetDataEXTHandle* const outData)
{
    return CreateHandle(ObjectKind::MorphTargetDataEXT, std::move(data), outData);
}

[[nodiscard]] CNA_Result GetSkinnedModel(
    const CNA_SkinnedModelEXTHandle handle,
    std::shared_ptr<SkinnedModelResource>* const outModel)
{
    return LookupHandle(handle, ObjectKind::SkinnedModelEXT, outModel);
}

[[nodiscard]] CNA_Result CreateSkinnedModelHandle(
    std::shared_ptr<SkinnedModelResource> model,
    CNA_SkinnedModelEXTHandle* const outModel)
{
    return CreateHandle(ObjectKind::SkinnedModelEXT, std::move(model), outModel);
}

[[nodiscard]] CNA_Result GetSkinningData(
    const CNA_SkinningDataHandle handle,
    std::shared_ptr<SkinningDataResource>* const outData)
{
    return LookupHandle(handle, ObjectKind::SkinningData, outData);
}

[[nodiscard]] CNA_Result CreateSkinningDataHandle(
    std::shared_ptr<SkinningDataResource> data,
    CNA_SkinningDataHandle* const outData)
{
    return CreateHandle(ObjectKind::SkinningData, std::move(data), outData);
}

[[nodiscard]] CNA_Result GetAnimationPlayer(
    const CNA_AnimationPlayerHandle handle,
    std::shared_ptr<AnimationPlayerResource>* const outPlayer)
{
    return LookupHandle(handle, ObjectKind::AnimationPlayer, outPlayer);
}

[[nodiscard]] CNA_Result CreateAnimationPlayerHandle(
    std::shared_ptr<AnimationPlayerResource> player,
    CNA_AnimationPlayerHandle* const outPlayer)
{
    return CreateHandle(ObjectKind::AnimationPlayer, std::move(player), outPlayer);
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
        return Overflow("The AnimationClipEXT track count is too large.");
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
            return Overflow("The BoneTrackEXT keyframe count is too large.");
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
        return Overflow("The SkinningData clip count is too large.");
    }
    copied.AnimationClips.reserve(static_cast<std::size_t>(descriptor.clip_count));
    for (uint64_t index = 0U; index < descriptor.clip_count; ++index) {
        const CNA_NamedAnimationClipEXTDescriptor& source = descriptor.clips[index];
        std::string name;
        if (const CNA_Result result = ReadText(
                source.name, "A SkinningData clip name is not valid UTF-8 text.", &name,
                EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        if (!IsValidTimeSpanSeconds(key.time_seconds)) {
            return InvalidArgument("Morph track keyframe times must fit a finite TimeSpan.");
        }
        if (index != 0U && key.time_seconds < previousTime) {
            return InvalidArgument("Morph track keyframe times must be in ascending order.");
        }
        previousTime = key.time_seconds;
        if (!hasTrackWeightCount) {
            if (key.weight_count > static_cast<uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                return Overflow("The MorphWeightTrackEXT weight count is too large.");
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
        return BufferTooSmall(smallMessage);
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
    const CNA_Result result = LookupHandle(handle, ObjectKind::Effect, outEffect);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outEffect)->value->getIsDisposedProperty()) {
        return InvalidState("A disposed Effect cannot be assigned to a ModelMeshPart.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveVertexBuffer(
    const CNA_VertexBufferHandle handle,
    std::shared_ptr<VertexBufferResource>* const outBuffer)
{
    const CNA_Result result = LookupHandle(handle, ObjectKind::VertexBuffer, outBuffer);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outBuffer)->value->getIsDisposedProperty()) {
        return InvalidState("A disposed VertexBuffer cannot be assigned to a ModelMeshPart.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveIndexBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    const CNA_Result result = LookupHandle(handle, ObjectKind::IndexBuffer, outBuffer);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outBuffer)->value->getIsDisposedProperty()) {
        return InvalidState("A disposed IndexBuffer cannot be assigned to a ModelMeshPart.");
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
            return Overflow("The ModelMeshPart Effect retention count overflowed.");
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
        return InvalidState("A SkinnedModelEXT-owned part cannot replace its VertexBuffer.");
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
        return Overflow("The ModelMeshPart VertexBuffer retention count overflowed.");
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
        return InvalidState("A SkinnedModelEXT-owned part cannot replace its IndexBuffer.");
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
        return Overflow("The ModelMeshPart IndexBuffer retention count overflowed.");
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
        return Overflow("The ModelMesh part count is too large.");
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
            return InvalidState("A ModelMeshPart cannot belong to more than one live ModelMesh.");
        }
        if (part->activeSkinnedModelReferenceCount != 0U) {
            return InvalidState("A SkinnedModelEXT-owned part cannot also belong to a ModelMesh.");
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
        return Overflow("The Model collection count is too large.");
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
        return Overflow("The MorphTargetDataEXT target count is too large.");
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
            positions.push_back(ToNative(input.position_deltas[index]));
        }
        for (uint64_t index = 0U; index < input.normal_delta_count; ++index) {
            normals.push_back(ToNative(input.normal_deltas[index]));
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
        return Overflow("The MorphTargetDataEXT target count is too large.");
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
        return InvalidArgument("The SkinnedModelEXT output is null.");
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

namespace {

[[nodiscard]] CNA_Result ModelBoneCreateDefault(CNA_ModelBoneHandle* const outBone)
{
    auto node = std::make_shared<BoneNode>();
    node->value = std::make_shared<ModelBone>();
    return CreateBoneHandle(std::move(node), outBone);
}

[[nodiscard]] CNA_Result ModelBoneCreate(
    const int32_t index,
    const CNA_StringView name,
    CNA_ModelBoneHandle* const outBone)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The ModelBone name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto node = std::make_shared<BoneNode>();
    node->value = std::make_shared<ModelBone>(
        static_cast<int>(index), std::move(copiedName));
    return CreateBoneHandle(std::move(node), outBone);
}

[[nodiscard]] CNA_Result ModelBoneGetParent(
    const CNA_ModelBoneHandle boneHandle,
    CNA_Bool* const outHasParent,
    CNA_ModelBoneHandle* const outParent)
{
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
}

[[nodiscard]] CNA_Result ModelBoneAddChild(
    const CNA_ModelBoneHandle boneHandle,
    const CNA_ModelBoneHandle childHandle)
{
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
}

[[nodiscard]] CNA_Result ModelBoneCollectionGetAt(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_ModelBoneHandle* const outBone)
{
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
}

[[nodiscard]] CNA_Result ModelBoneCollectionFind(
    const CNA_ModelBoneCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_ModelBoneHandle* const outBone)
{
    *outFound = CNA_FALSE;
    *outBone = CNA_INVALID_HANDLE;
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The ModelBone lookup name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result ModelMeshPartCreate(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_IndexBufferHandle indexBufferHandle,
    const int32_t numVertices,
    const int32_t primitiveCount,
    const int32_t startIndex,
    const int32_t vertexOffset,
    CNA_ModelMeshPartHandle* const outPart)
{
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
}

} // namespace

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

namespace {

[[nodiscard]] CNA_Result ModelMeshPartSetNumVertices(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartSetPrimitiveCount(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartSetStartIndex(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartSetVertexOffset(
    const CNA_ModelMeshPartHandle partHandle,
    const int32_t value)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartGetEffect(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasEffect,
    CNA_EffectHandle* const outEffect)
{
    std::shared_ptr<PartResource> part;
    if (const CNA_Result result = GetPart(partHandle, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outHasEffect = part->effect.handle == CNA_INVALID_HANDLE ? CNA_FALSE : CNA_TRUE;
    *outEffect = part->effect.handle;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelMeshPartGetVertexBuffer(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasBuffer,
    CNA_VertexBufferHandle* const outBuffer)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartGetIndexBuffer(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasBuffer,
    CNA_IndexBufferHandle* const outBuffer)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartCollectionCreate(
    const CNA_ModelMeshPartHandle* const partHandles,
    const uint64_t partCount,
    CNA_ModelMeshPartCollectionHandle* const outCollection)
{
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
        return Overflow("The ModelMeshPart collection is too large.");
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
}

[[nodiscard]] CNA_Result ModelMeshPartCollectionGetAt(
    const CNA_ModelMeshPartCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_ModelMeshPartHandle* const outPart)
{
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
}

[[nodiscard]] CNA_Result ModelMeshCreateNamed(
    const CNA_Handle graphicsDevice,
    const CNA_StringView name,
    const CNA_ModelMeshPartHandle* const parts,
    const uint64_t partCount,
    CNA_ModelMeshHandle* const outMesh)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The ModelMesh name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateMesh(
        graphicsDevice, copiedName, true, parts, partCount, outMesh);
}

[[nodiscard]] CNA_Result ModelMeshGetMeshParts(
    const CNA_ModelMeshHandle meshHandle,
    CNA_ModelMeshPartCollectionHandle* const outParts)
{
    std::shared_ptr<MeshResource> mesh;
    if (const CNA_Result result = GetMesh(meshHandle, &mesh);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto collection = std::make_shared<PartCollectionResource>();
    collection->mesh = std::move(mesh);
    return CreatePartCollectionHandle(std::move(collection), outParts);
}

[[nodiscard]] CNA_Result ModelMeshGetParentBone(
    const CNA_ModelMeshHandle meshHandle,
    CNA_Bool* const outHasParent,
    CNA_ModelBoneHandle* const outParent)
{
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
}

[[nodiscard]] CNA_Result ModelMeshSetParentBone(
    const CNA_ModelMeshHandle meshHandle,
    const CNA_ModelBoneHandle parentHandle)
{
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
}

[[nodiscard]] CNA_Result ModelMeshCollectionCreate(
    const CNA_ModelMeshHandle* const meshHandles,
    const uint64_t meshCount,
    CNA_ModelMeshCollectionHandle* const outCollection)
{
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
        return Overflow("The ModelMesh collection is too large.");
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
}

[[nodiscard]] CNA_Result ModelMeshCollectionFind(
    const CNA_ModelMeshCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_ModelMeshHandle* const outMesh)
{
    *outFound = CNA_FALSE;
    *outMesh = CNA_INVALID_HANDLE;
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The ModelMesh lookup name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result ModelEffectCollectionGetAt(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectHandle* const outEffect)
{
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
}

[[nodiscard]] CNA_Result ModelEffectCollectionAdd(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const CNA_EffectHandle effectHandle)
{
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
        return Overflow("The ModelEffectCollection retention count overflowed.");
    }
    collection->mesh->effects.reserve(collection->mesh->effects.size() + 1U);
    collection->mesh->value->getEffectsPropertyMutable().Add(effect->value.get());
    AddMeshEffect(*collection->mesh, effectHandle, effect);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelEffectCollectionRemove(
    const CNA_ModelEffectCollectionHandle collectionHandle,
    const CNA_EffectHandle effectHandle)
{
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
}

[[nodiscard]] CNA_Result ModelCreateDefault(CNA_ModelHandle* const outModel)
{
    auto model = std::make_shared<ModelResource>();
    model->value = std::make_shared<Model>();
    return CreateModelHandle(std::move(model), outModel);
}

[[nodiscard]] CNA_Result ModelDestroy(const CNA_ModelHandle modelHandle)
{
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // CBIND-118: taken before the release, because the resource record is what holds them.
    const std::vector<CNA_Handle> published = model->contentOwnedHandles;
    if (const CNA_Result result = ReleaseHandle(modelHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // A content-loaded model published these; the caller never created them, so releasing the
    // model is what ends them. Each release is best-effort: a caller that already released one
    // by hand must not make destroying the model fail.
    for (const CNA_Handle handle : published) {
        static_cast<void>(GetRuntimeHandles().Release(handle));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelGetBones(
    const CNA_ModelHandle modelHandle,
    CNA_ModelBoneCollectionHandle* const outBones)
{
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto collection = std::make_shared<BoneCollectionResource>();
    collection->nodes = model->bones;
    return CreateCollectionHandle(std::move(collection), outBones);
}

[[nodiscard]] CNA_Result ModelGetMeshes(
    const CNA_ModelHandle modelHandle,
    CNA_ModelMeshCollectionHandle* const outMeshes)
{
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto collection = std::make_shared<MeshCollectionResource>();
    collection->meshes = model->meshes;
    return CreateMeshCollectionHandle(std::move(collection), outMeshes);
}

[[nodiscard]] CNA_Result ModelGetRoot(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasRoot,
    CNA_ModelBoneHandle* const outRoot)
{
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
}

[[nodiscard]] CNA_Result ModelSetOwnedResources(
    const CNA_ModelHandle modelHandle,
    void* const context,
    const CNA_ModelOwnedResourcesReleaseCallback release)
{
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
}

[[nodiscard]] CNA_Result ModelCopyAbsoluteBoneTransforms(
    const CNA_ModelHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The Model absolute-transform output is invalid.");
    }
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCount = static_cast<uint64_t>(model->bones.size());
    if (capacity < model->bones.size()) {
        return BufferTooSmall(
            "The destination cannot hold all absolute Model bone transforms.");
    }
    std::vector<Matrix> transforms(model->bones.size());
    model->value->CopyAbsoluteBoneTransformsTo(transforms);
    for (std::size_t index = 0U; index < transforms.size(); ++index) {
        destination[index] = ToC(transforms[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelSetBoneTransforms(
    const CNA_ModelHandle modelHandle,
    const CNA_Matrix* const source,
    const uint64_t count)
{
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
}

[[nodiscard]] CNA_Result ModelCopyBoneTransforms(
    const CNA_ModelHandle modelHandle,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The Model local-transform output is invalid.");
    }
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCount = static_cast<uint64_t>(model->bones.size());
    if (capacity < model->bones.size()) {
        return BufferTooSmall("The destination cannot hold all local Model bone transforms.");
    }
    std::vector<Matrix> transforms(model->bones.size());
    model->value->CopyBoneTransformsTo(transforms);
    for (std::size_t index = 0U; index < transforms.size(); ++index) {
        destination[index] = ToC(transforms[index]);
    }
    return CNA_RESULT_SUCCESS;
}

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

[[nodiscard]] CNA_Result ModelGetGltfImportReportExt(
    const CNA_ModelHandle modelHandle,
    CNA_GltfImportReportEXT* const outReport)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outReport, "The glTF import report output is null or malformed.",
            ReportStructureVersion);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        .anything_lost = ToCBool(report.AnythingLost())};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelSetGltfImportReportExt(
    const CNA_ModelHandle modelHandle,
    const CNA_GltfImportReportEXT* const report)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            report, "The glTF import report is null or malformed.", ReportStructureVersion);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result ModelAddGltfImportDiagnosticExt(
    const CNA_ModelHandle modelHandle,
    const CNA_GltfImportDiagnosticDescriptorEXT* const descriptor)
{
    if (descriptor == nullptr) {
        return InvalidArgument("The glTF import diagnostic descriptor is null.");
    }
    if (descriptor->severity > CNA_GLTF_IMPORT_SEVERITY_MAXIMUM_EXT) {
        return InvalidArgument(
            "The glTF import diagnostic severity is not a defined identity.");
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
    if (const CNA_Result result = ReadText(
            descriptor->code, "The glTF import diagnostic code is not valid UTF-8 text.",
            &entry.Code, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            descriptor->subject, "The glTF import diagnostic subject is not valid UTF-8 text.",
            &entry.Subject, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            descriptor->message, "The glTF import diagnostic message is not valid UTF-8 text.",
            &entry.Message, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    entry.Details.reserve(static_cast<std::size_t>(descriptor->detail_count));
    for (uint64_t index = 0U; index < descriptor->detail_count; ++index) {
        std::string detail;
        if (const CNA_Result result = ReadText(
                descriptor->details[index],
                "A glTF import diagnostic detail is not valid UTF-8 text.", &detail,
                EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
}

[[nodiscard]] CNA_Result ModelGetGltfImportDiagnosticExt(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_GltfImportDiagnosticEXT* const outDiagnostic)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outDiagnostic, "The glTF import diagnostic output is null or malformed.",
            ReportStructureVersion);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtCreate(
    const CNA_MorphTargetDataEXTDescriptor* const descriptor,
    CNA_MorphTargetDataEXTHandle* const outData)
{
    if (descriptor == nullptr) {
        return InvalidArgument("The MorphTargetDataEXT descriptor or output is null.");
    }
    MorphTargetDataEXT copied;
    if (const CNA_Result result = CopyMorphDataDescriptor(*descriptor, &copied);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<MorphDataResource>();
    resource->value = std::make_shared<MorphTargetDataEXT>(std::move(copied));
    return CreateMorphDataHandle(std::move(resource), outData);
}

[[nodiscard]] CNA_Result MorphTargetDataExtSetWeights(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const float* const weights,
    const uint64_t weightCount)
{
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
        return InvalidArgument(
            "The MorphTargetDataEXT weight count must equal its target count.");
    }
    data->value->Weights = std::move(copied);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MorphTargetDataExtSetWeightTrack(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const CNA_MorphWeightTrackEXTDescriptor* const track)
{
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtGetWeightTrackInfo(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    uint64_t* const outKeyframeCount,
    CNA_Bool* const outStepInterpolation,
    CNA_Bool* const outCubicSpline)
{
    std::shared_ptr<MorphDataResource> data;
    if (const CNA_Result result = GetMorphData(dataHandle, &data);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outKeyframeCount = static_cast<uint64_t>(data->value->WeightTrack.Keys.size());
    *outStepInterpolation = ToCBool(data->value->WeightTrack.StepInterpolation);
    *outCubicSpline = ToCBool(data->value->WeightTrack.CubicSpline);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MorphTargetDataExtBlend(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const float* const weights,
    const uint64_t weightCount,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if ((destination == nullptr && capacity != 0U)) {
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
        return BufferTooSmall("The destination cannot hold all blended morph bytes.");
    }
    const std::vector<std::uint8_t> blended =
        Microsoft::Xna::Framework::Graphics::BlendMorphTargetsEXT(
            *data->value, copiedWeights);
    if (!blended.empty()) {
        std::memcpy(destination, blended.data(), blended.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MorphWeightTrackExtEvaluate(
    const CNA_MorphWeightTrackEXTDescriptor* const track,
    const double timeSeconds,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outWeightCount)
{
    if (track == nullptr || (destination == nullptr && capacity != 0U)) {
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
        return BufferTooSmall("The destination cannot hold all evaluated morph weights.");
    }
    if (!evaluated.empty()) {
        std::copy(evaluated.begin(), evaluated.end(), destination);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelMeshPartSetMorphTargetDataExt(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_MorphTargetDataEXTHandle dataHandle)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartGetMorphTargetDataExt(
    const CNA_ModelMeshPartHandle partHandle,
    CNA_Bool* const outHasData,
    CNA_MorphTargetDataEXTHandle* const outData)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartSetMorphWeightsExt(
    const CNA_ModelMeshPartHandle partHandle,
    const float* const weights,
    const uint64_t weightCount)
{
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
        return InvalidState("The ModelMeshPart has no attached MorphTargetDataEXT.");
    }
    if (part->value->getVertexBufferProperty() == nullptr) {
        return InvalidState("The ModelMeshPart has no vertex buffer for morph upload.");
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
}

[[nodiscard]] CNA_Result SkinnedModelExtCreate(
    const CNA_SkinnedModelEXTDescriptor* const descriptor,
    CNA_SkinnedModelEXTHandle* const outModel)
{
    if (descriptor == nullptr) {
        return InvalidArgument("The SkinnedModelEXT descriptor or output is null.");
    }
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
        return Overflow("The SkinnedModelEXT clip count is too large.");
    }
    resource->value->Clips.reserve(static_cast<std::size_t>(descriptor->clip_count));
    for (uint64_t index = 0U; index < descriptor->clip_count; ++index) {
        const CNA_NamedAnimationClipEXTDescriptor& source = descriptor->clips[index];
        std::string name;
        if (const CNA_Result result = ReadText(
                source.name, "A SkinnedModelEXT clip name is not valid UTF-8 text.", &name,
                EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
}

[[nodiscard]] CNA_Result SkinnedModelExtCreateMove(
    const CNA_SkinnedModelEXTHandle sourceHandle,
    CNA_SkinnedModelEXTHandle* const outModel)
{
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
}

[[nodiscard]] CNA_Result SkinnedModelExtMoveAssign(
    const CNA_SkinnedModelEXTHandle destinationHandle,
    const CNA_SkinnedModelEXTHandle sourceHandle)
{
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
}

[[nodiscard]] CNA_Result SkinnedModelExtSetSkeleton(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const int32_t boneCount,
    const int32_t* const parentBoneIndices,
    const CNA_Matrix* const bindPoseLocal,
    const CNA_Matrix* const inverseBindPoseGlobal)
{
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
}

[[nodiscard]] CNA_Result SkinnedModelExtSetClip(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const CNA_AnimationClipEXTDescriptor* const clip)
{
    if (clip == nullptr) {
        return InvalidArgument("The AnimationClipEXT descriptor is null.");
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The AnimationClipEXT name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result SkinnedModelExtGetClipNameByteCountAt(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
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
}

[[nodiscard]] CNA_Result SkinnedModelExtCopyClipNameAt(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<SkinnedModelResource> model;
    if (const CNA_Result result = GetSkinnedModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<std::string> names = SortedClipNames(*model->value);
    if (clipIndex >= names.size()) {
        return InvalidArgument("The AnimationClipEXT index is outside the valid range.");
    }
    return CopyText(
        names[static_cast<std::size_t>(clipIndex)], destination, capacity,
        outByteCount);
}

[[nodiscard]] CNA_Result SkinnedModelExtGetClipInfo(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The AnimationClipEXT name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result SkinnedModelExtCopyClipTrack(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const uint64_t trackIndex,
    int32_t* const outBoneIndex,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("A BoneTrackEXT output is invalid.");
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The AnimationClipEXT name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return BufferTooSmall("The destination cannot hold all BoneTrackEXT keyframes.");
    }
    *outBoneIndex = static_cast<int32_t>(track.BoneIndex);
    for (std::size_t index = 0U; index < track.Keys.size(); ++index) {
        destination[index] = ToCKeyframe(track.Keys[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SkinnedModelExtComputeBoneTransforms(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView clipName,
    const double positionSeconds,
    const CNA_Bool loop,
    CNA_Matrix* const destination,
    const uint64_t capacity,
    uint64_t* const outBoneCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The SkinnedModelEXT transform output is invalid.");
    }
    if (!IsValidTimeSpanSeconds(positionSeconds)) {
        return InvalidArgument(
            "The SkinnedModelEXT playback position must fit a finite TimeSpan.");
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            clipName, "The AnimationClipEXT name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return BufferTooSmall("The destination cannot hold all computed bone transforms.");
    }
    std::vector<Matrix> computed;
    model->value->ComputeBoneTransformsEXT(
        copiedName, System::TimeSpan::FromSeconds(positionSeconds),
        loop == CNA_TRUE, computed);
    for (std::size_t index = 0U; index < computed.size(); ++index) {
        destination[index] = ToC(computed[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SkinnedModelExtAddPart(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name,
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_Handle textureHandle)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The SkinnedModelEXT part name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
            return InvalidState("A disposed Texture2D cannot be added to a SkinnedModelEXT.");
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
        return InvalidState("A ModelMeshPart cannot belong to more than one live model.");
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
        return Overflow("A SkinnedModelEXT resource retention count overflowed.");
    }
    if (model->parts.size() == model->parts.max_size()) {
        return Overflow("The SkinnedModelEXT part count is too large.");
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
}

[[nodiscard]] CNA_Result SkinnedModelExtAttachParts(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_SkinnedModelEXTHandle otherHandle)
{
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
        return Overflow("The attached SkinnedModelEXT part count is too large.");
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
}

[[nodiscard]] CNA_Result SkinnedModelExtRemovePart(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const CNA_StringView name)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The SkinnedModelEXT part name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result SkinnedModelExtGetPartAt(
    const CNA_SkinnedModelEXTHandle modelHandle,
    const uint64_t partIndex,
    CNA_ModelMeshPartHandle* const outPart,
    CNA_Bool* const outHasTexture,
    CNA_Handle* const outTexture)
{
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
}

[[nodiscard]] CNA_Result SkinnedModelExtGetOwnedResourceCounts(
    const CNA_SkinnedModelEXTHandle modelHandle,
    uint64_t* const outVertexBuffers,
    uint64_t* const outIndexBuffers,
    uint64_t* const outParts,
    uint64_t* const outTextures)
{
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
}

[[nodiscard]] CNA_Result SkinningDataCreate(
    const CNA_SkinningDataDescriptor* const descriptor,
    CNA_SkinningDataHandle* const outData)
{
    if (descriptor == nullptr) {
        return InvalidArgument("The SkinningData descriptor or output is null.");
    }
    SkinningData copied;
    if (const CNA_Result result = CopySkinningDataDescriptor(*descriptor, &copied);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<SkinningDataResource>();
    resource->value = std::make_shared<SkinningData>(std::move(copied));
    return CreateSkinningDataHandle(std::move(resource), outData);
}

[[nodiscard]] CNA_Result SkinningDataGetClipNameByteCountAt(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
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
}

[[nodiscard]] CNA_Result SkinningDataCopyClipNameAt(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<SkinningDataResource> data;
    if (const CNA_Result result = GetSkinningData(dataHandle, &data);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<std::string> names = SortedClipNames(*data->value);
    if (clipIndex >= names.size()) {
        return InvalidArgument("The SkinningData clip index is outside the valid range.");
    }
    return CopyText(
        names[static_cast<std::size_t>(clipIndex)], destination, capacity,
        outByteCount);
}

[[nodiscard]] CNA_Result SkinningDataGetClipInfo(
    const CNA_SkinningDataHandle dataHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The SkinningData clip name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result SkinningDataCopyClipTrack(
    const CNA_SkinningDataHandle dataHandle,
    const CNA_StringView name,
    const uint64_t trackIndex,
    int32_t* const outBoneIndex,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("A SkinningData BoneTrack output is invalid.");
    }
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            name, "The SkinningData clip name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return BufferTooSmall("The destination cannot hold all SkinningData keyframes.");
    }
    *outBoneIndex = static_cast<int32_t>(track.BoneIndex);
    for (std::size_t index = 0U; index < track.Keys.size(); ++index) {
        destination[index] = ToCKeyframe(track.Keys[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AnimationPlayerCreate(
    const CNA_SkinningDataHandle dataHandle,
    CNA_AnimationPlayerHandle* const outPlayer)
{
    std::shared_ptr<SkinningDataResource> data;
    if (const CNA_Result result = GetSkinningData(dataHandle, &data);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<AnimationPlayerResource>();
    resource->data = std::move(data);
    resource->value = std::make_shared<AnimationPlayer>(*resource->data->value);
    return CreateAnimationPlayerHandle(std::move(resource), outPlayer);
}

[[nodiscard]] CNA_Result AnimationPlayerStartClip(
    const CNA_AnimationPlayerHandle playerHandle,
    const CNA_StringView clipName)
{
    std::string copiedName;
    if (const CNA_Result result = ReadText(
            clipName, "The AnimationPlayer clip name is not valid UTF-8 text.", &copiedName,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result AnimationPlayerUpdate(
    const CNA_AnimationPlayerHandle playerHandle,
    const double timeSeconds,
    const CNA_Bool relativeToCurrentTime,
    const CNA_Bool loop)
{
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
}

[[nodiscard]] CNA_Result AnimationPlayerGetCurrentClipInfo(
    const CNA_AnimationPlayerHandle playerHandle,
    CNA_Bool* const outHasClip,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount)
{
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
}

[[nodiscard]] CNA_Result ModelMeshPartSetSamplerStateExt(
    const CNA_ModelMeshPartHandle partHandle,
    const CNA_PbrTextureSlot slot,
    const CNA_SamplerState* const state)
{
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
}

[[nodiscard]] CNA_Result SkinningDataGetClipTargetSpaceExt(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    CNA_ClipTargetSpaceEXT* const outValue)
{
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
}

[[nodiscard]] CNA_Result SkinningDataSetClipTargetSpaceExt(
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t clipIndex,
    const CNA_ClipTargetSpaceEXT value)
{
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtCopyTangentDeltas(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t targetIndex,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outDeltaCount)
{
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtSetTriangleIndicesExt(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint32_t* const indices,
    const uint64_t indexCount)
{
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtSetTangentDeltas(
    const CNA_MorphTargetDataEXTHandle dataHandle,
    const uint64_t targetIndex,
    const CNA_Vector3* const deltas,
    const uint64_t deltaCount)
{
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
        target.push_back(ToNative(deltas[index]));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_ModelCameraEXT ToC(const ModelCameraEXT& value) noexcept
{
    return CNA_ModelCameraEXT{
        .struct_size = sizeof(CNA_ModelCameraEXT),
        .struct_version = ReportStructureVersion,
        .scene_node_index = value.SceneNodeIndex,
        .is_perspective = ToCBool(value.IsPerspective),
        .has_infinite_far_plane = ToCBool(value.HasInfiniteFarPlane),
        .has_authored_aspect_ratio = ToCBool(value.HasAuthoredAspectRatio),
        .projection = ToC(value.Projection),
        .world_transform = ToC(value.WorldTransform),
        .aspect_ratio = value.AspectRatio,
        .field_of_view = value.FieldOfView,
        .near_plane_distance = value.NearPlaneDistance,
        .far_plane_distance = value.FarPlaneDistance};
}

[[nodiscard]] bool IsModelCamera(const CNA_ModelCameraEXT* const value) noexcept
{
    return IsVersionedStructure(value, ReportStructureVersion);
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

[[nodiscard]] CNA_Result ModelGetCameraExt(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_ModelCameraEXT* const outCamera)
{
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
}

[[nodiscard]] CNA_Result ModelAddCameraExt(
    const CNA_ModelHandle modelHandle,
    const CNA_ModelCameraDescriptorEXT* const descriptor)
{
    if (descriptor == nullptr || !IsModelCamera(&descriptor->camera)) {
        return InvalidArgument("The Model camera descriptor is null or malformed.");
    }
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    ModelCameraEXT camera;
    if (const CNA_Result result = ReadText(
            descriptor->name, "The Model camera name is not valid UTF-8 text.", &camera.Name,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result ModelGetSkinExt(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_Bool* const outHasData,
    uint64_t* const outMeshCount)
{
    std::shared_ptr<ModelResource> model;
    const ModelSkinEXT* skin = nullptr;
    if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outHasData = ToCBool(skin->Data != nullptr);
    *outMeshCount = static_cast<uint64_t>(skin->Meshes.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelCreateSkinSkeletonHandleExt(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    CNA_SkinningDataHandle* const outData)
{
    std::shared_ptr<ModelResource> model;
    const ModelSkinEXT* skin = nullptr;
    if (const CNA_Result result = GetModelSkin(modelHandle, index, &model, &skin);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (skin->Data == nullptr) {
        return InvalidState("The Model skin names no skeleton.");
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
}

[[nodiscard]] CNA_Result ModelGetSkinMeshIndexExt(
    const CNA_ModelHandle modelHandle,
    const uint64_t index,
    const uint64_t meshIndex,
    uint64_t* const outModelMeshIndex)
{
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
    return InvalidState("The Model skin names a mesh this Model does not own.");
}

[[nodiscard]] CNA_Result ModelClearSkinsExt(const CNA_ModelHandle modelHandle)
{
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    model->value->setSkinsEXTProperty({});
    model->skinSkeletons.clear();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelAddSkinExt(
    const CNA_ModelHandle modelHandle,
    const CNA_StringView name,
    const CNA_SkinningDataHandle dataHandle,
    const uint64_t* const meshIndices,
    const uint64_t meshIndexCount)
{
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
    if (const CNA_Result result = ReadText(
            name, "The Model skin name is not valid UTF-8 text.", &skin.Name,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result ModelGetBoundingSphereExt(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasValue,
    CNA_BoundingSphere* const outSphere)
{
    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::optional<BoundingSphere> sphere =
        model->value->getBoundingSphereEXTProperty();
    *outHasValue = ToCBool(sphere.has_value());
    *outSphere = sphere.has_value() ? ToC(*sphere) : CNA_BoundingSphere{};
    return CNA_RESULT_SUCCESS;
}

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
    return LookupHandle(handle, ObjectKind::ModelAnimationsEXT, outAnimations);
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

[[nodiscard]] CNA_Result ModelAnimationsExtCreate(
    const CNA_NamedAnimationClipEXTDescriptor* const clips,
    const uint64_t clipCount,
    CNA_ModelAnimationsEXTHandle* const outAnimations)
{
    if (clips == nullptr && clipCount != 0U) {
        return InvalidArgument("The ModelAnimationsEXT clip array is null.");
    }
    auto resource = std::make_shared<ModelAnimationsResource>();
    if (clipCount > resource->value->Clips.max_size()) {
        return Overflow("The ModelAnimationsEXT clip count is too large.");
    }
    resource->value->Clips.reserve(static_cast<std::size_t>(clipCount));
    for (uint64_t index = 0U; index < clipCount; ++index) {
        const CNA_NamedAnimationClipEXTDescriptor& source = clips[index];
        std::string name;
        if (const CNA_Result result = ReadText(
                source.name, "A ModelAnimationsEXT clip name is not valid UTF-8 text.", &name,
                EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
}

[[nodiscard]] CNA_Result ModelAnimationsExtGetClipNameByteCountAt(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    uint64_t* const outByteCount)
{
    std::shared_ptr<ModelAnimationsResource> animations;
    std::string name;
    if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outByteCount = static_cast<uint64_t>(name.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ModelAnimationsExtCopyClipNameAt(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<ModelAnimationsResource> animations;
    std::string name;
    if (const CNA_Result result = GetAnimationClipAt(handle, clipIndex, &animations, &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyText(
        name, destination, capacity, outByteCount);
}

[[nodiscard]] CNA_Result ModelAnimationsExtGetClipInfoAt(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount,
    CNA_ClipTargetSpaceEXT* const outTargetSpace)
{
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
}

[[nodiscard]] CNA_Result ModelAnimationsExtSetClipTargetSpaceAt(
    const CNA_ModelAnimationsEXTHandle handle,
    const uint64_t clipIndex,
    const CNA_ClipTargetSpaceEXT value)
{
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
}

[[nodiscard]] CNA_Result ModelApplyClipToBonesExt(
    const CNA_ModelHandle modelHandle,
    const CNA_ModelAnimationsEXTHandle animationsHandle,
    const uint64_t clipIndex,
    const double timeSeconds)
{
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
}

} // namespace

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
    if (const CNA_Result result = CreateHandle(kind, resource, &handle);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
                    // A model-owned effect is not the caller's to dispose, but every other Effect
                    // route must work on it; parts sharing one effect share this one state.
                    effect->disposeAllowed = false;
                    if (effect->adapterState == nullptr) {
                        effect->adapterState =
                            CNA::C::Detail::CreateEffectAdapterState(effect->value);
                    }
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

[[nodiscard]] CNA_Result ContentManagerLoadModel(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_ModelHandle* const outModel)
{
    std::string assetNameText;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameText,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
    } catch (const Microsoft::Xna::Framework::Content::ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // The asset's root reader produced something that is not a Model. Reporting the
        // mismatch beats the exception barrier's catch-all calling it an internal fault.
        return IoError("The asset's root type reader did not produce a Model.");
    }

    std::shared_ptr<ModelResource> model;
    if (const CNA_Result result = MirrorLoadedModel(
            std::move(loaded), contentManager.parentGame, supportsThreeD, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateModelHandle(std::move(model), outModel);
}

} // namespace

/* --- CBIND-118: the Tag the content pipeline wrote --------------------------------------------- */

namespace {

[[nodiscard]] CNA_Result ModelGetContentTagDictionaryExt(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasTag,
    CNA_ObjectDictionaryHandle* const outDictionary)
{
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
}

[[nodiscard]] CNA_Result ModelGetContentTagForeignObjectExt(
    const CNA_ModelHandle modelHandle,
    CNA_Bool* const outHasTag,
    void** const outObject)
{
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
}

[[nodiscard]] CNA_Result MorphTargetDataExtCopyWeightKeyframe(
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
    if ((weights == nullptr && weightCapacity != 0U)
        || (inTangents == nullptr && inTangentCapacity != 0U)
        || (outTangents == nullptr && outTangentCapacity != 0U)) {
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
        return BufferTooSmall("A destination cannot hold the complete MorphWeightKeyframeEXT.");
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
}

} // namespace

// The routes generated from modules/c-api/spec/Models.toml.
#include "generated/Models.inc"
