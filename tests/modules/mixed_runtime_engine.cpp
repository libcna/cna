// SPDX-License-Identifier: MS-PL
// cna-engine UP-006: a module-only static library consumed beside the umbrella.
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

int CnaMixedRuntimeEngine()
{
    using namespace Microsoft::Xna::Framework;
    // Retain the adapter TU and its static lifetime without needing a window.
    auto* volatile adapters = &Graphics::GraphicsAdapter::getAdaptersProperty;
    const Input::KeyboardState keyboard;
    return adapters != nullptr && !keyboard.IsKeyDown(Input::Keys::Space) &&
           Vector3::Distance(Vector3::Zero, Vector3(3, 4, 0)) == 5 &&
           !Graphics::SamplerState::PointClamp.getIsDisposedProperty() ? 0 : 1;
}
