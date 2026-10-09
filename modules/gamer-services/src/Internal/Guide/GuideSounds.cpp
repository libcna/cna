// SPDX-License-Identifier: MS-PL
// The CNA system sounds: short, quiet tones synthesized here (no sound files), played through the
// standard SoundEffect path when the Guide is driven by a player. CNA_GAMER_SERVICES_SOUNDS=0 turns
// them off; without an audio device they are silently absent.
#include "GuideScreen.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace Audio=Microsoft::Xna::Framework::Audio;
constexpr int Rate=22050;
constexpr double Tau=6.283185307179586;

// A tone: partials (frequency, level) under a fast attack and an exponential decay.
struct Note {double start,length;std::array<std::pair<double,double>,3> partials;double decay;};

std::vector<SharpRuntime::bytecs> synthesize(std::initializer_list<Note> notes,double seconds)
{
    const int samples=static_cast<int>(seconds*Rate);
    std::vector<double> mix(static_cast<std::size_t>(samples),0.0);
    for(const auto& note:notes) {
        const int first=static_cast<int>(note.start*Rate),count=static_cast<int>(note.length*Rate);
        for(int i=0;i<count&&first+i<samples;++i) {
            const double t=static_cast<double>(i)/Rate;
            const double envelope=std::min(1.0,t/0.004)*std::exp(-t*note.decay);
            double value=0.0;
            for(const auto& [frequency,level]:note.partials)if(frequency>0)value+=level*std::sin(Tau*frequency*t);
            mix[static_cast<std::size_t>(first+i)]+=value*envelope;
        }
    }
    std::vector<SharpRuntime::bytecs> bytes(static_cast<std::size_t>(samples)*2);
    for(int i=0;i<samples;++i) {
        // A short fade at the end so no sound clicks off.
        const double tail=std::min(1.0,static_cast<double>(samples-i)/(0.01*Rate));
        const auto sample=static_cast<std::int16_t>(std::clamp(mix[static_cast<std::size_t>(i)]*tail,-1.0,1.0)*14000.0);
        bytes[static_cast<std::size_t>(i)*2]=static_cast<SharpRuntime::bytecs>(sample&0xff);
        bytes[static_cast<std::size_t>(i)*2+1]=static_cast<SharpRuntime::bytecs>((sample>>8)&0xff);
    }
    return bytes;
}

std::vector<SharpRuntime::bytecs> soundData(Sound sound)
{
    switch(sound) {
    case Sound::Move:   // a soft wooden tick
        return synthesize({{0.0,0.05,{{{1320,0.5},{2640,0.12},{0,0}}},70.0}},0.06);
    case Sound::Accept: // two notes up a fifth
        return synthesize({{0.0,0.09,{{{660,0.45},{1320,0.10},{0,0}}},28.0},{0.055,0.16,{{{990,0.45},{1980,0.10},{0,0}}},22.0}},0.22);
    case Sound::Back:   // two notes down
        return synthesize({{0.0,0.08,{{{880,0.4},{1760,0.08},{0,0}}},30.0},{0.05,0.13,{{{587,0.4},{1174,0.08},{0,0}}},26.0}},0.19);
    case Sound::Open:   // a soft open chord
        return synthesize({{0.0,0.35,{{{523.25,0.30},{659.25,0.24},{783.99,0.20}}},9.0}},0.36);
    case Sound::Notify: // a two-tone bell
        return synthesize({{0.0,0.5,{{{1046.5,0.35},{2093,0.10},{3136,0.04}}},8.0},{0.09,0.55,{{{1568,0.30},{3136,0.08},{0,0}}},7.0}},0.66);
    case Sound::Error:  // a low double knock
        return synthesize({{0.0,0.09,{{{220,0.5},{440,0.2},{0,0}}},30.0},{0.11,0.1,{{{196,0.5},{392,0.2},{0,0}}},30.0}},0.23);
    }
    return {};
}

bool soundsWanted()
{
    static const bool wanted=[] {
        const auto* value=std::getenv("CNA_GAMER_SERVICES_SOUNDS");
        return !(value&&std::string(value)=="0");
    }();
    return wanted;
}

struct Bank {
    std::array<std::unique_ptr<Audio::SoundEffect>,6> effects;
    bool failed=false;
};
Bank& bank(){static Bank* value=new Bank;return *value;}
}

void play(Sound sound)
{
    auto& sounds=bank();
    if(!soundsWanted()||sounds.failed)return;
    auto& effect=sounds.effects[static_cast<std::size_t>(sound)];
    try {
        if(!effect)effect=std::make_unique<Audio::SoundEffect>(soundData(sound),Rate,Audio::AudioChannels::Mono);
        (void)effect->Play(0.45f,0.0f,0.0f);
    } catch(...) {
        // No audio device (or none this build has): the Guide stays silent from now on.
        sounds.failed=true;
        for(auto& owned:sounds.effects)owned.reset();
    }
}

void releaseSounds()
{
    for(auto& owned:bank().effects)owned.reset();
}
}
