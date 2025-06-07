//
// Created by robertvokac on 5/24/25.
//

#ifndef COLOR_H
#define COLOR_H
#include "CNA/CnaHelper.h"
#include <limits>

#include "MathHelper.h"


namespace Microsoft::Xna::Framework {
    using CNA::uintcs;
    struct Color {
    private:
        CNA::uintcs packedValue;


    public:
        Color(CNA::uintcs packedValue) : packedValue(packedValue){

        }
        Color(int r, int g, int b, int alpha) {
            if (((long) (r | g | b | alpha) & 4294967040L) != 0L)
            {
                uintcs num1 = (uintcs) MathHelper::Clamp(r, 0, (int) BYTE_MAX);
                uintcs num2 = (uintcs) MathHelper::Clamp(g, 0, (int) BYTE_MAX);
                uintcs num3 = (uintcs) MathHelper::Clamp(b, 0, (int) BYTE_MAX);
                this->packedValue = (uintcs) (MathHelper::Clamp(alpha, 0, (int) BYTE_MAX) << 24 | (int) num3 << 16 /*0x10*/ | (int) num2 << 8) | num1;
            }
            else
                this->packedValue = (uintcs) (alpha << 24 | b << 16 /*0x10*/ | g << 8 | r);
        }

        static Color FromNonPremultiplied(int i, int i1, int i2, int i3);
    };

    const Color CornflowerBlue = Color(4293760356U);
    const Color White = Color(0, 0, 0, 0);
}


#endif //COLOR_H
