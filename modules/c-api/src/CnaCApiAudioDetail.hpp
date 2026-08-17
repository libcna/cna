// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_AUDIO_DETAIL_HPP
#define CNA_C_API_AUDIO_DETAIL_HPP

#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

#include <memory>
#include <utility>

namespace Microsoft::Xna::Framework::Audio {
class SoundEffect;
}

namespace CNA::C::Detail {

// Every audio event registration -- streaming buffers, capture buffers, XACT disposal -- is released
// through one `cna_audio_unsubscribe_ext` route, so every one of them must be stored under the same
// C++ type: the handle registry casts a slot straight back to the type the caller names. The base
// class is what makes that single cast correct for all of them.
class AudioRegistrationBase {
public:
    AudioRegistrationBase() = default;
    AudioRegistrationBase(const AudioRegistrationBase&) = delete;
    AudioRegistrationBase& operator=(const AudioRegistrationBase&) = delete;
    virtual ~AudioRegistrationBase() = default;
};

class AudioRegistration final : public AudioRegistrationBase {
public:
    using Source = System::EventHandler<System::EventArgs>;
    using Token = Source::Token;

    AudioRegistration(std::shared_ptr<void> owner, Source* const source, const Token token)
        : owner_(std::move(owner))
        , source_(source)
        , token_(token)
    {
    }

    ~AudioRegistration() override
    {
        source_->Remove(token_);
    }

private:
    std::shared_ptr<void> owner_;
    Source* source_;
    Token token_;
};

[[nodiscard]] inline CNA_Result PublishAudioRegistration(
    std::shared_ptr<AudioRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::AudioEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The audio registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

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
