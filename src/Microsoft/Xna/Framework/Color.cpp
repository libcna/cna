//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Color.hpp"

#include "CNA/CnaHelper.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework {

    Color::Color(CNA::uintcs packedValue) : packedValue(packedValue)
    {
    }

    Color::Color(intcs r, intcs g, intcs b, intcs alpha)
    {
        if ((static_cast<uintcs>(r | g | b | alpha) & ~uintcs{0xFF}) != uintcs{0})
        {
            r = MathHelper::Clamp(r, 0, CNA::BYTE_MAX);
            g = MathHelper::Clamp(g, 0, CNA::BYTE_MAX);
            b = MathHelper::Clamp(b, 0, CNA::BYTE_MAX);
            alpha = MathHelper::Clamp(alpha, 0, CNA::BYTE_MAX);
        }

        this->packedValue =
            (static_cast<uintcs>(alpha) << 24) |
            (static_cast<uintcs>(b)     << 16) |
            (static_cast<uintcs>(g)     << 8)  |
            static_cast<uintcs>(r);
    }

    Color Color::FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a)
    {
        return {
            r * a / static_cast<intcs>(CNA::BYTE_MAX),
            g * a / static_cast<intcs>(CNA::BYTE_MAX),
            b * a / static_cast<intcs>(CNA::BYTE_MAX),
            a
        };
    }

}
