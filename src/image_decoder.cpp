#include <windows.h>

#include <wincodec.h>

#include "fbc/image_decoder.h"

#include <algorithm>
#include <cmath>

#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace fbc {

namespace {

//! Host pool threads have no apartment of their own; if the thread already has one
//! (RPC_E_CHANGED_MODE) it is left exactly as it was.
class ComScope {
public:
    ComScope() noexcept : initialised_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() {
        if (initialised_) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool initialised_{false};
};

} // namespace

std::optional<DecodedImage> decode_image(std::span<const std::uint8_t> bytes, std::uint32_t max_edge) noexcept {
    if (bytes.empty() || max_edge == 0 || bytes.size() > 0x7fffffffu) return std::nullopt;
    try {
        const ComScope com;

        // WICImagingFactory1 explicitly: with a Windows 8+ _WIN32_WINNT, CLSID_WICImagingFactory
        // means the version-2 factory, which Windows 7 does not have.
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(factory.GetAddressOf())))) {
            return std::nullopt;
        }
        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return std::nullopt;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) {
            return std::nullopt;
        }
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                    decoder.GetAddressOf()))) {
            return std::nullopt;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return std::nullopt;

        UINT source_width = 0;
        UINT source_height = 0;
        if (FAILED(frame->GetSize(&source_width, &source_height)) || source_width == 0 || source_height == 0) {
            return std::nullopt;
        }
        const UINT longest = (std::max)(source_width, source_height);
        const double scale = longest > max_edge ? static_cast<double>(max_edge) / static_cast<double>(longest) : 1.0;
        const auto width = static_cast<UINT>((std::max)(1L, std::lround(source_width * scale)));
        const auto height = static_cast<UINT>((std::max)(1L, std::lround(source_height * scale)));

        IWICBitmapSource* source = frame.Get();
        ComPtr<IWICBitmapScaler> scaler;
        if (width != source_width || height != source_height) {
            if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
                FAILED(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant))) {
                return std::nullopt;
            }
            source = scaler.Get();
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeMedianCut))) {
            return std::nullopt;
        }

        DecodedImage out;
        out.width = width;
        out.height = height;
        out.pixels.resize(static_cast<std::size_t>(width) * height * 4u);
        if (FAILED(converter->CopyPixels(nullptr, out.stride(), static_cast<UINT>(out.pixels.size()),
                                         out.pixels.data()))) {
            return std::nullopt;
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace fbc
