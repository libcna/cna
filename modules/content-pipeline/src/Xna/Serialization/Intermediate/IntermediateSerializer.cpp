// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

#include <array>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureReferenceDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/AnimationChannelSerializer.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/DateTime.hpp"
#include "System/Decimal.hpp"
#include "System/FormatException.hpp"
#include "System/OverflowException.hpp"
#include "System/TimeSpan.hpp"
#include "System/Xml/XmlConvert.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    // -----------------------------------------------------------------------------------------
    // Registry
    // -----------------------------------------------------------------------------------------
    namespace
    {
        struct Registry
        {
            std::mutex mutex;
            std::vector<std::unique_ptr<ContentTypeSerializerBase>> owned;
            std::unordered_map<System::Type, ContentTypeSerializerBase*> byType;
            std::unordered_map<std::type_index, ContentTypeSerializerBase*> byCarrier;
            std::unordered_map<std::string, ContentTypeSerializerBase*> byName;
            std::unordered_set<std::string> knownNames;
            bool builtInsRegistered = false;
        };

        Registry& TheRegistry()
        {
            static Registry registry;
            return registry;
        }

        constexpr std::string_view kWhitespace = " \t\n\r";

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && kWhitespace.find(text.front()) != std::string_view::npos)
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && kWhitespace.find(text.back()) != std::string_view::npos)
            {
                text.remove_suffix(1);
            }
            return text;
        }

        [[noreturn]] void ThrowBadFormat() { throw System::FormatException("Input string was not in a correct format."); }

        // -------------------------------------------------------------------------------------
        // .NET Framework 4.0 number formatting. XNA runs on the .NET Framework, whose "R"
        // format tries the general format with 7 (float) or 15 (double) significant digits and
        // falls back to 9 or 17 when the shorter text does not round-trip; the exponent is
        // spelled "E+300" / "E-05". .NET Core's shortest round-trip form -- which sharp-runtime's
        // Single::ToString follows -- writes 0.33333334 where XNA writes 0.333333343.
        // -------------------------------------------------------------------------------------
        std::string FormatGeneral(double value, int precision)
        {
            std::array<char, 64> buffer{};
            const std::to_chars_result result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, precision);
            std::string text(buffer.data(), result.ptr);
            const std::size_t e = text.find('e');
            if (e != std::string::npos)
            {
                const std::string mantissa = text.substr(0, e);
                const char sign = text[e + 1];
                std::string digits = text.substr(e + 2);
                while (digits.size() > 2 && digits.front() == '0')
                {
                    digits.erase(digits.begin());
                }
                text = mantissa + "E" + sign + digits;
            }
            return text;
        }

        std::string FormatSingleR(float value)
        {
            if (std::isnan(value))
            {
                return "NaN";
            }
            if (std::isinf(value))
            {
                return value < 0 ? "-INF" : "INF";
            }
            std::string text = FormatGeneral(static_cast<double>(value), 7);
            if (std::strtof(text.c_str(), nullptr) == value)
            {
                return text;
            }
            return FormatGeneral(static_cast<double>(value), 9);
        }

        std::string FormatDoubleR(double value)
        {
            if (std::isnan(value))
            {
                return "NaN";
            }
            if (std::isinf(value))
            {
                return value < 0 ? "-INF" : "INF";
            }
            std::string text = FormatGeneral(value, 15);
            if (std::strtod(text.c_str(), nullptr) == value)
            {
                return text;
            }
            return FormatGeneral(value, 17);
        }

        bool IsFloatGrammar(std::string_view text)
        {
            std::size_t i = 0;
            if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            {
                ++i;
            }
            std::size_t digits = 0;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
            {
                ++i;
                ++digits;
            }
            if (i < text.size() && text[i] == '.')
            {
                ++i;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                {
                    ++i;
                    ++digits;
                }
            }
            if (digits == 0)
            {
                return false;
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
            {
                ++i;
                if (i < text.size() && (text[i] == '+' || text[i] == '-'))
                {
                    ++i;
                }
                std::size_t exponentDigits = 0;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                {
                    ++i;
                    ++exponentDigits;
                }
                if (exponentDigits == 0)
                {
                    return false;
                }
            }
            return i == text.size();
        }

        double ParseDoubleText(const std::string& raw, bool single)
        {
            const std::string_view text = Trim(raw);
            if (text == "INF" || text == "Infinity")
            {
                return std::numeric_limits<double>::infinity();
            }
            if (text == "-INF" || text == "-Infinity")
            {
                return -std::numeric_limits<double>::infinity();
            }
            if (text == "NaN")
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (!IsFloatGrammar(text))
            {
                ThrowBadFormat();
            }
            const std::string owned(text);
            const double value = std::strtod(owned.c_str(), nullptr);
            if (single ? std::fabs(value) > static_cast<double>(FLT_MAX) : std::isinf(value))
            {
                throw System::OverflowException(single ? "Value was either too large or too small for a Single."
                                                       : "Value was either too large or too small for a Double.");
            }
            return value;
        }

        float ParseSingleText(const std::string& raw) { return static_cast<float>(ParseDoubleText(raw, true)); }

        template<typename T>
        T ParseIntegerText(const std::string& raw, const char* overflowMessage)
        {
            const std::string_view text = Trim(raw);
            std::size_t i = 0;
            bool negative = false;
            if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            {
                if constexpr (std::is_unsigned_v<T>)
                {
                    ThrowBadFormat();
                }
                negative = text[i] == '-';
                ++i;
            }
            if (i >= text.size())
            {
                ThrowBadFormat();
            }
            std::uint64_t magnitude = 0;
            bool overflow = false;
            for (; i < text.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(text[i])))
                {
                    ThrowBadFormat();
                }
                const std::uint64_t digit = static_cast<std::uint64_t>(text[i] - '0');
                if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
                {
                    overflow = true;
                }
                else
                {
                    magnitude = magnitude * 10 + digit;
                }
            }
            if (!overflow)
            {
                if (negative)
                {
                    const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<T>::max()) + 1;
                    if (magnitude > limit)
                    {
                        overflow = true;
                    }
                }
                else if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                {
                    overflow = true;
                }
            }
            if (overflow)
            {
                throw System::OverflowException(overflowMessage);
            }
            if (negative)
            {
                return static_cast<T>(-static_cast<std::int64_t>(magnitude - 1) - 1);
            }
            return static_cast<T>(magnitude);
        }

        std::string FormatFloats(std::initializer_list<float> values)
        {
            std::string text;
            for (float value : values)
            {
                if (!text.empty())
                {
                    text += ' ';
                }
                text += FormatSingleR(value);
            }
            return text;
        }

        void RequireTokenCount(std::span<const std::string> tokens, std::size_t expected)
        {
            if (tokens.size() < expected)
            {
                throw InvalidContentException("XML does not have enough entries in space-separated list.");
            }
            if (tokens.size() > expected)
            {
                throw InvalidContentException("XML has too many entries in the space-separated list.");
            }
        }

        std::string Utf16UnitToUtf8(char16_t unit)
        {
            std::string out;
            if (unit < 0x80)
            {
                out += static_cast<char>(unit);
            }
            else if (unit < 0x800)
            {
                out += static_cast<char>(0xC0 | (unit >> 6));
                out += static_cast<char>(0x80 | (unit & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xE0 | (unit >> 12));
                out += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (unit & 0x3F));
            }
            return out;
        }

        char16_t Utf8ToSingleUtf16Unit(const std::string& text)
        {
            const auto fail = [] { throw System::FormatException("String must be exactly one character long."); };
            if (text.empty())
            {
                fail();
            }
            const unsigned char lead = static_cast<unsigned char>(text[0]);
            std::size_t length = 1;
            std::uint32_t codePoint = lead;
            if (lead >= 0xF0)
            {
                fail(); // outside the BMP: two UTF-16 units
            }
            else if (lead >= 0xE0)
            {
                length = 3;
                codePoint = lead & 0x0F;
            }
            else if (lead >= 0xC0)
            {
                length = 2;
                codePoint = lead & 0x1F;
            }
            if (text.size() != length)
            {
                fail();
            }
            for (std::size_t i = 1; i < length; ++i)
            {
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
            }
            return static_cast<char16_t>(codePoint);
        }

        // -------------------------------------------------------------------------------------
        // Text serializers: one whitespace-separated token per value unless noted.
        // -------------------------------------------------------------------------------------
        template<typename T, typename Traits>
        class TextSerializer final : public ContentTypeSerializer<T>
        {
        public:
            TextSerializer() : ContentTypeSerializer<T>(Traits::XmlName()) {}

            [[nodiscard]] std::size_t PackedTokenCount() const noexcept override { return Traits::Tokens; }

            // A `string` is a reference in .NET, so XNA writes and accepts Null="true" for it; a
            // std::string has no null, so reading one yields the empty string (recorded divergence).
            [[nodiscard]] bool IsNullable() const noexcept override { return Traits::NullReadsAsDefault; }

            // An optional member whose value is an empty string is omitted, exactly as an optional
            // empty collection is: measured on ContentItem's own Name, which XNA writes only once
            // it has been given one (tests/reference/xna40/graphics, material/serialize_with_name
            // versus material/serialize_basic).
            [[nodiscard]] bool ObjectIsEmpty(const ContentObject& value) const override
            {
                if constexpr (std::is_same_v<T, std::string>)
                {
                    return Unbox<T>(value).empty();
                }
                else
                {
                    (void)value;
                    return false;
                }
            }

            [[nodiscard]] ContentObject NullObject() const override
            {
                if constexpr (Traits::NullReadsAsDefault)
                {
                    return Box<T>(T{});
                }
                else
                {
                    return ContentObject{};
                }
            }

            [[nodiscard]] std::string FormatPacked(const ContentObject& value) const override
            {
                return Traits::Format(Unbox<T>(value));
            }

            [[nodiscard]] ContentObject ParsePacked(std::span<const std::string> tokens) const override
            {
                return Box<T>(Traits::ParseTokens(tokens));
            }

        protected:
            void Serialize(IntermediateWriter& output, const T& value, const ContentSerializerAttribute& format) override
            {
                (void)format;
                const std::string text = Traits::Format(value);
                if (!text.empty() || Traits::WriteEmptyText)
                {
                    output.getXmlProperty().WriteString(text);
                }
            }

            [[nodiscard]] T Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                        T existingInstance) override
            {
                (void)existingInstance;
                return Traits::Parse(input.ReadText(format));
            }
        };

        template<std::size_t N>
        struct PackedTraits
        {
            static constexpr std::size_t Tokens = N;
            static constexpr bool WriteEmptyText = false;
            static constexpr bool NullReadsAsDefault = false;
        };

        template<typename T>
        struct ScalarTraits : PackedTraits<1>
        {
            static T ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, 1);
                return static_cast<T>(std::stoll(tokens[0]));
            }
        };

