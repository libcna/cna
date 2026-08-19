// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGameComponentsDetail.hpp"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/EventHandler.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateOwnedCanonicalGameComponent;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedSpriteBatchValue;
using CNA::C::Detail::GetOwnedSpriteFont;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ValidateCanonicalBool;

using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::GamerServicesComponent;
using Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher;
using Microsoft::Xna::Framework::GamerServices::Guide;
using Microsoft::Xna::Framework::GamerServices::MessageBoxIcon;
using Microsoft::Xna::Framework::GamerServices::NotificationPosition;

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

// The canonical begin routes hand back a raw operation the caller owns and must delete, and only one
// of each kind may be pending at a time. Since no operation object crosses this ABI, the C layer
// keeps the one operation itself: that is what lets the end routes take no argument, and what stops
// the operation leaking when a caller never asks for its answer.
std::unique_ptr<System::IAsyncResult>& PendingKeyboardInput()
{
    static std::unique_ptr<System::IAsyncResult> pending;
    return pending;
}

std::unique_ptr<System::IAsyncResult>& PendingMessageBox()
{
    static std::unique_ptr<System::IAsyncResult> pending;
    return pending;
}

class GuideRegistrationBase {
public:
    GuideRegistrationBase() = default;
    GuideRegistrationBase(const GuideRegistrationBase&) = delete;
    GuideRegistrationBase& operator=(const GuideRegistrationBase&) = delete;
    virtual ~GuideRegistrationBase() = default;
};

class GuideRegistration final : public GuideRegistrationBase {
public:
    using Source = System::EventHandler<System::EventArgs>;
    using Token = Source::Token;

    GuideRegistration(Source* const source, const Token token)
        : source_(source)
        , token_(token)
    {
    }

    ~GuideRegistration() override
    {
        source_->Remove(token_);
    }

private:
    Source* source_;
    Token token_;
};

[[nodiscard]] CNA_Result CopyGuideText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The guide text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the guide text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (view.data == nullptr && view.byte_length != UINT64_C(0)) {
        return InvalidInput(message);
    }
    outText->assign(
        view.data == nullptr ? "" : view.data,
        static_cast<std::size_t>(view.byte_length));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool TryMapPlayerIndex(
    const CNA_PlayerIndex index,
    PlayerIndex* const outIndex) noexcept
{
    if (index > CNA_PLAYER_INDEX_FOUR) {
        return false;
    }
    *outIndex = static_cast<PlayerIndex>(index);
    return true;
}

[[nodiscard]] CNA_Result ValidateBoolean(const CNA_Bool value, const char* const message)
{
    if (value != CNA_FALSE && value != CNA_TRUE) {
        return InvalidInput(message);
    }
    return CNA_RESULT_SUCCESS;
}

