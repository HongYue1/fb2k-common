#pragma once

// The colour of a cover: what the components tint and accent with when the accent follows the
// now-playing cover. Deterministic (the same pixels always give the same answer), no Windows or
// foobar2000 dependency. Runs on the worker that decoded the cover, once per cover, in well
// under a millisecond for the 256 px decodes the components use.

#include <cstdint>
#include <optional>
#include <vector>

namespace fbc {

//! Premultiplied BGRA, top-down, tightly packed (WIC's 32bppPBGRA).
struct DecodedImage {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] std::uint32_t stride() const noexcept { return width * 4u; }
    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

struct CoverColours {
    //! The cover's colour, 0x00RRGGBB: a real colour from the cover, as vivid as it is there.
    //! Not legible as is; pass it through colour::accent_for_background for marks and text.
    std::uint32_t primary{0};
    //! A second, clearly different hue that is also prominent, if the cover has one.
    std::optional<std::uint32_t> secondary;
    //! How colourful the cover is, 0 (greyscale) .. 1 (most of it vivid). Hosts can use it to
    //! skip tinting large areas for a nearly grey cover.
    float colourfulness{0.0f};
};

//! Nothing when the image has no opaque pixels.
[[nodiscard]] std::optional<CoverColours> cover_colours(const DecodedImage& image) noexcept;

} // namespace fbc
