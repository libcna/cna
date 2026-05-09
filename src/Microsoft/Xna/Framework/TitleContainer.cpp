#include "Microsoft/Xna/Framework/TitleContainer.hpp"

#include <string>
#include "CNA/Logger.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#if defined(__ANDROID__)
#  include <SDL3/SDL.h>
#endif

namespace Microsoft::Xna::Framework
{
    std::unique_ptr<System::IO::Stream> TitleContainer::OpenStream(const std::string& path)
    {
        CNA::Logger::Info("[TitleContainer] OpenStream requested: " + path);

        // Try normal filesystem first (works on desktop and sometimes Android)
        try
        {
            auto stream = std::make_unique<System::IO::FileStream>(path);
            CNA::Logger::Info("[TitleContainer] Filesystem open succeeded: " + path);
            return stream;
        }
        catch (const std::exception& e)
        {
            CNA::Logger::Warn("[TitleContainer] Filesystem open failed for \"" + path + "\": " + e.what());
        }

#if defined(__ANDROID__)
        // On Android, fall back to APK assets via SDL3
        CNA::Logger::Info("[TitleContainer] Trying Android APK asset: " + path);

        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (!io)
        {
            CNA::Logger::Error("[TitleContainer] Android asset open failed for \"" + path + "\": " + SDL_GetError());
            throw std::runtime_error("[TitleContainer] Failed to open asset: " + path);
        }

        size_t dataSize = 0;
        void* rawData = SDL_LoadFile_IO(io, &dataSize, true);
        if (!rawData)
        {
            CNA::Logger::Error("[TitleContainer] Android asset read failed for \"" + path + "\": " + SDL_GetError());
            throw std::runtime_error("[TitleContainer] Failed to read asset: " + path);
        }

        CNA::Logger::Info("[TitleContainer] Android asset loaded: " + path +
                          " (" + std::to_string(dataSize) + " bytes)");

        auto* bytes = static_cast<SharpRuntime::bytecs*>(rawData);
        auto memStream = std::make_unique<System::IO::MemoryStream>(
            bytes, static_cast<SharpRuntime::intcs>(dataSize));
        SDL_free(rawData);

        return memStream;
#else
        throw std::runtime_error("[TitleContainer] Failed to open stream: " + path);
#endif
    }
}
