//
// Created by robertvokac on 5/30/25.
//

#include "System/TimeSpan.h"

#include "Microsoft/Xna/Framework/GameTime.h"

namespace System {

    const TimeSpan TimeSpan::Zero = TimeSpan(0);
    const TimeSpan TimeSpan::MaxValue = TimeSpan(CSLONG_MAX);

    const TimeSpan TimeSpan::MinValue = TimeSpan(CSLONG_MIN);


    TimeSpan::TimeSpan(cslong ticks): ticks_internal(ticks) {
    }

    TimeSpan::TimeSpan(csint hours, csint minutes, csint seconds): ticks_internal(
        TimeToTicks(hours, minutes, seconds)) {
    }

    TimeSpan::TimeSpan(csint days, csint hours, csint minutes, csint seconds, csint milliseconds,
                       csint microseconds): ticks_internal(
        TimeToTicks(days, hours, minutes, seconds, milliseconds, microseconds) * TicksPerMicrosecond) {
    }

    cslong TimeSpan::TimeToTicks(csint days, csint hours, csint minutes, csint seconds, csint milliseconds,
                                csint microseconds) {
        long totalMicroseconds = (((cslong) days * 3600 * 24 + (cslong) hours * 3600 + (cslong) minutes * 60 + seconds) *
                                  1000 + milliseconds) * 1000 + microseconds;
        if (totalMicroseconds > MaxMicroSeconds || totalMicroseconds < MinMicroSeconds)
            throw ArgumentOutOfRangeException("Time span is too long.");
        return totalMicroseconds;
    }

    TimeSpan &TimeSpan::operator=(const TimeSpan &) {
        return *this;
    }
    cslong TimeSpan::getTicks() const {
        return ticks_internal;
    }


    [[nodiscard]] int TimeSpan::getDays() const { return (csint) (ticks_internal / TicksPerDay); }


    [[nodiscard]] int TimeSpan::getHours() const { return (csint) ((ticks_internal / TicksPerHour) % 24); }


    [[nodiscard]] csint TimeSpan::getMilliseconds() const {
        return (csint) ((ticks_internal / TicksPerMillisecond) % 1000);
    }


    [[nodiscard]] csint TimeSpan::getMicroseconds() const {
        return (csint) ((ticks_internal / TicksPerMicrosecond) % 1000);
    }

    /**
     * @brief Retrieves the nanosecond portion of the time span.
     *
     * Provides access to the nanosecond component of the current TimeSpan instance.
     *
     * @return The number of whole nanoseconds in the time span.
     *
     * @details The `Nanoseconds` property returns only full nanoseconds, while
     * `TotalNanoseconds` provides both complete and fractional values.
     */


    [[nodiscard]] csint TimeSpan::getNanoseconds() const {
        return (csint) ((ticks_internal % TicksPerMicrosecond) * 100);
    }


    [[nodiscard]] csint TimeSpan::getMinutes() const { return (csint) ((ticks_internal / TicksPerMinute) % 60); }


    [[nodiscard]] csint TimeSpan::getSeconds() const { return (csint) ((ticks_internal / TicksPerSecond) % 60); }


    [[nodiscard]] double TimeSpan::getTotalDays() const { return ((double) ticks_internal) / TicksPerDay; }


    [[nodiscard]] double TimeSpan::getTotalHours() const { return (double) ticks_internal / TicksPerHour; }


    [[nodiscard]] double TimeSpan::getTotalMilliseconds() const {
        double temp = (double) ticks_internal / TicksPerMillisecond;
        if (temp > MaxMilliSeconds)
            return (double) MaxMilliSeconds;

        if (temp < MinMilliSeconds)
            return (double) MinMilliSeconds;

        return temp;
    }

    [[nodiscard]] double TimeSpan::getTotalMicroseconds() const {
        return (double) ticks_internal / TicksPerMicrosecond;
    }


    [[nodiscard]] double TimeSpan::getTotalNanoseconds() const { return (double) ticks_internal * NanosecondsPerTick; }

    [[nodiscard]] double TimeSpan::getTotalMinutes() const { return (double) ticks_internal / TicksPerMinute; }

    [[nodiscard]] double TimeSpan::getTotalSeconds() const { return (double) ticks_internal / TicksPerSecond; }

    ////
    long TimeSpan::TimeToTicks(int hour, int minute, int second)
    {
        // totalSeconds is bounded by 2^31 * 2^12 + 2^31 * 2^8 + 2^31,
        // which is less than 2^44, meaning we won't overflow totalSeconds.
        long totalSeconds = (long)hour * 3600 + (long)minute * 60 + (long)second;
        if (totalSeconds > MaxSeconds || totalSeconds < MinSeconds)
            throw ArgumentOutOfRangeException("Time span is too long.");
        return totalSeconds * TicksPerSecond;
    }

    TimeSpan TimeSpan::FromTicks(long i) {return 0;
    }

    TimeSpan TimeSpan::FromSeconds(double x) {return 0;
    }

    TimeSpan TimeSpan::FromMilliseconds(double value) {
        return 0;
    }
}
