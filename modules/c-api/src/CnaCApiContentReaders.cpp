// SPDX-License-Identifier: MS-PL

#include "CNA/C/content_readers.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiStorageDetail.hpp"

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::AcquireStorageStream;
using CNA::C::Detail::BorrowContentManager;
using CNA::C::Detail::BorrowedContentManager;
using CNA::C::Detail::BorrowedStorageStream;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReleaseStorageStream;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderBase;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::KnownUnsupportedContentTypeReader;
using Microsoft::Xna::Framework::Content::UnsupportedContentReaderReason;

constexpr uint32_t StructureVersion = UINT32_C(1);

static_assert(
    static_cast<uint32_t>(
        static_cast<std::underlying_type_t<UnsupportedContentReaderReason>>(
            UnsupportedContentReaderReason::CompiledPlatformShaderBytecode)) ==
    CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE);

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

// The canonical reader keeps raw pointers to both the stream and the manager, so the C resource
// keeps the borrow records that own them and gives them back only when the handle is released.
struct ContentReaderResource final {
    std::unique_ptr<ContentReader> value;
    BorrowedStorageStream stream;
    BorrowedContentManager contentManager;
    CNA_Handle contentManagerHandle = CNA_INVALID_HANDLE;

    ContentReaderResource() = default;
    ContentReaderResource(const ContentReaderResource&) = delete;
    ContentReaderResource& operator=(const ContentReaderResource&) = delete;

    ~ContentReaderResource()
    {
        value.reset();
        ReleaseStorageStream(stream);
    }
};

struct ContentTypeReaderResource final {
    std::unique_ptr<ContentTypeReaderBase> value;
};

[[nodiscard]] CNA_Result GetReader(
    const CNA_Handle handle,
    std::shared_ptr<ContentReaderResource>* const outReader)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::ContentReader, outReader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned ContentReader handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetTypeReader(
    const CNA_Handle handle,
    std::shared_ptr<ContentTypeReaderResource>* const outTypeReader)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::ContentTypeReader,
        outTypeReader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned content type-reader handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    const char* const message)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(message);
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, message);
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyReaderName(const CNA_StringView value, std::string* const outValue)
{
    if (const CNA_Result result = CopyStringView(value, true, outValue);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The canonical reader name is not valid UTF-8.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result ReadValue(
    const CNA_Handle readerHandle,
    const void* const output,
    const char* const message,
    TCallable&& callable)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<ContentReaderResource> reader;
    if (const CNA_Result result = GetReader(readerHandle, &reader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*reader->value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateTypeReaderHandle(
    std::unique_ptr<ContentTypeReaderBase> typeReader,
    CNA_ContentTypeReaderHandle* const outTypeReader)
{
    const auto resource = std::make_shared<ContentTypeReaderResource>();
    resource->value = std::move(typeReader);
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::ContentTypeReader,
        resource,
        outTypeReader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned content type-reader handle could not be created.");
}

} // namespace

CNA_Result cna_content_reader_create(
    const CNA_ContentReaderCreateInfo* const createInfo,
    CNA_ContentReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The ContentReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_ContentReaderCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->reserved[0] != 0U ||
            createInfo->reserved[1] != 0U || createInfo->reserved[2] != 0U) {
            return InvalidArgument("The ContentReader creation configuration is invalid.");
        }

        std::string assetName;
        if (const CNA_Result result = CopyStringView(createInfo->asset_name, true, &assetName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ContentReader asset name is not valid UTF-8.");
        }

        const auto resource = std::make_shared<ContentReaderResource>();
        if (createInfo->content_manager != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = BorrowContentManager(
                    createInfo->content_manager,
                    &resource->contentManager);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            resource->contentManagerHandle = createInfo->content_manager;
        }
        if (const CNA_Result result = AcquireStorageStream(createInfo->stream, &resource->stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        resource->value = std::make_unique<ContentReader>(
            resource->contentManager.value,
            resource->stream.value,
            std::move(assetName),
            createInfo->version,
            static_cast<char>(createInfo->platform));

        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ContentReader,
            resource,
            outReader);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned ContentReader handle could not be created.");
    });
}

CNA_Result cna_content_reader_get_content_manager(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Handle* const outContentManager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContentManager == nullptr) {
            return InvalidArgument("The ContentManager output handle is null.");
        }
        *outContentManager = CNA_INVALID_HANDLE;
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (reader->value->getContentManagerProperty() != reader->contentManager.value) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical reader reported a different content manager.");
        }
        *outContentManager = reader->contentManagerHandle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_get_asset_name_size(
    const CNA_ContentReaderHandle readerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The asset-name size output is null.");
        }
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = reader->value->getAssetNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_copy_asset_name(
    const CNA_ContentReaderHandle readerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            reader->value->getAssetNameProperty(),
            destination,
            capacity,
            outBytes,
            "The asset-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_reader_get_version(
    const CNA_ContentReaderHandle readerHandle,
    int32_t* const outVersion)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outVersion,
            "The container-version output is null.",
            [outVersion](ContentReader& reader) {
                *outVersion = static_cast<int32_t>(reader.getVersionProperty());
            });
    });
}

