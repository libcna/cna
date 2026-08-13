// SPDX-License-Identifier: MS-PL

#include "TerminalSurfacePresenter.hpp"

#include "../Common/SurfaceFrameValidation.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>

namespace CNA::Platform::Terminal {

    namespace {

        std::uint64_t NowNanoseconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

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
        : TerminalSurfacePresenter(
              std::make_shared<TerminalSessionController>(outputDescriptor, inputDescriptor,
                                                          /*kittyKeyboardSupported=*/false),
              depth, targetWidth, targetHeight)
    {
    }

    TerminalSurfacePresenter::TerminalSurfacePresenter(
        std::shared_ptr<TerminalSessionController> sessions, const TerminalColourDepth depth,
        const int targetWidth, const int targetHeight)
        : sessions_(std::move(sessions))
        , writer_(depth)
        , outputDescriptor_(sessions_->GetOutputDescriptor())
        , targetWidth_(targetWidth)
        , targetHeight_(targetHeight)
    {
        if (targetWidth_ <= 0 || targetHeight_ <= 0)
        {
            throw PlatformException("TerminalPresenter", "a non-positive target size");
        }
        // Acquiring the presenter's lease here is still the first point presentation touches the
        // terminal. The controller lets a keyboard lease coexist without either subsystem trying
        // to create the process-global TerminalSession a second time.
        presenterLease_ = sessions_->Acquire(TerminalSessionUse::Presenter);
        sessionGeneration_ = sessions_->GetGeneration();
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
        (void)Common::ValidateSurfaceFrame(frame, "TerminalPresenter::Present");

        if (sessionGeneration_ != sessions_->GetGeneration())
        {
            // Enabling keyboard input rebuilds the signal-safe session with the union of both
            // option sets. Leaving and re-entering the alternate screen invalidates every cell
            // the presenter remembered, so the next frame must be complete rather than a diff.
            sessionGeneration_ = sessions_->GetGeneration();
            haveScreenContents_ = false;
            writer_.ForgetTerminalState();
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

            case PresentScaleMode::Overscan:
            {
                // Fill the grid while retaining aspect ratio by cropping the centred excess from
                // the source. Account for the same approximately 1:2 terminal-cell aspect used by
                // Letterbox above so switching modes does not change the picture's proportions.
                const long long frameAspect =
                    static_cast<long long>(frame.width) * size.rows * kCellAspect;
                const long long gridAspect = static_cast<long long>(frame.height) * size.columns;
                if (frameAspect > gridAspect)
                {
                    sourceWidth = static_cast<int>(
                        (static_cast<long long>(frame.height) * size.columns) /
                        (static_cast<long long>(size.rows) * kCellAspect));
                    sourceWidth = std::clamp(sourceWidth, 1, frame.width);
                    sourceX = (frame.width - sourceWidth) / 2;
                }
                else
                {
                    sourceHeight = static_cast<int>(
                        (static_cast<long long>(frame.width) * size.rows * kCellAspect) /
                        size.columns);
                    sourceHeight = std::clamp(sourceHeight, 1, frame.height);
                    sourceY = (frame.height - sourceHeight) / 2;
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

            case PresentScaleMode::Native:
            {
                sourceWidth = std::min(frame.width, size.columns);
                sourceHeight = std::min(frame.height, size.rows * kCellAspect);
                destinationColumns = sourceWidth;
                destinationRows = std::max(1, sourceHeight / kCellAspect);
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

        if (lastFrame_.empty())
        {
            // Nothing changed, so nothing to send and nothing to charge for. Swapping the grids
            // below is still correct: they are identical.
            grid_.cells.swap(onScreen_.cells);
            onScreen_.columns = grid_.columns;
            onScreen_.rows = grid_.rows;
            haveScreenContents_ = true;
            return;
        }

        // A full redraw is exempt. It happens on the first frame and after a resize, and both are
        // states where the screen is *wrong* rather than merely stale -- dropping it would leave
        // the terminal showing nothing, or showing the previous size's picture, until something
        // else forced a redraw. Dropping a diff is safe precisely because it is a diff.
        const bool mustSend = !haveScreenContents_;
        if (!mustSend && !budget_.CanAfford(lastFrame_.size(), NowNanoseconds()))
        {
            // Dropped, not queued. The record of what is on screen is left untouched, so the next
            // frame's diff covers everything that changed since the last frame actually drawn --
            // which is the only reason dropping is safe at all.
            ++droppedFrames_;
            lastFrame_.clear();
            lastRedrawnCells_ = 0;
            return;
        }

        const std::uint64_t writeStarted = NowNanoseconds();
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
        // How long the write took is the measurement: a write that blocked reveals the link's
        // real throughput, and one that returned at once reveals there is no constraint worth
        // modelling.
        budget_.ObserveWrite(lastFrame_.size(), NowNanoseconds() - writeStarted);

        // Swapped, not copied: the grid just drawn becomes the screen's contents and the old one
        // becomes next frame's scratch, so a steady-state frame allocates nothing.
        grid_.cells.swap(onScreen_.cells);
        onScreen_.columns = grid_.columns;
        onScreen_.rows = grid_.rows;
        haveScreenContents_ = true;
    }

} // namespace CNA::Platform::Terminal
