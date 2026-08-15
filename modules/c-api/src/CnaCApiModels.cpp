// SPDX-License-Identifier: MS-PL

#include "CNA/C/models.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

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
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelBoneCollection;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;

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

struct PartResource final {
    std::shared_ptr<ModelMeshPart> value = std::make_shared<ModelMeshPart>();
    PartRetainedSlot effect;
    PartRetainedSlot vertexBuffer;
    PartRetainedSlot indexBuffer;
    CNA_ModelMeshPartTag tag = 0U;
};

struct PartCollectionResource final {
    std::vector<std::shared_ptr<PartResource>> parts;
};

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

[[nodiscard]] const ModelBoneCollection& NativeCollection(
    const BoneCollectionResource& collection)
{
    return collection.parent != nullptr
        ? collection.parent->value->getChildrenProperty()
        : *collection.standalone;
}

[[nodiscard]] std::shared_ptr<BoneNode> CollectionNodeAt(
    const BoneCollectionResource& collection,
    const std::size_t index)
{
    return collection.parent->children[index];
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

[[nodiscard]] CNA_Result ValidatePartDevice(
    const PartResource& part,
    const PartRetainedSlot* const replacedSlot,
    const CNA_Handle parentGame)
{
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
    if (effectHandle == CNA_INVALID_HANDLE) {
        part->value->setEffectProperty(nullptr);
        part->effect.Reset();
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = ResolveEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidatePartDevice(
            *part, &part->effect, effect->parentGame);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (effectHandle == part->effect.handle) {
        return CNA_RESULT_SUCCESS;
    }
    if (effect->activeModelReferenceCount == std::numeric_limits<uint64_t>::max()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The ModelMeshPart Effect retention count overflowed.");
    }
    part->value->setEffectProperty(effect->value.get());
    return part->effect.Set(
        effectHandle, effect->parentGame, effect, &effect->activeModelReferenceCount);
}

[[nodiscard]] CNA_Result SetPartVertexBuffer(
    const std::shared_ptr<PartResource>& part,
    const CNA_VertexBufferHandle bufferHandle)
{
    if (bufferHandle == CNA_INVALID_HANDLE) {
        part->value->SetVertexBuffer(nullptr);
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
    return part->vertexBuffer.Set(
        bufferHandle, buffer->parentGame, buffer, &buffer->activeModelReferenceCount);
}

[[nodiscard]] CNA_Result SetPartIndexBuffer(
    const std::shared_ptr<PartResource>& part,
    const CNA_IndexBufferHandle bufferHandle)
{
    if (bufferHandle == CNA_INVALID_HANDLE) {
        part->value->SetIndexBuffer(nullptr);
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
    return part->indexBuffer.Set(
        bufferHandle, buffer->parentGame, buffer, &buffer->activeModelReferenceCount);
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
        *outCount = static_cast<uint64_t>(NativeCollection(*collection).getCountProperty());
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
        const uint64_t count = static_cast<uint64_t>(
            NativeCollection(*collection).getCountProperty());
        if (index >= count || collection->parent == nullptr) {
            return InvalidArgument("The ModelBoneCollection index is outside the valid range.");
        }
        (void)NativeCollection(*collection)[static_cast<int>(index)];
        return CreateBoneHandle(
            CollectionNodeAt(*collection, static_cast<std::size_t>(index)), outBone);
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
        ModelBone* native = nullptr;
        if (!NativeCollection(*collection).TryGetValue(copiedName, native)) {
            return CNA_RESULT_SUCCESS;
        }
        if (collection->parent == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "An empty standalone ModelBoneCollection returned a bone.");
        }
        for (const std::shared_ptr<BoneNode>& child : collection->parent->children) {
            if (child->value.get() == native) {
                if (const CNA_Result result = CreateBoneHandle(child, outBone);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                *outFound = CNA_TRUE;
                return CNA_RESULT_SUCCESS;
            }
        }
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The ModelBoneCollection sidecar is inconsistent with the native collection.");
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
        *outContains = NativeCollection(*collection).Contains(bone->node->value.get())
            ? CNA_TRUE
            : CNA_FALSE;
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
        *outCount = static_cast<uint64_t>(collection->parts.size());
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
        if (index >= collection->parts.size()) {
            return InvalidArgument(
                "The ModelMeshPartCollection index is outside the valid range.");
        }
        return CreatePartHandle(
            collection->parts[static_cast<std::size_t>(index)], outPart);
    });
}
