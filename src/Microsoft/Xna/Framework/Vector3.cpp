//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Vector3.hpp"
namespace Microsoft::Xna::Framework {
    Vector3::Vector3(const float x, const float y , const float z) : X(x), Y(y), Z(z) {

    }
    Vector3::Vector3() :Vector3(0,0,0) {

    }
}