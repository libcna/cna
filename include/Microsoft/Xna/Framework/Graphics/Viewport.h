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
        int width;
        int height;
        float minDepth;
        float maxDepth;

    public:
        [[nodiscard]] int WidthProperty() const;

    public:
        void WidthProperty(int v);

    public:
        [[nodiscard]] int HeightProperty() const;

    public:
        void HeightProperty(int v);

        Viewport();
    };
}


#endif //VIEWPORT_H
