// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VideoProcessor.hpp"

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    Media::VideoSoundtrackType VideoProcessor::getVideoSoundtrackTypeProperty() const noexcept
    {
        return soundtrack_;
    }

    void VideoProcessor::setVideoSoundtrackTypeProperty(const Media::VideoSoundtrackType value) noexcept
    {
        soundtrack_ = value;
    }

    std::shared_ptr<VideoContent> VideoProcessor::Process(const std::shared_ptr<VideoContent>& input,
                                                          ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            // XNA's own refusal names the parameter (measured, videoprocessor/process_null).
            throw System::ArgumentNullException("input");
        }
        input->setVideoSoundtrackTypeProperty(soundtrack_);
        // The built video streams from the source file rather than carrying it, so the source is
        // both what the build depends on and what it deploys.
        context.AddDependency(input->getFilenameProperty());
        context.AddOutputFile(input->getFilenameProperty());
        return input;
    }

    const std::string& VideoProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
