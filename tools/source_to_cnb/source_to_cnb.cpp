// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-109/CNBF-110/CNBF-112: compile a source file straight into a `.cnb`.
//
// One front end rather than one executable per asset type. What the input IS decides what comes
// out, because that is already unambiguous from the extension, and seven near-identical programs
// would be seven places to fix the same bug.
//
// Everything it does is headless: it constructs no GraphicsDevice and opens no audio device, so it
// runs on a build machine with no display, no GPU and no sound card. The import path is
// source bytes -> canonical CPU representation -> CNB codec, and nothing else.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"

namespace
{
    void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) { throw std::runtime_error("cannot write '" + path.string() + "'"); }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) { throw std::runtime_error("failed while writing '" + path.string() + "'"); }
    }

    std::string LowerExtension(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    void PrintUsage(const char* argv0)
    {
        std::cerr
            << "Usage: " << argv0 << " <input> <output.cnb> [options]\n\n"
            << "Compiles one source file into a .cnb asset. Headless and deterministic: no\n"
            << "GraphicsDevice, no audio device, no clock, no randomness.\n\n"
            << "Input is chosen by extension:\n"
            << "  .png .jpg .jpeg .bmp .tga .gif .psd .hdr .pic .pnm  -> Texture2D\n"
            << "  .wav                                                -> SoundEffect\n"
            << "  anything else, with --as song|video                 -> Song/Video metadata\n\n"
            << "Options:\n"
            << "  --name <logical>       Logical asset name recorded in CMET.\n"
            << "                         Default: the input file's stem.\n"
            << "  --color-key R,G,B      Texture2D only. Make pixels of exactly this RGB fully\n"
            << "                         transparent, keeping their colour. Never applied unless\n"
            << "                         asked for: silently rewriting pixels is worse than\n"
            << "                         making the author say so.\n"
            << "  --as <kind>            Force the output kind: texture2d, soundeffect, song,\n"
            << "                         video.\n"
            << "  --stream <name>        Song/Video only, REQUIRED. Logical name of the media\n"
            << "                         file to stream, relative to the content root.\n"
            << "  --duration-ms <n>      Song/Video metadata. Default 0 (\"unknown\").\n"
            << "  --title <text>         Song only. Display name.\n"
            << "  --frame-size <WxH>     Video only, REQUIRED.\n"
            << "  --fps <f>              Video only, REQUIRED.\n"
            << "  --soundtrack <n>       Video only. 0 Music, 1 Dialog, 2 MusicAndDialog.\n"
            << "  --quiet                Print nothing on success.\n"
            << "  --help                 Show this message.\n\n"
            << "Song and Video carry metadata plus a reference, never the media itself, so the\n"
            << "values above are ARGUMENTS rather than something read from the file: determining\n"
            << "them would need a multimedia decoder CNA does not expose headlessly, and inventing\n"
            << "them would be worse than requiring them.\n";
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> positional;
    std::string logicalName;
    std::string forcedKind;
    std::string stream;
    std::string title;
    std::string frameSize;
    std::optional<std::array<std::uint8_t, 3>> colorKey;
    std::uint32_t durationMs = 0u;
    std::uint32_t soundtrack = 0u;
    float fps = 0.0f;
    bool quiet = false;
    bool haveFps = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const auto next = [&](const char* what) -> std::string
        {
            if (i + 1 >= argc) { throw std::runtime_error(std::string(what) + " needs a value"); }
            return argv[++i];
        };
        try
        {
            if (arg == "--help") { PrintUsage(argv[0]); return 0; }
            else if (arg == "--quiet") { quiet = true; }
            else if (arg == "--name") { logicalName = next("--name"); }
            else if (arg == "--as") { forcedKind = next("--as"); }
            else if (arg == "--stream") { stream = next("--stream"); }
            else if (arg == "--title") { title = next("--title"); }
            else if (arg == "--frame-size") { frameSize = next("--frame-size"); }
            else if (arg == "--duration-ms") { durationMs = static_cast<std::uint32_t>(std::stoul(next("--duration-ms"))); }
            else if (arg == "--soundtrack") { soundtrack = static_cast<std::uint32_t>(std::stoul(next("--soundtrack"))); }
            else if (arg == "--fps") { fps = std::stof(next("--fps")); haveFps = true; }
            else if (arg == "--color-key")
            {
                const std::string value = next("--color-key");
                std::array<std::uint8_t, 3> key{};
                std::size_t start = 0;
                for (std::size_t c = 0; c < 3u; ++c)
                {
                    const std::size_t comma = value.find(',', start);
                    const std::string part = value.substr(start, comma - start);
                    const int component = std::stoi(part);
                    if (component < 0 || component > 255)
                    {
                        throw std::runtime_error("--color-key components are 0-255");
                    }
                    key[c] = static_cast<std::uint8_t>(component);
                    if (comma == std::string::npos && c != 2u)
                    {
                        throw std::runtime_error("--color-key needs three components: R,G,B");
                    }
                    start = comma == std::string::npos ? value.size() : comma + 1u;
                }
                colorKey = key;
            }
            else if (!arg.empty() && arg.front() == '-')
            {
                std::cerr << "error: unknown option '" << arg << "'\n";
                return 1;
            }
            else { positional.emplace_back(arg); }
        }
        catch (const std::exception& e)
        {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }

    if (positional.size() != 2u) { PrintUsage(argv[0]); return 1; }
    const std::filesystem::path input = positional[0];
    const std::filesystem::path output = positional[1];

    try
    {
        if (!std::filesystem::exists(input))
        {
            std::cerr << "error: cannot open '" << input.string() << "'.\n";
            return 1;
        }
        const std::string name = logicalName.empty() ? input.stem().string() : logicalName;
        const std::string ext = LowerExtension(input);

        std::string kind = forcedKind;
        if (kind.empty())
        {
            if (ext == ".wav") { kind = "soundeffect"; }
            else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                      ext == ".tga" || ext == ".gif" || ext == ".psd" || ext == ".hdr" ||
                      ext == ".pic" || ext == ".pnm" || ext == ".ppm" || ext == ".pgm")
            {
                kind = "texture2d";
            }
            else
            {
                std::cerr << "error: '" << ext << "' does not name a kind this compiler knows. "
                          << "Use --as texture2d|soundeffect|song|video.\n";
                return 1;
            }
        }

        std::vector<std::uint8_t> bytes;
        std::string produced;
        if (kind == "texture2d")
        {
            CNA::Content::Cnb::CnbImageImportOptions options;
            options.colorKey = colorKey;
            const auto texture =
                CNA::Content::Cnb::ImportImageAsCnbTexture2D(input.string(), options);
            bytes = CNA::Content::Cnb::EncodeTexture2DToCnb(texture, name);
            produced = "Texture2D " + std::to_string(texture.width) + "x" +
                        std::to_string(texture.height) + " Rgba8";
        }
        else if (kind == "soundeffect")
        {
            const auto sound = CNA::Content::Cnb::ImportWavAsCnbSoundEffect(input.string());
            bytes = CNA::Content::Cnb::EncodeSoundEffectToCnb(sound, name);
            produced = "SoundEffect " + std::to_string(sound.frameCount) + " frames, " +
                        std::to_string(sound.channels) + "ch " +
                        std::to_string(sound.sampleRate) + " Hz Pcm16";
        }
        else if (kind == "song" || kind == "video")
        {
            if (stream.empty())
            {
                std::cerr << "error: --stream is required for " << kind
                          << ": a Song/Video .cnb carries metadata and a reference, so the "
                             "reference is the one thing it cannot omit.\n";
                return 1;
            }
            if (kind == "song")
            {
                CNA::Content::Cnb::CnbSongData song;
                song.streamReference = stream;
                song.name = title;
                song.durationMs = durationMs;
                bytes = CNA::Content::Cnb::EncodeSongToCnb(song, name);
                produced = "Song -> " + stream;
            }
            else
            {
                std::uint32_t width = 0u;
                std::uint32_t height = 0u;
                const std::size_t x = frameSize.find('x');
                if (x != std::string::npos)
                {
                    width = static_cast<std::uint32_t>(std::stoul(frameSize.substr(0, x)));
                    height = static_cast<std::uint32_t>(std::stoul(frameSize.substr(x + 1u)));
                }
                if (width == 0u || height == 0u || !haveFps)
                {
                    std::cerr << "error: video needs --frame-size WxH and --fps. CNA has no "
                                 "headless multimedia decoder, so these are arguments rather "
                                 "than values guessed from the file.\n";
                    return 1;
                }
                CNA::Content::Cnb::CnbVideoData video;
                video.streamReference = stream;
                video.durationMs = durationMs;
                video.width = width;
                video.height = height;
                video.framesPerSecond = fps;
                video.soundtrackType = soundtrack;
                bytes = CNA::Content::Cnb::EncodeVideoToCnb(video, name);
                produced = "Video -> " + stream;
            }
        }
        else
        {
            std::cerr << "error: unknown kind '" << kind << "'.\n";
            return 1;
        }

        std::error_code ec;
        if (output.has_parent_path())
        {
            std::filesystem::create_directories(output.parent_path(), ec);
        }
        WriteFile(output, bytes);
        if (!quiet)
        {
            std::cout << output.string() << ": " << produced << ", " << bytes.size()
                      << " bytes\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
