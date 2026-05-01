//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "System/EventArgs.hpp"
#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework {
    class ExitingEventArgs : System::EventArgs {
    public:
        explicit ExitingEventArgs()
            : Cancel_(false)
        {
        }

    private:
        DEF_PROP(bool, Cancel, getter1, setter1, member1, static0, constret1, ref0, constmet1)
    };
}

