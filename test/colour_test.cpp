// Offline checks of fbc/colour.h and fbc/cover_accent.h on synthetic covers: APCA against
// published reference values, accents clear their contrast floor on dark and light panels,
// secondary colours, translucent covers, fills. Built and run by test\build_tests.bat.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "fbc/colour.h"
#include "fbc/cover_accent.h"

using namespace fbc;

namespace {

constexpr std::uint32_t side = 256;
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

double now_ms() {
    LARGE_INTEGER f{};
    LARGE_INTEGER t{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

struct Cover {
    const char* name;
    std::uint32_t (*px)(int x, int y);
    bool expect_secondary;
};

const Cover covers[] = {
    {"gradient checker", [](int x, int y) -> std::uint32_t {
         return (static_cast<std::uint32_t>(200 - y * 120 / 256) << 16) |
                (static_cast<std::uint32_t>(60 + x * 100 / 256) << 8) |
                (120u + static_cast<std::uint32_t>((x / 32 + y / 32) % 2) * 60u);
     }, false},
    {"khaki + 12% blue", [](int x, int y) -> std::uint32_t {
         const int dx = x - 170, dy = y - 90;
         return dx * dx + dy * dy < 50 * 50 ? 0x2A7BF0u : (y > 200 ? 0x5C4A32u : 0xA89A6Eu);
     }, true},
    {"greyscale", [](int x, int y) -> std::uint32_t {
         const auto v = static_cast<std::uint32_t>(40 + (x + y) * 170 / 512);
         return (v << 16) | (v << 8) | v;
     }, false},
    {"navy + 4% orange", [](int x, int y) -> std::uint32_t {
         return (x > 100 && x < 150 && y > 100 && y < 152) ? 0xF28A1Eu : 0x14203Au;
     }, true},
    {"black + red stripe", [](int, int y) -> std::uint32_t { return (y > 120 && y < 146) ? 0xC81E24u : 0x0A0A0Cu; },
     false},
    {"pastel pink + white", [](int x, int) -> std::uint32_t { return x < 150 ? 0xF4C6D2u : 0xFAFAFAu; }, false},
    {"yellow + black type", [](int x, int y) -> std::uint32_t {
         return (y > 180 && y < 210 && (x / 12) % 2) ? 0x111111u : 0xF2D21Bu;
     }, false},
    {"teal / orange", [](int x, int y) -> std::uint32_t { return x + y < 256 ? 0x1F7A80u : 0xE0772Fu; }, true},
    {"forest + skin", [](int x, int y) -> std::uint32_t {
         const int dx = x - 128, dy = y - 128;
         return dx * dx + dy * dy < 60 * 60 ? 0xD9A07Eu : (((x ^ y) & 8) ? 0x2E4A22u : 0x3F6130u);
     }, true},
};

//! Premultiplied BGRA, fully opaque, as a WIC 32bppPBGRA decode hands it over.
DecodedImage make_cover(const Cover& cover) {
    DecodedImage image;
    image.width = side;
    image.height = side;
    image.pixels.resize(static_cast<std::size_t>(side) * side * 4u);
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const std::uint32_t rgb = cover.px(static_cast<int>(x), static_cast<int>(y));
            std::uint8_t* px = image.pixels.data() + (static_cast<std::size_t>(y) * side + x) * 4u;
            px[0] = static_cast<std::uint8_t>(rgb & 0xffu);
            px[1] = static_cast<std::uint8_t>((rgb >> 8) & 0xffu);
            px[2] = static_cast<std::uint8_t>((rgb >> 16) & 0xffu);
            px[3] = 0xff;
        }
    }
    return image;
}

float hue_distance(std::uint32_t a, std::uint32_t b) {
    float d = std::fabs(colour::hue(colour::from_rgb(a)) - colour::hue(colour::from_rgb(b)));
    if (d > colour::pi) d = 2.0f * colour::pi - d;
    return d * 180.0f / colour::pi;
}

} // namespace

