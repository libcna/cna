// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "CNA/Logger.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManifestEntry.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/LooseFileContentTypeReader.hpp"
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

namespace Microsoft::Xna::Framework::Audio  { class SoundEffect; }
namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; class Texture2D; class TextureCube; }
namespace CNA::Internal::Renderers { class ITextureRenderer; }

namespace Microsoft::Xna::Framework::Content
{
    using log = CNA::Logger;

    /**
     * @brief Content manager with extensible type reader support and asset caching.
     *
     * Supports loading assets by type. Custom types are registered via RegisterTypeReader<T>().
     * Built-in loaders (Texture2D, SoundEffect, Effect) are registered in the constructor.
     */
    class ContentManager : public System::IDisposable
    {
    private:
        std::string rootDirectory_ = "Content";
        Graphics::GraphicsDevice* graphicsDevice_ = nullptr;
        System::IServiceProvider* serviceProvider_ = nullptr;
        bool disposed_ = false;

        // plans/plan_cnj.md CNB-36: keyed by (T's type_index, normalized logical name), not name
        // alone -- otherwise a second Load<T2>() for a logical name a different T1 already
        // cached under would std::any_cast<T2> a std::any actually holding T1, throwing
        // std::bad_any_cast (an unrelated, undocumented exception type) instead of reaching the
        // normal .cnj envelope validation that would otherwise produce a clear
        // ContentLoadException naming both types.
        struct AssetCacheKey
        {
            std::type_index typeIndex;
            std::string normalizedName;

            bool operator==(const AssetCacheKey& other) const
            {
                return typeIndex == other.typeIndex && normalizedName == other.normalizedName;
            }
        };

        struct AssetCacheKeyHash
        {
            std::size_t operator()(const AssetCacheKey& k) const
            {
                return std::hash<std::type_index>()(k.typeIndex) ^
                       (std::hash<std::string>()(k.normalizedName) << 1);
            }
        };

        std::unordered_map<AssetCacheKey, std::any, AssetCacheKeyHash> loadedAssets_;
        std::unordered_map<std::type_index, std::any> typeReaders_;

        // plans/plan_cnj.md CNB-24: per-C++-type table of named .cnj loaders, keyed first by the
        // requested type T (std::type_index), then by the .cnj document's own "type" string.
        // Populated by RegisterCnjLoader<T>(); consulted by GenericCnjTypeReader<T> below.
        std::unordered_map<std::type_index, std::unordered_map<std::string, std::any>> cnjNamedLoaders_;

        struct WeakTextureEntry {
            std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> renderer;
            std::weak_ptr<std::vector<uint8_t>> cpuPixels;
            Graphics::SurfaceFormat fmt;
            int levelCount;
        };
        std::unordered_map<std::string, WeakTextureEntry> textureCache_;

        // plans/plan_xnb.md Phase B3 (XNB-65/66/67): a point-in-time snapshot of the content root,
        // built lazily on first access (or explicitly via RefreshContentManifest()). Additive
        // only in this pass -- NOT yet consulted by ResolveAssetPath()/Load<T>()'s own
        // exists()-based resolution, which keeps its existing live-filesystem-check behavior
        // unchanged. Wiring the manifest into that hot path is deliberately deferred to a
        // separate, isolated follow-up task, so as not to risk the very large existing test
        // surface that depends on ContentManager noticing a file the instant it's written.
        std::vector<ContentManifestEntry> contentManifest_;
        bool contentManifestBuilt_ = false;

        [[nodiscard]] std::vector<std::string> ScanXnbReaderNames(const std::filesystem::path& xnbPath) const;

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret1, ref1, constmet1)

        [[nodiscard]] std::string BuildAssetPath(const std::string& assetName) const;
        [[nodiscard]] std::string ResolveExistingAssetPath(const std::string& path) const;
        [[nodiscard]] std::string NormalizeKey(const std::string& assetName) const;

        void RegisterBuiltinLoaders();

    public:
        /**
         * @brief Constructs a ContentManager with the given service provider.
         *
         * @param serviceProvider Service provider used to resolve graphics and other services.
         */
        explicit ContentManager(System::IServiceProvider* serviceProvider);

        /**
         * @brief Constructs a ContentManager with the given service provider and root directory.
         *
         * @param serviceProvider Service provider used to resolve graphics and other services.
         * @param rootDirectory   Root path prepended to all asset names.
         */
        ContentManager(System::IServiceProvider* serviceProvider,
                       const std::string& rootDirectory);

