#include "Microsoft/Xna/Framework/TitleContainer.hpp"

#include "System/IO/FileStream.hpp"

namespace Microsoft::Xna::Framework
{
    std::unique_ptr<System::IO::Stream> TitleContainer::OpenStream(const std::string& path)
    {
        return std::make_unique<System::IO::FileStream>(path);
    }
}