//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "CNA/CnaHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using CNA::uintcs;
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

        Color(intcs r, intcs g, intcs b, intcs alpha);

        static Color FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a);
    };

    const Color CornflowerBlue = Color(4293760356U);
    const Color White = Color(0, 0, 0, 0);
}
