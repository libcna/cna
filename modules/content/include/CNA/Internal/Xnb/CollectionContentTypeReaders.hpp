// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

// plans/plan_xnb.md XNB-21/XNB-22: generic collection .xnb readers.
//
// FNA resolves ArrayReader<T>/ListReader<T>/DictionaryReader<TKey,TValue>/NullableReader<T>'s
// element reader(s) via *reflection*: Initialize(ContentTypeReaderManager manager) calls
// manager.GetTypeReader(typeof(T)), a lookup among the CURRENT FILE's own already-instantiated
// type-reader table. CNA has no reflection, so it cannot resolve "typeof(T)" from a C++ template
// parameter at runtime. Since CNA's registry (XNB-5's canonical-name scheme) is already a
// per-combination, hand-registered mechanism -- there is no way to register "ListReader<T> for
// any T" generically in the first place, only "ListReader<Vector3>" as its own distinct
// registration -- the natural, minimal fix is: each concrete closed-generic registration
// (see the Register*XnbReader() helpers below) hardcodes its own element reader's canonical name
// directly in the registration closure, and the element reader instance is constructed fresh via
// ContentTypeReaderManager::CreateReader() in the C++ constructor rather than looked up from the
// file's own table via Initialize(). This is valid because every CNA reader implemented so far is
// stateless -- revisit only if a future reader genuinely needs per-file Initialize() state.
//
// These classes live in CNA::Internal::Xnb -- see PrimitiveContentTypeReaders.hpp's own note on
// why (FNA's own equivalents are all `internal`, never subclassed by game code).

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderBase;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    namespace Detail
    {
        template <typename T>
        struct IsSharedPtr : std::false_type {};

        template <typename U>
        struct IsSharedPtr<std::shared_ptr<U>> : std::true_type {};

        inline std::unique_ptr<ContentTypeReaderBase> RequireReader(const std::string& canonicalName)
        {
            auto reader = ContentTypeReaderManager::CreateReader(canonicalName);
            if (!reader)
            {
                throw ContentLoadException(
                    "Unregistered .xnb element content type reader '" + canonicalName + "'.");
            }
            return reader;
        }
    }

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.ArrayReader<T>`
     *        (`src/Content/ContentReaders/ArrayReader.cs`) -- `T[]` maps to `std::vector<T>` on
     *        the CNA side (CNA has no separate fixed-size array type).
     *
     * A minor, deliberate deviation from FNA: if @p existingInstance is provided but its size
     * does not match the serialized count, this resizes rather than assuming (as FNA's raw
     * array-index-write does) that the caller already sized it correctly -- avoids
     * out-of-bounds writes instead of relying on undefined caller behavior.
     */
    template <typename T>
    class ArrayReader : public ContentTypeReader<std::vector<T>>
    {
    public:
        /**
         * @param targetTypeName            Canonical XNA type name for `std::vector<T>` (e.g.
         *                                  `"Microsoft.Xna.Framework.Vector3[]"`).
         * @param elementReaderCanonicalName Canonical `.xnb` reader name for a single `T`
         *                                  element (e.g.
         *                                  `"Microsoft.Xna.Framework.Content.Vector3Reader"`),
         *                                  used only when `T` is not `shared_ptr`-shaped.
         */
        ArrayReader(std::string targetTypeName, std::string elementReaderCanonicalName)
            : ContentTypeReader<std::vector<T>>(std::move(targetTypeName))
            , elementReaderCanonicalName_(std::move(elementReaderCanonicalName))
        {
        }

    protected:
        std::vector<T> Read(ContentReader& input, std::optional<std::vector<T>> existingInstance) override
        {
            const uint32_t count = input.ReadUInt32();
            input.CheckCollectionElementCount(count, this->getTargetTypeNameProperty());
            std::vector<T> array = existingInstance.value_or(std::vector<T>(count));
            if (array.size() != count) array.resize(count);

            if constexpr (Detail::IsSharedPtr<T>::value)
            {
                // Reference-type-like elements: each gets its own 1-based dispatch index
                // (matches FNA's per-element polymorphic dispatch), handled by ReadObject<T>()'s
                // own generic index read.
                for (uint32_t i = 0; i < count; ++i)
                {
                    array[i] = input.template ReadObject<T>();
                }
            }
            else
            {
                auto elementReader = Detail::RequireReader(elementReaderCanonicalName_);
                for (uint32_t i = 0; i < count; ++i)
                {
                    array[i] = input.template ReadObject<T>(*elementReader);
                }
            }
            return array;
        }

    private:
        std::string elementReaderCanonicalName_;
    };

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.ListReader<T>`
     *        (`src/Content/ContentReaders/ListReader.cs`) -- `List<T>` maps to `std::vector<T>`.
     *
     * Faithfully matches FNA's own (perhaps-surprising) behavior: unlike `DictionaryReader`,
     * `existingInstance`'s elements are **not** cleared first -- new elements are appended after
     * whatever was already there, exactly as FNA's own `list.Add(...)` loop (with no preceding
     * `.Clear()`) does.
     */
    template <typename T>
    class ListReader : public ContentTypeReader<std::vector<T>>
    {
    public:
        ListReader(std::string targetTypeName, std::string elementReaderCanonicalName)
            : ContentTypeReader<std::vector<T>>(std::move(targetTypeName))
            , elementReaderCanonicalName_(std::move(elementReaderCanonicalName))
        {
        }

        bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

    protected:
        std::vector<T> Read(ContentReader& input, std::optional<std::vector<T>> existingInstance) override
        {
            const int32_t count = input.ReadInt32();
            input.CheckCollectionElementCount(count, this->getTargetTypeNameProperty());
            std::vector<T> list = existingInstance.value_or(std::vector<T>{});
            list.reserve(list.size() + static_cast<std::size_t>(count));

            if constexpr (Detail::IsSharedPtr<T>::value)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    list.push_back(input.template ReadObject<T>());
                }
            }
            else
            {
                auto elementReader = Detail::RequireReader(elementReaderCanonicalName_);
                for (int32_t i = 0; i < count; ++i)
                {
                    list.push_back(input.template ReadObject<T>(*elementReader));
                }
            }
            return list;
        }

    private:
        std::string elementReaderCanonicalName_;
    };

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.DictionaryReader<TKey, TValue>`
     *        (`src/Content/ContentReaders/DictionaryReader.cs`) -- maps to
     *        `std::unordered_map<TKey, TValue>` (XNA's `Dictionary<TKey,TValue>` makes no
     *        ordering guarantee either).
     *
     * Faithfully matches FNA: unlike `ListReader`, `existingInstance` **is** cleared first if
     * provided, then repopulated with `count` freshly-read pairs.
     */
    template <typename TKey, typename TValue>
    class DictionaryReader : public ContentTypeReader<std::unordered_map<TKey, TValue>>
    {
    public:
        DictionaryReader(std::string targetTypeName,
                          std::string keyReaderCanonicalName,
                          std::string valueReaderCanonicalName)
            : ContentTypeReader<std::unordered_map<TKey, TValue>>(std::move(targetTypeName))
            , keyReaderCanonicalName_(std::move(keyReaderCanonicalName))
            , valueReaderCanonicalName_(std::move(valueReaderCanonicalName))
        {
        }

        bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

    protected:
        std::unordered_map<TKey, TValue> Read(
            ContentReader& input, std::optional<std::unordered_map<TKey, TValue>> existingInstance) override
        {
            const int32_t count = input.ReadInt32();
            input.CheckCollectionElementCount(count, this->getTargetTypeNameProperty());
            std::unordered_map<TKey, TValue> dictionary = existingInstance.value_or(std::unordered_map<TKey, TValue>{});
            dictionary.clear();

            std::unique_ptr<ContentTypeReaderBase> keyReader;
            std::unique_ptr<ContentTypeReaderBase> valueReader;
            if constexpr (!Detail::IsSharedPtr<TKey>::value) keyReader = Detail::RequireReader(keyReaderCanonicalName_);
            if constexpr (!Detail::IsSharedPtr<TValue>::value) valueReader = Detail::RequireReader(valueReaderCanonicalName_);

            for (int32_t i = 0; i < count; ++i)
            {
                TKey key;
                if constexpr (Detail::IsSharedPtr<TKey>::value) key = input.template ReadObject<TKey>();
                else key = input.template ReadObject<TKey>(*keyReader);

                TValue value;
                if constexpr (Detail::IsSharedPtr<TValue>::value) value = input.template ReadObject<TValue>();
                else value = input.template ReadObject<TValue>(*valueReader);

                dictionary.emplace(std::move(key), std::move(value));
            }
            return dictionary;
        }

    private:
        std::string keyReaderCanonicalName_;
        std::string valueReaderCanonicalName_;
    };

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.NullableReader<T>`
     *        (`src/Content/ContentReaders/NullableReader.cs`, `where T : struct`) -- maps to
     *        `std::optional<T>`.
     */
    template <typename T>
    class NullableReader : public ContentTypeReader<std::optional<T>>
    {
    public:
        NullableReader(std::string targetTypeName, std::string elementReaderCanonicalName)
            : ContentTypeReader<std::optional<T>>(std::move(targetTypeName))
            , elementReaderCanonicalName_(std::move(elementReaderCanonicalName))
        {
        }

    protected:
        std::optional<T> Read(ContentReader& input, std::optional<std::optional<T>>) override
        {
            if (!input.ReadBoolean())
            {
                return std::nullopt;
            }
            auto elementReader = Detail::RequireReader(elementReaderCanonicalName_);
            return input.template ReadObject<T>(*elementReader);
        }

    private:
        std::string elementReaderCanonicalName_;
    };
}
