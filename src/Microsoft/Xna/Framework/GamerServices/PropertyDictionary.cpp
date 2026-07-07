// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotImplementedException.hpp"
#include <any>

namespace Microsoft::Xna::Framework::GamerServices
{
    PropertyDictionary::PropertyDictionary(std::map<std::string, std::any> dict)
        : dictionary_(std::move(dict))
    {
    }

    PropertyDictionary PropertyDictionary::CreateInternal(std::map<std::string, std::any> dict)
    {
        return PropertyDictionary(std::move(dict));
    }

    int PropertyDictionary::getCountProperty() const
    {
        return static_cast<int>(dictionary_.size());
    }

    std::any& PropertyDictionary::operator[](const std::string& key)
    {
        // Task 7.4: FNA's real indexer getter is `return dictionary[key];`, which throws
        // KeyNotFoundException for a missing key via Dictionary<TKey,TValue> - dictionary_[key]
        // (std::map::operator[]) instead silently default-constructed and inserted an empty
        // std::any, inflating Count as a side effect of a read. Every SetValue overload already
        // writes through dictionary_[key] directly (not through this operator), so switching this
        // to .at() - matching the const overload just below - only affects reads, never writes.
        return dictionary_.at(key);
    }

    const std::any& PropertyDictionary::operator[](const std::string& key) const
    {
        return dictionary_.at(key);
    }

    bool PropertyDictionary::ContainsKey(const std::string& key) const
    {
        return dictionary_.count(key) > 0;
    }

    bool PropertyDictionary::TryGetValue(const std::string& key, std::any& value) const
    {
        auto it = dictionary_.find(key);
        if (it == dictionary_.end())
            return false;
        value = it->second;
        return true;
    }

    System::DateTime PropertyDictionary::GetValueDateTime(const std::string& key) const
    {
        return std::any_cast<System::DateTime>(dictionary_.at(key));
    }

    double PropertyDictionary::GetValueDouble(const std::string& key) const
    {
        return std::any_cast<double>(dictionary_.at(key));
    }

    int PropertyDictionary::GetValueInt32(const std::string& key) const
    {
        return std::any_cast<int>(dictionary_.at(key));
    }

    long long PropertyDictionary::GetValueInt64(const std::string& key) const
    {
        return std::any_cast<long long>(dictionary_.at(key));
    }

    LeaderboardOutcome PropertyDictionary::GetValueOutcome(const std::string& key) const
    {
        return std::any_cast<LeaderboardOutcome>(dictionary_.at(key));
    }

    float PropertyDictionary::GetValueSingle(const std::string& key) const
    {
        return std::any_cast<float>(dictionary_.at(key));
    }

    System::IO::Stream* PropertyDictionary::GetValueStream(const std::string& key) const
    {
        return std::any_cast<System::IO::Stream*>(dictionary_.at(key));
    }

    std::string PropertyDictionary::GetValueString(const std::string& key) const
    {
        return std::any_cast<std::string>(dictionary_.at(key));
    }

    System::TimeSpan PropertyDictionary::GetValueTimeSpan(const std::string& key) const
    {
        return std::any_cast<System::TimeSpan>(dictionary_.at(key));
    }

    void PropertyDictionary::SetValue(const std::string& key, System::DateTime value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, double value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, int value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, long long value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, LeaderboardOutcome value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, float value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, const std::string& value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::SetValue(const std::string& key, System::TimeSpan value)
    {
        dictionary_[key] = value;
    }

    void PropertyDictionary::Add(const std::string& key, std::any value)
    {
        // Task 8.1: matches Dictionary<TKey,TValue>.Add's real throw-on-duplicate-key behavior
        // (real .NET's own message format: "An item with the same key has already been added.
        // Key: <key>").
        if (dictionary_.count(key) > 0)
        {
            throw System::ArgumentException("An item with the same key has already been added. Key: " + key);
        }
        dictionary_.emplace(key, std::move(value));
    }

    bool PropertyDictionary::Remove(const std::string& key)
    {
        return dictionary_.erase(key) > 0;
    }

    void PropertyDictionary::Clear()
    {
        dictionary_.clear();
    }

    std::vector<std::string> PropertyDictionary::Keys() const
    {
        std::vector<std::string> keys;
        keys.reserve(dictionary_.size());
        for (const auto& [key, value] : dictionary_)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<std::any> PropertyDictionary::Values() const
    {
        std::vector<std::any> values;
        values.reserve(dictionary_.size());
        for (const auto& [key, value] : dictionary_)
        {
            values.push_back(value);
        }
        return values;
    }

    bool PropertyDictionary::getIsReadOnlyProperty() const
    {
        return true;
    }

    void PropertyDictionary::CopyTo(std::vector<std::pair<std::string, std::any>>& /*array*/, int /*arrayIndex*/) const
    {
        throw System::NotImplementedException();
    }
}
