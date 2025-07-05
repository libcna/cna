//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.h"

#include <iostream>
#include <bits/ostream.tcc>

namespace Microsoft::Xna::Framework {
    IMPL_PROP(TimeSpan, TotalGameTime, getter1, setter1, member0, static0, constret1, ref1, constmet1, GameTime, nothing)

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

    IMPL_PROP(bool, IsRunningSlowly, getter1, setter0, member0, static0, constret1, ref1, constmet1, GameTime, nothing)

    GameTime::GameTime(): TotalGameTime_(0), ElapsedGameTime_(TimeSpan(0)), IsRunningSlowly_(false) {
    }

    GameTime::~GameTime() {
    }
}
