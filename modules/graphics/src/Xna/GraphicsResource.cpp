// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    GraphicsResource::GraphicsResource(GraphicsDevice* device)
        : graphicsDevice_(device)
        , graphicsDeviceLifetime_(device != nullptr
              ? std::weak_ptr<void>(device->resourceDeviceLifetime_)
              : std::weak_ptr<void>{})
    {
        if (graphicsDevice_)
        {
            graphicsDevice_->AddResourceReference(this);
            graphicsDevice_->OnResourceCreated(this);
        }
    }

    GraphicsResource::GraphicsResource(const GraphicsResource& other)
        : graphicsDevice_(other.graphicsDevice_)
        , graphicsDeviceLifetime_(other.graphicsDeviceLifetime_)
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
            graphicsDeviceLifetime_ = other.graphicsDeviceLifetime_;
            name_ = other.name_;
            tag_ = other.tag_;
            isDisposed_ = false;
            // Disposing handlers deliberately not copied
        }
        return *this;
    }

    GraphicsResource::GraphicsResource(GraphicsResource&& other) noexcept
        : Disposing(std::move(other.Disposing))
        , graphicsDevice_(other.graphicsDevice_)
        , graphicsDeviceLifetime_(other.graphicsDeviceLifetime_)
        , name_(std::move(other.name_))
        , tag_(other.tag_)
        , isDisposed_(other.isDisposed_)
    {
        if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            graphicsDevice_->TransferResourceReference(&other, this);
        other.graphicsDevice_ = nullptr;
        other.graphicsDeviceLifetime_.reset();
        other.tag_ = nullptr;
        other.isDisposed_ = true;
    }

    GraphicsResource& GraphicsResource::operator=(GraphicsResource&& other) noexcept
    {
        if (this != &other)
        {
            GraphicsDevice* const previousDevice = graphicsDevice_;
            const std::weak_ptr<void> previousLifetime = graphicsDeviceLifetime_;
            GraphicsDevice* const incomingDevice = other.graphicsDevice_;
            const std::weak_ptr<void> incomingLifetime = other.graphicsDeviceLifetime_;

            if (previousDevice != nullptr && !previousLifetime.expired() &&
                previousDevice != incomingDevice)
            {
                previousDevice->RemoveResourceReference(this);
            }
            graphicsDevice_ = other.graphicsDevice_;
            graphicsDeviceLifetime_ = other.graphicsDeviceLifetime_;
            name_ = std::move(other.name_);
            tag_ = other.tag_;
            isDisposed_ = other.isDisposed_;
            Disposing = std::move(other.Disposing);
            if (incomingDevice != nullptr && !incomingLifetime.expired())
                incomingDevice->TransferResourceReference(&other, this);
            other.graphicsDevice_ = nullptr;
            other.graphicsDeviceLifetime_.reset();
            other.tag_ = nullptr;
            other.isDisposed_ = true;
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
        if (graphicsDevice_ && !graphicsDeviceLifetime_.expired())
        {
            graphicsDevice_->OnResourceDestroyed(name_, tag_);
            graphicsDevice_->RemoveResourceReference(this);
        }
        isDisposed_ = true;
    }
}
