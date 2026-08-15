// SPDX-License-Identifier: MS-PL
//
// PLAT-78b: the contract's virtual-key vocabulary.

#include "CNA/Platform/Input/KeyCode.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using namespace CNA::Platform;

TEST(KeyCodeTests, NoneIsZeroAndIsARecognisedAnswer)
{
    // None means "this platform names no virtual key for that press", which a caller has to be
    // able to receive and name -- not a value the contract rejects.
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::None), 0u);
    EXPECT_TRUE(IsKnownKeyCode(0));
    EXPECT_EQ(ToString(KeyCode::None), "None");
}

TEST(KeyCodeTests, TheValuesAreWindowsVirtualKeyCodes)
{
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Back), 0x08);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Enter), 0x0D);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Space), 0x20);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::D0), 0x30);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::A), 0x41);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::F1), 0x70);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::LeftShift), 0xA0);
}

TEST(KeyCodeTests, TheVirtualKeySpaceIsSparseAndTheGapsAreRejected)
{
    // Virtual-Key numbering leaves holes, and casting an implementation's native keycode lands in
    // one sooner or later -- which is why the contract offers a check rather than a bare cast.
    for (const std::uint16_t value : {std::uint16_t{1}, std::uint16_t{58}, std::uint16_t{59},
                                      std::uint16_t{64}, std::uint16_t{252}, std::uint16_t{999}})
    {
        EXPECT_FALSE(IsKnownKeyCode(value)) << "value " << value;
    }
}

TEST(KeyCodeTests, AnUnrecognisedValueIsNamedRatherThanLeftBlank)
{
    EXPECT_EQ(ToString(static_cast<KeyCode>(999)), "None");
}

TEST(KeyCodeTests, NamesAreStableStorage)
{
    const std::string& first = ToString(KeyCode::LeftShift);
    const std::string& second = ToString(KeyCode::LeftShift);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(first, "LeftShift");
}

TEST(KeyCodeTests, TheLetterAndDigitBlocksAreContiguous)
{
    // Both blocks are contiguous in the Virtual-Key numbering. A generator that reordered or
    // renumbered them would show up here rather than as one unbindable key months later.
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Z) - static_cast<std::uint16_t>(KeyCode::A), 25);
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::D9) - static_cast<std::uint16_t>(KeyCode::D0), 9);
    for (int i = 0; i < 26; ++i)
    {
        EXPECT_TRUE(IsKnownKeyCode(static_cast<std::uint16_t>(0x41 + i))) << "letter index " << i;
    }
}

} // namespace
