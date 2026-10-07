#include "fbc/fonts.h"

#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace fbc::fonts {

namespace {

[[nodiscard]] std::string size_text(std::uint32_t tenths) {
    char size[24]{};
    if (tenths % 10 == 0) {
        std::snprintf(size, sizeof size, "%upt", tenths / 10);
    } else {
        std::snprintf(size, sizeof size, "%u.%upt", tenths / 10, tenths % 10);
    }
    return size;
}

[[nodiscard]] const char* weight_name(std::uint32_t weight) noexcept {
    switch (weight / 100) {
    case 1: return "Thin";
    case 2: return "Extra Light";
    case 3: return "Light";
    case 5: return "Medium";
    case 6: return "Semi Bold";
    case 7: return "Bold";
    case 8: return "Extra Bold";
    case 9: return "Black";
    default: return nullptr;
    }
}

[[nodiscard]] int screen_dpi() noexcept {
    HDC screen = GetDC(nullptr);
    const int dpi = screen != nullptr ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    return dpi;
}

//! What the Font dialog's family box said when OK was pressed. Only filled for family-only picks.
thread_local std::wstring g_picked_family;

//! For a fallback font only the family matters, so the dialog's style and size pickers are hidden
//! (they would suggest a choice that has no effect) and the caption says so.
UINT_PTR CALLBACK family_only_hook(HWND dlg, UINT msg, WPARAM wparam, LPARAM) {
    if (msg == WM_INITDIALOG) {
        // stc2/stc3 (labels) and cmb2/cmb3 (style, size) of the common Font dialog.
        for (int id : {0x441, 0x442, 0x471, 0x472}) {
            if (HWND h = GetDlgItem(dlg, id)) ShowWindow(h, SW_HIDE);
        }
        SetWindowTextW(dlg, L"Fallback font (family only)");
        g_picked_family.clear();
    } else if (msg == WM_COMMAND && LOWORD(wparam) == IDOK) {
        // cmb1, the family box: the name the user actually chose.
        wchar_t name[LF_FACESIZE]{};
        GetDlgItemTextW(dlg, 0x470, name, LF_FACESIZE);
        g_picked_family = name;
    }
    return 0;
}

int CALLBACK family_found(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

[[nodiscard]] bool gdi_has_family(const std::wstring& name) {
    LOGFONTW probe{};
    probe.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(probe.lfFaceName, name.c_str(), _TRUNCATE);
    bool found = false;
    if (HDC screen = GetDC(nullptr)) {
        EnumFontFamiliesExW(screen, &probe, &family_found, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, screen);
    }
    return found;
}

//! The family a picked font belongs to, weights and all. ChooseFont names the GDI family of the
//! style it has selected, and a family with more than four styles is split into one GDI family per
//! weight: "IBM Plex Sans JP Thin". With the style list hidden that is whichever style comes first,
//! so a fallback came back as Thin. DirectWrite groups the weights, so its name is the one to keep.
//! Falls back to the GDI name when DirectWrite or GDI does not know the grouped name.
[[nodiscard]] std::wstring base_family(const LOGFONTW& picked) {
    const std::wstring gdi_name = picked.lfFaceName;
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteGdiInterop> interop;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) ||
        FAILED(factory->GetGdiInterop(&interop)) || FAILED(interop->CreateFontFromLOGFONT(&picked, &font)) ||
        FAILED(font->GetFontFamily(&family)) || FAILED(family->GetFamilyNames(&names))) {
        return gdi_name;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) index = 0;
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length)) || length == 0 || length >= LF_FACESIZE) return gdi_name;
    std::wstring name(length + 1, L'\0');
    if (FAILED(names->GetString(index, name.data(), length + 1))) return gdi_name;
    name.resize(length);
    // Only a name GDI can open too.
    return gdi_has_family(name) ? name : gdi_name;
}

//! The family box's text, which is what the user clicked, else the DirectWrite grouping of
//! whatever style the hidden style list held.
[[nodiscard]] std::wstring fallback_family(const LOGFONTW& picked) {
    if (!g_picked_family.empty() && gdi_has_family(g_picked_family)) return g_picked_family;
    return base_family(picked);
}

[[nodiscard]] bool run_dialog(HWND owner, LOGFONTW& logical, bool family_only, INT& point_size) {
    CHOOSEFONTW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.lpLogFont = &logical;
    dialog.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    if (family_only) {
        dialog.Flags |= CF_NOSIZESEL | CF_NOSTYLESEL | CF_ENABLEHOOK;
        dialog.lpfnHook = &family_only_hook;
    }
    if (!ChooseFontW(&dialog) || logical.lfFaceName[0] == L'\0') return false;
    point_size = dialog.iPointSize;
    return true;
}

} // namespace