CNA_Result cna_content_reader_get_platform(
    const CNA_ContentReaderHandle readerHandle,
    uint8_t* const outPlatform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outPlatform,
            "The platform output is null.",
            [outPlatform](ContentReader& reader) {
                *outPlatform = static_cast<uint8_t>(reader.getPlatformProperty());
            });
    });
}

CNA_Result cna_content_reader_read_matrix(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The matrix output is null.",
            [outValue](ContentReader& reader) {
                const Matrix value = reader.ReadMatrix();
                outValue->m11 = value.M11;
                outValue->m12 = value.M12;
                outValue->m13 = value.M13;
                outValue->m14 = value.M14;
                outValue->m21 = value.M21;
                outValue->m22 = value.M22;
                outValue->m23 = value.M23;
                outValue->m24 = value.M24;
                outValue->m31 = value.M31;
                outValue->m32 = value.M32;
                outValue->m33 = value.M33;
                outValue->m34 = value.M34;
                outValue->m41 = value.M41;
                outValue->m42 = value.M42;
                outValue->m43 = value.M43;
                outValue->m44 = value.M44;
            });
    });
}

CNA_Result cna_content_reader_read_quaternion(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Quaternion* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The quaternion output is null.",
            [outValue](ContentReader& reader) {
                const Quaternion value = reader.ReadQuaternion();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
                outValue->w = value.W;
            });
    });
}

CNA_Result cna_content_reader_read_vector2(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Vector2* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](ContentReader& reader) {
                const Vector2 value = reader.ReadVector2();
                outValue->x = value.X;
                outValue->y = value.Y;
            });
    });
}

CNA_Result cna_content_reader_read_vector3(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](ContentReader& reader) {
                const Vector3 value = reader.ReadVector3();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
            });
    });
}

CNA_Result cna_content_reader_read_vector4(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Vector4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](ContentReader& reader) {
                const Vector4 value = reader.ReadVector4();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
                outValue->w = value.W;
            });
    });
}

CNA_Result cna_content_reader_read_color(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Color* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The color output is null.",
            [outValue](ContentReader& reader) {
                const Color value = reader.ReadColor();
                outValue->r = value.getRProperty();
                outValue->g = value.getGProperty();
                outValue->b = value.getBProperty();
                outValue->a = value.getAProperty();
            });
    });
}

CNA_Result cna_content_reader_read_bounding_sphere(
    const CNA_ContentReaderHandle readerHandle,
    CNA_BoundingSphere* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outValue,
            "The bounding-sphere output is null.",
            [outValue](ContentReader& reader) {
                const BoundingSphere value = reader.ReadBoundingSphere();
                outValue->center.x = value.Center.X;
                outValue->center.y = value.Center.Y;
                outValue->center.z = value.Center.Z;
                outValue->radius = value.Radius;
            });
    });
}

