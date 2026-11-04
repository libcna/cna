// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>
#include <string_view>
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Graphics
{
    // XNA ProfileCapabilities.ThrowNotSupportedException IL_0000-002e formats the profile into the
    // FrameworkResources diagnostic before constructing System.NotSupportedException.
    /**
     * @brief Builds the standard XNA profile diagnostic prefix and detail.
     * @param profile Active XNA graphics profile.
     * @param detail Diagnostic text following the profile name.
     * @return Complete English diagnostic.
     */
    [[nodiscard]] std::string ProfileDiagnostic(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile, std::string_view detail);
    /**
     * @brief Gets the XNA SurfaceFormat name used in diagnostics.
     * @param format Surface format value.
     * @return XNA format name or the numeric value for an extension.
     */
    [[nodiscard]] std::string ProfileFormatName(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format);
}
