//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMETIME_H
#define GAMETIME_H

#include "System/TimeSpan.h"

namespace Microsoft::Xna::Framework {
    using System::TimeSpan;
class GameTime {
public:
    DEF_PROP_AUTO(TimeSpan, TotalGameTime, TimeSpan())
    DEF_PROP_AUTO(TimeSpan, ElapsedGameTime, TimeSpan())
    GameTime();
};

}



#endif //GAMETIME_H
