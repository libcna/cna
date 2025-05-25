//
// Created by robertvokac on 5/24/25.
//

#ifndef VECTOR2_H
#define VECTOR2_H


namespace Microsoft::Xna::Framework {
    struct Vector2 {
    public:
        Vector2(float x, float y) : Vector2(0,0) {} // Default constructor
        Vector2();
        float X;
        float Y;
    };
}


#endif //VECTOR2_H
