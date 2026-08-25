// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"

namespace CNA
{
    /**
     * @brief Declares the running program's product title, the counterpart of .NET's
     *        <c>[assembly: AssemblyTitle("…")]</c>.
     *
     * XNA takes a game's default window title from the entry assembly's
     * <c>AssemblyTitleAttribute</c>, falling back to the assembly's simple name. C++ has
     * no assembly metadata, so a program declares the same thing here. The title must be
     * set before the graphics device creates the window -- a namespace-scope
     * AssemblyTitleAttributeEXT does that at static initialization, exactly as the C#
     * attribute is read before the game runs.
     *
     * @param title The product title, e.g. "FuzzyLogic".
     */
    CNAEXT void SetAssemblyTitleEXT(const std::string& title);

    /**
     * @brief Returns the product title declared by SetAssemblyTitleEXT().
     *
     * @return The declared title, or an empty string when the program declared none.
     */
    CNAEXT [[nodiscard]] const std::string& GetAssemblyTitleEXT();

    /**
     * @brief Declarative form of SetAssemblyTitleEXT(), for use at namespace scope.
     *
     * The C++ counterpart of a C# assembly attribute: an object of this type declared at
     * namespace scope in one translation unit registers the title before main() runs.
     *
     * @code
     * namespace { const CNA::AssemblyTitleAttributeEXT assemblyTitle{"FuzzyLogic"}; }
     * @endcode
     */
    CNAEXT struct AssemblyTitleAttributeEXT
    {
        /**
         * @brief Declares @p title as the running program's product title.
         * @param title The product title.
         */
        explicit AssemblyTitleAttributeEXT(const std::string& title);
    };
}
