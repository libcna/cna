// SPDX-License-Identifier: MS-PL
// 2026-10-26 dependency audit, FNA3D B2-01/B2-02/B2-03: a render target's GetData writes exactly
// the bytes its format and rectangle occupy, tightly packed, at every mip level and on every cube
// face -- and not one byte more.
//
// FNA3D's OpenGL driver used to read back as RGBA8 whatever the format (four bytes per texel into
// a two-byte HalfSingle array), left GL_PACK_ALIGNMENT at four (an odd-width one- or two-byte
// format's rows padded past a tight buffer) and computed mip sizes without the clamp to one texel
// (level 2 of an 8x2 target came out 2x0, a zero-byte temporary buffer for a 2x1 read). Those were
// writes past the destination. Every read here goes into a buffer with guard bytes behind the
// exact length and checks that they are untouched. That guards what CNA copies into the caller's
// array; a renderer reads into CNA's own exactly sized scratch first, whose bounds the forks'
// tests/cna suites and AddressSanitizer runs check.
//
// Render targets, because their content lives only on the GPU: a plain Texture2D's GetData is
// served from CNA's own upload copy and would never reach a renderer's readback. A target cleared
// to one colour holds one texel value everywhere, so a read whose rows came back displaced or
// padded shows up as texels that differ from their neighbours, without the test having to know
// how each renderer quantizes the colour into each format; for Color, whose quantization XNA fixes,
// the texels must also hold the clear colour itself, and each cube face is cleared to its own
// colour so that a read of the wrong face shows. The destination starts out filled with the guard
// byte too, and no clear colour here encodes to it, so a read that wrote nothing fails instead of
// passing as uniform -- and, because CNA hands a renderer its own zero-filled scratch, every
// payload must hold something other than zeros too, which no clear colour here encodes to.

#include <gtest/gtest.h>

#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;

namespace
{
    constexpr std::uint8_t kGuard = 0xA5;
    constexpr std::size_t kGuardBytes = 64;

    /// Every uncompressed SurfaceFormat XNA allows a HiDef render target to prefer, one- to
    /// sixteen-byte texels: the narrow ones are the formats the old readback overran.
    const SurfaceFormat kFormats[] = {
        SurfaceFormat::Color,         SurfaceFormat::Bgr565,      SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,      SurfaceFormat::Alpha8,      SurfaceFormat::Single,
        SurfaceFormat::HalfSingle,    SurfaceFormat::HalfVector2, SurfaceFormat::Vector2,
        SurfaceFormat::HalfVector4,   SurfaceFormat::Vector4,     SurfaceFormat::HdrBlendable,
        SurfaceFormat::Rg32,          SurfaceFormat::Rgba64,      SurfaceFormat::Rgba1010102,
    };

    std::string FormatName(SurfaceFormat format)
    {
        return "SurfaceFormat " + std::to_string(static_cast<int>(format));
    }

    /// A read of @p bytes into a guarded buffer; returns the payload, checks the guard and that the
    /// read wrote the payload at all.
    template <typename Read>
    std::vector<std::uint8_t> GuardedRead(std::size_t bytes, const std::string& where, Read read)
    {
        std::vector<std::uint8_t> buffer(bytes + kGuardBytes, kGuard);
        try
        {
            read(buffer.data(), static_cast<int>(bytes));
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << where << ": GetData threw: " << error.what();
            return {};
        }
        for (std::size_t i = bytes; i < buffer.size(); ++i)
        {
            if (buffer[i] != kGuard)
            {
                ADD_FAILURE() << where << ": GetData wrote past the " << bytes
                              << "-byte destination (byte " << i << " changed)";
                break;
            }
        }
        buffer.resize(bytes);
        bool untouched = true;
        bool zero = true;
        for (const std::uint8_t byte : buffer)
        {
            untouched = untouched && byte == kGuard;
            zero = zero && byte == 0;
        }
        if (untouched)
            ADD_FAILURE() << where << ": GetData left all " << bytes << " destination bytes unwritten";
        else if (zero)
            ADD_FAILURE() << where << ": GetData returned only zeros, which no clear here encodes to";
        return buffer;
    }

    /// Every texel of a uniformly cleared surface holds the same bytes.
    void ExpectUniform(const std::vector<std::uint8_t>& texels, std::size_t texelBytes,
                       const std::string& where)
    {
        ASSERT_EQ(texels.size() % texelBytes, 0u) << where;
        for (std::size_t offset = texelBytes; offset < texels.size(); offset += texelBytes)
        {
            if (std::memcmp(texels.data(), texels.data() + offset, texelBytes) != 0)
            {
                ADD_FAILURE() << where << ": texel " << (offset / texelBytes)
                              << " differs from texel 0 (displaced or padded rows)";
                return;
            }
        }
    }