        /** @brief Constructs a ContentManager with a default root directory of "Content". */
        CNAEXT ContentManager();

        /** @brief Destroys the content manager and releases all loaded assets. */
        ~ContentManager() override = default;

        /** @brief Releases all resources used by this content manager. */
        void Dispose() override;

        /**
         * @brief Gets the service provider associated with this content manager.
         *
         * @return Pointer to the IServiceProvider, or nullptr if none was provided.
         */
        [[nodiscard]] System::IServiceProvider* getServiceProviderProperty() const;

        /**
         * @brief Sets the graphics device used when loading GPU resources such as textures.
         *
         * @param graphicsDevice The graphics device to associate with this manager.
         */
        void setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice);

        /**
         * @brief Returns the graphics device associated with this content manager.
         *
         * @return Reference to the associated GraphicsDevice.
         */
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceInternal() const;

        /** @brief Unloads all cached assets and frees the associated resources. */
        void Unload();

        /**
         * @brief CNAEXT: (re)scans the content root and rebuilds the content manifest
         *        (plans/plan_xnb.md XNB-65/65A), replacing any previous scan. Not called
         *        automatically after construction -- the first call to GetContentManifest()/
         *        GetXnbReaderUsageSummary() triggers it lazily if it hasn't run yet.
         *
         * The manifest is a point-in-time snapshot: a file added to the content root after this
         * call is not reflected until RefreshContentManifest() runs again. There is no
         * filesystem-watch/hot-reload mechanism.
         */
        CNAEXT void RefreshContentManifest();

        /**
         * @brief CNAEXT: returns the content manifest (plans/plan_xnb.md XNB-66), one entry per logical
         *        asset name found under the content root, building it via RefreshContentManifest()
         *        first if it hasn't been built yet.
         *
         * @return The current manifest snapshot.
         */
        CNAEXT [[nodiscard]] const std::vector<ContentManifestEntry>& GetContentManifest();

        /**
         * @brief CNAEXT: aggregates the manifest's per-file `.xnb` reader-name inventories
         *        (plans/plan_xnb.md XNB-67) into one row per distinct reader name -- how many files
         *        reference it, and whether `ContentTypeReaderManager` currently has a reader
         *        registered for it. Builds the manifest first via GetContentManifest() if needed.
         *
         * @return One ContentManifestReaderUsage row per distinct reader name found, unordered.
         */
        CNAEXT [[nodiscard]] std::vector<ContentManifestReaderUsage> GetXnbReaderUsageSummary();

        /**
         * @brief Registers a custom type reader for assets of type T.
         *
         * @tparam T     Asset type this reader produces.
         * @param reader Unique pointer to the type reader to register.
         */
        template <typename T>
        void RegisterTypeReader(std::unique_ptr<LooseFileContentTypeReader<T>> reader)
        {
            typeReaders_[std::type_index(typeid(T))] =
                std::shared_ptr<LooseFileContentTypeReader<T>>(std::move(reader));
        }

        /**
         * @brief Factory signature for a game-registered named .cnj loader (see
         *        RegisterCnjLoader()).
         *
         * Receives the raw .cnj JSON document text and the owning ContentManager (for
         * recursively loading any files it references), and returns a constructed T.
         */
        template <typename T>
        CNAEXT using CnjLoaderFn = std::function<T(const std::string& cnjJson, ContentManager& cm)>;

        /**
         * @brief Registers a named .cnj loader for asset type T, selected by the .cnj
         *        document's own "type" field rather than by T alone.
         *
         * Unlike RegisterTypeReader<T>() (one reader per T, fixed at the Load<T>() call site),
         * multiple differently-named .cnj "type" values can each register their own factory
         * here, all producing the same T -- e.g. a game's "EnemyDefinition" and "LootTable" .cnj
         * types both deserializing into the same generic data struct via two different
         * factories (see cnj.md's "Custom loaders" section). Only applies to a T with no
         * existing reader already registered (built-in or via RegisterTypeReader<T>()); throws
         * immediately if one already exists, since it would never be consulted by that reader.
         *
         * Registration is deterministic and fails fast (plans/plan_cnj.md CNB-37): an empty
         * @p typeName or an empty @p factory is rejected immediately, and re-registering an
         * already-used `(T, typeName)` pair throws rather than silently replacing the earlier
         * factory -- two *different* `typeName`s for the same `T` remain fully supported (that
         * is the feature's whole point); only an exact repeat is rejected.
         *
         * @tparam T       Asset type the factory produces.
         * @param typeName The .cnj document's "type" string this factory handles. Must not be
         *                 empty.
         * @param factory  Callback invoked with the raw .cnj JSON text and this ContentManager.
         *                 Must not be empty.
         * @throws std::invalid_argument if @p typeName or @p factory is empty.
         * @throws std::logic_error if a reader is already registered for T, or if @p typeName is
         *         already registered for T.
         */
        template <typename T>
        CNAEXT void RegisterCnjLoader(const std::string& typeName, CnjLoaderFn<T> factory)
        {
            if (typeName.empty())
            {
                throw std::invalid_argument(
                    "ContentManager::RegisterCnjLoader<T>(): typeName must not be empty.");
            }
            if (!factory)
            {
                throw std::invalid_argument(
                    "ContentManager::RegisterCnjLoader<T>(): factory must not be empty.");
            }

            const auto ti = std::type_index(typeid(T));
            const bool alreadyOwnedByAnotherReader =
                typeReaders_.find(ti) != typeReaders_.end() &&
                cnjNamedLoaders_.find(ti) == cnjNamedLoaders_.end();
            if (alreadyOwnedByAnotherReader)
            {
                throw std::logic_error(
                    "ContentManager::RegisterCnjLoader<T>(): a reader is already registered for "
                    "this type; RegisterCnjLoader only applies to a type with no existing reader, "
                    "since an existing reader would never consult this table.");
            }

            auto& innerMap = cnjNamedLoaders_[ti];
            if (innerMap.find(typeName) != innerMap.end())
            {
                throw std::logic_error(
                    "ContentManager::RegisterCnjLoader<T>(): '" + typeName + "' is already "
                    "registered for this type; RegisterCnjLoader never silently replaces an "
                    "existing factory.");
            }

            const bool firstForThisType = innerMap.empty();
            innerMap[typeName] = std::move(factory);

            if (firstForThisType)
            {
                RegisterTypeReader<T>(std::make_unique<GenericCnjTypeReader<T>>());
            }
        }

        /**
         * @brief Loads an asset of type T from the content root.
         *
         * Results are cached — subsequent calls with the same asset name return the
         * already-loaded instance. If assetName has no file extension, each extension
         * returned by the registered reader's GetExtensions() is tried in order.
         *
         * @tparam T Asset type.
         * @param assetName Relative file path inside the content root (with or without extension).
         * @return Loaded asset instance.
         * @throws ContentLoadException if the asset cannot be loaded.
         */
        template <typename T>
        [[nodiscard]] T Load(const std::string& assetName)
        {
            if (disposed_)
            {
                throw std::runtime_error("ContentManager has been disposed.");
            }

            const std::string key = NormalizeKey(assetName);
            const AssetCacheKey cacheKey{std::type_index(typeid(T)), key};
            log::Debug(std::string("Loading asset: ") + assetName);

            auto cacheIt = loadedAssets_.find(cacheKey);
            if (cacheIt != loadedAssets_.end())
            {
                return std::any_cast<T>(cacheIt->second);
            }

            // .xnb always wins first (cnj.md's "Core rule", 2026-07-16 decision): checked even
            // ahead of a literal caller-given path or a registered LooseFileContentTypeReader<T>
            // (a real compiled .xnb asset represents authentic external content that should never
            // be silently shadowed by CNA's own loose-file/.cnj conveniences). Unlike the
            // loose-file path, .xnb dispatch needs no per-T reader registered on ContentManager
            // at all -- root-object dispatch is entirely driven by the file's own type-reader
            // table via the process-wide ContentTypeReaderManager registry (plans/plan_xnb.md XNB-17B).
            const std::string xnbCandidate =
                ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb");
            if (std::filesystem::exists(xnbCandidate))
            {
                T result = LoadXnbAsset<T>(xnbCandidate, assetName);
                loadedAssets_[cacheKey] = result;
                return result;
            }

            auto readerIt = typeReaders_.find(std::type_index(typeid(T)));
            if (readerIt == typeReaders_.end())
            {
                throw ContentLoadException(
                    std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                    + assetName + "'.");
            }

            auto* readerPtr = std::any_cast<std::shared_ptr<LooseFileContentTypeReader<T>>>(&readerIt->second);
            if (!readerPtr || !*readerPtr)
            {
                throw ContentLoadException(
                    std::string("ContentManager::Load<T>(): Reader is null for asset '")
                    + assetName + "'.");
            }

            LooseFileContentTypeReader<T>& reader = **readerPtr;
            const std::string resolvedPath = ResolveAssetPath(assetName, reader);

            T result = reader.Read(resolvedPath, *this);
            loadedAssets_[cacheKey] = result;
            return result;
        }

    private:
        // Generic reader for game-registered .cnj "type" values that don't have a dedicated
        // LooseFileContentTypeReader<T>. Looks up the .cnj envelope's "type" field in cnjNamedLoaders_
        // and invokes whichever RegisterCnjLoader<T>()-registered factory matches (cnj.md's
        // "Custom loaders" section). Auto-registered by RegisterCnjLoader<T>() the first time
        // it's called for a T with no existing reader; never auto-registered for a T that
        // already has a built-in or otherwise-registered reader.
        template <typename T>
        class GenericCnjTypeReader : public LooseFileContentTypeReader<T>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            T Read(const std::string& path, ContentManager& cm) override
            {
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    throw ContentLoadException("Cannot open file: " + path);
                }
                std::ostringstream ss;
                ss << file.rdbuf();
                const std::string json = ss.str();

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelopeBaseline(envelope, path);

                auto outerIt = cm.cnjNamedLoaders_.find(std::type_index(typeid(T)));
                if (outerIt != cm.cnjNamedLoaders_.end())
                {
                    auto innerIt = outerIt->second.find(envelope.type);
                    if (innerIt != outerIt->second.end())
                    {
                        auto* factory = std::any_cast<CnjLoaderFn<T>>(&innerIt->second);
                        if (factory && *factory)
                        {
                            return (*factory)(json, cm);
                        }
                    }
                }

                throw ContentLoadException(
                    "ContentManager: '" + path + "' has unrecognized .cnj type '" +
                    envelope.type + "'.");
            }
        };

        /**
         * @brief Loads @p assetName as a real `.xnb` binary asset from @p xnbPath
         *        (plans/plan_xnb.md XNB-17B), via ContentReader's root-object dispatch. LZX-compressed
         *        files (plans/plan_xnb.md XNB-28/29) are decompressed first.
         *
         * @tparam T        Requested asset type; must match (via `std::any_cast`) whatever the
         *                  file's root type-reader actually produces.
         * @param xnbPath   Full filesystem path to the `.xnb` file.
         * @param assetName Logical asset name, passed through to ContentReader for diagnostics.
         * @return The deserialized root asset.
         * @throws ContentLoadException if the file is malformed, decompression fails, or names
         *         an unregistered/version-mismatched reader.
         */
        template <typename T>
        [[nodiscard]] T LoadXnbAsset(const std::string& xnbPath, const std::string& assetName)
        {
            std::ifstream file(xnbPath, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException("ContentManager: cannot open '" + xnbPath + "'.");
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            const std::string bytes = ss.str();

            System::IO::MemoryStream headerStream(
                reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<int32_t>(bytes.size()));
            System::IO::BinaryReader headerReader(&headerStream, true);
            const auto header = CNA::Internal::Xnb::ParseXnbHeader(headerReader, xnbPath);

            // header.totalLength is a value the FILE ITSELF declares, not something ParseXnbHeader
            // can verify against the real file size on its own -- cross-check it against the
            // actual number of bytes just read from disk before it's used for any pointer
            // arithmetic below (plans/plan_xnb.md XNB-43). A file claiming more bytes than it actually
            // has (truncated, or an adversarial totalLength) would otherwise let the Lzx branch's
            // compressedSize computation read past the end of `bytes`.
            if (header.totalLength < 10 || static_cast<std::size_t>(header.totalLength) > bytes.size())
            {
                throw ContentLoadException(
                    "'" + xnbPath + "' declares a totalLength (" + std::to_string(header.totalLength) +
                    ") inconsistent with its actual file size (" + std::to_string(bytes.size()) + ").");
            }

            switch (header.compression)
            {
                case CNA::Internal::Xnb::XnbCompression::None:
                {
                    System::IO::MemoryStream bodyStream(
                        reinterpret_cast<const uint8_t*>(bytes.data()) + 10,
                        static_cast<int32_t>(bytes.size()) - 10);
                    ContentReader contentReader(this, &bodyStream, assetName, header.version, header.platform);
                    return contentReader.ReadAsset<T>();
                }
                case CNA::Internal::Xnb::XnbCompression::Lzx:
                {
                    System::IO::MemoryStream sizeStream(
                        reinterpret_cast<const uint8_t*>(bytes.data()) + 10, 4);
                    System::IO::BinaryReader sizeReader(&sizeStream, true);
                    const int32_t decompressedSize = sizeReader.ReadInt32();
                    const int32_t compressedSize = header.totalLength - 14;

                    const auto decompressed = CNA::Internal::Xnb::DecompressXnbPayload(
                        reinterpret_cast<const uint8_t*>(bytes.data()) + 14,
                        compressedSize, decompressedSize, xnbPath);

                    System::IO::MemoryStream bodyStream(decompressed.data(), static_cast<int32_t>(decompressed.size()));
                    ContentReader contentReader(this, &bodyStream, assetName, header.version, header.platform);
                    return contentReader.ReadAsset<T>();
                }
                case CNA::Internal::Xnb::XnbCompression::Lz4:
                    throw ContentLoadException(
                        "'" + xnbPath + "' uses MonoGame's Lz4 compression, which CNA does not yet "
                        "support (plans/plan_xnb.md XNB-30C).");
                case CNA::Internal::Xnb::XnbCompression::Unknown:
                default:
                    throw ContentLoadException(
                        "'" + xnbPath + "' has an unrecognized compression flag combination.");
            }
        }

        /**
         * @brief Resolves the full filesystem path for an asset, trying reader extensions
         *        when the literal asset path does not exist.
         *
         * @tparam T       Asset type.
         * @param assetName Relative asset name.
         * @param reader    Type reader whose extensions are tried.
         * @return Full resolved filesystem path.
         */
        template <typename T>
        [[nodiscard]] std::string ResolveAssetPath(
            const std::string& assetName,
            LooseFileContentTypeReader<T>& reader) const
        {
            const std::string base = BuildAssetPath(assetName);

            // If the literal path already exists, use it as-is. This covers
            // assetName with an explicit, correct extension. Checking
            // existence rather than std::filesystem::path::has_extension()
            // matters because asset names can legitimately contain a '.'
            // that is not a file extension (e.g. localized names like
            // "Flag.en-US"), which has_extension() would otherwise
            // misinterpret as already-resolved and never try appending
            // a reader extension.
            const std::string literalPath = ResolveExistingAssetPath(base);
            if (std::filesystem::exists(literalPath))
            {
                return literalPath;
            }

            // .cnj is always tried before any native/reader-declared extension (cnj.md's "core
            // rule" -- when a .cnj sidecar is present, it always has final say over how an asset
            // name resolves, even if a native file with the same name also exists). This makes
            // .cnj usable as an optional metadata sidecar (plans/plan_cnj.md CNB-4), not just a
            // mutually-exclusive alternative to a native file.
            const std::string cnjCandidate = ResolveExistingAssetPath(base + ".cnj");
            if (std::filesystem::exists(cnjCandidate))
            {
                return cnjCandidate;
            }

            // Try each extension declared by the reader.
            const auto extensions = reader.GetExtensions();
            for (const auto& ext : extensions)
            {
                const std::string candidate = ResolveExistingAssetPath(base + ext);
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }

            // Fall back to bare path (reader may handle the extension itself).
            return base;
        }
    };

    // Explicit specialisation: Texture2D assets use a weak cache so that the
    // GPU renderer is freed as soon as the last external Texture2D copy is dropped,
    // preventing per-world RAM growth when worlds load unique background textures.
    template<>
    Graphics::Texture2D ContentManager::Load<Graphics::Texture2D>(const std::string& assetName);

    // Explicit specialisation: SoundEffect is move-only with per-owner Dispose-cascade
    // semantics (T-3G) -- sharing one cached instance across unrelated Load<SoundEffect>()
    // call sites would let disposing one caller's copy silently cascade-stop another,
    // unrelated caller's still-playing instances. Each call gets its own independently-owned
    // SoundEffect instead; the generic loadedAssets_ any-cache (which requires T to be
    // CopyConstructible) is skipped entirely for this type.
    template<>
    Audio::SoundEffect ContentManager::Load<Audio::SoundEffect>(const std::string& assetName);

    // Explicit specialisation: TextureCube is move-only (CNAEXT, copy constructor deleted --
    // unlike Texture2D, which supports reference-counted renderer sharing via its own weak-cache
    // specialisation above), so it cannot be held in the generic strong (std::any-based) cache
    // either. Mirrors SoundEffect's own identical not-cached specialisation: each call gets its
    // own independently-decoded instance.
    template<>
    Graphics::TextureCube ContentManager::Load<Graphics::TextureCube>(const std::string& assetName);
}
