// SPDX-License-Identifier: MS-PL
#pragma once
namespace CNA::Internal::Runtime {
/** @brief Frames of system UI while game code waits on it, as a console keeps its system screens
 * running while the game thread is blocked (Guide.EndShowMessageBox before the user answers). */
class IModalFrames {
public:
    /** @brief Releases the service. */
    virtual ~IModalFrames() = default;
    /** @brief Polls input and presents the system overlay over a cleared screen for one frame,
     * without running the game's own Update or Draw.
     * @return False when no frame can run: the game has not started, is exiting or disposed, or
     * the platform cannot wait inside a frame (the browser). */
    virtual bool runModalFrame() = 0;
};
/** @brief Gets the modal frames of the most recently constructed game still alive.
 * @return The service, or null when no game exists. */
IModalFrames* activeModalFrames();
}
