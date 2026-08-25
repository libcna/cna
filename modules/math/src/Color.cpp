// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Microsoft::Xna::Framework
{
    // ------------------------------------------------------------------
    // File-local helpers
    // ------------------------------------------------------------------

    namespace
    {
        constexpr uintcs ComponentMask = uintcs{0xFFU};
        constexpr intcs ByteMin = intcs{0};
        constexpr intcs ByteMax = intcs{255};

        [[nodiscard]] uintcs ToPackedComponent(bytecs value)
        {
            return static_cast<uintcs>(value) & ComponentMask;
        }

        [[nodiscard]] intcs ClampToByteInt(intcs value)
        {
            return std::clamp(value, ByteMin, ByteMax);
        }

        [[nodiscard]] bytecs ToByte(intcs value)
        {
            return static_cast<bytecs>(ClampToByteInt(value));
        }

        // Scale a unit float in [0,1] to a byte value in [0,255], clamping out-of-range input.
        // Matches every float-taking Color constructor in real FNA (e.g. "R = (byte)
        // MathHelper.Clamp(color.X * 255, Byte.MinValue, Byte.MaxValue);", Color.cs) -- used by
        // this file's Color(Vector4)/Color(Vector3)/Color(float,float,float[,float]) ctors.
        [[nodiscard]] bytecs ToByteFromUnitClamped(float value)
        {
            const float clamped = MathHelper::Clamp(value * 255.0f, static_cast<float>(SharpRuntime::BYTE_MIN),
                                                     static_cast<float>(SharpRuntime::BYTE_MAX));
            if (std::isnan(clamped))
            {
                // MathHelper::Clamp faithfully reproduces FNA's own comparison-based clamp,
                // under which a NaN input passes through unclamped (NaN < min and max < NaN are
                // both false) -- correct to preserve, since MathHelper::Clamp is a public XNA
                // API method used well beyond this file. But static_cast<intcs>(NaN) below would
                // be undefined behavior in C++ (NaN is not representable in any integer type),
                // unlike C#'s own (byte) cast, which merely yields an unspecified-but-defined
                // value for NaN. 0 is this project's deterministic, documented stand-in for a
                // result FNA itself leaves unspecified.
                return bytecs{0};
            }
            return static_cast<bytecs>(static_cast<intcs>(clamped));
        }

        // Scale a unit float to a byte value via truncation (not clamping), matching FNA's
        // IPackedVector.PackFromVector4 exactly: unlike every constructor above, FNA's real
        // implementation does NOT clamp -- "R = (byte) (vector.X * 255.0f);" (Color.cs) casts
        // directly, so an out-of-range component wraps rather than saturating at 0/255. A
        // direct static_cast<bytecs>(value * 255.0f) is genuine undefined behavior in C++
        // whenever the scaled value cannot be represented in bytecs's source integer type --
        // which routinely happens for an HDR-authored or otherwise-unclamped Vector4. This
        // truncates through a wide, always-safely-representable integer type first (a signed
        // int -> bytecs narrowing conversion is well-defined modulo-256 wraparound in C++, not
        // UB), reproducing FNA's truncating low-byte behavior for every realistic input while
        // remaining well-defined for all of them; NaN/+-Infinity deterministically produce 0 (a
        // result .NET itself leaves unspecified for this exact conversion).
        [[nodiscard]] bytecs ToByteFromUnitTruncated(float value)
        {
            const float scaled = value * 255.0f;
            if (!std::isfinite(scaled))
            {
                return bytecs{0};
            }
            constexpr float SafeBound = 8388608.0f; // 2^23: exact in float, well within intcs
            const float bounded = std::clamp(scaled, -SafeBound, SafeBound);
            return static_cast<bytecs>(static_cast<intcs>(bounded));
        }

        // Safely narrows a float already destined for the intcs-taking Color constructor (which
        // clamps again to [0,255] via ToByte() immediately) down to intcs, avoiding the
        // undefined behavior of casting a non-finite or out-of-intcs-range float directly to a
        // 32-bit integer. Color::Lerp/Multiply share the exact same root cause as
        // ToByteFromUnitTruncated above -- an interpolation/scale factor that is NaN (e.g. a
        // caller's own 0/0) or otherwise produces a huge intermediate float reaches a bare
        // static_cast<intcs>(...) with no guard. The exact pre-clamp bound is otherwise
        // inconsequential -- ToByte()'s own clamp to [0,255] runs right after -- so any range
        // comfortably inside intcs's representable span is safe to pick.
        [[nodiscard]] intcs SafeFloatToIntcs(float value)
        {
            if (!std::isfinite(value))
            {
                return intcs{0};
            }
            constexpr float SafeBound = 1'000'000.0f;
            return static_cast<intcs>(std::clamp(value, -SafeBound, SafeBound));
        }

        [[nodiscard]] int ToInt(bytecs value)
        {
            return static_cast<int>(static_cast<uintcs>(value) & ComponentMask);
        }
    }

    // ------------------------------------------------------------------
    // Static color definitions
    // ------------------------------------------------------------------

    const Color Color::Transparent(UInt32{0x00000000U});
    const Color Color::AliceBlue(UInt32{0xfffff8f0U});
    const Color Color::AntiqueWhite(UInt32{0xffd7ebfaU});
    const Color Color::Aqua(UInt32{0xffffff00U});
    const Color Color::Aquamarine(UInt32{0xffd4ff7fU});
    const Color Color::Azure(UInt32{0xfffffff0U});
    const Color Color::Beige(UInt32{0xffdcf5f5U});
    const Color Color::Bisque(UInt32{0xffc4e4ffU});
    const Color Color::Black(UInt32{0xff000000U});
    const Color Color::BlanchedAlmond(UInt32{0xffcdebffU});
    const Color Color::Blue(UInt32{0xffff0000U});
    const Color Color::BlueViolet(UInt32{0xffe22b8aU});
    const Color Color::Brown(UInt32{0xff2a2aa5U});
    const Color Color::BurlyWood(UInt32{0xff87b8deU});
    const Color Color::CadetBlue(UInt32{0xffa09e5fU});
    const Color Color::Chartreuse(UInt32{0xff00ff7fU});
    const Color Color::Chocolate(UInt32{0xff1e69d2U});
    const Color Color::Coral(UInt32{0xff507fffU});
    const Color Color::CornflowerBlue(UInt32{0xffed9564U});
    const Color Color::Cornsilk(UInt32{0xffdcf8ffU});
    const Color Color::Crimson(UInt32{0xff3c14dcU});
    const Color Color::Cyan(UInt32{0xffffff00U});
    const Color Color::DarkBlue(UInt32{0xff8b0000U});
    const Color Color::DarkCyan(UInt32{0xff8b8b00U});
    const Color Color::DarkGoldenrod(UInt32{0xff0b86b8U});
    const Color Color::DarkGray(UInt32{0xffa9a9a9U});
    const Color Color::DarkGreen(UInt32{0xff006400U});
    const Color Color::DarkKhaki(UInt32{0xff6bb7bdU});
    const Color Color::DarkMagenta(UInt32{0xff8b008bU});
    const Color Color::DarkOliveGreen(UInt32{0xff2f6b55U});
    const Color Color::DarkOrange(UInt32{0xff008cffU});
    const Color Color::DarkOrchid(UInt32{0xffcc3299U});
    const Color Color::DarkRed(UInt32{0xff00008bU});
    const Color Color::DarkSalmon(UInt32{0xff7a96e9U});
    const Color Color::DarkSeaGreen(UInt32{0xff8bbc8fU});
    const Color Color::DarkSlateBlue(UInt32{0xff8b3d48U});
    const Color Color::DarkSlateGray(UInt32{0xff4f4f2fU});
    const Color Color::DarkTurquoise(UInt32{0xffd1ce00U});
    const Color Color::DarkViolet(UInt32{0xffd30094U});
    const Color Color::DeepPink(UInt32{0xff9314ffU});
    const Color Color::DeepSkyBlue(UInt32{0xffffbf00U});
    const Color Color::DimGray(UInt32{0xff696969U});
    const Color Color::DodgerBlue(UInt32{0xffff901eU});
    const Color Color::Firebrick(UInt32{0xff2222b2U});
    const Color Color::FloralWhite(UInt32{0xfff0faffU});
    const Color Color::ForestGreen(UInt32{0xff228b22U});
    const Color Color::Fuchsia(UInt32{0xffff00ffU});
    const Color Color::Gainsboro(UInt32{0xffdcdcdcU});
    const Color Color::GhostWhite(UInt32{0xfffff8f8U});
    const Color Color::Gold(UInt32{0xff00d7ffU});
    const Color Color::Goldenrod(UInt32{0xff20a5daU});
    const Color Color::Gray(UInt32{0xff808080U});
    const Color Color::Green(UInt32{0xff008000U});
    const Color Color::GreenYellow(UInt32{0xff2fffadU});
    const Color Color::Honeydew(UInt32{0xfff0fff0U});
    const Color Color::HotPink(UInt32{0xffb469ffU});
    const Color Color::IndianRed(UInt32{0xff5c5ccdU});
    const Color Color::Indigo(UInt32{0xff82004bU});
    const Color Color::Ivory(UInt32{0xfff0ffffU});
    const Color Color::Khaki(UInt32{0xff8ce6f0U});
    const Color Color::Lavender(UInt32{0xfffae6e6U});
    const Color Color::LavenderBlush(UInt32{0xfff5f0ffU});
    const Color Color::LawnGreen(UInt32{0xff00fc7cU});
    const Color Color::LemonChiffon(UInt32{0xffcdfaffU});
    const Color Color::LightBlue(UInt32{0xffe6d8adU});
    const Color Color::LightCoral(UInt32{0xff8080f0U});
    const Color Color::LightCyan(UInt32{0xffffffe0U});
    const Color Color::LightGoldenrodYellow(UInt32{0xffd2fafaU});
    const Color Color::LightGray(UInt32{0xffd3d3d3U});
    const Color Color::LightGreen(UInt32{0xff90ee90U});
    const Color Color::LightPink(UInt32{0xffc1b6ffU});
    const Color Color::LightSalmon(UInt32{0xff7aa0ffU});
    const Color Color::LightSeaGreen(UInt32{0xffaab220U});
    const Color Color::LightSkyBlue(UInt32{0xffface87U});
    const Color Color::LightSlateGray(UInt32{0xff998877U});
    const Color Color::LightSteelBlue(UInt32{0xffdec4b0U});
    const Color Color::LightYellow(UInt32{0xffe0ffffU});
    const Color Color::Lime(UInt32{0xff00ff00U});
    const Color Color::LimeGreen(UInt32{0xff32cd32U});
    const Color Color::Linen(UInt32{0xffe6f0faU});
    const Color Color::Magenta(UInt32{0xffff00ffU});
    const Color Color::Maroon(UInt32{0xff000080U});
    const Color Color::MediumAquamarine(UInt32{0xffaacd66U});
    const Color Color::MediumBlue(UInt32{0xffcd0000U});
    const Color Color::MediumOrchid(UInt32{0xffd355baU});
    const Color Color::MediumPurple(UInt32{0xffdb7093U});
    const Color Color::MediumSeaGreen(UInt32{0xff71b33cU});
    const Color Color::MediumSlateBlue(UInt32{0xffee687bU});
    const Color Color::MediumSpringGreen(UInt32{0xff9afa00U});
    const Color Color::MediumTurquoise(UInt32{0xffccd148U});
    const Color Color::MediumVioletRed(UInt32{0xff8515c7U});
    const Color Color::MidnightBlue(UInt32{0xff701919U});
    const Color Color::MintCream(UInt32{0xfffafff5U});
    const Color Color::MistyRose(UInt32{0xffe1e4ffU});
    const Color Color::Moccasin(UInt32{0xffb5e4ffU});
    const Color Color::NavajoWhite(UInt32{0xffaddeffU});
    const Color Color::Navy(UInt32{0xff800000U});
    const Color Color::OldLace(UInt32{0xffe6f5fdU});
    const Color Color::Olive(UInt32{0xff008080U});
    const Color Color::OliveDrab(UInt32{0xff238e6bU});
    const Color Color::Orange(UInt32{0xff00a5ffU});
    const Color Color::OrangeRed(UInt32{0xff0045ffU});
    const Color Color::Orchid(UInt32{0xffd670daU});
    const Color Color::PaleGoldenrod(UInt32{0xffaae8eeU});
    const Color Color::PaleGreen(UInt32{0xff98fb98U});
    const Color Color::PaleTurquoise(UInt32{0xffeeeeafU});
    const Color Color::PaleVioletRed(UInt32{0xff9370dbU});
    const Color Color::PapayaWhip(UInt32{0xffd5efffU});
    const Color Color::PeachPuff(UInt32{0xffb9daffU});
    const Color Color::Peru(UInt32{0xff3f85cdU});
    const Color Color::Pink(UInt32{0xffcbc0ffU});
    const Color Color::Plum(UInt32{0xffdda0ddU});
    const Color Color::PowderBlue(UInt32{0xffe6e0b0U});
    const Color Color::Purple(UInt32{0xff800080U});
    const Color Color::Red(UInt32{0xff0000ffU});
    const Color Color::RosyBrown(UInt32{0xff8f8fbcU});
    const Color Color::RoyalBlue(UInt32{0xffe16941U});
    const Color Color::SaddleBrown(UInt32{0xff13458bU});
    const Color Color::Salmon(UInt32{0xff7280faU});
    const Color Color::SandyBrown(UInt32{0xff60a4f4U});
    const Color Color::SeaGreen(UInt32{0xff578b2eU});
    const Color Color::SeaShell(UInt32{0xffeef5ffU});
    const Color Color::Sienna(UInt32{0xff2d52a0U});
    const Color Color::Silver(UInt32{0xffc0c0c0U});
    const Color Color::SkyBlue(UInt32{0xffebce87U});
    const Color Color::SlateBlue(UInt32{0xffcd5a6aU});
    const Color Color::SlateGray(UInt32{0xff908070U});
    const Color Color::Snow(UInt32{0xfffafaffU});
    const Color Color::SpringGreen(UInt32{0xff7fff00U});
    const Color Color::SteelBlue(UInt32{0xffb48246U});
    const Color Color::Tan(UInt32{0xff8cb4d2U});
    const Color Color::Teal(UInt32{0xff808000U});
    const Color Color::Thistle(UInt32{0xffd8bfd8U});
    const Color Color::Tomato(UInt32{0xff4763ffU});
    const Color Color::Turquoise(UInt32{0xffd0e040U});
    const Color Color::Violet(UInt32{0xffee82eeU});
    const Color Color::Wheat(UInt32{0xffb3def5U});
    const Color Color::White(UInt32{0xffffffffU});
    const Color Color::WhiteSmoke(UInt32{0xfff5f5f5U});
    const Color Color::Yellow(UInt32{0xff00ffffU});
    const Color Color::YellowGreen(UInt32{0xff32cd9aU});

    // ------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------

    Color::Color()
        : packedValue(0)
    {
    }

    Color::Color(UInt32 pv)
        : packedValue(pv)
    {
    }

    Color::Color(const Vector4& color)
        : packedValue(0)
    {
        setRProperty(ToByteFromUnitClamped(color.X));
        setGProperty(ToByteFromUnitClamped(color.Y));
        setBProperty(ToByteFromUnitClamped(color.Z));
        setAProperty(ToByteFromUnitClamped(color.W));
    }

    Color::Color(const Vector3& color)
        : packedValue(0)
    {
        setRProperty(ToByteFromUnitClamped(color.X));
        setGProperty(ToByteFromUnitClamped(color.Y));
        setBProperty(ToByteFromUnitClamped(color.Z));
        setAProperty(static_cast<bytecs>(255));
    }

    Color::Color(float r, float g, float b)
        : Color(ToByteFromUnitClamped(r), ToByteFromUnitClamped(g), ToByteFromUnitClamped(b), static_cast<bytecs>(255))
    {
    }

    Color::Color(float r, float g, float b, float alpha)
        : Color(ToByteFromUnitClamped(r), ToByteFromUnitClamped(g), ToByteFromUnitClamped(b),
                ToByteFromUnitClamped(alpha))
    {
    }

    Color::Color(intcs r, intcs g, intcs b)
        : Color(ToByte(r), ToByte(g), ToByte(b), static_cast<bytecs>(255))
    {
    }

    Color::Color(intcs r, intcs g, intcs b, intcs alpha)
        : Color(ToByte(r), ToByte(g), ToByte(b), ToByte(alpha))
    {
    }

    Color::Color(bytecs r, bytecs g, bytecs b)
        : Color(r, g, b, static_cast<bytecs>(255))
    {
    }

    Color::Color(bytecs r, bytecs g, bytecs b, bytecs alpha)
        : packedValue(
            (ToPackedComponent(alpha) << 24U) |
            (ToPackedComponent(b) << 16U) |
            (ToPackedComponent(g) << 8U) |
            ToPackedComponent(r))
    {
    }

    // ------------------------------------------------------------------
    // Component property accessors
    // ------------------------------------------------------------------

    bytecs Color::getRProperty() const
    {
        return static_cast<bytecs>(packedValue & ComponentMask);
    }

    void Color::setRProperty(bytecs value)
    {
        packedValue = (packedValue & uintcs{0xFFFFFF00U}) | ToPackedComponent(value);
    }

    bytecs Color::getGProperty() const
    {
        return static_cast<bytecs>((packedValue >> 8U) & ComponentMask);
    }

    void Color::setGProperty(bytecs value)
    {
        packedValue = (packedValue & uintcs{0xFFFF00FFU}) | (ToPackedComponent(value) << 8U);
    }

    bytecs Color::getBProperty() const
    {
        return static_cast<bytecs>((packedValue >> 16U) & ComponentMask);
    }

    void Color::setBProperty(bytecs value)
    {
        packedValue = (packedValue & uintcs{0xFF00FFFFU}) | (ToPackedComponent(value) << 16U);
    }

    bytecs Color::getAProperty() const
    {
        return static_cast<bytecs>((packedValue >> 24U) & ComponentMask);
    }

    void Color::setAProperty(bytecs value)
    {
        packedValue = (packedValue & uintcs{0x00FFFFFFU}) | (ToPackedComponent(value) << 24U);
    }

    // ------------------------------------------------------------------
    // PackedValue property
    // ------------------------------------------------------------------

    UInt32 Color::getPackedValueProperty() const
    {
        return packedValue;
    }

    void Color::setPackedValueProperty(UInt32 value)
    {
        packedValue = value;
    }

    // ------------------------------------------------------------------
    // Debug / display
    // ------------------------------------------------------------------

    std::string Color::getDebugDisplayStringProperty() const
    {
        std::ostringstream stream;
        stream << ToInt(getRProperty()) << ' '
            << ToInt(getGProperty()) << ' '
            << ToInt(getBProperty()) << ' '
            << ToInt(getAProperty());
        return stream.str();
    }

    // ------------------------------------------------------------------
    // Public instance methods
    // ------------------------------------------------------------------

    bool Color::Equals(const Color& other) const
    {
        return packedValue == other.packedValue;
    }

    Vector3 Color::ToVector3() const
    {
        return Vector3(
            static_cast<float>(ToInt(getRProperty())) / 255.0f,
            static_cast<float>(ToInt(getGProperty())) / 255.0f,
            static_cast<float>(ToInt(getBProperty())) / 255.0f);
    }

    Vector4 Color::ToVector4() const
    {
        return Vector4(
            static_cast<float>(ToInt(getRProperty())) / 255.0f,
            static_cast<float>(ToInt(getGProperty())) / 255.0f,
            static_cast<float>(ToInt(getBProperty())) / 255.0f,
            static_cast<float>(ToInt(getAProperty())) / 255.0f);
    }

    std::size_t Color::GetHashCode() const
    {
        return static_cast<std::size_t>(packedValue);
    }

    std::string Color::ToString() const
    {
        std::ostringstream stream;
        stream << "{R:" << ToInt(getRProperty())
            << " G:" << ToInt(getGProperty())
            << " B:" << ToInt(getBProperty())
            << " A:" << ToInt(getAProperty())
            << "}";
        return stream.str();
    }

    // ------------------------------------------------------------------
    // Public static methods
    // ------------------------------------------------------------------

    Color Color::Lerp(const Color& value1, const Color& value2, float amount)
    {
        amount = MathHelper::Clamp(amount, 0.0f, 1.0f);
        return Color(
            SafeFloatToIntcs(MathHelper::Lerp(
                static_cast<float>(ToInt(value1.getRProperty())),
                static_cast<float>(ToInt(value2.getRProperty())), amount)),
            SafeFloatToIntcs(MathHelper::Lerp(
                static_cast<float>(ToInt(value1.getGProperty())),
                static_cast<float>(ToInt(value2.getGProperty())), amount)),
            SafeFloatToIntcs(MathHelper::Lerp(
                static_cast<float>(ToInt(value1.getBProperty())),
                static_cast<float>(ToInt(value2.getBProperty())), amount)),
            SafeFloatToIntcs(MathHelper::Lerp(
                static_cast<float>(ToInt(value1.getAProperty())),
                static_cast<float>(ToInt(value2.getAProperty())), amount)));
    }

    Color Color::FromNonPremultiplied(const Vector4& vector)
    {
        return Color(
            vector.X * vector.W,
            vector.Y * vector.W,
            vector.Z * vector.W,
            vector.W);
    }

    Color Color::FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a)
    {
        return Color(
            static_cast<intcs>(r * a / ByteMax),
            static_cast<intcs>(g * a / ByteMax),
            static_cast<intcs>(b * a / ByteMax),
            a);
    }

    Color Color::Multiply(const Color& value, float scale)
    {
        return Color(
            SafeFloatToIntcs(static_cast<float>(ToInt(value.getRProperty())) * scale),
            SafeFloatToIntcs(static_cast<float>(ToInt(value.getGProperty())) * scale),
            SafeFloatToIntcs(static_cast<float>(ToInt(value.getBProperty())) * scale),
            SafeFloatToIntcs(static_cast<float>(ToInt(value.getAProperty())) * scale));
    }

    // ------------------------------------------------------------------
    // IPackedVector member
    // ------------------------------------------------------------------

    void Color::PackFromVector4(const Vector4& vector)
    {
        setRProperty(ToByteFromUnitTruncated(vector.X));
        setGProperty(ToByteFromUnitTruncated(vector.Y));
        setBProperty(ToByteFromUnitTruncated(vector.Z));
        setAProperty(ToByteFromUnitTruncated(vector.W));
    }

    // ------------------------------------------------------------------
    // Operators
    // ------------------------------------------------------------------

    bool operator==(const Color& a, const Color& b)
    {
        return a.Equals(b);
    }

    bool operator!=(const Color& a, const Color& b)
    {
        return !(a == b);
    }

    Color operator*(const Color& value, float scale)
    {
        return Color::Multiply(value, scale);
    }

    Color operator*(float scale, const Color& value)
    {
        return Color::Multiply(value, scale);
    }
} // namespace Microsoft::Xna::Framework
