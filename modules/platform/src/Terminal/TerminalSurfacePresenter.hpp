// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"

#include "TerminalAnsiWriter.hpp"
#include "TerminalFrameBudget.hpp"
#include "TerminalFrameGrid.hpp"
#include "TerminalSessionController.hpp"

#include <memory>
#include <string>

namespace CNA::Platform::Terminal {

    /** @brief The terminal's size, in character cells. */
    struct TerminalSize
    {
        /** @brief Width in columns. */
        int columns = 0;
        /** @brief Height in rows. */
        int rows = 0;
    };

    /**
     * @brief Asks the terminal how large it is.
     *
     * @param descriptor A descriptor referring to the terminal.
     * @return Its size, or a conventional 80×24 when the terminal will not say — a fallback, not
     *         a guess dressed as an answer, and the size every terminal is at least.
     */
    [[nodiscard]] TerminalSize QueryTerminalSize(int descriptor);

    /**
     * @brief Presents finished CPU pixels by quantising them into characters.
     *
     * This is what makes `IPlatformSurfacePresenter` worth having as a contract rather than as a
     * name for `SDL_Renderer`. The interface was written for `SKIA` and `BLEND2D`, which rasterise
     * on the CPU and want the result on a screen; a terminal satisfies the same interface with no
     * window, no GPU and no pixels at all — the frame becomes a grid of glyphs, and a caller that
     * only knows `Present(frame)` never learns the difference.
     *
     * ### Presentation ownership starts here
     *
     * Constructing a presenter is the first moment presentation needs the terminal, so this is
     * where it acquires its PLAT-131 session lease. Exact keyboard input may already hold another
     * lease; the shared controller applies the union and restores only after the final user exits.
     * Deliberately neither starts at platform construction nor at `AcquireSubsystem(Video)`.
     *
     * ### Aspect ratio
     *
     * A character cell is about twice as tall as it is wide. Filling the grid edge to edge would
     * therefore show every picture squashed vertically by half, so `Letterbox` fits the frame
     * against a 1:2 cell and leaves the remaining cells blank.
     */
    class TerminalSurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Takes over the terminal and prepares to present to it.
         *
         * @param outputDescriptor The descriptor frames are written to.
         * @param inputDescriptor The descriptor input arrives on.
         * @param depth How much colour the terminal can show.
         * @param targetWidth The width, in pixels, callers should rasterise at.
         * @param targetHeight The height, in pixels, callers should rasterise at.
         * @throws PlatformException If the descriptor is not a terminal, or a session is already
         *         active in this process.
         */
        TerminalSurfacePresenter(int outputDescriptor, int inputDescriptor,
                                 TerminalColourDepth depth, int targetWidth, int targetHeight);

        /**
         * @brief Uses a session controller shared with terminal input.
         * @param sessions The controller that owns the process's one terminal session.
         * @param depth How much colour the terminal can show.
         * @param targetWidth The width, in pixels, callers should rasterise at.
         * @param targetHeight The height, in pixels, callers should rasterise at.
         */
        TerminalSurfacePresenter(std::shared_ptr<TerminalSessionController> sessions,
                                 TerminalColourDepth depth, int targetWidth, int targetHeight);

        /** @brief Releases presentation; the final session user gives the terminal back. */
        ~TerminalSurfacePresenter() override;

        TerminalSurfacePresenter(const TerminalSurfacePresenter&) = delete;
        TerminalSurfacePresenter& operator=(const TerminalSurfacePresenter&) = delete;

        /**
         * @brief Sets how a frame is fitted to the grid.
         *
         * @param mode The scaling mode.
         * @param filter Ignored: quantisation box-averages every source pixel in a cell, which is
         *        already an area filter, and there is no smaller unit than a cell to filter into.
         */
        void SetScaleMode(PresentScaleMode mode, PresentFilter filter) override;

