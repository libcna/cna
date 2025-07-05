//
// Created by robertvokac on 5/31/25.
//

#include "Microsoft/Xna/Framework/Graphics/Viewport.h"

#include <algorithm>

namespace Microsoft::Xna::Framework::Graphics {

    IMPL_PROP(int, Height, getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(int, Width, getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)


    Viewport::Viewport(): x(0), y(0), Height_(0), Width_(0), minDepth(0), maxDepth(0) {
    }
}
