// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework
{
    bool FrameworkDispatcher::ActiveSongChanged = false;
    bool FrameworkDispatcher::MediaStateChanged = false;
    std::vector<Audio::DynamicSoundEffectInstance*> FrameworkDispatcher::Streams;
    std::mutex FrameworkDispatcher::StreamsMutex;

    void FrameworkDispatcher::Update()
    {
        // P11-DISPATCH-001: instance->Update() below can synchronously fire BufferNeeded (a
        // realistic XNA usage pattern is disposing the instance from inside that very handler
        // once no more data will be provided), which reaches DynamicSoundEffectInstance::
        // StopInternal() -- which itself locks StreamsMutex to remove itself from Streams. A
        // std::mutex is not recursive, so calling instance->Update() while still holding
        // StreamsMutex here would self-deadlock the very first time real game code disposed a
        // stream from its own BufferNeeded handler. Snapshot the list under the lock, then run
        // every instance's Update() with the lock released; a disposed instance already removes
        // itself from Streams via StopInternal(), so the trailing cleanup pass below is mostly
        // defensive (matches the previous code's own belt-and-suspenders erase-if-disposed check).
        std::vector<Audio::DynamicSoundEffectInstance*> snapshot;
        {
            std::lock_guard<std::mutex> lock(StreamsMutex);
            snapshot = Streams;
        }

        for (Audio::DynamicSoundEffectInstance* instance : snapshot)
        {
            if (instance != nullptr)
            {
                instance->Update();
            }
        }

        {
            std::lock_guard<std::mutex> lock(StreamsMutex);
            Streams.erase(
                std::remove_if(Streams.begin(), Streams.end(),
                    [](Audio::DynamicSoundEffectInstance* instance)
                    {
                        return instance == nullptr || instance->getIsDisposedProperty();
                    }),
                Streams.end());
        }

        Audio::Microphone::CheckAllBuffers();

        Media::MediaPlayer::Update();

        if (ActiveSongChanged)
        {
            Media::MediaPlayer::OnActiveSongChanged();
            ActiveSongChanged = false;
        }

        if (MediaStateChanged)
        {
            Media::MediaPlayer::OnMediaStateChanged();
            MediaStateChanged = false;
        }

        if (Input::Touch::TouchPanel::getTouchDeviceExistsProperty())
        {
            Input::Touch::TouchPanel::Update();
        }
    }
}
