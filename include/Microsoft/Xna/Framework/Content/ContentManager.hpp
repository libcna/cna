#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>

#include "CNA/Logger.hpp"
#include "CNA/Prop.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Content {

    using log = CNA::Logger;

    /**
     * @brief Minimal content manager for the current CNA subset.
     *
     * This implementation intentionally supports only the small subset
     * currently needed by the Speedy Blupi port:
     * - Graphics::Texture2D
     * - Audio::SoundEffect
     *
     * It does not use XNB files and does not cache assets.
     * Assets are loaded directly from files under RootDirectory.
     */
    class ContentManager {
    private:
        std::string RootDirectory_ = "Content";
        Graphics::GraphicsDevice* graphicsDevice_ = nullptr;

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret1, ref1, constmet1)

    private:
        /**
         * @brief Builds a full asset path from the root directory and asset name.
         *
         * @param assetName Relative asset path.
         * @return Full path to the asset file.
         */
        [[nodiscard]] std::string BuildAssetPath(const std::string& assetName) const;

    public:
        /**
         * @brief Constructs a content manager.
         */

        ContentManager();

        void setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice);

        /**
         * @brief Loads an asset of type T from the content root.
         *
         * Currently supported types:
         * - Microsoft::Xna::Framework::Graphics::Texture2D
         * - Microsoft::Xna::Framework::Audio::SoundEffect
         *
         * @tparam T Asset type.
         * @param assetName Relative file path inside the content root.
         * @return Loaded asset instance.
         */
        template<typename T>
        [[nodiscard]] T Load(const std::string& assetName) {
            const std::string fullPath = BuildAssetPath(assetName);
            log::Debug(std::string("Loading asset: ") + fullPath);

            if constexpr (std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Texture2D>) {
                if (graphicsDevice_ == nullptr) {
                    throw std::runtime_error("ContentManager::Load<Texture2D>() failed: GraphicsDevice is null.");
                }
                return T(fullPath, *graphicsDevice_);
            } else if constexpr (std::is_same_v<T, Microsoft::Xna::Framework::Audio::SoundEffect>) {
                return T(fullPath);
            } else {
                static_assert(
                    std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Texture2D> ||
                    std::is_same_v<T, Microsoft::Xna::Framework::Audio::SoundEffect>,
                    "ContentManager::Load<T>() currently supports only Texture2D and SoundEffect."
                );
            }
        }
    };
}