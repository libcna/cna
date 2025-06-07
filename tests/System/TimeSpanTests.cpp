#include <gtest/gtest.h>

#include "System/OverflowException.h"
#include "System/TimeSpan.h"

using System::TimeSpan;
using CNA::longcs;

TEST(TimeSpanTests, ConstructorWithTicks) {
    TimeSpan ts(1000000); // 100,000,000 ns = 0.1 s
    EXPECT_EQ(ts.getTicksProperty(), 1000000);
}

TEST(TimeSpanTests, ConstructorWithHMS) {
    TimeSpan ts(1, 2, 3); // 1h 2m 3s
    EXPECT_EQ(ts.getHoursProperty(), 1);
    EXPECT_EQ(ts.getMinutesProperty(), 2);
    EXPECT_EQ(ts.getSecondsProperty(), 3);
}

TEST(TimeSpanTests, ConstructorFull) {
    TimeSpan ts(1, 2, 3, 4, 5, 6); // 1d 2h 3m 4s 5ms 6us
    EXPECT_EQ(ts.getDaysProperty(), 1);
    EXPECT_EQ(ts.getHoursProperty(), 2);
    EXPECT_EQ(ts.getMinutesProperty(), 3);
    EXPECT_EQ(ts.getSecondsProperty(), 4);
    EXPECT_EQ(ts.getMillisecondsProperty(), 5);
    EXPECT_EQ(ts.getMicrosecondsProperty(), 6);
}

TEST(TimeSpanPropertyTest, GettersFromConstructedTimeSpan) {
    // TimeSpan(d, h, m, s, ms, us)
    TimeSpan ts(1, 2, 3, 4, 5, 6); // 1d 2h 3m 4s 5ms 6us

    EXPECT_EQ(ts.getDaysProperty(), 1);
    EXPECT_EQ(ts.getHoursProperty(), 2);
    EXPECT_EQ(ts.getMinutesProperty(), 3);
    EXPECT_EQ(ts.getSecondsProperty(), 4);
    EXPECT_EQ(ts.getMillisecondsProperty(), 5);
    EXPECT_EQ(ts.getMicrosecondsProperty(), 6);

    // Ticks = total nanoseconds
    CNA::longcs expectedTicks =
            1LL * 24 * 60 * 60 * 10 * 1000 * 1000 + // days
            2LL * 60 * 60 * 10 * 1000 * 1000 + // hours
            3LL * 60 * 10 * 1000 * 1000 + // minutes
            4LL * 10 * 1000 * 1000 + // seconds
            5LL * 10 * 1000 + // milliseconds
            6LL * 10; // microseconds

    EXPECT_EQ(ts.getTicksProperty(), expectedTicks);
}

TEST(TimeSpanPropertyTest, SubSecondProperties) {
    TimeSpan ts(0, 0, 0, 0, 1, 234); // 1ms + 234us = 1234us = 1_234_000ns

    EXPECT_EQ(ts.getMillisecondsProperty(), 1);
    EXPECT_EQ(ts.getMicrosecondsProperty(), 234);
    EXPECT_EQ(ts.getNanosecondsProperty(), 0); // No leftover nanos beyond microseconds

    EXPECT_DOUBLE_EQ(ts.getTotalMillisecondsProperty(), 1.234);
    EXPECT_DOUBLE_EQ(ts.getTotalMicrosecondsProperty(), 1234.0);
    EXPECT_DOUBLE_EQ(ts.getTotalNanosecondsProperty(), 1234000.0);
}

TEST(TimeSpanPropertyTest, TotalProperties) {
    TimeSpan ts(1, 12, 30, 15, 500, 0); // 1d 12h 30m 15s 500ms

#define tolerance 0.000005
    EXPECT_NEAR(ts.getTotalDaysProperty(), 1.52101273148148, tolerance);
    EXPECT_NEAR(ts.getTotalHoursProperty(), 36.5043055555556, tolerance);
    EXPECT_NEAR(ts.getTotalMinutesProperty(), 2190.2583333333332, tolerance);
    EXPECT_NEAR(ts.getTotalSecondsProperty(), 131415.5, tolerance);
    EXPECT_NEAR(ts.getTotalMillisecondsProperty(), 131415500.0, tolerance);
    EXPECT_NEAR(ts.getTotalMicrosecondsProperty(), 131415500000.0, tolerance);
    EXPECT_NEAR(ts.getTotalNanosecondsProperty(), 131415500000000.0, tolerance);
}

