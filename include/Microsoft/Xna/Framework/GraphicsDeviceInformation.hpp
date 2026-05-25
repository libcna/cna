#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    /// Settings used when creating or resetting a graphics device.
    class GraphicsDeviceInformation : public System::Object
    {
    public:
        GraphicsDeviceInformation();
        ~GraphicsDeviceInformation() override = default;

        /// Gets the adapter on which the graphics device should be created.
        [[nodiscard]] Graphics::GraphicsAdapter* getAdapterProperty() const;

        /// Sets the adapter on which the graphics device should be created.
        void setAdapterProperty(Graphics::GraphicsAdapter* value);

        /// Gets the requested graphics feature profile.
        [[nodiscard]] Graphics::GraphicsProfile getGraphicsProfileProperty() const;

        /// Sets the requested graphics feature profile.
        void setGraphicsProfileProperty(Graphics::GraphicsProfile value);

        /// Gets the presentation settings for the graphics device.
        [[nodiscard]] Graphics::PresentationParameters& getPresentationParametersProperty();

        /// Gets the presentation settings for the graphics device.
        [[nodiscard]] const Graphics::PresentationParameters& getPresentationParametersProperty() const;

        /// Sets the presentation settings for the graphics device.
        void setPresentationParametersProperty(const Graphics::PresentationParameters& value);

        /// Creates a copy of this graphics-device information object.
        [[nodiscard]] GraphicsDeviceInformation Clone() const;

        [[nodiscard]] const SharpRuntime::String& GetTypeName() const override;

    private:
        Graphics::GraphicsAdapter* adapter_;
        Graphics::GraphicsProfile graphicsProfile_;
        Graphics::PresentationParameters presentationParameters_;
    };
}
