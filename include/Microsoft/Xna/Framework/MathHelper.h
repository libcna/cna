//
// Created by robertvokac on 6/7/25.
//

#ifndef MATHHELPER_H
#define MATHHELPER_H


namespace Microsoft::Xna::Framework {
    //static class
    class MathHelper {
    public:
        static int Clamp(int value, const int& min, const int& max);
    };
}


#endif //MATHHELPER_H
