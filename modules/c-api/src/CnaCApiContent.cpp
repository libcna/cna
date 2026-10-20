// SPDX-License-Identifier: MS-PL

#include "CNA/C/content.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiContentDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

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
using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IoError;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedContentManager;
using CNA::C::Detail::RequireIndex;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentManifestEntry;
using Microsoft::Xna::Framework::Content::ContentManifestReaderUsage;
using Microsoft::Xna::Framework::Content::ResourceContentManager;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

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
    return LookupHandle(handle, ObjectKind::ContentManager, outContentManager);
}

[[nodiscard]] CNA_Result CopyAssetName(const CNA_StringView value, std::string* const outValue)
{
    if (const CNA_Result result = ReadText(
            value, "The content asset name is not valid UTF-8.", outValue, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return OutOfRange("The content manifest index is outside the manifest snapshot.");
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

namespace {

[[nodiscard]] CNA_Result ContentManagerCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ContentManagerCreateInfo* const createInfo,
    CNA_Handle* const outContentManager)
{
    if (!IsVersionedStructure(createInfo) || createInfo->reserved != 0U) {
        return InvalidArgument("The ContentManager creation configuration is invalid.");
    }

    std::string rootDirectory;
    if (const CNA_Result result = ReadText(
            createInfo->root_directory, "The ContentManager root directory is not valid UTF-8.",
            &rootDirectory, EmbeddedNul::Refused);
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

    const auto nativeManager = std::make_shared<ContentManager>();
    nativeManager->setRootDirectoryProperty(std::move(rootDirectory));
    nativeManager->setGraphicsDevice(*graphicsDevice->value);
    const auto resource = std::make_shared<ContentManagerResource>(
        ContentManagerResource{
            nativeManager,
            graphicsDevice->parentGame,
            graphicsDeviceHandle,
            false});
    if (const CNA_Result result = CreateHandle(ObjectKind::ContentManager,
        resource,
        outContentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedContentManager();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerLoadTexture2d(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outTexture)
{
    std::string assetNameCopy;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameCopy,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (assetNameCopy.empty()) {
        return InvalidArgument("The content asset name must not be empty.");
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
        return CNA::C::Detail::CreateOwnedTexture2D(
            std::make_shared<Texture2D>(std::move(loaded)),
            contentManager->parentGame,
            outTexture);
    } catch (const ContentLoadException&) {
        // The common barrier preserves both the IO result and the canonical exception metadata.
        throw;
    } catch (const std::bad_any_cast&) {
        // A compiled asset whose root reader produced a different type. The canonical read
        // reports that by failing the unbox, which is not a runtime_error and would otherwise
        // reach the barrier's catch-all and be reported as an internal fault -- an honest
        // "this asset is not that type" turned into "something went wrong inside CNA".
        return IoError(
            "The asset's root type reader produced a different type than this loader reads.");
    } catch (const System::NotSupportedException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
    }
}

[[nodiscard]] CNA_Result ContentManagerCreateResource(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ContentManagerCreateInfo* const createInfo,
    CNA_Handle* const outContentManager)
{
    if (!IsVersionedStructure(createInfo) || createInfo->reserved != 0U) {
        return InvalidArgument("The ContentManager creation configuration is invalid.");
    }

    std::string rootDirectory;
    if (const CNA_Result result = ReadText(
            createInfo->root_directory, "The ContentManager root directory is not valid UTF-8.",
            &rootDirectory, EmbeddedNul::Refused);
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
    if (const CNA_Result result = CreateHandle(ObjectKind::ContentManager,
        resource,
        outContentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedContentManager();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerLoadSoundEffect(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outSoundEffect)
{
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
        return NotSupported(exception.what());
    } catch (const ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // A compiled asset whose root reader produced a different type. The canonical read
        // reports that by failing the unbox, which is not a runtime_error and would otherwise
        // reach the barrier's catch-all and be reported as an internal fault -- an honest
        // "this asset is not that type" turned into "something went wrong inside CNA".
        return IoError(
            "The asset's root type reader produced a different type than this loader reads.");
    } catch (const System::NotSupportedException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
    }
}

[[nodiscard]] CNA_Result ContentManagerLoadTextureCube(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outTexture)
{
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
    } catch (const ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // A compiled asset whose root reader produced a different type. The canonical read
        // reports that by failing the unbox, which is not a runtime_error and would otherwise
        // reach the barrier's catch-all and be reported as an internal fault -- an honest
        // "this asset is not that type" turned into "something went wrong inside CNA".
        return IoError(
            "The asset's root type reader produced a different type than this loader reads.");
    } catch (const System::NotSupportedException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
    }
}

[[nodiscard]] CNA_Result ContentManagerLoadSpriteFont(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outSpriteFont,
    CNA_Handle* const outTexture)
{
    std::string assetNameCopy;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameCopy,
            EmbeddedNul::Refused);
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

    std::shared_ptr<SpriteFont> font;
    try {
        font = std::make_shared<SpriteFont>(
            contentManager->value->Load<SpriteFont>(assetNameCopy));
    } catch (const ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // A compiled asset whose root reader produced a different type. The canonical read
        // reports that by failing the unbox, which is not a runtime_error and would otherwise
        // reach the barrier's catch-all and be reported as an internal fault -- an honest
        // "this asset is not that type" turned into "something went wrong inside CNA".
        return IoError(
            "The asset's root type reader produced a different type than this loader reads.");
    } catch (const System::NotSupportedException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
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
    AddOwnedGraphicsResourceFor(contentManager->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerLoadForeignExt(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    void** const outObject)
{
    *outObject = nullptr;

    std::string assetNameCopy;
    if (const CNA_Result result = ReadText(
            assetName, "The content asset name is not valid UTF-8.", &assetNameCopy,
            EmbeddedNul::Refused);
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
        const CNA::Content::ForeignContentObjectEXT loaded =
            contentManager->value->Load<CNA::Content::ForeignContentObjectEXT>(assetNameCopy);
        *outObject = loaded.value;
        return CNA_RESULT_SUCCESS;
    } catch (const ContentLoadException&) {
        throw;
    } catch (const std::bad_any_cast&) {
        // The file's root reader produced something else, which means the asset is not the
        // caller's type at all. Reporting the mismatch beats returning a pointer into an
        // object of a different type.
        return IoError(
            "The asset's root type reader is not a caller-registered reader, so it did not "
            "produce a foreign object.");
    } catch (const System::NotSupportedException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
    }
}

[[nodiscard]] CNA_Result ContentManagerRegisterCnjLoaderExt(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView typeName,
    const CNA_CnjLoaderCallback callback,
    void* const context)
{
    if (callback == nullptr) {
        return InvalidArgument("The .cnj loader callback is null.");
    }
    std::string typeNameCopy;
    if (const CNA_Result result = ReadText(
            typeName, "The .cnj type name is not valid UTF-8.", &typeNameCopy,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (typeNameCopy.empty()) {
        return InvalidArgument("The .cnj type name must not be empty.");
    }

    std::shared_ptr<ContentManagerResource> contentManager;
    if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    try {
        // The canonical factory is a std::function returning T by value, which is why this
        // route exists at all: T is CNA::Content::ForeignContentObjectEXT, a concrete type C
        // can name through the same carrier a registered XNB reader already produces. The
        // coverage matrix recorded this family as having no C form "because registering a
        // reader requires naming an arbitrary C++ type T" -- true of the general template and
        // not of the one instantiation a C caller needs.
        contentManager->value->RegisterCnjLoader<CNA::Content::ForeignContentObjectEXT>(
            typeNameCopy,
            [callback, context](const std::string& json, ContentManager&)
                -> CNA::Content::ForeignContentObjectEXT {
                const CNA_StringView view{json.data(), json.size()};
                void* object = nullptr;
                const CNA_Result result = callback(context, view, &object);
                if (result != CNA_RESULT_SUCCESS) {
                    throw ContentLoadException(
                        "A caller-supplied .cnj loader failed (CNA_Result " +
                        std::to_string(result) + ").");
                }
                return CNA::Content::ForeignContentObjectEXT{object};
            });
    } catch (const std::invalid_argument& exception) {
        return InvalidArgument(exception.what());
    } catch (const std::logic_error& exception) {
        // The canonical refusal for a name already taken. It is a state failure rather than a
        // bad argument: the same name is perfectly valid on a manager that does not hold it.
        return InvalidState(exception.what());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerGetAssetPathSize(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    uint64_t* const outBytes)
{
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
}

[[nodiscard]] CNA_Result ContentManagerCopyAssetPath(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
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
        outBytes);
}

[[nodiscard]] CNA_Result ContentManagerGetNormalizedKeySize(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    uint64_t* const outBytes)
{
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
}

[[nodiscard]] CNA_Result ContentManagerCopyNormalizedKey(
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
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
        outBytes);
}

[[nodiscard]] CNA_Result ContentManagerGetGraphicsDevice(
    const CNA_Handle contentManagerHandle,
    CNA_Handle* const outGraphicsDevice)
{
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
        return InternalError(
            "The canonical content manager reported a different graphics device.");
    }
    *outGraphicsDevice = contentManager->graphicsDevice;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerSetGraphicsDevice(
    const CNA_Handle contentManagerHandle,
    const CNA_Handle graphicsDeviceHandle)
{
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
        return InvalidState(
            "The graphics device belongs to a different game than the content manager.");
    }
    contentManager->value->setGraphicsDevice(*graphicsDevice->value);
    contentManager->graphicsDevice = graphicsDeviceHandle;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerGetManifestEntry(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    CNA_ContentManifestEntryInfo* const outEntry)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outEntry, "The manifest entry output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
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
    outEntry->has_xnb = ToCBool(entry->hasXnb);
    outEntry->has_cnj = ToCBool(entry->hasCnj);
    std::memset(outEntry->reserved, 0, sizeof(outEntry->reserved));
    outEntry->native_extension_count = entry->nativeExtensions.size();
    outEntry->xnb_reader_name_count = entry->xnbReaderNames.size();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerGetXnbReaderUsage(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    CNA_ContentReaderUsageInfo* const outUsage)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outUsage, "The reader-usage output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<ContentManagerResource> contentManager;
    if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<ContentManifestReaderUsage> usage = SortedReaderUsage(contentManager);
    if (index >= usage.size()) {
        return OutOfRange("The reader-usage index is outside the summary.");
    }
    const ContentManifestReaderUsage& row = usage[static_cast<std::size_t>(index)];
    outUsage->is_registered = ToCBool(row.isRegistered);
    std::memset(outUsage->reserved, 0, sizeof(outUsage->reserved));
    outUsage->file_count = static_cast<uint64_t>(row.fileCount);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ContentManagerCopyXnbReaderUsageName(
    const CNA_Handle contentManagerHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<ContentManagerResource> contentManager;
    if (const CNA_Result result = GetContentManager(contentManagerHandle, &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<ContentManifestReaderUsage> usage = SortedReaderUsage(contentManager);
    if (index >= usage.size()) {
        return OutOfRange("The reader-usage index is outside the summary.");
    }
    return CopyText(
        usage[static_cast<std::size_t>(index)].readerName,
        destination,
        capacity,
        outBytes);
}

[[nodiscard]] CNA_Result ContentManagerDestroy(const CNA_Handle contentManagerHandle)
{
    std::shared_ptr<ContentManagerResource> contentManager;
    if (const CNA_Result result = GetContentManager(
            contentManagerHandle,
            &contentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (contentManager->borrowed) {
        return InvalidState("The game owns this ContentManager; it is released with the game.");
    }
    contentManager->value->Dispose();
    if (const CNA_Result releaseResult = ReleaseHandle(contentManagerHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    RemoveOwnedContentManager();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameGetContentManagerExt(
    const CNA_Handle gameHandle,
    CNA_Handle* const outContentManager)
{
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
    if (const CNA_Result result = CreateHandle(ObjectKind::ContentManager,
        resource,
        outContentManager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    cached = *outContentManager;
    return CNA_RESULT_SUCCESS;
}

} // namespace

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

CNA_Result BorrowContentManagerForCallback(
    ContentManager& contentManager,
    CNA_Handle* const outContentManager)
{
    // A fresh handle per callback rather than the cached game one: this manager is whichever one is
    // performing the load, it may be owned by nobody C knows, and its handle must die with the call
    // that made it. `borrowed` is what makes cna_content_manager_destroy refuse it.
    const auto resource = std::make_shared<ContentManagerResource>(
        ContentManagerResource{
            std::shared_ptr<ContentManager>(&contentManager, [](ContentManager*) {}),
            CNA_INVALID_HANDLE,
            CNA_INVALID_HANDLE,
            true});
    return GetRuntimeHandles().Create(
        ObjectKind::ContentManager, resource, outContentManager);
}

CNA_Result GetContentManagerObject(
    const CNA_Handle handle,
    ContentManager** const outContentManager)
{
    std::shared_ptr<ContentManagerResource> resource;
    if (const CNA_Result result = GetContentManager(handle, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outContentManager = resource->value.get();
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

// The routes generated from modules/c-api/spec/Content.toml.
#include "generated/Content.inc"
