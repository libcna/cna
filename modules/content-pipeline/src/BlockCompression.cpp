// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-53: an independent BC1/BC2/BC3 encoder.
//
// The design goal that shapes every decision here is that the encoder minimizes the error a
// *decoder* will actually produce, not the error of an idealized interpolation. So the palette
// this file builds uses byte-for-byte the same 565 expansion and the same truncating thirds as
// CNA::Internal::Graphics::DxtUtil, which is both what CNA's own runtime uses on renderers
// without native BC support and what the D3D specification describes.
//
// Everything is integer arithmetic. A content build has to produce the same bytes on every
// machine, and floating point makes that a promise about the compiler's contraction and
// excess-precision behaviour rather than about this code.

#include "CNA/Content/Pipeline/BlockCompression.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <stdexcept>

namespace CNA::Content::Pipeline
{
    namespace
    {
        /** @brief One source texel. */
        struct Texel
        {
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 0;
        };

        /** @brief The 8-bit value a 5-bit 565 component decodes to. */
        [[nodiscard]] constexpr int Expand5(int value)
        {
            const int temp = value * 255 + 16;
            return (temp / 32 + temp) / 32;
        }

        /** @brief The 8-bit value a 6-bit 565 component decodes to. */
        [[nodiscard]] constexpr int Expand6(int value)
        {
            const int temp = value * 255 + 32;
            return (temp / 64 + temp) / 64;
        }

        /** @brief Nearest 5-bit code for an 8-bit component, by decoded distance. */
        [[nodiscard]] constexpr int Quantize5(int value)
        {
            int best = 0;
            int bestError = INT_MAX;
            for (int code = 0; code < 32; ++code)
            {
                const int error = Expand5(code) - value;
                const int squared = error * error;
                if (squared < bestError)
                {
                    bestError = squared;
                    best = code;
                }
            }
            return best;
        }

        /** @brief Nearest 6-bit code for an 8-bit component, by decoded distance. */
        [[nodiscard]] constexpr int Quantize6(int value)
        {
            int best = 0;
            int bestError = INT_MAX;
            for (int code = 0; code < 64; ++code)
            {
                const int error = Expand6(code) - value;
                const int squared = error * error;
                if (squared < bestError)
                {
                    bestError = squared;
                    best = code;
                }
            }
            return best;
        }

        /** @brief Packs an already-clamped RGB triple into a 565 code. */
        [[nodiscard]] int Pack565(const std::array<int, 3>& rgb)
        {
            return (Quantize5(rgb[0]) << 11) | (Quantize6(rgb[1]) << 5) | Quantize5(rgb[2]);
        }

        /** @brief Decodes a 565 code exactly as CNA's runtime decoder does. */
        [[nodiscard]] std::array<int, 3> Unpack565(int code)
        {
            return {Expand5((code >> 11) & 0x1F), Expand6((code >> 5) & 0x3F),
                    Expand5(code & 0x1F)};
        }

        /** @brief How the four colour indices of a block are interpreted. */
        enum class ColorMode
        {
            /** @brief `c0 > c1`: four interpolated colours, no transparency. */
            Four,

            /** @brief `c0 <= c1`: three colours plus a fully transparent index 3. */
            Three
        };

        /** @brief A block's colour palette, index 3 possibly being the transparent one. */
        using Palette = std::array<std::array<int, 3>, 4>;

        /** @brief Builds the palette a decoder will produce for a pair of 565 codes. */
        [[nodiscard]] Palette BuildPalette(int code0, int code1, ColorMode mode)
        {
            const std::array<int, 3> c0 = Unpack565(code0);
            const std::array<int, 3> c1 = Unpack565(code1);
            Palette palette{};
            palette[0] = c0;
            palette[1] = c1;
            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                if (mode == ColorMode::Four)
                {
                    palette[2][channel] = (2 * c0[channel] + c1[channel]) / 3;
                    palette[3][channel] = (c0[channel] + 2 * c1[channel]) / 3;
                }
                else
                {
                    palette[2][channel] = (c0[channel] + c1[channel]) / 2;
                    palette[3][channel] = 0;
                }
            }
            return palette;
        }

