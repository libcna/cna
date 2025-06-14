//
// Created by robertvokac on 5/30/25.
//

#include "System/TimeSpan.h"

#include <iomanip>

#include "Microsoft/Xna/Framework/GameTime.h"
#include "System/Int64.h"
#include "System/OverflowException.h"

namespace System {

    int TimeSpan::copy_count = 0;
    int TimeSpan::move_count = 0;

    int TimeSpan::getCopyCount() { return copy_count; }
    int TimeSpan::getMoveCount() { return move_count; }

    void TimeSpan::resetCopyCount() {
        copy_count = 0;
        move_count = 0;
    }

    void TimeSpan::resetMoveCount() {
    }

    const TimeSpan TimeSpan::Zero = TimeSpan(0);
    const TimeSpan TimeSpan::MaxValue = TimeSpan(LONGCS_MAX);

    const TimeSpan TimeSpan::MinValue = TimeSpan(LONGCS_MIN);


    TimeSpan::TimeSpan(longcs ticks): ticks_internal(ticks) {
    }

    TimeSpan::TimeSpan(intcs hours, intcs minutes, intcs seconds): ticks_internal(
        TimeToTicks(hours, minutes, seconds)) {
    }

    TimeSpan::TimeSpan(intcs days, intcs hours, intcs minutes, intcs seconds, intcs milliseconds,
                       intcs microseconds): ticks_internal(
        TimeToTicks(days, hours, minutes, seconds, milliseconds, microseconds) * TicksPerMicrosecond) {
    }

    TimeSpan::TimeSpan(const TimeSpan& other) : ticks_internal(other.ticks_internal) {
        copy_count++;
    }

    TimeSpan::TimeSpan(TimeSpan&& other) noexcept : ticks_internal(other.ticks_internal) {
        move_count++;
    }

    longcs TimeSpan::TimeToTicks(intcs days, intcs hours, intcs minutes, intcs seconds, intcs milliseconds,
                                 intcs microseconds) {
        long totalMicroseconds = (((longcs) days * 3600 * 24 + (longcs) hours * 3600 + (longcs) minutes * 60 + seconds)
                                  *
                                  1000 + milliseconds) * 1000 + microseconds;
        if (totalMicroseconds > MaxMicroSeconds || totalMicroseconds < MinMicroSeconds)
            throw ArgumentOutOfRangeException("Time span is too long.");
        return totalMicroseconds;
    }

    TimeSpan &TimeSpan::operator=(const TimeSpan &other) {
        if (this != &other) {  // Prevent self-assignment
            ticks_internal = other.ticks_internal;  // Copy internal data
        }
        return *this;
    }

    TimeSpan& TimeSpan::operator=(TimeSpan&& other) noexcept {
        if (this != &other) {  // Prevent self-assignment
            ticks_internal = other.ticks_internal;  // Transfer ownership
            move_count++;
        }
        return *this;
    }

    longcs TimeSpan::getTicksProperty() const {
        return ticks_internal;
    }


    [[nodiscard]] int TimeSpan::getDaysProperty() const { return (intcs) (ticks_internal / TicksPerDay); }


    [[nodiscard]] int TimeSpan::getHoursProperty() const { return (intcs) ((ticks_internal / TicksPerHour) % 24); }


    [[nodiscard]] intcs TimeSpan::getMillisecondsProperty() const {
        return (intcs) ((ticks_internal / TicksPerMillisecond) % 1000);
    }


