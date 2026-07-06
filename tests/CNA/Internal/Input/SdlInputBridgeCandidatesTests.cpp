// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

using CNA::Internal::Input::SdlInputBridge;
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

    const char* cands[] = {"\xE6\x84\x9B", "love", "eye"}; // UTF-8 incl. a CJK char
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_EDITING_CANDIDATES;
    e.edit_candidates.candidates = cands;
    e.edit_candidates.num_candidates = 3;
    e.edit_candidates.selected_candidate = 1;
    e.edit_candidates.horizontal = true;
    SdlInputBridge::ProcessEvent(e);

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

    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_EDITING_CANDIDATES;
    e.edit_candidates.candidates = nullptr;
    e.edit_candidates.num_candidates = 0;
    e.edit_candidates.selected_candidate = -1;
    e.edit_candidates.horizontal = false;
    SdlInputBridge::ProcessEvent(e);

    EXPECT_TRUE(fired);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(selected, -1);
}
