#pragma once

namespace CNA
{
    struct RuntimeOptions
    {
        bool enableGraphics = true;
        bool enableAudio = true;
        bool enableInput = true;
        bool enableContent = true;
    };

    class Runtime
    {
    public:
        bool Initialize(const RuntimeOptions& options);
        void Shutdown();

        bool IsGraphicsEnabled() const;
        bool IsAudioEnabled() const;
        bool IsInputEnabled() const;
    };
}
