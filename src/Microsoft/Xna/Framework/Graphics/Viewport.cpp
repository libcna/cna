//
// Created by robertvokac on 5/31/25.
//

#include "Microsoft/Xna/Framework/Graphics/Viewport.h"

namespace Microsoft::Xna::Framework::Graphics {
    int Viewport::WidthProperty() const { return width; }
    void Viewport::WidthProperty(int v) { width = v; }
    int Viewport::HeightProperty() const { return height; }
    void Viewport::HeightProperty(int v) { height = v; }
}
