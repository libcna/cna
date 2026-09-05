// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Collections/ObjectModel/Collection.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides a collection of child objects for a content item, maintaining each
     *        child's back reference to its parent.
     *
     * Children are held as shared pointers (they are .NET reference types); the parent back
     * reference a derived class stores through `SetParent` is a raw pointer, valid exactly while
     * the child is in the collection -- the same lifetime a .NET reference gives it.
     *
     * @tparam TParent The type of the parent object.
     * @tparam TChild The type of the child objects.
     */
    template<typename TParent, typename TChild>
    class ChildCollection : public System::Collections::ObjectModel::Collection<std::shared_ptr<TChild>>
    {
    public:
        /** @brief Destroys the collection. Children keep living wherever else they are shared. */
        ~ChildCollection() override = default;

    protected:
        /**
         * @brief Initializes a collection owned by @p parent.
         *
         * @param parent The parent every added child is attached to; must not be null.
         * @throws System::ArgumentNullException when @p parent is null.
         */
        explicit ChildCollection(TParent* parent) : parent_(parent)
        {
            if (parent_ == nullptr) { throw System::ArgumentNullException("parent"); }
        }

        /**
         * @brief Gets the parent of a child object.
         *
         * @param child The child to query.
         * @return The parent, or null when the child is detached.
         */
        [[nodiscard]] virtual TParent* GetParent(const std::shared_ptr<TChild>& child) const = 0;

        /**
         * @brief Sets the parent of a child object.
         *
         * @param child The child to modify.
         * @param parent The new parent, or null to detach.
         */
        virtual void SetParent(const std::shared_ptr<TChild>& child, TParent* parent) = 0;

        /** @brief Removes all children, detaching each from this parent. */
        void ClearItems() override
        {
            for (const std::shared_ptr<TChild>& child : this->items_) { SetParent(child, nullptr); }
            System::Collections::ObjectModel::Collection<std::shared_ptr<TChild>>::ClearItems();
        }

        /**
         * @brief Inserts a child, attaching it to this parent.
         *
         * @param index Position to insert at.
         * @param item The child; must be non-null and not already owned by a parent.
         * @throws System::ArgumentNullException when @p item is null.
         * @throws System::ArgumentException when @p item already has a parent.
         */
        void InsertItem(SharpRuntime::intcs index, const std::shared_ptr<TChild>& item) override
        {
            RequireAdoptable(item);
            System::Collections::ObjectModel::Collection<std::shared_ptr<TChild>>::InsertItem(index, item);
            SetParent(item, parent_);
        }

        /**
         * @brief Removes the child at @p index, detaching it.
         *
         * @param index Position to remove.
         */
        void RemoveItem(SharpRuntime::intcs index) override
        {
            const std::shared_ptr<TChild> child = this->items_[static_cast<std::size_t>(index)];
            System::Collections::ObjectModel::Collection<std::shared_ptr<TChild>>::RemoveItem(index);
            SetParent(child, nullptr);
        }

        /**
         * @brief Replaces the child at @p index, detaching the old one and attaching the new.
         *
         * @param index Position to replace.
         * @param item The replacement; must be non-null and not already owned.
         * @throws System::ArgumentNullException when @p item is null.
         * @throws System::ArgumentException when @p item already has a parent.
         */
        void SetItem(SharpRuntime::intcs index, const std::shared_ptr<TChild>& item) override
        {
            RequireAdoptable(item);
            const std::shared_ptr<TChild> previous = this->items_[static_cast<std::size_t>(index)];
            System::Collections::ObjectModel::Collection<std::shared_ptr<TChild>>::SetItem(index, item);
            SetParent(previous, nullptr);
            SetParent(item, parent_);
        }

    private:
        void RequireAdoptable(const std::shared_ptr<TChild>& item) const
        {
            if (item == nullptr) { throw System::ArgumentNullException("item"); }
            if (GetParent(item) != nullptr)
            {
                throw System::ArgumentException(
                    "The item already belongs to a parent; remove it from its current collection first.",
                    "item");
            }
        }

        TParent* parent_;
    };
}
