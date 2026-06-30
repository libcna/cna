// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
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
        return dictionary_[key];
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
}
