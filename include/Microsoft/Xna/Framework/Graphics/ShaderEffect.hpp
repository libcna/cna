// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <string>
#include <memory>

namespace CNA::Internal::Backends { class IEffectBackend; }

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

        /**
         * @brief Gets the GLSL vertex shader source string.
         *
         * @return The vertex shader source.
         */
        NOXNA [[nodiscard]] const std::string& getVertexSourceProperty() const;

        /**
         * @brief Gets the GLSL fragment shader source string.
         *
         * @return The fragment shader source.
         */
        NOXNA [[nodiscard]] const std::string& getFragmentSourceProperty() const;

        /** @brief Returns the fully qualified CNA type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Applies the GLSL shaders to the graphics device before drawing.
         */
        void OnApply() override;

        /** @brief Returns the backend effect handle (CNA extension). */
        NOXNA [[nodiscard]] CNA::Internal::Backends::IEffectBackend* GetEffectBackend() const;

    private:
        std::string vertSrc_;
        std::string fragSrc_;
        std::unique_ptr<CNA::Internal::Backends::IEffectBackend> effectBackend_;
    };
}
