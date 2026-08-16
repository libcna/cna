// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_RUNTIME_DETAIL_HPP
#define CNA_C_API_RUNTIME_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework {
class Game;
class GameWindow;
}

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
}

namespace CNA::C::Detail {

struct BorrowedGraphicsDevice final {
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* value;
    CNA_Handle parentGame;
};

[[nodiscard]] HandleRegistry& GetRuntimeHandles() noexcept;

[[nodiscard]] CNA_Result ValidateActiveGameHandle(CNA_Handle handle);

[[nodiscard]] CNA_Result GetBorrowedGraphicsDevice(
    CNA_Handle handle,
    std::shared_ptr<BorrowedGraphicsDevice>* outGraphicsDevice);

[[nodiscard]] CNA_Result BorrowGameGraphicsDevice(
    CNA_Handle game,
    CNA_Handle* outGraphicsDevice);

// The canonical display queries take a window, and this ABI has no window handle of its own: a game
// owns exactly one, so the game handle addresses it.
[[nodiscard]] CNA_Result GetGameWindow(
    CNA_Handle game,
    Microsoft::Xna::Framework::GameWindow** outWindow);

// The canonical game object behind a game handle. Its component collection and service container are
// reached through it, so they need no handle of their own: a game owns exactly one of each.
[[nodiscard]] CNA_Result GetGameObject(
    CNA_Handle game,
    Microsoft::Xna::Framework::Game** outGame);

void AddOwnedGameComponent() noexcept;

void RemoveOwnedGameComponent() noexcept;

[[nodiscard]] bool HasOwnedGameComponents() noexcept;

// Drops the C-side state that outlives a single graphics device: live event subscriptions and the
// recorded sampler-slot bindings. The game-destroy path calls this once the canonical device has
// already raised its own Disposing event, so a C subscriber still observes that event while no
// later unsubscription can touch the destroyed native handler.
void ResetGraphicsDeviceAdapterState() noexcept;

// Releases every graphics device manager this ABI kept alive past its handle. The canonical game
// caches a raw pointer to the graphics device service and never clears it, so a manager destroyed
// while its game still lives would leave that pointer dangling; the object is therefore retained
// until the game itself is going away.
void ResetGraphicsDeviceManagerState() noexcept;

// Releases the single borrowed handle to the game's own content manager. The manager is a value
// member of the game, so the handle is invalidated with the game rather than destroyed by a caller.
void ResetGameContentManagerState() noexcept;

void AddOwnedGraphicsResource() noexcept;

void RemoveOwnedGraphicsResource() noexcept;

[[nodiscard]] bool HasOwnedGraphicsResources() noexcept;

void AddOwnedContentManager() noexcept;

void RemoveOwnedContentManager() noexcept;

[[nodiscard]] bool HasOwnedContentManagers() noexcept;

void AddOwnedAudioResource() noexcept;

void RemoveOwnedAudioResource() noexcept;

[[nodiscard]] bool HasOwnedAudioResources() noexcept;

} // namespace CNA::C::Detail

#endif
