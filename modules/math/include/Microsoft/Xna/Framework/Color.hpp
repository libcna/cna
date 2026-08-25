// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IEquatable.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

#include <string>

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::bytecs;
    using SharpRuntime::intcs;
    using SharpRuntime::uintcs;
    using SharpRuntime::UInt32;

    /** @brief Describes a 32-bit packed RGBA color (packed as AABBGGRR). */
    struct Color : public Graphics::PackedVector::IPackedVectorT<UInt32>,
                   public System::IEquatable<Color>
    {
        // ------------------------------------------------------------------
        // Public component properties
        // ------------------------------------------------------------------

        /** @brief Gets the red component (0–255). */
        [[nodiscard]] bytecs getRProperty() const;
        /** @brief Sets the red component (0–255). */
        void setRProperty(bytecs value);

        /** @brief Gets the green component (0–255). */
        [[nodiscard]] bytecs getGProperty() const;
        /** @brief Sets the green component (0–255). */
        void setGProperty(bytecs value);

        /** @brief Gets the blue component (0–255). */
        [[nodiscard]] bytecs getBProperty() const;
        /** @brief Sets the blue component (0–255). */
        void setBProperty(bytecs value);

        /** @brief Gets the alpha component (0–255). */
        [[nodiscard]] bytecs getAProperty() const;
        /** @brief Sets the alpha component (0–255). */
        void setAProperty(bytecs value);

        // ------------------------------------------------------------------
        // PackedValue property
        // ------------------------------------------------------------------

        /**
         * @brief Gets or sets the packed value of this Color.
         *
         * @return The raw packed AABBGGRR value.
         */
        [[nodiscard]] UInt32 getPackedValueProperty() const override;

        /**
         * @brief Sets the packed value of this Color.
         *
         * @param value The raw packed AABBGGRR value.
         */
        void setPackedValueProperty(UInt32 value) override;

        // ------------------------------------------------------------------
        // Static named colors
        // ------------------------------------------------------------------

        /** @brief Transparent color (R:0, G:0, B:0, A:0). */
        static const Color Transparent;
        /** @brief AliceBlue color (R:240, G:248, B:255, A:255). */
        static const Color AliceBlue;
        /** @brief AntiqueWhite color (R:250, G:235, B:215, A:255). */
        static const Color AntiqueWhite;
        /** @brief Aqua color (R:0, G:255, B:255, A:255). */
        static const Color Aqua;
        /** @brief Aquamarine color (R:127, G:255, B:212, A:255). */
        static const Color Aquamarine;
        /** @brief Azure color (R:240, G:255, B:255, A:255). */
        static const Color Azure;
        /** @brief Beige color (R:245, G:245, B:220, A:255). */
        static const Color Beige;
        /** @brief Bisque color (R:255, G:228, B:196, A:255). */
        static const Color Bisque;
        /** @brief Black color (R:0, G:0, B:0, A:255). */
        static const Color Black;
        /** @brief BlanchedAlmond color (R:255, G:235, B:205, A:255). */
        static const Color BlanchedAlmond;
        /** @brief Blue color (R:0, G:0, B:255, A:255). */
        static const Color Blue;
        /** @brief BlueViolet color (R:138, G:43, B:226, A:255). */
        static const Color BlueViolet;
        /** @brief Brown color (R:165, G:42, B:42, A:255). */
        static const Color Brown;
        /** @brief BurlyWood color (R:222, G:184, B:135, A:255). */
        static const Color BurlyWood;
        /** @brief CadetBlue color (R:95, G:158, B:160, A:255). */
        static const Color CadetBlue;
        /** @brief Chartreuse color (R:127, G:255, B:0, A:255). */
        static const Color Chartreuse;
        /** @brief Chocolate color (R:210, G:105, B:30, A:255). */
        static const Color Chocolate;
        /** @brief Coral color (R:255, G:127, B:80, A:255). */
        static const Color Coral;
        /** @brief CornflowerBlue color (R:100, G:149, B:237, A:255). */
        static const Color CornflowerBlue;
        /** @brief Cornsilk color (R:255, G:248, B:220, A:255). */
        static const Color Cornsilk;
        /** @brief Crimson color (R:220, G:20, B:60, A:255). */
        static const Color Crimson;
        /** @brief Cyan color (R:0, G:255, B:255, A:255). */
        static const Color Cyan;
        /** @brief DarkBlue color (R:0, G:0, B:139, A:255). */
        static const Color DarkBlue;
        /** @brief DarkCyan color (R:0, G:139, B:139, A:255). */
        static const Color DarkCyan;
        /** @brief DarkGoldenrod color (R:184, G:134, B:11, A:255). */
        static const Color DarkGoldenrod;
        /** @brief DarkGray color (R:169, G:169, B:169, A:255). */
        static const Color DarkGray;
        /** @brief DarkGreen color (R:0, G:100, B:0, A:255). */
        static const Color DarkGreen;
        /** @brief DarkKhaki color (R:189, G:183, B:107, A:255). */
        static const Color DarkKhaki;
        /** @brief DarkMagenta color (R:139, G:0, B:139, A:255). */
        static const Color DarkMagenta;
        /** @brief DarkOliveGreen color (R:85, G:107, B:47, A:255). */
        static const Color DarkOliveGreen;
        /** @brief DarkOrange color (R:255, G:140, B:0, A:255). */
        static const Color DarkOrange;
        /** @brief DarkOrchid color (R:153, G:50, B:204, A:255). */
        static const Color DarkOrchid;
        /** @brief DarkRed color (R:139, G:0, B:0, A:255). */
        static const Color DarkRed;
        /** @brief DarkSalmon color (R:233, G:150, B:122, A:255). */
        static const Color DarkSalmon;
        /** @brief DarkSeaGreen color (R:143, G:188, B:139, A:255). */
        static const Color DarkSeaGreen;
        /** @brief DarkSlateBlue color (R:72, G:61, B:139, A:255). */
        static const Color DarkSlateBlue;
        /** @brief DarkSlateGray color (R:47, G:79, B:79, A:255). */
        static const Color DarkSlateGray;
        /** @brief DarkTurquoise color (R:0, G:206, B:209, A:255). */
        static const Color DarkTurquoise;
        /** @brief DarkViolet color (R:148, G:0, B:211, A:255). */
        static const Color DarkViolet;
        /** @brief DeepPink color (R:255, G:20, B:147, A:255). */
        static const Color DeepPink;
        /** @brief DeepSkyBlue color (R:0, G:191, B:255, A:255). */
        static const Color DeepSkyBlue;
        /** @brief DimGray color (R:105, G:105, B:105, A:255). */
        static const Color DimGray;
        /** @brief DodgerBlue color (R:30, G:144, B:255, A:255). */
        static const Color DodgerBlue;
        /** @brief Firebrick color (R:178, G:34, B:34, A:255). */
        static const Color Firebrick;
        /** @brief FloralWhite color (R:255, G:250, B:240, A:255). */
        static const Color FloralWhite;
        /** @brief ForestGreen color (R:34, G:139, B:34, A:255). */
        static const Color ForestGreen;
        /** @brief Fuchsia color (R:255, G:0, B:255, A:255). */
        static const Color Fuchsia;
        /** @brief Gainsboro color (R:220, G:220, B:220, A:255). */
        static const Color Gainsboro;
        /** @brief GhostWhite color (R:248, G:248, B:255, A:255). */
        static const Color GhostWhite;
        /** @brief Gold color (R:255, G:215, B:0, A:255). */
        static const Color Gold;
        /** @brief Goldenrod color (R:218, G:165, B:32, A:255). */
        static const Color Goldenrod;
        /** @brief Gray color (R:128, G:128, B:128, A:255). */
        static const Color Gray;
        /** @brief Green color (R:0, G:128, B:0, A:255). */
        static const Color Green;
        /** @brief GreenYellow color (R:173, G:255, B:47, A:255). */
        static const Color GreenYellow;
        /** @brief Honeydew color (R:240, G:255, B:240, A:255). */
        static const Color Honeydew;
        /** @brief HotPink color (R:255, G:105, B:180, A:255). */
        static const Color HotPink;
        /** @brief IndianRed color (R:205, G:92, B:92, A:255). */
        static const Color IndianRed;
        /** @brief Indigo color (R:75, G:0, B:130, A:255). */
        static const Color Indigo;
        /** @brief Ivory color (R:255, G:255, B:240, A:255). */
        static const Color Ivory;
        /** @brief Khaki color (R:240, G:230, B:140, A:255). */
        static const Color Khaki;
        /** @brief Lavender color (R:230, G:230, B:250, A:255). */
        static const Color Lavender;
        /** @brief LavenderBlush color (R:255, G:240, B:245, A:255). */
        static const Color LavenderBlush;
        /** @brief LawnGreen color (R:124, G:252, B:0, A:255). */
        static const Color LawnGreen;
        /** @brief LemonChiffon color (R:255, G:250, B:205, A:255). */
        static const Color LemonChiffon;
        /** @brief LightBlue color (R:173, G:216, B:230, A:255). */
        static const Color LightBlue;
        /** @brief LightCoral color (R:240, G:128, B:128, A:255). */
        static const Color LightCoral;
        /** @brief LightCyan color (R:224, G:255, B:255, A:255). */
        static const Color LightCyan;
        /** @brief LightGoldenrodYellow color (R:250, G:250, B:210, A:255). */
        static const Color LightGoldenrodYellow;
        /** @brief LightGray color (R:211, G:211, B:211, A:255). */
        static const Color LightGray;
        /** @brief LightGreen color (R:144, G:238, B:144, A:255). */
        static const Color LightGreen;
        /** @brief LightPink color (R:255, G:182, B:193, A:255). */
        static const Color LightPink;
        /** @brief LightSalmon color (R:255, G:160, B:122, A:255). */
        static const Color LightSalmon;
        /** @brief LightSeaGreen color (R:32, G:178, B:170, A:255). */
        static const Color LightSeaGreen;
        /** @brief LightSkyBlue color (R:135, G:206, B:250, A:255). */
        static const Color LightSkyBlue;
        /** @brief LightSlateGray color (R:119, G:136, B:153, A:255). */
        static const Color LightSlateGray;
        /** @brief LightSteelBlue color (R:176, G:196, B:222, A:255). */
        static const Color LightSteelBlue;
        /** @brief LightYellow color (R:255, G:255, B:224, A:255). */
        static const Color LightYellow;
        /** @brief Lime color (R:0, G:255, B:0, A:255). */
        static const Color Lime;
        /** @brief LimeGreen color (R:50, G:205, B:50, A:255). */
        static const Color LimeGreen;
        /** @brief Linen color (R:250, G:240, B:230, A:255). */
        static const Color Linen;
        /** @brief Magenta color (R:255, G:0, B:255, A:255). */
        static const Color Magenta;
        /** @brief Maroon color (R:128, G:0, B:0, A:255). */
        static const Color Maroon;
        /** @brief MediumAquamarine color (R:102, G:205, B:170, A:255). */
        static const Color MediumAquamarine;
        /** @brief MediumBlue color (R:0, G:0, B:205, A:255). */
        static const Color MediumBlue;
        /** @brief MediumOrchid color (R:186, G:85, B:211, A:255). */
        static const Color MediumOrchid;
        /** @brief MediumPurple color (R:147, G:112, B:219, A:255). */
        static const Color MediumPurple;
        /** @brief MediumSeaGreen color (R:60, G:179, B:113, A:255). */
        static const Color MediumSeaGreen;
        /** @brief MediumSlateBlue color (R:123, G:104, B:238, A:255). */
        static const Color MediumSlateBlue;
        /** @brief MediumSpringGreen color (R:0, G:250, B:154, A:255). */
        static const Color MediumSpringGreen;
        /** @brief MediumTurquoise color (R:72, G:209, B:204, A:255). */
        static const Color MediumTurquoise;
        /** @brief MediumVioletRed color (R:199, G:21, B:133, A:255). */
        static const Color MediumVioletRed;
        /** @brief MidnightBlue color (R:25, G:25, B:112, A:255). */
        static const Color MidnightBlue;
        /** @brief MintCream color (R:245, G:255, B:250, A:255). */
        static const Color MintCream;
        /** @brief MistyRose color (R:255, G:228, B:225, A:255). */
        static const Color MistyRose;
        /** @brief Moccasin color (R:255, G:228, B:181, A:255). */
        static const Color Moccasin;
        /** @brief NavajoWhite color (R:255, G:222, B:173, A:255). */
        static const Color NavajoWhite;
        /** @brief Navy color (R:0, G:0, B:128, A:255). */
        static const Color Navy;
        /** @brief OldLace color (R:253, G:245, B:230, A:255). */
        static const Color OldLace;
        /** @brief Olive color (R:128, G:128, B:0, A:255). */
        static const Color Olive;
        /** @brief OliveDrab color (R:107, G:142, B:35, A:255). */
        static const Color OliveDrab;
        /** @brief Orange color (R:255, G:165, B:0, A:255). */
        static const Color Orange;
        /** @brief OrangeRed color (R:255, G:69, B:0, A:255). */
        static const Color OrangeRed;
        /** @brief Orchid color (R:218, G:112, B:214, A:255). */
        static const Color Orchid;
        /** @brief PaleGoldenrod color (R:238, G:232, B:170, A:255). */
        static const Color PaleGoldenrod;
        /** @brief PaleGreen color (R:152, G:251, B:152, A:255). */
        static const Color PaleGreen;
        /** @brief PaleTurquoise color (R:175, G:238, B:238, A:255). */
        static const Color PaleTurquoise;
        /** @brief PaleVioletRed color (R:219, G:112, B:147, A:255). */
        static const Color PaleVioletRed;
        /** @brief PapayaWhip color (R:255, G:239, B:213, A:255). */
        static const Color PapayaWhip;
        /** @brief PeachPuff color (R:255, G:218, B:185, A:255). */
        static const Color PeachPuff;
        /** @brief Peru color (R:205, G:133, B:63, A:255). */
        static const Color Peru;
        /** @brief Pink color (R:255, G:192, B:203, A:255). */
        static const Color Pink;
        /** @brief Plum color (R:221, G:160, B:221, A:255). */
        static const Color Plum;
        /** @brief PowderBlue color (R:176, G:224, B:230, A:255). */
        static const Color PowderBlue;
        /** @brief Purple color (R:128, G:0, B:128, A:255). */
        static const Color Purple;
        /** @brief Red color (R:255, G:0, B:0, A:255). */
        static const Color Red;
        /** @brief RosyBrown color (R:188, G:143, B:143, A:255). */
        static const Color RosyBrown;
        /** @brief RoyalBlue color (R:65, G:105, B:225, A:255). */
        static const Color RoyalBlue;
        /** @brief SaddleBrown color (R:139, G:69, B:19, A:255). */
        static const Color SaddleBrown;
        /** @brief Salmon color (R:250, G:128, B:114, A:255). */
        static const Color Salmon;
        /** @brief SandyBrown color (R:244, G:164, B:96, A:255). */
        static const Color SandyBrown;
        /** @brief SeaGreen color (R:46, G:139, B:87, A:255). */
        static const Color SeaGreen;
        /** @brief SeaShell color (R:255, G:245, B:238, A:255). */
        static const Color SeaShell;
        /** @brief Sienna color (R:160, G:82, B:45, A:255). */
        static const Color Sienna;
        /** @brief Silver color (R:192, G:192, B:192, A:255). */
        static const Color Silver;
        /** @brief SkyBlue color (R:135, G:206, B:235, A:255). */
        static const Color SkyBlue;
        /** @brief SlateBlue color (R:106, G:90, B:205, A:255). */
        static const Color SlateBlue;
        /** @brief SlateGray color (R:112, G:128, B:144, A:255). */
        static const Color SlateGray;
        /** @brief Snow color (R:255, G:250, B:250, A:255). */
        static const Color Snow;
        /** @brief SpringGreen color (R:0, G:255, B:127, A:255). */
        static const Color SpringGreen;
        /** @brief SteelBlue color (R:70, G:130, B:180, A:255). */
        static const Color SteelBlue;
        /** @brief Tan color (R:210, G:180, B:140, A:255). */
        static const Color Tan;
        /** @brief Teal color (R:0, G:128, B:128, A:255). */
        static const Color Teal;
        /** @brief Thistle color (R:216, G:191, B:216, A:255). */
        static const Color Thistle;
        /** @brief Tomato color (R:255, G:99, B:71, A:255). */
        static const Color Tomato;
        /** @brief Turquoise color (R:64, G:224, B:208, A:255). */
        static const Color Turquoise;
        /** @brief Violet color (R:238, G:130, B:238, A:255). */
        static const Color Violet;
        /** @brief Wheat color (R:245, G:222, B:179, A:255). */
        static const Color Wheat;
        /** @brief White color (R:255, G:255, B:255, A:255). */
        static const Color White;
        /** @brief WhiteSmoke color (R:245, G:245, B:245, A:255). */
        static const Color WhiteSmoke;
        /** @brief Yellow color (R:255, G:255, B:0, A:255). */
        static const Color Yellow;
        /** @brief YellowGreen color (R:154, G:205, B:50, A:255). */
        static const Color YellowGreen;

        // ------------------------------------------------------------------
        // Internal / debug property
        // ------------------------------------------------------------------

        /**
         * @brief Returns a debug string @c "R G B A" for display purposes.
         */
        CNAEXT [[nodiscard]] std::string getDebugDisplayStringProperty() const;

        // ------------------------------------------------------------------
        // Constructors
        // ------------------------------------------------------------------

        /**
         * @brief Creates a transparent black color (R:0, G:0, B:0, A:0).
         *
         * Color is a value type, so its .NET default value — @c default(Color), @c new @c Color()
         * and every element of a freshly allocated @c Color[] — is the all-zero packed value. This
         * constructor is that default value, matching the same mapping already used by the other
         * XNA value types such as Vector2, Point and Rectangle.
         */
        Color();

        /**
         * @brief Constructs an RGBA color from the XYZW unit length components of a vector.
         *
         * Each component is clamped to [0,1] then scaled to [0,255].
         *
         * @param color A Vector4 representing a color.
         */
        explicit Color(const Vector4& color);

        /**
         * @brief Constructs an RGBA color from the XYZ unit length components of a vector.
         *
         * Alpha is set to 255 (fully opaque).
         *
         * @param color A Vector3 representing a color.
         */
        explicit Color(const Vector3& color);

        /**
         * @brief Constructs an RGBA color from scalars representing red, green and blue values.
         *
         * Alpha value will be opaque (255).
         *
         * @param r Red component value from 0.0f to 1.0f.
         * @param g Green component value from 0.0f to 1.0f.
         * @param b Blue component value from 0.0f to 1.0f.
         */
        Color(float r, float g, float b);

        /**
         * @brief Constructs an RGBA color from scalars representing red, green, blue and alpha values.
         *
         * @param r Red component value from 0.0f to 1.0f.
         * @param g Green component value from 0.0f to 1.0f.
         * @param b Blue component value from 0.0f to 1.0f.
         * @param alpha Alpha component value from 0.0f to 1.0f.
         */
        Color(float r, float g, float b, float alpha);

        /**
         * @brief Constructs an RGBA color from scalars representing red, green and blue values.
         *
         * Alpha value will be opaque (255). Values are clamped to [0,255].
         *
         * @param r Red component value from 0 to 255.
         * @param g Green component value from 0 to 255.
         * @param b Blue component value from 0 to 255.
         */
        Color(intcs r, intcs g, intcs b);

        /**
         * @brief Constructs an RGBA color from scalars representing red, green, blue and alpha values.
         *
         * Values are clamped to [0,255].
         *
         * @param r Red component value from 0 to 255.
         * @param g Green component value from 0 to 255.
         * @param b Blue component value from 0 to 255.
         * @param alpha Alpha component value from 0 to 255.
         */
        Color(intcs r, intcs g, intcs b, intcs alpha);

        /**
         * @brief Constructs an RGBA color from 8-bit byte components.
         *
         * Alpha value will be opaque (255).
         *
         * @param r Red component value from 0 to 255.
         * @param g Green component value from 0 to 255.
         * @param b Blue component value from 0 to 255.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. CNA convenience overload.
         */
        CNAEXT Color(bytecs r, bytecs g, bytecs b);

        /**
         * @brief Constructs an RGBA color from 8-bit byte components including alpha.
         *
         * @param r Red component value from 0 to 255.
         * @param g Green component value from 0 to 255.
         * @param b Blue component value from 0 to 255.
         * @param alpha Alpha component value from 0 to 255.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. CNA convenience overload.
         */
        CNAEXT Color(bytecs r, bytecs g, bytecs b, bytecs alpha);

        // ------------------------------------------------------------------
        // Public instance methods
        // ------------------------------------------------------------------

        /**
         * @brief Compares whether current instance is equal to specified Color.
         *
         * @param other The Color to compare.
         * @return @c true if the instances are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Color& other) const override;

        /**
         * @brief Gets a Vector3 representation for this object.
         *
         * @return A Vector3 representation for this object.
         */
        [[nodiscard]] Vector3 ToVector3() const;

        /**
         * @brief Gets a Vector4 representation for this object.
         *
         * @return A Vector4 representation for this object.
         */
        [[nodiscard]] Vector4 ToVector4() const;

        /**
         * @brief Gets the hash code of this Color.
         *
         * @return Hash code of this Color.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a string representation of this Color in the format:
         * {R:[red] G:[green] B:[blue] A:[alpha]}
         *
         * @return String representation of this Color.
         */
        [[nodiscard]] std::string ToString() const;

        // ------------------------------------------------------------------
        // Public static methods
        // ------------------------------------------------------------------

        /**
         * @brief Performs linear interpolation of Color.
         *
         * The @p amount is clamped to [0,1].
         *
         * @param value1 Source Color.
         * @param value2 Destination Color.
         * @param amount Interpolation factor.
         * @return Interpolated Color.
         */
        static Color Lerp(const Color& value1, const Color& value2, float amount);

        /**
         * @brief Translates a non-premultiplied alpha Color to a Color that contains premultiplied alpha.
         *
         * @param vector A Vector4 representing color.
         * @return A Color which contains premultiplied alpha data.
         */
        static Color FromNonPremultiplied(const Vector4& vector);

        /**
         * @brief Translates a non-premultiplied alpha Color to a Color that contains premultiplied alpha.
         *
         * @param r Red component value.
         * @param g Green component value.
         * @param b Blue component value.
         * @param a Alpha component value.
         * @return A Color which contains premultiplied alpha data.
         */
        static Color FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a);

        /**
         * @brief Multiplies a Color by a scalar value.
         *
         * @param value Source Color.
         * @param scale Multiplicator.
         * @return Multiplication result.
         */
        static Color Multiply(const Color& value, float scale);

        // ------------------------------------------------------------------
        // IPackedVector member  (non-generic interface)
        // ------------------------------------------------------------------

        /**
         * @brief Packs a four-component color from a vector format into the format of a color object.
         *
         * Implements the non-generic @c IPackedVector.PackFromVector4.
         *
         * @param vector A four-component color.
         */
        void PackFromVector4(const Vector4& vector) override;

    private:
        // R stored in bits 0-7, G in 8-15, B in 16-23, A in 24-31 (value = AABBGGRR).
        uintcs packedValue;

        /** @brief Constructs a Color directly from a raw packed AABBGGRR value. Used by static color definitions. */
        explicit Color(UInt32 packedValue);
    };

    // ------------------------------------------------------------------
    // Operators
    // ------------------------------------------------------------------

    /**
     * @brief Compares whether two Color instances are equal.
     *
     * @param a Color instance on the left of the equal sign.
     * @param b Color instance on the right of the equal sign.
     * @return @c true if the instances are equal; @c false otherwise.
     */
    [[nodiscard]] bool operator==(const Color& a, const Color& b);

    /**
     * @brief Compares whether two Color instances are not equal.
     *
     * @param a Color instance on the left of the not equal sign.
     * @param b Color instance on the right of the not equal sign.
     * @return @c true if the instances are not equal; @c false otherwise.
     */
    [[nodiscard]] bool operator!=(const Color& a, const Color& b);

    /**
     * @brief Multiplies a Color by a scalar value.
     *
     * @param value Source Color.
     * @param scale Multiplicator.
     * @return Multiplication result.
     */
    [[nodiscard]] Color operator*(const Color& value, float scale);

    /**
     * @brief Multiplies a Color by a scalar value.
     *
     * @param scale Multiplicator.
     * @param value Source Color.
     * @return Multiplication result.
     *
     * @note CNAEXT — commutative form not present in XNA 4.0 / FNA. CNA convenience overload.
     */
    CNAEXT [[nodiscard]] Color operator*(float scale, const Color& value);
} // namespace Microsoft::Xna::Framework
