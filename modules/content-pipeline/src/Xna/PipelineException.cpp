// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    PipelineException::PipelineException()
        : System::Exception("An error occurred in the content pipeline.")
    {
    }

    PipelineException::PipelineException(const std::string& message)
        : System::Exception(message)
    {
    }

    PipelineException::PipelineException(const std::string& message,
                                         std::exception_ptr innerException)
        : System::Exception(message, std::move(innerException))
    {
    }
}
