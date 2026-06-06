#pragma once

#include <string>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /// Base class for all graphics resources.
    class GraphicsResource : public System::Object, public System::IDisposable
    {
    public:
        /// Raised when this resource is disposed.
        System::EventHandler<System::EventArgs> Disposing;

        ~GraphicsResource() override;

        /// Gets the graphics device that owns this resource.
        [[nodiscard]] GraphicsDevice* getGraphicsDeviceProperty() const;

        /// Gets whether this resource has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Gets the name of this resource.
        [[nodiscard]] virtual std::string getNameProperty() const;

        /// Sets the name of this resource.
        virtual void setNameProperty(const std::string& value);

        /// Gets an arbitrary user tag for this resource.
        [[nodiscard]] System::Object* getTagProperty() const;

        /// Sets an arbitrary user tag for this resource.
        void setTagProperty(System::Object* value);

        void Dispose() override;

    protected:
        explicit GraphicsResource(GraphicsDevice* device = nullptr);

        virtual void Dispose(bool disposing);

        GraphicsDevice* graphicsDevice_;
        std::string name_;
        System::Object* tag_ = nullptr;
        bool isDisposed_ = false;
    };
}
