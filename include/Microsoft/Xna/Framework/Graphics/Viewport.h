//
// Created by robertvokac on 5/24/25.
//

#ifndef VIEWPORT_H
#define VIEWPORT_H
#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Graphics {
    struct Viewport {
    private:
        int x;
        int y;
        ddata(int, Height)

        ddata(int, Width)

        float minDepth;
        float maxDepth;

        Viewport();
    };
}


#endif //VIEWPORT_H
