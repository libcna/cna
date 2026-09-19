// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file XmlSerializationEXT.hpp
 * @brief CNAEXT registration of every XNA Keys enumerator for XML serialization.
 *
 * XNA's XmlSerializer obtains enum member names through reflection. This opt-in
 * adapter supplies the same names to SharpRuntime without a sample-specific map.
 */

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "System/Xml/Serialization/detail/XmlMember.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief CNAEXT: supplies the XNA names of every Keys value in declaration order.
     * @return The complete Keys enum metadata for XML serialization.
     */
    CNAEXT SHARP_XML_ENUM(Keys,
        SHARP_XML_E(Keys, None), SHARP_XML_E(Keys, Back), SHARP_XML_E(Keys, Tab),
        SHARP_XML_E(Keys, Enter), SHARP_XML_E(Keys, Pause), SHARP_XML_E(Keys, CapsLock),
        SHARP_XML_E(Keys, Kana), SHARP_XML_E(Keys, Kanji), SHARP_XML_E(Keys, Escape),
        SHARP_XML_E(Keys, ImeConvert), SHARP_XML_E(Keys, ImeNoConvert), SHARP_XML_E(Keys, Space),
        SHARP_XML_E(Keys, PageUp), SHARP_XML_E(Keys, PageDown), SHARP_XML_E(Keys, End),
        SHARP_XML_E(Keys, Home), SHARP_XML_E(Keys, Left), SHARP_XML_E(Keys, Up),
        SHARP_XML_E(Keys, Right), SHARP_XML_E(Keys, Down), SHARP_XML_E(Keys, Select),
        SHARP_XML_E(Keys, Print), SHARP_XML_E(Keys, Execute), SHARP_XML_E(Keys, PrintScreen),
        SHARP_XML_E(Keys, Insert), SHARP_XML_E(Keys, Delete), SHARP_XML_E(Keys, Help),
        SHARP_XML_E(Keys, D0), SHARP_XML_E(Keys, D1), SHARP_XML_E(Keys, D2),
        SHARP_XML_E(Keys, D3), SHARP_XML_E(Keys, D4), SHARP_XML_E(Keys, D5),
        SHARP_XML_E(Keys, D6), SHARP_XML_E(Keys, D7), SHARP_XML_E(Keys, D8),
        SHARP_XML_E(Keys, D9), SHARP_XML_E(Keys, A), SHARP_XML_E(Keys, B),
        SHARP_XML_E(Keys, C), SHARP_XML_E(Keys, D), SHARP_XML_E(Keys, E),
        SHARP_XML_E(Keys, F), SHARP_XML_E(Keys, G), SHARP_XML_E(Keys, H),
        SHARP_XML_E(Keys, I), SHARP_XML_E(Keys, J), SHARP_XML_E(Keys, K),
        SHARP_XML_E(Keys, L), SHARP_XML_E(Keys, M), SHARP_XML_E(Keys, N),
        SHARP_XML_E(Keys, O), SHARP_XML_E(Keys, P), SHARP_XML_E(Keys, Q),
        SHARP_XML_E(Keys, R), SHARP_XML_E(Keys, S), SHARP_XML_E(Keys, T),
        SHARP_XML_E(Keys, U), SHARP_XML_E(Keys, V), SHARP_XML_E(Keys, W),
        SHARP_XML_E(Keys, X), SHARP_XML_E(Keys, Y), SHARP_XML_E(Keys, Z),
        SHARP_XML_E(Keys, LeftWindows), SHARP_XML_E(Keys, RightWindows), SHARP_XML_E(Keys, Apps),
        SHARP_XML_E(Keys, Sleep), SHARP_XML_E(Keys, NumPad0), SHARP_XML_E(Keys, NumPad1),
        SHARP_XML_E(Keys, NumPad2), SHARP_XML_E(Keys, NumPad3), SHARP_XML_E(Keys, NumPad4),
        SHARP_XML_E(Keys, NumPad5), SHARP_XML_E(Keys, NumPad6), SHARP_XML_E(Keys, NumPad7),
        SHARP_XML_E(Keys, NumPad8), SHARP_XML_E(Keys, NumPad9), SHARP_XML_E(Keys, Multiply),
        SHARP_XML_E(Keys, Add), SHARP_XML_E(Keys, Separator), SHARP_XML_E(Keys, Subtract),
        SHARP_XML_E(Keys, Decimal), SHARP_XML_E(Keys, Divide), SHARP_XML_E(Keys, F1),
        SHARP_XML_E(Keys, F2), SHARP_XML_E(Keys, F3), SHARP_XML_E(Keys, F4),
        SHARP_XML_E(Keys, F5), SHARP_XML_E(Keys, F6), SHARP_XML_E(Keys, F7),
        SHARP_XML_E(Keys, F8), SHARP_XML_E(Keys, F9), SHARP_XML_E(Keys, F10),
        SHARP_XML_E(Keys, F11), SHARP_XML_E(Keys, F12), SHARP_XML_E(Keys, F13),
        SHARP_XML_E(Keys, F14), SHARP_XML_E(Keys, F15), SHARP_XML_E(Keys, F16),
        SHARP_XML_E(Keys, F17), SHARP_XML_E(Keys, F18), SHARP_XML_E(Keys, F19),
        SHARP_XML_E(Keys, F20), SHARP_XML_E(Keys, F21), SHARP_XML_E(Keys, F22),
        SHARP_XML_E(Keys, F23), SHARP_XML_E(Keys, F24), SHARP_XML_E(Keys, NumLock),
        SHARP_XML_E(Keys, Scroll), SHARP_XML_E(Keys, LeftShift), SHARP_XML_E(Keys, RightShift),
        SHARP_XML_E(Keys, LeftControl), SHARP_XML_E(Keys, RightControl), SHARP_XML_E(Keys, LeftAlt),
        SHARP_XML_E(Keys, RightAlt), SHARP_XML_E(Keys, BrowserBack), SHARP_XML_E(Keys, BrowserForward),
        SHARP_XML_E(Keys, BrowserRefresh), SHARP_XML_E(Keys, BrowserStop), SHARP_XML_E(Keys, BrowserSearch),
        SHARP_XML_E(Keys, BrowserFavorites), SHARP_XML_E(Keys, BrowserHome), SHARP_XML_E(Keys, VolumeMute),
        SHARP_XML_E(Keys, VolumeDown), SHARP_XML_E(Keys, VolumeUp), SHARP_XML_E(Keys, MediaNextTrack),
        SHARP_XML_E(Keys, MediaPreviousTrack), SHARP_XML_E(Keys, MediaStop), SHARP_XML_E(Keys, MediaPlayPause),
        SHARP_XML_E(Keys, LaunchMail), SHARP_XML_E(Keys, SelectMedia), SHARP_XML_E(Keys, LaunchApplication1),
        SHARP_XML_E(Keys, LaunchApplication2), SHARP_XML_E(Keys, OemSemicolon), SHARP_XML_E(Keys, OemPlus),
        SHARP_XML_E(Keys, OemComma), SHARP_XML_E(Keys, OemMinus), SHARP_XML_E(Keys, OemPeriod),
        SHARP_XML_E(Keys, OemQuestion), SHARP_XML_E(Keys, OemTilde), SHARP_XML_E(Keys, ChatPadGreen),
        SHARP_XML_E(Keys, ChatPadOrange), SHARP_XML_E(Keys, OemOpenBrackets), SHARP_XML_E(Keys, OemPipe),
        SHARP_XML_E(Keys, OemCloseBrackets), SHARP_XML_E(Keys, OemQuotes), SHARP_XML_E(Keys, Oem8),
        SHARP_XML_E(Keys, OemBackslash), SHARP_XML_E(Keys, ProcessKey), SHARP_XML_E(Keys, OemCopy),
        SHARP_XML_E(Keys, OemAuto), SHARP_XML_E(Keys, OemEnlW), SHARP_XML_E(Keys, Attn),
        SHARP_XML_E(Keys, Crsel), SHARP_XML_E(Keys, Exsel), SHARP_XML_E(Keys, EraseEof),
        SHARP_XML_E(Keys, Play), SHARP_XML_E(Keys, Zoom), SHARP_XML_E(Keys, Pa1),
        SHARP_XML_E(Keys, OemClear))
}
