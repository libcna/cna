// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <mutex>

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
            // A buffer that is not a CNA avatar keeps the reference default of 0.
            const auto descriptor = CNA::Internal::GamerServices::Avatars::decode(description_);
            height_ = descriptor ? static_cast<float>(descriptor->heightMillimeters) / 1000.0f : 0.0f;
        }
        return *height_;
    }

    AvatarBodyType AvatarDescription::getBodyTypeProperty() const
    {
        if (!bodyType_.has_value())
        {
            const auto descriptor = CNA::Internal::GamerServices::Avatars::decode(description_);
            bodyType_ = descriptor && descriptor->bodyType == 1 ? AvatarBodyType::Male : AvatarBodyType::Female;
        }
        return *bodyType_;
    }

    AvatarDescription AvatarDescription::CreateRandom()
    {
        return CreateRandom(std::optional<int>{});
    }

    AvatarDescription AvatarDescription::CreateRandom(AvatarBodyType bodyType)
    {
        int value = static_cast<int>(bodyType);
        if (value < 0 || value > 1)
        {
            throw System::ArgumentOutOfRangeException("bodyType");
        }
        return CreateRandom(std::optional<int>{value});
    }

    AvatarDescription AvatarDescription::CreateRandom(std::optional<int> bodyType)
    {
        namespace Avatars = CNA::Internal::GamerServices::Avatars;
        static std::mutex lock;
        static std::mt19937 random{std::random_device{}()};
        Avatars::AvatarDescriptor descriptor;
        {
            std::lock_guard guard(lock);
            descriptor = Avatars::randomDescriptor(
                bodyType ? std::optional<std::uint8_t>(static_cast<std::uint8_t>(*bodyType)) : std::nullopt, random);
        }
        return AvatarDescription(Avatars::encode(descriptor), false);
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
