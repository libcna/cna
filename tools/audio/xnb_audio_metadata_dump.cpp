// SPDX-License-Identifier: MS-PL
//
// plans/plan_audio.md AUD-06-025: standalone tool that inspects a .xnb SoundEffect asset's metadata
// (name, decoded duration) without playing it -- loads through the real, unmodified production
// path (ContentManager::Load<SoundEffect>(), the exact same entry point a real game uses, going
// through actual decode so the reported duration reflects the real decoded audio, not just a
// header field) and never calls Play()/CreateInstance(). Emits stable, single-line JSON to stdout
// for scripting/CI manifests: on success, `{"asset":"...","status":"ok","durationMs":...}`; on
// failure, `{"asset":"...","status":"error","message":"..."}` (with `"` and `\` escaped). Exit
// code 0 on success, 1 on load failure, 2 on bad usage.
//
// Deliberately does not report raw WAVEFORMATEX fields (format tag, channels, sample rate, bits,
// loop points): real XNA's SoundEffect only exposes Name and Duration publicly, and this tool
// intentionally stays within that same public surface rather than adding new CNAEXT accessors
// under a P2 tooling task -- see AUD-06-021's differential tool
// (tools/audio/fna_soundeffect_metadata_dump/) for a from-first-principles WAVEFORMATEX field dump
// against a real fixture corpus if that level of detail is ever needed for debugging.
//
// Usage: cna_xnb_audio_metadata_dump <path-to.xnb>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

namespace
{
    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <path-to.xnb>\n", argv[0]);
        return 2;
    }

    const std::filesystem::path xnbPath(argv[1]);
    const std::string assetName = xnbPath.stem().string();
    const std::string rootDir = xnbPath.has_parent_path()
        ? xnbPath.parent_path().string()
        : ".";

    CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();

    try
    {
        Microsoft::Xna::Framework::Content::ContentManager cm(nullptr, rootDir);
        Microsoft::Xna::Framework::Audio::SoundEffect effect =
            cm.Load<Microsoft::Xna::Framework::Audio::SoundEffect>(assetName);

        const double durationMs = effect.getDurationProperty().getTotalMillisecondsProperty();
        std::printf(
            "{\"asset\":\"%s\",\"status\":\"ok\",\"durationMs\":%.3f}\n",
            JsonEscape(assetName).c_str(), durationMs);
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::printf(
            "{\"asset\":\"%s\",\"status\":\"error\",\"message\":\"%s\"}\n",
            JsonEscape(assetName).c_str(), JsonEscape(ex.what()).c_str());
        return 1;
    }
}
