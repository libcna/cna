// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"

#include <any>

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    RendererDetail::RendererDetail(std::string friendlyName, std::string rendererId)
        : friendlyName_(std::move(friendlyName)),
          rendererId_(std::move(rendererId))
    {
    }

    const std::string& RendererDetail::getFriendlyNameProperty() const
    {
        return friendlyName_;
    }

    const std::string& RendererDetail::getRendererIdProperty() const
    {
        return rendererId_;
    }

    std::string RendererDetail::ToString() const
    {
        return friendlyName_;
    }

    bool RendererDetail::Equals(const std::any& obj) const
    {
        const RendererDetail* other = std::any_cast<RendererDetail>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool RendererDetail::Equals(const RendererDetail& other) const
    {
        // Both fields, not the id alone. Microsoft's operator== is
        // `if (left._name == right._name) { return left._id == right._id; } return false;`, and its
        // GetHashCode XORs the hashes of both, so the friendly name is part of the identity. CNA
        // compared the id only until XNA-MISSING-017 added Equals(object): the boxed overload has
        // to agree with the typed one, so the typed one had to be the documented comparison.
        return friendlyName_ == other.friendlyName_ && rendererId_ == other.rendererId_;
    }

    bool RendererDetail::operator==(const RendererDetail& other) const
    {
        return Equals(other);
    }

    bool RendererDetail::operator!=(const RendererDetail& other) const
    {
        return !(*this == other);
    }

    int RendererDetail::GetHashCode() const
    {
        // XORs both fields, as Microsoft does, substituting 0 for an empty one. The exact CLR
        // string-hash values are not reproducible here (CHECKLIST.md records that deviation for
        // audio), but the combination and the empty-string case are.
        const auto hashOf = [](const std::string& value) -> std::size_t {
            return value.empty() ? 0u : std::hash<std::string>{}(value);
        };
        return static_cast<int>(hashOf(friendlyName_) ^ hashOf(rendererId_));
    }
}
