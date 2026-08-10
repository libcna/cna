// SPDX-License-Identifier: MS-PL
// Task 934: verify Content.Load<TextureCube>() reads a real .dds cubemap file end to end --
// exercising the new TextureCubeTypeReader (ContentManager.cpp), which delegates to the
// already-existing, already-proven TextureCube::DDSFromStreamEXT() decoder.
//
// Confirmed gap (DEFERRED.md item #14): ContentManager.cpp had no TextureCubeTypeReader
// registered at all, so Content.Load<TextureCube>(...) always threw, even though the
// underlying DDS/DXT decoding (TextureCube::DDSFromStreamEXT, used directly by every existing
// easygl_texturecube_* test) already worked correctly.
//
// Method: hand-encode a minimal, real DXT1-compressed DDS cubemap (6 faces, 4x4 texels each --
// the smallest valid square DXT1 size, one full compression block per face, no mip chain) with
// each face set to a distinct solid color, write it to a temp file, load it via
// Content.Load<TextureCube>(), and read back every face's pixel data via TextureCube::GetData()
// to confirm each face decoded to (approximately, allowing for DXT1's inherent RGB565 precision
// loss) the color that was actually encoded for it -- not a placeholder, not another face's
// color, not garbage.
//
// Exit code 0 = all 6 faces PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void AppendU32LE(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }

    void AppendU16LE(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    // Packs an 8-bit RGB color into RGB565 (DXT1's own colour endpoint format).
    std::uint16_t ToRgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::uint16_t r5 = static_cast<std::uint16_t>(r >> 3);
        const std::uint16_t g6 = static_cast<std::uint16_t>(g >> 2);
        const std::uint16_t b5 = static_cast<std::uint16_t>(b >> 3);
        return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
    }

    // A single solid-color DXT1 4x4 block: color0 == color1 (both the target color), every
    // 2-bit pixel index == 0b00 (references color0) -- valid and unambiguous regardless of
    // which of DXT1's two interpolation modes an equal-endpoint block falls into.
    void AppendSolidDxt1Block(std::vector<std::uint8_t>& out, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::uint16_t c = ToRgb565(r, g, b);
        AppendU16LE(out, c);
        AppendU16LE(out, c);
        AppendU32LE(out, 0); // all 16 indices = 0
    }

    constexpr std::uint32_t kDdsMagic       = 0x20534444; // "DDS " LE
    constexpr std::uint32_t kDdsHeaderSize  = 124;
    constexpr std::uint32_t kDdsdCaps       = 0x1;
    constexpr std::uint32_t kDdsdHeight     = 0x2;
    constexpr std::uint32_t kDdsdWidth      = 0x4;
    constexpr std::uint32_t kDdsdPixelFmt   = 0x1000;
    constexpr std::uint32_t kDdpfFourCC     = 0x4;
    constexpr std::uint32_t kFourCcDxt1     = 0x31545844; // "DXT1" LE
    constexpr std::uint32_t kDdscapsTexture = 0x1000;
    constexpr std::uint32_t kDdscapsComplex = 0x8;
    constexpr std::uint32_t kDdscaps2Cubemap = 0x200;
    constexpr std::uint32_t kDdscaps2AllFaces = 0xFC00;

    // Builds a real, minimal, single-mip DXT1 DDS cubemap: 4x4 texels/face, one distinct solid
    // color per face.
    std::vector<std::uint8_t> BuildDxt1Cubemap()
    {
        std::vector<std::uint8_t> out;
        out.reserve(128 + 6 * 8);

        AppendU32LE(out, kDdsMagic);
        AppendU32LE(out, kDdsHeaderSize);
        AppendU32LE(out, kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFmt);
        AppendU32LE(out, 4);  // height
        AppendU32LE(out, 4);  // width
        AppendU32LE(out, 8);  // pitchOrLinearSize (one DXT1 block's byte size; not validated)
        AppendU32LE(out, 0);  // depth
        AppendU32LE(out, 1);  // mipMapCount
        for (int i = 0; i < 11; ++i) AppendU32LE(out, 0); // reserved1

        AppendU32LE(out, 32);              // pixelformat.dwSize
        AppendU32LE(out, kDdpfFourCC);      // pixelformat.dwFlags
        AppendU32LE(out, kFourCcDxt1);      // pixelformat.dwFourCC
        AppendU32LE(out, 0);                // dwRGBBitCount
        AppendU32LE(out, 0);                // dwRBitMask
        AppendU32LE(out, 0);                // dwGBitMask
        AppendU32LE(out, 0);                // dwBBitMask
        AppendU32LE(out, 0);                // dwABitMask

        AppendU32LE(out, kDdscapsTexture | kDdscapsComplex); // dwCaps
        AppendU32LE(out, kDdscaps2Cubemap | kDdscaps2AllFaces); // dwCaps2
        AppendU32LE(out, 0); // dwCaps3
        AppendU32LE(out, 0); // dwCaps4
        AppendU32LE(out, 0); // reserved2

        // Face order matches DDSFromStreamEXT's own `static_cast<CubeMapFace>(face)` loop
        // (0=PositiveX, 1=NegativeX, 2=PositiveY, 3=NegativeY, 4=PositiveZ, 5=NegativeZ).
        AppendSolidDxt1Block(out, 255, 0, 0);     // +X Red
        AppendSolidDxt1Block(out, 0, 255, 255);   // -X Cyan
        AppendSolidDxt1Block(out, 0, 255, 0);     // +Y Green
        AppendSolidDxt1Block(out, 255, 0, 255);   // -Y Magenta
        AppendSolidDxt1Block(out, 0, 0, 255);     // +Z Blue
        AppendSolidDxt1Block(out, 255, 255, 0);   // -Z Yellow

        return out;
    }
}

class TextureCubeContentLoadTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    static bool closeTo(const Color& c, int r, int g, int b, int tol = 40)
    {
        return std::abs(static_cast<int>(c.getRProperty()) - r) <= tol
            && std::abs(static_cast<int>(c.getGProperty()) - g) <= tol
            && std::abs(static_cast<int>(c.getBProperty()) - b) <= tol;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_texturecube_content_load_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        const auto ddsBytes = BuildDxt1Cubemap();
        std::ofstream f(root / "cube.dds", std::ios::binary);
        f.write(reinterpret_cast<const char*>(ddsBytes.data()),
                static_cast<std::streamsize>(ddsBytes.size()));
        f.close();

        getContentProperty().setRootDirectoryProperty(root.string());

        TextureCube cube = getContentProperty().Load<TextureCube>("cube");

        check(cube.getSizeProperty() == 4, "Loaded cube face size == 4 (matches encoded DDS)");

        struct FaceCheck { CubeMapFace face; int r, g, b; const char* label; };
        const FaceCheck checks[] = {
            { CubeMapFace::PositiveX, 255, 0,   0,   "+X face decodes to Red" },
            { CubeMapFace::NegativeX, 0,   255, 255, "-X face decodes to Cyan" },
            { CubeMapFace::PositiveY, 0,   255, 0,   "+Y face decodes to Green" },
            { CubeMapFace::NegativeY, 255, 0,   255, "-Y face decodes to Magenta" },
            { CubeMapFace::PositiveZ, 0,   0,   255, "+Z face decodes to Blue" },
            { CubeMapFace::NegativeZ, 255, 255, 0,   "-Z face decodes to Yellow" },
        };

        for (const auto& c : checks) {
            std::vector<Color> data(16, Color(0, 0, 0, 0));
            cube.GetData(c.face, data.data(), 16);
            bool allMatch = true;
            for (const auto& px : data)
                if (!closeTo(px, c.r, c.g, c.b)) allMatch = false;
            check(allMatch, c.label);
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    TextureCubeContentLoadTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    TextureCubeContentLoadTest game;
    game.Run();
    return game.getResult();
}
