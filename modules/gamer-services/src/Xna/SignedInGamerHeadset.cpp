// SPDX-License-Identifier: MS-PL
// SignedInGamer.IsHeadset. Its own translation unit because it names Microphone: asking whether a
// gamer is signed in must not link the audio capture subsystem.
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    bool SignedInGamer::IsHeadset(const Microsoft::Xna::Framework::Audio::Microphone& microphone) const
    {
        return microphone.getIsHeadsetProperty();
    }

}