    /// A Color texel read back after a clear to (r, g, b, 1): each channel within rounding.
    void ExpectColor(const std::vector<std::uint8_t>& texels, float r, float g, float b,
                     const std::string& where)
    {
        if (texels.size() < 4) return;
        const float expected[4] = {r * 255.0f, g * 255.0f, b * 255.0f, 255.0f};
        for (int channel = 0; channel < 4; ++channel)
        {
            EXPECT_NEAR(static_cast<float>(texels[static_cast<std::size_t>(channel)]),
                        expected[channel], 1.0f)
                << where << ": channel " << channel << " is not the clear colour";
        }
    }

    int LevelExtent(int base, int level)
    {
        for (int i = 0; i < level; ++i) base = base > 1 ? base / 2 : 1;
        return base;
    }

    /// The format a target of @p format actually got (XNA's preferred-format fallback may pick
    /// another one), or nothing when this renderer cannot read targets back.
    bool UsableTargetFormat(GraphicsDevice& device, SurfaceFormat format)
    {
        return device.SupportsSurfaceFormatAsRenderTargetEXT(format);
    }

    void ClearTarget(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(&target);
        device.Clear(0.75f, 0.5f, 0.25f, 1.0f);
        device.SetRenderTarget(nullptr);
    }

    /// Whether this renderer declares render-target readback. What it declares decides, never a
    /// failed read: that would turn a regression into a skip.
    bool ReadsTargetsBack(GraphicsDevice& device)
    {
        return device.GetRendererFeatureSupportEXT(CNA::RendererFeature::RenderTargetReadback) ==
               CNA::RendererFeatureSupport::Supported;
    }
}

// Odd widths with one- and two-byte texels are where a four-byte row alignment pads each row.
TEST(RenderTargetReadbackLayoutTest, OddWidthTargetsReadBackTightlyInEveryFormat)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!ReadsTargetsBack(device))
        GTEST_SKIP() << "this renderer cannot read render targets back";

    int checked = 0;
    for (const SurfaceFormat requested : kFormats)
    {
        if (!UsableTargetFormat(device, requested)) continue;
        for (const auto [width, height] : {std::pair{3, 2}, std::pair{5, 3}, std::pair{7, 1}})
        {
            RenderTarget2D target(device, width, height, false, requested, DepthFormat::None);
            const SurfaceFormat format = target.getFormatProperty();
            const std::size_t texelBytes = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
            const std::string where = FormatName(format) + " " + std::to_string(width) + "x" +
                                      std::to_string(height);
            ClearTarget(device, target);

            const std::size_t whole = texelBytes * static_cast<std::size_t>(width * height);
            const auto all = GuardedRead(whole, where + " whole", [&](std::uint8_t* data, int n) {
                target.GetData(data, n);
            });
            ExpectUniform(all, texelBytes, where + " whole");
            if (format == SurfaceFormat::Color) ExpectColor(all, 0.75f, 0.5f, 0.25f, where + " whole");

            // A rectangle off the origin, one texel narrower and shorter where the size allows.
            const Rectangle rect(width > 1 ? 1 : 0, height > 1 ? 1 : 0,
                                 width > 1 ? width - 1 : 1, height > 1 ? height - 1 : 1);
            const std::size_t part = texelBytes * static_cast<std::size_t>(rect.Width * rect.Height);
            const auto sub = GuardedRead(part, where + " rect", [&](std::uint8_t* data, int n) {
                target.GetData(0, &rect, data, 0, n);
            });
            ExpectUniform(sub, texelBytes, where + " rect");
            if (!sub.empty() && !all.empty())
                EXPECT_EQ(std::memcmp(sub.data(), all.data(), texelBytes), 0) << where;
            ++checked;
        }
    }
    EXPECT_GT(checked, 0) << "no render-target format could be read back";
}

// A non-square chain clamps each side to one texel on its own: 8x2 is 8x2, 4x1, 2x1, 1x1.
TEST(RenderTargetReadbackLayoutTest, NonSquareMipLevelsKeepAtLeastOneTexelPerSide)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!ReadsTargetsBack(device))
        GTEST_SKIP() << "this renderer cannot read render targets back";

    int checked = 0;
    for (const SurfaceFormat requested : {SurfaceFormat::Color, SurfaceFormat::HalfSingle,
                                          SurfaceFormat::Bgr565, SurfaceFormat::Vector4})
    {
        if (!UsableTargetFormat(device, requested)) continue;
        for (const auto [width, height] : {std::pair{8, 2}, std::pair{2, 8}})
        {
            ++checked;
            RenderTarget2D target(device, width, height, true, requested, DepthFormat::None);
            const SurfaceFormat format = target.getFormatProperty();
            const std::size_t texelBytes = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
            ClearTarget(device, target);
            const int levels = target.getLevelCountProperty();
            EXPECT_EQ(levels, 4) << FormatName(format);
            for (int level = 0; level < levels; ++level)
            {
                const int w = LevelExtent(width, level);
                const int h = LevelExtent(height, level);
                const std::string where = FormatName(format) + " " + std::to_string(width) + "x" +
                                          std::to_string(height) + " level " +
                                          std::to_string(level) + " (" + std::to_string(w) + "x" +
                                          std::to_string(h) + ")";
                const Rectangle rect(0, 0, w, h);
                const auto texels = GuardedRead(
                    texelBytes * static_cast<std::size_t>(w * h), where,
                    [&](std::uint8_t* data, int n) { target.GetData(level, &rect, data, 0, n); });
                ExpectUniform(texels, texelBytes, where);
                if (format == SurfaceFormat::Color) ExpectColor(texels, 0.75f, 0.5f, 0.25f, where);
                if (w > 1 || h > 1)
                {
                    // ...and a one-texel rectangle at the far corner of the level.
                    const Rectangle corner(w - 1, h - 1, 1, 1);
                    GuardedRead(texelBytes, where + " corner", [&](std::uint8_t* data, int n) {
                        target.GetData(level, &corner, data, 0, n);
                    });
                }
            }
        }
    }
    EXPECT_GT(checked, 0) << "no mipmapped render-target format could be read back";
}

