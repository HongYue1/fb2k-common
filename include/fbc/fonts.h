#pragma once

// Fonts as the components' preferences pick them, and the user's fallback chain for DirectWrite.
// Windows only (links comdlg32, gdi32, dwrite). Strings are UTF-8 except where DirectWrite wants
// wide ones.
//
// The Fonts page every component shares (same order, same wording where it applies):
//   Fonts           one row per font: label, description, [Select...] [Default], then a note
//                   on what Default follows; a text-rendering checkbox here when there is one.
//   Fallback fonts  Fallback 1-3: family, [Select...] [Clear], then a note on the order.

#include <windows.h>

#include <dwrite_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fbc::fonts {

//! One font as the Windows font dialog reports it. An empty family means "not picked".
struct FontChoice {
    std::string family;
    std::uint32_t tenths_pt{0}; //!< 0 = the caller's default size
    std::uint32_t weight{0};    //!< 0 = the caller's default weight
    bool italic{false};

    [[nodiscard]] bool operator==(const FontChoice&) const = default;
};

//! What an unpicked font renders as: the font dialog starts there.
struct FontDefault {
    std::string family;          //!< empty = Segoe UI
    std::uint32_t tenths_pt{90};
    std::uint32_t weight{400};
};

//! "IBM Plex Sans, 12pt, Bold, Italic". Regular is left out.
[[nodiscard]] std::string describe(const FontChoice& choice);
//! "Default (Segoe UI, 9pt, Bold)": what an unpicked font row shows. `style` may be empty.
[[nodiscard]] std::string describe_default(const FontDefault& font, std::string_view style = {});

//! The font dialog, started from `choice` (or `start` for what is unset). False on Cancel.
[[nodiscard]] bool pick_font(HWND owner, FontChoice& choice, const FontDefault& start);
//! The font dialog without size and style, for a fallback: only the family is kept, under the
//! name DirectWrite groups its weights by ("IBM Plex Sans JP", not "IBM Plex Sans JP Thin").
[[nodiscard]] bool pick_family(HWND owner, std::string& family, std::string_view start);

[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

//! For characters the main font lacks: `families` in order (the whole of Unicode), then `then`,
//! or Windows' own fallback when `then` is null. Families the system does not have are skipped.
//! Null when none is usable or DirectWrite is older than Windows 8.1.
[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFontFallback>
make_fallback(IDWriteFactory* factory, std::span<const std::wstring> families,
              IDWriteFontFallback* then = nullptr) noexcept;

//! Edit controls get 4 px (at 96 DPI) inside their border; their own margin is a pixel or two.
void pad_text_fields(HWND dialog) noexcept;

} // namespace fbc::fonts
