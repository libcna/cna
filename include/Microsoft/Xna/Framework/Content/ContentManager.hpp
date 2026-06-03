#pragma once

#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>

#include "CNA/Logger.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

namespace Microsoft::Xna::Framework::Content
{
    using log = CNA::Logger;

    /**
     * @brief Content manager with extensible type reader support and asset caching.
     *
     * Supports loading assets by type. Built-in support for:
     * - Graphics::Texture2D
     * - Audio::SoundEffect
     * - std::shared_ptr<Graphics::Effect> (via .shader.json descriptor)
     *
     * Custom types can be registered via RegisterTypeReader<T>().
     */
    class ContentManager
    {
    private:
        std::string RootDirectory_ = "Content";
        Graphics::GraphicsDevice* graphicsDevice_ = nullptr;

        std::unordered_map<std::string, std::any> loadedAssets_;
        std::unordered_map<std::type_index, std::any> typeReaders_;

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret1, ref1, constmet1)
        void Dispose();

    private:
        /**
         * @brief Builds a full asset path from the root directory and asset name.
         *
         * @param assetName Relative asset path.
         * @return Full path to the asset file.
         */
        [[nodiscard]] std::string BuildAssetPath(const std::string& assetName) const;

        /**
         * @brief Normalises an asset name to a canonical cache key.
         *
         * Converts to lowercase and replaces backslashes with forward slashes.
         *
         * @param assetName Raw asset name.
         * @return Normalised key string.
         */
        [[nodiscard]] std::string NormalizeKey(const std::string& assetName) const;

        /**
         * @brief Registers the built-in Effect loader for .shader.json files.
         */
        void RegisterBuiltinEffectLoader();

    public:
        /**
         * @brief Constructs a content manager and registers built-in loaders.
         */
        ContentManager();

        void setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice);

        /**
         * @brief Returns the graphics device, or throws if not set.
         *
         * Used internally by built-in loaders that need to create GPU resources.
         */
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceInternal() const;

        /**
         * @brief Releases all cached assets.
         */
        void Unload();

        /**
         * @brief Registers a custom type reader for assets of type T.
         *
         * @tparam T Asset type.
         * @param reader Unique pointer to the type reader implementation.
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
         * Results are cached — subsequent calls with the same asset name
         * return the already-loaded instance without I/O.
         *
         * Built-in supported types:
         * - Microsoft::Xna::Framework::Graphics::Texture2D
         * - Microsoft::Xna::Framework::Audio::SoundEffect
         * - std::shared_ptr<Microsoft::Xna::Framework::Graphics::Effect>
         *
         * @tparam T Asset type.
         * @param assetName Relative file path inside the content root.
         * @return Loaded asset instance.
         * @throws ContentLoadException if the asset cannot be loaded.
         */
        template <typename T>
        [[nodiscard]] T Load(const std::string& assetName)
        {
            const std::string key = NormalizeKey(assetName);
            log::Debug(std::string("Loading asset: ") + assetName);

            auto cacheIt = loadedAssets_.find(key);
            if (cacheIt != loadedAssets_.end())
            {
                return std::any_cast<T>(cacheIt->second);
            }

            auto readerIt = typeReaders_.find(std::type_index(typeid(T)));
            if (readerIt != typeReaders_.end())
            {
                auto& readerAny = readerIt->second;
                auto* reader = std::any_cast<std::shared_ptr<ContentTypeReader<T>>>(&readerAny);
                if (reader && *reader)
                {
                    T result = (*reader)->Read(BuildAssetPath(assetName), *this);
                    loadedAssets_[key] = result;
                    return result;
                }
            }

            const std::string fullPath = BuildAssetPath(assetName);

            if constexpr (std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Texture2D>)
            {
                if (graphicsDevice_ == nullptr)
                {
                    throw ContentLoadException("ContentManager::Load<Texture2D>() failed: GraphicsDevice is null.");
                }
                std::string path = fullPath;
                if (!CONTAINS(assetName, "."))
                {
                    path += ".png";
                }
                T result(path, *graphicsDevice_);
                loadedAssets_[key] = result;
                return result;
            }
            else if constexpr (std::is_same_v<T, Microsoft::Xna::Framework::Audio::SoundEffect>)
            {
                std::string path = fullPath;
                if (!CONTAINS(assetName, "."))
                {
                    path += ".wav";
                }
                T result(path);
                loadedAssets_[key] = result;
                return result;
            }
            else
            {
                throw ContentLoadException(
                    std::string("ContentManager::Load<T>(): No reader registered for type and asset '") +
                    assetName + "'."
                );
            }
        }
    };
}
