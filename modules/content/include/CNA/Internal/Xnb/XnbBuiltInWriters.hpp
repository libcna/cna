// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/XnbWriter.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

// System::Decimal exists only where the compiler provides a native 128-bit integer, so its
// writer -- like DecimalReader -- lives behind sharp-runtime's own capability macro.
#if SHARP_RUNTIME_HAS_NATIVE_INT128
#include "System/Decimal.hpp"
#endif

namespace CNA::Internal::Xnb
{
    /**
     * @brief A built-in `.xnb` type writer described declaratively by its reader identity and a
     *        stateless payload function (plans/plan_xnapipeline.md `XNAP-20`, `XNAP-21`).
     *
     * The stock format has many types whose payload is a fixed field sequence and nothing else.
     * Expressing each as data plus one free function keeps the sixteen primitive writers and the
     * framework value-type writers auditable side by side, and keeps the reader identity — the
     * part with real provenance weight — in one visible table rather than scattered across
     * classes.
     *
     * @tparam T The exact C++ type serialized by this writer.
     */
    template<typename T>
    class XnbFunctionTypeWriter final : public XnbTypeWriter<T>
    {
    public:
        /** @brief Stateless payload emitter for one value of `T`. */
        using PayloadWriter = void (*)(XnbWriter&, const T&);

        /**
         * @brief Creates a declarative writer.
         *
         * @param identity Reader identity emitted into the type-reader table.
         * @param serializedByReference Whether a nested element of `T` carries its own dispatch
         *        index, i.e. whether the .NET type is a reference type.
         * @param payload Non-null stateless payload emitter.
         */
        XnbFunctionTypeWriter(XnbReaderIdentity identity, const bool serializedByReference,
                              const PayloadWriter payload)
            : identity_(std::move(identity))
            , serializedByReference_(serializedByReference)
            , payload_(payload)
        {
        }

        /** @brief Returns the configured reader identity. */
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override { return identity_; }

        /** @brief Returns whether nested elements of `T` carry a dispatch index. */
        [[nodiscard]] bool IsSerializedByReference() const noexcept override
        {
            return serializedByReference_;
        }

    protected:
        /**
         * @brief Delegates to the configured payload emitter.
         *
         * @param output Per-file object-graph writer.
         * @param value The value to serialize.
         */
        void Write(XnbWriter& output, const T& value) const override { payload_(output, value); }

    private:
        XnbReaderIdentity identity_;
        bool serializedByReference_ = false;
        PayloadWriter payload_ = nullptr;
    };

    /**
     * @brief Explicit `T[]` carrier, distinguishing a managed array from a `List<T>`.
     *
     * CNA represents both `T[]` and `List<T>` as `std::vector<T>` on the reading side, where the
     * two are told apart by the reader *name*. A writer registry keyed by C++ type cannot do that,
     * so an asset that must emit `ArrayReader<T>` rather than `ListReader<T>` wraps its values in
     * this type. The wrapper is a build-time serialization detail and never reaches a runtime API.
     *
     * @tparam T The element type.
     */
    template<typename T>
    struct XnbArray
    {
        /** @brief The array elements in order. */
        std::vector<T> values;

        /** @brief Compares the complete element sequence. */
        bool operator==(const XnbArray& other) const = default;
    };

    /**
     * @brief Writer for `List<T>`, serialized as an `Int32` count followed by the elements.
     *
     * An element of a .NET reference type carries its own dispatch index; a value-typed element
     * does not, matching how `ListReader<T>` reads them. The element reader is interned either
     * way, which is why a value-typed element reader still appears in the type-reader table.
     *
     * @tparam T The element type.
     */
    template<typename T>
    class XnbListTypeWriter final : public XnbTypeWriter<std::vector<T>>
    {
    public:
        /**
         * @brief Creates a `List<T>` writer.
         *
         * @param elementIdentity Reader identity of one element, used to spell this reader's own
         *        generic argument.
         */
        explicit XnbListTypeWriter(XnbReaderIdentity elementIdentity)
            : elementIdentity_(std::move(elementIdentity))
        {
        }

        /** @brief Returns `ListReader\`1[[<element target type>]]`. */
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = "Microsoft.Xna.Framework.Content.ListReader`1";
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = "System.Collections.Generic.List`1";
            identity.targetAssembly = XnbAssembly::Mscorlib;
            identity.genericArguments = {elementIdentity_};
            identity.evidence = XnbNameEvidence::Xna40Fixture;
            return identity;
        }

        /** @brief A `List<T>` is a .NET reference type. */
        [[nodiscard]] bool IsSerializedByReference() const noexcept override { return true; }