TEST(TimeSpanTests, AddTimeSpans) {
    TimeSpan a = TimeSpan::FromSeconds(30);
    TimeSpan b = TimeSpan::FromSeconds(45);
    TimeSpan c = a.Add(b);
    EXPECT_EQ(c.getTotalSecondsProperty(), 75.0);
}

TEST(TimeSpanTests, CompareTo) {
    TimeSpan a = TimeSpan::FromSeconds(30);
    TimeSpan b = TimeSpan::FromSeconds(45);
    TimeSpan c = TimeSpan::FromSeconds(45);
    EXPECT_EQ(a.CompareTo(b), -1);
    EXPECT_EQ(b.CompareTo(a), 1);
    EXPECT_EQ(b.CompareTo(c), 0);
}

TEST(TimeSpanTests, CompareToStatic) {
    TimeSpan a = TimeSpan::FromSeconds(30);
    TimeSpan b = TimeSpan::FromSeconds(45);
    TimeSpan c = TimeSpan::FromSeconds(45);
    EXPECT_EQ(TimeSpan::Compare(a, b), -1);
    EXPECT_EQ(TimeSpan::Compare(b, a), 1);
    EXPECT_EQ(TimeSpan::Compare(b, c), 0);
}

TEST(TimeSpanTest, FromDaysBasic) {
    // Create a TimeSpan for exactly 1 day
    TimeSpan ts1 = TimeSpan::FromDays(1.0);
    EXPECT_EQ(ts1.getDaysProperty(), 1);
    EXPECT_NEAR(ts1.getTotalDaysProperty(), 1.0, 1e-9);

    // Create a TimeSpan for 1.5 days (1 day and 12 hours)
    TimeSpan ts2 = TimeSpan::FromDays(1.5);
    EXPECT_EQ(ts2.getDaysProperty(), 1);
    EXPECT_NEAR(ts2.getTotalDaysProperty(), 1.5, 1e-9);
    EXPECT_NEAR(ts2.getTotalHoursProperty(), 36.0, 1e-9); // 24 + 12 hours

    // Create a TimeSpan for 0.25 days (6 hours)
    TimeSpan ts3 = TimeSpan::FromDays(0.25);
    EXPECT_EQ(ts3.getDaysProperty(), 0);
    EXPECT_NEAR(ts3.getTotalHoursProperty(), 6.0, 1e-9);

    // Create a TimeSpan for a negative value (-2.75 days)
    TimeSpan ts4 = TimeSpan::FromDays(-2.75);
    EXPECT_EQ(ts4.getDaysProperty(), -2);
    EXPECT_NEAR(ts4.getTotalDaysProperty(), -2.75, 1e-9);
    EXPECT_NEAR(ts4.getTotalHoursProperty(), -66.0, 1e-9); // -2.75 * 24
}

TEST(TimeSpanTest, DurationPositive) {
    // Duration of a positive TimeSpan should be the same as original
    TimeSpan ts(1234567890); // positive ticks
    TimeSpan dur = ts.Duration();
    EXPECT_EQ(dur.getTicksProperty(), ts.getTicksProperty());
}

TEST(TimeSpanTest, DurationNegative) {
    // Duration of a negative TimeSpan should be the absolute value (positive)
    TimeSpan ts(-1234567890); // negative ticks
    TimeSpan dur = ts.Duration();
    EXPECT_EQ(dur.getTicksProperty(), -ts.getTicksProperty());
}

TEST(TimeSpanTest, DurationZero) {
    // Duration of zero TimeSpan is zero
    TimeSpan ts(0);
    TimeSpan dur = ts.Duration();
    EXPECT_EQ(dur.getTicksProperty(), 0);
}

