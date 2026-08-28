// SPDX-License-Identifier: MS-PL

#include "CNA/C/content_readers.h"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiStorageDetail.hpp"

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"
#include "CNA/Content/ForeignContentObjectEXT.hpp"
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
        void* object = nullptr;
        if (existingInstance.has_value()) {
            const auto* const existing = std::any_cast<ForeignContentObjectEXT>(&existingInstance);
            if (existing == nullptr) {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "The existing instance offered to the reflective reader for '" +
                    registration_->targetTypeName + "' was produced by a different reader.");
            }
            object = existing->value;
        }
        if (object == nullptr) {
            const CNA_Result result =
                registration_->create(registration_->createContext, &object);
            if (result != CNA_RESULT_SUCCESS || object == nullptr) {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "The object factory for the reflective reader registered as '" +
                    registration_->targetTypeName + "' failed (CNA_Result " +
                    std::to_string(result) + ").");
            }
        }

        for (const ReflectiveField& field : registration_->fields) {
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
            if (GetRuntimeHandles().Create(
                    ObjectKind::ContentReader, resource, &readerHandle) !=
                CNA_RESULT_SUCCESS) {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "A borrowed ContentReader handle could not be created for the reflective "
                    "reader registered as '" + registration_->targetTypeName + "'.");
            }
            const CNA_Result result = field.callback(field.context, object, readerHandle);
            static_cast<void>(GetRuntimeHandles().Release(readerHandle));
            if (result != CNA_RESULT_SUCCESS) {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    "A custom member of the reflective reader registered as '" +
                    registration_->targetTypeName + "' failed to read (CNA_Result " +
                    std::to_string(result) + ").");
            }
        }
        return std::any(ForeignContentObjectEXT{object});
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
