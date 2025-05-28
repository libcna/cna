//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMETIME_H
#define GAMETIME_H
#include "NeoSdk/Property.h"
#include "System/TimeSpan.h"

namespace Microsoft::Xna::Framework {
class GameTime {
    using System::TimeSpan;
    DEF_PROP_AUTO(TimeSpan, TotalGameTime, TimeSpan())
    GameTime() : IMPL_PROP_AUTO(TimeSpan, TotalGameTime) {
    }
};

}



#endif //GAMETIME_H
