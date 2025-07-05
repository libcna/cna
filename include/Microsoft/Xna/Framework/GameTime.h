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


#endif //GAMETIME_H
