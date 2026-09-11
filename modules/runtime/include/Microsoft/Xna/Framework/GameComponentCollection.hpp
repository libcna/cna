// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Collection of game components that raises events when components are added or removed. */
    class GameComponentCollection final : public System::Object
    {
    public:
        /** @brief Element count type for C++ container compatibility. */
        CNAEXT using size_type = std::vector<IGameComponent*>::size_type;
        /** @brief Mutable iterator over components (replaces IEnumerable<IGameComponent>). */
        CNAEXT using iterator = std::vector<IGameComponent*>::iterator;
        /** @brief Read-only iterator over components. */
        CNAEXT using const_iterator = std::vector<IGameComponent*>::const_iterator;

        /**
         * @brief Event that is triggered when a GameComponent is added to this collection.
         */
        System::EventHandler<GameComponentCollectionEventArgs> ComponentAdded;

        /**
         * @brief Event that is triggered when a GameComponent is removed from this collection.
         */
        System::EventHandler<GameComponentCollectionEventArgs> ComponentRemoved;

        /** @brief Creates an empty collection. */
        GameComponentCollection() = default;

        /** @brief Destructor. */
        ~GameComponentCollection() override = default;

        /**
         * @brief Gets the number of components in the collection.
         * @return The number of components currently stored.
         */
        [[nodiscard]] size_type getCountProperty() const;

        /**
         * @brief Returns whether the collection contains the specified component.
         * @param item Pointer to the component to search for.
         * @return true if the component is present; false otherwise.
         */
        [[nodiscard]] bool Contains(IGameComponent* item) const;

        /**
         * @brief Adds a component the collection keeps alive for as long as it is registered.
         *
         * XNA's `GameComponentCollection` is a `Collection<IGameComponent>` and therefore holds a
         * strong reference: a component stays alive for exactly as long as it is registered, and a
         * game that drops its own last reference leaves a live component in `Game.Components`
         * rather than a dangling one. This overload restores that guarantee for a component the
         * game owns on the heap; the raw-pointer overload remains right for one the game holds as
         * a member and outlives itself.
         *
         * The reference is released after `ComponentRemoved` is raised, so a handler still sees a
         * live component, and dropping it may destroy the component there and then.
         *
         * @note CNAEXT — not a separate XNA overload; C++ has to say which of the two ownership
         *       shapes a call means, where C# has only one.
         * @param item The component to add. A null pointer is added as a null entry, matching the
         *        raw-pointer overload.
         */
        CNAEXT void Add(std::shared_ptr<IGameComponent> item);

        /**
         * @brief Inserts a component the collection keeps alive, at the given index.
         *
         * @note CNAEXT — see Add(std::shared_ptr<IGameComponent>).
         * @param index Zero-based index to insert at.
         * @param item The component to insert.
         */
        CNAEXT void Insert(size_type index, std::shared_ptr<IGameComponent> item);

        /**
         * @brief Adds a component to the end of the collection.
         * @param item Pointer to the component to add.
         */
        void Add(IGameComponent* item);

        /**
         * @brief Inserts a component at the specified index.
         * @param index Zero-based position at which to insert the component.
         * @param item Pointer to the component to insert.
         */
        void Insert(size_type index, IGameComponent* item);

        /**
         * @brief Removes the first matching component.
         * @param item Pointer to the component to remove.
         * @return true if the component was found and removed; false otherwise.
         */
        [[nodiscard]] bool Remove(IGameComponent* item);

        /**
         * @brief Removes the component at the specified index.
         * @param index Zero-based index of the component to remove.
         */
        void RemoveAt(size_type index);

        /**
         * @brief Removes every component from the collection and raises removal events.
         */
        void Clear();

        /**
         * @brief Returns the index of the component, or -1 when it is not found.
         * @param item Pointer to the component to locate.
         * @return The zero-based index of the component, or -1 if not found.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(IGameComponent* item) const;

        /**
         * @brief Gets the component at the specified index.
         * @param index Zero-based index of the component to retrieve.
         * @return Pointer to the component at the given index.
         */
        [[nodiscard]] IGameComponent* operator[](size_type index) const;

        /**
         * @brief Returns an iterator to the first component.
         * @return A mutable iterator pointing to the first component.
         */
        CNAEXT [[nodiscard]] iterator begin();

        /**
         * @brief Returns an iterator past the last component.
         * @return A mutable iterator one past the last component.
         */
        CNAEXT [[nodiscard]] iterator end();

        /**
         * @brief Returns a const iterator to the first component.
         * @return An immutable iterator pointing to the first component.
         */
        CNAEXT [[nodiscard]] const_iterator begin() const;

        /**
         * @brief Returns a const iterator past the last component.
         * @return An immutable iterator one past the last component.
         */
        CNAEXT [[nodiscard]] const_iterator end() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<IGameComponent*> items_;

        // Strong references for components added through the shared_ptr overloads; a
        // component added as a raw pointer has no entry and its lifetime stays the game's.
        std::unordered_map<IGameComponent*, std::shared_ptr<IGameComponent>> owned_;

        void InsertItem(size_type index, IGameComponent* item);
        void RemoveItem(size_type index);
        void ClearItems();
        void SetItem(size_type index, IGameComponent* item);

        void OnComponentAdded(const GameComponentCollectionEventArgs& eventArgs);
        void OnComponentRemoved(const GameComponentCollectionEventArgs& eventArgs);
    };
}
