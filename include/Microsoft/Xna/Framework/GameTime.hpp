//
// Created by robertvokac on 5/28/25.
//

#pragma once

#include "CNA/Prop.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework {
    using System::TimeSpan;

    class GameTime {
    public:
        DEF_PROP(TimeSpan, TotalGameTime, getter1, setter1, member1, static0, constret1, ref1, constmet1)


    private:
        TimeSpan ElapsedGameTime_;

    public:
        [[nodiscard]] TimeSpan& getElapsedGameTimeProperty();

    public:
        void setElapsedGameTimeProperty(const TimeSpan& v);
        void setElapsedGameTimeProperty(TimeSpan&& v);

        DEF_PROP(bool, IsRunningSlowly, getter1, setter1, member1, static0, constret1, ref1, constmet1)


        GameTime();
        ~GameTime();
    };
}
