//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.hpp"

#include <utility>

namespace Microsoft::Xna::Framework {

    IMPL_PROP(TimeSpan, TotalGameTime, getter1, setter1, member0, static0, constret1, ref1, constmet1, GameTime, nothing)
    IMPL_PROP(bool, IsRunningSlowly, getter1, setter1, member0, static0, constret1, ref1, constmet1, GameTime, nothing)

    const TimeSpan& GameTime::getElapsedGameTimeProperty() const
    {
        return ElapsedGameTime_;
    }

    void GameTime::setElapsedGameTimeProperty(const TimeSpan& v)
    {
        ElapsedGameTime_ = v;
    }

    void GameTime::setElapsedGameTimeProperty(TimeSpan&& v)
    {
        ElapsedGameTime_ = std::move(v);
    }

    GameTime::GameTime()
        : TotalGameTime_(TimeSpan::Zero),
          ElapsedGameTime_(TimeSpan::Zero),
          IsRunningSlowly_(false)
    {
    }

    GameTime::GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime)
        : TotalGameTime_(std::move(totalGameTime)),
          ElapsedGameTime_(std::move(elapsedGameTime)),
          IsRunningSlowly_(false)
    {
    }

    GameTime::GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime, bool isRunningSlowly)
        : TotalGameTime_(std::move(totalGameTime)),
          ElapsedGameTime_(std::move(elapsedGameTime)),
          IsRunningSlowly_(isRunningSlowly)
    {
    }

} // namespace Microsoft::Xna::Framework