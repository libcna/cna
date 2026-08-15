// SPDX-License-Identifier: MS-PL

#include "CNA/C/models.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelBoneCollection;

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
