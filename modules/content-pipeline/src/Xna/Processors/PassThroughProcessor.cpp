// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/PassThroughProcessor.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    ContentObject PassThroughProcessor::Process(const ContentObject& input, ContentProcessorContext& context)
    {
        (void)context;
        return input;
    }

    const std::string& PassThroughProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
