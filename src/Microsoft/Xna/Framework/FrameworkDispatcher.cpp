#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"

namespace Microsoft::Xna::Framework
{
    bool FrameworkDispatcher::ActiveSongChanged = false;
    bool FrameworkDispatcher::MediaStateChanged = false;
    std::vector<Audio::DynamicSoundEffectInstance*> FrameworkDispatcher::Streams;
    std::mutex FrameworkDispatcher::StreamsMutex;

    void FrameworkDispatcher::Update()
    {
        {
            std::lock_guard<std::mutex> lock(StreamsMutex);

            for (std::size_t i = 0; i < Streams.size();)
            {
                Audio::DynamicSoundEffectInstance* instance = Streams[i];

                if (instance != nullptr)
                {
                    instance->Update();
                }

                if (instance == nullptr || instance->getIsDisposedProperty())
                {
                    Streams.erase(Streams.begin() + static_cast<std::ptrdiff_t>(i));
                }
                else
                {
                    ++i;
                }
            }
        }

        if (Audio::Microphone::micList != nullptr)
        {
            for (Audio::Microphone* microphone : *Audio::Microphone::micList)
            {
                if (microphone != nullptr)
                {
                    microphone->CheckBuffer();
                }
            }
        }

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
