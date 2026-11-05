// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <limits>
#include "System/Globalization/CultureInfo.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
namespace {
class Tq075CultureGuard {
    System::Globalization::CultureInfo saved = System::Globalization::CultureInfo::getCurrentCultureProperty();
public:
    explicit Tq075CultureGuard(const char* name) {
        System::Globalization::CultureInfo::setCurrentCultureProperty(System::Globalization::CultureInfo(name));
    }
    ~Tq075CultureGuard() { System::Globalization::CultureInfo::setCurrentCultureProperty(saved); }
};
}

TEST(Tq075GamePadTextTest, TriggersUseFrameworkG7AndCanonicalZero)
{
    // Refresh GamePadTriggers.ToString IL_0000..0033: CurrentCulture and boxed Single default G7.
    // 63/512=.123046875 rounds to .1230469 at seven significant digits, not .123047 (G6).
    Tq075CultureGuard culture("");
    EXPECT_EQ(GamePadTriggers(0.123046875f, 0.876953125f).ToString(), "{Left:0.1230469 Right:0.8769531}");
    EXPECT_EQ(GamePadTriggers(-0.0f, 0.0f).ToString(), "{Left:0 Right:0}");
    // 1/131072=.00000762939453125 -> G7 7.629395E-06.
    EXPECT_EQ(GamePadTriggers(0.00000762939453125f, 1).ToString(), "{Left:7.629395E-06 Right:1}");
    EXPECT_EQ(GamePadTriggers(std::numeric_limits<float>::quiet_NaN(), 1).ToString(), "{Left:NaN Right:1}");
}

TEST(Tq075GamePadTextTest, AnalogTextUsesCurrentCultureAndNestedVectorFormat)
{
    // Refresh Triggers/ThumbSticks.ToString IL_0000..0033 both pass CurrentCulture;
    // nested Vector2.ToString IL_0000..0033 formats X/Y with the same provider.
    Tq075CultureGuard culture("de-DE");
    EXPECT_EQ(GamePadTriggers(0.25f, 0.75f).ToString(), "{Left:0,25 Right:0,75}");
    EXPECT_EQ(GamePadThumbSticks(Vector2(0.25f, -0.5f), Vector2(0.75f, 0.125f)).ToString(),
              "{Left:{X:0,25 Y:-0,5} Right:{X:0,75 Y:0,125}}");
}
