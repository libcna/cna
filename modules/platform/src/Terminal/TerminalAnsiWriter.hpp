// SPDX-License-Identifier: MS-PL
#pragma once

#include "TerminalCapabilityProbe.hpp"
#include "TerminalFrameGrid.hpp"

#include <cstdint>
#include <string>

namespace CNA::Platform::Terminal {

    /**
     * @brief Turns a quantised grid into the bytes a terminal draws.
     *
     * ### Why the colour depth is a parameter and not a constant
     *
     * A truecolor escape sent to a terminal that only has sixteen colours is not gracefully
     * ignored everywhere — some emulators print the parameters. So the depth detected at startup
     * (`TerminalCapabilityProbe`) picks the encoding, and every rung is implemented: dropping to
     * a coarser palette loses fidelity, which is honest, where emitting a sequence the terminal
     * cannot parse loses the *frame*.
     *
     * ### Why the state is remembered between calls
     *
     * The cost of a frame is dominated by the SGR sequences, not the glyphs: a 120×40 grid is
     * 4800 characters but nearly 100 KB if every cell restates its colour. So the writer tracks
     * the colour the terminal is currently in and emits a change only when the colour actually
     * differs — which is why it is an object rather than a free function.
     */
    class TerminalAnsiWriter
    {
    public:
        /**
         * @brief Creates a writer for a given colour depth.
         *
         * @param depth How much colour the terminal can show.
         */
        explicit TerminalAnsiWriter(TerminalColourDepth depth);

        /**
         * @brief Appends a full redraw of a grid.
         *
         * @param grid The grid to draw.
         * @param output Receives the bytes; appended to, never cleared.
         */
        void WriteFullFrame(const TerminalGrid& grid, std::string& output);

        /**
         * @brief Appends only the cells that differ from the previous grid.
         *
         * ### Why this is mandatory rather than an optimisation
         *
         * A 120×40 truecolor frame is roughly 96 KB. At 60 frames a second that is 5.8 MB/s,
         * which a local pseudo-terminal absorbs and an ssh link does not: the terminal falls
         * progressively further behind, and because the writer blocks on a full pipe, the *game*
         * falls behind with it. Most frames change a small fraction of their cells, so sending
         * only those is the difference between usable and unusable — not between fast and fast
         * enough.
         *
         * Runs of adjacent changed cells are written as one positioning escape followed by their
         * glyphs, because a cursor move costs about as much as six glyphs; positioning every cell
         * individually would give back most of what the diff saves.
         *
         * @param previous The grid currently on screen. A size mismatch forces a full redraw.
         * @param current The grid to bring the screen to.
         * @param output Receives the bytes; appended to, never cleared.
         * @return How many cells were redrawn, so a caller can see what the diff actually saved.
         */
        int WriteChangedCells(const TerminalGrid& previous, const TerminalGrid& current,
                              std::string& output);

        /**
         * @brief Forgets what colour the terminal is in.
         *
         * Called whenever something outside this writer may have changed the terminal's state —
         * a resize, a fresh session — because a remembered colour that is no longer true makes
         * the writer skip exactly the escape that would have corrected it.
         */
        void ForgetTerminalState();

        /** @brief Gets the colour depth in use. @return The depth this writer encodes for. */
        [[nodiscard]] TerminalColourDepth GetColourDepth() const { return depth_; }

    private:
        void AppendColour(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                          std::string& output);

        TerminalColourDepth depth_;
        bool hasCurrentColour_ = false;
        std::uint8_t currentRed_ = 0, currentGreen_ = 0, currentBlue_ = 0;
    };

    /**
     * @brief Maps a colour onto the 256-colour palette index that best matches it.
     *
     * Exposed for testing: the mapping is arithmetic on a 6×6×6 cube plus a 24-step grey ramp,
     * and picking the grey ramp for near-grey colours is what stops a grey picture from coming
     * out faintly tinted.
     *
     * @param red Red channel.
     * @param green Green channel.
     * @param blue Blue channel.
     * @return A palette index in the 16–255 range.
     */
    [[nodiscard]] int NearestIndexed256(std::uint8_t red, std::uint8_t green, std::uint8_t blue);

    /**
     * @brief Maps a colour onto the nearest of the sixteen ANSI colours.
     *
     * @param red Red channel.
     * @param green Green channel.
     * @param blue Blue channel.
     * @return An SGR foreground parameter: 30–37 for the normal colours, 90–97 for the bright ones.
     */
    [[nodiscard]] int NearestAnsi16(std::uint8_t red, std::uint8_t green, std::uint8_t blue);

} // namespace CNA::Platform::Terminal
