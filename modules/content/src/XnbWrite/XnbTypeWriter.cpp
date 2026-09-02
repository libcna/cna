// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbTypeWriter.hpp"

#include <mutex>
#include <utility>

namespace CNA::Content::Xnb
{
    namespace
    {
        [[nodiscard]] bool StartsWith(const std::string& text, const std::string& prefix)
        {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }
    }

    std::string XnbQualifiedTypeName(const std::string& typeName)
    {
        if (typeName.find(", Version=") != std::string::npos)
        {
            return typeName;   // already qualified by its author
        }
        if (StartsWith(typeName, "System."))
        {
            return typeName + ", " + MscorlibAssembly;
        }
        if (StartsWith(typeName, "Microsoft.Xna.Framework.Graphics."))
        {
            return typeName + ", " + XnaGraphicsAssembly;
        }
        if (StartsWith(typeName, "Microsoft.Xna.Framework."))
        {
            // Every other framework type lives in the assembly XNA resolves reader names from, so
            // .NET's own minimal spelling omits the qualifier -- and a real fixture confirms it.
            return typeName;
        }
        // A game's own type: only the game's assembly can resolve it, and only the game knows its
        // identity, so the name is written exactly as declared.
        return typeName;
    }

    std::string XnbQualifiedReaderName(const std::string& readerName, const std::string& assembly)
    {
        if (readerName.find(", Version=") != std::string::npos) { return readerName; }
        if (assembly.empty()) { return readerName; }
        return readerName + ", " + assembly;
    }

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
