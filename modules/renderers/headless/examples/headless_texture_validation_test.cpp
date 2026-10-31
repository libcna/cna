// SPDX-License-Identifier: MS-PL
// Direct Headless Texture2D boundary checks. These deliberately bypass GraphicsDevice's public
// validation so malformed renderer inputs cannot hide behind a higher-layer guard.

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Headless;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool ThrowsValidation(const auto& action)
    {
        try
        {
            action();
            return false;
        }
        catch (const HeadlessValidationException&)
        {
            return true;
        }
    }
}

int main()
{
    HeadlessRenderer renderer(64, 64);
    renderer.SetMode(HeadlessMode::Validation);

    ImageData image{4, 2, std::vector<std::uint8_t>(32, 0), 1, 0};
    auto textureBase = renderer.CreateTexture(image);
    auto& texture = dynamic_cast<HeadlessTextureRenderer&>(*textureBase);

    std::vector<std::uint8_t> paddedRows(40, 0xEE);
    for (int i = 0; i < 16; ++i)
    {
        paddedRows[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        paddedRows[20u + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(16 + i);
    }
    texture.UpdatePixels(paddedRows.data(), 20);
    std::vector<std::uint8_t> expected(32);
    for (int i = 0; i < 32; ++i)
        expected[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    Check(texture.Pixels() == expected,
          "UpdatePixels copies format-native rows and skips positive-stride padding");

    const std::vector<std::uint8_t> beforeShortStride = texture.Pixels();
    Check(ThrowsValidation([&] { texture.UpdatePixels(paddedRows.data(), 15); }) &&
              texture.Pixels() == beforeShortStride,
          "UpdatePixels rejects a positive stride shorter than a Color row without modifying storage");
    Check(ThrowsValidation([&] { texture.UpdatePixels(nullptr, 16); }),
          "UpdatePixels rejects a null source");

    const std::size_t resourcesBeforeBadCreate = renderer.AliveResources().size();
    ImageData shortImage{4, 2, std::vector<std::uint8_t>(31, 0), 1, 0};
    Check(ThrowsValidation([&] { (void) renderer.CreateTexture(shortImage); }) &&
              renderer.AliveResources().size() == resourcesBeforeBadCreate,
          "CreateTexture rejects a short level-zero payload without leaking a registry entry");
    ImageData tooManyMips{4, 4, std::vector<std::uint8_t>(64, 0), 4, 0};
    Check(ThrowsValidation([&] { (void) renderer.CreateTexture(tooManyMips); }) &&
              renderer.AliveResources().size() == resourcesBeforeBadCreate,
          "CreateTexture rejects a mip chain longer than its dimensions allow without leaking");

    ImageData mipImage{4, 4, std::vector<std::uint8_t>(64, 0), 3, 0};
    auto mipTextureBase = renderer.CreateTexture(mipImage);
    auto& mipTexture = dynamic_cast<HeadlessTextureRenderer&>(*mipTextureBase);
    const std::vector<std::uint8_t> mipOne(16, 0x5A);
    mipTexture.UpdatePixelsLevel(1, mipOne.data(), 2, 2);
    Check(mipTexture.MipPixels(1) == mipOne,
          "UpdatePixelsLevel stores the exact bytes for a valid nonzero mip");
    Check(ThrowsValidation([&] { mipTexture.UpdatePixelsLevel(3, mipOne.data(), 1, 1); }) &&
              ThrowsValidation([&] { mipTexture.UpdatePixelsLevel(1, mipOne.data(), 4, 4); }) &&
              ThrowsValidation([&] { mipTexture.UpdatePixelsLevel(1, nullptr, 2, 2); }),
          "UpdatePixelsLevel rejects an invalid level, mismatched dimensions and a null source");

    renderer.SetMode(HeadlessMode::Fast);
    const std::vector<std::uint8_t> beforeFastShortStride = texture.Pixels();
    bool fastThrew = false;
    try
    {
        texture.UpdatePixels(paddedRows.data(), 15);
    }
    catch (...)
    {
        fastThrew = true;
    }
    Check(!fastThrew && texture.Pixels() == beforeFastShortStride,
          "Fast mode ignores an unsafe short stride without reading past a row or corrupting storage");

    std::printf("=== %d/%d PASS ===\n", g_passCount, 8);
    return g_passCount == 8 ? 0 : 1;
}
