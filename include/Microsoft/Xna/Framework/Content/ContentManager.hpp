// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Logger.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Audio  { class SoundEffect; }
namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; class Texture2D; }
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
        /**
         * @brief Resolves the full filesystem path for an asset, trying reader extensions
         *        when the asset name has no extension.
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

            // If assetName already has an extension, use the path as-is.
            if (std::filesystem::path(assetName).has_extension())
            {
                return base;
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
}
