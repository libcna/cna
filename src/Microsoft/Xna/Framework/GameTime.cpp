//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.h"

#include <iostream>
#include <bits/ostream.tcc>

namespace Microsoft::Xna::Framework {
    idata(TimeSpan, TotalGameTime, GameTime)
    TimeSpan& GameTime::getElapsedGameTimeProperty() {
        return ElapsedGameTime_;
    }
    void GameTime::setElapsedGameTimeProperty(const TimeSpan& v) {
        ElapsedGameTime_ = v;
    }

    void GameTime::setElapsedGameTimeProperty(TimeSpan&& v)
    {
        ElapsedGameTime_ = std::move(v);  // Uses move semantics to avoid unnecessary copy
    }
    idata(bool, IsRunningSlowly, GameTime)

    GameTime::GameTime(): TotalGameTime_(0), ElapsedGameTime_(TimeSpan(0)), IsRunningSlowly_(false) {
    }

    GameTime::~GameTime() {
    }
}
