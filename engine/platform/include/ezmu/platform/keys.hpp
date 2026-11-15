#pragma once

#include <cstdint>

// Keyboard and mouse identifiers. These are ezMU types; SDL values never escape the module.

namespace ezmu::platform {

/// Physical key position, independent of the keyboard layout. Values are USB HID keyboard usage
/// IDs (page 0x07), so they are stable and safe to store in keybind files. Keys without a named
/// enumerator are still reported with their HID value.
// NOLINTNEXTLINE(performance-enum-size): HID usage IDs go beyond 255 (kLeftCtrl = 224, ...).
enum class Scancode : std::uint16_t {
    kUnknown = 0,
    kA = 4,
    kB = 5,
    kC = 6,
    kD = 7,
    kE = 8,
    kF = 9,
    kG = 10,
    kH = 11,
    kI = 12,
    kJ = 13,
    kK = 14,
    kL = 15,
    kM = 16,
    kN = 17,
    kO = 18,
    kP = 19,
    kQ = 20,
    kR = 21,
    kS = 22,
    kT = 23,
    kU = 24,
    kV = 25,
    kW = 26,
    kX = 27,
    kY = 28,
    kZ = 29,
    kDigit1 = 30,
    kDigit2 = 31,
    kDigit3 = 32,
    kDigit4 = 33,
    kDigit5 = 34,
    kDigit6 = 35,
    kDigit7 = 36,
    kDigit8 = 37,
    kDigit9 = 38,
    kDigit0 = 39,
    kReturn = 40,
    kEscape = 41,
    kBackspace = 42,
    kTab = 43,
    kSpace = 44,
    kMinus = 45,
    kEquals = 46,
    kLeftBracket = 47,
    kRightBracket = 48,
    kBackslash = 49,
    kNonUsHash = 50,
    kSemicolon = 51,
    kApostrophe = 52,
    kGrave = 53,
    kComma = 54,
    kPeriod = 55,
    kSlash = 56,
    kCapsLock = 57,
    kF1 = 58,
    kF2 = 59,
    kF3 = 60,
    kF4 = 61,
    kF5 = 62,
    kF6 = 63,
    kF7 = 64,
    kF8 = 65,
    kF9 = 66,
    kF10 = 67,
    kF11 = 68,
    kF12 = 69,
    kPrintScreen = 70,
    kScrollLock = 71,
    kPause = 72,
    kInsert = 73,
    kHome = 74,
    kPageUp = 75,
    kDelete = 76,
    kEnd = 77,
    kPageDown = 78,
    kRight = 79,
    kLeft = 80,
    kDown = 81,
    kUp = 82,
    kNumLock = 83,
    kKeypadDivide = 84,
    kKeypadMultiply = 85,
    kKeypadMinus = 86,
    kKeypadPlus = 87,
    kKeypadEnter = 88,
    kKeypad1 = 89,
    kKeypad2 = 90,
    kKeypad3 = 91,
    kKeypad4 = 92,
    kKeypad5 = 93,
    kKeypad6 = 94,
    kKeypad7 = 95,
    kKeypad8 = 96,
    kKeypad9 = 97,
    kKeypad0 = 98,
    kKeypadPeriod = 99,
    kNonUsBackslash = 100,
    kApplication = 101,
    kLeftCtrl = 224,
    kLeftShift = 225,
    kLeftAlt = 226,
    kLeftGui = 227,
    kRightCtrl = 228,
    kRightShift = 229,
    kRightAlt = 230,
    kRightGui = 231,
};

/// Layout-dependent key symbol. Printable keys are their (lowercase) Unicode code point;
/// other keys are `kKeycodeScancodeMask | scancode`. Compare with the constants below or with
/// `keycodeFromChar` / `keycodeFromScancode`.
// NOLINTNEXTLINE(performance-enum-size): holds code points and kKeycodeScancodeMask values.
enum class Keycode : std::uint32_t {
    kUnknown = 0,
    kBackspace = 0x08,
    kTab = 0x09,
    kReturn = 0x0D,
    kEscape = 0x1B,
    kSpace = 0x20,
    kDelete = 0x7F,
};

inline constexpr std::uint32_t kKeycodeScancodeMask = 1U << 30U;

[[nodiscard]] constexpr Keycode keycodeFromChar(char32_t codePoint) noexcept {
    return static_cast<Keycode>(codePoint);
}

[[nodiscard]] constexpr Keycode keycodeFromScancode(Scancode scancode) noexcept {
    return static_cast<Keycode>(kKeycodeScancodeMask | static_cast<std::uint32_t>(scancode));
}

/// Modifier key state (bit flags).
enum class KeyMod : std::uint16_t {
    kNone = 0,
    kLeftShift = 1U << 0U,
    kRightShift = 1U << 1U,
    kLeftCtrl = 1U << 2U,
    kRightCtrl = 1U << 3U,
    kLeftAlt = 1U << 4U,
    kRightAlt = 1U << 5U,
    kLeftGui = 1U << 6U,
    kRightGui = 1U << 7U,
    kNumLock = 1U << 8U,
    kCapsLock = 1U << 9U,
    kScrollLock = 1U << 10U,
    kAltGr = 1U << 11U,

    kShift = kLeftShift | kRightShift,
    kCtrl = kLeftCtrl | kRightCtrl,
    kAlt = kLeftAlt | kRightAlt,
    kGui = kLeftGui | kRightGui,
};

[[nodiscard]] constexpr KeyMod operator|(KeyMod a, KeyMod b) noexcept {
    return static_cast<KeyMod>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

[[nodiscard]] constexpr KeyMod operator&(KeyMod a, KeyMod b) noexcept {
    return static_cast<KeyMod>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

/// True if any flag of `flags` is set in `mods` (e.g. `hasAny(mods, KeyMod::kCtrl)`).
[[nodiscard]] constexpr bool hasAny(KeyMod mods, KeyMod flags) noexcept {
    return (mods & flags) != KeyMod::kNone;
}

enum class MouseButton : std::uint8_t {
    kUnknown = 0,
    kLeft = 1,
    kMiddle = 2,
    kRight = 3,
    kX1 = 4,
    kX2 = 5,
};

} // namespace ezmu::platform
