// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include "../Internal/DeferredResourceCreation.hpp"

#include <algorithm>
#include <exception>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        // Shared identities are process-wide: the static preset states (SamplerState::LinearWrap
        // and the rest) are aliased by every copy in every device, and each new device rebinds
        // them while it builds its state collections. Devices created on several threads at once
        // raced on one aliases list and faulted (RUST-UPSTREAM-023). Recursive, because sharing
        // detaches and a dispose raises Disposing on aliases that may detach in turn.
        //
        // Deliberately never destroyed. It is first used after the static preset states are
        // constructed, so as an ordinary function-local static it was destroyed BEFORE them at
        // exit, and their destructors then locked a destroyed mutex: harmless on glibc, but
        // Darwin's pthread_mutex_lock reports EINVAL, libc++ throws, and every process that had
        // shared a preset state terminated on exit (Apple M4 AM4-022).
        std::recursive_mutex& IdentityMutex()
        {
            static auto* const mutex = new std::recursive_mutex();
            return *mutex;
        }

        // The device a tracked type's base initializer passed through DeferResourceCreated(); the
        // next GraphicsResource(GraphicsDevice*) constructor on this thread consumes it.
        Microsoft::Xna::Framework::Graphics::GraphicsDevice*& PendingDeferral() noexcept
        {
            thread_local Microsoft::Xna::Framework::Graphics::GraphicsDevice* device = nullptr;
            return device;
        }

        // Resources registered with their device whose ResourceCreated has not been raised yet:
        // tracked resources whose most-derived constructor is still running (or threw). Like the
        // identity mutex, deliberately never destroyed.
        std::mutex& UnannouncedMutex()
        {
            static auto* const mutex = new std::mutex();
            return *mutex;
        }

        std::unordered_set<const void*>& Unannounced()
        {
            static auto* const set = new std::unordered_set<const void*>();
            return *set;
        }

        void MarkUnannounced(const void* resource)
        {
            const std::scoped_lock lock(UnannouncedMutex());
            Unannounced().insert(resource);
        }

        // True when @p resource was registered but never announced; forgets it either way.
        bool TakeUnannounced(const void* resource)
        {
            const std::scoped_lock lock(UnannouncedMutex());
            return Unannounced().erase(resource) != 0;
        }
    }

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

    struct GraphicsResource::SharedMetadata
    {
        std::string name;
        System::Object* tag = nullptr;
    };

    GraphicsResource::GraphicsResource(GraphicsDevice* device)
        : graphicsDevice_(device)
        , graphicsDeviceLifetime_(device != nullptr
              ? std::weak_ptr<void>(device->resourceDeviceLifetime_)
              : std::weak_ptr<void>{})
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        diagnosticResource_ = CNA::Diagnostics::ResourceHandle(
            CNA::Diagnostics::ResourceDescriptor{});
#endif
        // A tracked type armed this through CNA::Internal::Graphics::DeferResourceCreated() in
        // its base initializer and announces itself when its own constructor has finished.
        const bool deferred = graphicsDevice_ != nullptr && PendingDeferral() == graphicsDevice_;
        PendingDeferral() = nullptr;
        if (graphicsDevice_)
        {
            graphicsDevice_->AddResourceReference(this);
            if (deferred)
            {
                MarkUnannounced(this);
                return;
            }
            try
            {
                graphicsDevice_->OnResourceCreated(this);
            }
            catch (...)
            {
                // C++ does not destroy an object whose constructor throws; retire its raw
                // registration here instead of leaving device teardown a dangling pointer.
                if (!graphicsDeviceLifetime_.expired())
                    graphicsDevice_->RemoveResourceReference(this);
                throw;
            }
        }
    }

    GraphicsResource::GraphicsResource(const GraphicsResource& other)
        : graphicsDevice_(other.getGraphicsDeviceProperty())
        , graphicsDeviceLifetime_([&other] {
              const std::scoped_lock lock(IdentityMutex());
              return other.sharedIdentity_
                  ? other.sharedIdentity_->graphicsDeviceLifetime
                  : other.graphicsDeviceLifetime_;
          }())
        , name_(other.getNameProperty())
        , tag_(other.getTagProperty())
        , isDisposed_(false)
        , sharedMetadata_(other.sharedMetadata_)
        // Disposing handlers deliberately not copied: each object owns its lifecycle
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        diagnosticResource_ = CNA::Diagnostics::ResourceHandle(
            CNA::Diagnostics::ResourceDescriptor{});
