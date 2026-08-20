// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    class ClusteredLightAssignment;
    class ClusteredLightGrid;
    class ClusteredLightSetEXT;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The lights, and which cluster holds which, in a form a fragment shader can read.
     *
     * Three textures, because a shader cannot follow a pointer and this layer's shader floor is
     * **GLSL ES 3.00**, which has no storage buffers:
     *
     * - the **light data**, sixteen texels per light, one float each;
     * - the **cluster table**, two texels per cluster -- where its light list starts, and how long
     *   it is;
     * - the **index list**, one texel per light reference, in cluster order.
     *
     * **Every value is stored as the four bytes of its IEEE representation** in an ordinary
     * `Color`-format texture, and read back with `texelFetch` and `uintBitsToFloat`. That is not
     * an aesthetic choice: this renderer's textures are 8-bit only, so a float cannot be stored as
     * one, and the alternatives are worse -- a normalised encoding needs a range decided in advance
     * and loses precision where the range is generous, while uniform arrays cannot hold 256 lights
     * inside GL ES 3.0's uniform limits. `texelFetch` is what makes the byte encoding exact: it
     * takes integer coordinates and no filtering, so a byte comes back as the byte it was written
     * as.
     *
     * @ref getLightLookupGlsl emits the decode side, along with the cluster arithmetic, so the
     * shader and this class cannot disagree about the layout.
     */
    class ClusteredLightBuffer
    {
    public:
        /** @brief Floats stored per light, and therefore texels per row of the light texture. */
        static constexpr int kFloatsPerLight = 16;

        /** @brief Width in texels of the cluster table and index textures. */
        static constexpr int kTableWidth = 256;

        /**
         * @brief Creates an empty buffer holding no textures yet.
         *
         * @param device The device the textures are created on.
         */
        explicit ClusteredLightBuffer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the buffer and its textures. */
        ~ClusteredLightBuffer();

        ClusteredLightBuffer(const ClusteredLightBuffer&)            = delete;
        ClusteredLightBuffer& operator=(const ClusteredLightBuffer&) = delete;

        /**
         * @brief Replaces the buffer's contents with one frame's lights and assignment.
         *
         * The three inputs have to be describing the same frame -- the assignment's light indices
         * are positions in @p lights, and its cluster indices are positions in @p grid.
         *
         * @param lights     The lights, in index order.
         * @param grid       The grid the assignment sorted into.
         * @param assignment The result of that sorting.
         * @throws std::invalid_argument When the assignment does not match the set and the grid.
         */
        void upload(const ClusteredLightSetEXT& lights, const ClusteredLightGrid& grid,
                    const ClusteredLightAssignment& assignment);

        /**
         * @brief Binds the three textures and their sizes to an effect.
         *
         * @param effect    The effect whose shader was built with @ref getLightLookupGlsl.
         * @param firstUnit The first of three consecutive sampler units to use.
         * @throws std::runtime_error When nothing has been uploaded yet.
         */
        void bind(Microsoft::Xna::Framework::Graphics::ShaderEffect& effect, int firstUnit) const;

        /**
         * @brief Returns the GLSL that reads what this class writes.
         *
         * Declares the three samplers and the size uniforms @ref bind sets, and defines
         * `CnaClusteredLight`, `cnaLoadLight`, `cnaClusterFromNdc`, `cnaClusterLightCount` and
         * `cnaClusterLightIndex`. A shader includes it and never touches the layout itself.
         *
         * @return The GLSL source, with no `#version` line.
         */
        [[nodiscard]] static std::string getLightLookupGlsl();

        /** @brief Returns how many lights were uploaded. */
        [[nodiscard]] int getLightCount() const;
        /** @brief Returns how many clusters the table covers. */
        [[nodiscard]] int getClusterCount() const;
        /** @brief Returns how many light references the index list holds. */
        [[nodiscard]] int getReferenceCount() const;
        /** @brief Returns whether @ref upload has been called. */
        [[nodiscard]] bool isUploaded() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> lightData_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> clusterTable_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> indexList_;

        int lightCount_     = 0;
        int clusterCount_   = 0;
        int referenceCount_ = 0;
        int tilesX_ = 0;
        int tilesY_ = 0;
        int sliceCount_ = 0;
        float nearPlane_ = 0.0f;
        float farPlane_  = 0.0f;
        bool uploaded_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
