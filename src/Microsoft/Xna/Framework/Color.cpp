//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Color.hpp"

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework {

    Color::Color(SharpRuntime::uintcs packedValue) : packedValue(packedValue)
    {
    }

    Color::Color(bytecs r, bytecs g, bytecs b, bytecs alpha)
    {
        if ((static_cast<uintcs>(r | g | b | alpha) & ~uintcs{0xFF}) != uintcs{0})
        {
            r = MathHelper::Clamp(r, 0, SharpRuntime::BYTE_MAX);
            g = MathHelper::Clamp(g, 0, SharpRuntime::BYTE_MAX);
            b = MathHelper::Clamp(b, 0, SharpRuntime::BYTE_MAX);
            alpha = MathHelper::Clamp(alpha, 0, SharpRuntime::BYTE_MAX);
        }

        this->packedValue =
            (static_cast<uintcs>(alpha) << 24) |
            (static_cast<uintcs>(b)     << 16) |
            (static_cast<uintcs>(g)     << 8)  |
            static_cast<uintcs>(r);
    }

    bytecs Color::getRProperty() const
    {
        return static_cast<bytecs>(packedValue & uintcs{0xFF});
    }

    bytecs Color::getGProperty() const
    {
        return static_cast<bytecs>((packedValue >> 8) & uintcs{0xFF});
    }

    bytecs Color::getBProperty() const
    {
        return static_cast<bytecs>((packedValue >> 16) & uintcs{0xFF});
    }

    bytecs Color::getAProperty() const
    {
        return static_cast<bytecs>((packedValue >> 24) & uintcs{0xFF});
    }

    Color Color::FromNonPremultiplied(intcs r, intcs g, intcs b, intcs a)
    {
        return {
            static_cast<bytecs>(r * a / static_cast<bytecs>(SharpRuntime::BYTE_MAX)),
            static_cast<bytecs>(g * a / static_cast<bytecs>(SharpRuntime::BYTE_MAX)),
            static_cast<bytecs>(b * a / static_cast<bytecs>(SharpRuntime::BYTE_MAX)),
            static_cast<bytecs>(a)
        };
    }

}