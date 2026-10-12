// SPDX-License-Identifier: MS-PL
// ContentTypeReaderManager's queries by name: IsRegistered, CreateReader, RemoveTypeCreatorEXT.
//
// Link boundary: each first installs every declared built-in reader, so that the registry answers
// as if a Game's constructor had registered them all. That names every reader CNA has, so it lives
// here, apart from the registry the load path uses (detail::CreateRegisteredReader), and only a
// program that asks the registry by name links it.
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "ContentTypeReaderManagerState.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"

#include <mutex>

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        void InstallDeclaredReaders()
        {
            if (!detail::DeclaredXnbReadersPending())
            {
                return;
            }
            static std::mutex installing;
            const std::lock_guard lock(installing);
            if (detail::DeclaredXnbReadersPending())
            {
                CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            }
        }
    }

    bool ContentTypeReaderManager::RemoveTypeCreatorEXT(const std::string& canonicalName)
    {
        InstallDeclaredReaders();
        return detail::EraseTypeCreator(canonicalName);
    }

    std::unique_ptr<ContentTypeReaderBase> ContentTypeReaderManager::CreateReader(const std::string& canonicalName)
    {
        InstallDeclaredReaders();
        return detail::CreateRegisteredReader(canonicalName);
    }

    bool ContentTypeReaderManager::IsRegistered(const std::string& canonicalName)
    {
        InstallDeclaredReaders();
        return detail::HasTypeCreator(canonicalName);
    }
}
