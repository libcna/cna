// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "System/AsyncCallback.hpp"
#include "System/IAsyncResult.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <any>
#include <optional>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamerCollection;
    class GamerProfile;

    /**
     * @brief Represents a gamer profile on the platform.
     *
     * Abstract base class for all gamer types in the XNA GamerServices API.
     */
    class Gamer
    {
    public:
        /** @brief Virtual destructor for safe polymorphic destruction. */
        virtual ~Gamer() = default;

        /**
         * @brief Gets the display name of the gamer.
         *
         * @return The display name string.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const;

        /**
         * @brief Sets the display name of the gamer.
         *
         * @param value The new display name.
         */
        void setDisplayNameProperty(const std::string& value);

        /**
         * @brief Gets the gamertag of the gamer.
         *
         * @return The gamertag string.
         */
        [[nodiscard]] const std::string& getGamertagProperty() const;

        /**
         * @brief Gets whether this gamer has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the object used to read leaderboard entries for this gamer.
         *
         * @return Reference to the LeaderboardWriter.
         */
        [[nodiscard]] LeaderboardWriter& getLeaderboardWriterProperty();

        /**
         * @brief Gets the user-defined tag object attached to this gamer.
         *
         * @return Reference to the tag value.
         */
        [[nodiscard]] std::any& getTagProperty();

        /**
         * @brief Sets the user-defined tag object attached to this gamer.
         *
         * @param value The tag value.
         */
        void setTagProperty(const std::any& value);

        /**
         * @brief Gets the collection of currently signed-in gamers.
         *
         * @return Pointer to the signed-in gamer collection.
         */
        static SignedInGamerCollection* getSignedInGamersProperty();

        /**
         * @brief Sets the collection of currently signed-in gamers.
         *
         * C# `internal set`; exposed here as a plain public setter since C++ has no
         * assembly-internal access modifier, matching the precedent set by
         * GamerServicesDispatcher::UpdateAsync().
         *
         * @param value The new signed-in gamer collection; ownership transfers to Gamer.
         */
        static void setSignedInGamersProperty(SignedInGamerCollection* value);

        /**
         * @brief Returns the display name of this gamer.
         *
         * @return The display name string.
         */
        [[nodiscard]] virtual std::string ToString() const;

        /**
         * @brief Synchronously retrieves the profile for this gamer.
         *
         * @return Heap-allocated GamerProfile; caller owns it and must Dispose()/delete it.
         */
        [[nodiscard]] GamerProfile* GetProfile();

        /**
         * @brief Begins an asynchronous request for this gamer's profile.
         *
         * @param callback   Invoked when the operation completes (may be an empty std::function).
         * @param asyncState User-defined state passed through to the callback.
         * @return Heap-allocated IAsyncResult; caller owns it and must delete it after EndGetProfile.
         */
        [[nodiscard]] System::IAsyncResult* BeginGetProfile(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous request for this gamer's profile.
         *
         * @param result The result returned by BeginGetProfile.
         * @return Heap-allocated GamerProfile; caller owns it and must Dispose()/delete it.
         */
        [[nodiscard]] GamerProfile* EndGetProfile(System::IAsyncResult* result);

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param gamertag The gamertag to look up.
         * @return Never returns.
         */
        [[nodiscard]] static Gamer* GetFromGamertag(const std::string& gamertag);

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param gamertag   The gamertag to look up.
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return Never returns.
         */
        [[nodiscard]] static System::IAsyncResult* BeginGetFromGamertag(
            const std::string& gamertag,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param result The result returned by BeginGetFromGamertag.
         * @return Never returns.
         */
        [[nodiscard]] static Gamer* EndGetFromGamertag(System::IAsyncResult* result);

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param audienceUri The audience URI to request a token for.
         * @return Never returns.
         */
        [[nodiscard]] static std::string GetPartnerToken(const std::string& audienceUri);

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param audienceUri The audience URI to request a token for.
         * @param callback    Invoked when the operation completes.
         * @param asyncState  User-defined state passed through to the callback.
         * @return Never returns.
         */
        [[nodiscard]] static System::IAsyncResult* BeginGetPartnerToken(
            const std::string& audienceUri,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Not supported in this platform's implementation.
         *
         * @param result The result returned by BeginGetPartnerToken.
         * @return Never returns.
         */
        [[nodiscard]] static std::string EndGetPartnerToken(System::IAsyncResult* result);

    protected:
        /**
         * @brief Constructs a Gamer with the given gamertag and display name.
         *
         * Task 10.1: matches FNA's own `DisplayName = displayName ?? gamertag` exactly - the
         * fallback applies only when displayName is omitted (C#'s implicit `null`), never for an
         * explicitly-supplied empty string, which is preserved as-is. A plain empty-string
         * sentinel (as this constructor previously used) cannot make that distinction, since
         * std::string has no null state; std::optional<std::string> models C#'s nullable
         * `string displayName = null` default precisely.
         *
         * @param gamertag    The gamertag string.
         * @param displayName The display name; when omitted, falls back to gamertag. An
         * explicitly-passed empty string is kept as empty.
         */
        Gamer(const std::string& gamertag, std::optional<std::string> displayName = std::nullopt);

        /**
         * @brief Internal IAsyncResult implementation backing Gamer's asynchronous Begin/End methods.
         */
        class GamerAction : public System::IAsyncResult
        {
        public:
            /**
             * @brief Constructs a GamerAction already positioned to complete.
             *
             * @param state    The user-defined async state.
             * @param callback The callback to invoke on completion.
             */
            GamerAction(std::any state, System::AsyncCallback callback);

            /**
             * @brief Gets the user-defined state supplied to the Begin* call.
             *
             * @return The async state value.
             */
            [[nodiscard]] const std::any& getAsyncStateProperty() const override;

            /** @brief Always false; this stub never completes synchronously. */
            [[nodiscard]] bool getCompletedSynchronouslyProperty() const override;

            /** @brief Gets whether the asynchronous operation has completed. */
            [[nodiscard]] bool getIsCompletedProperty() const override;

            /**
             * @brief Sets whether the asynchronous operation has completed.
             *
             * @param value The new completion state.
             */
            void setIsCompletedProperty(bool value);

            /**
             * @brief Gets the wait handle signalled when the operation completes.
             *
             * @return Reference to the wait handle.
             */
            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override;

            /** @brief The callback to invoke when the operation completes. */
            const System::AsyncCallback Callback;

        private:
            std::any asyncState_;
            bool isCompleted_{false};

            // Mutable: IAsyncResult::getAsyncWaitHandleProperty() is const but returns a
            // non-const WaitHandle&, so the handle exposed through it must be mutable.
            mutable System::Threading::EventWaitHandle asyncWaitHandle_;
        };

        std::string displayName_;
        std::string gamertag_;
        bool isDisposed_{false};
        std::any tag_;
        // Task 4.3 (plans/plan_net.md Phase 4): LeaderboardWriter captures `this` at construction time
        // (see Gamer.cpp's constructor) and neither Gamer nor LeaderboardWriter declares a custom
        // copy/move constructor, so that captured pointer is copied verbatim - not re-pointed - by
        // any copy or move of a constructed Gamer/SignedInGamer, including an ordinary
        // std::vector<SignedInGamer>::push_back(prvalue). Once a Gamer-derived object's
        // LeaderboardWriter may be used, that object's address must never change again; prefer
        // heap allocation (see cna_demo_leaderboard_viewer's own syntheticGamers_ for the pattern
        // that keeps a batch of them at stable addresses) over by-value containers that can move
        // or reallocate their elements.
        LeaderboardWriter leaderboardWriter_;

    private:
        static SignedInGamerCollection* signedInGamers_;
    };
}