#endif
    }

    GraphicsResource& GraphicsResource::operator=(const GraphicsResource& other)
    {
        if (this != &other)
        {
            const std::scoped_lock lock(IdentityMutex());
            DetachSharedIdentity();
            graphicsDevice_ = other.getGraphicsDeviceProperty();
            graphicsDeviceLifetime_ = other.sharedIdentity_
                ? other.sharedIdentity_->graphicsDeviceLifetime
                : other.graphicsDeviceLifetime_;
            name_ = other.getNameProperty();
            tag_ = other.getTagProperty();
            sharedMetadata_ = other.sharedMetadata_;
            isDisposed_ = false;
            // No diagnostic registration is made here. Every caller of this operator (BlendState,
            // DepthStencilState, RasterizerState, SamplerState, VertexDeclaration) follows it with
            // ShareResourceIdentityWith(), which makes this object an alias of `other` and drops
            // any registration it holds, because the shared identity already has one. Registering
            // here only published a create/destroy pair per assignment: SpriteBatch::Begin
            // assigns four states per call, which flooded the event stream and put a map insert
            // and erase on the draw path even in STATS.
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
#if CNA_DIAGNOSTICS_LEVEL >= 1
        , diagnosticResource_(std::move(other.diagnosticResource_))
#endif
        , sharedMetadata_(std::move(other.sharedMetadata_))
    {
        if (other.sharedIdentity_)
        {
            const std::scoped_lock lock(IdentityMutex());
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
            sharedMetadata_ = std::move(other.sharedMetadata_);
            if (other.sharedIdentity_)
            {
                const std::scoped_lock lock(IdentityMutex());
                if (sharedIdentity_)
                    DetachSharedIdentity();
                else if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
                    graphicsDevice_->RemoveResourceReference(this);

                graphicsDevice_ = other.graphicsDevice_;
                graphicsDeviceLifetime_ = other.graphicsDeviceLifetime_;
                name_ = std::move(other.name_);
                tag_ = other.tag_;
                isDisposed_ = other.isDisposed_;
#if CNA_DIAGNOSTICS_LEVEL >= 1
                diagnosticResource_ = std::move(other.diagnosticResource_);
#endif
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
#if CNA_DIAGNOSTICS_LEVEL >= 1
            diagnosticResource_ = std::move(other.diagnosticResource_);
#endif
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
        (void)TakeUnannounced(this);
    }

    GraphicsDevice* GraphicsResource::getGraphicsDeviceProperty() const
    {
        if (!sharedIdentity_)
            return graphicsDevice_;
        const std::scoped_lock lock(IdentityMutex());
        return sharedIdentity_->graphicsDevice;
    }

    bool GraphicsResource::getIsDisposedProperty() const
    {
        if (!sharedIdentity_)
            return isDisposed_;
        const std::scoped_lock lock(IdentityMutex());
        return sharedIdentity_->isDisposed;
    }

    std::string GraphicsResource::getNameProperty() const
    {
        if (!sharedIdentity_ && !sharedMetadata_)
            return name_;
        const std::scoped_lock lock(IdentityMutex());
        return sharedIdentity_ ? sharedIdentity_->name : sharedMetadata_->name;
    }

    void GraphicsResource::setNameProperty(const std::string& value)
    {
        if (sharedIdentity_)
        {
            const std::scoped_lock lock(IdentityMutex());
            sharedIdentity_->name = value;
            for (GraphicsResource* alias : sharedIdentity_->aliases)
                alias->name_ = value;
        }
        else if (sharedMetadata_)
        {
            const std::scoped_lock lock(IdentityMutex());
            sharedMetadata_->name = value;
        }
        else
        {
            name_ = value;
        }
    }

    System::Object* GraphicsResource::getTagProperty() const
    {
        if (!sharedIdentity_ && !sharedMetadata_)
            return tag_;
        const std::scoped_lock lock(IdentityMutex());
        return sharedIdentity_ ? sharedIdentity_->tag : sharedMetadata_->tag;
    }

    void GraphicsResource::setTagProperty(System::Object* value)
    {
        if (sharedIdentity_)
        {
            const std::scoped_lock lock(IdentityMutex());
            sharedIdentity_->tag = value;
            for (GraphicsResource* alias : sharedIdentity_->aliases)
                alias->tag_ = value;
        }
        else if (sharedMetadata_)
        {
            const std::scoped_lock lock(IdentityMutex());
            sharedMetadata_->tag = value;
        }
        else
        {
            tag_ = value;
        }
    }

    void GraphicsResource::EnableSharedContentMetadata() const
    {
        const std::scoped_lock lock(IdentityMutex());
        if (!sharedMetadata_)
            sharedMetadata_ = std::make_shared<SharedMetadata>(
                SharedMetadata{getNameProperty(), getTagProperty()});
    }

    void GraphicsResource::DetachSharedContentMetadata() const
    {
        const std::scoped_lock lock(IdentityMutex());
        if (sharedMetadata_)
            sharedMetadata_ = std::make_shared<SharedMetadata>(*sharedMetadata_);
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
            const std::scoped_lock lock(IdentityMutex());
            const std::shared_ptr<SharedIdentity> identity = sharedIdentity_;
            if (identity->isDisposed)
                return;

            identity->isDisposed = true;
            for (GraphicsResource* alias : identity->aliases)
            {
                alias->isDisposed_ = true;
#if CNA_DIAGNOSTICS_LEVEL >= 1
                alias->diagnosticResource_.Reset();
#endif
            }

            if (disposing)
            {
                const std::vector<GraphicsResource*> aliases = identity->aliases;
                System::Object* const sender = identity->canonical;
                for (GraphicsResource* alias : aliases)
                {
                    if (std::find(identity->aliases.begin(), identity->aliases.end(), alias) !=
                            identity->aliases.end() &&
                        alias->RaisesDisposingEventInternal())
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
        // XNA GraphicsResource.Dispose IL publishes IsDisposed before invoking Disposing.
        // Reentry and a throwing subscriber must not repeat the disposal event.
        isDisposed_ = true;
#if CNA_DIAGNOSTICS_LEVEL >= 1
        diagnosticResource_.Reset();
#endif
        const auto retireDeviceReference = [this]
        {
            if (graphicsDevice_ && !graphicsDeviceLifetime_.expired())
                graphicsDevice_->RemoveResourceReference(this);
        };
        // XNA Texture2D/VertexBuffer Dispose(bool) IL_0003-000d release the native object --
        // DeviceResourceManager::ReleaseAllReferences IL_0040-0057 raises ResourceDestroyed --
        // inside a try block, and raise Disposing from GraphicsResource::Dispose(true) in the
        // finally block. Disposing therefore follows ResourceDestroyed and still runs when a
        // ResourceDestroyed subscriber throws; an exception from Disposing replaces that one.
        // A resource whose constructor never announced it is not reported as destroyed either:
        // XNA's DeviceResourceManager::ReleaseAllReferences IL_0021-0026 finds no tracked entry.
        const bool announced = !TakeUnannounced(this);
        std::exception_ptr destroyedFailure;
        try
        {
            if (announced && graphicsDevice_ && !graphicsDeviceLifetime_.expired())
                graphicsDevice_->OnResourceDestroyed(getNameProperty(), getTagProperty());
        }
        catch (...)
        {
            destroyedFailure = std::current_exception();
        }
        try
        {
            // XNA's '~GraphicsResource' IL_0000-0006 returns before raising Disposing
            // (IL_000e-0021) when isDisposed is already set -- which Effect ('!Effect'
            // IL_0008-000a) and OcclusionQuery ('~OcclusionQuery' IL_0008-000a) always do before
            // their Dispose(bool) reaches it. ResourceDestroyed above still happens for them.
            if (disposing && RaisesDisposingEventInternal())
                Disposing.Raise(this, System::EventArgs::Empty);
        }
        catch (...)
        {
            // The destructor now observes disposed state, so it cannot retire this later.
            retireDeviceReference();
            throw;
        }
        retireDeviceReference();
        if (destroyedFailure)
            std::rethrow_exception(destroyedFailure);
    }

    bool GraphicsResource::RaisesDisposingEventInternal() const noexcept
    {
        return true;
    }

#if CNA_DIAGNOSTICS_LEVEL >= 1
    void GraphicsResource::UpdateDiagnosticResourceEXT(
        const CNA::Diagnostics::ResourceDescriptor& descriptor) noexcept
    {
        diagnosticResource_.Update(descriptor);
    }
#endif

    std::shared_ptr<GraphicsResource::SharedIdentity>
    GraphicsResource::EnsureSharedIdentity() const
    {
        const std::scoped_lock lock(IdentityMutex());
        if (sharedIdentity_)
            return sharedIdentity_;

        auto identity = std::make_shared<SharedIdentity>();
        identity->graphicsDevice = graphicsDevice_;
        identity->graphicsDeviceLifetime = graphicsDeviceLifetime_;
        identity->name = getNameProperty();
        identity->tag = getTagProperty();
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
        const std::scoped_lock lock(IdentityMutex());

        const std::shared_ptr<SharedIdentity> identity = sharedIdentity_;
        identity->aliases.erase(
            std::remove(identity->aliases.begin(), identity->aliases.end(), this),
            identity->aliases.end());
        if (identity->canonical == this)
        {
            identity->canonical = identity->aliases.empty() ? nullptr : identity->aliases.front();
#if CNA_DIAGNOSTICS_LEVEL >= 1
            if (identity->canonical != nullptr)
                identity->canonical->diagnosticResource_ = std::move(diagnosticResource_);
#endif
        }

        if (identity->aliases.empty() && !identity->isDisposed)
            identity->isDisposed = true;
        sharedIdentity_.reset();
    }

    void GraphicsResource::ShareResourceIdentityWith(const GraphicsResource& other)
    {
        if (this == &other)
            return;

        const std::scoped_lock lock(IdentityMutex());
        const std::shared_ptr<SharedIdentity> identity = other.EnsureSharedIdentity();
        if (sharedIdentity_ == identity)
            return;
        DetachSharedIdentity();
        sharedIdentity_ = identity;
        sharedIdentity_->aliases.push_back(this);
#if CNA_DIAGNOSTICS_LEVEL >= 1
        diagnosticResource_.Reset();
#endif
        graphicsDevice_ = identity->graphicsDevice;
        graphicsDeviceLifetime_ = identity->graphicsDeviceLifetime;
        name_ = identity->name;
        tag_ = identity->tag;
        isDisposed_ = identity->isDisposed;
    }

    void GraphicsResource::BindSharedResourceIdentityToDevice(GraphicsDevice* device) const
    {
        const std::scoped_lock lock(IdentityMutex());
        const std::shared_ptr<SharedIdentity> identity = EnsureSharedIdentity();
        if (identity->graphicsDevice == device &&
            (device == nullptr || !identity->graphicsDeviceLifetime.expired()))
        {
            return;
        }

        if (device != nullptr)
        {
            auto& identities = device->untrackedResourceIdentities_;
            std::erase_if(identities, [&identity](const auto& weak)
            {
                const auto bound = weak.lock();
                return !bound || bound == identity;
            });
            identities.push_back(identity);
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

    void GraphicsResource::ReleaseDeviceNativeResources(GraphicsDevice& device)
    {
        const std::scoped_lock lock(IdentityMutex());
        auto identities = std::move(device.untrackedResourceIdentities_);
        device.untrackedResourceIdentities_.clear();
        for (const auto& weak : identities)
        {
            const auto identity = weak.lock();
            if (!identity || identity->graphicsDevice != &device ||
                identity->graphicsDeviceLifetime.expired())
                continue;
            // Release only native children, never the identity's managed disposal state/events.
            for (GraphicsResource* alias : identity->aliases)
                alias->ReleaseNativeResourcesForDeviceTeardown();
        }
    }

    void GraphicsResource::ReleaseNativeResourcesForDeviceTeardown()
    {
    }

    std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease>
    GraphicsResource::LeaseRendererContext(GraphicsDevice* device)
    {
        return device != nullptr ? device->AcquireRendererThreadContextLease() : nullptr;
    }

    std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease>
    GraphicsResource::LeaseRendererContext() const
    {
        // A resource can outlive its device; its destructor then has nothing left to lease.
        if (graphicsDevice_ == nullptr || graphicsDeviceLifetime_.expired())
            return nullptr;
        return LeaseRendererContext(graphicsDevice_);
    }

    std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease>
    GraphicsResource::TryLeaseRendererContext() const noexcept
    {
        try
        {
            return LeaseRendererContext();
        }
        catch (...)
        {
            return nullptr;
        }
    }
}

namespace CNA::Internal::Graphics
{
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* DeferResourceCreated(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* const device) noexcept
    {
        Microsoft::Xna::Framework::Graphics::PendingDeferral() = device;
        return device;
    }

    void PublishResourceCreated(Microsoft::Xna::Framework::Graphics::GraphicsResource& resource)
    {
        if (!Microsoft::Xna::Framework::Graphics::TakeUnannounced(&resource))
            return;
        if (auto* const device = resource.getGraphicsDeviceProperty())
            device->OnResourceCreated(&resource);
    }
}
