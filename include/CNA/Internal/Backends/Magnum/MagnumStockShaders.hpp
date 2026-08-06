// SPDX-License-Identifier: MS-PL
#pragma once

#include "MagnumProgram.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace CNA::Internal::Backends::Magnum
{
    /**
     * @brief The vertex layouts CNA's stock (non-`ShaderEffect`) programs are written for.
     *
     * Selected by byte stride, matching how every CNA backend picks its input layout: the four
     * built-in XNA vertex types are distinguishable by stride alone.
     */
    enum class MagnumStockLayout
    {
        /** @brief `VertexPositionColor` -- stride 16. */
        PositionColor,
        /** @brief `VertexPositionTexture` -- stride 20. */
        PositionTexture,
        /** @brief `VertexPositionColorTexture` -- stride 24. */
        PositionColorTexture,
        /** @brief `VertexPositionNormalTexture` -- stride 32. */
        PositionNormalTexture,
    };

    /**
     * @brief Resolves the stock layout a vertex stride selects.
     *
     * @param strideInBytes  Byte stride of one combined vertex.
     * @param layoutOut      Receives the selected layout when the stride is recognized.
     * @return True when @p strideInBytes names one of the built-in layouts.
     */
    [[nodiscard]] bool StockLayoutForStride(std::size_t strideInBytes, MagnumStockLayout& layoutOut);

    /**
     * @brief GLSL vertex shader source for one stock layout.
     *
     * @param layout Layout the shader declares its inputs for.
     * @return Complete GLSL 3.30 source.
     */
    [[nodiscard]] std::string StockVertexShaderSource(MagnumStockLayout layout);

    /**
     * @brief GLSL fragment shader source for one stock layout.
     *
     * @param layout Layout the matching vertex shader was generated for.
     * @return Complete GLSL 3.30 source.
     */
    [[nodiscard]] std::string StockFragmentShaderSource(MagnumStockLayout layout);

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
         * @brief Returns the program for one stock layout, compiling it on first use.
         *
         * @param layout Layout to select.
         * @return The linked program, or nullptr when compilation failed.
         */
        MagnumProgram* ForLayout(MagnumStockLayout layout);

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
