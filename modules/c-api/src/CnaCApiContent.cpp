// SPDX-License-Identifier: MS-PL

#include "CNA/C/content.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"

#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManifestEntry.hpp"
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"
#include "CNA/Content/ForeignContentObjectEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <any>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedContentManager;
using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedContentManager;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::Texture2DResource;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentManifestEntry;
using Microsoft::Xna::Framework::Content::ContentManifestReaderUsage;
using Microsoft::Xna::Framework::Content::ResourceContentManager;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct ContentManagerResource final {
    std::shared_ptr<ContentManager> value;
    CNA_Handle parentGame;
    // The canonical manager stores a raw device pointer, so the C layer remembers which borrowed
    // handle supplied it and re-validates that handle before ever answering with it again.
    CNA_Handle graphicsDevice;
    // A game owns its own content manager as a value member. C can reach it, but never owns it: the
    // handle borrows, refuses to be destroyed and is released with the game.
    bool borrowed = false;
};

// A game owns exactly one content manager as a value member, so C borrows it through a single
// handle that is created on first use and released when the game goes away. Handing out a new
// handle per call would give a caller several names for one object and several chances to get its
// lifetime wrong.
std::mutex& BorrowedGameContentMutex()
{
    static std::mutex mutex;
    return mutex;
}

CNA_Handle& BorrowedGameContentHandle()
{
    static CNA_Handle handle = CNA_INVALID_HANDLE;
    return handle;
}

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

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
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

