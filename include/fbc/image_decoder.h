#pragma once

// WIC, wrapped: encoded image bytes -> premultiplied BGRA (fbc::DecodedImage) at a bounded size.
// Windows only (windowscodecs.lib, linked by the .cpp). Meant for a worker thread; it initialises
// COM for the duration when the thread has none, and leaves an existing apartment alone.

#include <cstdint>
#include <optional>
#include <span>

#include "cover_accent.h"

namespace fbc {

//! Downscales (Fant) so neither edge exceeds `max_edge`; never upscales. Nothing on any failure.
[[nodiscard]] std::optional<DecodedImage> decode_image(std::span<const std::uint8_t> bytes,
                                                       std::uint32_t max_edge) noexcept;

} // namespace fbc
