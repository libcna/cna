// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        // C# `private class AvatarDescriptionAsyncResult : IAsyncResult`, only ever used within
        // AvatarDescription's own BeginGetFromGamer/EndGetFromGamer pair — kept as a
        // translation-unit-private type rather than a nested class (mirrors Guide.cpp's
        // GuideAction), since nothing outside AvatarDescription needs to name it.
        class AvatarDescriptionAsyncResult : public System::IAsyncResult
        {
        public:
            explicit AvatarDescriptionAsyncResult(std::any state)
                : asyncState_(std::move(state))
                , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
            {
            }

            [[nodiscard]] bool getIsCompletedProperty() const override { return true; }
            [[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return true; }
            [[nodiscard]] const std::any& getAsyncStateProperty() const override { return asyncState_; }

            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override
            {
                return asyncWaitHandle_;
            }

        private:
            std::any asyncState_;
            mutable System::Threading::EventWaitHandle asyncWaitHandle_;
        };
    }

    System::EventHandler<System::EventArgs> AvatarDescription::Changed;

    AvatarDescription::AvatarDescription(const std::vector<SharpRuntime::bytecs>& data)
        : AvatarDescription(data, true)
    {
    }

    AvatarDescription::AvatarDescription(std::vector<SharpRuntime::bytecs> data, bool makeCopy)
    {
        if (static_cast<int>(data.size()) != DescriptionSize)
        {
            throw System::ArgumentException("Resource data must be exactly 1021 bytes.", "data");
        }
        description_ = makeCopy ? data : std::move(data);
    }

    bool AvatarDescription::getIsValidProperty() const
    {
        if (static_cast<int>(description_.size()) != DescriptionSize)
        {
            return false;
        }
        return description_[0] != 0;
    }

    std::vector<SharpRuntime::bytecs> AvatarDescription::getDescriptionProperty() const
    {
        return description_;
    }

    SharpRuntime::Single AvatarDescription::getHeightProperty() const
    {
        if (!height_.has_value())
        {
            height_ = 0.0f;
        }
        return *height_;
    }

    AvatarBodyType AvatarDescription::getBodyTypeProperty() const
    {
        if (!bodyType_.has_value())
        {
            bodyType_ = AvatarBodyType::Female;
        }
        return *bodyType_;
    }

    AvatarDescription AvatarDescription::CreateRandom()
    {
        // Despite the name, the real XNA implementation never actually randomizes anything -
        // always an all-zero (invalid) description. Preserved exactly, not "fixed."
        return AvatarDescription(std::vector<SharpRuntime::bytecs>(DescriptionSize, 0), false);
    }

    AvatarDescription AvatarDescription::CreateRandom(AvatarBodyType bodyType)
    {
        int value = static_cast<int>(bodyType);
        if (value < 0 || value > 1)
        {
            throw System::ArgumentOutOfRangeException("bodyType");
        }
        // bodyType is validated but, matching the real XNA implementation, never actually used.
        return AvatarDescription(std::vector<SharpRuntime::bytecs>(DescriptionSize, 0), false);
    }

    System::IAsyncResult* AvatarDescription::BeginGetFromGamer(
        Gamer* gamer,
        System::AsyncCallback callback,
        std::any state
    ) {
        if (gamer == nullptr)
        {
            throw System::ArgumentNullException("gamer");
        }
        if (gamer->getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("gamer");
        }

        auto* result = new AvatarDescriptionAsyncResult(std::move(state));
        if (callback)
        {
            callback(*result);
        }
        return result;
    }

    AvatarDescription AvatarDescription::EndGetFromGamer(System::IAsyncResult* result)
    {
        // Always an all-zero (invalid) description, matching the real XNA implementation.
        std::vector<SharpRuntime::bytecs> zeroData(DescriptionSize, 0);

        if (dynamic_cast<AvatarDescriptionAsyncResult*>(result) == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginGetFromGamer.");
        }

        return AvatarDescription(std::move(zeroData), false);
    }
}
