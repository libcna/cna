// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace CNA::Content::Xnb
{
    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Curve;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Plane;
    using Microsoft::Xna::Framework::Point;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Ray;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;

    namespace
    {
        /**
         * @brief A stateless built-in writer whose payload is one small emit function.
         *
         * Every primitive, math and system type differs only in its target name, reader name and
         * a handful of field writes, so they share one class rather than thirty near-identical
         * ones. The emit function is a plain function pointer, so the writer holds no state and
         * is trivially reentrant.
         */
        template <typename T>
        class SimpleXnbTypeWriter final : public XnbTypeWriterT<T>
        {
        public:
            using Emit = void (*)(XnbWriter&, const T&);

            SimpleXnbTypeWriter(std::string targetTypeName, std::string readerName,
                                const bool isValueType, const Emit emit)
                : targetTypeName_(std::move(targetTypeName)),
                  readerName_(std::move(readerName)),
                  isValueType_(isValueType),
                  emit_(emit)
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override { return targetTypeName_; }
            [[nodiscard]] std::string RuntimeReaderName() const override { return readerName_; }
            [[nodiscard]] bool IsValueType() const override { return isValueType_; }

            void Write(XnbWriter& output, const T& value) const override { emit_(output, value); }

        private:
            std::string targetTypeName_;
            std::string readerName_;
            bool isValueType_ = true;
            Emit emit_ = nullptr;
        };

        template <typename T>
        void RegisterSimple(XnbTypeWriterRegistry& registry, const char* readerSuffix,
                            const bool isValueType, void (*emit)(XnbWriter&, const T&))
        {
            registry.Register(std::make_shared<const SimpleXnbTypeWriter<T>>(
                XnbTypeKey<T>::Name(),
                std::string("Microsoft.Xna.Framework.Content.") + readerSuffix, isValueType,
                emit));
        }

        void EmitVector2(XnbWriter& output, const Vector2& value)
        {
            output.WriteSingle(value.X);
            output.WriteSingle(value.Y);
        }

        void EmitVector3(XnbWriter& output, const Vector3& value)
        {
            output.WriteSingle(value.X);
            output.WriteSingle(value.Y);
            output.WriteSingle(value.Z);
        }

        void EmitVector4(XnbWriter& output, const Vector4& value)
        {
            output.WriteSingle(value.X);
            output.WriteSingle(value.Y);
            output.WriteSingle(value.Z);
            output.WriteSingle(value.W);
        }

        void EmitMatrix(XnbWriter& output, const Matrix& value)
        {
            output.WriteSingle(value.M11); output.WriteSingle(value.M12);
            output.WriteSingle(value.M13); output.WriteSingle(value.M14);
            output.WriteSingle(value.M21); output.WriteSingle(value.M22);
            output.WriteSingle(value.M23); output.WriteSingle(value.M24);
            output.WriteSingle(value.M31); output.WriteSingle(value.M32);
            output.WriteSingle(value.M33); output.WriteSingle(value.M34);
            output.WriteSingle(value.M41); output.WriteSingle(value.M42);
            output.WriteSingle(value.M43); output.WriteSingle(value.M44);
        }

        void EmitCurve(XnbWriter& output, const Curve& value)
        {
            output.WriteInt32(static_cast<std::int32_t>(value.getPreLoopProperty()));
            output.WriteInt32(static_cast<std::int32_t>(value.getPostLoopProperty()));
            const auto& keys = value.getKeysProperty();
            const int count = keys.getCountProperty();
            output.WriteCollectionCount(static_cast<std::size_t>(count < 0 ? 0 : count),
                                        "Microsoft.Xna.Framework.Content.CurveReader");
            for (int index = 0; index < count; ++index)
            {
                const auto& key = keys[index];
                output.WriteSingle(key.getPositionProperty());
                output.WriteSingle(key.getValueProperty());
                output.WriteSingle(key.getTangentInProperty());
                output.WriteSingle(key.getTangentOutProperty());
                output.WriteInt32(static_cast<std::int32_t>(key.getContinuityProperty()));
            }
        }

        /** @brief The `List<T>`/`T[]` writer, closed over one element type at registration time. */
        class CollectionXnbTypeWriter final : public XnbTypeWriterT<XnbBoxedList>
        {
        public:
            CollectionXnbTypeWriter(std::string targetTypeName, std::string readerName,
                                    std::string elementTypeName)
                : targetTypeName_(std::move(targetTypeName)),
                  readerName_(std::move(readerName)),
                  elementTypeName_(std::move(elementTypeName))
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override { return targetTypeName_; }
            [[nodiscard]] std::string RuntimeReaderName() const override { return readerName_; }
            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const XnbBoxedList& value) const override
            {
                if (value.elementTypeName != elementTypeName_)
                {
                    throw XnbWriteException(
                        "'" + targetTypeName_ + "' received elements declared as '" +
                        value.elementTypeName + "'.");
                }
                WriteXnbListPayload(output, elementTypeName_, value.elements);
            }

        private:
            std::string targetTypeName_;
            std::string readerName_;
            std::string elementTypeName_;
        };

        /** @brief The `Dictionary<K,V>` writer, closed over one key and value type. */
        class DictionaryXnbTypeWriter final : public XnbTypeWriterT<XnbBoxedDictionary>
        {
        public:
            DictionaryXnbTypeWriter(std::string keyTypeName, std::string valueTypeName)
                : keyTypeName_(std::move(keyTypeName)), valueTypeName_(std::move(valueTypeName))
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbDictionaryTypeName(keyTypeName_, valueTypeName_);
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return XnbDictionaryReaderName(keyTypeName_, valueTypeName_);
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const XnbBoxedDictionary& value) const override
            {
                if (value.keyTypeName != keyTypeName_ || value.valueTypeName != valueTypeName_)
                {
                    throw XnbWriteException(
                        "'" + TargetTypeName() + "' received entries declared as <" +
                        value.keyTypeName + ", " + value.valueTypeName + ">.");
                }
                output.WriteCollectionCount(value.entries.size(), TargetTypeName());
                for (const auto& [key, item] : value.entries)
                {
                    output.WriteValueOrObject(keyTypeName_, key);
                    output.WriteValueOrObject(valueTypeName_, item);
                }
            }

        private:
            std::string keyTypeName_;
            std::string valueTypeName_;
        };

        /** @brief The `Nullable<T>` writer, closed over one value type. */
        class NullableXnbTypeWriter final : public XnbTypeWriterT<XnbBoxedNullable>
        {
        public:
            explicit NullableXnbTypeWriter(std::string valueTypeName)
                : valueTypeName_(std::move(valueTypeName))
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbNullableTypeName(valueTypeName_);
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return XnbNullableReaderName(valueTypeName_);
            }

            // Nullable<T> is itself a value type in .NET, so it is written raw wherever the format
            // says Object? T -- the "has value" boolean is the payload's own first byte.
            [[nodiscard]] bool IsValueType() const override { return true; }

            void Write(XnbWriter& output, const XnbBoxedNullable& value) const override
            {
                if (value.valueTypeName != valueTypeName_)
                {
                    throw XnbWriteException(
                        "'" + TargetTypeName() + "' received a value declared as '" +
                        value.valueTypeName + "'.");
                }
                output.WriteBoolean(value.value.has_value());
                if (value.value.has_value())
                {
                    output.WriteRawObject(valueTypeName_, *value.value);
                }
            }

        private:
            std::string valueTypeName_;
        };

        /** @brief The 32-bit `EnumReader<T>` writer, closed over one enum type. */
        class EnumXnbTypeWriter final : public XnbTypeWriterT<XnbBoxedEnum>
        {
        public:
            explicit EnumXnbTypeWriter(std::string enumTypeName)
                : enumTypeName_(std::move(enumTypeName))
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override { return enumTypeName_; }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return XnbEnumReaderName(enumTypeName_);
            }

            [[nodiscard]] bool IsValueType() const override { return true; }

            void Write(XnbWriter& output, const XnbBoxedEnum& value) const override
            {
                if (value.enumTypeName != enumTypeName_)
                {
                    throw XnbWriteException(
                        "'" + enumTypeName_ + "' received a value declared as '" +
                        value.enumTypeName + "'.");
                }
                output.WriteInt32(value.value);
            }

        private:
            std::string enumTypeName_;
        };
    }

    std::string XnbListTypeName(const std::string& elementTypeName)
    {
        return "System.Collections.Generic.List`1[[" + elementTypeName + "]]";
    }

    std::string XnbArrayTypeName(const std::string& elementTypeName)
    {
        return elementTypeName + "[]";
    }

    std::string XnbDictionaryTypeName(const std::string& keyTypeName,
                                      const std::string& valueTypeName)
    {
        return "System.Collections.Generic.Dictionary`2[[" + keyTypeName + "],[" + valueTypeName +
               "]]";
    }

    std::string XnbNullableTypeName(const std::string& valueTypeName)
    {
        return "System.Nullable`1[[" + valueTypeName + "]]";
    }

    std::string XnbListReaderName(const std::string& elementTypeName)
    {
        return "Microsoft.Xna.Framework.Content.ListReader`1[[" + elementTypeName + "]]";
    }

    std::string XnbArrayReaderName(const std::string& elementTypeName)
    {
        return "Microsoft.Xna.Framework.Content.ArrayReader`1[[" + elementTypeName + "]]";
    }

    std::string XnbDictionaryReaderName(const std::string& keyTypeName,
                                        const std::string& valueTypeName)
    {
        return "Microsoft.Xna.Framework.Content.DictionaryReader`2[[" + keyTypeName + "],[" +
               valueTypeName + "]]";
    }

    std::string XnbNullableReaderName(const std::string& valueTypeName)
    {
        return "Microsoft.Xna.Framework.Content.NullableReader`1[[" + valueTypeName + "]]";
    }

    std::string XnbEnumReaderName(const std::string& enumTypeName)
    {
        return "Microsoft.Xna.Framework.Content.EnumReader`1[[" + enumTypeName + "]]";
    }

    void WriteXnbListPayload(XnbWriter& output, const std::string& elementTypeName,
                             const std::vector<std::any>& elements)
    {
        output.WriteCollectionCount(elements.size(), elementTypeName);
        for (const std::any& element : elements)
        {
            output.WriteValueOrObject(elementTypeName, element);
        }
    }

    void RegisterXnbListWriter(XnbTypeWriterRegistry& registry, const std::string& elementTypeName)
    {
        (void)registry.Resolve(elementTypeName);
        registry.Register(std::make_shared<const CollectionXnbTypeWriter>(
            XnbListTypeName(elementTypeName), XnbListReaderName(elementTypeName), elementTypeName));
    }

    void RegisterXnbArrayWriter(XnbTypeWriterRegistry& registry, const std::string& elementTypeName)
    {
        (void)registry.Resolve(elementTypeName);
        registry.Register(std::make_shared<const CollectionXnbTypeWriter>(
            XnbArrayTypeName(elementTypeName), XnbArrayReaderName(elementTypeName),
            elementTypeName));
    }

    void RegisterXnbDictionaryWriter(XnbTypeWriterRegistry& registry,
                                     const std::string& keyTypeName,
                                     const std::string& valueTypeName)
    {
        (void)registry.Resolve(keyTypeName);
        (void)registry.Resolve(valueTypeName);
        registry.Register(
            std::make_shared<const DictionaryXnbTypeWriter>(keyTypeName, valueTypeName));
    }

    void RegisterXnbNullableWriter(XnbTypeWriterRegistry& registry,
                                   const std::string& valueTypeName)
    {
        const auto valueWriter = registry.Resolve(valueTypeName);
        if (!valueWriter->IsValueType())
        {
            throw XnbWriteException(
                "Nullable<" + valueTypeName +
                "> is not serializable: .NET constrains Nullable<T> to a value type.");
        }
        registry.Register(std::make_shared<const NullableXnbTypeWriter>(valueTypeName));
    }

    void RegisterXnbEnumWriter(XnbTypeWriterRegistry& registry, const std::string& enumTypeName)
    {
        registry.Register(std::make_shared<const EnumXnbTypeWriter>(enumTypeName));
    }

    void RegisterBuiltInXnbTypeWriters(XnbTypeWriterRegistry& registry)
    {
        RegisterSimple<std::uint8_t>(registry, "ByteReader", true,
            [](XnbWriter& out, const std::uint8_t& v) { out.WriteByte(v); });
        RegisterSimple<std::int8_t>(registry, "SByteReader", true,
            [](XnbWriter& out, const std::int8_t& v) { out.WriteSByte(v); });
        RegisterSimple<std::int16_t>(registry, "Int16Reader", true,
            [](XnbWriter& out, const std::int16_t& v) { out.WriteInt16(v); });
        RegisterSimple<std::uint16_t>(registry, "UInt16Reader", true,
            [](XnbWriter& out, const std::uint16_t& v) { out.WriteUInt16(v); });
        RegisterSimple<std::int32_t>(registry, "Int32Reader", true,
            [](XnbWriter& out, const std::int32_t& v) { out.WriteInt32(v); });
        RegisterSimple<std::uint32_t>(registry, "UInt32Reader", true,
            [](XnbWriter& out, const std::uint32_t& v) { out.WriteUInt32(v); });
        RegisterSimple<std::int64_t>(registry, "Int64Reader", true,
            [](XnbWriter& out, const std::int64_t& v) { out.WriteInt64(v); });
        RegisterSimple<std::uint64_t>(registry, "UInt64Reader", true,
            [](XnbWriter& out, const std::uint64_t& v) { out.WriteUInt64(v); });
        RegisterSimple<float>(registry, "SingleReader", true,
            [](XnbWriter& out, const float& v) { out.WriteSingle(v); });
        RegisterSimple<double>(registry, "DoubleReader", true,
            [](XnbWriter& out, const double& v) { out.WriteDouble(v); });
        RegisterSimple<bool>(registry, "BooleanReader", true,
            [](XnbWriter& out, const bool& v) { out.WriteBoolean(v); });
        RegisterSimple<char16_t>(registry, "CharReader", true,
            [](XnbWriter& out, const char16_t& v) { out.WriteChar(v); });
        RegisterSimple<std::string>(registry, "StringReader", false,
            [](XnbWriter& out, const std::string& v) { out.WriteString(v); });

        RegisterSimple<System::TimeSpan>(registry, "TimeSpanReader", true,
            [](XnbWriter& out, const System::TimeSpan& v)
            {
                out.WriteInt64(static_cast<std::int64_t>(v.getTicksProperty()));
            });
        RegisterSimple<System::DateTime>(registry, "DateTimeReader", true,
            [](XnbWriter& out, const System::DateTime& v)
            {
                // The top two bits carry DateTimeKind; CNA's DateTime does not track one, and the
                // reader discards it, so Unspecified (zero) keeps the two sides exact inverses.
                out.WriteUInt64(static_cast<std::uint64_t>(v.getTicksProperty()));
            });
#if SHARP_RUNTIME_HAS_NATIVE_INT128
        RegisterSimple<System::Decimal>(registry, "DecimalReader", true,
            [](XnbWriter& out, const System::Decimal& v)
            {
                SharpRuntime::intcs lo = 0;
                SharpRuntime::intcs mid = 0;
                SharpRuntime::intcs hi = 0;
                SharpRuntime::intcs flags = 0;
                System::Decimal::GetBits(v, lo, mid, hi, flags);
                out.WriteInt32(static_cast<std::int32_t>(lo));
                out.WriteInt32(static_cast<std::int32_t>(mid));
                out.WriteInt32(static_cast<std::int32_t>(hi));
                out.WriteInt32(static_cast<std::int32_t>(flags));
            });
#endif

        RegisterSimple<Vector2>(registry, "Vector2Reader", true, EmitVector2);
        RegisterSimple<Vector3>(registry, "Vector3Reader", true, EmitVector3);
        RegisterSimple<Vector4>(registry, "Vector4Reader", true, EmitVector4);
        RegisterSimple<Matrix>(registry, "MatrixReader", true, EmitMatrix);
        RegisterSimple<Quaternion>(registry, "QuaternionReader", true,
            [](XnbWriter& out, const Quaternion& v)
            {
                out.WriteSingle(v.X); out.WriteSingle(v.Y);
                out.WriteSingle(v.Z); out.WriteSingle(v.W);
            });
        RegisterSimple<Color>(registry, "ColorReader", true,
            [](XnbWriter& out, const Color& v)
            {
                out.WriteByte(v.getRProperty());
                out.WriteByte(v.getGProperty());
                out.WriteByte(v.getBProperty());
                out.WriteByte(v.getAProperty());
            });
        RegisterSimple<Plane>(registry, "PlaneReader", true,
            [](XnbWriter& out, const Plane& v) { EmitVector3(out, v.Normal); out.WriteSingle(v.D); });
        RegisterSimple<Point>(registry, "PointReader", true,
            [](XnbWriter& out, const Point& v) { out.WriteInt32(v.X); out.WriteInt32(v.Y); });
        RegisterSimple<Rectangle>(registry, "RectangleReader", true,
            [](XnbWriter& out, const Rectangle& v)
            {
                out.WriteInt32(v.X); out.WriteInt32(v.Y);
                out.WriteInt32(v.Width); out.WriteInt32(v.Height);
            });
        RegisterSimple<BoundingBox>(registry, "BoundingBoxReader", true,
            [](XnbWriter& out, const BoundingBox& v)
            {
                EmitVector3(out, v.Min);
                EmitVector3(out, v.Max);
            });
        RegisterSimple<BoundingSphere>(registry, "BoundingSphereReader", true,
            [](XnbWriter& out, const BoundingSphere& v)
            {
                EmitVector3(out, v.Center);
                out.WriteSingle(v.Radius);
            });
        RegisterSimple<BoundingFrustum>(registry, "BoundingFrustumReader", false,
            [](XnbWriter& out, const BoundingFrustum& v) { EmitMatrix(out, v.getMatrixProperty()); });
        RegisterSimple<Ray>(registry, "RayReader", true,
            [](XnbWriter& out, const Ray& v)
            {
                EmitVector3(out, v.Position);
                EmitVector3(out, v.Direction);
            });
        RegisterSimple<Curve>(registry, "CurveReader", false, EmitCurve);
    }
}
