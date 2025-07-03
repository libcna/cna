//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    int TouchCollection::getCountProperty() const { return this->touches.size(); }


    std::vector<TouchLocation>::iterator TouchCollection::begin() { return touches.begin(); }
    std::vector<TouchLocation>::iterator TouchCollection::end() { return touches.end(); }

    TouchCollection::TouchCollection() = default;
}
