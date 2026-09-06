// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    GameComponentCollection::size_type GameComponentCollection::getCountProperty() const
    {
        return items_.size();
    }

    bool GameComponentCollection::Contains(IGameComponent* item) const
    {
        return IndexOf(item) != -1;
    }

    void GameComponentCollection::Add(IGameComponent* item)
    {
        InsertItem(items_.size(), item);
    }

    void GameComponentCollection::Insert(size_type index, IGameComponent* item)
    {
        InsertItem(index, item);
    }

    void GameComponentCollection::Add(std::shared_ptr<IGameComponent> item)
    {
        Insert(items_.size(), std::move(item));
    }

    void GameComponentCollection::Insert(size_type index, std::shared_ptr<IGameComponent> item)
    {
        IGameComponent* const raw = item.get();
        if (raw != nullptr)
        {
            // Taken before the insert so a ComponentAdded handler that removes the component
            // again already finds the reference to release; put back on failure so a rejected
            // add leaves no ownership behind.
            owned_.insert_or_assign(raw, std::move(item));
        }

        try
        {
            InsertItem(index, raw);
        }
        catch (...)
        {
            if (raw != nullptr)
            {
                owned_.erase(raw);
            }
            throw;
        }
    }

    bool GameComponentCollection::Remove(IGameComponent* item)
    {
        const auto it = std::find(items_.begin(), items_.end(), item);
        if (it == items_.end())
        {
            return false;
        }

        RemoveItem(static_cast<size_type>(std::distance(items_.begin(), it)));
        return true;
    }

    void GameComponentCollection::RemoveAt(size_type index)
    {
        RemoveItem(index);
    }

    void GameComponentCollection::Clear()
    {
        ClearItems();
    }

    SharpRuntime::intcs GameComponentCollection::IndexOf(IGameComponent* item) const
    {
        const auto it = std::find(items_.begin(), items_.end(), item);
        if (it == items_.end())
        {
            return -1;
        }

        return static_cast<SharpRuntime::intcs>(std::distance(items_.begin(), it));
    }

    IGameComponent* GameComponentCollection::operator[](size_type index) const
    {
        if (index >= items_.size())
        {
            throw std::out_of_range("index");
        }

        return items_[index];
    }

    GameComponentCollection::iterator GameComponentCollection::begin()
    {
        return items_.begin();
    }

    GameComponentCollection::iterator GameComponentCollection::end()
    {
        return items_.end();
    }

    GameComponentCollection::const_iterator GameComponentCollection::begin() const
    {
        return items_.begin();
    }

    GameComponentCollection::const_iterator GameComponentCollection::end() const
    {
        return items_.end();
    }

    const std::string& GameComponentCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GameComponentCollection";
        return typeName;
    }

    void GameComponentCollection::ClearItems()
    {
        for (IGameComponent* component : items_)
        {
            if (component != nullptr)
            {
                OnComponentRemoved(GameComponentCollectionEventArgs(component));
            }
        }

        items_.clear();
        owned_.clear();
    }

    void GameComponentCollection::InsertItem(size_type index, IGameComponent* item)
    {
        if (IndexOf(item) != -1)
        {
            throw std::invalid_argument("Cannot Add Same Component Multiple Times");
        }

        if (index > items_.size())
        {
            throw std::out_of_range("index");
        }

        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), item);

        if (item != nullptr)
        {
            OnComponentAdded(GameComponentCollectionEventArgs(item));
        }
    }

    void GameComponentCollection::RemoveItem(size_type index)
    {
        if (index >= items_.size())
        {
            throw std::out_of_range("index");
        }

        IGameComponent* gameComponent = items_[index];
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));

        if (gameComponent != nullptr)
        {
            OnComponentRemoved(GameComponentCollectionEventArgs(gameComponent));
            // After the event, not before: a handler must still see a live component. Dropping
            // the reference here may be what destroys it.
            owned_.erase(gameComponent);
        }
    }

    void GameComponentCollection::SetItem(size_type index, IGameComponent* item)
    {
        (void)index;
        (void)item;
        // FNA throws NotSupportedException; C++ uses std::logic_error (no System::NotSupportedException yet).
        throw std::logic_error("SetItem is not supported.");
    }

    void GameComponentCollection::OnComponentAdded(const GameComponentCollectionEventArgs& eventArgs)
    {
        ComponentAdded.Raise(this, eventArgs);
    }

    void GameComponentCollection::OnComponentRemoved(const GameComponentCollectionEventArgs& eventArgs)
    {
        ComponentRemoved.Raise(this, eventArgs);
    }
}
