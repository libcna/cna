// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/ServiceUpdateSubscription.hpp"
#include "../Internal/ServiceAsyncResult.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include <array>
#include <chrono>
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
            AvatarDescriptionAsyncResult(std::any state, std::vector<unsigned char> description, int playerIndex = -1,
                                         std::string identity = {})
                : asyncState_(std::move(state))
                , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
                , description_(std::move(description))
                , playerIndex_(playerIndex)
                , identity_(std::move(identity))
            {
            }

            [[nodiscard]] const std::vector<unsigned char>& description() const { return description_; }
            [[nodiscard]] int playerIndex() const { return playerIndex_; }
            [[nodiscard]] const std::string& identity() const { return identity_; }

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
            std::vector<unsigned char> description_;
            int playerIndex_;
            std::string identity_;
        };

        // A service read of a signed-in gamer's avatar: the bytes, and whose slot they are for.
        struct ServiceAvatar
        {
            std::vector<unsigned char> bytes;
            long long revision = 0;
            int playerIndex = -1;
            std::string identity;
        };

        // XNA keeps one description per signed-in player index: EndGetFromGamer returns it until
        // that gamer's avatar changes, when its Changed is raised and the slot is emptied
        // (AvatarDescription.OnAvatarChanged, GamerServicesDispatcher.ReadAvatarChanged). The
        // description's Changed is shared, so every copy handed out is that one object's event.
        struct CachedSlot
        {
            std::optional<AvatarDescription> description;
            std::string identity;
            bool service = false;
            long long revision = 0;
            std::chrono::steady_clock::time_point nextCheck;
            bool checking = false;
        };
        std::array<CachedSlot, 4> cache;
        std::weak_ptr<CNA::Internal::GamerServices::IGamerServicesBackend> cacheBackend;
        std::unique_ptr<CNA::Internal::GamerServices::ServiceUpdateSubscription> changeWatch;
        std::chrono::milliseconds checkInterval{10000};

        SignedInGamer* SignedInAt(int index)
        {
            const auto* gamers = Gamer::getSignedInGamersProperty();
            return gamers ? (*gamers)[static_cast<Microsoft::Xna::Framework::PlayerIndex>(index)] : nullptr;
        }

        std::string IdentityOf(const SignedInGamer& gamer)
        {
            const auto userId = CNA::Internal::GamerServices::GamerAccess::userId(gamer);
            return userId.empty() ? "local:" + gamer.getGamertagProperty() : "service:" + userId;
        }

        void RaiseChanged(int index, SignedInGamer& gamer)
        {
            // Empty the slot first: a handler reading the avatar again gets a new description.
            AvatarDescription changed = *cache[static_cast<std::size_t>(index)].description;
            cache[static_cast<std::size_t>(index)] = CachedSlot{};
            changed.Changed.Raise(&gamer, System::EventArgs::Empty);
        }

        // Owner thread, each GamerServicesDispatcher.Update: read cached gamers' avatars again now
        // and then, and report the ones that differ.
        void CheckCachedAvatars()
        {
            namespace Service = CNA::Internal::GamerServices;
            auto current = Service::backend();
            if (cacheBackend.lock() != current)
            {
                cache = {};
                cacheBackend = current;
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            for (int index = 0; index < 4; ++index)
            {
                auto& slot = cache[static_cast<std::size_t>(index)];
                if (!slot.description)
                {
                    continue;
                }
                auto* gamer = SignedInAt(index);
                if (gamer == nullptr || IdentityOf(*gamer) != slot.identity)
                {
                    // Signed out, or another gamer in the slot: not a change of this avatar.
                    slot = CachedSlot{};
                    continue;
                }
                if (slot.checking || now < slot.nextCheck)
                {
                    continue;
                }
                slot.nextCheck = now + checkInterval;
                const auto known = slot.description->getDescriptionProperty();
                if (!slot.service)
                {
                    const auto stored = Service::localProfileAvatar(gamer->getGamertagProperty());
                    if (stored.size() == known.size() && !std::equal(stored.begin(), stored.end(), known.begin()))
                    {
                        RaiseChanged(index, *gamer);
                    }
                    continue;
                }
                if (!current->serviceEnabled())
                {
                    continue;
                }
                slot.checking = true;
                auto fetched = std::make_shared<std::optional<Service::ServiceAvatarRecord>>();
                const auto userId = Service::GamerAccess::userId(*gamer);
                const std::weak_ptr<Service::IGamerServicesBackend> origin = current;
                current->submit(
                    [fetched, userId, origin] {
                        try
                        {
                            if (auto service = origin.lock())
                            {
                                *fetched = service->avatars({userId}).at(0);
                            }
                        }
                        catch (...)
                        {
                            // Unreachable or failed: no news, ask again later.
                        }
                    },
                    [fetched, index, identity = slot.identity, known, revision = slot.revision] {
                        auto& again = cache[static_cast<std::size_t>(index)];
                        if (!again.description || again.identity != identity)
                        {
                            return;
                        }
                        again.checking = false;
                        auto* owner = SignedInAt(index);
                        if (!*fetched || owner == nullptr || IdentityOf(*owner) != identity)
                        {
                            return;
                        }
                        // The stored avatar's revision says whether it changed; the bytes can differ
                        // without a change (a catalog installed since, a projection no longer needed).
                        const auto& bytes = (*fetched)->description;
                        const bool same = revision != 0 && (*fetched)->revision != 0
                            ? revision == (*fetched)->revision
                            : bytes.empty() ? std::none_of(known.begin(), known.end(), [](auto b) { return b != 0; })
                                            : bytes.size() == known.size() && std::equal(bytes.begin(), bytes.end(), known.begin());
                        if (!same)
                        {
                            RaiseChanged(index, *owner);
                        }
                    });
            }
        }

        AvatarDescription Cached(int index, const std::string& identity, bool service, std::vector<unsigned char> bytes,
                                 long long revision = 0)
        {
            auto* gamer = SignedInAt(index);
            if (index < 0 || index > 3 || gamer == nullptr || IdentityOf(*gamer) != identity)
            {
                return AvatarDescription(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
            }
            auto& slot = cache[static_cast<std::size_t>(index)];
            if (!slot.description || slot.identity != identity)
            {
                cacheBackend = CNA::Internal::GamerServices::backend();
                slot = CachedSlot{};
                slot.description.emplace(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
                slot.description->Changed.Share();
                slot.identity = identity;
                slot.service = service;
                slot.revision = revision;
                slot.nextCheck = std::chrono::steady_clock::now() + checkInterval;
                if (!changeWatch)
                {
                    changeWatch = std::make_unique<CNA::Internal::GamerServices::ServiceUpdateSubscription>(CheckCachedAvatars);
                }
            }
            return *slot.description;
        }
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

        // A gamer with a service identity (signed in, or met in an online session) has its
        // avatar read from the service, and a signed-in local profile has the one stored with the
        // profile; anyone else has none. A signed-in gamer's slot keeps the description it got
        // until that avatar changes.
        const auto userId = CNA::Internal::GamerServices::GamerAccess::userId(*gamer);
        auto service = CNA::Internal::GamerServices::backend();
        const auto* signedIn = dynamic_cast<const SignedInGamer*>(gamer);
        const int playerIndex = signedIn != nullptr ? static_cast<int>(signedIn->getPlayerIndexProperty()) : -1;
        const std::string identity = signedIn != nullptr ? IdentityOf(*signedIn) : std::string();
        if (playerIndex >= 0 && playerIndex < 4)
        {
            const auto& slot = cache[static_cast<std::size_t>(playerIndex)];
            if (slot.description && slot.identity == identity && cacheBackend.lock() == service)
            {
                const auto bytes = slot.description->getDescriptionProperty();
                auto* result = new AvatarDescriptionAsyncResult(std::move(state), std::vector<unsigned char>(bytes.begin(), bytes.end()),
                                                                playerIndex, identity);
                if (callback)
                {
                    callback(*result);
                }
                return result;
            }
        }
        if (service->serviceEnabled() && !userId.empty())
        {
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin(
                "avatar", nullptr,
                [userId, playerIndex, identity](auto& executor) -> std::any {
                    auto record = executor.avatars({userId}).at(0);
                    return ServiceAvatar{std::move(record.description), record.revision, playerIndex, identity};
                },
                std::move(callback), std::move(state), std::move(service));
        }

        std::vector<unsigned char> local;
        if (signedIn != nullptr && userId.empty() && !signedIn->getIsSignedInToLiveProperty() && !signedIn->getIsGuestProperty())
        {
            local = CNA::Internal::GamerServices::localProfileAvatar(signedIn->getGamertagProperty());
        }
        auto* result = new AvatarDescriptionAsyncResult(std::move(state), std::move(local), playerIndex, identity);
        if (callback)
        {
            callback(*result);
        }
        return result;
    }

    AvatarDescription AvatarDescription::EndGetFromGamer(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result) != nullptr)
        {
            ServiceAvatar read;
            try
            {
                read = std::any_cast<ServiceAvatar>(CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "avatar", nullptr));
            }
            catch (const System::ArgumentException&)
            {
                throw;
            }
            catch (const System::InvalidOperationException&)
            {
                throw;
            }
            catch (const std::exception&)
            {
                // An unreachable service reads as "no avatar", as it would for a gamer without one;
                // it is not kept, so the next read asks again.
                return AvatarDescription(std::vector<SharpRuntime::bytecs>(DescriptionSize, 0), false);
            }
            auto bytes = static_cast<int>(read.bytes.size()) == DescriptionSize ? read.bytes
                                                                                 : std::vector<unsigned char>(DescriptionSize, 0);
            if (read.playerIndex >= 0)
            {
                return Cached(read.playerIndex, read.identity, true, std::move(bytes), read.revision);
            }
            return AvatarDescription(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()), false);
        }

        const auto* local = dynamic_cast<AvatarDescriptionAsyncResult*>(result);
        if (local == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginGetFromGamer.");
        }
        // Without a service identity or local profile there is no avatar: an all-zero (invalid) description.
        auto bytes = static_cast<int>(local->description().size()) == DescriptionSize ? local->description()
                                                                                        : std::vector<unsigned char>(DescriptionSize, 0);
        if (local->playerIndex() >= 0)
        {
            return Cached(local->playerIndex(), local->identity(), local->identity().starts_with("service:"), std::move(bytes));
        }
        return AvatarDescription(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()), false);
    }
}

namespace CNA::Internal::GamerServices::Avatars
{
    void setAvatarChangeCheckInterval(std::chrono::milliseconds interval)
    {
        Microsoft::Xna::Framework::GamerServices::checkInterval = interval;
    }
}
