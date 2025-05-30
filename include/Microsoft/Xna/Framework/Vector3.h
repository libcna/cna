//
// Created by robertvokac on 5/24/25.
//

#ifndef VECTOR3_H
#define VECTOR3_H


namespace Microsoft::Xna::Framework {
    struct Vector3 {
    public:
        Vector3(float x, float y ,float z);
        Vector3();
        float X;
        float Y;
        float Z;
    };
}


#endif //VECTOR3_H
