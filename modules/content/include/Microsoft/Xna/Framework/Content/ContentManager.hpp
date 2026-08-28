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
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
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
        std::string rootDirectory_;
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
         * @brief Factory signature for a game-registered `.cnb` loader
         *        (see RegisterCnbLoaderEXT()).
         *
         * Receives the already-validated `.cnb` container and the owning ContentManager (for
         * resolving the file's external references through the normal cache), and returns a
         * constructed T.
         */
        template <typename T>
        CNAEXT using CnbLoaderFn =
            std::function<T(const CNA::Content::Cnb::CnbDocument& document, ContentManager& cm)>;

        /**
         * @brief CNAEXT: registers a loader for a game-defined `.cnb` asset type
         *        (plans/plan_cnb.md `CNBF-082`).
         *
         * CNB's extension model is deliberately much smaller than XNB's: a `.cnb` header carries
         * one `u32` asset type identifier, and this call says which C++ type that identifier
         * decodes to. Mint the identifier once with
         * `CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Level")` and use the same value in
         * the tool that writes the file.
         *
         * Registration is process-wide (it outlives this ContentManager) and shared with every
         * other ContentManager, matching `ContentTypeReaderManager`'s behaviour for `.xnb`.
         *
         * @tparam T            The asset type @p factory produces; must be exactly the `T` later
         *                      passed to `Load<T>()`.
         * For a custom identifier @p canonicalTypeName is **not** a diagnostic label: it must be
         * exactly the string passed to `CnbAssetTypeIdFromName()`, and the load path compares it
         * against the name the file itself carries before dispatching. That is what stops two game
         * types whose 31-bit hashes collide from decoding each other's content.
         *
         * **The identifier must be in the custom range** (`>= 0x80000000`), which is what
         * `CnbAssetTypeIdFromName()` mints (plans/plan_cnb.md `CNBF-119`). A game extension has no
         * business claiming `Texture2D`'s identifier or one of the range CNA has reserved for its
         * own future types: the built-in loaders are installed by `ContentManager` itself, and
         * whether a game's factory or CNA's ended up in the table would have depended on which ran
         * first. Registering a built-in or reserved identifier is refused here rather than
         * silently ignored, because "accepted and had no effect" is the shape of this mistake that
         * is hardest to find.
         *
         * There is also no way *around* this call: `CnbLoaderRegistry::Register()` applies the
         * same rule, and CNA's own built-in route is private to `CnbLoaderRegistry` and reachable
         * only by `ContentManager` (plans/plan_cnb.md `CNBF-122`).
         *
         * @param assetTypeId       The identifier written into the `.cnb` header. Must be a custom
         *                          identifier, `CnbAssetTypeId::CustomRangeFirst` or above.
         * @param canonicalTypeName The type's canonical name; must hash to @p assetTypeId.
         * @param factory           Decodes the container into a T. Must not be empty.
         * @throws std::invalid_argument if @p assetTypeId is not a custom identifier, if
         *         @p canonicalTypeName or @p factory is invalid, or if @p canonicalTypeName does
         *         not hash to @p assetTypeId.
         * @throws std::logic_error if @p assetTypeId is already registered under a different name.
         */
        template <typename T>
        CNAEXT static void RegisterCnbLoaderEXT(std::uint32_t assetTypeId,
                                                 const std::string& canonicalTypeName,
                                                 CnbLoaderFn<T> factory)
        {
            if (!factory)
            {
                throw std::invalid_argument(
                    "ContentManager::RegisterCnbLoaderEXT<T>(): factory must not be empty.");
            }
            // The custom-range rule itself is enforced once, by CnbLoaderRegistry::Register(),
            // which is the game-extension route and accepts nothing else -- there is deliberately
            // no second copy of it here (plans/plan_cnb.md `CNBF-119`, `CNBF-122`).
            CNA::Content::CnbLoaderRegistry::Register(
                assetTypeId, canonicalTypeName,
                [factory](const CNA::Content::Cnb::CnbDocument& document, ContentManager& cm,
                          const std::string&) -> std::any
                { return std::any(factory(document, cm)); });
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

            auto cacheIt = loadedAssets_.find(cacheKey);
            if (cacheIt != loadedAssets_.end())
            {
                return std::any_cast<T>(cacheIt->second);
            }

            // A cache hit is not a load. Besides describing the operation accurately, keeping
            // this after the lookup avoids per-frame console traffic in faithful XNA code that
            // calls Content.Load from Draw and relies on ContentManager's cache (SAMPLE-014).
            log::Debug(std::string("Loading asset: ") + assetName);

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

            // .cnb ranks immediately below .xnb and above everything CNA can compile FROM
            // (plans/plan_cnb.md CNBF-081, decision D8): a `.cnb` is CNA's own compiled artifact, so
            // it must win over the loose `.cnj`/native sources it was produced from, while still
            // yielding to a genuine externally-produced `.xnb`. Like the `.xnb` tier above it,
            // this sits ahead of the per-T reader lookup, because a `.cnb` is self-describing --
            // the file's own asset type identifier selects the loader through the process-wide
            // CNA::Content::CnbLoaderRegistry, with no reader registered on this ContentManager
            // at all.
            const std::string cnbCandidate =
                ResolveExistingAssetPath(BuildAssetPath(assetName) + ".cnb");
            if (std::filesystem::exists(cnbCandidate))
            {
                T result = LoadCnbAsset<T>(cnbCandidate, assetName);
                loadedAssets_[cacheKey] = result;
                return result;
            }

            // A caller that passes a full "Foo.cnb" name is handled too, which the .xnb tier does
            // not do for ".xnb" -- the check costs a string comparison, not a stat call, because
            // it only runs when the name actually ends that way.
            if (assetName.size() > 4 &&
                assetName.compare(assetName.size() - 4, 4, ".cnb") == 0)
            {
                const std::string literalCnb = ResolveExistingAssetPath(BuildAssetPath(assetName));
                if (std::filesystem::exists(literalCnb))
                {
                    T result = LoadCnbAsset<T>(literalCnb, assetName);
                    loadedAssets_[cacheKey] = result;
                    return result;
                }
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
         * @brief Loads @p assetName from the compiled `.cnb` file at @p cnbPath
         *        (plans/plan_cnb.md `CNBF-081`).
         *
         * The container is parsed and fully validated first (`CnbDocument::Parse` enforces every
         * structural invariant in `plans/plan_cnb.md` §4), then the loader registered for the file's
         * own asset type identifier produces the object.
         *
         * @tparam T       Requested asset type; must match exactly what the registered loader
         *                 produces.
         * @param cnbPath   Full filesystem path to the `.cnb` file.
         * @param assetName Logical asset name, passed to the loader for diagnostics.
         * @return The decoded asset.
         * @throws ContentLoadException if the file is malformed, holds an asset type this build
         *         has no loader for, holds a custom type whose canonical name disagrees with the
         *         registered one, or holds a different type than @p T.
         */
        template <typename T>
        [[nodiscard]] T LoadCnbAsset(const std::string& cnbPath, const std::string& assetName)
        {
            const CNA::Content::Cnb::CnbDocument document =
                CNA::Content::Cnb::CnbDocument::ParseFile(cnbPath);

            // Through ResolveForDocument rather than a bare numeric lookup: for a CUSTOM asset
            // type the identifier is only a 31-bit hash, so a numeric match does not prove the
            // file is that type. The resolver additionally requires the file's own canonical type
            // name to equal the registered one (plans/plan_cnb.md CNBF-H002). It hands back a COPY of
            // the loader, so nothing that registers or withdraws a loader on another thread can
            // pull it out from under the call below (CNBF-H003).
            const CNA::Content::CnbLoaderRegistry::LoaderFn loader =
                CNA::Content::CnbLoaderRegistry::ResolveForDocument(document);

            std::any produced = loader(document, *this, assetName);
            try
            {
                return std::any_cast<T>(std::move(produced));
            }
            catch (const std::bad_any_cast&)
            {
                // The registry is keyed by the file's own asset type, so this means the caller
                // asked for a different C++ type than that asset produces. Saying so beats
                // letting std::bad_any_cast -- an unrelated, undocumented exception type --
                // escape the content subsystem.
                throw ContentLoadException(
                    "'" + cnbPath + "' holds a " +
                    CNA::Content::Cnb::AssetTypeIdToString(document.AssetTypeId()) +
                    " asset, which is not the type requested for '" + assetName + "'.");
            }
        }

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

    // Explicit specialisation: SoundEffect is move-only with per-owner Dispose-cascade
    // semantics (T-3G) -- sharing one cached instance across unrelated Load<SoundEffect>()
    // call sites would let disposing one caller's copy silently cascade-stop another,
    // unrelated caller's still-playing instances. Each call gets its own independently-owned
    // SoundEffect instead; the generic loadedAssets_ any-cache (which requires T to be
    // CopyConstructible) is skipped entirely for this type.
    template<>
    Audio::SoundEffect ContentManager::Load<Audio::SoundEffect>(const std::string& assetName);

}
