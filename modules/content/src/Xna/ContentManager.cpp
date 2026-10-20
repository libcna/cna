// SPDX-License-Identifier: MS-PL
// ContentManager itself: the root, the cache, disposal, path resolution and the content manifest.
//
// Link boundary: the readers of each asset type live in that family's own translation unit
// (Content*Loaders.cpp) and are registered by the first Load<T>() of the type
// (detail::BuiltinContent), never from here -- one object naming every reader made every game link
// the Model reader, the glTF importer and Draco (docs/link-size-architecture.md).
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/ContentRoot.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Internal/GltfImport/CgltfFileCallbacks.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/Graphics/ModelMaterialVariantsEXT.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/IO/IOException.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <filesystem>
#include <optional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Content
{
    // ---------------------------------------------------------------------------
    // Property accessors
    // ---------------------------------------------------------------------------

    const std::string& ContentManager::getRootDirectoryProperty() const
    {
        return rootDirectory_;
    }

    void ContentManager::setRootDirectoryProperty(const std::string& v)
    {
        // Measured XNA refuses even same-root assignment with cached assets; FNA's auto-property does not.
        if (!loadedAssets_.empty())
        {
            throw System::InvalidOperationException("RootDirectory cannot be changed while content is loaded.");
        }
        rootDirectory_ = v;
    }

    void ContentManager::setRootDirectoryProperty(std::string&& v)
    {
        if (!loadedAssets_.empty())
        {
            throw System::InvalidOperationException("RootDirectory cannot be changed while content is loaded.");
        }
        rootDirectory_ = std::move(v);
    }

    CNA::Internal::Xnb::XnbHeader ContentManager::ReadXnbHeader(
        const std::vector<std::uint8_t>& bytes, const std::string& xnbPath)
    {
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        {
            throw ContentLoadException("ContentManager: '" + xnbPath + "' is too large to load.");
        }
        CNA::Internal::Xnb::XnbHeader header;
        try
        {
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            System::IO::BinaryReader reader(&stream, true);
            header = CNA::Internal::Xnb::ParseXnbHeader(reader, xnbPath);
        }
        catch (const System::IO::IOException& error)
        {
            // The low-level parser keeps its EOF contract; XNA wraps I/O at the content-load boundary.
            throw ContentLoadException("Error loading '" + xnbPath + "'. Invalid XNB header.", error);
        }
        // Validate the file's declared range before either loader performs pointer arithmetic.
        if (header.totalLength < 10 || static_cast<std::size_t>(header.totalLength) > bytes.size())
        {
            throw ContentLoadException(
                "'" + xnbPath + "' declares a totalLength (" + std::to_string(header.totalLength) +
                ") inconsistent with its actual file size (" + std::to_string(bytes.size()) + ").");
        }
        if (header.compression != CNA::Internal::Xnb::XnbCompression::None && header.totalLength < 14)
        {
            throw ContentLoadException(
                "'" + xnbPath + "' is truncated before its compressed-payload size field.");
        }
        return header;
    }

    // ---------------------------------------------------------------------------
    // Constructor / lifecycle
    // ---------------------------------------------------------------------------

    // Every built-in .cnb loader counts as registered from a manager's construction on, as when
    // the constructor registered them all; each is installed by the first load of its type
    // (detail::BuiltinContent), so a program links the loaders of the types it loads.
    ContentManager::ContentManager(System::IServiceProvider* serviceProvider)
        : serviceProvider_(serviceProvider)
    {
        CNA::Content::CnbLoaderRegistry::DeclareBuiltIns();
    }

    ContentManager::ContentManager(System::IServiceProvider* serviceProvider,
                                   const std::string& rootDirectory)
        : serviceProvider_(serviceProvider), rootDirectory_(rootDirectory)
    {
        CNA::Content::CnbLoaderRegistry::DeclareBuiltIns();
    }

    ContentManager::ContentManager()
        : rootDirectory_("Content")
    {
        CNA::Content::CnbLoaderRegistry::DeclareBuiltIns();
    }

    System::IServiceProvider* ContentManager::getServiceProviderProperty() const
    {
        return serviceProvider_;
    }

    void ContentManager::setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice)
    {
        graphicsDevice_ = &graphicsDevice;
    }

    Graphics::GraphicsDevice& ContentManager::getGraphicsDeviceInternal() const
    {
        Graphics::GraphicsDevice* graphicsDevice = graphicsDevice_;
        if (graphicsDevice == nullptr && serviceProvider_ != nullptr)
        {
            auto* graphicsDeviceService = static_cast<Graphics::IGraphicsDeviceService*>(
                serviceProvider_->GetService(typeid(Graphics::IGraphicsDeviceService)));
            if (graphicsDeviceService != nullptr)
            {
                graphicsDevice = graphicsDeviceService->getGraphicsDeviceProperty();
            }
        }

        if (graphicsDevice == nullptr)
        {
            throw ContentLoadException(
                "ContentManager: no GraphicsDevice is available from the service provider.");
        }
        return *graphicsDevice;
    }

    void ContentManager::Dispose()
    {
        Dispose(true);
    }

    void ContentManager::Dispose(const bool disposing)
    {
        if (!disposed_)
        {
            // XNA unloads the cached assets only for an explicit disposal, and drops both tables
            // either way; Unload() is what clears them here.
            if (disposing)
            {
                Unload();
            }
            loadedAssets_.clear();
            disposed_ = true;
        }
    }

    std::vector<std::uint8_t> ContentManager::ReadStreamToEnd(System::IO::Stream& stream,
                                                              const std::string& assetName)
    {
        try
        {
            std::vector<std::uint8_t> bytes;
            std::array<std::uint8_t, 64 * 1024> buffer{};
            for (;;)
            {
                const auto read = stream.Read(buffer.data(), 0,
                                              static_cast<SharpRuntime::intcs>(buffer.size()));
                if (read <= 0)
                {
                    break;
                }
                bytes.insert(bytes.end(), buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(read));
            }
            return bytes;
        }
        catch (const ContentLoadException&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            throw ContentLoadException(
                "ContentManager: could not read asset '" + assetName + "'.", error);
        }
    }

    std::unique_ptr<System::IO::Stream> ContentManager::OpenStream(const std::string& assetName)
    {
        const std::string path = ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb");
        std::vector<std::uint8_t> bytes;
        try
        {
            // TryReadAssetBytes, not a plain file open: it is the resolution the rest of this
            // manager uses, and it covers the platform's packaged assets and their case-insensitive
            // matching as well as the filesystem. An asset served from a package is as much an
            // asset as one on disk, and a subclass overriding this method inherits neither concern.
            if (!TryReadAssetBytes(path, bytes))
            {
                throw ContentLoadException(
                    "ContentManager: could not find asset '" + assetName + "' at '" + path + "'.");
            }
        }
        catch (const ContentLoadException&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            // XNA reports a missing file and any other open failure the same way: a
            // ContentLoadException naming the asset, carrying the original error as its cause.
            throw ContentLoadException(
                "ContentManager: could not open asset '" + assetName + "' at '" + path + "'.",
                error);
        }

        // MemoryStream copies the range it is given, so the stream owns its bytes outright and the
        // local buffer can go: the caller owns the stream and nothing else holds the data.
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw ContentLoadException(
                "ContentManager: asset '" + assetName + "' is too large to open.");
        }
        return std::make_unique<System::IO::MemoryStream>(
            bytes.data(), static_cast<SharpRuntime::intcs>(bytes.size()), false);
    }

    void ContentManager::Unload()
    {
        loadedAssets_.clear();
    }

    // ---------------------------------------------------------------------------
    // Content manifest (plans/plan_xnb.md Phase B3: XNB-65/65A/66/67/61a)
    // ---------------------------------------------------------------------------

    std::vector<std::string> ContentManager::ScanXnbReaderNames(const std::filesystem::path& xnbPath) const
    {
        std::vector<std::string> names;
        try
        {
            std::ifstream file(xnbPath, std::ios::binary);
            if (!file.is_open())
            {
                return names;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            const std::string bytes = ss.str();
            if (bytes.size() < 10)
            {
                return names;
            }

            System::IO::MemoryStream headerStream(
                reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<int32_t>(bytes.size()));
            System::IO::BinaryReader headerReader(&headerStream, true);
            const auto header =
                CNA::Internal::Xnb::ParseXnbHeader(headerReader, CNA::Internal::PathToUtf8(xnbPath));

            if (header.compression != CNA::Internal::Xnb::XnbCompression::None)
            {
                // XNB-61b (compressed-file inventory) is unblocked now that Phase D's decompressor
                // exists, but not yet picked up -- an empty inventory for this one entry is not a
                // scan failure, matching the same "not implemented yet" treatment for every
                // compression scheme (Lzx included), not just the ones CNA can't decode at all.
                return names;
            }

            System::IO::MemoryStream bodyStream(
                reinterpret_cast<const uint8_t*>(bytes.data()) + 10,
                static_cast<int32_t>(bytes.size()) - 10);
            System::IO::BinaryReader bodyReader(&bodyStream, true);
            const auto table = CNA::Internal::Xnb::ParseXnbTypeReaderTable(
                bodyReader, CNA::Internal::PathToUtf8(xnbPath));
            names.reserve(table.size());
            for (const auto& entry : table)
            {
                names.push_back(entry.normalizedName);
            }
        }
        catch (...)
        {
            // Best-effort: a malformed .xnb elsewhere in the content root must not abort the
            // whole manifest scan -- just leave this one entry's inventory empty.
            names.clear();
        }
        return names;
    }

    void ContentManager::RefreshContentManifest()
    {
        namespace fs = std::filesystem;
        std::unordered_map<std::string, ContentManifestEntry> entriesByBase;

        // The root is UTF-8. Handed to the narrow fs:: overloads it was read as ANSI code page
        // bytes, so on Windows a content root with any character outside that code page simply did
        // not exist: the manifest came back EMPTY, every asset's real extension went undiscovered,
        // and Load<T>() then failed with "cannot open file" naming an extension-less path -- a
        // symptom several steps away from its cause.
        const std::optional<fs::path> root = CNA::Internal::TryPathFromUtf8(TitleRootedDirectory());

        std::error_code ec;
        if (!root || !fs::exists(*root, ec) || ec)
        {
            contentManifest_.clear();
            contentManifestBuilt_ = true;
            return;
        }

        auto it = fs::recursive_directory_iterator(
            *root, fs::directory_options::skip_permission_denied, ec);
        const auto end = fs::recursive_directory_iterator();
        for (; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || ec)
            {
                continue;
            }

            const fs::path& path = it->path();
            fs::path relative = fs::relative(path, *root, ec);
            if (ec)
            {
                continue;
            }

            const std::string ext = CNA::Internal::PathToUtf8(path.extension());
            // relStr becomes ContentManifestEntry::relativePath and the entriesByBase key, so it is
            // generic-form UTF-8: the identity has to match one produced on another platform.
            std::string relStr = CNA::Internal::PathToGenericUtf8(relative);
            const std::string base = relStr.substr(0, relStr.size() - ext.size());

            ContentManifestEntry& entry = entriesByBase[base];
            entry.relativePath = base;
            if (ext == ".xnb")
            {
                entry.hasXnb = true;
                entry.xnbReaderNames = ScanXnbReaderNames(path);
            }
            else if (ext == ".cnj")
            {
                entry.hasCnj = true;
            }
            else
            {
                entry.nativeExtensions.push_back(ext);
            }
        }

        contentManifest_.clear();
        contentManifest_.reserve(entriesByBase.size());
        for (auto& [base, entry] : entriesByBase)
        {
            contentManifest_.push_back(std::move(entry));
        }
        contentManifestBuilt_ = true;
    }

    const std::vector<ContentManifestEntry>& ContentManager::GetContentManifest()
    {
        if (!contentManifestBuilt_)
        {
            RefreshContentManifest();
        }
        return contentManifest_;
    }

    // ---------------------------------------------------------------------------
    // Path helpers
    // ---------------------------------------------------------------------------

    std::string ContentManager::TitleRootedDirectory() const
    {
#if defined(__ANDROID__)
        // Relative content is SDL's packaged-asset namespace there (ResolveExistingAssetPath).
        return rootDirectory_;
#else
        // XNA's ContentManager opens a relative content path through TitleContainer, under
        // TitleLocation.Path (XNA IL), not the working directory; a game started from anywhere
        // else found none of its content. ResolveContentRoot keeps the working directory as the
        // fallback.
        std::string title = CNA::Internal::TryGetTitlePath().value_or(std::string{});
        if (title.empty() && CNA::Platform::HasCurrentPlatform())
        {
            title = CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
        }
        return CNA::Internal::ResolveContentRoot(rootDirectory_, title);
#endif
    }

    std::string ContentManager::BuildAssetPath(const std::string& assetName) const
    {
        std::string normalizedRootDirectory = TitleRootedDirectory();
        std::replace(
            normalizedRootDirectory.begin(), normalizedRootDirectory.end(), '\\', '/');
        std::string normalizedAssetName = assetName;
        std::replace(
            normalizedAssetName.begin(), normalizedAssetName.end(), '\\', '/');
        if (assetName.empty())
        {
            return normalizedRootDirectory;
        }
        if (normalizedRootDirectory.empty())
        {
            return normalizedAssetName;
        }
        namespace fs = std::filesystem;
        // RootDirectory and the asset name are UTF-8 (docs/filesystem-path-model.md); the narrow
        // path constructor read them as ANSI code page bytes and .string() then threw on the way
        // back out, so a content root a Czech or Japanese user actually has stopped existing here.
        return CNA::Internal::PathToGenericUtf8(
            CNA::Internal::PathFromUtf8(normalizedRootDirectory)
            / CNA::Internal::PathFromUtf8(normalizedAssetName));
    }

    std::string ContentManager::ResolveExistingAssetPath(const std::string& path) const
    {
        namespace fs = std::filesystem;

        const std::optional<fs::path> requested = CNA::Internal::TryPathFromUtf8(path);
        if (!requested)
        {
            return path;
        }
#if defined(__ANDROID__)
        // Relative content paths belong to SDL's packaged-asset namespace on Android. They are
        // resolved by TryReadAssetBytes through the platform filesystem; walking the process
        // working directory here would instead enumerate `/`, which is unrelated and sandboxed.
        if (requested->is_relative())
        {
            return CNA::Internal::PathToGenericUtf8(*requested);
        }
#endif
        // The component-by-component case-insensitive walk used to be open-coded here -- the third
        // byte-for-byte copy of it in the repository, each one narrowing every directory entry it
        // enumerated, so a single non-ASCII sibling broke the lookup of an ordinary ASCII asset.
        return CNA::Internal::PathToGenericUtf8(
            CNA::Internal::ResolveExistingNativePath(*requested));
    }

    bool ContentManager::TryReadAssetBytes(
        const std::string& path, std::vector<std::uint8_t>& bytes) const
    {
        namespace fs = std::filesystem;

        const std::optional<fs::path> native = CNA::Internal::TryPathFromUtf8(path);
        if (!native)
        {
            return false;
        }
#if defined(__ANDROID__)
        if (native->is_relative())
        {
            return CNA::Platform::GetCurrentPlatform().GetFileSystem()
                ->TryLoadFileIgnoringCase(path, bytes);
        }
#endif
        std::error_code ec;
        if (fs::exists(*native, ec) && !ec)
        {
            std::ifstream file(*native, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                throw ContentLoadException("ContentManager: cannot open '" + path + "'.");
            }

            const std::streamsize length = file.tellg();
            if (length < 0)
            {
                throw ContentLoadException(
                    "ContentManager: cannot determine the size of '" + path + "'.");
            }
            if (static_cast<std::uintmax_t>(length) >
                static_cast<std::uintmax_t>(std::numeric_limits<int32_t>::max()))
            {
                throw ContentLoadException(
                    "ContentManager: '" + path + "' is too large to load.");
            }

            bytes.resize(static_cast<std::size_t>(length));
            file.seekg(0, std::ios::beg);
            if (length > 0 &&
                !file.read(reinterpret_cast<char*>(bytes.data()), length))
            {
                throw ContentLoadException("ContentManager: cannot read '" + path + "'.");
            }
            return true;
        }

        return CNA::Platform::GetCurrentPlatform().GetFileSystem()
            ->TryLoadFileIgnoringCase(path, bytes);
    }

    std::string ContentManager::NormalizeKey(const std::string& assetName) const
    {
        std::string key = assetName;
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    void ContentManager::RegisterBuiltinCnbLoader(std::uint32_t assetTypeId,
                                                  const std::string& canonicalTypeName,
                                                  CNA::Content::CnbLoaderRegistry::LoaderFn loader)
    {
        CNA::Content::CnbLoaderRegistry::RegisterDeclaredBuiltIn(assetTypeId, canonicalTypeName,
                                                                 std::move(loader));
    }
}
