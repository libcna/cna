// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    /// Collection of game components that raises events when components are added or removed.
    class GameComponentCollection final : public System::Object
    {
    public:
        /// Element count type for C++ container compatibility.
        NOXNA using size_type = std::vector<IGameComponent*>::size_type;
        /// Mutable iterator over components (replaces IEnumerable<IGameComponent>).
        NOXNA using iterator = std::vector<IGameComponent*>::iterator;
        /// Read-only iterator over components.
        NOXNA using const_iterator = std::vector<IGameComponent*>::const_iterator;

        /// Raised after a component is added.
        System::EventHandler<GameComponentCollectionEventArgs> ComponentAdded;

        /// Raised after a component is removed.
        System::EventHandler<GameComponentCollectionEventArgs> ComponentRemoved;

        /// Creates an empty collection.
        GameComponentCollection() = default;

        /// Destructor.
        ~GameComponentCollection() override = default;

        /// Gets the number of components in the collection.
        [[nodiscard]] size_type getCountProperty() const;

        /// Returns whether the collection contains the specified component.
        [[nodiscard]] bool Contains(IGameComponent* item) const;

        /// Adds a component to the end of the collection.
        void Add(IGameComponent* item);

        /// Inserts a component at the specified index.
        void Insert(size_type index, IGameComponent* item);

        /// Removes the first matching component.
        [[nodiscard]] bool Remove(IGameComponent* item);

        /// Removes the component at the specified index.
        void RemoveAt(size_type index);

        /// Removes every component from the collection and raises removal events.
        void Clear();

        /// Returns the index of the component, or -1 when it is not found.
        [[nodiscard]] SharpRuntime::intcs IndexOf(IGameComponent* item) const;

        /// Gets the component at the specified index.
        [[nodiscard]] IGameComponent* operator[](size_type index) const;

        /// Returns an iterator to the first component.
        NOXNA [[nodiscard]] iterator begin();
        /// Returns an iterator past the last component.
        NOXNA [[nodiscard]] iterator end();
        /// Returns a const iterator to the first component.
        NOXNA [[nodiscard]] const_iterator begin() const;
        /// Returns a const iterator past the last component.
        NOXNA [[nodiscard]] const_iterator end() const;

        /// Returns the fully-qualified .NET type name of this class.
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<IGameComponent*> items_;

        void InsertItem(size_type index, IGameComponent* item);
        void RemoveItem(size_type index);
        void ClearItems();
        void SetItem(size_type index, IGameComponent* item);

        void OnComponentAdded(const GameComponentCollectionEventArgs& eventArgs);
        void OnComponentRemoved(const GameComponentCollectionEventArgs& eventArgs);
    };
}
