// SPDX-License-Identifier: MS-PL

#include "CNA/C/content_readers.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiStorageDetail.hpp"

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"
#include "CNA/Content/ForeignContentObjectEXT.hpp"
#include "CNA/Content/ObjectDictionaryEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <any>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
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
using CNA::Content::ForeignContentObjectEXT;

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
    // shared_ptr rather than unique_ptr so the same resource shape can describe both an owned
    // reader and one this ABI merely borrows for the duration of a callback: the borrowed form
    // uses a no-op deleter, and every accessor below is written once for both.
    std::shared_ptr<ContentReader> value;
    BorrowedStorageStream stream;
    BorrowedContentManager contentManager;
    CNA_Handle contentManagerHandle = CNA_INVALID_HANDLE;
    // A borrowed reader belongs to the load in progress. Destroying its handle would take a
    // reader out from under CNA's own content pipeline, so the destroy route refuses it.
    bool isBorrowed = false;

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

        resource->value = std::make_shared<ContentReader>(
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
        if (reader->isBorrowed) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "A callback-scoped borrowed ContentReader handle cannot be destroyed; it is "
                "released when the callback that received it returns.");
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

namespace {

/// One registered caller-supplied factory, kept alive by its registration handle.
///
/// The table is copied so a caller may free its own storage; the `context` pointer inside it is
/// not, which the header states, because copying what it points at is impossible from here.
struct ForeignReaderRegistration final {
    std::string canonicalName;
    CNA_ContentTypeReaderCallbacks callbacks;
    std::string targetTypeName;
};

/// The adapter that makes a C callback table look like a canonical reader.
///
/// One instance per compiled asset file that names the registration, matching the built-in
/// readers. It holds the registration by shared_ptr rather than by reference: withdrawing a
/// registration mid-load must not leave a live reader pointing at freed callbacks.
class ForeignContentTypeReader final : public ContentTypeReaderBase {
public:
    explicit ForeignContentTypeReader(std::shared_ptr<ForeignReaderRegistration> registration)
        : ContentTypeReaderBase(registration->targetTypeName)
        , registration_(std::move(registration))
    {
        const CNA_Result result =
            registration_->callbacks.create(registration_->callbacks.context, &readerContext_);
        if (result != CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A caller-supplied content type reader for '" + registration_->canonicalName +
                "' refused to be constructed (CNA_Result " + std::to_string(result) + ").");
        }
        constructed_ = true;
    }

    ForeignContentTypeReader(const ForeignContentTypeReader&) = delete;
    ForeignContentTypeReader& operator=(const ForeignContentTypeReader&) = delete;

    ~ForeignContentTypeReader() override
    {
        if (constructed_ && registration_->callbacks.destroy != nullptr) {
            registration_->callbacks.destroy(readerContext_);
        }
    }

    [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override
    {
        return registration_->callbacks.can_deserialize_into_existing_object == CNA_TRUE;
    }

    [[nodiscard]] int getTypeVersionProperty() const override
    {
        return static_cast<int>(registration_->callbacks.type_version);
    }

    std::any ReadUntyped(ContentReader& input, std::any existingInstance) override
    {
        // The reader is CNA's, positioned mid-stream, and must not outlive this call: the handle
        // wraps it with a no-op deleter and is released before returning, on every path.
        const auto resource = std::make_shared<ContentReaderResource>();
        resource->value = std::shared_ptr<ContentReader>(&input, [](ContentReader*) {});
        resource->isBorrowed = true;
        CNA_Handle readerHandle = CNA_INVALID_HANDLE;
        if (GetRuntimeHandles().Create(ObjectKind::ContentReader, resource, &readerHandle) !=
            CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A borrowed ContentReader handle could not be created for the caller-supplied "
                "reader registered as '" + registration_->canonicalName + "'.");
        }

        void* existingObject = nullptr;
        if (existingInstance.has_value()) {
            // Anything else in the box came from a different reader, and handing its address to a
            // C callback that expects its own object would be a type confusion, not a read.
            const auto* existing = std::any_cast<ForeignContentObjectEXT>(&existingInstance);
            if (existing == nullptr) {
                static_cast<void>(GetRuntimeHandles().Release(readerHandle));
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "The existing instance offered to the caller-supplied reader registered as '" +
                    registration_->canonicalName + "' was produced by a different reader.");
            }
            existingObject = existing->value;
        }

        void* object = nullptr;
        const CNA_Result result = registration_->callbacks.read(
            readerContext_, readerHandle, existingObject, &object);
        static_cast<void>(GetRuntimeHandles().Release(readerHandle));
        if (result != CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "The caller-supplied content type reader registered as '" +
                registration_->canonicalName + "' failed to read (CNA_Result " +
                std::to_string(result) + ").");
        }
        return std::any(ForeignContentObjectEXT{object});
    }

private:
    std::shared_ptr<ForeignReaderRegistration> registration_;
    void* readerContext_ = nullptr;
    bool constructed_ = false;
};

[[nodiscard]] CNA_Result GetForeignRegistration(
    const CNA_Handle handle,
    std::shared_ptr<ForeignReaderRegistration>* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ContentTypeReaderRegistration, outRegistration);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned content type-reader registration handle is invalid for this call.");
}

} // namespace

