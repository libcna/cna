// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics {
    class RenderTarget2D;
    class Texture2D;
}

namespace CNA::Graphics {

    class RenderPipelineSettings;

    /**
     * @brief Everything a post-process pass may need for one invocation.
     *
     * One struct for every pass rather than a signature per pass: SSAO needs depth and camera
     * parameters that tonemapping does not, and a second entry point for those would make a chain
     * of mixed passes impossible to express. A pass reads the fields it needs and documents which
     * ones it requires; a missing requirement is reported by the pass, not by this type.
     */
    struct PostProcessContext
    {
        /** @brief The image to read. Required by every pass. */
        Microsoft::Xna::Framework::Graphics::Texture2D* source = nullptr;

        /** @brief Linear scene depth from the depth/normal prepass, or null when none ran. */
        Microsoft::Xna::Framework::Graphics::Texture2D* sourceDepth = nullptr;

        /** @brief View-space normals from the depth/normal prepass, or null when none ran. */
        Microsoft::Xna::Framework::Graphics::Texture2D* sourceNormals = nullptr;

        /** @brief Where to write. Null means the back buffer. */
        Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination = nullptr;

        /** @brief Destination width in pixels. */
        int width = 0;

        /** @brief Destination height in pixels. */
        int height = 0;

        /** @brief The pipeline configuration a pass reads its parameters from, or null for defaults. */
        const RenderPipelineSettings* settings = nullptr;

        /** @brief Seconds since the pipeline started, for passes with any time-varying term. */
        float elapsedSeconds = 0.0f;

        /** @brief Camera projection, required by passes that reconstruct position from depth. */
        Microsoft::Xna::Framework::Matrix projection{};

        /** @brief Inverse of @ref projection, supplied rather than recomputed per pass. */
        Microsoft::Xna::Framework::Matrix inverseProjection{};

        /** @brief Camera near plane distance; zero means "not supplied". */
        float nearPlane = 0.0f;

        /** @brief Camera far plane distance; zero means "not supplied". */
        float farPlane = 0.0f;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
