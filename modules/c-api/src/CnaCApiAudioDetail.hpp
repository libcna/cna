// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_AUDIO_DETAIL_HPP
#define CNA_C_API_AUDIO_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Audio {
class SoundEffect;
}

namespace CNA::C::Detail {

// Sound effects are created by the audio adapter, but the content adapter also produces them
// through the canonical Load<SoundEffect> specialization. Both routes must yield exactly the same
// owned game-child handle, so the audio adapter owns this one factory instead of each adapter
// building its own resource record.
[[nodiscard]] CNA_Result CreateOwnedSoundEffect(
    std::shared_ptr<Microsoft::Xna::Framework::Audio::SoundEffect> soundEffect,
    CNA_Handle parentGame,
    CNA_Handle* outSoundEffect);

} // namespace CNA::C::Detail

#endif
