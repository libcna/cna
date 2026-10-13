// SPDX-License-Identifier: MS-PL
#pragma once
// What ContentTypeReaderManager.cpp (the registry) gives ContentTypeReaderManagerQueries.cpp (the
// public queries that install the declared built-in readers first).

#include <string>

namespace Microsoft::Xna::Framework::Content::detail
{
    /** @brief Whether a factory is registered under @p canonicalName now. */
    bool HasTypeCreator(const std::string& canonicalName);

    /** @brief Removes the factory registered under @p canonicalName. @return Whether there was one. */
    bool EraseTypeCreator(const std::string& canonicalName);
}
