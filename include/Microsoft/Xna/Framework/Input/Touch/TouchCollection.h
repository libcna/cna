//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHCOLLECTION_H
#define TOUCHCOLLECTION_H
#include <vector>

#include "TouchLocation.h"
#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchCollection {
    private: std::vector<TouchLocation> touches;
        public: [[nodiscard]] int getCountProperty() const;
        DEF_PROP(int, Count, getter1, setter0, member0, static0, constret0, ref0, constmet0)


        std::vector<TouchLocation>::iterator begin();
        std::vector<TouchLocation>::iterator end();
        TouchCollection();
    };
}


#endif //TOUCHCOLLECTION_H
