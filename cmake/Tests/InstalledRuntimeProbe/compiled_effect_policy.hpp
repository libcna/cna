// SPDX-License-Identifier: MS-PL
#pragma once
#include "CompilerProducedEffectPixels.hpp"
#include "compiled_effect_bytes.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "System/NotSupportedException.hpp"
#include <cmath>
#include <iterator>
#include <string>

inline bool InstalledCompiledEffectPolicy(
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
{
    constexpr bool expected = PROBE_EXPECT_COMPILED_EFFECTS;
    if (device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects) != expected)
        return false;
    if (device.GetRendererFeatureSupportEXT(CNA::RendererFeature::CompiledXnaEffects) !=
        (expected ? CNA::RendererFeatureSupport::Supported : CNA::RendererFeatureSupport::Unsupported))
        return false;
    const std::vector<std::uint8_t> bytes(std::begin(CompiledEffectBytes), std::end(CompiledEffectBytes));
    if (!expected)
    {
        try { Microsoft::Xna::Framework::Graphics::Effect effect(device, bytes); }
        catch (const System::NotSupportedException& error)
        {
            return std::string(error.what()).find("GraphicsCapability::CompiledEffects") != std::string::npos;
        }
        return false;
    }
    const auto pixels = CNA::TestSupport::CompilerProducedEffectPixels(device, bytes);
    // CnaConformanceEffect.fx FlatPixelShader = Tint * Weights[1], with Weights[1]=1.
    // Quarter/half/three-quarter UNORM8 values: 63.75,127.5,191.25; tolerance 1 for rounding.
    const Microsoft::Xna::Framework::Color colors[] = {
        Microsoft::Xna::Framework::Color(64, 128, 191, 255),
        Microsoft::Xna::Framework::Color(191, 64, 128, 128)};
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        if (std::abs(int(pixels[i].getRProperty()) - int(colors[i].getRProperty())) > 1 ||
            std::abs(int(pixels[i].getGProperty()) - int(colors[i].getGProperty())) > 1 ||
            std::abs(int(pixels[i].getBProperty()) - int(colors[i].getBProperty())) > 1 ||
            std::abs(int(pixels[i].getAProperty()) - int(colors[i].getAProperty())) > 1)
            return false;
    }
    return true;
}
