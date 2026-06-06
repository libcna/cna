#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioCategory;

    /// XACT audio engine. Loads and manages .XGS global settings files.
    ///
    /// NOTE: XACT binary formats (.XGS / .XWB / .XSB) are not supported by the
    /// SDL3_mixer backend.  This implementation provides the full XNA 4.0 API
    /// surface as a functional stub: the engine initialises, variables and
    /// categories are tracked in memory, but no actual XACT sound is produced.
    /// Use SoundEffect and MediaPlayer for audio playback.
    class AudioEngine : public System::Object, public System::IDisposable
    {
    public:
        static constexpr int ContentVersion = 46;

        /// Raised when this engine is about to be disposed.
        System::EventHandler<System::EventArgs> Disposing;

        /// Constructs an AudioEngine from a .XGS settings file path.
        explicit AudioEngine(const std::string& settingsFile);

        /// Constructs an AudioEngine with explicit look-ahead time and renderer ID.
        AudioEngine(const std::string& settingsFile,
                    System::TimeSpan lookAheadTime,
                    const std::string& rendererId);

        ~AudioEngine() override;

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        /// Gets whether this instance has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Gets the list of available audio renderers.
        [[nodiscard]] const std::vector<RendererDetail>& getRendererDetailsProperty() const;

        /// Returns the AudioCategory with the given name.
        [[nodiscard]] AudioCategory GetCategory(const std::string& name);

        /// Gets the current value of a named global variable.
        [[nodiscard]] float GetGlobalVariable(const std::string& name) const;

        /// Sets a named global variable to the given value.
        void SetGlobalVariable(const std::string& name, float value);

        /// Processes pending XACT work. No-op on SDL3_mixer backend.
        void Update();

        /// Releases all XACT resources held by this engine.
        void Dispose() override;

    private:
        bool isDisposed_ = false;
        std::vector<RendererDetail> rendererDetails_;
        std::unordered_map<std::string, float> globalVariables_;

        void Init();

        GetTypeNameHPP()
    };
}
