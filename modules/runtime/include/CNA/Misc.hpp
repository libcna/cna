#pragma once

namespace CNA
{
    /** @brief Options that control which CNA subsystems are initialized at startup. */
    struct RuntimeOptions
    {
        /** @brief When true, the graphics subsystem is initialized. */
        bool enableGraphics = true;
        /** @brief When true, the audio subsystem is initialized. */
        bool enableAudio = true;
        /** @brief When true, the input subsystem is initialized. */
        bool enableInput = true;
        /** @brief When true, the content pipeline is initialized. */
        bool enableContent = true;
    };

    /** @brief Manages the lifetime of the CNA runtime and its subsystems. */
    class Runtime
    {
    public:
        /**
         * @brief Initializes all enabled subsystems.
         *
         * @param options Flags controlling which subsystems to bring up.
         * @return true if initialization succeeded; false otherwise.
         */
        bool Initialize(const RuntimeOptions& options);

        /** @brief Shuts down all active subsystems. */
        void Shutdown();

        /**
         * @brief Gets whether the graphics subsystem was successfully initialized.
         *
         * @return true if graphics is enabled; otherwise false.
         */
        bool IsGraphicsEnabled() const;

        /**
         * @brief Gets whether the audio subsystem was successfully initialized.
         *
         * @return true if audio is enabled; otherwise false.
         */
        bool IsAudioEnabled() const;

        /**
         * @brief Gets whether the input subsystem was successfully initialized.
         *
         * @return true if input is enabled; otherwise false.
         */
        bool IsInputEnabled() const;
    };
}
