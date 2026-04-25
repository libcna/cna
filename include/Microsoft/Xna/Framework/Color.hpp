//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "CppDotNet/CppDotNetHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using CppDotNet::uintcs;
    using CppDotNet::bytecs;
    using CppDotNet::intcs;

    /**
     * @note Status: Partial
     */
    struct Color
    {
    private:
        CppDotNet::uintcs packedValue;

    public:
        explicit Color(CppDotNet::uintcs packedValue);

        Color(bytecs r, bytecs g, bytecs b, bytecs alpha);

        [[nodiscard]] bytecs getRProperty() const;
        [[nodiscard]] bytecs getGProperty() const;
        [[nodiscard]] bytecs getBProperty() const;
        [[nodiscard]] bytecs getAProperty() const;

        static Color FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a);
    };

    // todo: should be static and part of the Color class
    const Color CornflowerBlue = Color(4293760356U);
    const Color White = Color(255, 255, 255, 255);
    const Color Green = Color(4278222848U);
}