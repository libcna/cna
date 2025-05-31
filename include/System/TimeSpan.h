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

    public: static constexpr TimeSpan Zero = TimeSpan(0);

    public: static constexpr TimeSpan MaxValue = TimeSpan(int64_max);
    public: static constexpr TimeSpan MinValue = TimeSpan(int64_min);

    private: const int64 ticks_internal;

    public: TimeSpan(int64 ticks):
        ticks_internal(ticks)
        {
        }

        public: TimeSpan(int32 hours, int32 minutes, int32 seconds):
        ticks_internal(TimeToTicks(hours, minutes, seconds))
        {
        }

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
                 int32 microseconds = 0): ticks_internal(
            TimeToTicks(days, hours, minutes, seconds, milliseconds, microseconds) * TicksPerMicrosecond) {
        }
    private: static int64 TimeToTicks(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds, int32 microseconds) {
        long totalMicroseconds = (((int64)days * 3600 * 24 + (int64)hours * 3600 + (int64)minutes * 60 + seconds) * 1000 + milliseconds) * 1000 + microseconds;
        if (totalMicroseconds > MaxMicroSeconds || totalMicroseconds < MinMicroSeconds)
            throw ArgumentOutOfRangeException("Time span is too long.");
        return totalMicroseconds;
    }

    public: int64 TicksProperty() {
        return ticks_internal;
    }


    public: int DaysProperty () const { return (int32)(ticks_internal / TicksPerDay);}

    public: int HoursProperty () const {return (int32)((ticks_internal / TicksPerHour) % 24);}

    public: int32 MillisecondsProperty () const {return  (int32)((ticks_internal / TicksPerMillisecond) % 1000);}

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

    public: int32 MicrosecondsProperty() const { return (int32)((ticks_internal / TicksPerMicrosecond) % 1000);}

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

    public: int32 NanosecondsProperty () const { return (int32)((ticks_internal % TicksPerMicrosecond) * 100);}

    public: int32 MinutesProperty () const { return (int32)((ticks_internal / TicksPerMinute) % 60);}

    public: int32 SecondsProperty () const { return (int32)((ticks_internal / TicksPerSecond) % 60);}


    public: double TotalDaysProperty () const {return  ((double)ticks_internal) / TicksPerDay;}

    public: double TotalHoursProperty() const {return  (double)ticks_internal / TicksPerHour;}

    public: double TotalMillisecondsProperty () const {

            double temp = (double)ticks_internal / TicksPerMillisecond;
            if (temp > MaxMilliSeconds)
                return (double)MaxMilliSeconds;

            if (temp < MinMilliSeconds)
                return (double)MinMilliSeconds;

            return temp;
    }

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

    public: double TotalMicrosecondsProperty () const {return  (double)ticks_internal / TicksPerMicrosecond;}

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
    public: double TotalNanosecondsProperty () const {return  (double)ticks_internal * NanosecondsPerTick;}

    public: double TotalMinutesProperty () const {return  (double)ticks_internal / TicksPerMinute;}
    
    public: double TotalSecondsProperty() const {return (double)ticks_internal / TicksPerSecond;}

        ////

        static TimeSpan FromTicks(long i);

        static TimeSpan FromSeconds(double x);

        static TimeSpan FromMilliseconds(double value);
    };
} // System

#endif //TIMESPAN_H
