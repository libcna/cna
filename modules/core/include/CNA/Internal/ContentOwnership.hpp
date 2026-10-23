// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <type_traits>
#include "System/IDisposable.hpp"

namespace CNA::Internal
{
    // A reader may return Object/IDisposable rather than the concrete resource
    // type. Preserve the ownership capability across that legitimate type erasure.
    class ContentOwnedResource
    {
    public:
        /** @brief Destroys the internal ownership capability. */
        virtual ~ContentOwnedResource() = default;
    private:
        friend struct ContentOwnershipAccess;
        [[nodiscard]] virtual std::shared_ptr<System::IDisposable> GetContentDisposalRecord() const = 0;
    };

    // Membership is intrusive so a move never allocates and disposal callbacks may
    // destroy other wrappers without leaving a snapshot of dangling addresses.
    // Distinct wrappers synchronize their shared membership. Concurrent mutation
    // or destruction of the same wrapper still requires caller synchronization.
    class ContentOwnershipLink
    {
    public:
        /** @brief Creates an unrecorded link to a live wrapper. @param owner Wrapper to dispose. */
        explicit ContentOwnershipLink(System::IDisposable* owner) noexcept;
        /** @brief Joins a copied wrapper to the source scope. @param owner New wrapper. @param other Source link. */
        ContentOwnershipLink(System::IDisposable* owner, const ContentOwnershipLink& other) noexcept;
        /** @brief Transfers a wrapper's scope membership. @param owner New wrapper. @param other Source link. */
        ContentOwnershipLink(System::IDisposable* owner, ContentOwnershipLink&& other) noexcept;
        /** @brief Removes a destroyed wrapper without disposing the content owner's representative. */
        ~ContentOwnershipLink();
        /** @brief A copied link must explicitly name its new wrapper. */
        ContentOwnershipLink(const ContentOwnershipLink&) = delete;
        /** @brief A moved link must explicitly name its new wrapper. */
        ContentOwnershipLink(ContentOwnershipLink&&) = delete;
        /** @brief Copies scope membership. @param other Source link. @return This link. */
        ContentOwnershipLink& operator=(const ContentOwnershipLink& other) noexcept;
        /** @brief Transfers scope membership. @param other Source link. @return This link. */
        ContentOwnershipLink& operator=(ContentOwnershipLink&& other) noexcept;

        /** @brief Returns the recorded concrete disposer, or null for an unrecorded wrapper. */
        [[nodiscard]] std::shared_ptr<System::IDisposable> Record() const;
        /**
         * @brief Establishes content ownership with a stable, resource-retaining representative.
         * @param representative Independent wrapper retained by the content owner.
         * @param representativeLink Link belonging to that representative.
         * @return A concrete disposer whose lifetime owns the tracking group.
         */
        [[nodiscard]] std::shared_ptr<System::IDisposable> Capture(
            std::shared_ptr<System::IDisposable> representative,
            ContentOwnershipLink& representativeLink);
        /** @brief Disposes the group when called on its representative; returns whether handled. */
        [[nodiscard]] bool DisposeRepresentative();
        /** @brief Returns whether owning disposal finished for this group. */
        [[nodiscard]] bool IsDisposed() const noexcept;
        /** @brief Removes an explicitly disposed wrapper and releases an otherwise unused representative. */
        void MarkDisposed();
        /** @brief Removes this wrapper from content ownership without disposing its resource. */
        void Detach() noexcept;
        /**
         * @brief Transfers an exclusively held value out of content ownership without disposal.
         * @return The old representative to release without disposal, or null if not exclusive.
         */
        [[nodiscard]] std::shared_ptr<System::IDisposable> ReleaseExclusiveOwnership() noexcept;

    private:
        struct Group;
        System::IDisposable* owner_;
        std::shared_ptr<Group> group_;
        std::weak_ptr<Group> representativeGroup_;
        ContentOwnershipLink* previous_ = nullptr;
        ContentOwnershipLink* next_ = nullptr;
        bool linked_ = false;

        [[nodiscard]] std::shared_ptr<Group> GetGroup() const noexcept;
        void Attach(std::shared_ptr<Group> group) noexcept;
        void Unlink() noexcept;
        void MoveFrom(ContentOwnershipLink& other) noexcept;
    };

    // Only participating resource types grant friendship. Ordinary custom
    // IDisposable objects continue to use their actual shared_ptr identity.
    struct ContentOwnershipAccess
    {
        /** @brief Gets a participating value resource's disposer. @param resource Resource to record. @return Disposer or null. */
        template<class T>
        static std::shared_ptr<System::IDisposable> Record(const T& resource)
        {
            if constexpr (requires { resource.GetContentDisposalRecord(); })
                return resource.GetContentDisposalRecord();
            else if constexpr (std::is_polymorphic_v<T>)
                if (const auto* owned = dynamic_cast<const ContentOwnedResource*>(&resource))
                    return owned->GetContentDisposalRecord();
            return {};
        }

        /** @brief Makes a wrapper independently owned. @param resource Wrapper to detach. */
        template<class T>
        static void Detach(const T& resource)
        {
            if constexpr (requires { resource.DetachContentOwnership(); })
                resource.DetachContentOwnership();
            else
                resource.contentOwnership_.Detach();
        }
    };
}
