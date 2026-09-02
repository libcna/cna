// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbTypeWriter.hpp"

#include <mutex>
#include <utility>

namespace CNA::Content::Xnb
{
    void XnbTypeWriterRegistry::Register(std::shared_ptr<const XnbTypeWriter> writer)
    {
        if (!writer)
        {
            throw XnbWriteException("XnbTypeWriterRegistry::Register(): the writer must not be null.");
        }
        if (frozen_.load(std::memory_order_acquire))
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): the registry is frozen and cannot be changed.");
        }

        std::string targetTypeName = writer->TargetTypeName();
        if (targetTypeName.empty())
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): the target type name must not be empty.");
        }
        if (writer->RuntimeReaderName().empty())
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): '" + targetTypeName +
                "' declares an empty runtime reader name.");
        }
        if (writer->TypeVersion() < 0)
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): '" + targetTypeName +
                "' declares a negative type version.");
        }

        const std::unique_lock lock(mutex_);
        if (frozen_.load(std::memory_order_acquire))
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): the registry is frozen and cannot be changed.");
        }
        const auto [entry, inserted] = writers_.emplace(std::move(targetTypeName), std::move(writer));
        if (!inserted)
        {
            throw XnbWriteException(
                "XnbTypeWriterRegistry::Register(): '" + entry->first +
                "' already has a registered .xnb type writer.");
        }
    }

    void XnbTypeWriterRegistry::Freeze() const
    {
        frozen_.store(true, std::memory_order_release);
    }

    bool XnbTypeWriterRegistry::IsFrozen() const noexcept
    {
        return frozen_.load(std::memory_order_acquire);
    }

    std::shared_ptr<const XnbTypeWriter> XnbTypeWriterRegistry::Find(
        const std::string& targetTypeName) const
    {
        if (frozen_.load(std::memory_order_acquire))
        {
            // A frozen registry is immutable, so lookups need no lock at all. This is the hot
            // path: every dispatched object performs one.
            const auto entry = writers_.find(targetTypeName);
            return entry == writers_.end() ? nullptr : entry->second;
        }
        const std::shared_lock lock(mutex_);
        const auto entry = writers_.find(targetTypeName);
        return entry == writers_.end() ? nullptr : entry->second;
    }

    std::shared_ptr<const XnbTypeWriter> XnbTypeWriterRegistry::Resolve(
        const std::string& targetTypeName) const
    {
        auto writer = Find(targetTypeName);
        if (!writer)
        {
            throw XnbWriteException(
                "No .xnb type writer is registered for '" + targetTypeName +
                "'. Register one, or call RegisterBuiltInXnbTypeWriters() if it is a built-in "
                "XNA type.");
        }
        return writer;
    }

    std::vector<std::string> XnbTypeWriterRegistry::RegisteredTypeNames() const
    {
        std::vector<std::string> names;
        const auto collect = [&names](const auto& writers)
        {
            names.reserve(writers.size());
            for (const auto& [name, writer] : writers)
            {
                (void)writer;
                names.push_back(name);
            }
        };
        if (frozen_.load(std::memory_order_acquire))
        {
            collect(writers_);
            return names;
        }
        const std::shared_lock lock(mutex_);
        collect(writers_);
        return names;
    }
}
