//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Color.h"

#include "CNA/CnaHelper.h"
#include <limits>

namespace Microsoft::Xna::Framework {
    Color Color::FromNonPremultiplied(int r, int g, int b, int a) {
        return Color(r * a / (int) BYTE_MAX, g * a / (int) BYTE_MAX, b * a / (int) BYTE_MAX, a);
    }

}
