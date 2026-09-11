// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /** @brief Base class for all graphics resources. */
    class GraphicsResource : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when this resource is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /** @brief Destroys the GraphicsResource and releases renderer resources. */
        CNAEXT ~GraphicsResource() override;

        /** @brief Returns the graphics device that owns this resource. */
        [[nodiscard]] GraphicsDevice* getGraphicsDeviceProperty() const;

        /** @brief Returns true if this resource has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /** @brief Returns the name of this resource. */
        [[nodiscard]] virtual std::string getNameProperty() const;

        /**
         * @brief Sets the name of this resource.
         * @param value The new name to assign.
         */
        virtual void setNameProperty(const std::string& value);

        /** @brief Returns an arbitrary user tag associated with this resource. */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets an arbitrary user tag for this resource.
         * @param value Pointer to the tag object; the caller retains ownership.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Returns the Name if set, otherwise returns the type name.
         * @return Name or type name string.
         */
        [[nodiscard]] std::string ToString() const override;

        /** @brief Releases all resources held by this object. */
        void Dispose() override;

    protected:
        /**
         * @brief Constructs a GraphicsResource optionally bound to a device.
         * @param device The owning graphics device, or nullptr if not yet attached.
         */
        explicit GraphicsResource(GraphicsDevice* device = nullptr);

        // Copy: carries identity (device/name/tag) but NOT isDisposed_ or event handlers.
        // Each copy starts undisposed with its own fresh Disposing subscriber list.
        /** @brief Copy-constructs a GraphicsResource, carrying device/name/tag but resetting disposal state. */
        GraphicsResource(const GraphicsResource& other);
        /** @brief Copy-assigns a GraphicsResource, carrying device/name/tag but resetting disposal state. */
        GraphicsResource& operator=(const GraphicsResource& other);

        GraphicsResource(GraphicsResource&& other) noexcept;
        GraphicsResource& operator=(GraphicsResource&& other) noexcept;

        /**
         * @brief Makes this C++ wrapper share the source resource's managed identity.
         *
         * XNA resource properties hold object references. CNA normally keeps C++ value-copy
         * construction independent, but reference-valued public properties use this opt-in seam
         * so stack objects and temporaries remain lifetime-safe while their Name, Tag, device,
         * disposal state and Disposing subscriptions behave as one resource.
         *
         * @param other Source wrapper whose resource identity is retained.
         */
        void ShareResourceIdentityWith(const GraphicsResource& other);

        /**
         * @brief Rebinds a shared resource identity to a graphics device.
         *
         * @param device New owning device, or nullptr to detach it.
         */
        void BindSharedResourceIdentityToDevice(GraphicsDevice* device) const;

        /**
         * @brief Releases managed and native resources.
         *
         * Derived classes should override this to clean up their own resources.
         * Native resources must be released regardless of the @p disposing flag.
         *
         * @param disposing True when called from Dispose(); false when called from the finalizer.
         */
        virtual void Dispose(bool disposing);

    private:
        struct SharedIdentity;

        [[nodiscard]] std::shared_ptr<SharedIdentity> EnsureSharedIdentity() const;
        void DetachSharedIdentity();

    protected:
        GraphicsDevice* graphicsDevice_;
        // The raw device pointer preserves XNA's public ownership identity after Dispose. This
        // separate non-owning token says whether that C++ object still exists before a resource
        // destructor performs CNA-only binding cleanup.
        std::weak_ptr<void> graphicsDeviceLifetime_;
        std::string name_;
        System::Object* tag_ = nullptr;
        bool isDisposed_ = false;

    private:
        mutable std::shared_ptr<SharedIdentity> sharedIdentity_;
    };
}
