//
// Created by robertvokac on 5/26/25.
//

#ifndef TIMESPAN_H
#define TIMESPAN_H
#include <limits>

#include "System/ArgumentOutOfRangeException.h"
#include "CNA/Helper.h"

namespace System {
    using CNA::int32;
    using CNA::int64;
    /**
     * @class TimeSpan
     * @brief Represents a duration of time, which can be either positive or negative.
     *
     * TimeSpan is stored internally as a number of ticks, where a tick represents
     * 100 nanoseconds. This allows precise representation of hours, minutes, and
     * days. However, longer time periods such as months or years are not neatly
     * expressible due to variations in calendar calculations.
     *
     * For example:
     * - A month can range from 28 to 31 days.
     * - A year may consist of 365 or 366 days.
     * - A decade might include between 1 and 3 leap years.
     *
     * Due to these inconsistencies, TimeSpan does not offer direct methods
     * for retrieving months or years.
     */
    struct TimeSpan {
    public:
        /**
         * @brief Defines the number of nanoseconds in 1 tick.
         */
        static constexpr int64 NanosecondsPerTick = 100;

        /**
         * @brief Defines the number of ticks in 1 microsecond.
         */
        static constexpr int64 TicksPerMicrosecond = 10;

        /**
         * @brief Defines the number of ticks in 1 millisecond.
         */
        static constexpr int64 TicksPerMillisecond = TicksPerMicrosecond * 1000; // 10,000
        /**
         * @brief Defines the number of ticks in 1 second.
         */
        static constexpr int64 TicksPerSecond = TicksPerMillisecond * 1000; // 10,000,000
        /**
         * @brief Defines the number of ticks in 1 minute.
         */
        static constexpr int64 TicksPerMinute = TicksPerSecond * 60; // 600,000,000
        /**
         * @brief Defines the number of ticks in 1 hour.
         */
        static constexpr int64 TicksPerHour = TicksPerMinute * 60; // 36,000,000,000
        /**
         * @brief Defines the number of ticks in 1 day.
         */
        static constexpr int64 TicksPerDay = TicksPerHour * 24; // 864,000,000,000

    private:
        static constexpr int64 MaxSeconds = int64_max / TicksPerSecond;
        static constexpr int64 MinSeconds = int64_min / TicksPerSecond;

        static constexpr int64 MaxMilliSeconds = int64_max / TicksPerMillisecond;
        static constexpr int64 MinMilliSeconds = int64_min / TicksPerMillisecond;

        static constexpr int64 MaxMicroSeconds = int64_max / TicksPerMicrosecond;
        static constexpr int64 MinMicroSeconds = int64_min / TicksPerMicrosecond;

        static constexpr int64 TicksPerTenthSecond = TicksPerMillisecond * 100;

    public:
        static const TimeSpan Zero;

    public:
        static const TimeSpan MaxValue;

    public:
        static const TimeSpan MinValue;

    private:
        const int64 ticks_internal;

    public:
        TimeSpan(int64 ticks);

    public:
        TimeSpan(int32 hours, int32 minutes, int32 seconds);

        /**
         * @brief Constructs a TimeSpan object using specified time components.
         *
         * Creates a TimeSpan instance based on the given number of days, hours, minutes, seconds,
         * milliseconds, and microseconds.
         *
         * @param days The number of days.
         * @param hours The number of hours.
         * @param minutes The number of minutes.
         * @param seconds The number of seconds.
         * @param milliseconds The number of milliseconds.
         * @param microseconds The number of microseconds.
         *
         * @details The provided values are converted into internal tick units to initialize this instance.
         *
         * @throws std::out_of_range If the calculated TimeSpan falls outside the acceptable range.
         * The acceptable range is between TimeSpan at least MinValue and at most MaxValue.
         */
    public:
        TimeSpan(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds = 0,
                 int32 microseconds = 0);

    public: TimeSpan &operator=(const TimeSpan &);

    private:
        static int64 TimeToTicks(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds,
                                 int32 microseconds);

    public:
        [[nodiscard]] int64 getTicks() const;

    public:
        [[nodiscard]] int getDays() const;

    public:
        [[nodiscard]] int getHours() const;

    public:
        [[nodiscard]] int32 getMilliseconds() const;

        /**
         * @brief Retrieves the microseconds portion of the time span.
         *
         * Provides access to the microsecond component of the current TimeSpan instance.
         *
         * @return The number of whole microseconds in the time span.
         *
         * @details The `Microseconds` property returns only complete microseconds, while
         * `TotalMicroseconds` provides both whole and fractional values.
         */

    public:
        [[nodiscard]] int32 getMicroseconds() const;

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

    public:
        [[nodiscard]] int32 getNanoseconds() const;

    public:
        [[nodiscard]] int32 getMinutes() const;

    public:
        [[nodiscard]] int32 getSeconds() const;

    public:
        [[nodiscard]] double getTotalDays() const;

    public:
        [[nodiscard]] double getTotalHours() const;

    public:
        [[nodiscard]] double getTotalMilliseconds() const;

        /**
         * @brief Returns the time span value in whole and fractional microseconds.
         *
         * Converts the internal tick-based representation into microseconds,
         * including both complete and partial microseconds.
         *
         * @return The total number of microseconds, including fractional values.
         *
         * @details The `TotalMicroseconds` property provides both whole and fractional microseconds,
         * while `Microseconds` only returns complete microseconds.
         */

    public:
        [[nodiscard]] double getTotalMicroseconds() const;

        /**
         * @brief Returns the time span value in whole and fractional nanoseconds.
         *
         * Converts the internal tick-based representation into nanoseconds,
         * including both complete and fractional values.
         *
         * @return The total number of nanoseconds, including fractional values.
         *
         * @details The `TotalNanoseconds` property provides both whole and fractional nanoseconds,
         * while `Nanoseconds` only returns complete nanoseconds.
         */
    public:
        [[nodiscard]] double getTotalNanoseconds() const;

    public:
        [[nodiscard]] double getTotalMinutes() const;

    public:
        [[nodiscard]] double getTotalSeconds() const;

        ////

    private:
        static long TimeToTicks(int hour, int minute, int second);

        static TimeSpan FromTicks(long i);

        static TimeSpan FromSeconds(double x);

        public: static TimeSpan FromMilliseconds(double value);
    };
} // System

#endif //TIMESPAN_H
