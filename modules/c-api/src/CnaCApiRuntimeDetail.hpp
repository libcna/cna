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

void AddOwnedGraphicsResource() noexcept;

void RemoveOwnedGraphicsResource() noexcept;

[[nodiscard]] bool HasOwnedGraphicsResources() noexcept;

} // namespace CNA::C::Detail

#endif