    [[nodiscard]] intcs TimeSpan::getMicrosecondsProperty() const {
        return (intcs) ((ticks_internal / TicksPerMicrosecond) % 1000);
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


    [[nodiscard]] intcs TimeSpan::getNanosecondsProperty() const {
        return (intcs) ((ticks_internal % TicksPerMicrosecond) * 100);
    }


    [[nodiscard]] intcs TimeSpan::getMinutesProperty() const {
        return (intcs) ((ticks_internal / TicksPerMinute) % 60);
    }


    [[nodiscard]] intcs TimeSpan::getSecondsProperty() const {
        return (intcs) ((ticks_internal / TicksPerSecond) % 60);
    }


    [[nodiscard]] double TimeSpan::getTotalDaysProperty() const { return ((double) ticks_internal) / TicksPerDay; }


    [[nodiscard]] double TimeSpan::getTotalHoursProperty() const { return (double) ticks_internal / TicksPerHour; }


    [[nodiscard]] double TimeSpan::getTotalMillisecondsProperty() const {
        double temp = (double) ticks_internal / TicksPerMillisecond;
        if (temp > MaxMilliSeconds)
            return (double) MaxMilliSeconds;

        if (temp < MinMilliSeconds)
            return (double) MinMilliSeconds;

        return temp;
    }

    [[nodiscard]] double TimeSpan::getTotalMicrosecondsProperty() const {
        return (double) ticks_internal / TicksPerMicrosecond;
    }


    [[nodiscard]] double TimeSpan::getTotalNanosecondsProperty() const {
        return (double) ticks_internal * NanosecondsPerTick;
    }

    [[nodiscard]] double TimeSpan::getTotalMinutesProperty() const { return (double) ticks_internal / TicksPerMinute; }

    [[nodiscard]] double TimeSpan::getTotalSecondsProperty() const { return (double) ticks_internal / TicksPerSecond; }

    TimeSpan TimeSpan::Add(const TimeSpan &ts) const {
        if ((ts.ticks_internal > 0 && ticks_internal > std::numeric_limits<int64_t>::max() - ts.ticks_internal) ||
            (ts.ticks_internal < 0 && ticks_internal < std::numeric_limits<int64_t>::min() - ts.ticks_internal)) {
            throw OverflowException("TimeSpanTooLong");
        }

        return {ticks_internal + ts.ticks_internal};
    }

    intcs TimeSpan::Compare(const TimeSpan &t1, const TimeSpan &t2) {
        if (t1.ticks_internal > t2.ticks_internal) return 1;
        if (t1.ticks_internal < t2.ticks_internal) return -1;
        return 0;
    }

    intcs TimeSpan::CompareTo(const TimeSpan &value) const {
        return Compare(*this, value);
    }

    TimeSpan TimeSpan::FromDays(double value) {
        return Interval(value, TicksPerDay);
    }

    TimeSpan TimeSpan::Duration() const {
        if (getTicksProperty() == MinValue.getTicksProperty())
            throw OverflowException("Overflow_Duration");
        return {ticks_internal >= 0 ? ticks_internal : -ticks_internal};
    }

    bool TimeSpan::Equals(const TimeSpan &obj) const {
        return Equals(*this, obj);
    }

    bool TimeSpan::Equals(const TimeSpan &t1, const TimeSpan &t2) {
        return t1.ticks_internal == t2.ticks_internal;
    }

    TimeSpan TimeSpan::FromHours(double value) {
        return Interval(value, TicksPerHour);
    }

    TimeSpan TimeSpan::Interval(double value, double scale) {
        return IntervalFromDoubleTicks(value * scale);
    }

    TimeSpan TimeSpan::IntervalFromDoubleTicks(double ticks) {
        if ((ticks > LONGCS_MAX) || (ticks < LONGCS_MIN))
            throw OverflowException("TimeSpanTooLong");
        if (ticks == Int64::MaxValue)
            return MaxValue;
        return {(longcs) ticks};
    }

    TimeSpan TimeSpan::FromMilliseconds(double value) {
        return Interval(value, TicksPerMillisecond);
    }

    TimeSpan TimeSpan::FromMicroseconds(double value) {
        return Interval(value, TicksPerMicrosecond);
    }

    TimeSpan TimeSpan::FromMinutes(double value) {
        return Interval(value, TicksPerMinute);
    }

    TimeSpan TimeSpan::Negate() const {
        if (getTicksProperty() == MinValue.getTicksProperty())
            throw OverflowException("Overflow_NegateTwosCompNum");
        return TimeSpan(-ticks_internal);
    }

    TimeSpan TimeSpan::FromSeconds(double value) {
        return Interval(value, TicksPerSecond);
    }

    TimeSpan TimeSpan::Subtract(const TimeSpan &ts) const {
        const longcs result = ticks_internal - ts.ticks_internal;

        constexpr int signShift = sizeof(longcs) * 8 - 1;

        const bool overflow = ((ticks_internal >> signShift) != (ts.ticks_internal >> signShift)) &&
                              ((ticks_internal >> signShift) != (result >> signShift));

        if (overflow) {
            throw OverflowException("Overflow_TimeSpanTooLong");
        }

        return TimeSpan(result);
    }

    TimeSpan TimeSpan::Multiply(double factor) const { return (*this) * factor; }

    TimeSpan TimeSpan::Divide(double divisor) { return (*this) / divisor; }

    double TimeSpan::Divide(const TimeSpan &ts) const { return (*this) / ts; }

    TimeSpan TimeSpan::FromTicks(long value) {
        return TimeSpan(value);
    }

    long TimeSpan::TimeToTicks(int hour, int minute, int second) {
        long totalSeconds = (long) hour * 3600 + (long) minute * 60 + (long) second;
        if (totalSeconds > MaxSeconds || totalSeconds < MinSeconds)
            throw ArgumentOutOfRangeException("TimeSpanTooLong");
        return totalSeconds * TicksPerSecond;
    }

    std::string TimeSpan::ToString() const {
        constexpr longcs TicksPerMillisecond = 10000;
        constexpr longcs TicksPerSecond = TicksPerMillisecond * 1000;
        constexpr longcs TicksPerMinute = TicksPerSecond * 60;
        constexpr longcs TicksPerHour = TicksPerMinute * 60;

        longcs ticks = ticks_internal;
        bool negative = ticks < 0;
        if (negative) ticks = -ticks;

        longcs hours = ticks / TicksPerHour;
        ticks -= hours * TicksPerHour;

        longcs minutes = ticks / TicksPerMinute;
        ticks -= minutes * TicksPerMinute;

        longcs seconds = ticks / TicksPerSecond;
        ticks -= seconds * TicksPerSecond;

        std::ostringstream oss;
        if (negative) oss << '-';

        oss << hours << ":"
                << std::setw(2) << std::setfill('0') << minutes << ":"
                << std::setw(2) << std::setfill('0') << seconds << "."
                << std::setw(7) << std::setfill('0') << ticks;

        return oss.str();
    }


    TimeSpan TimeSpan::operator-() const {
        if (ticks_internal == MinValue.ticks_internal)
            throw OverflowException("Overflow_NegateTwosCompNum");
        return TimeSpan(-ticks_internal);
    }

    TimeSpan TimeSpan::operator-(const TimeSpan &t2) { return Subtract(t2); }

    TimeSpan TimeSpan::operator+() { return *this; };

    TimeSpan TimeSpan::operator+(const TimeSpan &t2) { return Add(t2); }


    TimeSpan TimeSpan::operator*(double factor) const {
        double ticks;
        ticks = std::round(getTicksProperty() * factor);
        return IntervalFromDoubleTicks(ticks);
    }

    TimeSpan TimeSpan::operator/(double divisor) {
        double ticks = std::round(getTicksProperty() / divisor);
        return IntervalFromDoubleTicks(ticks);
    }

    // Performing division using floating-point arithmetic allows the result to be infinity,
    // which makes sense when dividing a non-zero TimeSpan by TimeSpan.Zero.
    // For example, TimeSpan.FromHours(1) / TimeSpan.Zero asks how many zero-length intervals fit into one hour,
    // and mathematically, the answer is infinity.
    // Dividing TimeSpan.Zero by TimeSpan.Zero returns NaN, which may be less practical,
    // but it is no less valid than throwing an exception.


    double TimeSpan::operator/(const TimeSpan &t2) const { return getTicksProperty() / (double) t2.getTicksProperty(); }


    bool TimeSpan::operator==(const TimeSpan &t2) const { return ticks_internal == t2.ticks_internal; }


    bool TimeSpan::operator!=(const TimeSpan &t2) const { return ticks_internal != t2.ticks_internal; }


    bool TimeSpan::operator<(const TimeSpan &t2) const { return ticks_internal < t2.ticks_internal; }


    bool TimeSpan::operator<=(const TimeSpan &t2) const { return ticks_internal <= t2.ticks_internal; }


    bool TimeSpan::operator>(const TimeSpan &t2) const { return ticks_internal > t2.ticks_internal; }


    bool TimeSpan::operator>=(const TimeSpan &t2) const { return ticks_internal >= t2.ticks_internal; }


    TimeSpan operator*(double factor, const TimeSpan &timeSpan) { return timeSpan * factor; }
}
