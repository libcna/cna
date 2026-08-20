// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/DisplayColorSpace.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Encodes a scene-referred frame for whatever the display actually is.
     *
     * plan_modern.md `MOD-2092`. The last step before presenting, and the one that decides what the
     * numbers in the frame *mean*. In `Srgb` it **copies through, pixel for pixel** -- SDR output is
     * unchanged, because `TonemapPass` has already produced display-encoded sRGB and a second
     * transfer function applied to it would be visibly wrong. In an HDR space it takes the
     * scene-referred frame instead and encodes it directly, which is what "tonemapping bypassed or
     * retargeted" means: the SDR curve exists to fit a scene into 0..1, and an HDR display does not
     * need it.
     *
     * **Nothing here turns an SDR display into an HDR one.** No CNA platform back end offers an HDR
     * swap chain yet, so `GraphicsDevice::GetDisplayColorSpaceEXT()` answers `Srgb` everywhere and
     * this pass copies through unless a caller deliberately asks for another space -- which is
     * useful for producing an HDR image to a file or a texture, and is how the encoding is tested.
     * Setting an HDR space here does not reconfigure the swap chain and does not claim to.
     *
     * The two HDR encodings differ in what a value means:
     * - `Scrgb` is linear Rec. 709 where 1.0 is 80 nits, so the whole frame is simply scaled.
     * - `Hdr10` is Rec. 2020 primaries and the ST 2084 (PQ) curve, where the encoded value is an
     *   **absolute** luminance in nits. That is why @ref setPaperWhiteNits exists: the pass has to
     *   be told what "white" is worth in the real world before it can encode anything.
     */
    class HdrDisplayOutput
    {
    public:
        /** @brief The nits a scene-referred value of 1.0 is worth unless told otherwise. */
        static constexpr float kDefaultPaperWhiteNits = 200.0f;

        /** @brief The brightest luminance the pass will emit unless told otherwise. */
        static constexpr float kDefaultPeakNits = 1000.0f;

        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit HdrDisplayOutput(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~HdrDisplayOutput();

        HdrDisplayOutput(const HdrDisplayOutput&)            = delete;
        HdrDisplayOutput& operator=(const HdrDisplayOutput&) = delete;

        /** @brief Returns whether this renderer can run the pass. */
        [[nodiscard]] bool isSupported() const;

        /** @brief Returns the space the pass encodes for. */
        [[nodiscard]] CNA::DisplayColorSpace getColorSpace() const;
        /**
         * @brief Sets the space the pass encodes for.
         *
         * Independent of the swap chain: this says what the pass writes, not what the display
         * receives. Ask `GraphicsDevice::GetDisplayColorSpaceEXT()` for the latter.
         *
         * @param value The space.
         */
        void setColorSpace(CNA::DisplayColorSpace value);

        /** @brief Returns what a scene-referred value of 1.0 is worth, in nits. */
        [[nodiscard]] float getPaperWhiteNits() const;
        /**
         * @brief Sets what a scene-referred value of 1.0 is worth, in nits.
         *
         * @param value The luminance of diffuse white; clamped to at least 1.
         */
        void setPaperWhiteNits(float value);

        /** @brief Returns the brightest luminance the pass emits, in nits. */
        [[nodiscard]] float getPeakNits() const;
        /**
         * @brief Sets the brightest luminance the pass emits, in nits.
         *
         * Highlights are rolled off towards this rather than clipped at it, so a value above the
         * display's capability desaturates rather than turning into a flat white shape.
         *
         * @param value The peak; clamped to at least the paper white.
         */
        void setPeakNits(float value);

        /**
         * @brief Encodes @p source into @p destination.
         *
         * @param source      The frame to encode.
         * @param destination Where to write, or null for the back buffer.
         * @param width       The destination width in pixels.
         * @param height      The destination height in pixels.
         * @throws std::invalid_argument If @p source is null or a dimension is not positive.
         */
        void draw(Microsoft::Xna::Framework::Graphics::Texture2D* source,
                  Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination,
                  int width, int height);

        /**
         * @brief Encodes one absolute luminance with the ST 2084 (PQ) transfer function.
         *
         * @param nits The luminance, from 0 to 10000.
         * @return The encoded value, from 0 to 1.
         */
        [[nodiscard]] static float encodePq(float nits);

        /**
         * @brief The inverse of @ref encodePq.
         *
         * @param encoded A value from 0 to 1.
         * @return The luminance it represents, in nits.
         */
        [[nodiscard]] static float decodePq(float encoded);

        /**
         * @brief Converts a colour from Rec. 709 primaries to Rec. 2020's.
         *
         * Both are D65, so white stays white; what changes is how saturated colours are expressed.
         *
         * @param color The colour in Rec. 709.
         * @return The same colour in Rec. 2020.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 rec709ToRec2020(
            const Microsoft::Xna::Framework::Vector3& color);

        /**
         * @brief Rolls a luminance off towards a peak instead of clipping it there.
         *
         * @param nits The luminance.
         * @param peakNits The brightest the display can show.
         * @return A luminance that approaches but never reaches @p peakNits.
         */
        [[nodiscard]] static float rollOff(float nits, float peakNits);

        /**
         * @brief The CPU half of the shader: what one pixel becomes in a given space.
         *
         * Public because a comparison that can only be made through a whole frame is not a
         * comparison anyone runs.
         *
         * @param space          The space to encode for.
         * @param sceneLinear    The scene-referred colour, where 1.0 is diffuse white.
         * @param paperWhiteNits What that 1.0 is worth in nits.
         * @param peakNits       The brightest luminance to emit.
         * @return The encoded colour.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 encode(
            CNA::DisplayColorSpace space, const Microsoft::Xna::Framework::Vector3& sceneLinear,
            float paperWhiteNits, float peakNits);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        CNA::DisplayColorSpace space_ = CNA::DisplayColorSpace::Srgb;
        float paperWhiteNits_ = kDefaultPaperWhiteNits;
        float peakNits_       = kDefaultPeakNits;
        bool  supported_      = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