#define CNA_INTEGER_TRAITS(name, type, xmlName, message)                                         \
    struct name : PackedTraits<1>                                                                 \
    {                                                                                             \
        static std::string XmlName() { return xmlName; }                                         \
        static std::string Format(type value) { return std::to_string(value); }                  \
        static type Parse(const std::string& text) { return ParseIntegerText<type>(text, message); } \
        static type ParseTokens(std::span<const std::string> tokens)                             \
        {                                                                                         \
            RequireTokenCount(tokens, 1);                                                         \
            return Parse(tokens[0]);                                                              \
        }                                                                                         \
    }

        CNA_INTEGER_TRAITS(SByteTraits, std::int8_t, "sbyte", "Value was either too large or too small for a signed byte.");
        CNA_INTEGER_TRAITS(ByteTraits, std::uint8_t, "byte", "Value was either too large or too small for an unsigned byte.");
        CNA_INTEGER_TRAITS(Int16Traits, std::int16_t, "short", "Value was either too large or too small for an Int16.");
        CNA_INTEGER_TRAITS(UInt16Traits, std::uint16_t, "ushort", "Value was either too large or too small for a UInt16.");
        CNA_INTEGER_TRAITS(Int32Traits, std::int32_t, "int", "Value was either too large or too small for an Int32.");
        CNA_INTEGER_TRAITS(UInt32Traits, std::uint32_t, "uint", "Value was either too large or too small for a UInt32.");
        CNA_INTEGER_TRAITS(Int64Traits, std::int64_t, "long", "Value was either too large or too small for an Int64.");
        CNA_INTEGER_TRAITS(UInt64Traits, std::uint64_t, "ulong", "Value was either too large or too small for a UInt64.");