std::string describe(const FontChoice& choice) {
    std::string text = choice.family;
    if (choice.tenths_pt != 0) (text += ", ") += size_text(choice.tenths_pt);
    if (const char* weight = weight_name(choice.weight)) (text += ", ") += weight;
    if (choice.italic) text += ", Italic";
    return text;
}

std::string describe_default(const FontDefault& font, std::string_view style) {
    std::string text = "Default (";
    text += font.family.empty() ? std::string("Segoe UI") : font.family;
    if (font.tenths_pt != 0) (text += ", ") += size_text(font.tenths_pt);
    if (!style.empty()) (text += ", ") += style;
    return text + ")";
}

bool pick_font(HWND owner, FontChoice& choice, const FontDefault& start) {
    LOGFONTW logical{};
    logical.lfCharSet = DEFAULT_CHARSET;
    logical.lfWeight = static_cast<LONG>(choice.weight != 0 ? choice.weight : start.weight);
    logical.lfItalic = choice.italic ? TRUE : FALSE;
    logical.lfHeight = -MulDiv(static_cast<int>(choice.tenths_pt != 0 ? choice.tenths_pt : start.tenths_pt),
                               screen_dpi(), 720);
    // Unset means the font it falls back to, so that is what the dialog starts on.
    const std::string& family = !choice.family.empty() ? choice.family : start.family;
    wcsncpy_s(logical.lfFaceName, family.empty() ? L"Segoe UI" : widen(family).c_str(), _TRUNCATE);

    INT point_size = 0;
    if (!run_dialog(owner, logical, false, point_size)) return false;
    choice.family = narrow(logical.lfFaceName);
    choice.tenths_pt = point_size > 0 ? static_cast<std::uint32_t>(point_size) : 0u;
    choice.weight = logical.lfWeight > 0 ? static_cast<std::uint32_t>(logical.lfWeight) : 400u;
    choice.italic = logical.lfItalic != 0;
    return true;
}

bool pick_family(HWND owner, std::string& family, std::string_view start) {
    LOGFONTW logical{};
    logical.lfCharSet = DEFAULT_CHARSET;
    logical.lfWeight = FW_NORMAL;
    logical.lfHeight = -MulDiv(110, screen_dpi(), 720);
    const std::string_view from = !family.empty() ? std::string_view(family) : start;
    wcsncpy_s(logical.lfFaceName, from.empty() ? L"Segoe UI" : widen(from).c_str(), _TRUNCATE);

    INT point_size = 0;
    if (!run_dialog(owner, logical, true, point_size)) return false;
    family = narrow(fallback_family(logical));
    return true;
}

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>((std::max)(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>((std::max)(n, 0)), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    }
    return out;
}

ComPtr<IDWriteFontFallback> make_fallback(IDWriteFactory* factory, std::span<const std::wstring> families,
                                          IDWriteFontFallback* then) noexcept {
    try {
        if (factory == nullptr || families.empty()) return nullptr;
        ComPtr<IDWriteFactory2> factory2;
        ComPtr<IDWriteFontCollection> system;
        if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory2))) ||
            FAILED(factory->GetSystemFontCollection(&system, FALSE))) {
            return nullptr;
        }
        std::vector<const wchar_t*> names;
        for (const std::wstring& name : families) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (!name.empty() && SUCCEEDED(system->FindFamilyName(name.c_str(), &index, &exists)) && exists) {
                names.push_back(name.c_str());
            }
        }
        if (names.empty()) return nullptr;
        ComPtr<IDWriteFontFallbackBuilder> builder;
        ComPtr<IDWriteFontFallback> system_fallback;
        ComPtr<IDWriteFontFallback> chain;
        const DWRITE_UNICODE_RANGE everything{0, 0x10FFFF};
        if (FAILED(factory2->CreateFontFallbackBuilder(&builder)) ||
            FAILED(builder->AddMapping(&everything, 1, names.data(), static_cast<UINT32>(names.size()), nullptr,
                                       nullptr, nullptr, 1.0f))) {
            return nullptr;
        }
        if (then == nullptr) {
            if (FAILED(factory2->GetSystemFontFallback(&system_fallback))) return nullptr;
            then = system_fallback.Get();
        }
        if (FAILED(builder->AddMappings(then)) || FAILED(builder->CreateFontFallback(&chain))) return nullptr;
        return chain;
    } catch (...) {
        return nullptr;
    }
}

void pad_text_fields(HWND dialog) noexcept {
    HDC dc = GetDC(dialog);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc != nullptr) ReleaseDC(dialog, dc);
    EnumChildWindows(
        dialog,
        [](HWND child, LPARAM pad) -> BOOL {
            wchar_t name[16]{};
            GetClassNameW(child, name, 16);
            if (_wcsicmp(name, L"Edit") == 0) {
                SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
            }
            return TRUE;
        },
        MulDiv(4, dpi, 96));
}

} // namespace fbc::fonts
