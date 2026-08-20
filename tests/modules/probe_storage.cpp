// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Storage (plans/MODULARIZATION_PLAN.md §4/§11): the storage surface
// must be usable with nothing but the storage archive, sharp-runtime and the core module's
// header-only surface (PlayerIndex/CNAEXT marker -- no core symbol, so no core archive).
// The paired ModuleLinkClosure_Storage ctest asserts no other CNA archive appears.
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework;

    try {
        throw Storage::StorageDeviceNotConnectedException();
    } catch (const Storage::StorageDeviceNotConnectedException& e) {
        std::printf("storage probe: caught '%s' player=%d\n", e.what(),
                    static_cast<int>(PlayerIndex::One));
    }
    return 0;
}
