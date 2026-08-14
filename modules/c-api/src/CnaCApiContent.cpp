// SPDX-License-Identifier: MS-PL

#include "CNA/C/content.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::AddOwnedContentManager;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedContentManager;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct ContentManagerResource final {
    std::shared_ptr<ContentManager> value;
    CNA_Handle parentGame;
};

[[nodiscard]] CNA_Result GetContentManager(
    const CNA_Handle handle,
    std::shared_ptr<ContentManagerResource>* const outContentManager)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::ContentManager,
        outContentManager);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned ContentManager handle is invalid for this call.");
}

} // namespace

CNA_Result cna_content_manager_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ContentManagerCreateInfo* const createInfo,
    CNA_Handle* const outContentManager)
{
    return CallWithExceptionBarrier([&]() {
        if (outContentManager == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The ContentManager output handle is null.");
        }
        *outContentManager = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_ContentManagerCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->reserved != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The ContentManager creation configuration is invalid.");
        }

        std::string rootDirectory;
        if (const CNA_Result result = CopyStringView(
                createInfo->root_directory,
                true,
                &rootDirectory);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ContentManager root directory is not valid UTF-8.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const auto nativeManager = std::make_shared<ContentManager>();
        nativeManager->setRootDirectoryProperty(std::move(rootDirectory));
        nativeManager->setGraphicsDevice(*graphicsDevice->value);
        const auto resource = std::make_shared<ContentManagerResource>(
            ContentManagerResource{nativeManager, graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ContentManager,
            resource,
            outContentManager);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ContentManager handle could not be created.");
        }
        AddOwnedContentManager();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_root_directory_size(
    const CNA_Handle contentManagerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The root-directory size output is null.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = contentManager->value->getRootDirectoryProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_copy_root_directory(
    const CNA_Handle contentManagerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The root-directory output buffer is invalid.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::string& rootDirectory =
            contentManager->value->getRootDirectoryProperty();
        *outBytes = rootDirectory.size();
        if (capacity < rootDirectory.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The root-directory output buffer is too small.");
        }
        if (!rootDirectory.empty()) {
            std::memcpy(destination, rootDirectory.data(), rootDirectory.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_set_root_directory(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView rootDirectory)
{
    return CallWithExceptionBarrier([&]() {
        std::string rootDirectoryCopy;
        if (const CNA_Result result = CopyStringView(
                rootDirectory,
                true,
                &rootDirectoryCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The ContentManager root directory is not valid UTF-8.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        contentManager->value->setRootDirectoryProperty(std::move(rootDirectoryCopy));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_unload(const CNA_Handle contentManagerHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        contentManager->value->Unload();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_load_texture2d(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() {
        if (outTexture == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The loaded Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;

        std::string assetNameCopy;
        if (const CNA_Result result = CopyStringView(assetName, true, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The content asset name is not valid UTF-8.");
        }
        if (assetNameCopy.empty()) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The content asset name must not be empty.");
        }

        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        try {
            Texture2D loaded = contentManager->value->Load<Texture2D>(assetNameCopy);
            if (loaded.getFormatProperty() != SurfaceFormat::Color) {
                return Fail(
                    CNA_RESULT_NOT_SUPPORTED,
                    CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                    "The initial C content loader supports only Color Texture2D assets.");
            }
            return CNA::C::Detail::CreateOwnedTexture2D(
                std::make_shared<Texture2D>(std::move(loaded)),
                contentManager->parentGame,
                outTexture);
        } catch (const ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
    });
}

CNA_Result cna_content_manager_destroy(const CNA_Handle contentManagerHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(
                contentManagerHandle,
                &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        contentManager->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(contentManagerHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned ContentManager handle could not be released.");
        }
        RemoveOwnedContentManager();
        return CNA_RESULT_SUCCESS;
    });
}
