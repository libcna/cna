#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    GraphicsResource::GraphicsResource(GraphicsDevice* device)
        : graphicsDevice_(device)
    {
    }

    GraphicsResource::~GraphicsResource()
    {
        Dispose(false);
    }

    GraphicsDevice* GraphicsResource::getGraphicsDeviceProperty() const
    {
        return graphicsDevice_;
    }

    bool GraphicsResource::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string GraphicsResource::getNameProperty() const
    {
        return name_;
    }

    void GraphicsResource::setNameProperty(const std::string& value)
    {
        name_ = value;
    }

    System::Object* GraphicsResource::getTagProperty() const
    {
        return tag_;
    }

    void GraphicsResource::setTagProperty(System::Object* value)
    {
        tag_ = value;
    }

    void GraphicsResource::Dispose()
    {
        Dispose(true);
    }

    void GraphicsResource::Dispose(bool disposing)
    {
        if (isDisposed_)
        {
            return;
        }
        isDisposed_ = true;
        if (disposing)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
        }
    }
}
