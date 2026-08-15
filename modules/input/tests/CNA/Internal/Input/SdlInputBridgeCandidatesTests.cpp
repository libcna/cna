// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <string>
#include <vector>

using CNA::Internal::Input::PlatformInputBridge;
using CNA::Platform::TextEditingCandidatesEvent;
using Microsoft::Xna::Framework::Input::TextInputEXT;

namespace
{
    class SdlInputBridgeCandidatesTest : public ::testing::Test
    {
    protected:
        void SetUp() override { TextInputEXT::ResetForTests(); }
        void TearDown() override { TextInputEXT::ResetForTests(); }
    };
}

// N-014: an SDL IME candidate-list event is decoded into UTF-8 strings and dispatched with the
// selected index and orientation.
TEST_F(SdlInputBridgeCandidatesTest, CandidatesEventDecodesStringsSelectedAndOrientation)
{
    std::vector<std::string> got;
    int gotSelected = -99;
    bool gotHorizontal = false;
    int calls = 0;
    TextInputEXT::TextEditingCandidatesEXT +=
        [&](const std::vector<std::string>& c, int selected, bool horizontal)
        {
            got = c;
            gotSelected = selected;
            gotHorizontal = horizontal;
            ++calls;
        };

    TextEditingCandidatesEvent event;
    event.candidates = {"\xE6\x84\x9B", "love", "eye"}; // UTF-8 incl. a CJK char
    event.selectedCandidate = 1;
    event.horizontal = true;
    PlatformInputBridge::ProcessEvent(event);

    ASSERT_EQ(calls, 1);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], "\xE6\x84\x9B");
    EXPECT_EQ(got[1], "love");
    EXPECT_EQ(got[2], "eye");
    EXPECT_EQ(gotSelected, 1);
    EXPECT_TRUE(gotHorizontal);
}

// N-014: a null candidate list (SDL reports "no candidates available") dispatches an empty list.
TEST_F(SdlInputBridgeCandidatesTest, NullCandidatesDispatchEmptyList)
{
    bool fired = false;
    std::size_t size = 999;
    int selected = 0;
    TextInputEXT::TextEditingCandidatesEXT +=
        [&](const std::vector<std::string>& c, int sel, bool)
        {
            fired = true;
            size = c.size();
            selected = sel;
        };

    TextEditingCandidatesEvent event;
    event.selectedCandidate = -1;
    PlatformInputBridge::ProcessEvent(event);

    EXPECT_TRUE(fired);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(selected, -1);
}
