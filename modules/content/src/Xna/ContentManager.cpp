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
#include "System/detail/OrdinalCase.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
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
#include "System/IO/DirectoryNotFoundException.hpp"
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
#include "System/IO/FileNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

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
#include <exception>

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        // An override may delegate to the base, replace its exception, or recursively load
        // another asset. Record the identity of only this call's base missing-file exception;
        // neither exception type nor message alone can distinguish a deliberate override error.
        //
        // That identity is the exception's cause, not the ContentLoadException object itself:
        // std::current_exception() may return a copy of the object in flight ([propagation]), and
        // MSVC's always does, so comparing it with a recorded exception_ptr was false there for
        // the base's own exception and no asset without an .xnb loaded on Windows. The cause is an
        // exception_ptr inside the exception, and every copy shares the object it refers to.
        struct CompiledStreamAttempt
        {
            static thread_local CompiledStreamAttempt* active;
            const ContentManager* manager;
            const std::string& assetName;
            std::exception_ptr missingCause;
            std::string missingMessage;
            CompiledStreamAttempt* previous;

            CompiledStreamAttempt(const ContentManager* owner, const std::string& name)
                : manager(owner), assetName(name), previous(active) { active = this; }
            ~CompiledStreamAttempt() { active = previous; }
            CompiledStreamAttempt(const CompiledStreamAttempt&) = delete;
            CompiledStreamAttempt& operator=(const CompiledStreamAttempt&) = delete;
        };
        thread_local CompiledStreamAttempt* CompiledStreamAttempt::active = nullptr;

        bool IsXnaAbsoluteCleanPath(const std::string& path)
        {
            // Refresh TitleContainer.IsCleanPathAbsolute IL_0000-0056.
            return path.find_first_of(":*?\"<>|") != std::string::npos ||
                   path.starts_with('\\') || path.starts_with("..\\") ||
                   path.find("\\..\\") != std::string::npos ||
                   path.ends_with("\\..") || path == "..";
        }

    }

    // ---------------------------------------------------------------------------
    // Property accessors
    // ---------------------------------------------------------------------------

    const std::string& ContentManager::getRootDirectoryProperty() const
    {
        return rootDirectory_;
    }

    void ContentManager::setRootDirectoryProperty(const std::string& v)
    {
        // XNA Refresh set_RootDirectory IL_000e-0014 dereferences the cache nulled by Dispose.
        if (disposed_)
            throw System::NullReferenceException();
        // Measured XNA refuses even same-root assignment with cached assets; FNA's auto-property does not.
        if (!loadedAssets_.empty())
        {
            throw System::InvalidOperationException("RootDirectory cannot be changed while content is loaded.");
        }
        rootDirectory_ = v;
    }

    void ContentManager::setRootDirectoryProperty(std::string&& v)
    {
        // XNA Refresh set_RootDirectory IL_000e-0014 dereferences the cache nulled by Dispose.
        if (disposed_)
            throw System::NullReferenceException();
        if (!loadedAssets_.empty())
        {
            throw System::InvalidOperationException("RootDirectory cannot be changed while content is loaded.");
        }
        rootDirectory_ = std::move(v);
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
        // XNA Refresh ContentManager::.ctor(IServiceProvider, string) IL_0021-002e.
        if (!serviceProvider)
            throw System::ArgumentNullException("serviceProvider");
        CNA::Content::CnbLoaderRegistry::DeclareBuiltIns();
    }

    ContentManager::ContentManager(System::IServiceProvider* serviceProvider,
                                   const std::string& rootDirectory)
        : serviceProvider_(serviceProvider), rootDirectory_(rootDirectory)
    {
        // XNA Refresh ContentManager::.ctor(IServiceProvider, string) IL_0021-002e.
        if (!serviceProvider)
            throw System::ArgumentNullException("serviceProvider");
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

    Graphics::GraphicsDevice* ContentManager::FindGraphicsDevice() const
    {
        // CNA's injection first: a device set with setGraphicsDevice is used without asking the
        // service provider at all.
        if (graphicsDevice_ != nullptr)
        {
            return graphicsDevice_;
        }
        // Refresh GraphicsContentHelper.GraphicsDeviceFromContentReader IL_0000-003e: the
        // provider's IGraphicsDeviceService, asked again on every call, and its device.
        if (serviceProvider_ == nullptr)
        {
            return nullptr;
        }
        auto* graphicsDeviceService = static_cast<Graphics::IGraphicsDeviceService*>(
            serviceProvider_->GetService(typeid(Graphics::IGraphicsDeviceService)));
        return graphicsDeviceService != nullptr ? graphicsDeviceService->getGraphicsDeviceProperty()
                                                : nullptr;
    }

    Graphics::GraphicsDevice& ContentManager::getGraphicsDeviceInternal() const
    {
        Graphics::GraphicsDevice* graphicsDevice = FindGraphicsDevice();
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
            try
            {
                if (disposing)
                    Unload();
            }
            catch (...)
            {
                loadedAssets_.clear();
                disposableAssets_.clear();
                ++disposableAssetsVersion_;
                disposed_ = true;
                throw;
            }
            loadedAssets_.clear();
            disposableAssets_.clear();
            ++disposableAssetsVersion_;
            disposed_ = true;
        }
    }

    void ContentManager::CloseReadStream(System::IO::Stream& stream, const int closes,
                                         std::exception_ptr readFailure)
    {
        // Refresh ContentManager.ReadAsset<T> IL_0079-0082: the stream is disposed in a finally,
        // and Stream.Dispose() is the virtual Close() (mscorlib Stream.Dispose IL_0001). Once
        // ContentReader.Create has returned, the inner finally disposes the reader first
        // (IL_006d-0076); for an uncompressed container that reader is a BinaryReader over the
        // stream itself (PrepareStream IL_00ed), whose Dispose(true) closes it (IL_0003-0015), so
        // the stream is closed twice. An LZX reader reads a DecompressStream, whose Dispose(bool)
        // leaves its base stream open (IL_0000-002a). Each Close runs even when an earlier one
        // threw, as both finallies do. The owner decided (HND-TQ-35, 2026-11-09) that a throwing
        // Close does not replace a failure already propagating.
        for (int close = 0; close < closes; ++close)
        {
            try
            {
                stream.Close();
            }
            catch (...)
            {
                if (!readFailure)
                {
                    readFailure = std::current_exception();
                }
            }
        }
        if (readFailure)
        {
            std::rethrow_exception(readFailure);
        }
    }

    std::vector<std::uint8_t> ContentManager::ReadStreamToEnd(System::IO::Stream& stream,
                                                              const std::string& assetName,
                                                              const bool canSeek)
    {
        try
        {
            std::vector<std::uint8_t> bytes;
            const auto maxBytes = static_cast<std::size_t>(
                CNA::Internal::Xnb::DefaultXnbReadLimits().maxFileSize);
            // Refresh PrepareStream IL_009f-00b3 asks Length only of a stream that can seek; a
            // stream that cannot may refuse the question (NotSupportedException).
            const auto declaredLength = canSeek ? stream.getLengthProperty() : 0;
            if (declaredLength > 0 &&
                static_cast<std::size_t>(declaredLength) > maxBytes)
            {
                throw ContentLoadException(
                    "ContentManager: asset '" + assetName +
                    "' exceeds the maximum XNB file size (" +
                    std::to_string(maxBytes) + " bytes).");
            }
            std::array<std::uint8_t, 64 * 1024> buffer{};
            for (;;)
            {
                const auto read = stream.Read(buffer.data(), 0,
                                              static_cast<SharpRuntime::intcs>(buffer.size()));
                if (read <= 0)
                {
                    break;
                }
                if (static_cast<std::size_t>(read) > maxBytes - bytes.size())
                {
                    throw ContentLoadException(
                        "ContentManager: asset '" + assetName +
                        "' exceeds the maximum XNB file size (" +
                        std::to_string(maxBytes) + " bytes).");
                }
                bytes.insert(bytes.end(), buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(read));
            }
            return bytes;
        }
        catch (const System::IO::IOException&)
        {
            // Refresh PrepareStream IL_00f1-0106 / ReadAsset IL_0019-002c wrap
            // only IOException with BadXnb; resource text is Error loading "{0}".
            throw ContentLoadException(
                "Error loading \"" + assetName + "\".", std::current_exception());
        }
    }

    void ContentManager::RequireOpenedStream(const System::IO::Stream* stream)
    {
        // Refresh ContentManager.ReadAsset<T> IL_0027-0033 passes a null OpenStream() result to
        // ContentReader.Create, whose PrepareStream IL_0000-0001 wraps it in a BinaryReader;
        // mscorlib's BinaryReader..ctor(Stream, Encoding) IL_0006-0013 throws
        // ArgumentNullException("input"). PrepareStream catches only IOException (IL_00f1), and
        // the finally that disposes the stream skips a null one (ReadAsset IL_0079-007a).
        if (stream == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
    }

    std::unique_ptr<System::IO::Stream> ContentManager::TryOpenCompiledStream(const std::string& assetName)
    {
        CompiledStreamAttempt attempt(this, assetName);
        try
        {
            auto stream = OpenStream(assetName);
            RequireOpenedStream(stream.get());
            return stream;
        }
        catch (const ContentLoadException& error)
        {
            if (attempt.missingCause &&
                error.getInnerExceptionProperty() == attempt.missingCause &&
                error.getMessageProperty() == attempt.missingMessage)
                return nullptr;
            throw;
        }
    }

    std::unique_ptr<System::IO::Stream> ContentManager::OpenStream(const std::string& assetName)
    {
        // Refresh OpenStream IL_001d-0039 routes using the root's classification.
        // Retain CNA packaged-file resolution; enforce TitleContainer's refusal
        // before an absolute asset could replace the relative root in BuildAssetPath.
        const std::string cleanRoot = CleanAssetName(rootDirectory_);
        std::vector<std::uint8_t> bytes;
        std::string path;
        try
        {
            if (!IsXnaAbsoluteCleanPath(cleanRoot))
            {
                std::string combined;
                const std::string cleanAsset = CleanAssetName(assetName);
                if (cleanAsset.starts_with('\\')) combined = cleanAsset + ".xnb";
                else combined = cleanRoot + (cleanRoot.empty() ? "" : "\\") + assetName + ".xnb";
                if (IsXnaAbsoluteCleanPath(CleanAssetName(std::move(combined))))
                    throw System::ArgumentException("The title file name must be a valid relative path.");
            }
            path = ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb");
            // TryReadAssetBytes, not a plain file open: it is the resolution the rest of this
            // manager uses, and it covers the platform's packaged assets and their case-insensitive
            // matching as well as the filesystem. An asset served from a package is as much an
            // asset as one on disk, and a subclass overriding this method inherits neither concern.
            if (!TryReadAssetBytes(
                    path, bytes,
                    static_cast<std::size_t>(
                        CNA::Internal::Xnb::DefaultXnbReadLimits().maxFileSize)))
            {
                // Refresh OpenStream IL_003d-006e: missing-file cause, separate Message.
                auto cause = std::make_exception_ptr(System::IO::FileNotFoundException("Unable to find the specified file.", path));
                // Refresh OpenStream IL_0025-002a uses FileStream for an absolute
                // root: distinguish a missing directory from a missing leaf. A
                // relative-root TitleContainer failure is FileNotFoundException
                // even when its underlying FileStream had a missing directory
                // (TitleContainer.OpenStream IL_0075-00ae).
                const auto nativeRoot = CNA::Internal::TryPathFromUtf8(rootDirectory_);
                const auto nativePath = CNA::Internal::TryPathFromUtf8(path);
                if (nativeRoot && nativeRoot->is_absolute() && nativePath)
                {
                    std::error_code error;
                    const bool parentExists = std::filesystem::is_directory(nativePath->parent_path(), error);
                    if (!parentExists && (!error || error == std::errc::no_such_file_or_directory ||
                                          error == std::errc::not_a_directory))
                        cause = std::make_exception_ptr(System::IO::DirectoryNotFoundException(
                            "Could not find a part of the path."));
                }
                ContentLoadException missing(
                    "Error loading \"" + assetName + "\". File not found.", cause);
                auto* attempt = CompiledStreamAttempt::active;
                if (attempt && attempt->manager == this && attempt->assetName == assetName)
                {
                    attempt->missingCause = cause;
                    attempt->missingMessage = missing.getMessageProperty();
                }
                throw missing;
            }
        }
        catch (const ContentLoadException&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            // XNA reports a missing file and any other open failure the same way: a
            // ContentLoadException naming the asset, carrying the original error as its cause.
            throw ContentLoadException(
                "Error loading \"" + assetName + "\". Cannot open file.",
                std::current_exception());
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

    void ContentManager::RecordDisposableObject(std::shared_ptr<System::IDisposable> disposable)
    {
        // XNA 4.0 ContentManager.RecordDisposableObject appends each materialization,
        // including repeated references. Its Unload finally block clears even after failure.
        disposableAssets_.push_back(std::move(disposable));
        ++disposableAssetsVersion_;
    }

    void ContentManager::Unload()
    {
        if (disposed_)
            // XNA Refresh Load/ReadAsset/Unload IL_0008-0013: base Object.ToString().
            throw System::ObjectDisposedException("Microsoft.Xna.Framework.Content.ContentManager");
        try
        {
            const auto version = disposableAssetsVersion_;
            const auto count = disposableAssets_.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                // User disposal may load more content. Keep this object alive without
                // retaining a vector iterator across that callback, and match List's
                // invalidated-enumerator exception instead of accessing stale storage.
                auto disposable = disposableAssets_[i];
                disposable->Dispose();
                if (version != disposableAssetsVersion_)
                    throw System::InvalidOperationException("Content disposal records changed during Unload.");
            }
        }
        catch (...)
        {
            loadedAssets_.clear();
            disposableAssets_.clear();
            ++disposableAssetsVersion_;
            throw;
        }
        loadedAssets_.clear();
        disposableAssets_.clear();
        ++disposableAssetsVersion_;
    }

    // ---------------------------------------------------------------------------
    // Content manifest (XNB-65/65A/66/67/61a)
    // ---------------------------------------------------------------------------

    std::vector<std::string> ContentManager::ScanXnbReaderNames(const std::filesystem::path& xnbPath) const
    {
        std::vector<std::string> names;
        try
        {
            std::ifstream file(xnbPath, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return names;
            }
            const std::streamsize length = file.tellg();
            const auto maxBytes = static_cast<std::uintmax_t>(
                CNA::Internal::Xnb::DefaultXnbReadLimits().maxFileSize);
            if (length < 0 || static_cast<std::uintmax_t>(length) > maxBytes)
            {
                return names;
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
            file.seekg(0, std::ios::beg);
            if (length > 0 &&
                !file.read(reinterpret_cast<char*>(bytes.data()), length))
            {
                return names;
            }

            const std::string origin = CNA::Internal::PathToUtf8(xnbPath);
            const auto container = CNA::Internal::Xnb::DecodeXnbContainerBody(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), origin);
            const auto bodyStream = container.OpenBodyStream();
            System::IO::BinaryReader bodyReader(bodyStream.get(), true);
            const auto table = CNA::Internal::Xnb::ParseXnbTypeReaderTable(
                bodyReader, origin);
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
            const fs::path& path = it->path();
            const fs::path filename = path.filename();
            // The build tool owns this exact intermediate namespace. Do not exclude
            // unrelated hidden assets or directories with merely similar prefixes.
            if (filename == ".cna-contentproj" && it->is_directory(ec) && !ec)
            {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec) || ec)
            {
                continue;
            }
            if (filename == ".cna-content.lock" ||
                filename == CNA::Content::Pipeline::ContentBuildManifestFileName)
            {
                continue;
            }
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
        const auto root = CNA::Internal::TryPathFromUtf8(rootDirectory_);
        if (title.empty() && root && !root->empty() &&
            !root->is_absolute() && !root->has_root_name())
        {
            // A relative root must not depend on whether a Game or TitleLocation was
            // consulted first. Absolute/empty roots need no ambient platform at all.
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
        // Preserve XNA backslashes in reader names; convert only the host file path.
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
        const std::string& path,
        std::vector<std::uint8_t>& bytes,
        const std::size_t maxBytes) const
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
            const bool loaded = CNA::Platform::GetCurrentPlatform().GetFileSystem()
                ->TryLoadFileIgnoringCase(path, bytes);
            if (loaded && bytes.size() > maxBytes)
            {
                bytes.clear();
                throw ContentLoadException(
                    "ContentManager: '" + path + "' exceeds its maximum load size.");
            }
            return loaded;
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
            if (static_cast<std::uintmax_t>(length) >
                static_cast<std::uintmax_t>(maxBytes))
            {
                throw ContentLoadException(
                    "ContentManager: '" + path + "' exceeds its maximum load size.");
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

        const bool loaded = CNA::Platform::GetCurrentPlatform().GetFileSystem()
            ->TryLoadFileIgnoringCase(path, bytes);
        if (loaded && bytes.size() > maxBytes)
        {
            bytes.clear();
            throw ContentLoadException(
                "ContentManager: '" + path + "' exceeds its maximum load size.");
        }
        return loaded;
    }

    namespace
    {
        // XNA TitleContainer.CollapseParentDirectory, with '/' as the separator: removes the
        // segment before the "/.." found at `position` together with it.
        std::size_t CollapseParentDirectory(std::string& path, const std::size_t position,
                                            const std::size_t removeLength)
        {
            const std::size_t previous = path.rfind('/', position - 1);
            const std::size_t start = previous == std::string::npos ? 0 : previous + 1;
            path.erase(start, position - start + removeLength);
            return start > 1 ? start - 1 : 1;
        }

        // XNA TitleContainer.GetCleanPath, which ContentManager.Load<T> applies to the asset name
        // before its cache lookup, with '/' as the separator. Kept step for step, quirks included
        // (one non-overlapping "/./" pass; a leading "../" is left alone).
        std::string CleanAssetPath(std::string path)
        {
            for (std::size_t at = path.find("/./"); at != std::string::npos;
                 at = path.find("/./", at + 1))
            {
                path.replace(at, 3, "/");
            }
            while (path.starts_with("./"))
            {
                path.erase(0, 2);
            }
            while (path.ends_with("/."))
            {
                path = path.size() > 2 ? path.substr(0, path.size() - 2) : std::string("/");
            }
            for (std::size_t position = 1; position < path.size();)
            {
                position = path.find("/../", position);
                if (position == std::string::npos)
                {
                    break;
                }
                position = CollapseParentDirectory(path, position, 4);
            }
            if (path.ends_with("/..") && path.size() > 3)
            {
                CollapseParentDirectory(path, path.size() - 3, 3);
            }
            if (path == ".")
            {
                path.clear();
            }
            return path;
        }
    }

    ContentManager::XnbReadScope::XnbReadScope(ContentManager& manager, const std::string& assetName)
        : manager_(manager)
    {
        std::string key = manager.NormalizeKey(assetName);
        const auto& inProgress = manager.xnbReadsInProgress_;
        if (std::find(inProgress.begin(), inProgress.end(), key) != inProgress.end())
        {
            throw ContentLoadException(
                "ContentManager: '" + assetName + "' is already being read; its external "
                "references form a cycle, which is refused rather than followed.");
        }
        manager.xnbReadsInProgress_.push_back(std::move(key));
    }

    ContentManager::XnbReadScope::~XnbReadScope()
    {
        manager_.xnbReadsInProgress_.pop_back();
    }

    std::string ContentManager::CleanAssetName(std::string assetName)
    {
        // Refresh Load<T> IL_0027-002d forwards GetCleanPath to ReadAsset.
        // GetCleanPath IL_0000-000a maps '/' to '\\': readers and OpenStream
        // overrides see XNA's logical spelling. BuildAssetPath converts the file path.
        std::replace(assetName.begin(), assetName.end(), '\\', '/');
        assetName = CleanAssetPath(std::move(assetName));
        std::replace(assetName.begin(), assetName.end(), '/', '\\');
        return assetName;
    }

    std::string ContentManager::NormalizeKey(const std::string& assetName) const
    {
        std::string key = CleanAssetName(assetName);
        // Preserve the private cache-key spelling, separate from reader-visible names.
        std::replace(key.begin(), key.end(), '\\', '/');
        // Refresh .ctor IL_0001-000b: OrdinalIgnoreCase; Load IL_0027-0038:
        // GetCleanPath before lookup. Case conversion never changes reader names.
        // Use sharp-runtime's character rule for equality, hash and cycle keys.
        std::string folded;
        folded.reserve(key.size());
        for (std::size_t i = 0; i < key.size();)
        {
            std::uint32_t scalar = 0;
            std::size_t length = 1;
            if (!System::detail::TryDecodeUtf8Scalar(key, i, scalar, length))
            {
                folded.push_back(key[i++]);
                continue;
            }
            i += length;
            scalar = System::detail::OrdinalUpper(scalar);
            // Keep the CNAEXT diagnostic key's established lowercase ASCII spelling.
            if (scalar >= 'A' && scalar <= 'Z') scalar += 'a' - 'A';
            System::detail::AppendUtf8Scalar(folded, scalar);
        }
        return folded;
    }

    void ContentManager::RegisterBuiltinCnbLoader(std::uint32_t assetTypeId,
                                                  const std::string& canonicalTypeName,
                                                  CNA::Content::CnbLoaderRegistry::LoaderFn loader)
    {
        CNA::Content::CnbLoaderRegistry::RegisterDeclaredBuiltIn(assetTypeId, canonicalTypeName,
                                                                 std::move(loader));
    }
}
