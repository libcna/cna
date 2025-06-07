//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.h"

#include <iostream>
#include <bits/ostream.tcc>

namespace Microsoft::Xna::Framework {
    idata(TimeSpan, TotalGameTime, GameTime)
    TimeSpan* GameTime::getElapsedGameTimeProperty() { return ElapsedGameTime_; }
    void GameTime::setElapsedGameTimeProperty(TimeSpan* v) {
        ElapsedGameTime_ = v;
    }
    idata(bool, IsRunningSlowly, GameTime)

    GameTime::GameTime(): TotalGameTime_(0), ElapsedGameTime_(0) {
    }

}
