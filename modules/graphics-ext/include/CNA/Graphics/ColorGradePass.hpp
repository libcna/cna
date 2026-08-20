// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/LutInterpolation.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
    class Texture2D;
    class Texture3D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Replaces every colour in the frame with the one a lookup table names for it.
     *
     * This is the grade, and it is the last word on how the image looks: a colourist's decisions
     * arrive as a table rather than as a list of settings, so anything a table can express -- a
     * curve, a tint, a bleach bypass, a whole film emulation -- costs the same single lookup.
     *
     * The table is a **3D LUT stored as a 2D strip**: `size` slices, each `size` by `size`, laid
     * left to right, so a 32-entry table is a 1024 by 32 texture. That layout is what the format
     * every grading tool exports converts to, and it is the only one a renderer without 3D textures
     * can sample.
     *
     * **It runs on displayed pixels**, after tonemapping. A grade applied to scene-referred values
     * would be indexing a table by numbers that run past 1.0, where a table has nothing to say.
     */
    class ColorGradePass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit ColorGradePass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~ColorGradePass() override;

        /**
         * @brief Grades @ref PostProcessContext::source into the destination.
         *
         * With no table set, the source is copied through unchanged.
         *
         * @param context The images and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"ColorGrade"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when the renderer executes shader source and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the lookup table, or null when none is set. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getLut() const;

        /**
         * @brief Sets the lookup table, borrowed rather than owned.
         *
         * @param lut A strip of `size` slices of `size` by `size`, or null to disable the grade.
         * @throws std::invalid_argument When the texture's dimensions do not form such a strip.
         */
        void setLut(Microsoft::Xna::Framework::Graphics::Texture2D* lut);

        /** @brief Returns the volume lookup table, or null when none is set. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture3D* getVolumeLut() const;

        /**
         * @brief Sets a real volume lookup table, borrowed rather than owned.
         *
         * plans/plan_modern.md `MOD-2130`. The same table the strip carries, in the layout the hardware
         * understands: one fetch replaces the strip's two fetches and the hand-written blend
         * between slices, and there is no half-texel arithmetic to get wrong because there are no
         * slice boundaries to stay off. It is not the default because it needs
         * `GraphicsCapability::Texture3D`, which the strip does not.
         *
         * A volume table takes precedence over a strip while both are set.
         *
         * @param lut A cubic volume texture, or null to stop using one.
         * @throws EngineException When the renderer has no `GraphicsCapability::Texture3D`.
         * @throws std::invalid_argument When the texture is not a cube of an accepted size.
         */
        void setVolumeLut(Microsoft::Xna::Framework::Graphics::Texture3D* lut);

        /** @brief Returns how colours between the table's entries are worked out. */
        [[nodiscard]] LutInterpolation getInterpolation() const;

        /**
         * @brief Sets how colours between the table's entries are worked out.
         *
         * plans/plan_modern.md `MOD-2131`. See @ref LutInterpolation for what separates them; the short
         * version is that @ref LutInterpolation::Tetrahedral keeps neutrals neutral and
         * @ref LutInterpolation::Trilinear does not.
         *
         * @param value The interpolation to use.
         */
        void setInterpolation(LutInterpolation value);

        /** @brief Returns how strongly the graded colour replaces the original. */
        [[nodiscard]] float getStrength() const;
        /**
         * @brief Sets how strongly the graded colour replaces the original.
         *
         * @param value 0 leaves the frame untouched, 1 is the full grade. Clamped to [0, 1].
         */
        void setStrength(float value);

        /**
         * @brief The slice count a strip of these dimensions describes, or 0 if it describes none.
         *
         * A valid strip is `size * size` wide and `size` tall. Exposed so a caller can check a
         * texture before setting it, and so the rule has one statement rather than two.
         *
         * @param width  The strip's width in pixels.
         * @param height The strip's height in pixels.
         * @return The slice count, or 0 when the dimensions are not a strip.
         */
        [[nodiscard]] static int lutSizeForStrip(int width, int height);

        /**
         * @brief Builds a table that changes nothing, for a caller to start from.
         *
         * @param device The device to create the texture on.
         * @param size   The slice count; must be between 2 and @ref kMaxLutSize.
         * @return The identity table as a strip.
         * @throws std::invalid_argument When the size is outside the accepted range.
         */
        [[nodiscard]] static std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
        createIdentityLut(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int size);

        /** @brief The largest slice count a strip may describe. */
        static constexpr int kMaxLutSize = 64;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> tetrahedralStripEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> volumeEffect_;

        Microsoft::Xna::Framework::Graphics::Texture2D* lut_ = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture3D* volumeLut_ = nullptr;
        int   lutSize_       = 0;
        int   volumeLutSize_ = 0;
        float strength_      = 1.0f;
        LutInterpolation interpolation_ = LutInterpolation::Trilinear;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
