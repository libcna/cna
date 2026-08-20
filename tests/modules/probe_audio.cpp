// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Audio (plans/MODULARIZATION_PLAN.md §4/§11): the 3D audio value
// types must be usable without content, runtime, devices, extensions or networking. Audio
// legitimately pulls media (the declared audio<->media FrameworkDispatcher pump cycle),
// input (the dispatcher's TouchPanel pump), graphics-core (via media), math and core -- the
// paired ModuleLinkClosure_Audio ctest asserts the closure stays free of the layers above.
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework::Audio;

    AudioEmitter emitter;
    AudioListener listener;
    emitter.setDopplerScaleProperty(2.0f);
    std::printf("audio probe: doppler=%.1f state=%d\n",
                emitter.getDopplerScaleProperty(),
                static_cast<int>(SoundState::Stopped));
    (void)listener;
    return 0;
}
