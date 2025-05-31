//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.h"

namespace Microsoft::Xna::Framework {
    idata(TimeSpan, TotalGameTime, GameTime)
    idata(TimeSpan, ElapsedGameTime, GameTime)

    GameTime::GameTime(): TotalGameTime_(0), ElapsedGameTime_(0) {
    }
}
