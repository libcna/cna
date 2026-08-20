// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Input (plans/MODULARIZATION_PLAN.md §4/§11): the input value types
// must be usable without content, media, audio, runtime, devices, extensions or networking.
// Input legitimately pulls graphics-core (the declared graphics<->input XNA cycle), math and
// core -- the paired ModuleLinkClosure_Input ctest asserts the closure stays free of the
// layers above it.
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework::Input;

    KeyboardState state;
    std::printf("input probe: A down=%d\n", state.IsKeyDown(Keys::A) ? 1 : 0);
    return 0;
}
