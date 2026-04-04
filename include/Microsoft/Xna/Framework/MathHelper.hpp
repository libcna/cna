//
// Created by robertvokac on 6/7/25.
//

#pragma once


namespace Microsoft::Xna::Framework {
    //static class
    class MathHelper {
    public:
        static int Clamp(int value, const int& min, const int& max);
    };
}

