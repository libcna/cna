//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "CNA/CnaHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using CNA::uintcs;
    using CNA::bytecs;
    using CNA::intcs;

    /**
     * @note Status: Partial
     */
    struct Color
    {
    private:
        CNA::uintcs packedValue;

    public:
        explicit Color(CNA::uintcs packedValue);

        Color(bytecs r, bytecs g, bytecs b, bytecs alpha);

        [[nodiscard]] bytecs getRProperty() const;
        [[nodiscard]] bytecs getGProperty() const;
        [[nodiscard]] bytecs getBProperty() const;
        [[nodiscard]] bytecs getAProperty() const;

        static Color FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a);
    };

    const Color CornflowerBlue = Color(4293760356U);
    const Color White = Color(0, 0, 0, 0);
}