#undef CNA_INTEGER_TRAITS

        struct BoolTraits : PackedTraits<1>
        {
            static std::string XmlName() { return "bool"; }
            static std::string Format(bool value) { return value ? "true" : "false"; }
            static bool Parse(const std::string& text)
            {
                const std::string_view trimmed = Trim(text);
                if (trimmed == "true" || trimmed == "1")
                {
                    return true;
                }
                if (trimmed == "false" || trimmed == "0")
                {
                    return false;
                }
                throw System::FormatException("The string '" + std::string(trimmed) + "' is not a valid Boolean value.");
            }
            static bool ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, 1);
                return Parse(tokens[0]);
            }
        };

        struct SingleTraits : PackedTraits<1>
        {
            static std::string XmlName() { return "float"; }
            static std::string Format(float value) { return FormatSingleR(value); }
            static float Parse(const std::string& text) { return ParseSingleText(text); }
            static float ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, 1);
                return Parse(tokens[0]);
            }
        };

        struct DoubleTraits : PackedTraits<1>
        {
            static std::string XmlName() { return "double"; }
            static std::string Format(double value) { return FormatDoubleR(value); }
            static double Parse(const std::string& text) { return ParseDoubleText(text, false); }
            static double ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, 1);
                return Parse(tokens[0]);
            }
        };

        struct UnpackedTraits
        {
            static constexpr std::size_t Tokens = 0;
            static constexpr bool WriteEmptyText = false;
            static constexpr bool NullReadsAsDefault = false;
        };

        struct StringTraits : UnpackedTraits
        {
            static constexpr bool WriteEmptyText = true;
            static constexpr bool NullReadsAsDefault = true;
            static std::string XmlName() { return "string"; }
            static std::string Format(const std::string& value) { return value; }
            static std::string Parse(const std::string& text) { return text; }
            static std::string ParseTokens(std::span<const std::string> tokens) { return tokens.empty() ? std::string() : tokens[0]; }
        };

        struct CharTraits : UnpackedTraits
        {
            static std::string XmlName() { return std::string(); }
            static std::string Format(char16_t value) { return Utf16UnitToUtf8(value); }
            static char16_t Parse(const std::string& text) { return Utf8ToSingleUtf16Unit(text); }
            static char16_t ParseTokens(std::span<const std::string> tokens) { return Parse(tokens.empty() ? std::string() : tokens[0]); }
        };

        struct DecimalTraits : UnpackedTraits
        {
            static std::string XmlName() { return std::string(); }
            static std::string Format(const System::Decimal& value) { return System::Xml::XmlConvert::ToString(value); }
            static System::Decimal Parse(const std::string& text) { return System::Xml::XmlConvert::ToDecimal(std::string(Trim(text))); }
            static System::Decimal ParseTokens(std::span<const std::string> tokens) { return Parse(tokens.empty() ? std::string() : tokens[0]); }
        };

        struct TimeSpanTraits : UnpackedTraits
        {
            static std::string XmlName() { return std::string(); }
            static std::string Format(const System::TimeSpan& value) { return System::Xml::XmlConvert::ToString(value); }
            static System::TimeSpan Parse(const std::string& text) { return System::Xml::XmlConvert::ToTimeSpan(std::string(Trim(text))); }
            static System::TimeSpan ParseTokens(std::span<const std::string> tokens) { return Parse(tokens.empty() ? std::string() : tokens[0]); }
        };

        struct DateTimeTraits : UnpackedTraits
        {
            static std::string XmlName() { return std::string(); }
            static std::string Format(const System::DateTime& value) { return System::Xml::XmlConvert::ToString(value); }
            static System::DateTime Parse(const std::string& text) { return System::Xml::XmlConvert::ToDateTime(std::string(Trim(text))); }
            static System::DateTime ParseTokens(std::span<const std::string> tokens) { return Parse(tokens.empty() ? std::string() : tokens[0]); }
        };

        template<typename T, std::size_t N, typename Derived>
        struct FloatPackTraits : PackedTraits<N>
        {
            static std::string XmlName() { return std::string(); }
            static T Parse(const std::string& text)
            {
                const std::vector<std::string> tokens = IntermediateReader::SplitTokens(text);
                return ParseTokens(tokens);
            }
            static T ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, N);
                std::array<float, N> values{};
                for (std::size_t i = 0; i < N; ++i)
                {
                    values[i] = ParseSingleText(tokens[i]);
                }
                return Derived::FromComponents(values);
            }
        };

        struct Vector2Traits : FloatPackTraits<Vector2, 2, Vector2Traits>
        {
            static std::string Format(const Vector2& v) { return FormatFloats({v.X, v.Y}); }
            static Vector2 FromComponents(const std::array<float, 2>& c) { return Vector2(c[0], c[1]); }
        };

        struct Vector3Traits : FloatPackTraits<Vector3, 3, Vector3Traits>
        {
            static std::string Format(const Vector3& v) { return FormatFloats({v.X, v.Y, v.Z}); }
            static Vector3 FromComponents(const std::array<float, 3>& c) { return Vector3(c[0], c[1], c[2]); }
        };

        struct Vector4Traits : FloatPackTraits<Vector4, 4, Vector4Traits>
        {
            static std::string Format(const Vector4& v) { return FormatFloats({v.X, v.Y, v.Z, v.W}); }
            static Vector4 FromComponents(const std::array<float, 4>& c) { return Vector4(c[0], c[1], c[2], c[3]); }
        };

        struct QuaternionTraits : FloatPackTraits<Quaternion, 4, QuaternionTraits>
        {
            static std::string Format(const Quaternion& v) { return FormatFloats({v.X, v.Y, v.Z, v.W}); }
            static Quaternion FromComponents(const std::array<float, 4>& c) { return Quaternion(c[0], c[1], c[2], c[3]); }
        };

        struct PlaneTraits : FloatPackTraits<Plane, 4, PlaneTraits>
        {
            static std::string Format(const Plane& v) { return FormatFloats({v.Normal.X, v.Normal.Y, v.Normal.Z, v.D}); }
            static Plane FromComponents(const std::array<float, 4>& c) { return Plane(c[0], c[1], c[2], c[3]); }
        };

        struct MatrixTraits : FloatPackTraits<Matrix, 16, MatrixTraits>
        {
            static std::string Format(const Matrix& m)
            {
                return FormatFloats({m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24, m.M31, m.M32, m.M33, m.M34,
                                     m.M41, m.M42, m.M43, m.M44});
            }
            static Matrix FromComponents(const std::array<float, 16>& c)
            {
                return Matrix(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13],
                              c[14], c[15]);
            }
        };

        template<typename T, std::size_t N, typename Derived>
        struct IntPackTraits : PackedTraits<N>
        {
            static std::string XmlName() { return std::string(); }
            static T Parse(const std::string& text)
            {
                const std::vector<std::string> tokens = IntermediateReader::SplitTokens(text);
                return ParseTokens(tokens);
            }
            static T ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, N);
                std::array<std::int32_t, N> values{};
                for (std::size_t i = 0; i < N; ++i)
                {
                    values[i] = ParseIntegerText<std::int32_t>(tokens[i], "Value was either too large or too small for an Int32.");
                }
                return Derived::FromComponents(values);
            }
        };

        struct RectangleTraits : IntPackTraits<Rectangle, 4, RectangleTraits>
        {
            static std::string Format(const Rectangle& r)
            {
                return std::to_string(r.X) + " " + std::to_string(r.Y) + " " + std::to_string(r.Width) + " " +
                       std::to_string(r.Height);
            }
            static Rectangle FromComponents(const std::array<std::int32_t, 4>& c) { return Rectangle(c[0], c[1], c[2], c[3]); }
        };

        struct PointTraits : IntPackTraits<Point, 2, PointTraits>
        {
            static std::string Format(const Point& p) { return std::to_string(p.X) + " " + std::to_string(p.Y); }
            static Point FromComponents(const std::array<std::int32_t, 2>& c) { return Point(c[0], c[1]); }
        };

        struct ColorTraits : PackedTraits<1>
        {
            static std::string XmlName() { return std::string(); }
            static std::string Format(const Color& c)
            {
                const std::uint32_t packed = (static_cast<std::uint32_t>(c.getAProperty()) << 24) |
                                             (static_cast<std::uint32_t>(c.getRProperty()) << 16) |
                                             (static_cast<std::uint32_t>(c.getGProperty()) << 8) |
                                             static_cast<std::uint32_t>(c.getBProperty());
                return std::format("{:08X}", packed);
            }
            static Color Parse(const std::string& text)
            {
                const std::vector<std::string> tokens = IntermediateReader::SplitTokens(text);
                RequireTokenCount(tokens, 1);
                const std::string_view trimmed = tokens[0];
                std::uint64_t packed = 0;
                for (char ch : trimmed)
                {
                    if (!std::isxdigit(static_cast<unsigned char>(ch)))
                    {
                        ThrowBadFormat();
                    }
                    packed = (packed << 4) | static_cast<std::uint64_t>(std::isdigit(static_cast<unsigned char>(ch))
                                                                             ? ch - '0'
                                                                             : std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10);
                    if (packed > 0xFFFFFFFFull)
                    {
                        throw System::OverflowException("Value was either too large or too small for a UInt32.");
                    }
                }
                const std::uint32_t value = static_cast<std::uint32_t>(packed);
                return Color(static_cast<std::int32_t>((value >> 16) & 0xFF), static_cast<std::int32_t>((value >> 8) & 0xFF),
                             static_cast<std::int32_t>(value & 0xFF), static_cast<std::int32_t>(value >> 24));
            }
            static Color ParseTokens(std::span<const std::string> tokens)
            {
                RequireTokenCount(tokens, 1);
                return Parse(tokens[0]);
            }
        };

        // -------------------------------------------------------------------------------------
        // Element-structured framework types.
        // -------------------------------------------------------------------------------------
        ContentSerializerAttribute Named(const char* name)
        {
            ContentSerializerAttribute format;
            format.setElementNameProperty(name);
            return format;
        }

        class BoundingBoxSerializer final : public ContentTypeSerializer<BoundingBox>
        {
        protected:
            void Serialize(IntermediateWriter& output, const BoundingBox& value, const ContentSerializerAttribute&) override
            {
                output.WriteRawObject<Vector3>(value.Min, Named("Min"));
                output.WriteRawObject<Vector3>(value.Max, Named("Max"));
            }
            [[nodiscard]] BoundingBox Deserialize(IntermediateReader& input, const ContentSerializerAttribute&,
                                                  BoundingBox existing) override
            {
                existing.Min = input.ReadRawObject<Vector3>(Named("Min"));
                existing.Max = input.ReadRawObject<Vector3>(Named("Max"));
                return existing;
            }
        };

        class BoundingSphereSerializer final : public ContentTypeSerializer<BoundingSphere>
        {
        protected:
            void Serialize(IntermediateWriter& output, const BoundingSphere& value, const ContentSerializerAttribute&) override
            {
                output.WriteRawObject<Vector3>(value.Center, Named("Center"));
                output.WriteRawObject<float>(value.Radius, Named("Radius"));
            }
            [[nodiscard]] BoundingSphere Deserialize(IntermediateReader& input, const ContentSerializerAttribute&,
                                                     BoundingSphere existing) override
            {
                existing.Center = input.ReadRawObject<Vector3>(Named("Center"));
                existing.Radius = input.ReadRawObject<float>(Named("Radius"));
                return existing;
            }
        };

        class RaySerializer final : public ContentTypeSerializer<Ray>
        {
        protected:
            void Serialize(IntermediateWriter& output, const Ray& value, const ContentSerializerAttribute&) override
            {
                output.WriteRawObject<Vector3>(value.Position, Named("Position"));
                output.WriteRawObject<Vector3>(value.Direction, Named("Direction"));
            }
            [[nodiscard]] Ray Deserialize(IntermediateReader& input, const ContentSerializerAttribute&, Ray existing) override
            {
                existing.Position = input.ReadRawObject<Vector3>(Named("Position"));
                existing.Direction = input.ReadRawObject<Vector3>(Named("Direction"));
                return existing;
            }
        };

        class CurveSerializer final : public ContentTypeSerializer<Curve>
        {
        protected:
            void Serialize(IntermediateWriter& output, const Curve& value, const ContentSerializerAttribute&) override
            {
                output.WriteRawObject<CurveLoopType>(value.getPreLoopProperty(), Named("PreLoop"));
                output.WriteRawObject<CurveLoopType>(value.getPostLoopProperty(), Named("PostLoop"));
                std::string keys;
                const CurveKeyCollection& collection = value.getKeysProperty();
                for (int i = 0; i < collection.getCountProperty(); ++i)
                {
                    const CurveKey& key = collection[i];
                    if (!keys.empty())
                    {
                        keys += ' ';
                    }
                    keys += FormatFloats({key.getPositionProperty(), key.getValueProperty(), key.getTangentInProperty(),
                                          key.getTangentOutProperty()});
                    keys += ' ';
                    keys += EnumSerializer<CurveContinuity>::Format(key.getContinuityProperty());
                }
                System::Xml::XmlWriter& xml = output.getXmlProperty();
                xml.WriteStartElement("Keys");
                if (!keys.empty())
                {
                    xml.WriteString(keys);
                }
                xml.WriteEndElement();
            }

            [[nodiscard]] Curve Deserialize(IntermediateReader& input, const ContentSerializerAttribute&, Curve existing) override
            {
                existing.setPreLoopProperty(input.ReadRawObject<CurveLoopType>(Named("PreLoop")));
                existing.setPostLoopProperty(input.ReadRawObject<CurveLoopType>(Named("PostLoop")));
                const std::string text = input.ReadRawObject<std::string>(Named("Keys"));
                const std::vector<std::string> tokens = IntermediateReader::SplitTokens(text);
                if (tokens.size() % 5 != 0)
                {
                    throw InvalidContentException("XML does not have enough entries in space-separated list.");
                }
                existing.getKeysProperty().Clear();
                for (std::size_t i = 0; i < tokens.size(); i += 5)
                {
                    CurveContinuity continuity = CurveContinuity::Smooth;
                    bool found = false;
                    for (const auto& [member, name] : ContentEnumNames<CurveContinuity>::Names)
                    {
                        if (name == tokens[i + 4])
                        {
                            continuity = member;
                            found = true;
                        }
                    }
                    if (!found)
                    {
                        throw System::ArgumentException("Requested value '" + tokens[i + 4] + "' was not found.");
                    }
                    existing.getKeysProperty().Add(CurveKey(ParseSingleText(tokens[i]), ParseSingleText(tokens[i + 1]),
                                                            ParseSingleText(tokens[i + 2]), ParseSingleText(tokens[i + 3]),
                                                            continuity));
                }
                return existing;
            }
        };

        // -------------------------------------------------------------------------------------
        // Type-name grammar
        // -------------------------------------------------------------------------------------
        std::size_t FindTopLevel(std::string_view text, char wanted, std::size_t from = 0)
        {
            int depth = 0;
            for (std::size_t i = from; i < text.size(); ++i)
            {
                const char c = text[i];
                if (depth == 0 && c == wanted)
                {
                    return i;
                }
                if (c == '[')
                {
                    ++depth;
                }
                else if (c == ']')
                {
                    --depth;
                }
            }
            return std::string_view::npos;
        }

        std::vector<std::string_view> SplitTopLevel(std::string_view text)
        {
            std::vector<std::string_view> parts;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t comma = FindTopLevel(text, ',', start);
                if (comma == std::string_view::npos)
                {
                    parts.push_back(text.substr(start));
                    return parts;
                }
                parts.push_back(text.substr(start, comma - start));
                start = comma + 1;
            }
        }

        std::string StripArity(std::string_view base)
        {
            const std::size_t tick = base.find('`');
            return std::string(tick == std::string_view::npos ? base : base.substr(0, tick));
        }
    }

    // -----------------------------------------------------------------------------------------
    // IntermediateSerializer
    // -----------------------------------------------------------------------------------------
    IntermediateSerializer::IntermediateSerializer(std::string referenceRelocationPath)
        : referenceRelocationPath_(std::move(referenceRelocationPath))
    {
    }

    ContentTypeSerializerBase& IntermediateSerializer::GetTypeSerializer(System::Type type) const
    {
        EnsureBuiltInTypeSerializers();
        if (ContentTypeSerializerBase* found = FindTypeSerializer(type))
        {
            return *found;
        }
        throw System::ArgumentException("No type serializer is registered for '" + type.getName() + "'.");
    }

    ContentTypeSerializerBase* IntermediateSerializer::FindTypeSerializer(System::Type type) noexcept
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const auto found = registry.byType.find(type);
        return found == registry.byType.end() ? nullptr : found->second;
    }

    ContentTypeSerializerBase* IntermediateSerializer::FindTypeSerializerForCarrier(std::type_index carrier) noexcept
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const auto found = registry.byCarrier.find(carrier);
        return found == registry.byCarrier.end() ? nullptr : found->second;
    }

    ContentTypeSerializerBase* IntermediateSerializer::FindTypeSerializer(const ContentObject& value) noexcept
    {
        if (value.Empty())
        {
            return nullptr;
        }
        if (ContentTypeSerializerBase* byCarrier = FindTypeSerializerForCarrier(value.CppType()))
        {
            return byCarrier;
        }
        return FindTypeSerializer(value.StableType());
    }

    ContentTypeSerializerBase* IntermediateSerializer::FindTypeSerializer(const std::string& typeName) noexcept
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const auto found = registry.byName.find(CanonicalTypeName(typeName));
        return found == registry.byName.end() ? nullptr : found->second;
    }

    ContentTypeSerializerBase& IntermediateSerializer::RegisterTypeSerializer(
        std::unique_ptr<ContentTypeSerializerBase> serializer, std::vector<std::string> additionalNames, bool registerName)
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const System::Type type = serializer->getTargetTypeProperty();
        if (const auto existing = registry.byType.find(type); existing != registry.byType.end())
        {
            return *existing->second;
        }
        ContentTypeSerializerBase* raw = serializer.get();
        registry.owned.push_back(std::move(serializer));
        registry.byType.emplace(type, raw);
        registry.byCarrier.emplace(raw->CarrierType(), raw);
        if (registerName)
        {
            const std::string canonical = CanonicalTypeName(raw->TargetTypeName());
            registry.byName.emplace(canonical, raw);
            registry.knownNames.insert(canonical);
            if (!raw->getXmlTypeNameProperty().empty())
            {
                registry.byName.emplace(raw->getXmlTypeNameProperty(), raw);
            }
        }
        for (const std::string& name : additionalNames)
        {
            registry.byName.emplace(CanonicalTypeName(name), raw);
        }
        return *raw;
    }

    ContentTypeSerializerBase& IntermediateSerializer::RequireTypeSerializer(System::Type type, const std::string& typeName)
    {
        EnsureBuiltInTypeSerializers();
        if (ContentTypeSerializerBase* found = FindTypeSerializer(type))
        {
            return *found;
        }
        throw System::ArgumentException("No type serializer is registered for '" + typeName +
                                        "'; describe the type or register a ContentTypeSerializer for it.");
    }

    void IntermediateSerializer::RegisterKnownTypeName(const std::string& typeName)
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        registry.knownNames.insert(CanonicalTypeName(typeName));
    }

    bool IntermediateSerializer::IsKnownTypeName(const std::string& typeName) noexcept
    {
        Registry& registry = TheRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const std::string canonical = CanonicalTypeName(typeName);
        return registry.knownNames.count(canonical) > 0 || registry.byName.count(canonical) > 0;
    }

    std::string IntermediateSerializer::CanonicalTypeName(std::string_view name)
    {
        name = Trim(name);
        const std::size_t assemblyComma = FindTopLevel(name, ',');
        if (assemblyComma != std::string_view::npos)
        {
            name = Trim(name.substr(0, assemblyComma));
        }
        const std::size_t open = FindTopLevel(name, '[');
        if (open == std::string_view::npos)
        {
            return StripArity(name);
        }
        const std::string base = StripArity(name.substr(0, open));
        // Find the matching close bracket of the first group.
        int depth = 0;
        std::size_t close = open;
        for (; close < name.size(); ++close)
        {
            if (name[close] == '[')
            {
                ++depth;
            }
            else if (name[close] == ']' && --depth == 0)
            {
                break;
            }
        }
        std::string_view inner = name.substr(open + 1, close - open - 1);
        std::string result = base;
        if (Trim(inner).empty())
        {
            result += "[]";
        }
        else
        {
            result += '[';
            bool first = true;
            for (std::string_view arg : SplitTopLevel(inner))
            {
                arg = Trim(arg);
                if (!arg.empty() && arg.front() == '[' && arg.back() == ']')
                {
                    arg = arg.substr(1, arg.size() - 2);
                }
                if (!first)
                {
                    result += ',';
                }
                first = false;
                result += CanonicalTypeName(arg);
            }
            result += ']';
        }
        if (close + 1 < name.size())
        {
            result += CanonicalTypeName(name.substr(close + 1)).empty() ? "" : std::string(name.substr(close + 1));
        }
        return result;
    }

    const std::string& IntermediateSerializer::getReferenceRelocationPathProperty() const noexcept
    {
        return referenceRelocationPath_;
    }

    const std::vector<std::pair<std::string, std::string>>& IntermediateSerializer::NamespaceAliases() const noexcept
    {
        return aliases_;
    }

    namespace
    {
        std::string SpellCanonical(IntermediateSerializer& serializer, const std::string& canonical,
                                   std::vector<std::pair<std::string, std::string>>& aliases);

        std::string AliasFor(const std::string& ns, std::vector<std::pair<std::string, std::string>>& aliases)
        {
            for (const auto& [alias, existing] : aliases)
            {
                if (existing == ns)
                {
                    return alias;
                }
            }
            const std::size_t dot = ns.rfind('.');
            std::string alias = dot == std::string::npos ? ns : ns.substr(dot + 1);
            std::string candidate = alias;
            int suffix = 2;
            while (true)
            {
                bool taken = false;
                for (const auto& [existingAlias, existing] : aliases)
                {
                    if (existingAlias == candidate)
                    {
                        taken = true;
                    }
                }
                if (!taken)
                {
                    break;
                }
                candidate = alias + std::to_string(suffix++);
            }
            aliases.emplace_back(candidate, ns);
            return candidate;
        }

        std::string SpellBase(const std::string& base, std::vector<std::pair<std::string, std::string>>& aliases)
        {
            if (ContentTypeSerializerBase* known = IntermediateSerializer::FindTypeSerializer(base))
            {
                if (!known->getXmlTypeNameProperty().empty())
                {
                    return known->getXmlTypeNameProperty();
                }
            }
            const std::size_t dot = base.rfind('.');
            if (dot == std::string::npos)
            {
                return base;
            }
            const std::string ns = base.substr(0, dot);
            if (ns == "System")
            {
                return base;
            }
            return AliasFor(ns, aliases) + ":" + base.substr(dot + 1);
        }

        /**
         * @brief Spells a type name with an alias only when one is already declared.
         *
         * The external-reference section is written after the root element, so it cannot declare a
         * new namespace alias -- and XNA does not: the same target type is spelled
         * `Graphics:TextureContent` in a document whose root already declares that alias and
         * `Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent` in one that does not
         * (measured: tests/reference/xna40/graphics case material/serialize_basic against
         * tests/reference/xna40/intermediate/accept_external_relocated_relative.normalized.xml).
         */
        std::string SpellWithExistingAlias(const std::string& canonical,
                                           const std::vector<std::pair<std::string, std::string>>& aliases)
        {
            const std::size_t dot = canonical.rfind('.');
            if (dot == std::string::npos)
            {
                return canonical;
            }
            const std::string ns = canonical.substr(0, dot);
            for (const auto& [alias, existing] : aliases)
            {
                if (existing == ns)
                {
                    return alias + ":" + canonical.substr(dot + 1);
                }
            }
            return canonical;
        }

        std::string SpellCanonical(IntermediateSerializer& serializer, const std::string& canonical,
                                   std::vector<std::pair<std::string, std::string>>& aliases)
        {
            (void)serializer;
            const std::size_t open = FindTopLevel(canonical, '[');
            if (open == std::string::npos)
            {
                return SpellBase(canonical, aliases);
            }
            if (canonical.compare(open, 2, "[]") == 0)
            {
                return SpellCanonical(serializer, canonical.substr(0, open), aliases) + "[]";
            }
            const std::size_t close = canonical.rfind(']');
            std::string result = SpellBase(canonical.substr(0, open), aliases) + "[";
            bool first = true;
            for (std::string_view arg : SplitTopLevel(std::string_view(canonical).substr(open + 1, close - open - 1)))
            {
                if (!first)
                {
                    result += ',';
                }
                first = false;
                result += SpellCanonical(serializer, std::string(arg), aliases);
            }
            return result + "]";
        }

        std::string ResolveSpelled(std::string_view spelled, const System::Xml::XmlReader& scope, bool trim = true)
        {
            if (trim)
            {
                spelled = Trim(spelled);
            }
            const std::size_t assemblyComma = FindTopLevel(spelled, ',');
            if (assemblyComma != std::string_view::npos)
            {
                spelled = Trim(spelled.substr(0, assemblyComma));
            }
            const std::size_t open = FindTopLevel(spelled, '[');
            std::string_view baseView = open == std::string_view::npos ? spelled : spelled.substr(0, open);
            if (baseView.find('`') != std::string_view::npos)
            {
                // XNA reports the bracketed part after the arity marker, one bracket level deep.
                std::string_view brackets = open == std::string_view::npos ? std::string_view() : spelled.substr(open);
                if (brackets.size() >= 2 && brackets.front() == '[' && brackets[1] == '[')
                {
                    brackets = brackets.substr(1, brackets.size() - 2);
                }
                throw System::ArgumentException("XML contains invalid type name " + std::string(brackets) + ".");
            }
            std::string base(baseView);
            const std::size_t colon = base.find(':');
            if (colon != std::string::npos)
            {
                const std::string prefix = base.substr(0, colon);
                base = scope.LookupNamespace(prefix).value_or(std::string()) + "." + base.substr(colon + 1);
            }
            if (open == std::string_view::npos)
            {
                // A generic argument is looked up verbatim: XNA does not trim " int ".
                ContentTypeSerializerBase* found =
                    Trim(base) == base ? IntermediateSerializer::FindTypeSerializer(base) : nullptr;
                if (found == nullptr)
                {
                    throw System::ArgumentException("Cannot find type \"" + base + "\".");
                }
                return IntermediateSerializer::CanonicalTypeName(found->TargetTypeName());
            }
            const std::size_t close = spelled.rfind(']');
            const std::string_view inner = spelled.substr(open + 1, close - open - 1);
            if (Trim(inner).empty())
            {
                return ResolveSpelled(base, scope) + "[]";
            }
            std::string result = base + "[";
            bool first = true;
            for (std::string_view arg : SplitTopLevel(inner))
            {
                if (!first)
                {
                    result += ',';
                }
                first = false;
                result += ResolveSpelled(arg, scope, false);
            }
            return result + "]";
        }
    }

    std::string IntermediateSerializer::SpellTypeName(const ContentTypeSerializerBase& serializer)
    {
        return SpellCanonical(*this, CanonicalTypeName(serializer.TargetTypeName()), aliases_);
    }

    std::string IntermediateSerializer::SpellDeclaredTypeName(const std::string& typeName) const
    {
        return SpellWithExistingAlias(CanonicalTypeName(typeName), aliases_);
    }

    ContentTypeSerializerBase& IntermediateSerializer::ResolveTypeName(const std::string& spelledName,
                                                                       const System::Xml::XmlReader& scope)
    {
        EnsureBuiltInTypeSerializers();
        const std::string canonical = ResolveSpelled(spelledName, scope);
        if (ContentTypeSerializerBase* found = FindTypeSerializer(canonical))
        {
            return *found;
        }
        throw System::ArgumentException("Cannot find type \"" + canonical + "\".");
    }

    void IntermediateSerializer::SerializeObject(System::Xml::XmlWriter& output, const ContentObject& value,
                                                 ContentTypeSerializerBase& serializer,
                                                 const std::string& referenceRelocationPath)
    {
        EnsureBuiltInTypeSerializers();
        if (serializer.IsNull(value))
        {
            throw System::ArgumentNullException("value");
        }
        ContentObject payload = value;
        ContentTypeSerializerBase* declared = &serializer;
        if (const ContentTypeSerializerBase* underlying = serializer.UnderlyingSerializer())
        {
            payload = serializer.UnderlyingValue(payload);
            declared = const_cast<ContentTypeSerializerBase*>(underlying);
        }
        IntermediateSerializer pass(referenceRelocationPath);
        IntermediateWriter writer(pass, output);
        writer.ScanForNamespaces(payload, *declared);
        output.WriteStartDocument();
        output.WriteStartElement("XnaContent");
        for (const auto& [alias, ns] : pass.aliases_)
        {
            output.WriteAttributeString("xmlns:" + alias, ns);
        }
        writer.WriteRootAsset(payload, *declared);
        writer.WriteResources();
        writer.WriteExternalReferences();
        output.WriteEndElement();
        output.WriteEndDocument();
    }

    ContentObject IntermediateSerializer::DeserializeObject(System::Xml::XmlReader& input,
                                                            ContentTypeSerializerBase& serializer,
                                                            const std::string& referenceRelocationPath)
    {
        EnsureBuiltInTypeSerializers();
        IntermediateSerializer pass(referenceRelocationPath);
        IntermediateReader reader(pass, input);
        try
        {
            ContentObject result = reader.ReadRootAsset(serializer);
            const ContentTypeSerializerBase* actual = FindTypeSerializer(result);
            if (actual == nullptr || actual->IsNull(result))
            {
                throw InvalidContentException("Value cannot be null.\nParameter name: value");
            }
            // XNA reads a section when it is present, and requires it once a reference into it is
            // pending: a document with shared references but no Resources is refused.
            if (reader.HasPendingSharedFixups() || reader.MoveToElement("Resources"))
            {
                if (!reader.MoveToElement("Resources"))
                {
                    throw InvalidContentException("XML element \"Resources\" not found.");
                }
                reader.ReadResources();
            }
            if (reader.HasPendingExternalFixups() || reader.MoveToElement("ExternalReferences"))
            {
                if (!reader.MoveToElement("ExternalReferences"))
                {
                    throw InvalidContentException("XML element \"ExternalReferences\" not found.");
                }
                reader.ReadExternalReferences();
            }
            reader.ExpectEndOfDocument();
            reader.ApplyFixups();
            return result;
        }
        catch (const InvalidContentException&)
        {
            throw;
        }
        catch (const System::Exception& error)
        {
            throw InvalidContentException(
                "There was an error while deserializing intermediate XML. " + error.getMessageProperty(),
                std::current_exception());
        }
        catch (const std::exception& error)
        {
            throw InvalidContentException(std::string("There was an error while deserializing intermediate XML. ") +
                                              error.what(),
                                          std::current_exception());
        }
    }

    // -----------------------------------------------------------------------------------------
    // ObjectSerializer and the helpers the template headers declare
    // -----------------------------------------------------------------------------------------
    ObjectSerializer::ObjectSerializer()
        : ContentTypeSerializerBase(System::Type::From<System::Object>(), std::string(), "System.Object")
    {
    }

    bool ObjectSerializer::IsNullable() const noexcept { return true; }

    bool ObjectSerializer::IsReferenceType() const noexcept { return true; }

    bool ObjectSerializer::IsNull(const ContentObject& value) const
    {
        if (value.Empty())
        {
            return true;
        }
        // A box may hold a null reference of a known type: that type's serializer knows.
        if (const ContentTypeSerializerBase* actual = IntermediateSerializer::FindTypeSerializer(value))
        {
            return actual->IsNull(value);
        }
        return false;
    }

    ContentObject ObjectSerializer::NullObject() const { return ContentObject{}; }

    bool ObjectSerializer::IsAbstract() const noexcept { return true; }

    void ObjectSerializer::Serialize(IntermediateWriter& output, const ContentObject& value,
                                     const ContentSerializerAttribute& format)
    {
        (void)output;
        (void)format;
        throw InvalidContentException("No type serializer is registered for '" + value.StableType() + "'.");
    }

    ContentObject ObjectSerializer::Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                const ContentObject& existingInstance)
    {
        (void)input;
        (void)format;
        (void)existingInstance;
        throw InvalidContentException("XML is missing a \"Type\" attribute.");
    }

    void ThrowSharedResourceRequiresReference(const std::string& typeName, const std::string& memberName)
    {
        throw InvalidContentException("Member '" + memberName + "' of '" + typeName +
                                      "' is marked SharedResource, but the type is a value type; a shared resource "
                                      "needs an owner with identity (derive it from System::Object or hold it by "
                                      "std::shared_ptr).");
    }

    void ThrowInvalidEnumValue(const std::string& text, const std::string& enumTypeName)
    {
        throw InvalidContentException("XML contains invalid value \"" + text + "\" for enum " + enumTypeName + ".");
    }

    void ThrowNotEnoughPackedEntries()
    {
        throw InvalidContentException("XML does not have enough entries in space-separated list.");
    }

    void ThrowDuplicateDictionaryKey()
    {
        throw System::ArgumentException("An item with the same key has already been added.");
    }

    std::string_view TrimXmlWhitespace(std::string_view text) { return Trim(text); }

    void IntermediateSerializer::EnsureBuiltInTypeSerializers()
    {
        {
            Registry& registry = TheRegistry();
            std::lock_guard<std::mutex> lock(registry.mutex);
            if (registry.builtInsRegistered)
            {
                return;
            }
            registry.builtInsRegistered = true;
        }
        RegisterTypeSerializer(std::make_unique<TextSerializer<bool, BoolTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::int8_t, SByteTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::uint8_t, ByteTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::int16_t, Int16Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::uint16_t, UInt16Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::int32_t, Int32Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::uint32_t, UInt32Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::int64_t, Int64Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::uint64_t, UInt64Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<float, SingleTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<double, DoubleTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<std::string, StringTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<char16_t, CharTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<System::Decimal, DecimalTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<System::TimeSpan, TimeSpanTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<System::DateTime, DateTimeTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Vector2, Vector2Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Vector3, Vector3Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Vector4, Vector4Traits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Quaternion, QuaternionTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Matrix, MatrixTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Plane, PlaneTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Rectangle, RectangleTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Point, PointTraits>>());
        RegisterTypeSerializer(std::make_unique<TextSerializer<Color, ColorTraits>>());
        RegisterTypeSerializer(std::make_unique<BoundingBoxSerializer>());
        RegisterTypeSerializer(std::make_unique<BoundingSphereSerializer>());
        RegisterTypeSerializer(std::make_unique<RaySerializer>());
        RegisterTypeSerializer(std::make_unique<CurveSerializer>());
        RegisterTypeSerializer(std::make_unique<NamedValueDictionarySerializer<OpaqueDataDictionary, ContentObject>>());
        // A material's texture collection writes its entries as <Texture Key="…">, the collection
        // item name the type declares (measured, tests/reference/xna40/graphics,
        // material/serialize_basic).
        RegisterTypeSerializer(
            std::make_unique<NamedValueDictionarySerializer<Graphics::TextureReferenceDictionary,
                                                            std::shared_ptr<ExternalReference<Graphics::TextureContent>>>>(
                std::string(Graphics::TextureReferenceDictionary::CollectionItemName)));
        // ...and its value type, which the dictionary looks up by type rather than instantiating,
        // so the on-demand factory would never have run for it.
        IntermediateSerializer::TypeSerializerFor<ExternalReference<Graphics::TextureContent>>();
        // The animation dictionaries, whose entries are <Channel Key="…"> and <Animation Key="…">
        // (measured, tests/reference/xna40/graphics animation/serialize_content and
        // animation/serialize_dictionary), and the channel itself, which is a collection of
        // keyframes rather than a described type.
        RegisterTypeSerializer(std::make_unique<Graphics::detail::AnimationChannelSerializer>());
        IntermediateSerializer::TypeSerializerFor<Graphics::AnimationContent>();
        RegisterTypeSerializer(
            std::make_unique<NamedValueDictionarySerializer<Graphics::AnimationChannelDictionary,
                                                            std::shared_ptr<Graphics::AnimationChannel>>>(
                std::string(Graphics::AnimationChannelDictionary::CollectionItemName)));
        RegisterTypeSerializer(
            std::make_unique<NamedValueDictionarySerializer<Graphics::AnimationContentDictionary,
                                                            std::shared_ptr<Graphics::AnimationContent>>>(
                std::string(Graphics::AnimationContentDictionary::CollectionItemName)));
        ContentTypeSerializerBase& object = RegisterTypeSerializer(std::make_unique<ObjectSerializer>());
        {
            Registry& registry = TheRegistry();
            std::lock_guard<std::mutex> lock(registry.mutex);
            registry.byType.emplace(System::Type::From<ContentObject>(), &object);
        }
    }
}
