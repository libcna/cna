//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include <chrono>
#include <cmath>
#include <limits>

#include "IComparable.hpp"
#include "IEquatable.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "CNA/CnaHelper.hpp"

namespace System {
    using CNA::intcs;
    using CNA::longcs;

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
    struct TimeSpan : IEquatable<TimeSpan>, IComparable<TimeSpan> {
    private:
        static int copy_count;
        static int move_count;

    public:
        static int getCopyCount();
        static int getMoveCount();

        static void resetCopyCount();
        static void resetMoveCount();

    public:
        /**
         * @brief Defines the number of nanoseconds in 1 tick.
         */
        static constexpr longcs NanosecondsPerTick = 100;

#ifdef XNA5
        /**
         * @brief Defines the number of ticks in 1 nanosecond.
         */
        static constexpr double TicksPerNanosecond = 0.01f;
#endif
        /**
         * @brief Defines the number of ticks in 1 microsecond.
         */
        static constexpr longcs TicksPerMicrosecond = 10;

        /**
         * @brief Defines the number of ticks in 1 millisecond.
         */
        static constexpr longcs TicksPerMillisecond = TicksPerMicrosecond * 1000; // 10,000
        /**
         * @brief Defines the number of ticks in 1 second.
         */
        static constexpr longcs TicksPerSecond = TicksPerMillisecond * 1000; // 10,000,000
        /**
         * @brief Defines the number of ticks in 1 minute.
         */
        static constexpr longcs TicksPerMinute = TicksPerSecond * 60; // 600,000,000
        /**
         * @brief Defines the number of ticks in 1 hour.
         */
        static constexpr longcs TicksPerHour = TicksPerMinute * 60; // 36,000,000,000
        /**
         * @brief Defines the number of ticks in 1 day.
         */
        static constexpr longcs TicksPerDay = TicksPerHour * 24; // 864,000,000,000

    private:
        static constexpr longcs MaxSeconds = CNA::LONGCS_MAX / TicksPerSecond;
        static constexpr longcs MinSeconds = CNA::LONGCS_MIN / TicksPerSecond;

        static constexpr longcs MaxMilliSeconds = CNA::LONGCS_MAX / TicksPerMillisecond;
        static constexpr longcs MinMilliSeconds = CNA::LONGCS_MIN / TicksPerMillisecond;

        static constexpr longcs MaxMicroSeconds = CNA::LONGCS_MAX / TicksPerMicrosecond;
        static constexpr longcs MinMicroSeconds = CNA::LONGCS_MIN / TicksPerMicrosecond;

        static constexpr longcs TicksPerTenthSecond = TicksPerMillisecond * 100;

    public:
        static const TimeSpan Zero;

    public:
        static const TimeSpan MaxValue;

    public:
        static const TimeSpan MinValue;

    private:
        longcs ticks_internal;

    public:
        TimeSpan(longcs ticks);

    public:
        TimeSpan(intcs hours, intcs minutes, intcs seconds);

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
        TimeSpan(intcs days, intcs hours, intcs minutes, intcs seconds, intcs milliseconds = 0,
                 intcs microseconds = 0);

    public:
        TimeSpan &operator=(const TimeSpan &);

        TimeSpan(const TimeSpan& other);

        TimeSpan(TimeSpan&& other) noexcept;

        TimeSpan& operator=(TimeSpan&& other) noexcept;

    private:
        static longcs TimeToTicks(intcs days, intcs hours, intcs minutes, intcs seconds, intcs milliseconds,
                                  intcs microseconds);

    public:
        [[nodiscard]] longcs getTicksProperty() const;

    public:
        [[nodiscard]] int getDaysProperty() const;

    public:
        [[nodiscard]] int getHoursProperty() const;

    public:
        [[nodiscard]] intcs getMillisecondsProperty() const;

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
        [[nodiscard]] intcs getMicrosecondsProperty() const;

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
        [[nodiscard]] intcs getNanosecondsProperty() const;

    public:
        [[nodiscard]] intcs getMinutesProperty() const;

    public:
        [[nodiscard]] intcs getSecondsProperty() const;

    public:
        [[nodiscard]] double getTotalDaysProperty() const;

    public:
        [[nodiscard]] double getTotalHoursProperty() const;

    public:
        [[nodiscard]] double getTotalMillisecondsProperty() const;

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
        [[nodiscard]] double getTotalMicrosecondsProperty() const;

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
        [[nodiscard]] double getTotalNanosecondsProperty() const;

    public:
        [[nodiscard]] double getTotalMinutesProperty() const;

    public:
        [[nodiscard]] double getTotalSecondsProperty() const;

        /**
         * @brief Adds the specified TimeSpan to the current TimeSpan instance.
         *
         * Combines the time duration of the current instance with that of the specified
         * `TimeSpan` parameter, producing a new `TimeSpan` that represents their sum.
         *
         * @param ts A reference to the `TimeSpan` object to be added.
         *
         * @return A new `TimeSpan` representing the combined duration of the two instances.
         *
         * @throws OverflowException If the resulting time span exceeds the valid range for a `TimeSpan`.
         *
         * @details This method ensures that the resulting time span does not exceed the
         * limits supported by the `TimeSpan`. If an overflow occurs during the addition,
         * an `OverflowException` is thrown.
         */
    public:
        [[nodiscard]] TimeSpan Add(const TimeSpan &ts) const;

