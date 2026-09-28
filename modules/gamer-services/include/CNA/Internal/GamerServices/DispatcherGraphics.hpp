// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }

namespace CNA::Internal::GamerServices {
/** @brief The device gamer services draw with: the IGraphicsDeviceService of the provider passed
 * to GamerServicesDispatcher::Initialize (XNA's AvatarHelpers.GraphicsDevice).
 * @return Device. @throws System::InvalidOperationException when gamer services are not initialized
 * or no graphics device exists. */
Microsoft::Xna::Framework::Graphics::GraphicsDevice& dispatcherGraphicsDevice();
/** @brief Test hook: a device used instead of the dispatcher's service provider. @param device Device or null. */
void setDispatcherGraphicsDeviceForTesting(Microsoft::Xna::Framework::Graphics::GraphicsDevice* device);
}
