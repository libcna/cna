// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/ContentOwnership.hpp"
#include <mutex>
#include <utility>

namespace CNA::Internal
{
    struct ContentOwnershipLink::Group
    {
        mutable std::recursive_mutex mutex;
        std::shared_ptr<System::IDisposable> representative;
        ContentOwnershipLink* first = nullptr;
        bool disposing = false;
        bool disposed = false;

        void Dispose()
        {
            {
                const std::scoped_lock lock(mutex);
                if (disposed || disposing) return;
                disposing = true;
            }
            try
            {
                for (;;)
                {
                    System::IDisposable* owner;
                    {
                        const std::scoped_lock lock(mutex);
                        if (!first) break;
                        owner = first->owner_;
                        first->Unlink();
                    }
                    // A callback may join a worker that copies another live wrapper.
                    // Never hold the membership lock while executing user disposal.
                    owner->Dispose();
                }
                representative->Dispose();
                const std::scoped_lock lock(mutex);
                disposed = true;
                disposing = false;
            }
            catch (...)
            {
                const std::scoped_lock lock(mutex);
                disposing = false;
                throw;
            }
        }
    };

    ContentOwnershipLink::ContentOwnershipLink(System::IDisposable* owner) noexcept : owner_(owner) { }

    ContentOwnershipLink::ContentOwnershipLink(
        System::IDisposable* owner, const ContentOwnershipLink& other) noexcept : owner_(owner)
    {
        Attach(other.GetGroup());
    }

    ContentOwnershipLink::ContentOwnershipLink(
        System::IDisposable* owner, ContentOwnershipLink&& other) noexcept : owner_(owner)
    {
        MoveFrom(other);
    }

    ContentOwnershipLink::~ContentOwnershipLink() { Detach(); }

    ContentOwnershipLink& ContentOwnershipLink::operator=(const ContentOwnershipLink& other) noexcept
    {
        if (this != &other)
        {
            auto group = other.GetGroup();
            Detach();
            Attach(std::move(group));
        }
        return *this;
    }

    ContentOwnershipLink& ContentOwnershipLink::operator=(ContentOwnershipLink&& other) noexcept
    {
        if (this != &other)
        {
            Detach();
            MoveFrom(other);
        }
        return *this;
    }

    std::shared_ptr<ContentOwnershipLink::Group> ContentOwnershipLink::GetGroup() const noexcept
    {
        return group_ ? group_ : representativeGroup_.lock();
    }

    void ContentOwnershipLink::Attach(std::shared_ptr<Group> group) noexcept
    {
        group_ = std::move(group);
        if (!group_) return;
        const std::scoped_lock lock(group_->mutex);
        if (group_->disposed) return;
        next_ = group_->first;
        if (next_) next_->previous_ = this;
        group_->first = this;
        linked_ = true;
    }

    void ContentOwnershipLink::Unlink() noexcept
    {
        if (!group_) return;
        const std::scoped_lock lock(group_->mutex);
        if (!linked_) return;
        if (previous_) previous_->next_ = next_;
        else group_->first = next_;
        if (next_) next_->previous_ = previous_;
        previous_ = next_ = nullptr;
        linked_ = false;
    }

    void ContentOwnershipLink::Detach() noexcept
    {
        Unlink();
        group_.reset();
        representativeGroup_.reset();
    }

    void ContentOwnershipLink::MoveFrom(ContentOwnershipLink& other) noexcept
    {
        // The recorded representative stays at its stable address even when a caller
        // moves its value out; the destination becomes an ordinary tracked wrapper.
        const auto group = other.group_;
        if (!group)
        {
            Attach(other.representativeGroup_.lock());
            return;
        }
        const std::scoped_lock lock(group->mutex);
        group_ = std::move(other.group_);
        previous_ = other.previous_;
        next_ = other.next_;
        linked_ = other.linked_;
        if (linked_)
        {
            if (previous_) previous_->next_ = this;
            else group_->first = this;
            if (next_) next_->previous_ = this;
        }
        other.previous_ = other.next_ = nullptr;
        other.linked_ = false;
    }

    std::shared_ptr<System::IDisposable> ContentOwnershipLink::Record() const
    {
        const auto group = GetGroup();
        return group ? std::shared_ptr<System::IDisposable>(group, group->representative.get()) : nullptr;
    }

    std::shared_ptr<System::IDisposable> ContentOwnershipLink::Capture(
        std::shared_ptr<System::IDisposable> representative,
        ContentOwnershipLink& representativeLink)
    {
        if (auto record = Record()) return record;
        auto group = std::make_shared<Group>();
        group->representative = std::move(representative);
        representativeLink.Detach();
        // A weak link here avoids a cycle: the group owns its representative.
        representativeLink.representativeGroup_ = group;
        Attach(group);
        return std::shared_ptr<System::IDisposable>(group, group->representative.get());
    }

    bool ContentOwnershipLink::DisposeRepresentative()
    {
        const auto group = representativeGroup_.lock();
        if (!group) return false;
        {
            const std::scoped_lock lock(group->mutex);
            if (group->disposing) return false;
        }
        group->Dispose();
        return true;
    }

    bool ContentOwnershipLink::IsDisposed() const noexcept
    {
        const auto group = GetGroup();
        if (!group) return false;
        const std::scoped_lock lock(group->mutex);
        return group->disposed;
    }

    void ContentOwnershipLink::MarkDisposed()
    {
        // Explicit disposal of the last wrapper must release the retained resource
        // too. Mere destruction/detachment does not revoke the manager's ownership.
        const auto group = group_;
        if (!group) return;
        bool dispose;
        {
            const std::scoped_lock lock(group->mutex);
            Unlink();
            dispose = !group->first && !group->disposing;
        }
        if (dispose) group->Dispose();
    }

    std::shared_ptr<System::IDisposable> ContentOwnershipLink::ReleaseExclusiveOwnership() noexcept
    {
        const auto group = group_;
        if (!group) return {};
        const std::scoped_lock lock(group->mutex);
        if (!linked_ || group->first != this || next_ || group->disposing) return {};
        Unlink();
        group->disposed = true;
        group_.reset();
        return std::shared_ptr<System::IDisposable>(group, group->representative.get());
    }
}
