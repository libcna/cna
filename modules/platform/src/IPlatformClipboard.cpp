// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include "CNA/Platform/PlatformException.hpp"

namespace CNA::Platform {

    // The defaults of a clipboard that holds text only (plans/plan_x11.md X11-0158): nothing but
    // text to read, and a refusal naming the capability for anything else -- never a write that
    // is accepted and dropped.

    std::vector<std::string> IPlatformClipboard::GetMimeTypes() const
    {
        return {};
    }

    bool IPlatformClipboard::HasData(const std::string&) const
    {
        return false;
    }

    std::vector<std::uint8_t> IPlatformClipboard::GetData(const std::string&) const
    {
        return {};
    }

    void IPlatformClipboard::SetData(const std::vector<ClipboardOffer>&)
    {
        throw PlatformNotSupportedException(PlatformCapability::ClipboardData, "this clipboard");
    }

} // namespace CNA::Platform