// A guide screen takes a player index and does nothing with it on this runtime; validating it here
// is still the boundary's job, so a caller learns about a bad identity rather than having it ignored.
[[nodiscard]] CNA_Result ValidatePlayer(const CNA_PlayerIndex player, PlayerIndex* const outPlayer)
{
    if (!TryMapPlayerIndex(player, outPlayer)) {
        return InvalidInput("The player index is not a defined identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowGamerForGuide(const CNA_Handle handle, Gamer** const outGamer)
{
    return CNA::C::Detail::BorrowAnyGamer(handle, outGamer);
}

[[nodiscard]] CNA_Result CollectRecipients(
    const CNA_GamerHandle* const recipients,
    const uint64_t count,
    std::vector<Gamer*>* const outRecipients)
{
    if (recipients == nullptr && count != UINT64_C(0)) {
        return InvalidInput("The recipient array is null.");
    }
    outRecipients->reserve(static_cast<std::size_t>(count));
    for (uint64_t index = UINT64_C(0); index < count; ++index) {
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerForGuide(recipients[index], &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outRecipients->push_back(gamer);
    }
    return CNA_RESULT_SUCCESS;
}

// Both renderers take the same four surfaces, so they resolve them the same way.
struct GuideRenderSurfaces final {
    std::shared_ptr<BorrowedGraphicsDevice> device;
    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch = nullptr;
    std::shared_ptr<SpriteFontResource> font;
    std::shared_ptr<Texture2DResource> whitePixel;
};

[[nodiscard]] CNA_Result BorrowRenderSurfaces(
    const CNA_Handle deviceHandle,
    const CNA_Handle spriteBatchHandle,
    const CNA_Handle fontHandle,
    const CNA_Handle whitePixelHandle,
    GuideRenderSurfaces* const outSurfaces)
{
    if (const CNA_Result result = GetBorrowedGraphicsDevice(deviceHandle, &outSurfaces->device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            GetOwnedSpriteBatchValue(spriteBatchHandle, &outSurfaces->spriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = GetOwnedSpriteFont(fontHandle, &outSurfaces->font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return GetOwnedTexture2D(whitePixelHandle, &outSurfaces->whitePixel);
}

[[nodiscard]] CNA_Result PublishGuideRegistration(
    std::shared_ptr<GuideRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::GamerEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The gamer-services registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_guide_get_is_screen_saver_enabled(CNA_Bool* const outIsEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsEnabled == nullptr) {
            return InvalidInput("The screen-saver output is null.");
        }
        *outIsEnabled = Guide::getIsScreenSaverEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_set_is_screen_saver_enabled(const CNA_Bool isEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(isEnabled, "is_enabled");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateBoolean(isEnabled, "The screen-saver flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::setIsScreenSaverEnabledProperty(isEnabled == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_is_trial_mode(CNA_Bool* const outIsTrialMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsTrialMode == nullptr) {
            return InvalidInput("The trial-mode output is null.");
        }
        *outIsTrialMode = Guide::getIsTrialModeProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_set_is_trial_mode(const CNA_Bool isTrialMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(isTrialMode, "is_trial_mode");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateBoolean(isTrialMode, "The trial-mode flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::setIsTrialModeProperty(isTrialMode == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_is_visible(CNA_Bool* const outIsVisible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsVisible == nullptr) {
            return InvalidInput("The guide-visibility output is null.");
        }
        *outIsVisible = Guide::getIsVisibleProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_set_is_visible(const CNA_Bool isVisible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(isVisible, "is_visible");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateBoolean(isVisible, "The guide-visibility flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::setIsVisibleProperty(isVisible == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_notification_position(CNA_NotificationPosition* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPosition == nullptr) {
            return InvalidInput("The notification-position output is null.");
        }
        *outPosition = static_cast<CNA_NotificationPosition>(Guide::getNotificationPositionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_set_notification_position(const CNA_NotificationPosition position)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (position > CNA_NOTIFICATION_POSITION_MAXIMUM) {
            return InvalidInput("The notification position is not a defined identity.");
        }
        Guide::setNotificationPositionProperty(static_cast<NotificationPosition>(position));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_simulate_trial_mode(CNA_Bool* const outSimulate)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSimulate == nullptr) {
            return InvalidInput("The simulate-trial-mode output is null.");
        }
        *outSimulate = Guide::getSimulateTrialModeProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_set_simulate_trial_mode(const CNA_Bool simulate)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(simulate, "simulate");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateBoolean(simulate, "The simulate-trial-mode flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::setSimulateTrialModeProperty(simulate == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_begin_show_keyboard_input(
    const CNA_PlayerIndex player,
    const CNA_StringView title,
    const CNA_StringView description,
    const CNA_StringView defaultText,
    const CNA_Bool usePasswordMode,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(usePasswordMode, "use_password_mode");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateBoolean(usePasswordMode, "The password-mode flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeTitle;
        std::string nativeDescription;
        std::string nativeDefaultText;
        if (const CNA_Result result = ToNativeText(title, "The title is invalid.", &nativeTitle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(description, "The description is invalid.", &nativeDescription);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(defaultText, "The default text is invalid.", &nativeDefaultText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::AsyncCallback nativeCallback;
        if (callback != nullptr) {
            nativeCallback = [callback, context](System::IAsyncResult&) { callback(context); };
        }
        // The canonical route refuses a second pending input, so the previous one is only released
        // once this one has actually been accepted.
        auto* const action = Guide::BeginShowKeyboardInput(
            nativePlayer,
            nativeTitle,
            nativeDescription,
            nativeDefaultText,
            std::move(nativeCallback),
            std::any{},
            usePasswordMode == CNA_TRUE);
        PendingKeyboardInput().reset(action);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_end_show_keyboard_input_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The keyboard-input size output is null.");
        }
        if (PendingKeyboardInput() == nullptr) {
            return InvalidState("No keyboard input has been started.");
        }
        *outBytes = Guide::EndShowKeyboardInput(PendingKeyboardInput().get()).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_end_show_keyboard_input(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (PendingKeyboardInput() == nullptr) {
            return InvalidState("No keyboard input has been started.");
        }
        return CopyGuideText(
            Guide::EndShowKeyboardInput(PendingKeyboardInput().get()),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_guide_get_has_pending_keyboard_input_ext(CNA_Bool* const outHasPending)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasPending == nullptr) {
            return InvalidInput("The pending keyboard-input output is null.");
        }
        *outHasPending = Guide::getHasPendingKeyboardInputEXTProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_was_keyboard_input_canceled_ext(CNA_Bool* const outWasCanceled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWasCanceled == nullptr) {
            return InvalidInput("The keyboard-cancellation output is null.");
        }
        if (PendingKeyboardInput() == nullptr) {
            return InvalidState("No keyboard input has been started.");
        }
        *outWasCanceled =
            Guide::WasKeyboardInputCanceledEXT(PendingKeyboardInput().get()) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_pending_keyboard_input_title_size_ext(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The pending title size output is null.");
        }
        *outBytes = Guide::GetPendingKeyboardInputTitleForTestingEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_copy_pending_keyboard_input_title_ext(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyGuideText(
            Guide::GetPendingKeyboardInputTitleForTestingEXT(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_guide_get_pending_keyboard_input_description_size_ext(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The pending description size output is null.");
        }
        *outBytes = Guide::GetPendingKeyboardInputDescriptionForTestingEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_copy_pending_keyboard_input_description_ext(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyGuideText(
            Guide::GetPendingKeyboardInputDescriptionForTestingEXT(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_guide_get_pending_keyboard_input_display_text_size_ext(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The pending display-text size output is null.");
        }
        *outBytes = Guide::GetPendingKeyboardInputDisplayTextForTestingEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_copy_pending_keyboard_input_display_text_ext(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyGuideText(
            Guide::GetPendingKeyboardInputDisplayTextForTestingEXT(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_guide_render_pending_keyboard_input_ext(
    const CNA_Handle deviceHandle,
    const CNA_Handle spriteBatchHandle,
    const CNA_Handle fontHandle,
    const CNA_Handle whitePixelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GuideRenderSurfaces surfaces;
        if (const CNA_Result result = BorrowRenderSurfaces(
                deviceHandle,
                spriteBatchHandle,
                fontHandle,
                whitePixelHandle,
                &surfaces);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::RenderPendingKeyboardInputEXT(
            *surfaces.device->value,
            *surfaces.spriteBatch,
            *surfaces.font->value,
            *surfaces.whitePixel->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_simulate_keyboard_input_cancel_ext(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Guide::SimulateKeyboardInputCancelEXT();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_reset_pending_keyboard_input_ext(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Guide::ResetPendingKeyboardInputForTestingEXT();
        // The operation is gone as far as the canonical guide is concerned, so the C layer drops its
        // own hold too rather than leaving an answer nobody can complete.
        PendingKeyboardInput().reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_begin_show_message_box(
    const CNA_PlayerIndex player,
    const CNA_StringView title,
    const CNA_StringView text,
    const CNA_StringView* const buttons,
    const uint64_t buttonCount,
    const int32_t focusButton,
    const CNA_MessageBoxIcon icon,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (icon > CNA_MESSAGE_BOX_ICON_MAXIMUM) {
            return InvalidInput("The message-box icon is not a defined identity.");
        }
        if (buttons == nullptr && buttonCount != UINT64_C(0)) {
            return InvalidInput("The message-box button array is null.");
        }
        std::string nativeTitle;
        std::string nativeText;
        if (const CNA_Result result = ToNativeText(title, "The title is invalid.", &nativeTitle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeText(text, "The body text is invalid.", &nativeText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<std::string> nativeButtons;
        nativeButtons.reserve(static_cast<std::size_t>(buttonCount));
        for (uint64_t index = UINT64_C(0); index < buttonCount; ++index) {
            std::string caption;
            if (const CNA_Result result =
                    ToNativeText(buttons[index], "A button caption is invalid.", &caption);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            nativeButtons.push_back(std::move(caption));
        }
        System::AsyncCallback nativeCallback;
        if (callback != nullptr) {
            nativeCallback = [callback, context](System::IAsyncResult&) { callback(context); };
        }
        auto* const action = Guide::BeginShowMessageBox(
            nativePlayer,
            nativeTitle,
            nativeText,
            nativeButtons,
            static_cast<int>(focusButton),
            static_cast<MessageBoxIcon>(icon),
            std::move(nativeCallback),
            std::any{});
        PendingMessageBox().reset(action);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_end_show_message_box(
    CNA_Bool* const outHasChoice,
    int32_t* const outButtonIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasChoice == nullptr || outButtonIndex == nullptr) {
            return InvalidInput("The message-box answer output is invalid.");
        }
        *outHasChoice = CNA_FALSE;
        if (PendingMessageBox() == nullptr) {
            return InvalidState("No message box has been started.");
        }
        const std::optional<int> selected = Guide::EndShowMessageBox(PendingMessageBox().get());
        if (!selected.has_value()) {
            return CNA_RESULT_SUCCESS;
        }
        *outHasChoice = CNA_TRUE;
        *outButtonIndex = static_cast<int32_t>(*selected);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_has_pending_message_box_ext(CNA_Bool* const outHasPending)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasPending == nullptr) {
            return InvalidInput("The pending message-box output is null.");
        }
        *outHasPending = Guide::getHasPendingMessageBoxEXTProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_get_pending_message_box_focus_button_ext(int32_t* const outFocusButton)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFocusButton == nullptr) {
            return InvalidInput("The focus-button output is null.");
        }
        if (!Guide::getHasPendingMessageBoxEXTProperty()) {
            return InvalidState("No message box is currently pending.");
        }
        *outFocusButton =
            static_cast<int32_t>(Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_render_pending_message_box_ext(
    const CNA_Handle deviceHandle,
    const CNA_Handle spriteBatchHandle,
    const CNA_Handle fontHandle,
    const CNA_Handle whitePixelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GuideRenderSurfaces surfaces;
        if (const CNA_Result result = BorrowRenderSurfaces(
                deviceHandle,
                spriteBatchHandle,
                fontHandle,
                whitePixelHandle,
                &surfaces);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::RenderPendingMessageBoxEXT(
            *surfaces.device->value,
            *surfaces.spriteBatch,
            *surfaces.font->value,
            *surfaces.whitePixel->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_simulate_message_box_click_ext(const int32_t buttonIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Guide::SimulateMessageBoxClickEXT(static_cast<int>(buttonIndex));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_reset_pending_message_box_ext(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Guide::ResetPendingMessageBoxForTestingEXT();
        PendingMessageBox().reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_delay_notifications(const int64_t delayTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Guide::DelayNotifications(System::TimeSpan(delayTicks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_compose_message(
    const CNA_PlayerIndex player,
    const CNA_StringView text,
    const CNA_GamerHandle* const recipients,
    const uint64_t recipientCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeText;
        if (const CNA_Result result = ToNativeText(text, "The message text is invalid.", &nativeText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Gamer*> nativeRecipients;
        if (const CNA_Result result =
                CollectRecipients(recipients, recipientCount, &nativeRecipients);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowComposeMessage(nativePlayer, nativeText, nativeRecipients);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_friend_request(
    const CNA_PlayerIndex player,
    const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerForGuide(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowFriendRequest(nativePlayer, gamer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_friends(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowFriends(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_game_invite(
    const CNA_PlayerIndex player,
    const CNA_GamerHandle* const recipients,
    const uint64_t recipientCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Gamer*> nativeRecipients;
        if (const CNA_Result result =
                CollectRecipients(recipients, recipientCount, &nativeRecipients);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowGameInvite(nativePlayer, nativeRecipients);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_game_invite_for_session(const CNA_StringView sessionId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeSessionId;
        if (const CNA_Result result =
                ToNativeText(sessionId, "The session identifier is invalid.", &nativeSessionId);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowGameInvite(nativeSessionId);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_gamer_card(
    const CNA_PlayerIndex player,
    const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerForGuide(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowGamerCard(nativePlayer, gamer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_marketplace(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowMarketplace(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_messages(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowMessages(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_party(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowParty(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_party_sessions(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowPartySessions(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_player_review(
    const CNA_PlayerIndex player,
    const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerForGuide(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowPlayerReview(nativePlayer, gamer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_players(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowPlayers(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_sign_in(const int32_t paneCount, const CNA_Bool onlineOnly)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(onlineOnly, "online_only");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateBoolean(onlineOnly, "The online-only flag is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowSignIn(static_cast<int>(paneCount), onlineOnly == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_guide_show_achievements_ext(const CNA_PlayerIndex player)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayer = PlayerIndex::One;
        if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Guide::ShowAchievementsEXT(nativePlayer);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_get_is_initialized(CNA_Bool* const outIsInitialized)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsInitialized == nullptr) {
            return InvalidInput("The dispatcher initialization output is null.");
        }
        *outIsInitialized = GamerServicesDispatcher::getIsInitializedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_get_window_handle(uint64_t* const outWindowHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWindowHandle == nullptr) {
            return InvalidInput("The window-handle output is null.");
        }
        *outWindowHandle =
            static_cast<uint64_t>(GamerServicesDispatcher::getWindowHandleProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_set_window_handle(const uint64_t windowHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GamerServicesDispatcher::setWindowHandleProperty(
            static_cast<SharpRuntime::IntPtr>(windowHandle));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_initialize(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::Game* game = nullptr;
        if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GamerServicesDispatcher::Initialize(game->getServicesProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_update(void)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GamerServicesDispatcher::Update();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_update_async(CNA_Bool* const outDidWork)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDidWork == nullptr) {
            return InvalidInput("The dispatcher work output is null.");
        }
        *outDidWork = GamerServicesDispatcher::UpdateAsync() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_get_freed_gamer_count_ext(uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The freed-gamer count output is null.");
        }
        *outCount = static_cast<uint64_t>(GamerServicesDispatcher::GetFreedGamerCountForTesting());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_services_dispatcher_subscribe_installing_title_update_ext(
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The gamer registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The gamer event callback is null.");
        }
        auto* const source = &GamerServicesDispatcher::InstallingTitleUpdate;
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        return PublishGuideRegistration(
            std::make_shared<GuideRegistration>(source, token),
            outRegistration);
    });
}

CNA_Result cna_gamer_services_component_create(
    const CNA_Handle gameHandle,
    CNA_Handle* const outComponent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outComponent == nullptr) {
            return InvalidInput("The component output handle is null.");
        }
        *outComponent = CNA_INVALID_HANDLE;
        Microsoft::Xna::Framework::Game* game = nullptr;
        if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateOwnedCanonicalGameComponent(
            gameHandle,
            std::make_unique<GamerServicesComponent>(*game),
            outComponent);
    });
}