int main() {
    // APCA 0.0.98G-4g reference values (Myndex's published examples).
    {
        const float bw = colour::apca_contrast(0x000000u, 0xffffffu);
        const float wb = colour::apca_contrast(0xffffffu, 0x000000u);
        const float g = colour::apca_contrast(0x888888u, 0xffffffu);
        const float g2 = colour::apca_contrast(0xffffffu, 0x888888u);
        std::printf("apca black/white %.1f, white/black %.1f, #888/white %.1f, white/#888 %.1f\n", bw, wb, g, g2);
        check(std::fabs(bw - 106.0f) < 0.5f && std::fabs(wb + 107.9f) < 0.5f, "APCA black/white reference");
        check(std::fabs(g - 63.1f) < 0.5f && std::fabs(g2 + 68.5f) < 0.5f, "APCA #888 reference");
        check(colour::text_on(0xffffffu) == 0 && colour::text_on(0x101010u) == 0xffffffu, "text_on extremes");
    }
    // OKLab round trip over the whole cube, coarsely.
    {
        int worst = 0;
        for (std::uint32_t r = 0; r < 256; r += 15)
            for (std::uint32_t g = 0; g < 256; g += 15)
                for (std::uint32_t b = 0; b < 256; b += 15) {
                    const std::uint32_t rgb = (r << 16) | (g << 8) | b;
                    const std::uint32_t back = colour::to_rgb(colour::from_rgb(rgb));
                    for (int s = 0; s < 24; s += 8)
                        worst = (std::max)(worst, std::abs(static_cast<int>((rgb >> s) & 0xff) -
                                                           static_cast<int>((back >> s) & 0xff)));
                }
        std::printf("oklab round trip: worst channel error %d\n", worst);
        check(worst <= 1, "OKLab round trip");
    }

    constexpr std::uint32_t dark_bg = 0x1f1f1fu;
    constexpr std::uint32_t light_bg = 0xffffffu;
    for (const Cover& cover : covers) {
        const DecodedImage image = make_cover(cover);
        std::optional<CoverColours> colours;
        constexpr int runs = 20;
        const double t0 = now_ms();
        for (int i = 0; i < runs; ++i) colours = cover_colours(image);
        const double ms = (now_ms() - t0) / runs;
        if (!colours) {
            std::printf("%-20s none\n", cover.name);
            ++failures;
            continue;
        }
        const std::uint32_t raw = colours->primary;
        const std::uint32_t on_dark = colour::accent_for_background(raw, dark_bg);
        const std::uint32_t on_light = colour::accent_for_background(raw, light_bg);
        const float lc_dark = std::fabs(colour::apca_contrast(on_dark, dark_bg));
        const float lc_light = std::fabs(colour::apca_contrast(on_light, light_bg));
        std::printf("%-20s #%06X", cover.name, raw);
        if (colours->secondary) std::printf(" + #%06X", *colours->secondary);
        else std::printf("          ");
        std::printf(" colourful %.2f -> dark #%06X (Lc %.0f), light #%06X (Lc %.0f)  %.3f ms\n",
                    colours->colourfulness, on_dark, lc_dark, on_light, lc_light, ms);
        check(lc_dark >= colour::accent_min_lc_dark - 0.01f, "dark accent contrast");
        check(lc_light >= colour::accent_min_lc_light - 0.01f, "light accent contrast");
        check(colours->secondary.has_value() == cover.expect_secondary, "secondary colour presence");
        if (colours->secondary) check(hue_distance(raw, *colours->secondary) >= 60.0f, "secondary hue distance");
        check(colours->colourfulness >= 0.0f && colours->colourfulness <= 1.0f, "colourfulness range");
        // Once per cover on a worker; generous, so a busy machine does not fail the run.
        check(ms < 5.0, "extraction time");
    }

    // Specific picks.
    {
        const auto grey = cover_colours(make_cover(covers[2]));
        check(grey && grey->colourfulness < 0.05f && colour::chroma(colour::from_rgb(grey->primary)) < colour::grey_chroma,
              "greyscale cover stays grey");
        const auto navy = cover_colours(make_cover(covers[3]));
        check(navy && hue_distance(navy->primary, 0xF28A1Eu) < 15.0f, "a small vivid orange beats a large navy");
        const auto khaki = cover_colours(make_cover(covers[1]));
        check(khaki && hue_distance(khaki->primary, 0x2A7BF0u) < 15.0f, "a vivid blue beats khaki");
    }

    // Translucent covers: nothing opaque means no colour, not a guess.
    {
        DecodedImage clear = make_cover(covers[0]);
        for (std::size_t i = 3; i < clear.pixels.size(); i += 4) {
            clear.pixels[i - 3] = clear.pixels[i - 2] = clear.pixels[i - 1] = 0; // premultiplied
            clear.pixels[i] = 0;
        }
        const bool none = !cover_colours(clear).has_value();
        std::printf("transparent cover -> %s\n", none ? "none (ok)" : "a colour (WRONG)");
        check(none, "transparent cover");
        DecodedImage empty;
        check(!cover_colours(empty).has_value(), "empty image");
    }

    // A yellow cover on a light panel: the accent goes dark gold, the fill keeps it yellow and
    // carries black text.
    {
        const std::uint32_t yellow = 0xE6C81Eu;
        const std::uint32_t line = colour::accent_for_background(yellow, 0xFFFFFFu);
        const std::uint32_t fill = colour::fill_for_cover(yellow, true);
        const float L = colour::from_rgb(fill).L;
        const float text = std::fabs(colour::apca_contrast(colour::text_on(fill), fill));
        std::printf("yellow on light: line #%06X (Lc %.0f, hue turn %.0f deg), fill #%06X (L %.2f, text Lc %.0f)\n", line,
                    std::fabs(colour::apca_contrast(line, 0xFFFFFFu)), hue_distance(line, yellow), fill, L, text);
        check(L >= 0.68f && colour::text_on(fill) == 0 && text >= colour::text_min_lc, "yellow fill");
        check(hue_distance(fill, yellow) < 3.0f && hue_distance(line, yellow) <= 21.0f, "yellow hue kept");
    }

    // with_min_lc nudges a user colour only as far as needed, keeping its hue.
    {
        const std::uint32_t dim = 0x3050A0u;
        const std::uint32_t out = colour::with_min_lc(dim, 0x1f1f1fu, 45.0f);
        std::printf("with_min_lc #%06X on #1F1F1F -> #%06X (Lc %.0f)\n", dim, out,
                    std::fabs(colour::apca_contrast(out, 0x1f1f1fu)));
        check(std::fabs(colour::apca_contrast(out, 0x1f1f1fu)) >= 45.0f && hue_distance(out, dim) < 4.0f,
              "with_min_lc");
        check(colour::with_min_lc(0xffffffu, 0x000000u, 60.0f) == 0xffffffu, "with_min_lc leaves a legible colour");
    }

    // mix: endpoints exact, midpoint between in OKLab lightness.
    {
        const std::uint32_t a = 0xC81E24u, b = 0x2A7BF0u;
        check(colour::mix(a, b, 0.0f) == a && colour::mix(a, b, 1.0f) == b, "mix endpoints");
        const float Lm = colour::lightness(colour::mix(a, b, 0.5f));
        check(std::fabs(Lm - 0.5f * (colour::lightness(a) + colour::lightness(b))) < 0.01f, "mix midpoint");
    }

    // The accent search cost: hosts call it once per cover and per frame of a colour fade.
    {
        constexpr int runs = 200;
        std::uint32_t sink = 0;
        const double t0 = now_ms();
        for (int i = 0; i < runs; ++i) sink ^= colour::accent_for_background(0xE6C81Eu + static_cast<std::uint32_t>(i), 0xffffffu);
        const double us = (now_ms() - t0) * 1000.0 / runs;
        std::printf("accent_for_background: %.1f us (%u)\n", us, sink & 1u);
        check(us < 2000.0, "accent_for_background time");
    }

    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
