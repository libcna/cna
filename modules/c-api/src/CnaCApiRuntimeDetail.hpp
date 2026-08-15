// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_RUNTIME_DETAIL_HPP
#define CNA_C_API_RUNTIME_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

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

// Marks every live graphics-device event subscription dead. The game-destroy path calls this once
// the canonical device has already raised its own Disposing event, so a C subscriber still
// observes that event while no later unsubscription can touch the destroyed native handler.
void InvalidateGraphicsDeviceSubscriptions() noexcept;

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