        /**
         * @brief Reports that vertical blank synchronisation is unavailable.
         *
         * @param enabled Ignored.
         * @return False always. A terminal has no vertical blank to synchronise with, and
         *         claiming otherwise would let a caller believe its frame rate was being paced.
         *         Pacing is PLAT-135's frame budget, which is a different promise.
         */
        bool SetVSync(bool enabled) override;

        /**
         * @brief Presents one finished frame.
         *
         * @param frame The pixels to present.
         * @throws PlatformException If the frame is malformed or writing to the terminal failed.
         */
        void Present(const SurfaceFrame& frame) override;

        /**
         * @brief Gets the size callers should rasterise at, in pixels.
         *
         * The window's size, not the terminal's cell grid: a game draws at the resolution it
         * asked for and quantisation happens here.
         *
         * @param width Receives the target width.
         * @param height Receives the target height.
         */
        void GetTargetSize(int& width, int& height) const override;

        /**
         * @brief Gets the terminal's current size in cells, re-queried.
         *
         * @return The size the next present will draw into.
         */
        [[nodiscard]] TerminalSize GetGridSize() const;

        /**
         * @brief Gets how many frames have been dropped because the terminal could not keep up.
         *
         * A number worth surfacing rather than hiding: dropping frames is the correct behaviour on
         * a slow link, but a caller seeing a low frame rate deserves to know whether the terminal
         * or the game is the reason.
         *
         * @return The count of dropped frames since this presenter was created.
         */
        [[nodiscard]] int GetDroppedFrameCount() const { return droppedFrames_; }

        /**
         * @brief Gets the write throughput the frame budget currently believes in.
         *
         * @return Bytes per second, or zero before any frame has been written.
         */
        [[nodiscard]] double GetMeasuredBytesPerSecond() const
        {
            return budget_.GetMeasuredBytesPerSecond();
        }

        /**
         * @brief Overrides the measured throughput.
         *
         * For tests, which cannot make a pseudo-terminal slow.
         *
         * @param bytesPerSecond The rate to assume; zero restores "unmeasured".
         */
        void SetBytesPerSecondForTesting(double bytesPerSecond)
        {
            budget_.SetBytesPerSecondForTesting(bytesPerSecond);
        }

        /**
         * @brief Gets how many cells the most recent present actually redrew.
         *
         * The measure PLAT-133 exists to move. A still picture should redraw nothing at all; a
         * frame that redraws every cell every time is the bandwidth failure the damage tracking
         * is there to prevent, and it is invisible without a number.
         *
         * @return The redrawn cell count for the last frame.
         */
        [[nodiscard]] int GetLastRedrawnCellCount() const { return lastRedrawnCells_; }

        /**
         * @brief Gets the bytes the most recent present wrote.
         *
         * Exposed so a test can assert on what actually went to the terminal. Verifying
         * presentation by reading back a pseudo-terminal works, but a full frame can exceed the
         * pty's buffer and stall the writer, so the emitted bytes are also kept here.
         *
         * @return The last frame's bytes.
         */
        [[nodiscard]] const std::string& GetLastFrameBytes() const { return lastFrame_; }

    private:
        std::shared_ptr<TerminalSessionController> sessions_;
        TerminalSessionController::Lease presenterLease_;
        std::uint64_t sessionGeneration_ = 0;
        TerminalAnsiWriter writer_;
        int outputDescriptor_;
        int targetWidth_;
        int targetHeight_;
        PresentScaleMode scaleMode_ = PresentScaleMode::Letterbox;

        /// The grid being built this frame, and the one currently on screen. Two buffers rather
        /// than one because the diff needs both, and they are swapped rather than copied so a
        /// steady-state frame allocates nothing.
        TerminalGrid grid_;
        TerminalGrid onScreen_;
        bool haveScreenContents_ = false;

        TerminalFrameBudget budget_;

        std::string lastFrame_;
        int lastRedrawnCells_ = 0;
        int droppedFrames_ = 0;
    };

} // namespace CNA::Platform::Terminal
