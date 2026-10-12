// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGameComponentsDetail.hpp"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiDetail.hpp"

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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateOwnedCanonicalGameComponent;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedSpriteBatchValue;
using CNA::C::Detail::GetOwnedSpriteFont;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::TryMapPlayerIndex;
using CNA::C::Detail::ValidateCanonicalBool;

using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::GamerServicesComponent;
using Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher;
using Microsoft::Xna::Framework::GamerServices::Guide;
using Microsoft::Xna::Framework::GamerServices::MessageBoxIcon;
using Microsoft::Xna::Framework::GamerServices::NotificationPosition;

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

// Guide End runs once per result, as the reference allows, while the C calls read an answer as often
// as they like; the first End's answer is kept until the result is replaced or reset.
std::optional<std::string>& EndedKeyboardInput()
{
    static std::optional<std::string> text;
    return text;
}

std::optional<std::optional<int>>& EndedMessageBox()
{
    static std::optional<std::optional<int>> choice;
    return choice;
}

const std::string& KeyboardInputAnswer()
{
    if (!EndedKeyboardInput()) EndedKeyboardInput() = Guide::EndShowKeyboardInput(PendingKeyboardInput().get());
    return *EndedKeyboardInput();
}

std::optional<int> MessageBoxAnswer()
{
    if (!EndedMessageBox()) EndedMessageBox() = Guide::EndShowMessageBox(PendingMessageBox().get());
    return *EndedMessageBox();
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

// Validating a guide screen's player index is the boundary's job, so a caller learns about a bad
// identity as an argument failure before the canonical screen sees it.
[[nodiscard]] CNA_Result ValidatePlayer(const CNA_PlayerIndex player, PlayerIndex* const outPlayer)
{
    if (!TryMapPlayerIndex(player, outPlayer)) {
        return InvalidArgument("The player index is not a defined identity.");
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
        return InvalidArgument("The recipient array is null.");
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
    return CreateHandle(ObjectKind::GamerEventRegistration,
        std::move(registration),
        outRegistration);
}

[[nodiscard]] CNA_Result GuideBeginShowKeyboardInput(
    const CNA_PlayerIndex player,
    const CNA_StringView title,
    const CNA_StringView description,
    const CNA_StringView defaultText,
    const CNA_Bool usePasswordMode,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    PlayerIndex nativePlayer = PlayerIndex::One;
    if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeTitle;
    std::string nativeDescription;
    std::string nativeDefaultText;
    if (const CNA_Result result = ReadText(title, "The title is invalid.", &nativeTitle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ReadText(description, "The description is invalid.", &nativeDescription);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ReadText(defaultText, "The default text is invalid.", &nativeDefaultText);
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
    EndedKeyboardInput().reset();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideWasKeyboardInputCanceledExt(CNA_Bool* const outWasCanceled)
{
    if (PendingKeyboardInput() == nullptr) {
        return InvalidState("No keyboard input has been started.");
    }
    *outWasCanceled =
        ToCBool(Guide::WasKeyboardInputCanceledEXT(PendingKeyboardInput().get()));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideRenderPendingKeyboardInputExt(
    const CNA_Handle deviceHandle,
    const CNA_Handle spriteBatchHandle,
    const CNA_Handle fontHandle,
    const CNA_Handle whitePixelHandle)
{
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
}

[[nodiscard]] CNA_Result GuideResetPendingKeyboardInputExt(void)
{
    Guide::ResetPendingKeyboardInputForTestingEXT();
    // The operation is gone as far as the canonical guide is concerned, so the C layer drops its
    // own hold too rather than leaving an answer nobody can complete.
    PendingKeyboardInput().reset();
    EndedKeyboardInput().reset();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideBeginShowMessageBox(
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
    PlayerIndex nativePlayer = PlayerIndex::One;
    if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (icon > CNA_MESSAGE_BOX_ICON_MAXIMUM) {
        return InvalidArgument("The message-box icon is not a defined identity.");
    }
    if (buttons == nullptr && buttonCount != UINT64_C(0)) {
        return InvalidArgument("The message-box button array is null.");
    }
    std::string nativeTitle;
    std::string nativeText;
    if (const CNA_Result result = ReadText(title, "The title is invalid.", &nativeTitle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(text, "The body text is invalid.", &nativeText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<std::string> nativeButtons;
    nativeButtons.reserve(static_cast<std::size_t>(buttonCount));
    for (uint64_t index = UINT64_C(0); index < buttonCount; ++index) {
        std::string caption;
        if (const CNA_Result result =
                ReadText(buttons[index], "A button caption is invalid.", &caption);
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
    EndedMessageBox().reset();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideEndShowMessageBox(
    CNA_Bool* const outHasChoice,
    int32_t* const outButtonIndex)
{
    *outHasChoice = CNA_FALSE;
    if (PendingMessageBox() == nullptr) {
        return InvalidState("No message box has been started.");
    }
    const std::optional<int> selected = MessageBoxAnswer();
    if (!selected.has_value()) {
        return CNA_RESULT_SUCCESS;
    }
    *outHasChoice = CNA_TRUE;
    *outButtonIndex = static_cast<int32_t>(*selected);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideGetPendingMessageBoxFocusButtonExt(int32_t* const outFocusButton)
{
    if (!Guide::getHasPendingMessageBoxEXTProperty()) {
        return InvalidState("No message box is currently pending.");
    }
    *outFocusButton =
        static_cast<int32_t>(Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideRenderPendingMessageBoxExt(
    const CNA_Handle deviceHandle,
    const CNA_Handle spriteBatchHandle,
    const CNA_Handle fontHandle,
    const CNA_Handle whitePixelHandle)
{
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
}

[[nodiscard]] CNA_Result GuideResetPendingMessageBoxExt(void)
{
    Guide::ResetPendingMessageBoxForTestingEXT();
    PendingMessageBox().reset();
    EndedMessageBox().reset();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GuideShowComposeMessage(
    const CNA_PlayerIndex player,
    const CNA_StringView text,
    const CNA_GamerHandle* const recipients,
    const uint64_t recipientCount)
{
    PlayerIndex nativePlayer = PlayerIndex::One;
    if (const CNA_Result result = ValidatePlayer(player, &nativePlayer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeText;
    if (const CNA_Result result = ReadText(text, "The message text is invalid.", &nativeText);
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
}

[[nodiscard]] CNA_Result GuideShowGameInvite(
    const CNA_PlayerIndex player,
    const CNA_GamerHandle* const recipients,
    const uint64_t recipientCount)
{
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
}

[[nodiscard]] CNA_Result GamerServicesDispatcherSubscribeInstallingTitleUpdateExt(
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The gamer event callback is null.");
    }
    auto* const source = &GamerServicesDispatcher::InstallingTitleUpdate;
    const auto token = source->Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    return PublishGuideRegistration(
        std::make_shared<GuideRegistration>(source, token),
        outRegistration);
}

} // namespace

// The routes generated from modules/c-api/spec/Guide.toml.
#include "generated/Guide.inc"
