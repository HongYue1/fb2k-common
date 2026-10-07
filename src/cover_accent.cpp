#include "fbc/cover_accent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "fbc/colour.h"

namespace fbc {

namespace {

namespace col = colour;

constexpr int hue_bins = 36;           // 10 degrees each
constexpr int grey_bins = 16;
constexpr float corner_weight = 0.30f; // weight of a corner pixel relative to the centre
constexpr double min_colour = 0.02;    // colourful share of the weight below which it is grey
//! A second hue must be at least this far round the wheel from the first (in bins) and score at
//! least this share of it.
constexpr int secondary_min_distance = 6; // 60 degrees
constexpr double secondary_min_score = 0.35;

//! The decode is capped at 256x256, so a full pass is at most 65k pixels. Sampling ~16k on a
//! grid keeps it well under a millisecond and changes the answer by nothing visible.
constexpr std::size_t max_samples = 16384;

[[nodiscard]] float smoothstep(float e0, float e1, float x) noexcept {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Bin {
    double w{0.0}; // weight
    double L{0.0}; // weighted sums
    double a{0.0};
    double b{0.0};
    double c{0.0}; // absolute chroma
    double s{0.0}; // chroma relative to the most sRGB allows at that hue (its cusp)
};

[[nodiscard]] Bin neighbourhood(const std::array<Bin, hue_bins>& hues, int i) noexcept {
    Bin sum{};
    for (int d = -1; d <= 1; ++d) {
        const Bin& b = hues[static_cast<std::size_t>((i + d + hue_bins) % hue_bins)];
        const double k = d == 0 ? 1.0 : 0.5;
        sum.w += k * b.w;
        sum.L += k * b.L;
        sum.a += k * b.a;
        sum.b += k * b.b;
        sum.c += k * b.c;
        sum.s += k * b.s;
    }
    return sum;
}

//! The colour of a hue neighbourhood: its mean lightness and hue, with its mean chroma. The
//! plain mean of a and b is a colour duller than its pixels whenever their hues spread (the
//! vectors partly cancel), which turned a vivid cover into a muted accent.
[[nodiscard]] std::uint32_t colour_of(const Bin& n) noexcept {
    const float L = static_cast<float>(n.L / n.w);
    const float h = std::atan2(static_cast<float>(n.b), static_cast<float>(n.a));
    const float C = static_cast<float>(n.c / n.w);
    return col::from_lch(L, C, h);
}

} // namespace

// In four steps:
//  1. Every sampled pixel gets a weight: the centre of the picture counts more than its edges
//     (borders, logos, barcodes), and near-black / near-white pixels count little.
//  2. Colourful pixels (chroma above grey) are binned by hue, 36 bins of 10 degrees, weighted
//     by how colourful they are and how usable their lightness is.
//  3. Each hue (with its neighbours, so a hue on a bin edge is not split in two) is scored by
//     population^0.7 x vividness. A large area wins, but a vivid area beats a larger dull one,
//     which is what reads as a cover's "colour": the red title on a brown sleeve, not the brown.
//     Vividness is chroma relative to the most sRGB can show at that hue, so a saturated
//     yellow or green (whose chroma tops out near 0.2) is not outvoted by a half-saturated blue
//     (which can reach 0.3).
//  4. The winner's colour is its pixels' mean lightness and hue with their mean chroma.
// A cover with almost no colourful pixels answers with its dominant grey.
std::optional<CoverColours> cover_colours(const DecodedImage& image) noexcept {
    if (!image.valid()) return std::nullopt;
    return cover_colours(image.pixels.data(), image.width, image.height);
}

std::optional<CoverColours> cover_colours(const std::uint8_t* pbgra, std::uint32_t image_width,
                                          std::uint32_t image_height) noexcept {
    if (pbgra == nullptr || image_width == 0 || image_height == 0) return std::nullopt;

    const auto& to_linear = col::linear_lut();
    std::array<Bin, hue_bins> hues{};
    std::array<Bin, grey_bins> greys{};
    double total = 0.0;
    double colourful = 0.0;

    const std::size_t width = image_width;
    const std::size_t height = image_height;
    // A square grid, not every n-th pixel: a stride that divides the width samples the same few
    // columns on every row.
    const std::size_t step = (std::max<std::size_t>)(
        1, static_cast<std::size_t>(std::lround(std::sqrt(static_cast<double>(width * height) / max_samples))));
    const float half_w = static_cast<float>(width) * 0.5f;
    const float half_h = static_cast<float>(height) * 0.5f;
    const std::uint8_t* const data = pbgra;

    for (std::size_t y = step / 2; y < height; y += step) {
        for (std::size_t x = step / 2; x < width; x += step) {
            // Premultiplied BGRA. A translucent pixel is a border or a shadow, not the cover's
            // colour; the few 250..254 survivors are divided back out so they are not darkened.
            const std::uint8_t* const px = data + (y * width + x) * 4u;
            const unsigned alpha = px[3];
            if (alpha < 250u) continue;
            const auto channel = [&](std::uint8_t v) noexcept {
                const unsigned straight = alpha == 255u ? v : (std::min)(255u, (v * 255u + alpha / 2u) / alpha);
                return to_linear[straight];
            };
            const col::Lab c = col::linear_to_oklab(channel(px[2]), channel(px[1]), channel(px[0]));

            const float dx = (static_cast<float>(x) + 0.5f - half_w) / half_w;
            const float dy = (static_cast<float>(y) + 0.5f - half_h) / half_h;
            const float radius = (std::min)(1.0f, std::sqrt(dx * dx + dy * dy) * 0.70710678f);
            float w = 1.0f - (1.0f - corner_weight) * radius * radius;
            // Near black and near white say little about a cover's colour.
            w *= 0.15f + 0.85f * smoothstep(0.10f, 0.22f, c.L) * (1.0f - smoothstep(0.93f, 0.99f, c.L));
            total += w;

            const float C = col::chroma(c);
            const float vivid = smoothstep(col::grey_chroma, 0.12f, C);
            if (vivid > 0.0f) {
                colourful += static_cast<double>(w * vivid);
                // Lightness where an accent can live; very dark or pale colours are a last resort.
                const float usable =
                    0.35f + 0.65f * smoothstep(0.22f, 0.40f, c.L) * (1.0f - smoothstep(0.88f, 0.97f, c.L));
                const double cw = static_cast<double>(w * vivid * usable);
                float h = col::hue(c);
                if (h < 0.0f) h += 2.0f * col::pi;
                // Against the hue's cusp, not the most chroma at this pixel's lightness: that
                // made every dark or pale pixel look saturated (an olive shadow outvoted purple).
                // Half hue-relative, half absolute: fully relative let a pale sky (cyan's cusp is
                // only ~0.15) or a parchment background outvote the cover's figure.
                const float rel = (std::min)(1.0f, 0.5f * C / col::cusp_chroma(h) + 0.5f * C / 0.26f);
                Bin& bin = hues[static_cast<std::size_t>(
                    (std::min)(hue_bins - 1, static_cast<int>(h / (2.0f * col::pi) * hue_bins)))];
                bin.w += cw;
                bin.L += cw * c.L;
                bin.a += cw * c.a;
                bin.b += cw * c.b;
                bin.c += cw * C;
                bin.s += cw * rel;
            }
            if (C < 0.06f) {
                Bin& g = greys[static_cast<std::size_t>(std::clamp(static_cast<int>(c.L * grey_bins), 0, grey_bins - 1))];
                g.w += w;
                g.L += static_cast<double>(w) * c.L;
            }
        }
    }
    if (total <= 0.0) return std::nullopt; // nothing opaque at all

    CoverColours out;
    out.colourfulness = static_cast<float>(std::clamp(colourful / total, 0.0, 1.0));
    if (colourful >= min_colour * total) {
        std::array<double, hue_bins> score{};
        int best = -1;
        for (int i = 0; i < hue_bins; ++i) {
            const Bin n = neighbourhood(hues, i);
            if (n.w <= 0.0) continue;
            score[static_cast<std::size_t>(i)] = std::pow(n.w / total, 0.7) * (0.5 + 1.1 * (n.s / n.w));
            if (best < 0 || score[static_cast<std::size_t>(i)] > score[static_cast<std::size_t>(best)]) best = i;
        }
        if (best < 0) return std::nullopt;
        out.primary = colour_of(neighbourhood(hues, best));
        // The second colour: the best local peak far enough round the wheel from the first.
        int second = -1;
        for (int i = 0; i < hue_bins; ++i) {
            const int d = std::abs(i - best);
            if ((std::min)(d, hue_bins - d) < secondary_min_distance) continue;
            const double s = score[static_cast<std::size_t>(i)];
            if (s <= 0.0 || s < score[static_cast<std::size_t>((i + 1) % hue_bins)] ||
                s < score[static_cast<std::size_t>((i + hue_bins - 1) % hue_bins)]) {
                continue;
            }
            if (second < 0 || s > score[static_cast<std::size_t>(second)]) second = i;
        }
        if (second >= 0 && score[static_cast<std::size_t>(second)] >= secondary_min_score * score[static_cast<std::size_t>(best)]) {
            out.secondary = colour_of(neighbourhood(hues, second));
        }
    } else {
        // Monochrome: the most common grey level, averaged with its neighbours.
        int best = 0;
        for (int i = 1; i < grey_bins; ++i) {
            if (greys[static_cast<std::size_t>(i)].w > greys[static_cast<std::size_t>(best)].w) best = i;
        }
        double w = 0.0;
        double L = 0.0;
        for (int d = -1; d <= 1; ++d) {
            const int k = best + d;
            if (k < 0 || k >= grey_bins) continue;
            w += greys[static_cast<std::size_t>(k)].w;
            L += greys[static_cast<std::size_t>(k)].L;
        }
        if (w <= 0.0) return std::nullopt;
        out.primary = col::to_rgb(col::Lab{static_cast<float>(L / w), 0.0f, 0.0f});
    }
    return out;
}

} // namespace fbc
