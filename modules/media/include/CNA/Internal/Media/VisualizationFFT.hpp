// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstddef>

namespace CNA::Internal::Media
{
    /// Minimal from-scratch radix-2 FFT used to turn captured PCM into the frequency-domain half
    /// of Microsoft::Xna::Framework::Media::VisualizationData (plans/plan_media.md MEDIA-187).
    ///
    /// Deliberately dependency-free: XNA exposes only 256 frequency bins for visualization, so a
    /// textbook 512-point radix-2 transform is entirely sufficient and pulling in a third-party
    /// FFT library for it would be disproportionate.
    class VisualizationFFT
    {
    public:
        /// Number of real input samples consumed per transform.
        static constexpr std::size_t InputSize = 512;

        /// Number of magnitude bins produced (InputSize / 2), matching VisualizationData::Size.
        static constexpr std::size_t BinCount = InputSize / 2;

        /// Computes normalized magnitude spectrum of `input`.
        ///
        /// A Hann window is applied first to reduce spectral leakage, then magnitudes are scaled
        /// by 2/InputSize so a full-scale sine wave maps to approximately 1.0 in its own bin.
        /// XNA does not document its own normalization, so this scale is a CNA choice recorded in
        /// CHECKLIST.md rather than a claim of bit-exact XNA parity.
        ///
        /// @param input  InputSize real samples, nominally in [-1, 1].
        /// @param output BinCount magnitudes, index i covering frequency i * sampleRate/InputSize.
        static void ComputeMagnitudes(const std::array<float, InputSize>& input,
                                      std::array<float, BinCount>& output);
    };
}
