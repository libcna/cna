//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.hpp"

#include <utility>

namespace Microsoft::Xna::Framework
{
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

    const TimeSpan& GameTime::getTotalGameTimeProperty() const
    {
        return TotalGameTime_;
    }

    const TimeSpan& GameTime::getElapsedGameTimeProperty() const
    {
        return ElapsedGameTime_;
    }

    bool GameTime::getIsRunningSlowlyProperty() const
    {
        return IsRunningSlowly_;
    }

    void GameTime::setTotalGameTimeProperty(const TimeSpan& value)
    {
        TotalGameTime_ = value;
    }

    void GameTime::setTotalGameTimeProperty(TimeSpan&& value)
    {
        TotalGameTime_ = std::move(value);
    }

    void GameTime::setElapsedGameTimeProperty(const TimeSpan& value)
    {
        ElapsedGameTime_ = value;
    }

    void GameTime::setElapsedGameTimeProperty(TimeSpan&& value)
    {
        ElapsedGameTime_ = std::move(value);
    }

    void GameTime::setIsRunningSlowlyProperty(bool value)
    {
        IsRunningSlowly_ = value;
    }
} // namespace Microsoft::Xna::Framework