CNA_Result cna_content_reader_read_object_tag(
    const CNA_ContentReaderHandle readerHandle,
    CNA_Bool* const outHasValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadValue(
            readerHandle,
            outHasValue,
            "The object-presence output is null.",
            [outHasValue](ContentReader& reader) {
                const std::any value = reader.ReadObject();
                *outHasValue = value.has_value() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_content_reader_initialize_type_readers(const CNA_ContentReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->InitializeTypeReaders();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_read_shared_resources(const CNA_ContentReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->ReadSharedResources();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_check_collection_element_count(
    const CNA_ContentReaderHandle readerHandle,
    const int64_t count,
    const CNA_StringView readerName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string readerNameCopy;
        if (const CNA_Result result = CopyReaderName(readerName, &readerNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->CheckCollectionElementCount(count, readerNameCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_check_decoded_byte_size(
    const CNA_ContentReaderHandle readerHandle,
    const int64_t byteSize,
    const CNA_StringView readerName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string readerNameCopy;
        if (const CNA_Result result = CopyReaderName(readerName, &readerNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->CheckDecodedByteSize(byteSize, readerNameCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_read_bytes_exact(
    const CNA_ContentReaderHandle readerHandle,
    const int32_t count,
    const CNA_StringView readerName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The exact-read output buffer is invalid.");
        }
        std::string readerNameCopy;
        if (const CNA_Result result = CopyReaderName(readerName, &readerNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The capacity is decided before the stream is touched, so a destination that is too small
        // never consumes bytes the caller would then have to seek back over.
        if (count >= 0) {
            *outBytes = static_cast<uint64_t>(count);
            if (capacity < *outBytes) {
                return Fail(
                    CNA_RESULT_BUFFER_TOO_SMALL,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The exact-read output buffer is too small.");
            }
        } else {
            *outBytes = 0U;
        }

        const std::vector<uint8_t> data =
            reader->value->ReadBytesExactOrThrow(count, readerNameCopy);
        if (!data.empty()) {
            std::memcpy(destination, data.data(), data.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_reader_destroy(const CNA_ContentReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(readerHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned ContentReader handle could not be released.");
    });
}

CNA_Result cna_content_type_reader_manager_clear_type_creators(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        ContentTypeReaderManager::ClearTypeCreators();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_manager_get_is_registered(
    const CNA_StringView canonicalName,
    CNA_Bool* const outIsRegistered)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsRegistered == nullptr) {
            return InvalidArgument("The registration output is null.");
        }
        std::string canonicalNameCopy;
        if (const CNA_Result result = CopyReaderName(canonicalName, &canonicalNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsRegistered =
            ContentTypeReaderManager::IsRegistered(canonicalNameCopy) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_manager_create_reader(
    const CNA_StringView canonicalName,
    CNA_ContentTypeReaderHandle* const outTypeReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTypeReader == nullptr) {
            return InvalidArgument("The type-reader output handle is null.");
        }
        *outTypeReader = CNA_INVALID_HANDLE;
        std::string canonicalNameCopy;
        if (const CNA_Result result = CopyReaderName(canonicalName, &canonicalNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::unique_ptr<ContentTypeReaderBase> typeReader =
            ContentTypeReaderManager::CreateReader(canonicalNameCopy);
        if (typeReader == nullptr) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "No content type reader is registered under that canonical name.");
        }
        return CreateTypeReaderHandle(std::move(typeReader), outTypeReader);
    });
}

CNA_Result cna_content_register_known_unsupported_xnb_readers(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::Content::RegisterKnownUnsupportedXnbReaders();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_known_unsupported_content_type_reader_create(
    const CNA_StringView targetTypeName,
    const CNA_UnsupportedContentReaderReason reason,
    CNA_ContentTypeReaderHandle* const outTypeReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTypeReader == nullptr) {
            return InvalidArgument("The type-reader output handle is null.");
        }
        *outTypeReader = CNA_INVALID_HANDLE;
        if (reason != CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE) {
            return InvalidArgument(
                "The requested reason is not a canonical UnsupportedContentReaderReason identity.");
        }
        std::string targetTypeNameCopy;
        if (const CNA_Result result = CopyReaderName(targetTypeName, &targetTypeNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTypeReaderHandle(
            std::make_unique<KnownUnsupportedContentTypeReader>(
                std::move(targetTypeNameCopy),
                static_cast<UnsupportedContentReaderReason>(reason)),
            outTypeReader);
    });
}

CNA_Result cna_content_type_reader_get_can_deserialize_into_existing_object(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The in-place deserialization output is null.");
        }
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = typeReader->value->getCanDeserializeIntoExistingObjectProperty()
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_get_target_type_name_size(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The target type-name size output is null.");
        }
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = typeReader->value->getTargetTypeNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_copy_target_type_name(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            typeReader->value->getTargetTypeNameProperty(),
            destination,
            capacity,
            outBytes,
            "The target type-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_type_reader_get_type_version(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    int32_t* const outVersion)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVersion == nullptr) {
            return InvalidArgument("The type-version output is null.");
        }
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVersion = static_cast<int32_t>(typeReader->value->getTypeVersionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_supports_version(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    const int32_t serializedVersion,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The version-support output is null.");
        }
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = typeReader->value->SupportsVersion(static_cast<int>(serializedVersion))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_initialize(const CNA_ContentTypeReaderHandle typeReaderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical registry is entirely static, so the manager parameter carries no state and
        // the adapter supplies an instance rather than exposing an empty object to C.
        ContentTypeReaderManager manager;
        typeReader->value->Initialize(manager);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_read_untyped(
    const CNA_ContentTypeReaderHandle typeReaderHandle,
    const CNA_ContentReaderHandle readerHandle,
    CNA_Bool* const outHasValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasValue == nullptr) {
            return InvalidArgument("The object-presence output is null.");
        }
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::any value = typeReader->value->ReadUntyped(*reader->value, std::any{});
        *outHasValue = value.has_value() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_destroy(const CNA_ContentTypeReaderHandle typeReaderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentTypeReaderResource> typeReader;
        if (const CNA_Result result = GetTypeReader(typeReaderHandle, &typeReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(typeReaderHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned content type-reader handle could not be released.");
    });
}
