// SPDX-License-Identifier: MS-PL
#pragma once

#include "MagnumProgram.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace CNA::Internal::Renderers::Magnum
{
    /**
     * @brief The stock (non-`ShaderEffect`) programs this renderer generates.
     *
     * A program is chosen from the draw's combined vertex stride together with the stock effect
     * flags the draw carries, exactly as every other CNA renderer selects its own: the four built-in
     * XNA vertex types are distinguishable by stride alone, and the effect flags then pick between
     * the programs that share one.
     */
    enum class MagnumStockProgram
    {
        /** @brief `VertexPositionColor` (stride 16). */
        PositionColor,
        /** @brief `VertexPositionTexture` (stride 20). */
        PositionTexture,
        /** @brief `VertexPositionColorTexture` (stride 24). */
        PositionColorTexture,
        /** @brief `VertexPositionNormalTexture` (stride 32), unlit or lit per pixel. */
        PositionNormalTexture,
        /** @brief `VertexPositionNormalTexture` (stride 32) lit per vertex, XNA's own default. */
        PositionNormalTextureVertexLit,
        /** @brief `DualTextureEffect` over `VertexPositionTexture` (stride 20). */
        DualTexture,
        /** @brief `DualTextureEffect` over `VertexPositionColorTexture` (stride 24). */
        DualTextureColored,
        /** @brief `EnvironmentMapEffect` over `VertexPositionNormalTexture` (stride 32). */
        EnvironmentMap,
        /** @brief `SkinnedEffect` over `VertexPositionNormalTextureSkinned` (stride 52 or 56). */
        Skinned,
        /** @brief `SkinnedEffect` over the same layout, lit per vertex -- XNA's own default. */
        SkinnedVertexLit,
        /** @brief `PbrEffect` over `VertexPositionNormalTangentTexture` (stride 48). */
        Pbr,
        /** @brief `SkinnedPbrEffect` over the same layout plus a skinning suffix (stride 68). */
        PbrSkinned,
    };

    /** @brief What a draw asks of the stock shader set: its vertex stride plus its effect flags. */
    struct MagnumStockSelector
    {
        /** @brief Byte stride of one combined vertex. */
        std::size_t strideInBytes = 0;
        /** @brief `GpuDrawParams::dualTexture` -- select a two-sampler `DualTextureEffect` program. */
        bool dualTexture = false;
        /** @brief `GpuDrawParams::envMapping` -- select the cube-map `EnvironmentMapEffect` program. */
        bool envMapping = false;
        /** @brief `GpuDrawParams::skinned` -- select the bone-palette `SkinnedEffect` program. */
        bool skinned = false;
        /** @brief `GpuDrawParams::pbr` -- select the metallic-roughness `PbrEffect` program. */
        bool pbr = false;
        /**
         * @brief The draw is lit and asked for XNA's default per-vertex lighting.
         *
         * True only when `lightingEnabled` is set and `preferPerPixelLighting` is not: an unlit
         * draw gets the same picture from either family, so it keeps the per-pixel program rather
         * than paying for a second compile that would render identically.
         */
        bool vertexLighting = false;
    };

    /** @brief Bone palette size the skinned program declares, matching XNA's own limit. */
    inline constexpr int kMagnumMaxBones = 72;

    /**
     * @brief Resolves the stock program a draw selects.
     *
     * @param selector   The draw's stride and effect flags.
     * @param programOut Receives the selected program when one exists.
     * @return True when the stock shader set covers this draw.
     */
    [[nodiscard]] bool SelectStockProgram(const MagnumStockSelector& selector,
                                          MagnumStockProgram& programOut);

    /**
     * @brief GLSL vertex shader source for one stock program.
     *
     * @param program Program to generate.
     * @return Complete GLSL 3.30 source.
     */
    [[nodiscard]] std::string StockVertexShaderSource(MagnumStockProgram program);

    /**
     * @brief GLSL fragment shader source for one stock program.
     *
     * @param program Program to generate.
     * @return Complete GLSL 3.30 source.
     */
    [[nodiscard]] std::string StockFragmentShaderSource(MagnumStockProgram program);

    /** @brief GLSL vertex shader source for the SpriteBatch quad program. */
    [[nodiscard]] std::string SpriteVertexShaderSource();

    /** @brief GLSL fragment shader source for the SpriteBatch quad program. */
    [[nodiscard]] std::string SpriteFragmentShaderSource();

    /**
     * @brief Lazily compiled cache of the stock programs.
     *
     * A program is built the first time a draw actually selects it, so a game that never issues a
     * lit 3D draw never pays for the lit program's compile.
     */
    class MagnumStockShaderCache
    {
    public:
        /**
         * @brief Returns one stock program, compiling it on first use.
         *
         * @param program Program to select.
         * @return The linked program, or nullptr when compilation failed.
         */
        MagnumProgram* ForProgram(MagnumStockProgram program);

        /**
         * @brief Returns the SpriteBatch program, compiling it on first use.
         *
         * @return The linked program, or nullptr when compilation failed.
         */
        MagnumProgram* Sprite();

    private:
        std::unordered_map<int, std::unique_ptr<MagnumProgram>> programs_;
        std::unique_ptr<MagnumProgram> sprite_;
    };
}
