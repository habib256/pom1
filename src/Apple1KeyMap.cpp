// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Apple1KeyMap.cpp — see Apple1KeyMap.h.

#include "Apple1KeyMap.h"

namespace pom1::keymap {

bool isControlChord(int mods)
{
    return (mods & kModControl) && !(mods & (kModAlt | kModSuper));
}

namespace {

/// The letter a layout name stands for, upper case, or 0 when it is not one
/// ASCII letter ("a" → 'A'; "," / "ф" / "" / nullptr → 0).
char layoutLetter(const char* name)
{
    if (name == nullptr || name[0] == '\0' || name[1] != '\0') return 0;
    const char c = name[0];
    if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return c;
    return 0;
}

} // namespace

char mapKey(int key, int mods, const char* layoutName)
{
    // CTRL+letter → ASCII control code $01-$1A, the way the CTRL key on a real
    // Apple-1 ASCII keyboard worked. The host toolkit emits no character event
    // for a CTRL chord, so without this no control code is reachable at all —
    // Ctrl-C (Integer BASIC break) and Ctrl-H (Applesoft Lite's line editor)
    // included. Tested before the plain keys so Ctrl+M is a carriage return by
    // its control code rather than by the Enter branch.
    //
    // The letter is the one the user's layout prints on the key, not its US
    // position: [A] measured on macOS ABC-AZERTY, GLFW 3.4 — glfwGetKeyName
    // gives KEY_A → "q", KEY_Q → "a", KEY_W → "z", KEY_Z → "w",
    // KEY_SEMICOLON → "m", KEY_M → ",". [B] glfw3.h: key tokens are "named
    // after their use on the standard US keyboard layout".
    if (isControlChord(mods)) {
        if (const char letter = layoutLetter(layoutName))
            return static_cast<char>(letter - 'A' + 1);
        const bool usLetterKey = key >= kKeyA && key <= kKeyZ;
        // The layout prints one ASCII punctuation sign on a US letter
        // position (AZERTY's "," on KEY_M): no letter, so no control code.
        if (usLetterKey && layoutName != nullptr && layoutName[0] != '\0' && layoutName[1] == '\0' &&
            !(static_cast<unsigned char>(layoutName[0]) & 0x80))
            return kNoKey;
        // No usable answer — no name (WASM, a backend without key names), a
        // non-Latin letter (Cyrillic, Greek…): the US position, as every host
        // application keeps Ctrl+C where it is on the keyboard.
        if (usLetterKey) return static_cast<char>(key - kKeyA + 1);
    }

    switch (key) {
        case kKeyEnter:
        case kKeyKpEnter:   return '\r';
        case kKeyBackspace: return kBackspaceChar;   // '_', never $08 — see header
        case kKeyEscape:    return 27;
        default:            return kNoKey;
    }
}

} // namespace pom1::keymap
