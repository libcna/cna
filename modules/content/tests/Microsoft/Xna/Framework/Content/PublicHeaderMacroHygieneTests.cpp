// SPDX-License-Identifier: MS-PL
//
// CNA-DEMOS-ISSUE-008: CNA's public headers must not hand a game ordinary words as macros. They
// used to include SharpRuntime/Prop.hpp, which defines YES, NO, nothing, getter1, static0, ... at
// global scope, so `enum class Answer { YES, NO };` or a variable named `nothing` broke.

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <gtest/gtest.h>

#if defined(YES) || defined(NO) || defined(nothing) || defined(getter1) || defined(setter1) || \
    defined(member0) || defined(static0) || defined(constret1) || defined(ref1) || \
    defined(constmet1) || defined(DEF_PROP) || defined(DEF_MEMBER) || defined(DDATA) || \
    defined(IDATA)
#error "a CNA public header defines a common word as a macro (CNA-DEMOS-ISSUE-008)"
#endif

namespace
{
    enum class Answer { YES, NO };
}

TEST(PublicHeaderMacroHygiene, CommonWordsRemainOrdinaryIdentifiers)
{
    const int nothing = 3;
    EXPECT_EQ(nothing, 3);
    EXPECT_NE(Answer::YES, Answer::NO);
}
