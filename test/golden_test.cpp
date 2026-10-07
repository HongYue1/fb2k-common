// Golden test over real covers: decodes every image listed in test\local\covers.txt (one path
// per line, UTF-8) the way the components do (WIC, Fant scaler to a 256 px longest edge,
// 32bppPBGRA), runs cover_colours and the accent pipeline, and compares with
// test\local\golden.txt. test\local is not committed: the covers are the user's own.
//
//   golden_test.exe            compare; exit 1 on any difference or contrast failure
//   golden_test.exe --update   rewrite golden.txt from the current code
//
// A difference is not necessarily a bug: it lists what an algorithm change moved, with the
// OKLab distance, so the change can be judged (and the covers looked at) before --update.

#include <windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "fbc/colour.h"
#include "fbc/cover_accent.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;
using namespace fbc;

namespace {

constexpr std::uint32_t dark_bg = 0x1f1f1fu;
constexpr std::uint32_t light_bg = 0xffffffu;

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::optional<DecodedImage> decode(IWICImagingFactory* factory, const std::wstring& path, std::uint32_t max_edge) {
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder))) {
        return std::nullopt;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return std::nullopt;
    UINT sw = 0, sh = 0;
    if (FAILED(frame->GetSize(&sw, &sh)) || sw == 0 || sh == 0) return std::nullopt;
    const UINT longest = (std::max)(sw, sh);
    const double scale = longest > max_edge ? static_cast<double>(max_edge) / longest : 1.0;
    const auto w = static_cast<UINT>((std::max)(1L, std::lround(sw * scale)));
    const auto h = static_cast<UINT>((std::max)(1L, std::lround(sh * scale)));
    IWICBitmapSource* source = frame.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (w != sw || h != sh) {
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), w, h, WICBitmapInterpolationModeFant))) {
            return std::nullopt;
        }
        source = scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut))) {
        return std::nullopt;
    }
    DecodedImage out;
    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    if (FAILED(converter->CopyPixels(nullptr, out.stride(), static_cast<UINT>(out.pixels.size()), out.pixels.data()))) {
        return std::nullopt;
    }
    return out;
}

struct Result {
    std::uint32_t primary{0};
    std::int64_t secondary{-1};
    float colourful{0};
    std::uint32_t dark{0};
    std::uint32_t light{0};
};

std::string format(const Result& r) {
    char sec[8] = "------";
    if (r.secondary >= 0) std::snprintf(sec, sizeof sec, "%06X", static_cast<unsigned>(r.secondary));
    char buf[96];
    std::snprintf(buf, sizeof buf, "%06X %s %.2f %06X %06X", r.primary, sec, r.colourful, r.dark, r.light);
    return buf;
}

std::optional<Result> parse(const std::string& s) {
    std::istringstream in(s);
    std::string p, sec, d, l;
    Result r;
    if (!(in >> p >> sec >> r.colourful >> d >> l)) return std::nullopt;
    r.primary = std::stoul(p, nullptr, 16);
    r.secondary = sec == "------" ? -1 : static_cast<std::int64_t>(std::stoul(sec, nullptr, 16));
    r.dark = std::stoul(d, nullptr, 16);
    r.light = std::stoul(l, nullptr, 16);
    return r;
}

float distance(std::uint32_t a, std::uint32_t b) {
    const auto x = colour::from_rgb(a), y = colour::from_rgb(b);
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b));
}

} // namespace

int main(int argc, char** argv) {
    const bool update = argc > 1 && std::string(argv[1]) == "--update";
    std::ifstream list("local\\covers.txt");
    if (!list) {
        std::printf("no local\\covers.txt: skipped\n");
        return 0;
    }
    std::map<std::string, std::string> golden;
    {
        std::ifstream g("local\\golden.txt");
        std::string line;
        while (std::getline(g, line)) {
            const auto tab = line.find('\t');
            if (tab != std::string::npos) golden[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return 2;
    }

    std::ostringstream out;
    int covers = 0, changed = 0, failures = 0, missing = 0;
    double total_ms = 0, worst_ms = 0;
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    std::string path;
    while (std::getline(list, path)) {
        if (!path.empty() && path.back() == '\r') path.pop_back();
        if (path.empty() || path[0] == '#') continue;
        const auto image = decode(factory.Get(), widen(path), 256);
        if (!image) {
            std::printf("cannot decode: %s\n", path.c_str());
            ++missing;
            continue;
        }
        ++covers;
        LARGE_INTEGER t0{}, t1{};
        QueryPerformanceCounter(&t0);
        const auto colours = cover_colours(*image);
        QueryPerformanceCounter(&t1);
        const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        total_ms += ms;
        worst_ms = (std::max)(worst_ms, ms);
        if (!colours) {
            std::printf("no colour: %s\n", path.c_str());
            ++failures;
            continue;
        }
        Result r;
        r.primary = colours->primary;
        r.secondary = colours->secondary ? static_cast<std::int64_t>(*colours->secondary) : -1;
        r.colourful = colours->colourfulness;
        r.dark = colour::accent_for_background(r.primary, dark_bg);
        r.light = colour::accent_for_background(r.primary, light_bg);
        const float lc_dark = std::fabs(colour::apca_contrast(r.dark, dark_bg));
        const float lc_light = std::fabs(colour::apca_contrast(r.light, light_bg));
        if (lc_dark < colour::accent_min_lc_dark - 0.01f || lc_light < colour::accent_min_lc_light - 0.01f) {
            std::printf("contrast %s: dark Lc %.0f light Lc %.0f\n", path.c_str(), lc_dark, lc_light);
            ++failures;
        }
        const std::string now = format(r);
        out << path << '\t' << now << '\n';
        const auto it = golden.find(path);
        if (it == golden.end()) {
            if (!update) std::printf("new   %s  %s\n", now.c_str(), path.c_str());
            ++changed;
        } else if (it->second != now) {
            ++changed;
            const auto was = parse(it->second);
            std::printf("moved %s -> %s", it->second.c_str(), now.c_str());
            if (was) std::printf("  (dE primary %.3f, dark %.3f, light %.3f)", distance(was->primary, r.primary),
                                 distance(was->dark, r.dark), distance(was->light, r.light));
            std::printf("  %s\n", path.c_str());
        }
    }
    std::printf("%d covers, %d changed, %d failures, %d not decoded; cover_colours avg %.2f ms, worst %.2f ms\n", covers,
                changed, failures, missing, covers ? total_ms / covers : 0.0, worst_ms);
    if (update) {
        std::ofstream g("local\\golden.txt", std::ios::binary);
        g << out.str();
        std::printf("golden.txt updated\n");
        return failures == 0 ? 0 : 1;
    }
    return failures == 0 && changed == 0 ? 0 : 1;
}