        /** @brief Squared RGB distance between a texel and a palette entry. */
        [[nodiscard]] long long ColorError(const Texel& texel, const std::array<int, 3>& color)
        {
            const long long dr = texel.r - color[0];
            const long long dg = texel.g - color[1];
            const long long db = texel.b - color[2];
            return dr * dr + dg * dg + db * db;
        }

        /** @brief The endpoint weight numerator each palette index corresponds to. */
        [[nodiscard]] int EndpointWeight(ColorMode mode, int index)
        {
            if (mode == ColorMode::Four)
            {
                // index 0 is c0, index 1 is c1, index 2 is one third of the way to c1.
                static constexpr std::array<int, 4> kWeights{0, 3, 1, 2};
                return kWeights[static_cast<std::size_t>(index)];
            }
            static constexpr std::array<int, 3> kWeights{0, 2, 1};
            return kWeights[static_cast<std::size_t>(index)];
        }

        /**
         * @brief Rounds a rational to the nearest integer, halves away from zero.
         *
         * Written out rather than using a floating-point division so the encoder's output does
         * not depend on the compiler's rounding mode.
         */
        [[nodiscard]] long long DivideRounded(long long numerator, long long denominator)
        {
            if (denominator < 0)
            {
                numerator = -numerator;
                denominator = -denominator;
            }
            if (numerator >= 0) { return (numerator + denominator / 2) / denominator; }
            return -((-numerator + denominator / 2) / denominator);
        }

        /**
         * @brief Re-fits two endpoints to the indices a previous round chose.
         *
         * Solves the least-squares problem `min over A,B of sum |(1-w)A + wB - c|^2`, where `w`
         * is the palette weight of each texel's chosen index. That is a 2x2 normal system whose
         * coefficients are small integers, so it is solved exactly and rounded once.
         *
         * @return False when every texel chose the same weight, which leaves the endpoints
         *         underdetermined and the caller's current pair as good as any.
         */
        bool RefitEndpoints(const std::array<Texel, 16>& texels,
                            const std::array<bool, 16>& participates,
                            const std::array<int, 16>& indices, ColorMode mode,
                            std::array<int, 3>& endpoint0, std::array<int, 3>& endpoint1)
        {
            const long long divisor = mode == ColorMode::Four ? 3 : 2;
            long long sumAA = 0;
            long long sumAB = 0;
            long long sumBB = 0;
            std::array<long long, 3> sumAX{};
            std::array<long long, 3> sumBX{};
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                if (!participates[texel]) { continue; }
                const int index = indices[texel];
                if (mode == ColorMode::Three && index == 3) { continue; }
                const long long weight = EndpointWeight(mode, index);
                const long long complement = divisor - weight;
                sumAA += complement * complement;
                sumAB += complement * weight;
                sumBB += weight * weight;
                const std::array<int, 3> color{texels[texel].r, texels[texel].g, texels[texel].b};
                for (std::size_t channel = 0; channel < 3u; ++channel)
                {
                    sumAX[channel] += complement * color[channel];
                    sumBX[channel] += weight * color[channel];
                }
            }

            const long long determinant = sumAA * sumBB - sumAB * sumAB;
            if (determinant == 0) { return false; }