// Each face is read on its own and fits its own destination, odd sizes included.
TEST(RenderTargetReadbackLayoutTest, CubeFacesReadBackTightly)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!ReadsTargetsBack(device))
        GTEST_SKIP() << "this renderer cannot read render targets back";

    int checked = 0;
    for (const SurfaceFormat requested : {SurfaceFormat::Color, SurfaceFormat::HalfSingle,
                                          SurfaceFormat::Bgra4444, SurfaceFormat::Vector4})
    {
        if (!UsableTargetFormat(device, requested)) continue;
        for (const int size : {3, 5})
        {
            std::unique_ptr<RenderTargetCube> target;
            try
            {
                target = std::make_unique<RenderTargetCube>(device, size, false, requested,
                                                            DepthFormat::None);
            }
            catch (const std::exception& error)
            {
                // A renderer may refuse a cube target of a narrower format; that is not a
                // readback fault. HiDef guarantees a Color one.
                if (requested == SurfaceFormat::Color)
                    ADD_FAILURE() << "Color cube " << size << " refused: " << error.what();
                else
                    GTEST_LOG_(INFO) << FormatName(requested) << " cube refused: " << error.what();
                continue;
            }
            ++checked;
            const SurfaceFormat format = target->getFormatProperty();
            const std::size_t texelBytes = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
            // Face f is cleared to ((f + 1)/8, 0.5, 1 - f/8): six distinct colours, none of
            // them zero in any channel a one-channel format keeps.
            const auto faceRed = [](int f) { return static_cast<float>(f + 1) / 8.0f; };
            const auto faceBlue = [](int f) { return 1.0f - static_cast<float>(f) / 8.0f; };
            for (int f = 0; f < 6; ++f)
            {
                const auto face = static_cast<CubeMapFace>(f);
                device.SetRenderTarget(target.get(), face);
                device.Clear(faceRed(f), 0.5f, faceBlue(f), 1.0f);
            }
            device.SetRenderTarget(nullptr);
            for (int f = 0; f < 6; ++f)
            {
                const auto face = static_cast<CubeMapFace>(f);
                const std::string where = FormatName(format) + " cube " + std::to_string(size) +
                                          " face " + std::to_string(f);
                const auto texels = GuardedRead(
                    texelBytes * static_cast<std::size_t>(size * size), where,
                    [&](std::uint8_t* data, int n) { target->GetData(face, data, n); });
                ExpectUniform(texels, texelBytes, where);
                if (format == SurfaceFormat::Color)
                    ExpectColor(texels, faceRed(f), 0.5f, faceBlue(f), where);
                const Rectangle rect(1, 1, size - 1, size - 2);
                const auto sub = GuardedRead(
                    texelBytes * static_cast<std::size_t>(rect.Width * rect.Height),
                    where + " rect", [&](std::uint8_t* data, int n) {
                        target->GetData(face, 0, &rect, data, 0, n);
                    });
                ExpectUniform(sub, texelBytes, where + " rect");
                if (format == SurfaceFormat::Color)
                    ExpectColor(sub, faceRed(f), 0.5f, faceBlue(f), where + " rect");
            }
        }
    }
    EXPECT_GT(checked, 0) << "no cube render target could be read back";
}

// Reading the same target again returns the same bytes and stays inside its buffer.
TEST(RenderTargetReadbackLayoutTest, RepeatedReadsAreStable)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!ReadsTargetsBack(device))
        GTEST_SKIP() << "this renderer cannot read render targets back";
    if (!UsableTargetFormat(device, SurfaceFormat::HalfSingle))
        GTEST_SKIP() << "no HalfSingle render targets";

    RenderTarget2D target(device, 5, 3, false, SurfaceFormat::HalfSingle, DepthFormat::None);
    ClearTarget(device, target);
    const std::size_t bytes = static_cast<std::size_t>(
        Texture::GetFormatSizeEXT(target.getFormatProperty()) * 5 * 3);
    const auto first = GuardedRead(bytes, "read 1", [&](std::uint8_t* d, int n) {
        target.GetData(d, n);
    });
    for (int i = 2; i <= 8; ++i)
    {
        const auto again = GuardedRead(bytes, "read " + std::to_string(i),
                                       [&](std::uint8_t* d, int n) { target.GetData(d, n); });
        EXPECT_EQ(again, first) << "read " << i;
    }
}
