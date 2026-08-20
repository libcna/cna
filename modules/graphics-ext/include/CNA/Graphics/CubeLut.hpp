// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
    class Texture3D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A 3D colour lookup table read from the `.cube` exchange format.
     *
     * `ColorGradePass` has always taken a table; what it had no way to accept was the file a
     * colourist actually delivers. `.cube` -- Iridas', then Adobe's, and now everyone's -- is the
     * format every grading tool exports and every other tool reads, so a grade produced in DaVinci
     * Resolve or Photoshop can be dropped into a game without a conversion step in between.
     *
     * The parse is deliberately strict about the two things that silently produce a wrong image and
     * lenient about everything else. Entry **count** must match `LUT_3D_SIZE` cubed exactly, and
     * entry **order** is red-fastest then green then blue, which is the format's order and the
     * opposite of what a nested loop written from the axis names produces. Unknown keywords,
     * comments, blank lines and either line ending are ignored.
     *
     * A parsed table is a value: it holds its entries and can build a texture in either of the two
     * layouts @ref ColorGradePass samples -- a 2D strip that works on every renderer, or a real
     * volume texture where `GraphicsCapability::Texture3D` is available.
     */
    class CubeLut final
    {
    public:
        /**
         * @brief Parses a `.cube` document held in memory.
         *
         * @param text The whole file's text.
         * @return The parsed table.
         * @throws EngineException When the document has no `LUT_3D_SIZE`, declares a size this
         *         layer will not accept, or holds a number of entries other than the cube of it.
         */
        [[nodiscard]] static CubeLut parse(const std::string& text);

        /**
         * @brief Reads and parses a `.cube` file.
         *
         * @param path The file to read.
         * @return The parsed table.
         * @throws EngineException When the file cannot be opened, or the document is malformed.
         */
        [[nodiscard]] static CubeLut loadFromFile(const std::string& path);

        /** @brief Returns the table's edge length: a 32 means 32×32×32 entries. */
        [[nodiscard]] int getSize() const;

        /** @brief Returns the `TITLE` the file declared, or an empty string. */
        [[nodiscard]] const std::string& getTitle() const;

        /**
         * @brief Returns the input value the table's first entry corresponds to.
         *
         * `DOMAIN_MIN`, defaulting to `(0, 0, 0)`. A table graded for a log-encoded source may
         * declare a domain other than the unit cube; this layer's pass samples display-referred
         * colour in 0..1, so a table with any other domain is accepted, reported here, and
         * **not** rescaled -- see @ref isUnitDomain.
         *
         * @return The domain's lower corner.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getDomainMin() const;

        /** @brief Returns the input value the table's last entry corresponds to (`DOMAIN_MAX`). */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getDomainMax() const;

        /**
         * @brief Whether the declared domain is the unit cube this layer's grade samples.
         *
         * @return True when the domain is `(0,0,0)`..`(1,1,1)` to within a float epsilon.
         */
        [[nodiscard]] bool isUnitDomain() const;

        /**
         * @brief Returns one entry of the table.
         *
         * @param red   The red index, 0 to `getSize() - 1`.
         * @param green The green index.
         * @param blue  The blue index.
         * @return The graded colour, in the file's own units.
         * @throws std::out_of_range When any index is outside the table.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getEntry(int red, int green,
                                                                  int blue) const;

        /**
         * @brief Builds the 2D strip layout `ColorGradePass::setLut` accepts.
         *
         * `size` slices of `size`×`size` laid left to right. Works on every renderer, which is why
         * it is the default: the strip needs nothing a 2D texture does not already provide.
         *
         * @param device The device to create the texture on.
         * @return The strip texture.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
        createStripTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

        /**
         * @brief Builds a real volume texture of the table.
         *
         * The layout `ColorGradePass::setVolumeLut` accepts, where one hardware fetch replaces the
         * strip's two fetches and a hand-written blend.
         *
         * @param device The device to create the texture on.
         * @return The volume texture.
         * @throws System::NotSupportedException When the renderer has no `GraphicsCapability::Texture3D`.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>
        createVolumeTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

        /** @brief The smallest table this layer accepts; below two nothing can be interpolated. */
        static constexpr int kMinSize = 2;

        /** @brief The largest table this layer accepts, matching `ColorGradePass::kMaxLutSize`. */
        static constexpr int kMaxSize = 64;

    private:
        int size_ = 0;
        std::string title_;
        Microsoft::Xna::Framework::Vector3 domainMin_{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 domainMax_{1.0f, 1.0f, 1.0f};
        std::vector<float> entries_;   // three floats per entry, red index fastest
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
