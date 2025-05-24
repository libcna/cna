//
// Created by robertvokac on 5/24/25.
//

#ifndef VIEWPORT_H
#define VIEWPORT_H
#include "NeoSdk/Property.h"


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
        NeoSdk::Property<int> Width;
        NeoSdk::Property<int> Height;

        Viewport();
    };
}


#endif //VIEWPORT_H
