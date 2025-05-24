//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Graphics/Viewport.h"
namespace Microsoft::Xna::Framework::Graphics {
    Viewport::Viewport(): x(0), y(0), width(0), height(0), minDepth(0), maxDepth(0),
                          Width([this]() { return 0; }),
                          Height([this]() { return 0; })
    {
    }
}

