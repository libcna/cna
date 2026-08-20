// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Media (plans/MODULARIZATION_PLAN.md §4/§11): the media value types
// must be usable without content, runtime, devices, extensions or networking. Media
// legitimately pulls audio (the declared audio<->media cycle), graphics-core, input (the
// FrameworkDispatcher pump surface it compiles), math and core -- the paired
// ModuleLinkClosure_Media ctest asserts the closure stays free of the layers above it.
#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework::Media;

    VisualizationData data;
    std::printf("media probe: samples=%zu state=%d source=%d\n",
                data.getSamplesProperty().size(),
                static_cast<int>(MediaState::Stopped),
                static_cast<int>(MediaSourceType::LocalDevice));
    return 0;
}
