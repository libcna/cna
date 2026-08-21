// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    NoMicrophoneConnectedException::NoMicrophoneConnectedException()
        // System.Exception forms this fallback from the derived runtime type. CNA has no
        // reflection, so preserve the observable FNA/.NET result with the known type name.
        : System::Exception(
              "Exception of type 'Microsoft.Xna.Framework.Audio.NoMicrophoneConnectedException' was thrown.")
    {
    }

    NoMicrophoneConnectedException::NoMicrophoneConnectedException(const std::string& message)
        : System::Exception(message)
    {
    }

    NoMicrophoneConnectedException::NoMicrophoneConnectedException(
        const std::string& message,
        std::exception_ptr innerException
    )
        : System::Exception(message, std::move(innerException))
    {
    }
}
