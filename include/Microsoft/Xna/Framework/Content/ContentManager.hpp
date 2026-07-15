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
#include "CNA/Internal/CnbEnvelope.hpp"
#include "CNA/Logger.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Audio  { class SoundEffect; }
namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; class Texture2D; class TextureCube; }
namespace CNA::Internal::Backends { class ITextureBackend; }

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

        std::unordered_map<std::string, std::any> loadedAssets_;
        std::unordered_map<std::type_index, std::any> typeReaders_;

        // plan_cnb.md CNB-24: per-C++-type table of named .cnb loaders, keyed first by the
        // requested type T (std::type_index), then by the .cnb document's own "type" string.
        // Populated by RegisterCnbLoader<T>(); consulted by GenericCnbTypeReader<T> below.
        std::unordered_map<std::type_index, std::unordered_map<std::string, std::any>> cnbNamedLoaders_;

        struct WeakTextureEntry {
            std::weak_ptr<CNA::Internal::Backends::ITextureBackend> backend;
            std::weak_ptr<std::vector<uint8_t>> cpuPixels;
            Graphics::SurfaceFormat fmt;
            int levelCount;
        };
        std::unordered_map<std::string, WeakTextureEntry> textureCache_;

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret1, ref1, constmet1)

        [[nodiscard]] std::string BuildAssetPath(const std::string& assetName) const;
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
        NOXNA ContentManager();

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
         * @brief Registers a custom type reader for assets of type T.
         *
         * @tparam T     Asset type this reader produces.
         * @param reader Unique pointer to the type reader to register.
         */
        template <typename T>
        void RegisterTypeReader(std::unique_ptr<ContentTypeReader<T>> reader)
        {
            typeReaders_[std::type_index(typeid(T))] =
                std::shared_ptr<ContentTypeReader<T>>(std::move(reader));
        }

        /**
         * @brief Factory signature for a game-registered named .cnb loader (see
         *        RegisterCnbLoader()).
         *
         * Receives the raw .cnb JSON document text and the owning ContentManager (for
         * recursively loading any files it references), and returns a constructed T.
         */
        template <typename T>
        NOXNA using CnbLoaderFn = std::function<T(const std::string& cnbJson, ContentManager& cm)>;

        /**
         * @brief Registers a named .cnb loader for asset type T, selected by the .cnb
         *        document's own "type" field rather than by T alone.
         *
         * Unlike RegisterTypeReader<T>() (one reader per T, fixed at the Load<T>() call site),
         * multiple differently-named .cnb "type" values can each register their own factory
         * here, all producing the same T -- e.g. a game's "EnemyDefinition" and "LootTable" .cnb
         * types both deserializing into the same generic data struct via two different
         * factories (see cnb.md's "Custom loaders" section). Only applies to a T with no
         * existing reader already registered (built-in or via RegisterTypeReader<T>()); throws
         * immediately if one already exists, since it would never be consulted by that reader.
         *
         * @tparam T       Asset type the factory produces.
         * @param typeName The .cnb document's "type" string this factory handles.
         * @param factory  Callback invoked with the raw .cnb JSON text and this ContentManager.
         * @throws std::logic_error if a reader is already registered for T.
         */
        template <typename T>
        NOXNA void RegisterCnbLoader(const std::string& typeName, CnbLoaderFn<T> factory)
        {
            const auto ti = std::type_index(typeid(T));
            const bool alreadyOwnedByAnotherReader =
                typeReaders_.find(ti) != typeReaders_.end() &&
                cnbNamedLoaders_.find(ti) == cnbNamedLoaders_.end();
            if (alreadyOwnedByAnotherReader)
            {
                throw std::logic_error(
                    "ContentManager::RegisterCnbLoader<T>(): a reader is already registered for "
                    "this type; RegisterCnbLoader only applies to a type with no existing reader, "
                    "since an existing reader would never consult this table.");
            }

            auto& innerMap = cnbNamedLoaders_[ti];
            const bool firstForThisType = innerMap.empty();
            innerMap[typeName] = std::move(factory);

            if (firstForThisType)
            {
                RegisterTypeReader<T>(std::make_unique<GenericCnbTypeReader<T>>());
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
            log::Debug(std::string("Loading asset: ") + assetName);

            auto cacheIt = loadedAssets_.find(key);
            if (cacheIt != loadedAssets_.end())
            {
                return std::any_cast<T>(cacheIt->second);
            }

            auto readerIt = typeReaders_.find(std::type_index(typeid(T)));
            if (readerIt == typeReaders_.end())
            {
                throw ContentLoadException(
                    std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                    + assetName + "'.");
            }

            auto* readerPtr = std::any_cast<std::shared_ptr<ContentTypeReader<T>>>(&readerIt->second);
            if (!readerPtr || !*readerPtr)
            {
                throw ContentLoadException(
                    std::string("ContentManager::Load<T>(): Reader is null for asset '")
                    + assetName + "'.");
            }

            ContentTypeReader<T>& reader = **readerPtr;
            const std::string resolvedPath = ResolveAssetPath(assetName, reader);

            T result = reader.Read(resolvedPath, *this);
            loadedAssets_[key] = result;
            return result;
        }

    private:
        // Generic reader for game-registered .cnb "type" values that don't have a dedicated
        // ContentTypeReader<T>. Looks up the .cnb envelope's "type" field in cnbNamedLoaders_
        // and invokes whichever RegisterCnbLoader<T>()-registered factory matches (cnb.md's
        // "Custom loaders" section). Auto-registered by RegisterCnbLoader<T>() the first time
        // it's called for a T with no existing reader; never auto-registered for a T that
        // already has a built-in or otherwise-registered reader.
        template <typename T>
        class GenericCnbTypeReader : public ContentTypeReader<T>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnb"};
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

                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                if (!envelope.hasType)
                {
                    throw ContentLoadException(
                        "ContentManager: '" + path + "' is missing the required 'type' field.");
                }

                auto outerIt = cm.cnbNamedLoaders_.find(std::type_index(typeid(T)));
                if (outerIt != cm.cnbNamedLoaders_.end())
                {
                    auto innerIt = outerIt->second.find(envelope.type);
                    if (innerIt != outerIt->second.end())
                    {
                        auto* factory = std::any_cast<CnbLoaderFn<T>>(&innerIt->second);
                        if (factory && *factory)
                        {
                            return (*factory)(json, cm);
                        }
                    }
                }

                throw ContentLoadException(
                    "ContentManager: '" + path + "' has unrecognized .cnb type '" +
                    envelope.type + "'.");
            }
        };

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
            ContentTypeReader<T>& reader) const
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
            if (std::filesystem::exists(base))
            {
                return base;
            }

            // .cnb is always tried before any native/reader-declared extension (cnb.md's "core
            // rule" -- when a .cnb sidecar is present, it always has final say over how an asset
            // name resolves, even if a native file with the same name also exists). This makes
            // .cnb usable as an optional metadata sidecar (plan_cnb.md CNB-4), not just a
            // mutually-exclusive alternative to a native file.
            const std::string cnbCandidate = base + ".cnb";
            if (std::filesystem::exists(cnbCandidate))
            {
                return cnbCandidate;
            }

            // Try each extension declared by the reader.
            const auto extensions = reader.GetExtensions();
            for (const auto& ext : extensions)
            {
                const std::string candidate = base + ext;
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
    // GPU backend is freed as soon as the last external Texture2D copy is dropped,
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

    // Explicit specialisation: TextureCube is move-only (NOXNA, copy constructor deleted --
    // unlike Texture2D, which supports reference-counted backend sharing via its own weak-cache
    // specialisation above), so it cannot be held in the generic strong (std::any-based) cache
    // either. Mirrors SoundEffect's own identical not-cached specialisation: each call gets its
    // own independently-decoded instance.
    template<>
    Graphics::TextureCube ContentManager::Load<Graphics::TextureCube>(const std::string& assetName);
}
