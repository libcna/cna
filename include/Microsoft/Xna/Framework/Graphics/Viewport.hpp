//
// Created by robertvokac on 5/31/25.
//

#pragma once

#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Describes the drawable area of the render target.
     */
    class Viewport
    {
    private:
        int Height_;
        int Width_;

    public:
        int x;
        int y;
        float minDepth;
        float maxDepth;

        DEF_PROP(int, Height, getter1, setter1, member0, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Width, getter1, setter1, member0, static0, constret1, ref1, constmet1)

        /**
         * @brief Constructs an empty viewport.
         */
        Viewport();
    };
}
