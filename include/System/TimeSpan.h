//
// Created by robertvokac on 5/26/25.
//

#ifndef TIMESPAN_H
#define TIMESPAN_H
#include <limits>

#include "System/ArgumentOutOfRangeException.h"
#include "CNA/CnaHelper.h"

namespace System {
    using CNA::csint;
    using CNA::cslong;
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
        static constexpr cslong NanosecondsPerTick = 100;

        /**
         * @brief Defines the number of ticks in 1 microsecond.
         */
        static constexpr cslong TicksPerMicrosecond = 10;

        /**
         * @brief Defines the number of ticks in 1 millisecond.
         */
        static constexpr cslong TicksPerMillisecond = TicksPerMicrosecond * 1000; // 10,000
        /**
         * @brief Defines the number of ticks in 1 second.
         */
        static constexpr cslong TicksPerSecond = TicksPerMillisecond * 1000; // 10,000,000
        /**
         * @brief Defines the number of ticks in 1 minute.
         */
        static constexpr cslong TicksPerMinute = TicksPerSecond * 60; // 600,000,000
        /**
         * @brief Defines the number of ticks in 1 hour.
         */
        static constexpr cslong TicksPerHour = TicksPerMinute * 60; // 36,000,000,000
        /**
         * @brief Defines the number of ticks in 1 day.
         */
        static constexpr cslong TicksPerDay = TicksPerHour * 24; // 864,000,000,000

    private:
        static constexpr cslong MaxSeconds = CSLONG_MAX / TicksPerSecond;
        static constexpr cslong MinSeconds = CSLONG_MIN / TicksPerSecond;

        static constexpr cslong MaxMilliSeconds = CSLONG_MAX / TicksPerMillisecond;
        static constexpr cslong MinMilliSeconds = CSLONG_MIN / TicksPerMillisecond;

        static constexpr cslong MaxMicroSeconds = CSLONG_MAX / TicksPerMicrosecond;
        static constexpr cslong MinMicroSeconds = CSLONG_MIN / TicksPerMicrosecond;

        static constexpr cslong TicksPerTenthSecond = TicksPerMillisecond * 100;

    public:
        static const TimeSpan Zero;

    public:
        static const TimeSpan MaxValue;

    public:
        static const TimeSpan MinValue;

    private:
        const cslong ticks_internal;

    public:
        TimeSpan(cslong ticks);

    public:
        TimeSpan(csint hours, csint minutes, csint seconds);

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
        TimeSpan(csint days, csint hours, csint minutes, csint seconds, csint milliseconds = 0,
                 csint microseconds = 0);

    public: TimeSpan &operator=(const TimeSpan &);

    private:
        static cslong TimeToTicks(csint days, csint hours, csint minutes, csint seconds, csint milliseconds,
                                 csint microseconds);

    public:
        [[nodiscard]] cslong getTicks() const;

    public:
        [[nodiscard]] int getDays() const;

    public:
        [[nodiscard]] int getHours() const;

    public:
        [[nodiscard]] csint getMilliseconds() const;

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
        [[nodiscard]] csint getMicroseconds() const;

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
        [[nodiscard]] csint getNanoseconds() const;

    public:
        [[nodiscard]] csint getMinutes() const;

    public:
        [[nodiscard]] csint getSeconds() const;

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
    public:
        static TimeSpan FromTicks(long i);

        static TimeSpan FromSeconds(double x);

        public: static TimeSpan FromMilliseconds(double value);
    };
} // System

#endif //TIMESPAN_H
