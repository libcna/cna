#pragma once

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /// Loads content from application-embedded resources rather than loose files.
    ///
    /// XNA 4.0 uses this to read assets compiled into the assembly. CNA's
    /// file-extension content pipeline does not support embedded binaries yet;
    /// calls to OpenStream will throw std::runtime_error.
    // CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
    class ResourceContentManager : public ContentManager
    {
    public:
        /// Creates a ResourceContentManager using the supplied service provider.
        explicit ResourceContentManager(System::IServiceProvider* serviceProvider);

        ~ResourceContentManager() override = default;

    protected:
        /// Opens an asset stream from embedded application resources.
        ///
        /// Not implemented in CNA — throws std::runtime_error.
        [[nodiscard]] virtual std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName);
    };
}
