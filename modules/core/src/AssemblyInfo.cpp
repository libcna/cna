// SPDX-License-Identifier: MS-PL
#include "CNA/AssemblyInfo.hpp"

namespace CNA
{
    namespace
    {
        // Process-global, like the entry assembly's metadata it stands in for. A function
        // local keeps it initialized before the first namespace-scope
        // AssemblyTitleAttributeEXT in any translation unit can reach it.
        std::string& storage()
        {
            static std::string title;
            return title;
        }
    }

    void SetAssemblyTitleEXT(const std::string& title)
    {
        storage() = title;
    }

    const std::string& GetAssemblyTitleEXT()
    {
        return storage();
    }

    AssemblyTitleAttributeEXT::AssemblyTitleAttributeEXT(const std::string& title)
    {
        SetAssemblyTitleEXT(title);
    }
}
