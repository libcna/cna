// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief The arguments to the GraphicsDeviceManager PreparingDeviceSettings event. */
    class PreparingDeviceSettingsEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Creates a new instance with the given default device settings.
         * @param graphicsDeviceInformation The default device settings that may be overridden by subscribers.
         */
        explicit PreparingDeviceSettingsEventArgs(GraphicsDeviceInformation& graphicsDeviceInformation);

        /**
         * @brief Returns the graphics device settings that will be used in device creation.
         * @return A mutable reference to the graphics device information.
         */
        [[nodiscard]] GraphicsDeviceInformation& getGraphicsDeviceInformationProperty();

        /**
         * @brief Returns the graphics device settings that will be used in device creation.
         * @return A const reference to the graphics device information.
         */
        [[nodiscard]] const GraphicsDeviceInformation& getGraphicsDeviceInformationProperty() const;

        /**
         * @brief Returns the settings mutably, from a const argument object.
         *
         * In XNA this event is the way an application overrides the settings before the device is
         * created -- requesting multisampling, choosing a back-buffer format, picking an adapter.
         * The handler collection here delivers its argument as a `const` reference, so the const
         * overload above is the only one a subscriber can reach, and that made the whole event an
         * observation. This accessor restores the canonical power without weakening the
         * collection: the settings are held **by pointer**, so they were never const to begin
         * with, and handing back a mutable reference to a non-const object from a const argument
         * involves no cast and no undefined behavior.
         *
         * A subscriber that only reads should keep using the const overload; this one exists for
         * the subscriber that has something to change. Changes take effect for the device this
         * event is preparing.
         *
         * @return A mutable reference to the graphics device information.
         */
        CNAEXT [[nodiscard]] GraphicsDeviceInformation& getGraphicsDeviceInformationEXT() const;

    private:
        GraphicsDeviceInformation* graphicsDeviceInformation_;
    };
}