// Optional: test that Duration throws on MinValue (if implemented)
TEST(TimeSpanTest, DurationThrowsOnMinValue) {
    // Depending on implementation, Duration() may throw if ticks == MinValue
    try {
        TimeSpan dur = TimeSpan::MinValue.Duration();
        FAIL() << "Expected overflow exception";
    } catch (const System::OverflowException &e) {
        SUCCEED();
    } catch (...) {
        FAIL() << "Expected OverflowException";
    }
}

TEST(TimeSpanTest, EqualsInstanceMethod) {
    TimeSpan ts1(10000);
    TimeSpan ts2(10000);
    TimeSpan ts3(20000);

    // ts1 equals ts2 (same ticks)
    EXPECT_TRUE(ts1.Equals(ts2));

    // ts1 does not equal ts3 (different ticks)
    EXPECT_FALSE(ts1.Equals(ts3));

    // ts1 does not equal an unrelated object (nullptr or different type - if supported)
    // Here, as parameter is TimeSpan&, this case might not be applicable directly.
}

TEST(TimeSpanTest, EqualsStaticMethod) {
    TimeSpan ts1(5000);
    TimeSpan ts2(5000);
    TimeSpan ts3(10000);

    // static Equals returns true for equal ticks
    EXPECT_TRUE(TimeSpan::Equals(ts1, ts2));

    // static Equals returns false for different ticks
    EXPECT_FALSE(TimeSpan::Equals(ts1, ts3));
}

TEST(TimeSpanTest, FromHours_CreatesCorrectTimeSpan) {
    // Create a TimeSpan from 1.5 hours (1 hour 30 minutes)
    double hours = 1.5;
    TimeSpan ts = TimeSpan::FromHours(hours);

    // Expected ticks = hours * ticks per hour
    longcs expectedTicks = static_cast<longcs>(hours * TimeSpan::TicksPerHour);

    EXPECT_EQ(ts.getTicksProperty(), expectedTicks);

    // Also check total hours property matches (within some tolerance)
    EXPECT_NEAR(ts.getTotalHoursProperty(), hours, 1e-9);
}

TEST(TimeSpanTest, FromHours_Zero) {
    TimeSpan ts = TimeSpan::FromHours(0.0);
    EXPECT_EQ(ts.getTicksProperty(), 0);
}

TEST(TimeSpanTest, FromHours_Negative) {
    double negHours = -2.25; // -2 hours 15 minutes
    TimeSpan ts = TimeSpan::FromHours(negHours);

    longcs expectedTicks = static_cast<longcs>(negHours * TimeSpan::TicksPerHour);

    EXPECT_EQ(ts.getTicksProperty(), expectedTicks);
    EXPECT_NEAR(ts.getTotalHoursProperty(), negHours, 1e-9);
}


TEST(TimeSpanStaticFactoryTests, FromMillisecondsBasic) {
    double ms = 1500.0;\
    TimeSpan ts = TimeSpan::FromMilliseconds(ms);
\
    EXPECT_NEAR(ts.getTotalMillisecondsProperty(), ms, 1e-6);
}

TEST(TimeSpanStaticFactoryTests, FromMicrosecondsBasic) {
    double us = 1234567.8;
    TimeSpan ts = TimeSpan::FromMicroseconds(us);

    EXPECT_NEAR(ts.getTotalMicrosecondsProperty(), us, 1e-6);
}

TEST(TimeSpanStaticFactoryTests, FromMinutesBasic) {
    double minutes = 3.25;
    TimeSpan ts = TimeSpan::FromMinutes(minutes);

    EXPECT_NEAR(ts.getTotalMinutesProperty(), minutes, 1e-9);
    EXPECT_NEAR(ts.getTotalSecondsProperty(), minutes * 60, 1e-6);
}

TEST(TimeSpanTests, NegateAndDuration) {
    TimeSpan a = TimeSpan::FromSeconds(-10);
    EXPECT_EQ(a.Negate().getTotalSecondsProperty(), 10.0);
    EXPECT_EQ(a.Duration().getTotalSecondsProperty(), 10.0);
}