        /**
         * @brief Compares two TimeSpan instances to determine their relative values.
         *
         * Determines whether the first TimeSpan is shorter than, equal to, or longer than
         * the second TimeSpan, based on their internal tick counts.
         *
         * @param t1 The first TimeSpan instance to compare.
         * @param t2 The second TimeSpan instance to compare.
         * @return An integer representing the comparison result:
         *         - Returns 1 if t1 is greater than t2.
         *         - Returns -1 if t1 is less than t2.
         *         - Returns 0 if t1 is equal to t2.
         */
    public:
        static intcs Compare(const TimeSpan &t1, const TimeSpan &t2);

    public:
        [[nodiscard]] intcs CompareTo(const TimeSpan &value) const override;

    public:
        static TimeSpan FromDays(double value);

        /**
         * @brief Retrieves the absolute duration of the current TimeSpan instance.
         *
         * This method calculates and returns a new TimeSpan instance representing
         * the absolute value of the current TimeSpan's duration. If the TimeSpan
         * represents a negative duration, it is converted to its corresponding positive value.
         *
         * @return A TimeSpan instance representing the absolute duration.
         *
         * @throws OverflowException If the TimeSpan is set to its minimum value,
         * which cannot be represented as a positive value due to overflow.
         */
    public:
        [[nodiscard]] TimeSpan Duration() const;

        /**
         * @brief Determines whether the current TimeSpan instance is equal to another specified TimeSpan instance.
         *
         * Compares the internal tick count of two TimeSpan objects to determine equality.
         * Two TimeSpan instances are considered equal if their internal tick values are the same.
         *
         * @param obj The TimeSpan instance to compare with the current instance.
         * @return True if the specified TimeSpan instance has the same tick count as the current instance; otherwise, false.
         */
    public:
        [[nodiscard]] bool Equals(const TimeSpan &obj) const override;

    public:
        static bool Equals(const TimeSpan &t1, const TimeSpan &t2);

    public:
        static TimeSpan FromHours(double value);

    private:
        static TimeSpan Interval(double value, double scale);

    private:
        static TimeSpan IntervalFromDoubleTicks(double ticks);

        /**
         * @brief Creates a TimeSpan object representing a specific number of milliseconds.
         *
         * Converts the given value, representing a time duration in milliseconds, into a TimeSpan instance.
         *
         * @param value The number of milliseconds to be represented as a TimeSpan. This can be positive or negative.
         * @return A TimeSpan object corresponding to the provided number of milliseconds.
         *
         * @details If the `value` is fractional, the fractional part is converted into ticks, as TimeSpan uses ticks
         * for internal representation. A single tick equals 100 nanoseconds, which provides precision in the conversion.
         */
    public:
        static TimeSpan FromMilliseconds(double value);

    public:
        static TimeSpan FromMicroseconds(double value);

    public:
        static TimeSpan FromMinutes(double value);

    public:
        [[nodiscard]] TimeSpan Negate() const;

    public:
        static TimeSpan FromSeconds(double value);

    public:
        [[nodiscard]] TimeSpan Subtract(const TimeSpan &ts) const;

    public:
        TimeSpan Multiply(double factor) const;

    public:
        TimeSpan Divide(double divisor);

    public:
        [[nodiscard]] double Divide(const TimeSpan &ts) const;

    public:
        static TimeSpan FromTicks(long value);

        //[MethodImpl(MethodImplOptions.AggressiveInlining)]
    private:
        static long TimeToTicks(int hour, int minute, int second);

    public:
        [[nodiscard]] std::string ToString() const;

    public:
        TimeSpan operator-() const;\

    public:
        TimeSpan operator-(const TimeSpan &t2);

    public:
        TimeSpan operator+();

    public:
        TimeSpan operator+(const TimeSpan &t2);

    public:
        TimeSpan operator*(double factor) const;

        TimeSpan operator/(double divisor);

        // Performing division using floating-point arithmetic allows the result to be infinity,
        // which makes sense when dividing a non-zero TimeSpan by TimeSpan.Zero.
        // For example, TimeSpan.FromHours(1) / TimeSpan.Zero asks how many zero-length intervals fit into one hour,
        // and mathematically, the answer is infinity.
        // Dividing TimeSpan.Zero by TimeSpan.Zero returns NaN, which may be less practical,
        // but it is no less valid than throwing an exception.

    public:
        double operator/(const TimeSpan &t2) const;

    public:
        bool operator==(const TimeSpan &t2) const;

    public:
        bool operator!=(const TimeSpan &t2) const;

    public:
        bool operator<(const TimeSpan &t2) const;

    public:
        bool operator<=(const TimeSpan &t2) const;

    public:
        bool operator>(const TimeSpan &t2) const;

    public:
        bool operator>=(const TimeSpan &t2) const;
    };


    TimeSpan operator*(double factor, const TimeSpan &timeSpan);
} // System
