//
// Created by robertvokac on 5/30/25.
//

#include "System/TimeSpan.h"

#include "Microsoft/Xna/Framework/GameTime.h"

namespace System {

    const TimeSpan TimeSpan::Zero = TimeSpan(0);
    const TimeSpan TimeSpan::MaxValue = TimeSpan(int64_max);

    const TimeSpan TimeSpan::MinValue = TimeSpan(int64_min);


    TimeSpan::TimeSpan(int64 ticks): ticks_internal(ticks) {
    }

    TimeSpan::TimeSpan(int32 hours, int32 minutes, int32 seconds): ticks_internal(
        TimeToTicks(hours, minutes, seconds)) {
    }

    TimeSpan::TimeSpan(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds,
                       int32 microseconds): ticks_internal(
        TimeToTicks(days, hours, minutes, seconds, milliseconds, microseconds) * TicksPerMicrosecond) {
    }

    int64 TimeSpan::TimeToTicks(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds,
                                int32 microseconds) {
        long totalMicroseconds = (((int64) days * 3600 * 24 + (int64) hours * 3600 + (int64) minutes * 60 + seconds) *
                                  1000 + milliseconds) * 1000 + microseconds;
        if (totalMicroseconds > MaxMicroSeconds || totalMicroseconds < MinMicroSeconds)
            throw ArgumentOutOfRangeException("Time span is too long.");
        return totalMicroseconds;
    }

    TimeSpan &TimeSpan::operator=(const TimeSpan &) {
        return *this;
    }
    int64 TimeSpan::getTicks() const {
        return ticks_internal;
    }


    [[nodiscard]] int TimeSpan::getDays() const { return (int32) (ticks_internal / TicksPerDay); }


    [[nodiscard]] int TimeSpan::getHours() const { return (int32) ((ticks_internal / TicksPerHour) % 24); }


    [[nodiscard]] int32 TimeSpan::getMilliseconds() const {
        return (int32) ((ticks_internal / TicksPerMillisecond) % 1000);
    }


    [[nodiscard]] int32 TimeSpan::getMicroseconds() const {
        return (int32) ((ticks_internal / TicksPerMicrosecond) % 1000);
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


    [[nodiscard]] int32 TimeSpan::getNanoseconds() const {
        return (int32) ((ticks_internal % TicksPerMicrosecond) * 100);
    }


    [[nodiscard]] int32 TimeSpan::getMinutes() const { return (int32) ((ticks_internal / TicksPerMinute) % 60); }


    [[nodiscard]] int32 TimeSpan::getSeconds() const { return (int32) ((ticks_internal / TicksPerSecond) % 60); }


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