TEST(TimeSpanTests, SubtractTimeSpans) {
    TimeSpan a = TimeSpan::FromSeconds(60);
    TimeSpan b = TimeSpan::FromSeconds(20);
    TimeSpan c = a.Subtract(b);
    EXPECT_EQ(c.getTotalSecondsProperty(), 40.0);
}

TEST(TimeSpanTests, MultiplyAndDivide) {
    TimeSpan base = TimeSpan::FromSeconds(10);
    EXPECT_EQ((base * 2.5).getTotalSecondsProperty(), 25.0);
    EXPECT_EQ((base / 2.0).getTotalSecondsProperty(), 5.0);
}

TEST(TimeSpanTests, DivideByTimeSpan) {
    TimeSpan a = TimeSpan::FromSeconds(10);
    TimeSpan b = TimeSpan::FromSeconds(2);
    EXPECT_DOUBLE_EQ(a / b, 5.0);
}

TEST(TimeSpanTests, DivideByDouble) {
    TimeSpan a = TimeSpan::FromSeconds(10);
    TimeSpan b = TimeSpan::FromSeconds(2);
    EXPECT_DOUBLE_EQ((a / 5).getSecondsProperty(), b.getSecondsProperty());
}

TEST(TimeSpanStaticFactoryTests, FromTicksBasic) {
    long ticks = 50000000;
    TimeSpan ts = TimeSpan::FromTicks(ticks);

    EXPECT_EQ(ts.getTicksProperty(), ticks);
    EXPECT_NEAR(ts.getTotalSecondsProperty(), ticks / 10000000.0, 1e-6);
}

TEST(TimeSpanToStringTests, BasicFormatting) {
    TimeSpan ts(1, 2, 3, 4, 5, 6);
    std::string expected = "26:03:04.0050060";
    EXPECT_EQ(ts.ToString(), expected);
}

TEST(TimeSpanToStringTests, BasicFormatting2) {
    // 1 hour, 2 minutes, 3 seconds, 4567890 ticks
    longcs ticks = 1l * 60l * 60l * 10'000'000l + // 1 hour
                   2l * 60l * 10'000'000l + // 2 minutes
                   3l * 10'000'000l + // 3 seconds
                   4567890l; // fractional seconds
    //37234567890

    TimeSpan ts(ticks);
    std::string expected = "1:02:03.4567890";
    EXPECT_EQ(ts.ToString(), expected);
}


TEST(TimeSpanToStringTests, ZeroTime) {
    TimeSpan ts(0);
    std::string expected = "0:00:00.0000000";
    EXPECT_EQ(ts.ToString(), expected);
}

TEST(TimeSpanToStringTests, PaddingAndZeros) {
    // Exactly 12 hours
    TimeSpan ts(12LL * 60 * 60 * 10'000'000);
    std::string expected = "12:00:00.0000000";
    EXPECT_EQ(ts.ToString(), expected);
}

TEST(TimeSpanToStringTests, NegativeTime) {
    // -5 hours, 6 minutes, 7 seconds, 8900000 ticks
    longcs ticks = -(5 * 60 * 60 * 10'000'000l +
                     6 * 60 * 10'000'000l +
                     7 * 10'000'000l +
                     8900000l);

    TimeSpan ts(ticks);
    std::string expected = "-5:06:07.8900000";
    EXPECT_EQ(ts.ToString(), expected);
}

TEST(TimeSpanToStringTests, OnlyTicks) {
    // Only 1234567 ticks (less than a second)
    TimeSpan ts(1234567);
    std::string expected = "0:00:00.1234567";
    EXPECT_EQ(ts.ToString(), expected);
}


TEST(TimeSpanTests, Comparisons) {
    TimeSpan a = TimeSpan::FromSeconds(5);
    TimeSpan b = TimeSpan::FromSeconds(10);
    TimeSpan c = TimeSpan::FromSeconds(10);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(b >= a);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);

    EXPECT_TRUE(b<= c);
    EXPECT_TRUE(b >= c);
    EXPECT_TRUE(b == c);
    EXPECT_TRUE(a != c);
}


TEST(TimeSpanTests, ToStringIsNonEmpty) {
    TimeSpan ts = TimeSpan::FromMinutes(90);
    EXPECT_FALSE(ts.ToString().empty());
}
