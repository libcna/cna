// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    /**
     * @brief The settings used in creation of the graphics device.
     *
     * See GraphicsDeviceManager::PreparingDeviceSettings for context.
     */
    class GraphicsDeviceInformation : public System::Object
    {
    public:
        /** @brief Creates a new instance with default adapter, Reach profile, and default presentation parameters. */
        GraphicsDeviceInformation();

        /** @brief Destructor. */
        ~GraphicsDeviceInformation() override = default;

        /**
         * @brief Gets the adapter on which the graphics device should be created.
         * @return Pointer to the graphics adapter.
         */
        [[nodiscard]] Graphics::GraphicsAdapter* getAdapterProperty() const;

        /**
         * @brief Sets the adapter on which the graphics device should be created.
         * @param value Pointer to the graphics adapter to use.
         */
        void setAdapterProperty(Graphics::GraphicsAdapter* value);

        /**
         * @brief Gets the requested graphics device feature set.
         * @return The requested graphics profile.
         */
        [[nodiscard]] Graphics::GraphicsProfile getGraphicsProfileProperty() const;

        /**
         * @brief Sets the requested graphics device feature set.
         * @param value The desired graphics profile.
         */
        void setGraphicsProfileProperty(Graphics::GraphicsProfile value);

        /**
         * @brief Gets the settings that define how graphics will be presented to the display.
         * @return A mutable reference to the presentation parameters.
         */
        [[nodiscard]] Graphics::PresentationParameters& getPresentationParametersProperty();

        /**
         * @brief Gets the settings that define how graphics will be presented to the display.
         * @return A const reference to the presentation parameters.
         */
        [[nodiscard]] const Graphics::PresentationParameters& getPresentationParametersProperty() const;

        /**
         * @brief Sets the presentation settings for the graphics device.
         * @param value The new presentation parameters to apply.
         */
        void setPresentationParametersProperty(const Graphics::PresentationParameters& value);

        /**
         * @brief Creates a copy of this graphics-device information object.
         * @return A deep copy of this instance.
         */
        [[nodiscard]] GraphicsDeviceInformation Clone() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        Graphics::GraphicsAdapter* adapter_;
        Graphics::GraphicsProfile graphicsProfile_;
        Graphics::PresentationParameters presentationParameters_;
    };
}
