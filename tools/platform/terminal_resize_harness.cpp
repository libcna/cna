// SPDX-License-Identifier: MS-PL
//
// plans/plan_platform.md PLAT-136 -- the terminal resize path, exercised where a terminal really exists.
//
// TerminalPlatform installs its SIGWINCH watcher only when it is attached to a terminal, and reads
// the new size from its own standard output. Neither is true inside CnaTests: CI redirects output,
// so isatty() is false, the watcher is never installed, and the two tests that matter most would
// skip -- which is the failure this campaign has already been bitten by once.
//
// So the assertions run here instead, in a process whose stdin and stdout really are a
// pseudo-terminal, spawned by TerminalResizeTests. The exit status is the result:
//
//     0   everything asserted held
//     10  no terminal, so the harness was spawned wrong
//     11  the platform did not report surface presentation despite having a terminal
//     12  polling before any resize produced events
//     13  a simulated resize did not produce exactly the two expected window events
//     14  the events were not Resized followed by PixelSizeChanged for this window
//     15  the reported size was not positive, or the two events disagreed
//     16  the window did not adopt the size the event announced
//
// Distinct codes rather than one failure code, because a harness that can only say "no" makes the
// test that spawns it report a mystery.

#include "CNA/Platform/PlatformFactory.hpp"

#include "../../modules/platform/src/Terminal/TerminalResizeWatcher.hpp"

#include <unistd.h>

#include <memory>
#include <variant>
#include <vector>

using namespace CNA::Platform;
using CNA::Platform::Terminal::TerminalResizeWatcher;

int main()
{
    if (isatty(STDOUT_FILENO) == 0)
    {
        return 10;
    }

    const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Terminal");
    if (!platform->GetCapabilities().surfacePresentation)
    {
        return 11;
    }

    WindowDescription description;
    description.width = 320;
    description.height = 240;
    const std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);

    // The requested size stands at creation, exactly as on every other implementation. The
    // terminal becomes authoritative only when it changes -- the same way a real windowing system
    // behaves when a window manager resizes a window after it was made.
    if (window->GetClientBounds().width != 320 || window->GetClientBounds().height != 240)
    {
        return 16;
    }

    std::vector<PlatformEvent> batch;
    platform->PollEvents(batch);
    if (!batch.empty())
    {
        return 12;
    }

    TerminalResizeWatcher::SimulateResizeForTesting();
    platform->PollEvents(batch);
    if (batch.size() != 2)
    {
        return 13;
    }

    const WindowEvent* logical = std::get_if<WindowEvent>(&batch[0]);
    const WindowEvent* pixels = std::get_if<WindowEvent>(&batch[1]);
    if (logical == nullptr || pixels == nullptr ||
        logical->kind != WindowEventKind::Resized ||
        pixels->kind != WindowEventKind::PixelSizeChanged ||
        logical->window != window->GetId() || pixels->window != window->GetId())
    {
        return 14;
    }

    // No display scaling in a terminal, so the logical and pixel sizes always agree. Emitting only
    // one of the two would silently break whichever half of a game listened for the other.
    if (logical->data1 <= 0 || logical->data2 <= 0 || logical->data1 != pixels->data1 ||
        logical->data2 != pixels->data2)
    {
        return 15;
    }

    // Adopted at once, unlike SetSize(), which the contract says lands only on Sync(). A terminal
    // resize is the display stating a fact, not a caller making a request, and a window still
    // reporting the old size would contradict the event it just delivered.
    if (window->GetClientBounds().width != logical->data1 ||
        window->GetClientBounds().height != logical->data2 ||
        window->GetPixelSize().width != logical->data1)
    {
        return 16;
    }

    return 0;
}