    protected:
        /**
         * @brief Writes the element count and every element.
         *
         * @param output Per-file object-graph writer.
         * @param value The list to serialize.
         */
        void Write(XnbWriter& output, const std::vector<T>& value) const override
        {
            output.RequireCollectionCount(value.size(), "ListWriter");
            output.WriteInt32(static_cast<std::int32_t>(value.size()));
            for (const T& element : value)
            {
                if constexpr (Detail::IsSerializedReferenceType<T>::value)
                {
                    output.WriteObject(element);
                }
                else
                {
                    output.WriteRawObject(element);
                }
            }
        }

    private:
        XnbReaderIdentity elementIdentity_;
    };

    /**
     * @brief Writer for `T[]`, serialized as a `UInt32` count followed by the elements.
     *
     * @tparam T The element type.
     */
    template<typename T>
    class XnbArrayTypeWriter final : public XnbTypeWriter<XnbArray<T>>
    {
    public:
        /**
         * @brief Creates a `T[]` writer.
         *
         * @param elementIdentity Reader identity of one element.
         */
        explicit XnbArrayTypeWriter(XnbReaderIdentity elementIdentity)
            : elementIdentity_(std::move(elementIdentity))
        {
        }

        /** @brief Returns `ArrayReader\`1[[<element target type>]]`. */
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = "Microsoft.Xna.Framework.Content.ArrayReader`1";
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = XnbTargetTypeName(elementIdentity_) + "[]";
            identity.targetAssembly = elementIdentity_.targetAssembly;
            identity.genericArguments = {elementIdentity_};
            identity.evidence = XnbNameEvidence::DerivedRule;
            return identity;
        }

        /** @brief A managed array is a .NET reference type. */
        [[nodiscard]] bool IsSerializedByReference() const noexcept override { return true; }

    protected:
        /**
         * @brief Writes the element count and every element.
         *
         * @param output Per-file object-graph writer.
         * @param value The array to serialize.
         */
        void Write(XnbWriter& output, const XnbArray<T>& value) const override
        {
            output.RequireCollectionCount(value.values.size(), "ArrayWriter");
            output.WriteUInt32(static_cast<std::uint32_t>(value.values.size()));
            for (const T& element : value.values)
            {
                if constexpr (Detail::IsSerializedReferenceType<T>::value)
                {
                    output.WriteObject(element);
                }
                else
                {
                    output.WriteRawObject(element);
                }
            }
        }

    private:
        XnbReaderIdentity elementIdentity_;
    };

    /**
     * @brief Writer for `Dictionary<TKey,TValue>`, serialized as an `Int32` count then key/value
     *        pairs.
     *
     * Entries are emitted in a deterministic key order rather than in `std::unordered_map`
     * iteration order: a content build must be byte-reproducible, and `Dictionary<,>` itself
     * promises no ordering to the consuming game.
     *
     * @tparam TKey The key type.
     * @tparam TValue The value type.
     */
    template<typename TKey, typename TValue>
    class XnbDictionaryTypeWriter final
        : public XnbTypeWriter<std::unordered_map<TKey, TValue>>
    {
    public:
        /**
         * @brief Creates a `Dictionary<TKey,TValue>` writer.
         *
         * @param keyIdentity Reader identity of one key.
         * @param valueIdentity Reader identity of one value.
         */
        XnbDictionaryTypeWriter(XnbReaderIdentity keyIdentity, XnbReaderIdentity valueIdentity)
            : keyIdentity_(std::move(keyIdentity)), valueIdentity_(std::move(valueIdentity))
        {
        }

        /** @brief Returns `DictionaryReader\`2[[<key>],[<value>]]`. */
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = "Microsoft.Xna.Framework.Content.DictionaryReader`2";
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = "System.Collections.Generic.Dictionary`2";
            identity.targetAssembly = XnbAssembly::Mscorlib;
            identity.genericArguments = {keyIdentity_, valueIdentity_};
            identity.evidence = XnbNameEvidence::DerivedRule;
            return identity;
        }

        /** @brief A `Dictionary<,>` is a .NET reference type. */
        [[nodiscard]] bool IsSerializedByReference() const noexcept override { return true; }

    protected:
        /**
         * @brief Writes the pair count and every pair in deterministic key order.
         *
         * @param output Per-file object-graph writer.
         * @param value The dictionary to serialize.
         */
        void Write(XnbWriter& output,
                   const std::unordered_map<TKey, TValue>& value) const override
        {
            output.RequireCollectionCount(value.size(), "DictionaryWriter");
            std::vector<const TKey*> keys;
            keys.reserve(value.size());
            for (const auto& entry : value) { keys.push_back(&entry.first); }
            std::sort(keys.begin(), keys.end(),
                      [](const TKey* left, const TKey* right) { return *left < *right; });

            output.WriteInt32(static_cast<std::int32_t>(value.size()));
            for (const TKey* key : keys)
            {
                if constexpr (Detail::IsSerializedReferenceType<TKey>::value)
                {
                    output.WriteObject(*key);
                }
                else
                {
                    output.WriteRawObject(*key);
                }
                const TValue& entry = value.at(*key);
                if constexpr (Detail::IsSerializedReferenceType<TValue>::value)
                {
                    output.WriteObject(entry);
                }
                else
                {
                    output.WriteRawObject(entry);
                }
            }
        }

    private:
        XnbReaderIdentity keyIdentity_;
        XnbReaderIdentity valueIdentity_;
    };

    /**
     * @brief Writer for `Nullable<T>`, serialized as a `Boolean` presence flag then the value.
     *
     * @tparam T The underlying value type.
     */
    template<typename T>
    class XnbNullableTypeWriter final : public XnbTypeWriter<std::optional<T>>
    {
    public:
        /**
         * @brief Creates a `Nullable<T>` writer.
         *
         * @param elementIdentity Reader identity of the underlying value.
         */
        explicit XnbNullableTypeWriter(XnbReaderIdentity elementIdentity)
            : elementIdentity_(std::move(elementIdentity))
        {
        }

        /** @brief Returns `NullableReader\`1[[<underlying target type>]]`. */
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = "Microsoft.Xna.Framework.Content.NullableReader`1";
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = "System.Nullable`1";
            identity.targetAssembly = XnbAssembly::Mscorlib;
            identity.genericArguments = {elementIdentity_};
            identity.evidence = XnbNameEvidence::DerivedRule;
            return identity;
        }

        /** @brief `Nullable<T>` is itself a value type. */
        [[nodiscard]] bool IsSerializedByReference() const noexcept override { return false; }

    protected:
        /**
         * @brief Writes the presence flag and, when present, the underlying value.
         *
         * @param output Per-file object-graph writer.
         * @param value The optional to serialize.
         */
        void Write(XnbWriter& output, const std::optional<T>& value) const override
        {
            output.WriteBoolean(value.has_value());
            if (value.has_value()) { output.WriteRawObject(*value); }
        }

    private:
        XnbReaderIdentity elementIdentity_;
    };

    /**
     * @brief Returns the reader identity of one built-in type, for use as a generic argument.
     *
     * Every collection writer needs its element's identity before any registry exists, so the
     * built-in identities are available as free functions rather than only through the registry.
     *
     * @tparam T The built-in type whose identity is requested.
     * @return The reader identity CNA writes for `T`.
     */
    template<typename T>
    [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity();

    /** @brief Returns the reader identity for `System.Boolean`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<bool>();
    /** @brief Returns the reader identity for `System.Byte`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint8_t>();
    /** @brief Returns the reader identity for `System.SByte`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::int8_t>();
    /** @brief Returns the reader identity for `System.Int16`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::int16_t>();
    /** @brief Returns the reader identity for `System.UInt16`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint16_t>();
    /** @brief Returns the reader identity for `System.Int32`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::int32_t>();
    /** @brief Returns the reader identity for `System.UInt32`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint32_t>();
    /** @brief Returns the reader identity for `System.Int64`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::int64_t>();
    /** @brief Returns the reader identity for `System.UInt64`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint64_t>();
    /** @brief Returns the reader identity for `System.Single`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<float>();
    /** @brief Returns the reader identity for `System.Double`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<double>();
    /** @brief Returns the reader identity for `System.Char`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<SharpRuntime::charcs>();
    /** @brief Returns the reader identity for `System.String`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<std::string>();
    /** @brief Returns the reader identity for `System.TimeSpan`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<System::TimeSpan>();
    /** @brief Returns the reader identity for `System.DateTime`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<System::DateTime>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Vector2`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector2>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Vector3`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector3>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Vector4`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector4>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Matrix`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Matrix>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Quaternion`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Quaternion>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Color`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Color>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Point`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Point>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Rectangle`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Rectangle>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Plane`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Plane>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.BoundingBox`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::BoundingBox>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.BoundingSphere`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::BoundingSphere>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Ray`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Ray>();
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.BoundingFrustum`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::BoundingFrustum>();
#if SHARP_RUNTIME_HAS_NATIVE_INT128
    /** @brief Returns the reader identity for `System.Decimal`. */
    template<> [[nodiscard]] XnbReaderIdentity XnbBuiltInReaderIdentity<System::Decimal>();
#endif
    /** @brief Returns the reader identity for `Microsoft.Xna.Framework.Curve`. */
    template<> [[nodiscard]] XnbReaderIdentity
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Curve>();

    /**
     * @brief Registers the sixteen `System.*` primitive writers.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInPrimitiveXnbWriters(XnbTypeWriterRegistry& registry);

    /**
     * @brief Registers the `Microsoft.Xna.Framework` value-type writers, `Curve` included.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInMathXnbWriters(XnbTypeWriterRegistry& registry);

    /**
     * @brief Registers the closed generic collection writers CNA's runtime reader registry
     *        already resolves.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInCollectionXnbWriters(XnbTypeWriterRegistry& registry);
}
