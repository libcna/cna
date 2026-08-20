// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Net (plans/MODULARIZATION_PLAN.md §4/§11): the session-property
// container must be usable through the networking closure (gamer-services -> runtime ->
// framework + enet) without the device modules or the extension modules. The paired
// ModuleLinkClosure_Net ctest asserts those stay out while enet is genuinely present.
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"

#include <cstdio>

int main()
{
    Microsoft::Xna::Framework::Net::NetworkSessionProperties props;
    props.setItem(0, 7);
    std::printf("net probe: prop[0]=%d count=%d\n",
                props.getItem(0).value_or(0),
                props.getCountProperty());
    return 0;
}
