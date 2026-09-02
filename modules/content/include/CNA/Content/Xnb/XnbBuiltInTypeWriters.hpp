// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Xnb/XnbTypeWriter.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
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
#include "System/DateTime.hpp"
#include "System/Decimal.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Content::Xnb
{
    /**
     * @brief Registers every built-in primitive, system and math `.xnb` type writer
     *        (plans/plan_xnapipeline.md `XNAP-006`, `XNAP-007`, `XNAP-009`).
     *
     * Idempotent registration is deliberately *not* offered: a registry rejects a duplicate target
     * type, which is what catches a double registration during setup instead of silently keeping
     * whichever writer happened to arrive first.
     *
     * Asset-level writers (`Texture2D`, `SpriteFont`, `SoundEffect`, …) are registered separately
     * by `RegisterXnbAssetTypeWriters()`, so a producer that only serializes plain data does not
     * pull the graphics and audio content types in.
     *
     * @param registry Registry to configure before it is frozen.
     * @throws XnbWriteException when a built-in type is already registered.
     */
    void RegisterBuiltInXnbTypeWriters(XnbTypeWriterRegistry& registry);

    /**
     * @brief Returns the serialized .NET type name of a closed generic `List<T>`.
     *
     * @param elementTypeName Serialized element type name.
     * @return `System.Collections.Generic.List`1[[<element>]]`.
     */
    [[nodiscard]] std::string XnbListTypeName(const std::string& elementTypeName);

    /**
     * @brief Returns the serialized .NET type name of a closed generic array `T[]`.
     *
     * @param elementTypeName Serialized element type name.
     * @return `<element>[]`.
     */
    [[nodiscard]] std::string XnbArrayTypeName(const std::string& elementTypeName);

    /**
     * @brief Returns the serialized .NET type name of a closed generic `Dictionary<K,V>`.
     *
     * @param keyTypeName Serialized key type name.
     * @param valueTypeName Serialized value type name.
     * @return `System.Collections.Generic.Dictionary`2[[<key>],[<value>]]`.
     */
    [[nodiscard]] std::string XnbDictionaryTypeName(const std::string& keyTypeName,
                                                    const std::string& valueTypeName);

    /**
     * @brief Returns the serialized .NET type name of a closed generic `Nullable<T>`.
     *
     * @param valueTypeName Serialized value type name.
     * @return `System.Nullable`1[[<value>]]`.
     */
    [[nodiscard]] std::string XnbNullableTypeName(const std::string& valueTypeName);

    /**
     * @brief Returns the runtime reader name of a closed generic `ListReader<T>`.
     *
     * @param elementTypeName Serialized element type name.
     * @return `Microsoft.Xna.Framework.Content.ListReader`1[[<element>]]`.
     */
    [[nodiscard]] std::string XnbListReaderName(const std::string& elementTypeName);

    /**
     * @brief Returns the runtime reader name of a closed generic `ArrayReader<T>`.
     *
     * @param elementTypeName Serialized element type name.
     * @return `Microsoft.Xna.Framework.Content.ArrayReader`1[[<element>]]`.
     */
    [[nodiscard]] std::string XnbArrayReaderName(const std::string& elementTypeName);

    /**
     * @brief Returns the runtime reader name of a closed generic `DictionaryReader<K,V>`.
     *
     * @param keyTypeName Serialized key type name.
     * @param valueTypeName Serialized value type name.
     * @return `Microsoft.Xna.Framework.Content.DictionaryReader`2[[<key>],[<value>]]`.
     */
    [[nodiscard]] std::string XnbDictionaryReaderName(const std::string& keyTypeName,
                                                      const std::string& valueTypeName);

    /**
     * @brief Returns the runtime reader name of a closed generic `NullableReader<T>`.
     *
     * @param valueTypeName Serialized value type name.
     * @return `Microsoft.Xna.Framework.Content.NullableReader`1[[<value>]]`.
     */
    [[nodiscard]] std::string XnbNullableReaderName(const std::string& valueTypeName);

    /**
     * @brief Returns the runtime reader name of a closed generic `EnumReader<T>`.
     *
     * @param enumTypeName Serialized enum type name.
     * @return `Microsoft.Xna.Framework.Content.EnumReader`1[[<enum>]]`.
     */
    [[nodiscard]] std::string XnbEnumReaderName(const std::string& enumTypeName);

    /**
     * @brief Writes a homogeneous `List<T>` payload: a `UInt32` count, then each element.
     *
     * Elements use the format's `Object? T` form, so a value-typed element is raw and a
     * reference-typed element carries its own type identifier.
     *
     * @param output Writer positioned where the payload begins.
     * @param elementTypeName Serialized element type name.
     * @param elements Boxed elements in serialization order.
     */
    void WriteXnbListPayload(XnbWriter& output, const std::string& elementTypeName,
                             const std::vector<std::any>& elements);

    /**
     * @brief Serializes a closed generic collection type writer over one element type.
     *
     * Registering `List<Rectangle>` means registering both the list writer and, if it is not
     * present already, the element writer -- the file's type table must resolve in full before
     * any object is read, which is the same constraint the reader side documents.
     *
     * @param registry Registry to configure.
     * @param elementTypeName Serialized element type name, which must already be registered.
     * @throws XnbWriteException when the element type has no registered writer.
     */
    void RegisterXnbListWriter(XnbTypeWriterRegistry& registry, const std::string& elementTypeName);

    /**
     * @brief Registers a closed generic `T[]` array writer over one element type.
     *
     * @param registry Registry to configure.
     * @param elementTypeName Serialized element type name, which must already be registered.
     * @throws XnbWriteException when the element type has no registered writer.
     */
    void RegisterXnbArrayWriter(XnbTypeWriterRegistry& registry,
                                const std::string& elementTypeName);

    /**
     * @brief Registers a closed generic `Dictionary<K,V>` writer.
     *
     * @param registry Registry to configure.
     * @param keyTypeName Serialized key type name, which must already be registered.
     * @param valueTypeName Serialized value type name, which must already be registered.
     * @throws XnbWriteException when either type has no registered writer.
     */
    void RegisterXnbDictionaryWriter(XnbTypeWriterRegistry& registry,
                                     const std::string& keyTypeName,
                                     const std::string& valueTypeName);

    /**
     * @brief Registers a closed generic `Nullable<T>` writer.
     *
     * @param registry Registry to configure.
     * @param valueTypeName Serialized value type name, which must already be registered and must
     *        name a value type.
     * @throws XnbWriteException when the value type has no registered writer or is a reference type.
     */
    void RegisterXnbNullableWriter(XnbTypeWriterRegistry& registry,
                                   const std::string& valueTypeName);

    /**
     * @brief Registers an `EnumReader<T>`-backed writer for a 32-bit enum.
     *
     * The value is supplied as a `std::int32_t`, which is the underlying type of every enum the
     * XNA 4.0 content types use.
     *
     * @param registry Registry to configure.
     * @param enumTypeName Serialized enum type name, e.g. `"Microsoft.Xna.Framework.Graphics.SurfaceFormat"`.
     * @throws XnbWriteException when the enum type is already registered.
     */
    void RegisterXnbEnumWriter(XnbTypeWriterRegistry& registry, const std::string& enumTypeName);

    /** @brief A boxed `List<T>` payload: the element type plus its boxed elements. */
    struct XnbBoxedList
    {
        /** @brief Serialized element type name selecting the element writer. */
        std::string elementTypeName;

        /** @brief Boxed elements in serialization order. */
        std::vector<std::any> elements;
    };

    /** @brief A boxed `Dictionary<K,V>` payload in deterministic serialization order. */
    struct XnbBoxedDictionary
    {
        /** @brief Serialized key type name selecting the key writer. */
        std::string keyTypeName;

        /** @brief Serialized value type name selecting the value writer. */
        std::string valueTypeName;

        /** @brief Boxed key/value pairs, written in exactly this order. */
        std::vector<std::pair<std::any, std::any>> entries;
    };

    /** @brief A boxed `Nullable<T>` payload. */
    struct XnbBoxedNullable
    {
        /** @brief Serialized value type name selecting the value writer. */
        std::string valueTypeName;

        /** @brief The value, or absent for a null nullable. */
        std::optional<std::any> value;
    };

    /** @brief A boxed enum value with its serialized enum type name. */
    struct XnbBoxedEnum
    {
        /** @brief Serialized enum type name selecting the enum writer. */
        std::string enumTypeName;

        /** @brief The enum's 32-bit underlying value. */
        std::int32_t value = 0;
    };

    // -- XnbTypeKey specializations for the built-in types --

    /** @brief `System.Byte`. */
    template <> struct XnbTypeKey<std::uint8_t>
    { static std::string Name() { return "System.Byte"; } };

    /** @brief `System.SByte`. */
    template <> struct XnbTypeKey<std::int8_t>
    { static std::string Name() { return "System.SByte"; } };

    /** @brief `System.Int16`. */
    template <> struct XnbTypeKey<std::int16_t>
    { static std::string Name() { return "System.Int16"; } };

    /** @brief `System.UInt16`. */
    template <> struct XnbTypeKey<std::uint16_t>
    { static std::string Name() { return "System.UInt16"; } };

    /** @brief `System.Int32`. */
    template <> struct XnbTypeKey<std::int32_t>
    { static std::string Name() { return "System.Int32"; } };

    /** @brief `System.UInt32`. */
    template <> struct XnbTypeKey<std::uint32_t>
    { static std::string Name() { return "System.UInt32"; } };

    /** @brief `System.Int64`. */
    template <> struct XnbTypeKey<std::int64_t>
    { static std::string Name() { return "System.Int64"; } };

    /** @brief `System.UInt64`. */
    template <> struct XnbTypeKey<std::uint64_t>
    { static std::string Name() { return "System.UInt64"; } };

    /** @brief `System.Single`. */
    template <> struct XnbTypeKey<float>
    { static std::string Name() { return "System.Single"; } };

    /** @brief `System.Double`. */
    template <> struct XnbTypeKey<double>
    { static std::string Name() { return "System.Double"; } };

    /** @brief `System.Boolean`. */
    template <> struct XnbTypeKey<bool>
    { static std::string Name() { return "System.Boolean"; } };

    /** @brief `System.Char`. */
    template <> struct XnbTypeKey<char16_t>
    { static std::string Name() { return "System.Char"; } };

    /** @brief `System.String`. */
    template <> struct XnbTypeKey<std::string>
    { static std::string Name() { return "System.String"; } };

    /** @brief `Microsoft.Xna.Framework.Vector2`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Vector2>
    { static std::string Name() { return "Microsoft.Xna.Framework.Vector2"; } };

    /** @brief `Microsoft.Xna.Framework.Vector3`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Vector3>
    { static std::string Name() { return "Microsoft.Xna.Framework.Vector3"; } };

    /** @brief `Microsoft.Xna.Framework.Vector4`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Vector4>
    { static std::string Name() { return "Microsoft.Xna.Framework.Vector4"; } };

    /** @brief `Microsoft.Xna.Framework.Matrix`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Matrix>
    { static std::string Name() { return "Microsoft.Xna.Framework.Matrix"; } };

    /** @brief `Microsoft.Xna.Framework.Quaternion`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Quaternion>
    { static std::string Name() { return "Microsoft.Xna.Framework.Quaternion"; } };

    /** @brief `Microsoft.Xna.Framework.Color`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Color>
    { static std::string Name() { return "Microsoft.Xna.Framework.Color"; } };

    /** @brief `Microsoft.Xna.Framework.Plane`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Plane>
    { static std::string Name() { return "Microsoft.Xna.Framework.Plane"; } };

    /** @brief `Microsoft.Xna.Framework.Point`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Point>
    { static std::string Name() { return "Microsoft.Xna.Framework.Point"; } };

    /** @brief `Microsoft.Xna.Framework.Rectangle`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Rectangle>
    { static std::string Name() { return "Microsoft.Xna.Framework.Rectangle"; } };

    /** @brief `Microsoft.Xna.Framework.BoundingBox`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::BoundingBox>
    { static std::string Name() { return "Microsoft.Xna.Framework.BoundingBox"; } };

    /** @brief `Microsoft.Xna.Framework.BoundingSphere`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::BoundingSphere>
    { static std::string Name() { return "Microsoft.Xna.Framework.BoundingSphere"; } };

    /** @brief `Microsoft.Xna.Framework.BoundingFrustum`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::BoundingFrustum>
    { static std::string Name() { return "Microsoft.Xna.Framework.BoundingFrustum"; } };

    /** @brief `Microsoft.Xna.Framework.Ray`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Ray>
    { static std::string Name() { return "Microsoft.Xna.Framework.Ray"; } };

    /** @brief `Microsoft.Xna.Framework.Curve`. */
    template <> struct XnbTypeKey<Microsoft::Xna::Framework::Curve>
    { static std::string Name() { return "Microsoft.Xna.Framework.Curve"; } };

    /** @brief `System.TimeSpan`, written as the runtime type the reader produces. */
    template <> struct XnbTypeKey<System::TimeSpan>
    { static std::string Name() { return "System.TimeSpan"; } };

    /**
     * @brief `System.DateTime`, written as the runtime type the reader produces.
     *
     * The wire value packs a `DateTimeKind` into its top two bits. CNA's `System::DateTime` does
     * not carry a kind (a documented sharp-runtime limitation the reader already discards on the
     * way in), so the writer emits `Unspecified`. Reader and writer therefore remain exact
     * inverses of one another for every value either can represent.
     */
    template <> struct XnbTypeKey<System::DateTime>
    { static std::string Name() { return "System.DateTime"; } };

#if SHARP_RUNTIME_HAS_NATIVE_INT128
    /** @brief `System.Decimal`, written as the runtime type the reader produces. */
    template <> struct XnbTypeKey<System::Decimal>
    { static std::string Name() { return "System.Decimal"; } };
#endif
}
