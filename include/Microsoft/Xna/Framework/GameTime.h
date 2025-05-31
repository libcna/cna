//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMETIME_H
#define GAMETIME_H

#include "CNA/Prop.h"
#include "System/TimeSpan.h"

namespace Microsoft::Xna::Framework {
    using System::TimeSpan;
class GameTime {
public:
    ddata(TimeSpan, TotalGameTime)
    ddata(TimeSpan, ElapsedGameTime)
    GameTime();
};

}



#endif //GAMETIME_H
