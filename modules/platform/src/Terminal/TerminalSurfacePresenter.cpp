// SPDX-License-Identifier: MS-PL

#include "TerminalSurfacePresenter.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

namespace CNA::Platform::Terminal {

    namespace {

        /// How much taller a character cell is than it is wide.
        ///
        /// Not measurable: no escape sequence reports it, and it depends on the font the user
        /// chose. Two is the ratio nearly every monospace terminal font is close to, and getting
        /// it wrong is a squashed or stretched picture rather than a failure -- which is why this
        /// is a stated assumption rather than a detection attempt that would usually be wrong.
        constexpr int kCellAspect = 2;

    } // namespace

    TerminalSize QueryTerminalSize(const int descriptor)
    {
        winsize size{};
        if (ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0)
        {
            return TerminalSize{static_cast<int>(size.ws_col), static_cast<int>(size.ws_row)};
        }
        // The conventional minimum. A pty with no size set reports zero, and drawing into a
        // zero-sized grid would silently present nothing at all.
        return TerminalSize{80, 24};
    }

    TerminalSurfacePresenter::TerminalSurfacePresenter(const int outputDescriptor,
                                                       const int inputDescriptor,
                                                       const TerminalColourDepth depth,
                                                       const int targetWidth, const int targetHeight)
        : writer_(depth)
        , outputDescriptor_(outputDescriptor)
        , targetWidth_(targetWidth)
        , targetHeight_(targetHeight)
    {
        if (targetWidth_ <= 0 || targetHeight_ <= 0)
        {
            throw PlatformException("TerminalPresenter", "a non-positive target size");
        }
        // Starting the session here, and only here, is the whole point: this is the first moment
        // anything actually needs the terminal.
        session_ = std::make_unique<TerminalSession>(outputDescriptor, inputDescriptor,
                                                    TerminalSessionOptions{});
    }

    TerminalSurfacePresenter::~TerminalSurfacePresenter() = default;

    void TerminalSurfacePresenter::SetScaleMode(const PresentScaleMode mode, PresentFilter)
    {
        scaleMode_ = mode;
        // The picture's position within the grid changes with the mode, so a diff against what is
        // on screen would leave the cells that used to be outside it holding whatever they last
        // held. Forcing the next frame to redraw everything is the correct answer.
        haveScreenContents_ = false;
        writer_.ForgetTerminalState();
    }

    bool TerminalSurfacePresenter::SetVSync(bool) { return false; }

    void TerminalSurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        width = targetWidth_;
        height = targetHeight_;
    }

    TerminalSize TerminalSurfacePresenter::GetGridSize() const
    {
        return QueryTerminalSize(outputDescriptor_);
    }

    void TerminalSurfacePresenter::Present(const SurfaceFrame& frame)
    {
        if (frame.pixels == nullptr)
        {
            throw PlatformException("TerminalPresenter::Present", "the frame has no pixels");
        }
        if (frame.width <= 0 || frame.height <= 0)
        {
            throw PlatformException("TerminalPresenter::Present",
                                    "the frame has a non-positive size");
        }

        const TerminalSize size = QueryTerminalSize(outputDescriptor_);
        if (size.columns != onScreen_.columns || size.rows != onScreen_.rows)
        {
            // The terminal was resized, so nothing on screen corresponds to the new grid's
            // coordinates and everything the writer remembers describes a screen that is gone.
            haveScreenContents_ = false;
            writer_.ForgetTerminalState();
        }
        grid_.Reset(size.columns, size.rows);

        int sourceX = 0, sourceY = 0;
        int sourceWidth = frame.width, sourceHeight = frame.height;
        int destinationColumn = 0, destinationRow = 0;
        int destinationColumns = size.columns, destinationRows = size.rows;

        switch (scaleMode_)
        {
            case PresentScaleMode::Stretch:
                break;  // the whole frame onto the whole grid, aspect ratio be damned

            case PresentScaleMode::Letterbox:
            {
                // Fit against a cell that is kCellAspect times taller than it is wide, so the
                // picture keeps its shape instead of being squashed vertically by half.
                const long long frameAspect =
                    static_cast<long long>(frame.width) * size.rows * kCellAspect;
                const long long gridAspect = static_cast<long long>(frame.height) * size.columns;

                if (frameAspect > gridAspect)
                {
                    destinationRows = static_cast<int>(
                        (static_cast<long long>(size.columns) * frame.height) /
                        (static_cast<long long>(frame.width) * kCellAspect));
                    destinationRows = std::clamp(destinationRows, 1, size.rows);
                    destinationRow = (size.rows - destinationRows) / 2;
                }
                else
                {
                    destinationColumns = static_cast<int>(
                        (static_cast<long long>(size.rows) * kCellAspect * frame.width) /
                        frame.height);
                    destinationColumns = std::clamp(destinationColumns, 1, size.columns);
                    destinationColumn = (size.columns - destinationColumns) / 2;
                }
                break;
            }

            case PresentScaleMode::None:
            {
                // "No scaling" with a cell as the destination unit means one source pixel block
                // per cell at its natural aspect: the centre of the frame, cropped to what fits.
                sourceWidth = std::min(frame.width, size.columns);
                sourceHeight = std::min(frame.height, size.rows * kCellAspect);
                sourceX = (frame.width - sourceWidth) / 2;
                sourceY = (frame.height - sourceHeight) / 2;
                destinationColumns = sourceWidth;
                destinationRows = std::max(1, sourceHeight / kCellAspect);
                destinationColumn = (size.columns - destinationColumns) / 2;
                destinationRow = (size.rows - destinationRows) / 2;
                break;
            }
        }

        QuantizeInto(frame, sourceX, sourceY, sourceWidth, sourceHeight, grid_, destinationColumn,
                     destinationRow, destinationColumns, destinationRows);

        lastFrame_.clear();
        if (haveScreenContents_)
        {
            lastRedrawnCells_ = writer_.WriteChangedCells(onScreen_, grid_, lastFrame_);
        }
        else
        {
            writer_.WriteFullFrame(grid_, lastFrame_);
            lastRedrawnCells_ = grid_.columns * grid_.rows;
        }

        std::size_t written = 0;
        while (written < lastFrame_.size())
        {
            const ssize_t count =
                write(outputDescriptor_, lastFrame_.data() + written, lastFrame_.size() - written);
            if (count <= 0)
            {
                if (count < 0 && errno == EINTR)
                {
                    continue;  // a signal arrived mid-frame; the frame is still worth finishing
                }
                throw PlatformException("TerminalPresenter::Present",
                                        "the terminal stopped accepting output");
            }
            written += static_cast<std::size_t>(count);
        }

        // Swapped, not copied: the grid just drawn becomes the screen's contents and the old one
        // becomes next frame's scratch, so a steady-state frame allocates nothing.
        grid_.cells.swap(onScreen_.cells);
        onScreen_.columns = grid_.columns;
        onScreen_.rows = grid_.rows;
        haveScreenContents_ = true;
    }

} // namespace CNA::Platform::Terminal
