// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/VisualizationFFT.hpp"

#include <cmath>
#include <complex>
#include <numbers>

namespace CNA::Internal::Media
{
    namespace
    {
        // In-place iterative radix-2 Cooley-Tukey FFT. `data` length must be a power of two.
        void FftInPlace(std::array<std::complex<float>, VisualizationFFT::InputSize>& data)
        {
            constexpr std::size_t n = VisualizationFFT::InputSize;

            // Bit-reversal permutation.
            for (std::size_t i = 1, j = 0; i < n; ++i)
            {
                std::size_t bit = n >> 1;
                for (; j & bit; bit >>= 1)
                {
                    j ^= bit;
                }
                j ^= bit;
                if (i < j)
                {
                    std::swap(data[i], data[j]);
                }
            }

            // Butterfly stages.
            for (std::size_t len = 2; len <= n; len <<= 1)
            {
                const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
                const std::complex<float> wlen(std::cos(angle), std::sin(angle));
                for (std::size_t i = 0; i < n; i += len)
                {
                    std::complex<float> w(1.0f, 0.0f);
                    for (std::size_t k = 0; k < len / 2; ++k)
                    {
                        const std::complex<float> u = data[i + k];
                        const std::complex<float> v = data[i + k + len / 2] * w;
                        data[i + k]             = u + v;
                        data[i + k + len / 2]   = u - v;
                        w *= wlen;
                    }
                }
            }
        }
    }

    void VisualizationFFT::ComputeMagnitudes(const std::array<float, InputSize>& input,
                                             std::array<float, BinCount>& output)
    {
        std::array<std::complex<float>, InputSize> buffer{};

        // Hann window: 0.5 * (1 - cos(2*pi*i/(N-1))). Without it, a tone that does not land
        // exactly on a bin centre smears energy across the whole spectrum.
        for (std::size_t i = 0; i < InputSize; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(InputSize - 1);
            const float window = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * t));
            buffer[i] = std::complex<float>(input[i] * window, 0.0f);
        }

        FftInPlace(buffer);

        // Only the first half is meaningful for a real input (the second half mirrors it).
        // Scale by 2/N so a full-scale sine reads ~1.0 in its bin; the Hann window's own 0.5
        // coherent gain is deliberately left uncompensated -- documented, not accidental.
        constexpr float scale = 2.0f / static_cast<float>(InputSize);
        for (std::size_t i = 0; i < BinCount; ++i)
        {
            output[i] = std::abs(buffer[i]) * scale;
        }
    }
}
