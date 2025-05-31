//
// Created by robertvokac on 5/31/25.
//

#include "Microsoft/Xna/Framework/Graphics/Viewport.h"

namespace Microsoft::Xna::Framework::Graphics {
    idata(int, Height, Viewport)
    idata(int, Width, Viewport)

    Viewport::Viewport(): x(0), y(0), Height_(0), Width_(0), minDepth(0), maxDepth(0) {
    }
}
