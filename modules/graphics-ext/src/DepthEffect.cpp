// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthEffect.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "PostProcessShaderPackages.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <cstdint>
#include <vector>

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace {
    // The 216-colour "web-safe" palette: every combination of 6 levels per channel
    // (0, 51, 102, 153, 204, 255 -- 255/5 = 51 exactly). A real, well-known fixed palette,
    // unlike Color8Bit's independent per-channel rounding to an implicit 8/8/4-level grid.
    std::vector<std::uint8_t> BuildWebSafePalette()
    {
        constexpr std::uint8_t kLevels[6] = {0, 51, 102, 153, 204, 255};
        std::vector<std::uint8_t> pixels;
        pixels.reserve(216 * 4);
        for (std::uint8_t r : kLevels)
            for (std::uint8_t g : kLevels)
                for (std::uint8_t b : kLevels)
                {
                    pixels.push_back(r);
                    pixels.push_back(g);
                    pixels.push_back(b);
                    pixels.push_back(255);
                }
        return pixels;
    }

    // The classic 16-colour EGA/CGA palette (standard IBM values).
    std::vector<std::uint8_t> BuildEgaPalette()
    {
        constexpr std::uint8_t kEga[16][3] = {
            {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
            {170,   0,   0}, {170,   0, 170}, {170,  85,   0}, {170, 170, 170},
            { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
            {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
        };
        std::vector<std::uint8_t> pixels;
        pixels.reserve(16 * 4);
        for (const auto& c : kEga)
        {
            pixels.push_back(c[0]);
            pixels.push_back(c[1]);
            pixels.push_back(c[2]);
            pixels.push_back(255);
        }
        return pixels;
    }

} // namespace

namespace CNA::Graphics {

    DepthEffect::DepthEffect(GraphicsDevice& device)
        : ShaderEffect(
            device, detail::CreateDepthEffectShaderPackage(),
            std::string(detail::PostProcessGenerated::kFullscreenEsVertexSource),
            std::string(detail::PostProcessGenerated::kDepthEffectEsFragmentSource))
    {
    }

    DepthEffectMode DepthEffect::getMode() const { return mode_; }
    void DepthEffect::setMode(DepthEffectMode mode) { mode_ = mode; }

    DitherMode DepthEffect::getDitherMode() const { return ditherMode_; }
    void DepthEffect::setDitherMode(DitherMode mode) { ditherMode_ = mode; }

    const std::string& DepthEffect::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.DepthEffect";
        return name;
    }

    void DepthEffect::ensurePaletteTextures()
    {
        if (paletteTexturesBuilt_ || !IsEffectValid()) return;

        auto& device = getGraphicsDeviceInternal();
        palette256Texture_ = Texture2D::CreateFromPixels(device, 216, 1, BuildWebSafePalette());
        palette16Texture_ = Texture2D::CreateFromPixels(device, 16, 1, BuildEgaPalette());
        paletteTexturesBuilt_ = true;
    }

    void DepthEffect::OnApply()
    {
        ShaderEffect::OnApply();
        const bool isPalette256 = mode_ == DepthEffectMode::Palette256;
        const bool isPalette16 = mode_ == DepthEffectMode::Palette16;
        SetUniformVec4(
            "uDepthParams", static_cast<float>(mode_), static_cast<float>(ditherMode_),
            isPalette256 ? 216.0f : (isPalette16 ? 16.0f : 0.0f), 0.0f);

        if (isPalette256 || isPalette16)
        {
            ensurePaletteTextures();
            if (paletteTexturesBuilt_)
            {
                SetTexture(1, isPalette256 ? palette256Texture_ : palette16Texture_);
                SetUniformInt("uPalette", 1);
            }
        }
    }

    Effect* DepthEffect::Clone()
    {
        auto* clone = new DepthEffect(*device_);
        clone->setMode(mode_);
        clone->setDitherMode(ditherMode_);
        return clone;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
