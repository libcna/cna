// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief GLSL-source-based effect loaded from vertex and fragment shader strings.
     *
     * @note NOXNA — not part of the XNA 4.0 API. CNA extension.
     */
    NOXNA class ShaderEffect : public Effect
    {
    public:
        /**
         * @brief Constructs a ShaderEffect from GLSL source strings.
         *
         * @param device   GraphicsDevice that owns this effect.
         * @param vertSrc  Contents of the GLSL vertex shader source (not a file path).
         * @param fragSrc  Contents of the GLSL fragment shader source (not a file path).
         */
        NOXNA ShaderEffect(GraphicsDevice& device,
                           const std::string& vertSrc,
                           const std::string& fragSrc);

        /// Gets the GLSL vertex shader source string.
        NOXNA const std::string& getVertexSourceProperty() const;
        /// Gets the GLSL fragment shader source string.
        NOXNA const std::string& getFragmentSourceProperty() const;

    protected:
        /// Applies the GLSL shaders to the graphics device.
        void OnApply() override;

    private:
        std::string vertSrc_;
        std::string fragSrc_;
    };
}