[[nodiscard]] CNA_Result CopyAssetName(const CNA_StringView value, std::string* const outValue)
{
    if (const CNA_Result result = CopyStringView(value, true, outValue);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The content asset name is not valid UTF-8.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetManifestEntry(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    std::shared_ptr<ContentManagerResource>* const outContentManager,
    const ContentManifestEntry** const outEntry)
{
    if (const CNA_Result result = GetContentManager(contentManagerHandle, outContentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<ContentManifestEntry>& manifest =
        (*outContentManager)->value->GetContentManifest();
    if (index >= manifest.size()) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The content manifest index is outside the manifest snapshot.");
    }
    *outEntry = &manifest[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

// The canonical summary is explicitly unordered and rebuilt per call, so the C routes impose a
// stable reader-name ordering; without it a count call and the copy call that follows it could
// disagree about which row an index names.
[[nodiscard]] std::vector<ContentManifestReaderUsage> SortedReaderUsage(
    const std::shared_ptr<ContentManagerResource>& contentManager)
{
    std::vector<ContentManifestReaderUsage> usage =
        contentManager->value->GetXnbReaderUsageSummary();
    std::sort(
        usage.begin(),
        usage.end(),
        [](const ContentManifestReaderUsage& left, const ContentManifestReaderUsage& right) {
            return left.readerName < right.readerName;
        });
    return usage;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result BorrowContentManager(
    const CNA_Handle handle,
    BorrowedContentManager* const outContentManager)
{
    if (outContentManager == nullptr) {
        return InvalidArgument("The borrowed ContentManager output is null.");
    }
    *outContentManager = BorrowedContentManager{};
    std::shared_ptr<ContentManagerResource> contentManager;
    if (const CNA_Result result = GetContentManager(handle, &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outContentManager->value = contentManager->value.get();
    outContentManager->owner = contentManager;
    outContentManager->parentGame = contentManager->parentGame;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

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
            ContentManagerResource{
                nativeManager,
                graphicsDevice->parentGame,
                graphicsDeviceHandle,
                false});
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
        } catch (const std::bad_any_cast&) {
            // A compiled asset whose root reader produced a different type. The canonical read
            // reports that by failing the unbox, which is not a runtime_error and would otherwise
            // reach the barrier's catch-all and be reported as an internal fault -- an honest
            // "this asset is not that type" turned into "something went wrong inside CNA".
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader produced a different type than this loader reads.");
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
    });
}

CNA_Result cna_content_manager_create_resource(
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

        const std::shared_ptr<ContentManager> nativeManager =
            std::make_shared<ResourceContentManager>(nullptr);
        nativeManager->setRootDirectoryProperty(std::move(rootDirectory));
        nativeManager->setGraphicsDevice(*graphicsDevice->value);
        const auto resource = std::make_shared<ContentManagerResource>(
            ContentManagerResource{
                nativeManager,
                graphicsDevice->parentGame,
                graphicsDeviceHandle,
                false});
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

CNA_Result cna_content_manager_load_sound_effect(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outSoundEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSoundEffect == nullptr) {
            return InvalidArgument("The loaded SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;

        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (assetNameCopy.empty()) {
            return InvalidArgument("The content asset name must not be empty.");
        }

        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        try {
            SoundEffect loaded = contentManager->value->Load<SoundEffect>(assetNameCopy);
            return CNA::C::Detail::CreateOwnedSoundEffect(
                std::make_shared<SoundEffect>(std::move(loaded)),
                contentManager->parentGame,
                outSoundEffect);
        } catch (const NoAudioHardwareException& exception) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                exception.what());
        } catch (const ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // A compiled asset whose root reader produced a different type. The canonical read
            // reports that by failing the unbox, which is not a runtime_error and would otherwise
            // reach the barrier's catch-all and be reported as an internal fault -- an honest
            // "this asset is not that type" turned into "something went wrong inside CNA".
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader produced a different type than this loader reads.");
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
    });
}

CNA_Result cna_content_manager_load_texture_cube(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The loaded TextureCube output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;

        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (assetNameCopy.empty()) {
            return InvalidArgument("The content asset name must not be empty.");
        }

        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        try {
            TextureCube loaded = contentManager->value->Load<TextureCube>(assetNameCopy);
            return CNA::C::Detail::CreateOwnedTextureCube(
                std::make_shared<TextureCube>(std::move(loaded)),
                contentManager->parentGame,
                outTexture);
        } catch (const ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // A compiled asset whose root reader produced a different type. The canonical read
            // reports that by failing the unbox, which is not a runtime_error and would otherwise
            // reach the barrier's catch-all and be reported as an internal fault -- an honest
            // "this asset is not that type" turned into "something went wrong inside CNA".
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader produced a different type than this loader reads.");
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
    });
}

CNA_Result cna_content_manager_load_sprite_font(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outSpriteFont,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() {
        if (outSpriteFont == nullptr || outTexture == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A loaded SpriteFont output handle is null.");
        }
        *outSpriteFont = CNA_INVALID_HANDLE;
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
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::shared_ptr<SpriteFont> font;
        try {
            font = std::make_shared<SpriteFont>(
                contentManager->value->Load<SpriteFont>(assetNameCopy));
        } catch (const ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // A compiled asset whose root reader produced a different type. The canonical read
            // reports that by failing the unbox, which is not a runtime_error and would otherwise
            // reach the barrier's catch-all and be reported as an internal fault -- an honest
            // "this asset is not that type" turned into "something went wrong inside CNA".
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader produced a different type than this loader reads.");
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }

        // The atlas has to become a handle of its own: a SpriteFont holds its texture by value,
        // and every route that retains one -- this ABI's own font resource included -- speaks in
        // Texture2D handles. The copy shares the underlying renderer resource, which is why the
        // font's retention count below is what keeps a caller from disposing it early.
        if (const CNA_Result result = CNA::C::Detail::CreateOwnedTexture2D(
                std::make_shared<Texture2D>(font->getTextureEXT()),
                contentManager->parentGame,
                outTexture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(*outTexture, &texture);
            result != CNA_RESULT_SUCCESS) {
            static_cast<void>(GetRuntimeHandles().Release(*outTexture));
            *outTexture = CNA_INVALID_HANDLE;
            return result;
        }

        const auto resource = std::make_shared<SpriteFontResource>(
            SpriteFontResource{font, texture, contentManager->parentGame});
        if (const CNA_Result result = GetRuntimeHandles().Create(
                ObjectKind::SpriteFont, resource, outSpriteFont);
            result != CNA_RESULT_SUCCESS) {
            static_cast<void>(GetRuntimeHandles().Release(*outTexture));
            *outTexture = CNA_INVALID_HANDLE;
            *outSpriteFont = CNA_INVALID_HANDLE;
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The loaded SpriteFont handle could not be created.");
        }
        ++texture->activeFontReferenceCount;
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_load_foreign_ext(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    void** const outObject)
{
    return CallWithExceptionBarrier([&]() {
        if (outObject == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The loaded foreign-asset output pointer is null.");
        }
        *outObject = nullptr;

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
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        try {
            const CNA::Content::ForeignContentObjectEXT loaded =
                contentManager->value->Load<CNA::Content::ForeignContentObjectEXT>(assetNameCopy);
            *outObject = loaded.value;
            return CNA_RESULT_SUCCESS;
        } catch (const ContentLoadException& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        } catch (const std::bad_any_cast&) {
            // The file's root reader produced something else, which means the asset is not the
            // caller's type at all. Reporting the mismatch beats returning a pointer into an
            // object of a different type.
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The asset's root type reader is not a caller-registered reader, so it did not "
                "produce a foreign object.");
        } catch (const System::NotSupportedException&) {
            throw;
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
    });
}

CNA_Result cna_content_manager_get_asset_path_size(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The asset-path size output is null.");
        }
        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = contentManager->value->BuildAssetPath(assetNameCopy).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_copy_asset_path(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            contentManager->value->BuildAssetPath(assetNameCopy),
            destination,
            capacity,
            outBytes,
            "The asset-path output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_manager_get_normalized_key_size(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The normalized-key size output is null.");
        }
        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = contentManager->value->NormalizeKey(assetNameCopy).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_copy_normalized_key(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string assetNameCopy;
        if (const CNA_Result result = CopyAssetName(assetName, &assetNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            contentManager->value->NormalizeKey(assetNameCopy),
            destination,
            capacity,
            outBytes,
            "The normalized-key output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_manager_register_builtin_loaders(const CNA_Handle contentManagerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        contentManager->value->RegisterBuiltinLoaders();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_has_service_provider(
    const CNA_Handle contentManagerHandle,
    CNA_Bool* const outHasServiceProvider)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasServiceProvider == nullptr) {
            return InvalidArgument("The service-provider presence output is null.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasServiceProvider =
            contentManager->value->getServiceProviderProperty() != nullptr ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_graphics_device(
    const CNA_Handle contentManagerHandle,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGraphicsDevice == nullptr) {
            return InvalidArgument("The graphics-device output handle is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                contentManager->graphicsDevice,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (&contentManager->value->getGraphicsDeviceInternal() != graphicsDevice->value) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical content manager reported a different graphics device.");
        }
        *outGraphicsDevice = contentManager->graphicsDevice;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_set_graphics_device(
    const CNA_Handle contentManagerHandle,
    const CNA_Handle graphicsDeviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (graphicsDevice->parentGame != contentManager->parentGame) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The graphics device belongs to a different game than the content manager.");
        }
        contentManager->value->setGraphicsDevice(*graphicsDevice->value);
        contentManager->graphicsDevice = graphicsDeviceHandle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_refresh_content_manifest(const CNA_Handle contentManagerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        contentManager->value->RefreshContentManifest();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_manifest_entry_count(
    const CNA_Handle contentManagerHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The manifest entry-count output is null.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = contentManager->value->GetContentManifest().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_manifest_entry(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    CNA_ContentManifestEntryInfo* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEntry == nullptr || outEntry->struct_size < sizeof(CNA_ContentManifestEntryInfo) ||
            outEntry->struct_version != StructureVersion) {
            return InvalidArgument("The manifest entry output structure is invalid.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        const ContentManifestEntry* entry = nullptr;
        if (const CNA_Result result = GetManifestEntry(
                contentManagerHandle,
                index,
                &contentManager,
                &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outEntry->has_xnb = entry->hasXnb ? CNA_TRUE : CNA_FALSE;
        outEntry->has_cnj = entry->hasCnj ? CNA_TRUE : CNA_FALSE;
        std::memset(outEntry->reserved, 0, sizeof(outEntry->reserved));
        outEntry->native_extension_count = entry->nativeExtensions.size();
        outEntry->xnb_reader_name_count = entry->xnbReaderNames.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_copy_manifest_relative_path(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        const ContentManifestEntry* entry = nullptr;
        if (const CNA_Result result = GetManifestEntry(
                contentManagerHandle,
                index,
                &contentManager,
                &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            entry->relativePath,
            destination,
            capacity,
            outBytes,
            "The manifest relative-path output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_manager_copy_manifest_native_extension(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    const uint64_t extensionIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        const ContentManifestEntry* entry = nullptr;
        if (const CNA_Result result = GetManifestEntry(
                contentManagerHandle,
                index,
                &contentManager,
                &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (extensionIndex >= entry->nativeExtensions.size()) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The native-extension index is outside this manifest entry.");
        }
        return CopyText(
            entry->nativeExtensions[static_cast<std::size_t>(extensionIndex)],
            destination,
            capacity,
            outBytes,
            "The native-extension output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_manager_copy_manifest_xnb_reader_name(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    const uint64_t nameIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        const ContentManifestEntry* entry = nullptr;
        if (const CNA_Result result = GetManifestEntry(
                contentManagerHandle,
                index,
                &contentManager,
                &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (nameIndex >= entry->xnbReaderNames.size()) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The reader-name index is outside this manifest entry.");
        }
        return CopyText(
            entry->xnbReaderNames[static_cast<std::size_t>(nameIndex)],
            destination,
            capacity,
            outBytes,
            "The reader-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_content_manager_get_xnb_reader_usage_count(
    const CNA_Handle contentManagerHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The reader-usage count output is null.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = SortedReaderUsage(contentManager).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_get_xnb_reader_usage(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    CNA_ContentReaderUsageInfo* const outUsage)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outUsage == nullptr || outUsage->struct_size < sizeof(CNA_ContentReaderUsageInfo) ||
            outUsage->struct_version != StructureVersion) {
            return InvalidArgument("The reader-usage output structure is invalid.");
        }
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<ContentManifestReaderUsage> usage = SortedReaderUsage(contentManager);
        if (index >= usage.size()) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The reader-usage index is outside the summary.");
        }
        const ContentManifestReaderUsage& row = usage[static_cast<std::size_t>(index)];
        outUsage->is_registered = row.isRegistered ? CNA_TRUE : CNA_FALSE;
        std::memset(outUsage->reserved, 0, sizeof(outUsage->reserved));
        outUsage->file_count = static_cast<uint64_t>(row.fileCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_content_manager_copy_xnb_reader_usage_name(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentManagerResource> contentManager;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<ContentManifestReaderUsage> usage = SortedReaderUsage(contentManager);
        if (index >= usage.size()) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The reader-usage index is outside the summary.");
        }
        return CopyText(
            usage[static_cast<std::size_t>(index)].readerName,
            destination,
            capacity,
            outBytes,
            "The reader-name output buffer is invalid or too small.");
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
        if (contentManager->borrowed) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The game owns this ContentManager; it is released with the game.");
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

CNA_Result cna_game_get_content_manager_ext(
    const CNA_Handle gameHandle,
    CNA_Handle* const outContentManager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContentManager == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The content-manager output handle is null.");
        }
        *outContentManager = CNA_INVALID_HANDLE;
        Microsoft::Xna::Framework::Game* game = nullptr;
        if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::lock_guard<std::mutex> lock(BorrowedGameContentMutex());
        CNA_Handle& cached = BorrowedGameContentHandle();
        if (cached != CNA_INVALID_HANDLE) {
            std::shared_ptr<ContentManagerResource> existing;
            if (GetRuntimeHandles().Get(cached, ObjectKind::ContentManager, &existing) ==
                CNA_RESULT_SUCCESS) {
                *outContentManager = cached;
                return CNA_RESULT_SUCCESS;
            }
            cached = CNA_INVALID_HANDLE;
        }
        // A non-owning shared_ptr: the manager is a value member of the game and outlives every
        // borrow, so nothing here may ever delete it.
        std::shared_ptr<ContentManager> borrowed(
            std::shared_ptr<void>(),
            &game->getContentProperty());
        const auto resource = std::make_shared<ContentManagerResource>(
            ContentManagerResource{borrowed, gameHandle, CNA_INVALID_HANDLE, true});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ContentManager,
            resource,
            outContentManager);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The borrowed ContentManager handle could not be created.");
        }
        cached = *outContentManager;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_content_manager_ext(
    const CNA_Handle gameHandle,
    const CNA_Handle contentManagerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::Game* game = nullptr;
        if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ContentManagerResource> source;
        if (const CNA_Result result = GetContentManager(contentManagerHandle, &source);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical setter copies, so the caller keeps its own manager and later changes to it
        // never reach the game.
        game->setContentProperty(*source->value);
        return CNA_RESULT_SUCCESS;
    });
}

namespace CNA::C::Detail {

void ResetGameContentManagerState() noexcept
{
    const std::lock_guard<std::mutex> lock(BorrowedGameContentMutex());
    CNA_Handle& cached = BorrowedGameContentHandle();
    if (cached == CNA_INVALID_HANDLE) {
        return;
    }
    static_cast<void>(GetRuntimeHandles().Release(cached));
    cached = CNA_INVALID_HANDLE;
}

} // namespace CNA::C::Detail
