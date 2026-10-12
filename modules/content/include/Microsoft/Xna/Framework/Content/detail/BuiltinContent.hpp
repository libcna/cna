// SPDX-License-Identifier: MS-PL
#pragma once
// ContentManager's per-type reader registration (detail::BuiltinContent). Implementation detail of
// ContentManager::Load<T>(), in its own header so it is not mistaken for public API.

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework { class Curve; }
namespace Microsoft::Xna::Framework::Audio  { class SoundEffect; }
namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice; class Texture2D; class TextureCube; class Texture3D; class SpriteFont;
    class Effect; class Model; struct AnimationClipEXT; class SkinnedModelEXT;
}
namespace Microsoft::Xna::Framework::Media { class Song; class Video; }

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager;

    namespace detail
    {
        /**
         * @brief CNAEXT: the built-in readers of one asset type, registered by the first
         *        Load<T>() that needs them.
         *
         * Every asset type CNA can load itself specializes this (below) with a Register() defined
         * in that asset family's translation unit (modules/content/src/Xna/Content*Loaders.cpp),
         * which installs the family's loose-file reader on the manager and its process-wide
         * `.cnb` loader and `.xnb` readers. Load<T>() calls it, so a program links the readers,
         * importers and codecs of exactly the types it loads: a game that loads textures and
         * sounds never links the Model reader, the glTF importer or Draco.
         *
         * Any other T may be a game's own type whose reader reads a built-in asset, so the
         * primary template registers every built-in `.xnb` reader; a plain value (numbers,
         * strings, lists of them) needs only the primitive ones.
         *
         * Link boundary: never fold these back into one function every manager calls -- that made
         * every CNA game link every content importer (docs/link-size-architecture.md).
         */
        template <typename T>
        struct CNAEXT BuiltinContent;

        /** @brief Registers every built-in `.xnb` reader (a type that may hold any asset). */
        void RegisterEveryBuiltinXnbReader();
        /**
         * @brief Resolves an `.xnb` external reference against the referencing asset's name,
         *        refusing one that leaves the content root (ContentReader.cpp).
         * @param filePath The referencing asset's logical name.
         * @param relativeFile The reference as the file spells it.
         * @return The referenced asset's logical name.
         */
        std::string ResolveExternalReferencePath(const std::string& filePath,
                                                 const std::string& relativeFile);
        /** @brief Registers the `.xnb` readers of primitive values and their collections. */
        void RegisterValueXnbReaders();

        /** @brief Whether T is a plain value an `.xnb` stores with the primitive readers only. */
        template <typename T>
        struct IsValueContent : std::bool_constant<std::is_arithmetic_v<T> || std::is_enum_v<T>> {};
        /** @brief A string is a value. */
        template <>
        struct IsValueContent<std::string> : std::true_type {};
        /** @brief A list of values is a value. */
        template <typename T, typename A>
        struct IsValueContent<std::vector<T, A>> : IsValueContent<T> {};
        /** @brief A dictionary of values is a value. */
        template <typename K, typename V, typename H, typename E, typename A>
        struct IsValueContent<std::unordered_map<K, V, H, E, A>>
            : std::bool_constant<IsValueContent<K>::value && IsValueContent<V>::value> {};

        template <typename T>
        struct CNAEXT BuiltinContent
        {
            /** @brief False: CNA has no reader of its own for T. */
            static constexpr bool IsBuiltin = false;

            /** @brief Registers what an `.xnb` holding a T may need. @param manager Unused. */
            static void Register(ContentManager& manager)
            {
                (void)manager;
                if constexpr (IsValueContent<T>::value)
                    RegisterValueXnbReaders();
                else
                    RegisterEveryBuiltinXnbReader();
            }
        };

        // RegisterCnbLoader() installs the type's `.cnb` loader alone (Register() does it too); it
        // is defined only for the ten types that have one (CnbLoaderRegistryQueries.cpp).
#define CNA_BUILTIN_CONTENT(Type)                                                              \
        template <>                                                                            \
        struct CNAEXT BuiltinContent<Type>                                                     \
        {                                                                                      \
            static constexpr bool IsBuiltin = true;                                            \
            static void Register(ContentManager& manager);                                     \
            static void RegisterCnbLoader();                                                   \
        };
        /** @brief Texture2D (ContentTextureLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Graphics::Texture2D)
        /** @brief TextureCube (ContentTextureLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Graphics::TextureCube)
        /** @brief Texture3D, loaded as a shared_ptr (ContentTextureLoaders.cpp). */
        CNA_BUILTIN_CONTENT(std::shared_ptr<Graphics::Texture3D>)
        /** @brief SoundEffect (ContentSoundEffectLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Audio::SoundEffect)
        /** @brief SpriteFont (ContentSpriteFontLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Graphics::SpriteFont)
        /** @brief Effect, loaded as a shared_ptr (ContentEffectLoaders.cpp). */
        CNA_BUILTIN_CONTENT(std::shared_ptr<Graphics::Effect>)
        /** @brief Model (ContentModelLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Graphics::Model)
        /** @brief AnimationClipEXT (ContentModelLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Graphics::AnimationClipEXT)
        /** @brief SkinnedModelEXT, loaded as a shared_ptr (ContentModelLoaders.cpp). */
        CNA_BUILTIN_CONTENT(std::shared_ptr<Graphics::SkinnedModelEXT>)
        /** @brief Curve (ContentCurveLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Microsoft::Xna::Framework::Curve)
        /** @brief Song (ContentMediaLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Media::Song)
        /** @brief Video (ContentMediaLoaders.cpp). */
        CNA_BUILTIN_CONTENT(Media::Video)
#undef CNA_BUILTIN_CONTENT
    }
}
