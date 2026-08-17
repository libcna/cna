// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
}

namespace CNA::Graphics {

    /**
     * @brief Smooths aliased edges after tonemapping, without multisampling.
     *
     * A luminance-based edge filter, reimplemented here rather than ported: it finds the local
     * contrast around a pixel, decides whether that pixel sits on an edge, and blends along the
     * edge's direction. It costs one fullscreen pass and no extra memory, which is what makes it
     * the practical choice where MSAA is unavailable -- notably on a float render target, where
     * multisampling is far less widely supported than on the back buffer.
     *
     * It runs after tonemapping, on displayed pixels. Edge detection on scene-referred values
     * would treat a highlight ten times brighter than white as an enormous edge and blur it into
     * its surroundings.
     */
    class FxaaPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit FxaaPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~FxaaPass() override;

        /**
         * @brief Anti-aliases @ref PostProcessContext::source into the destination.
         *
         * @param context The images and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"FXAA"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the minimum local contrast that counts as an edge. */
        [[nodiscard]] float getEdgeThreshold() const;
        /**
         * @brief Sets the minimum local contrast that counts as an edge.
         *
         * Lower values smooth more and soften texture detail with it; the default (0.125) is the
         * usual compromise.
         */
        void setEdgeThreshold(float value);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float edgeThreshold_ = 0.125f;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
