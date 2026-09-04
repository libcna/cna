// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"

#include <algorithm>
#include <utility>

namespace CNA::Internal::Xnb
{
    void XnbTypeWriterRegistry::RequireMutable() const
    {
        if (frozen_)
        {
            throw XnbWriteException(
                "XNB: this type-writer registry is frozen and cannot accept further "
                "registrations.");
        }
    }

    void XnbTypeWriterRegistry::Register(std::shared_ptr<const XnbTypeWriterBase> writer)
    {
        RequireMutable();
        if (writer == nullptr)
        {
            throw XnbWriteException("XNB: a null type writer cannot be registered.");
        }
        const XnbTypeId type = writer->TargetTypeId();
        const auto existing = writers_.find(type);
        if (existing != writers_.end())
        {
            throw XnbWriteException(
                "XNB: a type writer emitting '" +
                XnbCanonicalReaderName(existing->second->ReaderIdentity()) +
                "' already claims the C++ type that '" +
                XnbCanonicalReaderName(writer->ReaderIdentity()) +
                "' is being registered for; exactly one writer may own a type.");
        }
        writers_.emplace(type, std::move(writer));
    }

    void XnbTypeWriterRegistry::Freeze() const { frozen_ = true; }

    bool XnbTypeWriterRegistry::IsFrozen() const noexcept { return frozen_; }

    const XnbTypeWriterBase* XnbTypeWriterRegistry::Find(const XnbTypeId type) const
    {
        const auto found = writers_.find(type);
        return found == writers_.end() ? nullptr : found->second.get();
    }

    const XnbTypeWriterBase& XnbTypeWriterRegistry::Require(
        const XnbTypeId type, const std::string& diagnosticTypeName) const
    {
        const XnbTypeWriterBase* writer = Find(type);
        if (writer == nullptr)
        {
            throw XnbWriteException(
                diagnosticTypeName +
                " has no registered XNB type writer. Register one before writing, or write the "
                "value through a type the built-in registry already covers.");
        }
        return *writer;
    }

    std::vector<std::string> XnbTypeWriterRegistry::RegisteredReaderNames() const
    {
        std::vector<std::string> names;
        names.reserve(writers_.size());
        for (const auto& [type, writer] : writers_)
        {
            names.push_back(XnbCanonicalReaderName(writer->ReaderIdentity()));
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}