            std::array<int, 3> fitted0{};
            std::array<int, 3> fitted1{};
            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                const long long numerator0 =
                    sumBB * sumAX[channel] - sumAB * sumBX[channel];
                const long long numerator1 =
                    sumAA * sumBX[channel] - sumAB * sumAX[channel];
                fitted0[channel] = static_cast<int>(std::clamp<long long>(
                    DivideRounded(divisor * numerator0, determinant), 0, 255));
                fitted1[channel] = static_cast<int>(std::clamp<long long>(
                    DivideRounded(divisor * numerator1, determinant), 0, 255));
            }
            const bool changed = fitted0 != endpoint0 || fitted1 != endpoint1;
            endpoint0 = fitted0;
            endpoint1 = fitted1;
            return changed;
        }

        /** @brief The best colour block found for one 4x4 group of texels. */
        struct ColorFit
        {
            int code0 = 0;
            int code1 = 0;
            std::array<int, 16> indices{};
            long long error = LLONG_MAX;
        };

        /**
         * @brief Refines one starting endpoint pair and returns the best block it reached.
         *
         * Alternates "assign each texel to its nearest palette entry" with "re-fit the endpoints
         * to those assignments". Both steps can only lower the objective, and the best result
         * seen is kept rather than the last one, so more rounds never produce a worse block.
         */
        [[nodiscard]] ColorFit RefineColorBlock(const std::array<Texel, 16>& texels,
                                                const std::array<bool, 16>& participates,
                                                ColorMode mode, std::uint32_t rounds,
                                                std::array<int, 3> endpoint0,
                                                std::array<int, 3> endpoint1)
        {
            ColorFit best;
            for (std::uint32_t round = 0; round <= rounds; ++round)
            {
                const int code0 = Pack565(endpoint0);
                const int code1 = Pack565(endpoint1);
                const Palette palette = BuildPalette(code0, code1, mode);
                const int candidateCount = mode == ColorMode::Four ? 4 : 3;

                std::array<int, 16> indices{};
                long long error = 0;
                for (std::size_t texel = 0; texel < 16u; ++texel)
                {
                    if (!participates[texel])
                    {
                        indices[texel] = 3;
                        continue;
                    }
                    int bestIndex = 0;
                    long long bestError = LLONG_MAX;
                    for (int candidate = 0; candidate < candidateCount; ++candidate)
                    {
                        const long long candidateError =
                            ColorError(texels[texel],
                                       palette[static_cast<std::size_t>(candidate)]);
                        if (candidateError < bestError)
                        {
                            bestError = candidateError;
                            bestIndex = candidate;
                        }
                    }
                    indices[texel] = bestIndex;
                    error += bestError;
                }

                if (error < best.error)
                {
                    best.code0 = code0;
                    best.code1 = code1;
                    best.indices = indices;
                    best.error = error;
                }
                if (error == 0) { break; }
                if (round == rounds) { break; }
                if (!RefitEndpoints(texels, participates, indices, mode, endpoint0, endpoint1))
                {
                    break;
                }
            }
            return best;
        }

        /**
         * @brief Finds the endpoint pair and indices with the lowest decoded error.
         *
         * Two starting guesses are refined and the better one kept, because neither is reliable
         * alone. The colour bounding box is the obvious choice and is usually good, but it fails
         * badly whenever a block's colours run along a diagonal: a block of pure red and pure
         * blue has the bounding box corners magenta and black, which are perpendicular to the
         * data and reproduce neither colour. The most distant pair of texels always spans the
         * data that is actually there, and rescues exactly that case. Refining both costs one
         * extra pass over sixteen texels and removes the worst-case behaviour entirely.
         */
        [[nodiscard]] ColorFit FitColorBlock(const std::array<Texel, 16>& texels,
                                             const std::array<bool, 16>& participates,
                                             ColorMode mode, std::uint32_t rounds)
        {
            std::array<int, 3> low{255, 255, 255};
            std::array<int, 3> high{0, 0, 0};
            bool any = false;
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                if (!participates[texel]) { continue; }
                any = true;
                const std::array<int, 3> color{texels[texel].r, texels[texel].g, texels[texel].b};
                for (std::size_t channel = 0; channel < 3u; ++channel)
                {
                    low[channel] = std::min(low[channel], color[channel]);
                    high[channel] = std::max(high[channel], color[channel]);
                }
            }
            if (!any)
            {
                // Every texel is transparent; the colour endpoints carry no information.
                ColorFit empty;
                empty.error = 0;
                empty.indices.fill(3);
                return empty;
            }

            ColorFit best = RefineColorBlock(texels, participates, mode, rounds, high, low);
            if (best.error == 0) { return best; }

            std::size_t farthest0 = 16u;
            std::size_t farthest1 = 16u;
            long long farthest = -1;
            for (std::size_t first = 0; first < 16u; ++first)
            {
                if (!participates[first]) { continue; }
                for (std::size_t second = first + 1u; second < 16u; ++second)
                {
                    if (!participates[second]) { continue; }
                    const std::array<int, 3> color{texels[second].r, texels[second].g,
                                                   texels[second].b};
                    const long long distance = ColorError(texels[first], color);
                    if (distance > farthest)
                    {
                        farthest = distance;
                        farthest0 = first;
                        farthest1 = second;
                    }
                }
            }
            if (farthest0 == 16u || farthest <= 0) { return best; }

            const ColorFit candidate = RefineColorBlock(
                texels, participates, mode, rounds,
                {texels[farthest0].r, texels[farthest0].g, texels[farthest0].b},
                {texels[farthest1].r, texels[farthest1].g, texels[farthest1].b});
            return candidate.error < best.error ? candidate : best;
        }

        /** @brief Writes a 16-bit value little-endian. */
        void PushUInt16(std::vector<std::uint8_t>& out, int value)
        {
            out.push_back(static_cast<std::uint8_t>(value & 0xFF));
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        }

        /**
         * @brief Emits the eight-byte colour half of a block.
         *
         * @param requireFourColorMode True for BC1 blocks that carry no transparency, and for
         *        the colour half of BC2 and BC3, where a decoder always interpolates four
         *        colours. In that case the encoded codes must satisfy `c0 > c1`, and this
         *        function is what guarantees it -- including for the single-colour block, where
         *        the two codes would otherwise come out equal and silently select the
         *        three-colour mode with its transparent fourth entry.
         */
        void EmitColorBlock(ColorFit fit, bool requireFourColorMode, std::vector<std::uint8_t>& out)
        {
            if (requireFourColorMode)
            {
                if (fit.code0 < fit.code1)
                {
                    std::swap(fit.code0, fit.code1);
                    for (int& index : fit.indices)
                    {
                        index = index == 0 ? 1 : index == 1 ? 0 : index == 2 ? 3 : 2;
                    }
                }
                if (fit.code0 == fit.code1)
                {
                    if (fit.code0 > 0)
                    {
                        // Index 0 selects c0 exactly, so c1 may be any smaller code.
                        fit.code1 = fit.code0 - 1;
                        fit.indices.fill(0);
                    }
                    else
                    {
                        // The colour is 565 code zero and no smaller code exists, so it moves
                        // into c1, which index 1 selects exactly.
                        fit.code0 = 1;
                        fit.code1 = 0;
                        fit.indices.fill(1);
                    }
                }
            }
            else if (fit.code0 > fit.code1)
            {
                std::swap(fit.code0, fit.code1);
                for (int& index : fit.indices)
                {
                    index = index == 0 ? 1 : index == 1 ? 0 : index;
                }
            }

            PushUInt16(out, fit.code0);
            PushUInt16(out, fit.code1);
            std::uint32_t mask = 0;
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                mask |= static_cast<std::uint32_t>(fit.indices[texel] & 0x03)
                        << (2u * static_cast<unsigned>(texel));
            }
            for (int shift = 0; shift < 32; shift += 8)
            {
                out.push_back(static_cast<std::uint8_t>((mask >> shift) & 0xFFu));
            }
        }

        /** @brief The alpha palette a decoder builds for a BC3 endpoint pair. */
        [[nodiscard]] std::array<int, 8> BuildAlphaPalette(int alpha0, int alpha1)
        {
            std::array<int, 8> palette{};
            palette[0] = alpha0;
            palette[1] = alpha1;
            if (alpha0 > alpha1)
            {
                for (int index = 2; index < 8; ++index)
                {
                    palette[static_cast<std::size_t>(index)] =
                        ((8 - index) * alpha0 + (index - 1) * alpha1) / 7;
                }
            }
            else
            {
                for (int index = 2; index < 6; ++index)
                {
                    palette[static_cast<std::size_t>(index)] =
                        ((6 - index) * alpha0 + (index - 1) * alpha1) / 5;
                }
                palette[6] = 0;
                palette[7] = 255;
            }
            return palette;
        }

        /** @brief The best alpha block found for one 4x4 group of texels. */
        struct AlphaFit
        {
            int alpha0 = 0;
            int alpha1 = 0;
            std::array<int, 16> indices{};
            long long error = LLONG_MAX;
        };

        /** @brief Assigns every texel its nearest alpha palette entry and totals the error. */
        [[nodiscard]] AlphaFit AssignAlpha(const std::array<Texel, 16>& texels, int alpha0,
                                           int alpha1)
        {
            const std::array<int, 8> palette = BuildAlphaPalette(alpha0, alpha1);
            AlphaFit fit;
            fit.alpha0 = alpha0;
            fit.alpha1 = alpha1;
            fit.error = 0;
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                int bestIndex = 0;
                long long bestError = LLONG_MAX;
                for (int candidate = 0; candidate < 8; ++candidate)
                {
                    const long long difference =
                        texels[texel].a - palette[static_cast<std::size_t>(candidate)];
                    const long long candidateError = difference * difference;
                    if (candidateError < bestError)
                    {
                        bestError = candidateError;
                        bestIndex = candidate;
                    }
                }
                fit.indices[texel] = bestIndex;
                fit.error += bestError;
            }
            return fit;
        }

        /**
         * @brief Re-fits a BC3 alpha endpoint pair to the indices already chosen.
         *
         * The same least-squares step the colour endpoints take, over a single channel and with
         * the divisor the selected alpha mode uses. Indices 6 and 7 of the six-value mode are
         * the constants 0 and 255 and take no part in the fit.
         */
        bool RefitAlpha(const std::array<Texel, 16>& texels, const AlphaFit& fit, bool eightValue,
                        int& alpha0, int& alpha1)
        {
            const long long divisor = eightValue ? 7 : 5;
            long long sumAA = 0;
            long long sumAB = 0;
            long long sumBB = 0;
            long long sumAX = 0;
            long long sumBX = 0;
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                const int index = fit.indices[texel];
                if (!eightValue && index >= 6) { continue; }
                const long long weight = index == 0 ? 0 : index == 1 ? divisor : index - 1;
                const long long complement = divisor - weight;
                sumAA += complement * complement;
                sumAB += complement * weight;
                sumBB += weight * weight;
                sumAX += complement * texels[texel].a;
                sumBX += weight * texels[texel].a;
            }
            const long long determinant = sumAA * sumBB - sumAB * sumAB;
            if (determinant == 0) { return false; }
            const int fitted0 = static_cast<int>(std::clamp<long long>(
                DivideRounded(divisor * (sumBB * sumAX - sumAB * sumBX), determinant), 0, 255));
            const int fitted1 = static_cast<int>(std::clamp<long long>(
                DivideRounded(divisor * (sumAA * sumBX - sumAB * sumAX), determinant), 0, 255));
            // The mode is chosen by the endpoint ordering, so a re-fit that would flip it has to
            // be rejected: it would silently reinterpret every index in the block.
            if (eightValue && fitted0 <= fitted1) { return false; }
            if (!eightValue && fitted0 > fitted1) { return false; }
            const bool changed = fitted0 != alpha0 || fitted1 != alpha1;
            alpha0 = fitted0;
            alpha1 = fitted1;
            return changed;
        }

        /** @brief Refines one alpha mode from a starting endpoint pair. */
        [[nodiscard]] AlphaFit RefineAlpha(const std::array<Texel, 16>& texels, int alpha0,
                                           int alpha1, bool eightValue, std::uint32_t rounds)
        {
            AlphaFit best;
            for (std::uint32_t round = 0; round <= rounds; ++round)
            {
                const AlphaFit candidate = AssignAlpha(texels, alpha0, alpha1);
                if (candidate.error < best.error) { best = candidate; }
                if (candidate.error == 0 || round == rounds) { break; }
                if (!RefitAlpha(texels, candidate, eightValue, alpha0, alpha1)) { break; }
            }
            return best;
        }

        /** @brief Chooses between BC3's eight-value and six-value alpha modes by measured error. */
        [[nodiscard]] AlphaFit FitAlphaBlock(const std::array<Texel, 16>& texels,
                                             std::uint32_t rounds)
        {
            int lowest = 255;
            int highest = 0;
            int interiorLowest = 255;
            int interiorHighest = 0;
            bool anyInterior = false;
            for (const Texel& texel : texels)
            {
                lowest = std::min(lowest, texel.a);
                highest = std::max(highest, texel.a);
                if (texel.a != 0 && texel.a != 255)
                {
                    anyInterior = true;
                    interiorLowest = std::min(interiorLowest, texel.a);
                    interiorHighest = std::max(interiorHighest, texel.a);
                }
            }

            AlphaFit best;
            if (highest > 0)
            {
                // The eight-value mode needs alpha0 > alpha1; a constant block borrows the code
                // one below it for the unused endpoint.
                const int start1 = highest > lowest ? lowest : highest - 1;
                best = RefineAlpha(texels, highest, start1, true, rounds);
            }

            const int sixLow = anyInterior ? interiorLowest : 0;
            const int sixHigh = anyInterior ? interiorHighest : 0;
            const AlphaFit six = RefineAlpha(texels, sixLow, sixHigh, false, rounds);
            if (six.error < best.error) { best = six; }
            return best;
        }

        /** @brief Emits BC3's two alpha endpoints and its sixteen three-bit indices. */
        void EmitAlphaBlock(const AlphaFit& fit, std::vector<std::uint8_t>& out)
        {
            out.push_back(static_cast<std::uint8_t>(fit.alpha0));
            out.push_back(static_cast<std::uint8_t>(fit.alpha1));
            std::uint64_t mask = 0;
            for (std::size_t texel = 0; texel < 16u; ++texel)
            {
                mask |= static_cast<std::uint64_t>(fit.indices[texel] & 0x07)
                        << (3u * static_cast<unsigned>(texel));
            }
            for (int shift = 0; shift < 48; shift += 8)
            {
                out.push_back(static_cast<std::uint8_t>((mask >> shift) & 0xFFu));
            }
        }

        /** @brief Emits BC2's eight bytes of explicit four-bit alpha. */
        void EmitBc2Alpha(const std::array<Texel, 16>& texels, std::vector<std::uint8_t>& out)
        {
            for (std::size_t pair = 0; pair < 8u; ++pair)
            {
                // A decoder expands a nibble by replicating it, so the nibble nearest to the
                // source byte is the one nearest to a multiple of 17.
                const int low = std::min(15, (texels[pair * 2u].a + 8) / 17);
                const int high = std::min(15, (texels[pair * 2u + 1u].a + 8) / 17);
                out.push_back(static_cast<std::uint8_t>((high << 4) | low));
            }
        }

        /** @brief Gathers one 4x4 block, repeating edge texels for a partial block. */
        [[nodiscard]] std::array<Texel, 16> GatherBlock(std::span<const std::uint8_t> rgba,
                                                        std::uint32_t width, std::uint32_t height,
                                                        std::uint32_t blockX, std::uint32_t blockY)
        {
            std::array<Texel, 16> texels{};
            for (std::uint32_t row = 0; row < 4u; ++row)
            {
                const std::uint32_t y = std::min(blockY * 4u + row, height - 1u);
                for (std::uint32_t column = 0; column < 4u; ++column)
                {
                    const std::uint32_t x = std::min(blockX * 4u + column, width - 1u);
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * width + x) * 4u;
                    Texel& texel = texels[row * 4u + column];
                    texel.r = rgba[offset];
                    texel.g = rgba[offset + 1u];
                    texel.b = rgba[offset + 2u];
                    texel.a = rgba[offset + 3u];
                }
            }
            return texels;
        }
    }

    std::size_t BlockCompressedBlockByteCount(const BlockCompressionFormat format)
    {
        return format == BlockCompressionFormat::Bc1 ? 8u : 16u;
    }

    std::size_t BlockCompressedByteCount(const BlockCompressionFormat format,
                                         const std::uint32_t width, const std::uint32_t height)
    {
        const std::size_t blocksX = (static_cast<std::size_t>(width) + 3u) / 4u;
        const std::size_t blocksY = (static_cast<std::size_t>(height) + 3u) / 4u;
        return blocksX * blocksY * BlockCompressedBlockByteCount(format);
    }

    std::string BlockCompressionFormatName(const BlockCompressionFormat format)
    {
        switch (format)
        {
        case BlockCompressionFormat::Bc1: return "Dxt1";
        case BlockCompressionFormat::Bc2: return "Dxt3";
        case BlockCompressionFormat::Bc3: return "Dxt5";
        }
        return "Unknown";
    }

    std::vector<std::uint8_t> EncodeBlockCompressedImage(const BlockCompressionFormat format,
                                                         std::span<const std::uint8_t> rgba,
                                                         const std::uint32_t width,
                                                         const std::uint32_t height,
                                                         const BlockCompressionOptions& options)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument(
                "EncodeBlockCompressedImage: an image needs a non-zero width and height");
        }
        const std::size_t expected = static_cast<std::size_t>(width) * height * 4u;
        if (rgba.size() != expected)
        {
            throw std::invalid_argument(
                "EncodeBlockCompressedImage: " + std::to_string(width) + "x" +
                std::to_string(height) + " needs exactly " + std::to_string(expected) +
                " RGBA bytes, but " + std::to_string(rgba.size()) + " were supplied");
        }

        const std::uint32_t blocksX = (width + 3u) / 4u;
        const std::uint32_t blocksY = (height + 3u) / 4u;
        std::vector<std::uint8_t> out;
        out.reserve(BlockCompressedByteCount(format, width, height));

        for (std::uint32_t blockY = 0; blockY < blocksY; ++blockY)
        {
            for (std::uint32_t blockX = 0; blockX < blocksX; ++blockX)
            {
                const std::array<Texel, 16> texels =
                    GatherBlock(rgba, width, height, blockX, blockY);

                if (format == BlockCompressionFormat::Bc2)
                {
                    EmitBc2Alpha(texels, out);
                }
                else if (format == BlockCompressionFormat::Bc3)
                {
                    EmitAlphaBlock(FitAlphaBlock(texels, options.refinementRounds), out);
                }

                std::array<bool, 16> participates{};
                bool anyTransparent = false;
                for (std::size_t texel = 0; texel < 16u; ++texel)
                {
                    const bool transparent = format == BlockCompressionFormat::Bc1 &&
                                             texels[texel].a < options.alphaCutoff;
                    participates[texel] = !transparent;
                    anyTransparent = anyTransparent || transparent;
                }

                const ColorMode mode = anyTransparent ? ColorMode::Three : ColorMode::Four;
                const ColorFit fit =
                    FitColorBlock(texels, participates, mode, options.refinementRounds);
                EmitColorBlock(fit, mode == ColorMode::Four, out);
            }
        }
        return out;
    }

    bool ImageHasTransparency(std::span<const std::uint8_t> rgba, const std::uint32_t width,
                              const std::uint32_t height)
    {
        const std::size_t count = static_cast<std::size_t>(width) * height;
        for (std::size_t texel = 0; texel < count && (texel * 4u + 3u) < rgba.size(); ++texel)
        {
            if (rgba[texel * 4u + 3u] != 255u) { return true; }
        }
        return false;
    }

    bool ImageHasPartialTransparency(std::span<const std::uint8_t> rgba, const std::uint32_t width,
                                     const std::uint32_t height)
    {
        const std::size_t count = static_cast<std::size_t>(width) * height;
        for (std::size_t texel = 0; texel < count && (texel * 4u + 3u) < rgba.size(); ++texel)
        {
            const std::uint8_t alpha = rgba[texel * 4u + 3u];
            if (alpha != 0u && alpha != 255u) { return true; }
        }
        return false;
    }
}
