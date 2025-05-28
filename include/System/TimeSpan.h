//
// Created by robertvokac on 5/26/25.
//

#ifndef TIMESPAN_H
#define TIMESPAN_H

namespace System {
    struct TimeSpan {
    public:
        DEF_PROP_AUTO(long, Ticks, 0)

        TimeSpan() : IMPL_PROP_AUTO(long, Ticks) {
        }

        static TimeSpan FromTicks(long i);

        static TimeSpan FromSeconds(double x);
    };
} // System

#endif //TIMESPAN_H
