#pragma once

// Colour maths shared by the components: OKLab / OKLCh (Bjoern Ottosson, 2020) for picking and
// adjusting colours, APCA for judging contrast.
//
// Header-only, no Windows or foobar2000 dependency, so the offline tests compile it as is.
// Colours cross this header as 0x00RRGGBB (an alpha byte is ignored on the way in, zero on the
// way out). Win32 COLORREF is 0x00BBGGRR: convert with rgb_from_colorref / colorref_from_rgb.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fbc::colour {

inline constexpr float pi = 3.14159265358979f;

struct Lab {
    float L{0.0f}; // 0 black .. 1 white
    float a{0.0f}; // green .. red
    float b{0.0f}; // blue .. yellow
};

[[nodiscard]] inline float srgb_to_linear(float c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linear_to_srgb(float c) noexcept {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

//! sRGB byte -> linear light, 256 entries built once.
[[nodiscard]] inline const std::array<float, 256>& linear_lut() noexcept {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (std::size_t i = 0; i < t.size(); ++i) t[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        return t;
    }();
    return table;
}

[[nodiscard]] inline Lab linear_to_oklab(float r, float g, float b) noexcept {
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return Lab{0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
               1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
               0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

inline void oklab_to_linear(const Lab& c, float& r, float& g, float& b) noexcept {
    const float l0 = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m0 = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s0 = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l = l0 * l0 * l0;
    const float m = m0 * m0 * m0;
    const float s = s0 * s0 * s0;
    r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

[[nodiscard]] inline Lab from_rgb(std::uint32_t rgb) noexcept {
    const auto& lin = linear_lut();
    return linear_to_oklab(lin[(rgb >> 16) & 0xffu], lin[(rgb >> 8) & 0xffu], lin[rgb & 0xffu]);
}

[[nodiscard]] inline bool in_gamut(const Lab& c) noexcept {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    oklab_to_linear(c, r, g, b);
    constexpr float e = 1e-4f;
    return r >= -e && r <= 1.0f + e && g >= -e && g <= 1.0f + e && b >= -e && b <= 1.0f + e;
}

//! Clamps to sRGB; callers that care about hue go through from_lch, which never needs to.
[[nodiscard]] inline std::uint32_t to_rgb(const Lab& c) noexcept {
    float lin[3]{};
    oklab_to_linear(c, lin[0], lin[1], lin[2]);
    std::uint32_t out = 0;
    for (float v : lin) {
        v = linear_to_srgb(std::clamp(v, 0.0f, 1.0f));
        out = (out << 8) | static_cast<std::uint32_t>(v * 255.0f + 0.5f);
    }
    return out;
}

[[nodiscard]] inline float chroma(const Lab& c) noexcept { return std::sqrt(c.a * c.a + c.b * c.b); }
//! Radians, -pi..pi.
[[nodiscard]] inline float hue(const Lab& c) noexcept { return std::atan2(c.b, c.a); }

//! The largest chroma sRGB can show at lightness L and hue h (bisection, exact to ~1e-5).
[[nodiscard]] inline float max_chroma(float L, float h) noexcept {
    if (L <= 0.0f || L >= 1.0f) return 0.0f;
    const float ca = std::cos(h);
    const float sa = std::sin(h);
    float lo = 0.0f;
    float hi = 0.5f;
    for (int i = 0; i < 18; ++i) {
        const float mid = (lo + hi) * 0.5f;
        (in_gamut(Lab{L, mid * ca, mid * sa}) ? lo : hi) = mid;
    }
    return lo;
}

//! The most chroma sRGB can show at hue h at any lightness (the hue's "cusp"), from a table of
//! 360 hues with linear interpolation, for per-pixel use. Yellows top out near 0.21, blues near
//! 0.31: dividing by this compares how saturated two colours are regardless of hue.
[[nodiscard]] inline float cusp_chroma(float h) noexcept {
    constexpr int hues = 360;
    static const auto table = [] {
        std::array<float, hues> t{};
        for (int i = 0; i < hues; ++i) {
            const float hh = static_cast<float>(i) * 2.0f * pi / hues;
            float best = 0.0f;
            for (int j = 1; j < 100; ++j) best = (std::max)(best, max_chroma(static_cast<float>(j) / 100.0f, hh));
            t[static_cast<std::size_t>(i)] = best;
        }
        return t;
    }();
    float hf = h / (2.0f * pi) * hues;
    hf -= std::floor(hf / hues) * hues;
    const int h0 = static_cast<int>(hf) % hues;
    const float t = hf - std::floor(hf);
    return table[static_cast<std::size_t>(h0)] + (table[static_cast<std::size_t>((h0 + 1) % hues)] - table[static_cast<std::size_t>(h0)]) * t;
}

//! L, C, h to sRGB. Keeps lightness and hue and gives up chroma until the colour exists in sRGB.
[[nodiscard]] inline std::uint32_t from_lch(float L, float C, float h) noexcept {
    const float cmax = max_chroma(L, h);
    const float c = (std::min)(C, cmax);
    return to_rgb(Lab{L, c * std::cos(h), c * std::sin(h)});
}

//! Blends two colours in OKLab: no muddy or dark midpoint, as in an sRGB blend. t 0 = a, 1 = b.
[[nodiscard]] inline std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return a & 0xffffffu;
    if (t >= 1.0f) return b & 0xffffffu;
    const Lab x = from_rgb(a);
    const Lab y = from_rgb(b);
    return to_rgb(Lab{x.L + (y.L - x.L) * t, x.a + (y.a - x.a) * t, x.b + (y.b - x.b) * t});
}

//! Below this chroma a colour reads as grey.
inline constexpr float grey_chroma = 0.035f;

[[nodiscard]] inline float lightness(std::uint32_t rgb) noexcept { return from_rgb(rgb).L; }

// ---------------------------------------------------------------------------------------------
// Contrast.

//! sRGB relative luminance, the WCAG 2 definition.
[[nodiscard]] inline float relative_luminance(std::uint32_t rgb) noexcept {
    const auto& lin = linear_lut();
    return 0.2126f * lin[(rgb >> 16) & 0xffu] + 0.7152f * lin[(rgb >> 8) & 0xffu] + 0.0722f * lin[rgb & 0xffu];
}

//! WCAG 2 contrast ratio, 1..21.
[[nodiscard]] inline float contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept {
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    return ((std::max)(la, lb) + 0.05f) / ((std::min)(la, lb) + 0.05f);
}

//! APCA lightness contrast Lc of `fg` on `bg` (APCA-W3 0.0.98G-4g, the constants published with
//! the WCAG 3 working draft). Positive for dark on light, negative for light on dark; compare
//! magnitudes. Unlike the WCAG 2 ratio it does not overrate dark pairs, so a dim accent on a
//! near-black panel no longer passes. Rough guide: 75 body text, 60 short text, 45 large text and
//! solid non-text marks, 30 large thin marks, 15 the least that is visible at all.
[[nodiscard]] inline float apca_contrast(std::uint32_t fg, std::uint32_t bg) noexcept {
    const auto Y = [](std::uint32_t rgb) {
        const auto ch = [](std::uint32_t v) { return std::pow(static_cast<float>(v & 0xffu) / 255.0f, 2.4f); };
        float y = 0.2126729f * ch(rgb >> 16) + 0.7151522f * ch(rgb >> 8) + 0.0721750f * ch(rgb);
        if (y < 0.022f) y += std::pow(0.022f - y, 1.414f);
        return y;
    };
    const float yt = Y(fg);
    const float yb = Y(bg);
    if (std::fabs(yb - yt) < 0.0005f) return 0.0f;
    if (yb > yt) {
        const float s = (std::pow(yb, 0.56f) - std::pow(yt, 0.57f)) * 1.14f;
        return s < 0.1f ? 0.0f : (s - 0.027f) * 100.0f;
    }
    const float s = (std::pow(yb, 0.65f) - std::pow(yt, 0.62f)) * 1.14f;
    return s > -0.1f ? 0.0f : (s + 0.027f) * 100.0f;
}

//! White or black text on `bg`, whichever APCA rates higher.
[[nodiscard]] inline std::uint32_t text_on(std::uint32_t bg) noexcept {
    return std::fabs(apca_contrast(0xffffffu, bg)) >= std::fabs(apca_contrast(0x000000u, bg)) ? 0xffffffu
                                                                                                : 0x000000u;
}

// ---------------------------------------------------------------------------------------------
// Making a cover colour usable.

//! A background at or above this OKLab lightness is a light theme.
inline constexpr float light_background_lightness = 0.6f;

//! What an accent (underline, outline, progress bar, glyph) has to clear against its background,
//! in APCA Lc. 45 is APCA's floor for solid marks. On a light panel an accent is often a thin
//! line (a 2 px underline, a 1 px outline) and 45 left a pale orange or sky blue that was hard to
//! see, so light panels ask for 55 (a little above WCAG 2's 3:1). On a dark panel 55 pushed reds
//! up to salmon pink; 45 keeps them red and still rejects the dim accents WCAG 2 let through.
inline constexpr float accent_min_lc_light = 55.0f;
inline constexpr float accent_min_lc_dark = 45.0f;

//! accent_min_lc_light or accent_min_lc_dark, by the background's lightness.
[[nodiscard]] inline float accent_min_lc_for(std::uint32_t background) noexcept {
    return lightness(background & 0xffffffu) >= light_background_lightness ? accent_min_lc_light : accent_min_lc_dark;
}

//! The cover colour `raw` made into an accent that reads on `background`: the same hue, as
//! colourful as the cover, at a lightness that clears `min_lc`. Of the lightness levels that
//! clear it, the one that keeps the most of the colour's chroma wins, with a pull towards the
//! cover's own lightness, so a blue stays blue instead of turning pastel on a dark panel; a hue
//! may turn up to 20 degrees when that saves a lot of colour, so a yellow becomes amber rather
//! than olive on a light panel. A grey cover gets an off-white or
//! charcoal. Never fails: when nothing clears `min_lc` the best-contrast candidate is returned.
//! `min_lc` 0 means accent_min_lc_for(background).
[[nodiscard]] inline std::uint32_t accent_for_background(std::uint32_t raw, std::uint32_t background,
                                                         float min_lc = 0.0f) noexcept {
    raw &= 0xffffffu;
    background &= 0xffffffu;
    const Lab c = from_rgb(raw);
    const float C = chroma(c);
    const bool light = lightness(background) >= light_background_lightness;
    if (min_lc <= 0.0f) min_lc = light ? accent_min_lc_light : accent_min_lc_dark;
    if (C < grey_chroma) {
        float L = light ? (std::min)(c.L, 0.36f) : (std::max)(c.L, 0.87f);
        std::uint32_t out = to_rgb(Lab{L, 0.0f, 0.0f});
        for (int i = 0; i < 60 && std::fabs(apca_contrast(out, background)) < min_lc; ++i) {
            L = std::clamp(L + (light ? -0.01f : 0.01f), 0.0f, 1.0f);
            out = to_rgb(Lab{L, 0.0f, 0.0f});
        }
        return out;
    }
    const float h = hue(c);
    // An accent is meant to be seen: a faded cover colour is lifted to at least this chroma.
    const float want = (std::max)(C, 0.12f);
    // Comfortable lightness for an accent on this kind of background; contrast decides within it.
    const float lo = light ? 0.30f : 0.55f;
    const float hi = light ? 0.70f : 0.92f;
    float best_score = -1e9f;
    std::uint32_t best = 0;
    float best_lc = -1.0f;
    std::uint32_t fallback = 0;
    // A small hue turn is allowed when it saves a lot of colour: a yellow has almost no chroma at
    // the lightness a light panel needs, and a slightly warmer amber reads as the same yellow
    // where a pure dark yellow reads as olive.
    constexpr float max_turn = 20.0f * pi / 180.0f;
    for (float turn = -max_turn; turn <= max_turn + 1e-4f; turn += max_turn / 4.0f) {
        const float hh = h + turn;
        for (float L = lo; L <= hi + 1e-4f; L += 0.01f) {
            const float cmax = max_chroma(L, hh);
            const std::uint32_t rgb = from_lch(L, want, hh);
            const float lc = std::fabs(apca_contrast(rgb, background));
            if (lc > best_lc) {
                best_lc = lc;
                fallback = rgb;
            }
            if (lc < min_lc) continue;
            // Chroma kept (0..1) matters most; leaving the cover's lightness and hue costs a little.
            const float kept = (std::min)(cmax, want) / want;
            const float score = kept - 0.6f * std::fabs(L - c.L) - 0.6f * std::fabs(turn);
            if (score > best_score) {
                best_score = score;
                best = rgb;
            }
        }
    }
    return best_score > -1e8f ? best : fallback;
}

//! Moves `rgb` away from `background` in OKLab lightness, 0.02 a step, until its APCA |Lc|
//! reaches `min_lc`. Hue is kept exactly, chroma as far as sRGB allows. For colours the user
//! chose (a custom accent, the theme's text on a custom background): they are only nudged.
[[nodiscard]] inline std::uint32_t with_min_lc(std::uint32_t rgb, std::uint32_t background, float min_lc) noexcept {
    rgb &= 0xffffffu;
    background &= 0xffffffu;
    if (std::fabs(apca_contrast(rgb, background)) >= min_lc) return rgb;
    const Lab lab = from_rgb(rgb);
    const float C = chroma(lab);
    const float h = hue(lab);
    const float step = lightness(background) < light_background_lightness ? 0.02f : -0.02f;
    float L = lab.L;
    for (int i = 0; i < 50 && std::fabs(apca_contrast(rgb, background)) < min_lc; ++i) {
        L = std::clamp(L + step, 0.0f, 1.0f);
        rgb = from_lch(L, C, h);
        if (L <= 0.0f || L >= 1.0f) break;
    }
    return rgb;
}

//! The minimum for the theme's text on a background the component chose (a custom colour or a
//! tint), in APCA Lc: short UI labels.
inline constexpr float text_min_lc = 60.0f;

//! The cover colour as a strong fill (an active tab's background, a card tint): same hue, a
//! mid lightness and at least 0.12 chroma, so text on it can be judged with text_on(). A grey
//! cover gets an off-white (`light_fill` false) or charcoal fill. Not an accent mark: for lines
//! and glyphs use accent_for_background, which judges contrast against the real background.
[[nodiscard]] inline std::uint32_t fill_for_cover(std::uint32_t raw, bool light_fill) noexcept {
    const Lab c = from_rgb(raw & 0xffffffu);
    const float C = chroma(c);
    if (C < grey_chroma) {
        const float L = light_fill ? (std::max)(c.L, 0.87f) : (std::min)(c.L, 0.36f);
        return to_rgb(Lab{L, 0.0f, 0.0f});
    }
    const float lo = light_fill ? 0.68f : 0.44f;
    const float hi = light_fill ? 0.83f : 0.58f;
    return from_lch(std::clamp(c.L, lo, hi), (std::max)(C, 0.12f), hue(c));
}

//! Win32 COLORREF (0x00BBGGRR) <-> 0x00RRGGBB.
[[nodiscard]] constexpr std::uint32_t rgb_from_colorref(std::uint32_t bgr) noexcept {
    return ((bgr & 0xffu) << 16) | (bgr & 0xff00u) | ((bgr >> 16) & 0xffu);
}
[[nodiscard]] constexpr std::uint32_t colorref_from_rgb(std::uint32_t rgb) noexcept {
    return rgb_from_colorref(rgb); // the same swap
}

} // namespace fbc::colour
