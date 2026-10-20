// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Microsoft::Xna::Framework
{
    class Curve;
    class BoundingFrustum;
    namespace Graphics
    {
        class Texture2D;
        class Texture3D;
        class TextureCube;
        class SpriteFont;
        class Model;
        class VertexDeclaration;
    }
    namespace Audio { class SoundEffect; }
    namespace Media { class Song; class Video; }
}

namespace CNA::Internal::Xnb::Detail
{
    template <typename T>
    struct IsSharedPtr : std::false_type {};

    template <typename T>
    struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};

    template <typename T>
    struct IsOptional : std::false_type {};

    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type {};

    // XNB's managed reference/value distinction is not C++ copyability and cannot
    // use Sharp Type::IsValueType (a documented reflection stub). Keep it shared
    // by collection element dispatch and ContentReader's explicit-reader overload.
    // Custom identity-bearing objects use shared_ptr; plain custom values remain
    // value carriers. These built-ins are managed reference types represented by
    // CNA's existing by-value wrappers, not newly invented managed identities.
    template <typename T>
    struct IsSerializedReferenceType : std::bool_constant<
        IsSharedPtr<T>::value || std::is_pointer_v<T> ||
        std::is_same_v<T, std::string> || std::is_same_v<T, std::any> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Curve> ||
        std::is_same_v<T, Microsoft::Xna::Framework::BoundingFrustum> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Texture2D> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Texture3D> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::TextureCube> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::SpriteFont> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::Model> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Graphics::VertexDeclaration> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Audio::SoundEffect> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Media::Song> ||
        std::is_same_v<T, Microsoft::Xna::Framework::Media::Video>> {};

    template <typename T, typename Allocator>
    struct IsSerializedReferenceType<std::vector<T, Allocator>> : std::true_type {};

    template <typename Key, typename Value, typename Compare, typename Allocator>
    struct IsSerializedReferenceType<std::map<Key, Value, Compare, Allocator>> : std::true_type {};

    template <typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
    struct IsSerializedReferenceType<std::unordered_map<Key, Value, Hash, Equal, Allocator>> : std::true_type {};
}
