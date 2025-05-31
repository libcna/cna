//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHLOCATION_H
#define TOUCHLOCATION_H
#include "TouchLocationState.h"
#include "CNA/Prop.h"

#include "Microsoft/Xna/Framework/Vector2.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchLocation {
    dgetter(TouchLocationState, State)
        dgetter(Vector2, PositionProperty_)

    private:
        Vector2 PositionProperty_ = Vector2();

    public:
        Vector2 PositionProperty() const;

    public:
        void PositionProperty(Vector2 v);

        TouchLocation();
    };
}

#endif //TOUCHLOCATION_H
