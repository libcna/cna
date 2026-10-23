// SPDX-License-Identifier: MS-PL
//
// CNA-DEMOS-ISSUE-008: the sensor reading headers used SharpRuntime/Prop.hpp's DEF_MEMBER and with
// it handed every includer YES, NO, nothing, getter1, ... as macros.

#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"

#include <gtest/gtest.h>

#if defined(YES) || defined(NO) || defined(nothing) || defined(getter1) || defined(member0) || \
    defined(DEF_MEMBER) || defined(DEF_PROP)
#error "a sensor header defines a common word as a macro (CNA-DEMOS-ISSUE-008)"
#endif

namespace
{
    enum class Answer { YES, NO };
}

TEST(SensorHeaderMacroHygiene, CommonWordsRemainOrdinaryIdentifiers)
{
    const int nothing = 4;
    EXPECT_EQ(nothing, 4);
    EXPECT_NE(Answer::YES, Answer::NO);
}
