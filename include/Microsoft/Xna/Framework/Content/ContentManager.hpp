#pragma once

#include <any>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>

#include "CNA/Logger.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Audio  { class SoundEffect; }
namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }

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
        bool disposed_ = false;

        std::unordered_map<std::string, std::any> loadedAssets_;
        std::unordered_map<std::type_index, std::any> typeReaders_;

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret1, ref1, constmet1)

        [[nodiscard]] std::string BuildAssetPath(const std::string& assetName) const;
        [[nodiscard]] std::string NormalizeKey(const std::string& assetName) const;

        void RegisterBuiltinLoaders();

    public:
        ContentManager();
        ~ContentManager() override = default;

        void Dispose() override;

        void setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice);

        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceInternal() const;

        void Unload();

        /**
         * @brief Registers a custom type reader for assets of type T.
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
}