CNA_Result cna_content_type_reader_manager_register(
    const CNA_StringView canonicalName,
    const CNA_ContentTypeReaderCallbacks* const callbacks,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The reader registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callbacks == nullptr ||
            callbacks->struct_size < sizeof(CNA_ContentTypeReaderCallbacks) ||
            callbacks->struct_version != StructureVersion ||
            callbacks->reserved[0] != 0U || callbacks->reserved[1] != 0U ||
            callbacks->reserved[2] != 0U ||
            callbacks->create == nullptr || callbacks->read == nullptr ||
            (callbacks->can_deserialize_into_existing_object != CNA_FALSE &&
             callbacks->can_deserialize_into_existing_object != CNA_TRUE)) {
            return InvalidArgument("The content type-reader callback table is invalid.");
        }

        const auto registration = std::make_shared<ForeignReaderRegistration>();
        registration->callbacks = *callbacks;
        if (const CNA_Result result =
                CopyStringView(canonicalName, true, &registration->canonicalName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The canonical reader name is not valid UTF-8.");
        }
        if (registration->canonicalName.empty()) {
            return InvalidArgument("The canonical reader name must not be empty.");
        }
        if (const CNA_Result result = CopyStringView(
                callbacks->target_type_name, true, &registration->targetTypeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The reader target type name is not valid UTF-8.");
        }
        if (registration->targetTypeName.empty()) {
            return InvalidArgument("The reader target type name must not be empty.");
        }
        // The copied name owns the storage the adapter reads; the caller's view does not outlive
        // this call.
        registration->callbacks.target_type_name.data = nullptr;
        registration->callbacks.target_type_name.byte_length = 0U;

        if (ContentTypeReaderManager::IsRegistered(registration->canonicalName)) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "A content type-reader factory is already registered under that canonical name.");
        }

        if (const CNA_Result result = GetRuntimeHandles().Create(
                ObjectKind::ContentTypeReaderRegistration, registration, outRegistration);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The content type-reader registration handle could not be created.");
        }

        ContentTypeReaderManager::AddTypeCreator(
            registration->canonicalName,
            [registration]() -> std::unique_ptr<ContentTypeReaderBase> {
                return std::make_unique<ForeignContentTypeReader>(registration);
            });
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_type_reader_manager_unregister(const CNA_Handle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ForeignReaderRegistration> registration;
        if (const CNA_Result result = GetForeignRegistration(registrationHandle, &registration);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        static_cast<void>(
            ContentTypeReaderManager::RemoveTypeCreatorEXT(registration->canonicalName));
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The content type-reader registration handle could not be released.");
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

/* --- CBIND-105: the reflective content readers ------------------------------------------------- */

namespace {

/// One declared member: either an inline value at a byte offset, or a caller callback.
struct ReflectiveField final {
    CNA_ContentFieldKind kind;
    uint64_t offset;
    CNA_ReflectiveFieldCallback callback;
    void* context;
};

struct ReflectiveRegistration final {
    std::string targetTypeName;
    CNA_ReflectiveObjectCreateCallback create;
    void* createContext;
    std::vector<ReflectiveField> fields;
};

/// The kind used to mark a caller callback rather than an inline value.
constexpr CNA_ContentFieldKind ReflectiveCustomFieldKind = UINT32_MAX;

/// Writes one inline value at its declared offset, in the layout the caller's own struct has.
void StoreReflectiveField(
    void* const object,
    const ReflectiveField& field,
    ContentReader& input)
{
    auto* const bytes = static_cast<std::uint8_t*>(object) + field.offset;
    switch (field.kind) {
    case CNA_CONTENT_FIELD_BOOLEAN: {
        const bool value = input.ReadBoolean();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_SINGLE: {
        const float value = input.ReadSingle();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_DOUBLE: {
        const double value = input.ReadDouble();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_INT32: {
        const std::int32_t value = input.ReadInt32();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_UINT32: {
        const std::uint32_t value = input.ReadUInt32();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_INT64: {
        const std::int64_t value = input.ReadInt64();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_BYTE: {
        const std::uint8_t value = input.ReadByte();
        std::memcpy(bytes, &value, sizeof(value));
        break;
    }
    case CNA_CONTENT_FIELD_VECTOR2: {
        const auto value = input.ReadVector2();
        const float pair[2] = {value.X, value.Y};
        std::memcpy(bytes, pair, sizeof(pair));
        break;
    }
    case CNA_CONTENT_FIELD_VECTOR3: {
        const auto value = input.ReadVector3();
        const float triple[3] = {value.X, value.Y, value.Z};
        std::memcpy(bytes, triple, sizeof(triple));
        break;
    }
    case CNA_CONTENT_FIELD_VECTOR4: {
        const auto value = input.ReadVector4();
        const float quad[4] = {value.X, value.Y, value.Z, value.W};
        std::memcpy(bytes, quad, sizeof(quad));
        break;
    }
    case CNA_CONTENT_FIELD_MATRIX: {
        const auto value = input.ReadMatrix();
        const float cells[16] = {
            value.M11, value.M12, value.M13, value.M14,
            value.M21, value.M22, value.M23, value.M24,
            value.M31, value.M32, value.M33, value.M34,
            value.M41, value.M42, value.M43, value.M44};
        std::memcpy(bytes, cells, sizeof(cells));
        break;
    }
    case CNA_CONTENT_FIELD_QUATERNION: {
        const auto value = input.ReadQuaternion();
        const float quad[4] = {value.X, value.Y, value.Z, value.W};
        std::memcpy(bytes, quad, sizeof(quad));
        break;
    }
    case CNA_CONTENT_FIELD_COLOR: {
        const auto value = input.ReadColor();
        const std::uint8_t channels[4] = {
            value.getRProperty(), value.getGProperty(),
            value.getBProperty(), value.getAProperty()};
        std::memcpy(bytes, channels, sizeof(channels));
        break;
    }
    default: {
        // A .NET TimeSpan is a value type written as its Int64 tick count, and it lands as ticks:
        // C has no TimeSpan value and inventing one here would be a second spelling of int64.
        const std::int64_t ticks = input.ReadInt64();
        std::memcpy(bytes, &ticks, sizeof(ticks));
        break;
    }
    }
}

/**
 * The reader a C-declared reflective type registers.
 *
 * It is not `ReflectiveTypeReader<T>` and cannot be: that template needs a C++ `T` to
 * default-construct and to hold member pointers into, and a C caller has neither. What is bound is
 * the **contract** -- the canonical reader name, the declared wire order, and value types read
 * inline -- and this implements the same one against a caller-made object and byte offsets.
 */
// CBIND-116: the field walk both reflective readers run. Extracted rather than duplicated,
// because the value-shaped and reference-shaped readers differ in exactly one thing -- the type
// the returned `std::any` carries -- and duplicating the walk would let those two drift apart.
[[nodiscard]] void* ReadReflectiveObject(
    const ReflectiveRegistration& registration,
    ContentReader& input,
    void* existingObject)
{
    void* object = existingObject;
    if (object == nullptr) {
        const CNA_Result result = registration.create(registration.createContext, &object);
        if (result != CNA_RESULT_SUCCESS || object == nullptr) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "The object factory for the reflective reader registered as '" +
                registration.targetTypeName + "' failed (CNA_Result " +
                std::to_string(result) + ").");
        }
    }

    for (const ReflectiveField& field : registration.fields) {
        if (field.kind != ReflectiveCustomFieldKind) {
            StoreReflectiveField(object, field, input);
            continue;
        }
        // Same borrowed-handle discipline the caller-supplied reader path uses: created over
        // CNA's own mid-stream reader with a no-op deleter, and released on every path.
        const auto resource = std::make_shared<ContentReaderResource>();
        resource->value = std::shared_ptr<ContentReader>(&input, [](ContentReader*) {});
        resource->isBorrowed = true;
        CNA_Handle readerHandle = CNA_INVALID_HANDLE;
        if (GetRuntimeHandles().Create(ObjectKind::ContentReader, resource, &readerHandle) !=
            CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A borrowed ContentReader handle could not be created for the reflective "
                "reader registered as '" + registration.targetTypeName + "'.");
        }
        const CNA_Result result = field.callback(field.context, object, readerHandle);
        static_cast<void>(GetRuntimeHandles().Release(readerHandle));
        if (result != CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A custom member of the reflective reader registered as '" +
                registration.targetTypeName + "' failed to read (CNA_Result " +
                std::to_string(result) + ").");
        }
    }
    return object;
}

class ReflectiveForeignReader final : public ContentTypeReaderBase {
public:
    explicit ReflectiveForeignReader(std::shared_ptr<ReflectiveRegistration> registration)
        : ContentTypeReaderBase(registration->targetTypeName)
        , registration_(std::move(registration))
    {
    }

    [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override
    {
        // The canonical reflective reader takes an existing instance and fills it, so this does.
        return true;
    }

    std::any ReadUntyped(ContentReader& input, std::any existingInstance) override
    {
        void* existing = nullptr;
        if (existingInstance.has_value()) {
            const auto* const offered = std::any_cast<ForeignContentObjectEXT>(&existingInstance);
            if (offered == nullptr) {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "The existing instance offered to the reflective reader for '" +
                    registration_->targetTypeName + "' was produced by a different reader.");
            }
            existing = offered->value;
        }
        return std::any(
            ForeignContentObjectEXT{ReadReflectiveObject(*registration_, input, existing)});
    }

private:
    std::shared_ptr<ReflectiveRegistration> registration_;
};

/// One enum reader the builder registers alongside, so the `.xnb`'s table resolves in full.
class ReflectiveEnumReader final : public ContentTypeReaderBase {
public:
    explicit ReflectiveEnumReader(std::string targetTypeName)
        : ContentTypeReaderBase(std::move(targetTypeName))
    {
    }

    std::any ReadUntyped(ContentReader& input, std::any) override
    {
        return std::any(static_cast<std::int32_t>(input.ReadInt32()));
    }
};

struct ReflectiveBuilderResource final {
    std::shared_ptr<ReflectiveRegistration> registration;
    std::vector<std::string> enumTypeNames;
};

[[nodiscard]] CNA_Result GetReflectiveBuilder(
    const CNA_ReflectiveTypeReaderBuilderHandle handle,
    std::shared_ptr<ReflectiveBuilderResource>* const outBuilder)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::ReflectiveTypeReaderBuilder, outBuilder);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The reflective type-reader builder handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] std::string ReflectiveCanonicalName(const std::string& targetTypeName)
{
    return "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[" + targetTypeName + "]]";
}

[[nodiscard]] std::string EnumCanonicalName(const std::string& targetTypeName)
{
    return "Microsoft.Xna.Framework.Content.EnumReader`1[[" + targetTypeName + "]]";
}

[[nodiscard]] CNA_Result CanonicalNameSize(
    const CNA_StringView targetTypeName,
    std::string (*compose)(const std::string&),
    uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The canonical-name byte-count output is null.");
    }
    std::string nameText;
    if (const CNA_Result result = CopyStringView(targetTypeName, true, &nameText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The target type name is not valid UTF-8.");
    }
    *outBytes = static_cast<uint64_t>(compose(nameText).size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CanonicalNameCopy(
    const CNA_StringView targetTypeName,
    std::string (*compose)(const std::string&),
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The canonical-name output is invalid.");
    }
    std::string nameText;
    if (const CNA_Result result = CopyStringView(targetTypeName, true, &nameText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The target type name is not valid UTF-8.");
    }
    const std::string composed = compose(nameText);
    *outBytes = static_cast<uint64_t>(composed.size());
    if (capacity < static_cast<uint64_t>(composed.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the canonical name.");
    }
    if (!composed.empty()) {
        std::memcpy(destination, composed.data(), composed.size());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_reflective_type_reader_builder_create(
    const CNA_StringView targetTypeName,
    const CNA_ReflectiveObjectCreateCallback createCallback,
    void* const context,
    CNA_ReflectiveTypeReaderBuilderHandle* const outBuilder)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBuilder == nullptr) {
            return InvalidArgument("The reflective builder output handle is null.");
        }
        *outBuilder = CNA_INVALID_HANDLE;
        if (createCallback == nullptr) {
            return InvalidArgument("The reflective object factory is null.");
        }
        const auto resource = std::make_shared<ReflectiveBuilderResource>();
        resource->registration = std::make_shared<ReflectiveRegistration>();
        if (const CNA_Result result = CopyStringView(
                targetTypeName, true, &resource->registration->targetTypeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The target type name is not valid UTF-8.");
        }
        if (resource->registration->targetTypeName.empty()) {
            return InvalidArgument("The target type name must not be empty.");
        }
        resource->registration->create = createCallback;
        resource->registration->createContext = context;
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ReflectiveTypeReaderBuilder, resource, outBuilder);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The reflective builder handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_builder_destroy(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(builderHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The reflective builder handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_builder_add_field(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle,
    const CNA_ContentFieldKind kind,
    const uint64_t offsetInBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (kind > CNA_CONTENT_FIELD_MAXIMUM) {
            return InvalidArgument("The content field kind is not a defined kind.");
        }
        builder->registration->fields.push_back(
            ReflectiveField{kind, offsetInBytes, nullptr, nullptr});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_builder_add_enum_field(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle,
    const uint64_t offsetInBytes,
    const CNA_StringView enumTypeName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string enumText;
        if (const CNA_Result result = CopyStringView(enumTypeName, true, &enumText);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The enum type name is not valid UTF-8.");
        }
        if (enumText.empty()) {
            return InvalidArgument("The enum type name must not be empty.");
        }
        // An enum is written inline as its Int32; the reader registered under its name exists so
        // the file's type-reader table resolves in full, not because this dispatches to it.
        builder->registration->fields.push_back(
            ReflectiveField{CNA_CONTENT_FIELD_INT32, offsetInBytes, nullptr, nullptr});
        builder->enumTypeNames.push_back(std::move(enumText));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_builder_add_custom(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle,
    const CNA_ReflectiveFieldCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback == nullptr) {
            return InvalidArgument("The custom member callback is null.");
        }
        builder->registration->fields.push_back(
            ReflectiveField{ReflectiveCustomFieldKind, 0U, callback, context});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_builder_register(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        for (const std::string& enumTypeName : builder->enumTypeNames) {
            ContentTypeReaderManager::AddTypeCreator(
                EnumCanonicalName(enumTypeName),
                [name = enumTypeName] {
                    return std::unique_ptr<ContentTypeReaderBase>(
                        std::make_unique<ReflectiveEnumReader>(name));
                });
        }
        // A snapshot, so a builder that keeps being appended to after registering does not change
        // what the table already holds -- the canonical Register() copies its field list too.
        const auto snapshot = std::make_shared<ReflectiveRegistration>(*builder->registration);
        ContentTypeReaderManager::AddTypeCreator(
            ReflectiveCanonicalName(snapshot->targetTypeName),
            [snapshot] {
                return std::unique_ptr<ContentTypeReaderBase>(
                    std::make_unique<ReflectiveForeignReader>(snapshot));
            });
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_reflective_type_reader_get_canonical_name_size(
    const CNA_StringView targetTypeName,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CanonicalNameSize(targetTypeName, ReflectiveCanonicalName, outBytes);
    });
}

CNA_Result cna_reflective_type_reader_copy_canonical_name(
    const CNA_StringView targetTypeName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CanonicalNameCopy(
            targetTypeName, ReflectiveCanonicalName, destination, capacity, outBytes);
    });
}

CNA_Result cna_enum_type_reader_get_canonical_name_size(
    const CNA_StringView targetTypeName,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CanonicalNameSize(targetTypeName, EnumCanonicalName, outBytes);
    });
}

CNA_Result cna_enum_type_reader_copy_canonical_name(
    const CNA_StringView targetTypeName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CanonicalNameCopy(
            targetTypeName, EnumCanonicalName, destination, capacity, outBytes);
    });
}

/* --- CBIND-116: the reference-shaped reflective reader, and the dictionary a Model.Tag holds --- */

namespace {

/// Carries a caller-made object where the canonical layer requires a `System::Object` reference.
///
/// `ModelReader::ReadTag` accepts exactly `std::shared_ptr<System::Object>`, and the collection
/// readers dispatch on the same shape. A C caller has no C++ type to derive from, so this is the
/// one CNA supplies: it holds the opaque pointer the caller's factory returned and nothing else.
/// It is deliberately not public C++ surface -- it exists to satisfy a canonical signature, and a
/// game written in C++ would use its own type instead.
class ForeignReferenceObject final : public System::Object {
public:
    explicit ForeignReferenceObject(std::string typeName, void* const object)
        : typeName_(std::move(typeName))
        , value_(object)
    {
    }

    [[nodiscard]] const std::string& GetTypeName() const override { return typeName_; }

    [[nodiscard]] void* getValue() const { return value_; }

private:
    std::string typeName_;
    void* value_;
};

/// The reference-shaped twin of ReflectiveForeignReader: same field walk, `shared_ptr` result.
///
/// The whole difference is the type the `std::any` carries, because that is the whole difference
/// on the wire too -- the container reader chooses inline or dispatched by the shape of what the
/// reader produces, so producing a reference is what makes the payload read at the right offset.
class ReflectiveForeignSharedReader final : public ContentTypeReaderBase {
public:
    explicit ReflectiveForeignSharedReader(std::shared_ptr<ReflectiveRegistration> registration)
        : ContentTypeReaderBase(registration->targetTypeName)
        , registration_(std::move(registration))
    {
    }

    std::any ReadUntyped(ContentReader& input, std::any) override
    {
        void* const object = ReadReflectiveObject(*registration_, input, nullptr);
        auto carrier = std::make_shared<ForeignReferenceObject>(
            registration_->targetTypeName, object);
        return std::any(std::static_pointer_cast<System::Object>(std::move(carrier)));
    }

private:
    std::shared_ptr<ReflectiveRegistration> registration_;
};

} // namespace

CNA_Result cna_reflective_type_reader_builder_register_shared(
    const CNA_ReflectiveTypeReaderBuilderHandle builderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ReflectiveBuilderResource> builder;
        if (const CNA_Result result = GetReflectiveBuilder(builderHandle, &builder);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        for (const std::string& enumTypeName : builder->enumTypeNames) {
            ContentTypeReaderManager::AddTypeCreator(
                EnumCanonicalName(enumTypeName),
                [name = enumTypeName] {
                    return std::unique_ptr<ContentTypeReaderBase>(
                        std::make_unique<ReflectiveEnumReader>(name));
                });
        }
        // Snapshotted for the same reason the value-shaped registration snapshots: a builder that
        // keeps being appended to after registering must not change what the table already holds.
        const auto snapshot = std::make_shared<ReflectiveRegistration>(*builder->registration);
        ContentTypeReaderManager::AddTypeCreator(
            ReflectiveCanonicalName(snapshot->targetTypeName),
            [snapshot] {
                return std::unique_ptr<ContentTypeReaderBase>(
                    std::make_unique<ReflectiveForeignSharedReader>(snapshot));
            });
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

using Microsoft::Xna::Framework::BoundingBox;

// CBIND-116: the handle owns its dictionary. The canonical container is reached through Model.Tag,
// which this ABI cannot load, so what a C caller gets is a dictionary built from an asset whose
// root object is one -- and building it is what makes the handle the owner rather than a borrower.
struct ObjectDictionaryResource final {
    std::shared_ptr<CNA::Content::ObjectDictionaryEXT> value;
};

/// What one entry holds, and the packed C size of a single element of it.
struct DictionaryValueShape final {
    CNA_ObjectDictionaryValueKind kind;
    bool isArray;
    std::uint64_t elementCount;
    std::uint64_t elementByteSize;
};

/// The scalar C++ types this ABI names, and the C size each one copies out as.
///
/// Sizes are written out rather than taken from `sizeof` on the C++ type: `Color` and the vector
/// types are the same bytes either way today, and asserting that here is what would notice if one
/// of them stopped being.
template <typename T>
[[nodiscard]] bool AnyHolds(const std::any& value)
{
    return value.type() == typeid(T);
}

[[nodiscard]] bool ScalarShape(const std::any& value, DictionaryValueShape* const outShape)
{
    const auto set = [outShape](
        const CNA_ObjectDictionaryValueKind kind, const std::uint64_t bytes) {
        *outShape = DictionaryValueShape{kind, false, 1U, bytes};
        return true;
    };
    if (AnyHolds<bool>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_BOOLEAN, sizeof(CNA_Bool));
    if (AnyHolds<std::int32_t>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_INT32, 4U);
    if (AnyHolds<float>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_SINGLE, 4U);
    if (AnyHolds<double>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_DOUBLE, 8U);
    if (AnyHolds<std::string>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_STRING, 0U);
    if (AnyHolds<Vector2>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_VECTOR2, sizeof(CNA_Vector2));
    if (AnyHolds<Vector3>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_VECTOR3, sizeof(CNA_Vector3));
    if (AnyHolds<Vector4>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_VECTOR4, sizeof(CNA_Vector4));
    if (AnyHolds<Matrix>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_MATRIX, sizeof(CNA_Matrix));
    if (AnyHolds<Quaternion>(value)) {
        return set(CNA_OBJECT_DICTIONARY_VALUE_QUATERNION, sizeof(CNA_Quaternion));
    }
    if (AnyHolds<Color>(value)) return set(CNA_OBJECT_DICTIONARY_VALUE_COLOR, sizeof(CNA_Color));
    if (AnyHolds<BoundingSphere>(value)) {
        return set(CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE, sizeof(CNA_BoundingSphere));
    }
    if (AnyHolds<BoundingBox>(value)) {
        return set(CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_BOX, sizeof(CNA_BoundingBox));
    }
    if (AnyHolds<ForeignContentObjectEXT>(value)) {
        return set(CNA_OBJECT_DICTIONARY_VALUE_FOREIGN_OBJECT, 0U);
    }
    if (AnyHolds<std::shared_ptr<System::Object>>(value)) {
        const auto& stored = std::any_cast<const std::shared_ptr<System::Object>&>(value);
        if (dynamic_cast<const ForeignReferenceObject*>(stored.get()) != nullptr) {
            return set(CNA_OBJECT_DICTIONARY_VALUE_FOREIGN_OBJECT, 0U);
        }
    }
    return false;
}

template <typename T>
[[nodiscard]] bool VectorShape(
    const std::any& value,
    const CNA_ObjectDictionaryValueKind kind,
    const std::uint64_t elementBytes,
    DictionaryValueShape* const outShape)
{
    if (!AnyHolds<std::vector<T>>(value)) {
        return false;
    }
    const auto& stored = std::any_cast<const std::vector<T>&>(value);
    *outShape = DictionaryValueShape{
        kind, true, static_cast<std::uint64_t>(stored.size()), elementBytes};
    return true;
}

[[nodiscard]] bool ArrayShape(const std::any& value, DictionaryValueShape* const outShape)
{
    return VectorShape<std::int32_t>(value, CNA_OBJECT_DICTIONARY_VALUE_INT32, 4U, outShape)
        || VectorShape<float>(value, CNA_OBJECT_DICTIONARY_VALUE_SINGLE, 4U, outShape)
        || VectorShape<double>(value, CNA_OBJECT_DICTIONARY_VALUE_DOUBLE, 8U, outShape)
        || VectorShape<Vector2>(
               value, CNA_OBJECT_DICTIONARY_VALUE_VECTOR2, sizeof(CNA_Vector2), outShape)
        || VectorShape<Vector3>(
               value, CNA_OBJECT_DICTIONARY_VALUE_VECTOR3, sizeof(CNA_Vector3), outShape)
        || VectorShape<Vector4>(
               value, CNA_OBJECT_DICTIONARY_VALUE_VECTOR4, sizeof(CNA_Vector4), outShape)
        || VectorShape<Matrix>(
               value, CNA_OBJECT_DICTIONARY_VALUE_MATRIX, sizeof(CNA_Matrix), outShape)
        || VectorShape<Quaternion>(
               value, CNA_OBJECT_DICTIONARY_VALUE_QUATERNION, sizeof(CNA_Quaternion), outShape)
        || VectorShape<Color>(
               value, CNA_OBJECT_DICTIONARY_VALUE_COLOR, sizeof(CNA_Color), outShape)
        || VectorShape<BoundingSphere>(
               value, CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE,
               sizeof(CNA_BoundingSphere), outShape)
        || VectorShape<BoundingBox>(
               value, CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_BOX,
               sizeof(CNA_BoundingBox), outShape);
}

[[nodiscard]] DictionaryValueShape ShapeOf(const std::any& value)
{
    DictionaryValueShape shape{CNA_OBJECT_DICTIONARY_VALUE_UNKNOWN, false, 1U, 0U};
    if (ScalarShape(value, &shape) || ArrayShape(value, &shape)) {
        return shape;
    }
    return shape;
}

/// Writes one element in the packed C layout its kind names.
void WriteDictionaryElement(
    std::uint8_t* const destination,
    const CNA_ObjectDictionaryValueKind kind,
    const void* const source)
{
    switch (kind) {
    case CNA_OBJECT_DICTIONARY_VALUE_BOOLEAN: {
        const CNA_Bool value = *static_cast<const bool*>(source) ? CNA_TRUE : CNA_FALSE;
        std::memcpy(destination, &value, sizeof(value));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_INT32:
        std::memcpy(destination, source, 4U);
        break;
    case CNA_OBJECT_DICTIONARY_VALUE_SINGLE:
        std::memcpy(destination, source, 4U);
        break;
    case CNA_OBJECT_DICTIONARY_VALUE_DOUBLE:
        std::memcpy(destination, source, 8U);
        break;
    case CNA_OBJECT_DICTIONARY_VALUE_VECTOR2: {
        const auto& value = *static_cast<const Vector2*>(source);
        const CNA_Vector2 packed{value.X, value.Y};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_VECTOR3: {
        const auto& value = *static_cast<const Vector3*>(source);
        const CNA_Vector3 packed{value.X, value.Y, value.Z};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_VECTOR4: {
        const auto& value = *static_cast<const Vector4*>(source);
        const CNA_Vector4 packed{value.X, value.Y, value.Z, value.W};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_MATRIX: {
        const auto& value = *static_cast<const Matrix*>(source);
        const CNA_Matrix packed{
            value.M11, value.M12, value.M13, value.M14,
            value.M21, value.M22, value.M23, value.M24,
            value.M31, value.M32, value.M33, value.M34,
            value.M41, value.M42, value.M43, value.M44};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_QUATERNION: {
        const auto& value = *static_cast<const Quaternion*>(source);
        const CNA_Quaternion packed{value.X, value.Y, value.Z, value.W};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_COLOR: {
        const auto& value = *static_cast<const Color*>(source);
        const CNA_Color packed{
            value.getRProperty(), value.getGProperty(),
            value.getBProperty(), value.getAProperty()};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE: {
        const auto& value = *static_cast<const BoundingSphere*>(source);
        const CNA_BoundingSphere packed{
            CNA_Vector3{value.Center.X, value.Center.Y, value.Center.Z}, value.Radius};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    case CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_BOX: {
        const auto& value = *static_cast<const BoundingBox*>(source);
        const CNA_BoundingBox packed{
            CNA_Vector3{value.Min.X, value.Min.Y, value.Min.Z},
            CNA_Vector3{value.Max.X, value.Max.Y, value.Max.Z}};
        std::memcpy(destination, &packed, sizeof(packed));
        break;
    }
    default:
        break;
    }
}

template <typename T>
[[nodiscard]] bool WriteVectorIfHeld(
    const std::any& value,
    const CNA_ObjectDictionaryValueKind kind,
    const std::uint64_t elementBytes,
    std::uint8_t* const destination)
{
    if (!AnyHolds<std::vector<T>>(value)) {
        return false;
    }
    const auto& stored = std::any_cast<const std::vector<T>&>(value);
    std::uint8_t* cursor = destination;
    for (const T& element : stored) {
        WriteDictionaryElement(cursor, kind, &element);
        cursor += elementBytes;
    }
    return true;
}

[[nodiscard]] CNA_Result GetObjectDictionary(
    const CNA_ObjectDictionaryHandle handle,
    std::shared_ptr<ObjectDictionaryResource>* const outDictionary)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::ObjectDictionaryEXT, outDictionary);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result), "The object-dictionary handle is invalid.");
    }
    if (!(*outDictionary)->value) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_STATE,
            "The object dictionary this handle borrowed is gone.");
    }
    return CNA_RESULT_SUCCESS;
}

/// Resolves a handle and a key together, since every entry route needs exactly that pair.
[[nodiscard]] CNA_Result GetDictionaryEntry(
    const CNA_ObjectDictionaryHandle handle,
    const CNA_StringView key,
    std::shared_ptr<ObjectDictionaryResource>* const outDictionary,
    const std::any** const outValue)
{
    if (const CNA_Result result = GetObjectDictionary(handle, outDictionary);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string keyText;
    if (const CNA_Result result = CopyStringView(key, true, &keyText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result), "The dictionary key is not valid UTF-8.");
    }
    const auto& values = (*outDictionary)->value->getValuesProperty();
    const auto found = values.find(keyText);
    if (found == values.end()) {
        // The canonical KeyNotFoundException and InvalidCastException are one result code here;
        // the message is what tells a caller which of the two it hit.
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
            "No dictionary entry has that key.");
    }
    *outValue = &found->second;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_object_dictionary_ext_get_count(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The dictionary count output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        if (const CNA_Result result = GetObjectDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(dictionary->value->getValuesProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_contains_key(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The dictionary containment output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        if (const CNA_Result result = GetObjectDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string keyText;
        if (const CNA_Result result = CopyStringView(key, true, &keyText);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result), "The dictionary key is not valid UTF-8.");
        }
        *outContains = dictionary->value->ContainsKey(keyText) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_get_key_size_at(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The dictionary key-size output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        if (const CNA_Result result = GetObjectDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& values = dictionary->value->getValuesProperty();
        if (index >= static_cast<uint64_t>(values.size())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE,
                "The dictionary key index is past the end.");
        }
        auto entry = values.begin();
        std::advance(entry, static_cast<std::ptrdiff_t>(index));
        *outBytes = static_cast<uint64_t>(entry->first.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_copy_key_at(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The dictionary key output is invalid.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        if (const CNA_Result result = GetObjectDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& values = dictionary->value->getValuesProperty();
        if (index >= static_cast<uint64_t>(values.size())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE,
                "The dictionary key index is past the end.");
        }
        auto entry = values.begin();
        std::advance(entry, static_cast<std::ptrdiff_t>(index));
        const std::string& name = entry->first;
        *outBytes = static_cast<uint64_t>(name.size());
        if (capacity < static_cast<uint64_t>(name.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the dictionary key.");
        }
        if (!name.empty()) {
            std::memcpy(destination, name.data(), name.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_get_entry(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_ObjectDictionaryEntry* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEntry == nullptr || outEntry->struct_size < sizeof(CNA_ObjectDictionaryEntry)) {
            return InvalidArgument("The dictionary entry output is invalid.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const DictionaryValueShape shape = ShapeOf(*value);
        outEntry->struct_size = static_cast<uint32_t>(sizeof(CNA_ObjectDictionaryEntry));
        outEntry->struct_version = UINT32_C(1);
        outEntry->kind = shape.kind;
        outEntry->is_array = shape.isArray ? CNA_TRUE : CNA_FALSE;
        outEntry->element_count = shape.elementCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_get_type_name_size(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The dictionary type-name size output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(std::strlen(value->type().name()));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_copy_type_name(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The dictionary type-name output is invalid.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const char* const name = value->type().name();
        const std::size_t length = std::strlen(name);
        *outBytes = static_cast<uint64_t>(length);
        if (capacity < static_cast<uint64_t>(length)) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the entry type name.");
        }
        if (length != 0U) {
            std::memcpy(destination, name, length);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_copy_value(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const CNA_ObjectDictionaryValueKind kind,
    void* const destination,
    const uint64_t capacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destination == nullptr) {
            return InvalidArgument("The dictionary value destination is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const DictionaryValueShape shape = ShapeOf(*value);
        if (shape.isArray) {
            return InvalidArgument(
                "That dictionary entry is an array; read it with "
                "cna_object_dictionary_ext_copy_array.");
        }
        if (shape.kind == CNA_OBJECT_DICTIONARY_VALUE_STRING ||
            shape.kind == CNA_OBJECT_DICTIONARY_VALUE_FOREIGN_OBJECT ||
            shape.kind == CNA_OBJECT_DICTIONARY_VALUE_UNKNOWN) {
            return InvalidArgument(
                "That dictionary entry has no fixed-layout value; use the route for its kind.");
        }
        if (kind != shape.kind) {
            // Naming the kind is the cast, and this is the C form of InvalidCastException.
            return InvalidArgument("That dictionary entry does not hold the requested kind.");
        }
        if (capacity < shape.elementByteSize) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the entry value.");
        }
        const void* source = nullptr;
        bool booleanValue = false;
        switch (shape.kind) {
        case CNA_OBJECT_DICTIONARY_VALUE_BOOLEAN:
            booleanValue = std::any_cast<bool>(*value);
            source = &booleanValue;
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_INT32:
            source = std::any_cast<std::int32_t>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_SINGLE:
            source = std::any_cast<float>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_DOUBLE:
            source = std::any_cast<double>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_VECTOR2:
            source = std::any_cast<Vector2>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_VECTOR3:
            source = std::any_cast<Vector3>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_VECTOR4:
            source = std::any_cast<Vector4>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_MATRIX:
            source = std::any_cast<Matrix>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_QUATERNION:
            source = std::any_cast<Quaternion>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_COLOR:
            source = std::any_cast<Color>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_SPHERE:
            source = std::any_cast<BoundingSphere>(value);
            break;
        case CNA_OBJECT_DICTIONARY_VALUE_BOUNDING_BOX:
            source = std::any_cast<BoundingBox>(value);
            break;
        default:
            return InvalidArgument("That dictionary entry kind has no fixed-layout value.");
        }
        WriteDictionaryElement(static_cast<std::uint8_t*>(destination), shape.kind, source);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_copy_array(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const CNA_ObjectDictionaryValueKind kind,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The dictionary array output is invalid.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const DictionaryValueShape shape = ShapeOf(*value);
        if (!shape.isArray) {
            return InvalidArgument(
                "That dictionary entry is not an array; read it with "
                "cna_object_dictionary_ext_copy_value.");
        }
        if (kind != shape.kind) {
            return InvalidArgument("That dictionary array does not hold the requested kind.");
        }
        const uint64_t required = shape.elementCount * shape.elementByteSize;
        *outBytes = required;
        if (capacity < required) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the entry array.");
        }
        auto* const bytes = static_cast<std::uint8_t*>(destination);
        const bool written =
            WriteVectorIfHeld<std::int32_t>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<float>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<double>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Vector2>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Vector3>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Vector4>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Matrix>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Quaternion>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<Color>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<BoundingSphere>(*value, shape.kind, shape.elementByteSize, bytes)
            || WriteVectorIfHeld<BoundingBox>(*value, shape.kind, shape.elementByteSize, bytes);
        if (!written) {
            return Fail(
                CNA_RESULT_INTERNAL, CNA_ERROR_CATEGORY_STATE,
                "The dictionary array was described but could not be copied.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_get_string_size(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The dictionary string-size output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!AnyHolds<std::string>(*value)) {
            return InvalidArgument("That dictionary entry does not hold a string.");
        }
        *outBytes =
            static_cast<uint64_t>(std::any_cast<const std::string&>(*value).size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_copy_string(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The dictionary string output is invalid.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!AnyHolds<std::string>(*value)) {
            return InvalidArgument("That dictionary entry does not hold a string.");
        }
        const auto& text = std::any_cast<const std::string&>(*value);
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < static_cast<uint64_t>(text.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the entry string.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_get_foreign_object(
    const CNA_ObjectDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    void** const outObject)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outObject == nullptr) {
            return InvalidArgument("The dictionary foreign-object output is null.");
        }
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        const std::any* value = nullptr;
        if (const CNA_Result result =
                GetDictionaryEntry(dictionaryHandle, key, &dictionary, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (AnyHolds<ForeignContentObjectEXT>(*value)) {
            *outObject = std::any_cast<const ForeignContentObjectEXT&>(*value).value;
            return CNA_RESULT_SUCCESS;
        }
        if (AnyHolds<std::shared_ptr<System::Object>>(*value)) {
            const auto& stored = std::any_cast<const std::shared_ptr<System::Object>&>(*value);
            if (const auto* const carrier =
                    dynamic_cast<const ForeignReferenceObject*>(stored.get());
                carrier != nullptr) {
                *outObject = carrier->getValue();
                return CNA_RESULT_SUCCESS;
            }
        }
        return InvalidArgument("That dictionary entry does not hold a caller-made object.");
    });
}

CNA_Result cna_content_manager_load_object_dictionary_ext(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_ObjectDictionaryHandle* const outDictionary)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDictionary == nullptr) {
            return InvalidArgument("The object-dictionary output handle is null.");
        }
        *outDictionary = CNA_INVALID_HANDLE;
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
        BorrowedContentManager contentManager;
        if (const CNA_Result result =
                BorrowContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (contentManager.value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_STATE,
                "The content manager handle does not name a manager.");
        }
        auto resource = std::make_shared<ObjectDictionaryResource>();
        try {
            // The dictionary reader's own product. Boxing it is the canonical constructor, which
            // is also the only way this type is ever made: ModelReader does exactly this when the
            // same payload arrives as a Model.Tag.
            resource->value = std::make_shared<CNA::Content::ObjectDictionaryEXT>(
                contentManager.value->Load<std::map<std::string, std::any>>(assetNameText));
        } catch (const Microsoft::Xna::Framework::Content::ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
        if (const CNA_Result result = GetRuntimeHandles().Create(
                ObjectKind::ObjectDictionaryEXT, resource, outDictionary);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The loaded object dictionary could not be published as a handle.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_object_dictionary_ext_destroy(const CNA_ObjectDictionaryHandle dictionaryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ObjectDictionaryResource> dictionary;
        if (const CNA_Result result = GetObjectDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetRuntimeHandles().Release(dictionaryHandle);
    });
}

namespace CNA::C::Detail {

CNA_Result PublishObjectDictionary(
    std::shared_ptr<CNA::Content::ObjectDictionaryEXT> dictionary,
    CNA_Handle* const outHandle)
{
    if (outHandle == nullptr) {
        return InvalidArgument("The object-dictionary output handle is null.");
    }
    *outHandle = CNA_INVALID_HANDLE;
    if (!dictionary) {
        return InvalidArgument("The object dictionary to publish is null.");
    }
    auto resource = std::make_shared<ObjectDictionaryResource>();
    resource->value = std::move(dictionary);
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::ObjectDictionaryEXT, resource, outHandle);
    return result == CNA_RESULT_SUCCESS
        ? CNA_RESULT_SUCCESS
        : Fail(
            result, ErrorCategoryForResult(result),
            "The object dictionary could not be published as a handle.");
}

bool TryGetForeignReferenceObject(const System::Object* const tag, void** const outObject)
{
    const auto* const carrier = dynamic_cast<const ForeignReferenceObject*>(tag);
    if (carrier == nullptr) {
        return false;
    }
    *outObject = carrier->getValue();
    return true;
}

} // namespace CNA::C::Detail
