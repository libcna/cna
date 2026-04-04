//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "CNA/Prop.hpp"

namespace Microsoft::Xna::Framework::Graphics {
    struct Viewport {
    private:
        int x;
        int y;

        DEF_PROP(int, Height, getter1, setter1, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Width, getter1, setter1, member1, static0, constret1, ref1, constmet1)

        float minDepth;
        float maxDepth;

        Viewport();
    };
}

