// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    GraphicsResource::GraphicsResource(GraphicsDevice* device)
        : graphicsDevice_(device)
    {
        if (graphicsDevice_)
        {
            graphicsDevice_->AddResourceReference(this);
            graphicsDevice_->OnResourceCreated(this);
        }
    }

    GraphicsResource::GraphicsResource(const GraphicsResource& other)
        : graphicsDevice_(other.graphicsDevice_)
        , name_(other.name_)
        , tag_(other.tag_)
        , isDisposed_(false)
        // Disposing handlers deliberately not copied: each object owns its lifecycle
    {
    }

    GraphicsResource& GraphicsResource::operator=(const GraphicsResource& other)
    {
        if (this != &other)
        {
            graphicsDevice_ = other.graphicsDevice_;
            name_ = other.name_;
            tag_ = other.tag_;
            isDisposed_ = false;
            // Disposing handlers deliberately not copied
        }
        return *this;
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

    std::string GraphicsResource::ToString() const
    {
        return name_.empty() ? Object::ToString() : name_;
    }

    void GraphicsResource::Dispose(bool disposing)
    {
        if (isDisposed_)
        {
            return;
        }
        if (disposing)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
        }
        if (graphicsDevice_)
        {
            graphicsDevice_->OnResourceDestroyed(name_, tag_);
            graphicsDevice_->RemoveResourceReference(this);
        }
        isDisposed_ = true;
    }
}
