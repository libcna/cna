// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    struct GraphicsResource::SharedIdentity
    {
        GraphicsDevice* graphicsDevice = nullptr;
        std::weak_ptr<void> graphicsDeviceLifetime;
        std::string name;
        System::Object* tag = nullptr;
        bool isDisposed = false;
        std::vector<GraphicsResource*> aliases;
        GraphicsResource* canonical = nullptr;
    };

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
        : graphicsDevice_(other.getGraphicsDeviceProperty())
        , graphicsDeviceLifetime_(other.sharedIdentity_
              ? other.sharedIdentity_->graphicsDeviceLifetime
              : other.graphicsDeviceLifetime_)
        , name_(other.getNameProperty())
        , tag_(other.getTagProperty())
        , isDisposed_(false)
        // Disposing handlers deliberately not copied: each object owns its lifecycle
    {
    }

    GraphicsResource& GraphicsResource::operator=(const GraphicsResource& other)
    {
        if (this != &other)
        {
            DetachSharedIdentity();
            graphicsDevice_ = other.getGraphicsDeviceProperty();
            graphicsDeviceLifetime_ = other.sharedIdentity_
                ? other.sharedIdentity_->graphicsDeviceLifetime
                : other.graphicsDeviceLifetime_;
            name_ = other.getNameProperty();
            tag_ = other.getTagProperty();
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
        if (other.sharedIdentity_)
        {
            sharedIdentity_ = std::move(other.sharedIdentity_);
            for (GraphicsResource*& alias : sharedIdentity_->aliases)
            {
                if (alias == &other)
                {
                    alias = this;
                    break;
                }
            }
            if (sharedIdentity_->canonical == &other)
                sharedIdentity_->canonical = this;
        }
        else if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
        {
            graphicsDevice_->TransferResourceReference(&other, this);
        }
        other.graphicsDevice_ = nullptr;
        other.graphicsDeviceLifetime_.reset();
        other.tag_ = nullptr;
        other.isDisposed_ = true;
    }

    GraphicsResource& GraphicsResource::operator=(GraphicsResource&& other) noexcept
    {
        if (this != &other)
        {
            if (other.sharedIdentity_)
            {
                if (sharedIdentity_)
                    DetachSharedIdentity();
                else if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
                    graphicsDevice_->RemoveResourceReference(this);

                graphicsDevice_ = other.graphicsDevice_;
                graphicsDeviceLifetime_ = other.graphicsDeviceLifetime_;
                name_ = std::move(other.name_);
                tag_ = other.tag_;
                isDisposed_ = other.isDisposed_;
                Disposing = std::move(other.Disposing);
                sharedIdentity_ = std::move(other.sharedIdentity_);
                for (GraphicsResource*& alias : sharedIdentity_->aliases)
                {
                    if (alias == &other)
                    {
                        alias = this;
                        break;
                    }
                }
                if (sharedIdentity_->canonical == &other)
                    sharedIdentity_->canonical = this;
                other.graphicsDevice_ = nullptr;
                other.graphicsDeviceLifetime_.reset();
                other.tag_ = nullptr;
                other.isDisposed_ = true;
                return *this;
            }

            if (sharedIdentity_)
                DetachSharedIdentity();
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
        if (sharedIdentity_)
        {
            DetachSharedIdentity();
            return;
        }
        Dispose(false);
    }

    GraphicsDevice* GraphicsResource::getGraphicsDeviceProperty() const
    {
        return sharedIdentity_ ? sharedIdentity_->graphicsDevice : graphicsDevice_;
    }

    bool GraphicsResource::getIsDisposedProperty() const
    {
        return sharedIdentity_ ? sharedIdentity_->isDisposed : isDisposed_;
    }

    std::string GraphicsResource::getNameProperty() const
    {
        return sharedIdentity_ ? sharedIdentity_->name : name_;
    }

    void GraphicsResource::setNameProperty(const std::string& value)
    {
        if (sharedIdentity_)
        {
            sharedIdentity_->name = value;
            for (GraphicsResource* alias : sharedIdentity_->aliases)
                alias->name_ = value;
        }
        else
        {
            name_ = value;
        }
    }

    System::Object* GraphicsResource::getTagProperty() const
    {
        return sharedIdentity_ ? sharedIdentity_->tag : tag_;
    }

    void GraphicsResource::setTagProperty(System::Object* value)
    {
        if (sharedIdentity_)
        {
            sharedIdentity_->tag = value;
            for (GraphicsResource* alias : sharedIdentity_->aliases)
                alias->tag_ = value;
        }
        else
        {
            tag_ = value;
        }
    }

    void GraphicsResource::Dispose()
    {
        Dispose(true);
    }

    std::string GraphicsResource::ToString() const
    {
        const std::string name = getNameProperty();
        return name.empty() ? Object::ToString() : name;
    }

    void GraphicsResource::Dispose(bool disposing)
    {
        if (sharedIdentity_)
        {
            const std::shared_ptr<SharedIdentity> identity = sharedIdentity_;
            if (identity->isDisposed)
                return;

            identity->isDisposed = true;
            for (GraphicsResource* alias : identity->aliases)
                alias->isDisposed_ = true;

            if (disposing)
            {
                const std::vector<GraphicsResource*> aliases = identity->aliases;
                System::Object* const sender = identity->canonical;
                for (GraphicsResource* alias : aliases)
                {
                    if (std::find(identity->aliases.begin(), identity->aliases.end(), alias) !=
                        identity->aliases.end())
                    {
                        alias->Disposing.Raise(sender, System::EventArgs::Empty);
                    }
                }
            }
            return;
        }

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

    std::shared_ptr<GraphicsResource::SharedIdentity>
    GraphicsResource::EnsureSharedIdentity() const
    {
        if (sharedIdentity_)
            return sharedIdentity_;

        auto identity = std::make_shared<SharedIdentity>();
        identity->graphicsDevice = graphicsDevice_;
        identity->graphicsDeviceLifetime = graphicsDeviceLifetime_;
        identity->name = name_;
        identity->tag = tag_;
        identity->isDisposed = isDisposed_;
        identity->aliases.push_back(const_cast<GraphicsResource*>(this));
        identity->canonical = const_cast<GraphicsResource*>(this);
        sharedIdentity_ = identity;
        return identity;
    }

    void GraphicsResource::DetachSharedIdentity()
    {
        if (!sharedIdentity_)
            return;

        const std::shared_ptr<SharedIdentity> identity = sharedIdentity_;
        identity->aliases.erase(
            std::remove(identity->aliases.begin(), identity->aliases.end(), this),
            identity->aliases.end());
        if (identity->canonical == this)
            identity->canonical = identity->aliases.empty() ? nullptr : identity->aliases.front();

        if (identity->aliases.empty() && !identity->isDisposed)
            identity->isDisposed = true;
        sharedIdentity_.reset();
    }

    void GraphicsResource::ShareResourceIdentityWith(const GraphicsResource& other)
    {
        if (this == &other)
            return;

        const std::shared_ptr<SharedIdentity> identity = other.EnsureSharedIdentity();
        if (sharedIdentity_ == identity)
            return;
        DetachSharedIdentity();
        sharedIdentity_ = identity;
        sharedIdentity_->aliases.push_back(this);
        graphicsDevice_ = identity->graphicsDevice;
        graphicsDeviceLifetime_ = identity->graphicsDeviceLifetime;
        name_ = identity->name;
        tag_ = identity->tag;
        isDisposed_ = identity->isDisposed;
    }

    void GraphicsResource::BindSharedResourceIdentityToDevice(GraphicsDevice* device) const
    {
        const std::shared_ptr<SharedIdentity> identity = EnsureSharedIdentity();
        if (identity->graphicsDevice == device &&
            (device == nullptr || !identity->graphicsDeviceLifetime.expired()))
        {
            return;
        }

        identity->graphicsDevice = device;
        identity->graphicsDeviceLifetime = device != nullptr
            ? std::weak_ptr<void>(device->resourceDeviceLifetime_)
            : std::weak_ptr<void>{};
        for (GraphicsResource* alias : identity->aliases)
        {
            alias->graphicsDevice_ = device;
            alias->graphicsDeviceLifetime_ = identity->graphicsDeviceLifetime;
        }
    }
}
