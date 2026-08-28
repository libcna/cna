// SPDX-License-Identifier: MS-PL

#include "CNA/Content/ObjectDictionaryEXT.hpp"

#include "System/Collections/Generic/KeyNotFoundException.hpp"

namespace CNA::Content
{
    const std::any& ObjectDictionaryEXT::At(const std::string& key) const
    {
        const auto found = values_.find(key);
        if (found == values_.end())
        {
            throw System::Collections::Generic::KeyNotFoundException(
                "ObjectDictionaryEXT: no entry named '" + key + "'.");
        }
        return found->second;
    }
}
