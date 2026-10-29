#include "presenter.h"

#include <SDL3/SDL.h>

#include <vector>

#include "hw_presenter.h"
#include "window_icon.h"
#include "software_presenter.h"

namespace dw3app {

void Presenter::notify(std::string text) { osd_.show(std::move(text), SDL_GetTicks()); }

void Presenter::set_status(std::string text) { osd_.set_status(std::move(text)); }

void Presenter::set_panel(const OsdImage* image, int x, int y, uint64_t serial) {
    panel_ = image && image->w > 0 && image->h > 0 ? image : nullptr;
    panel_x_ = x;
    panel_y_ = y;
    panel_serial_ = panel_ ? serial : 0;
}

namespace {

// Blends `src` (straight alpha) over `dst` at (x0, y0), sampling every `step`-th source pixel (nearest neighbour).
void blend_into(OsdImage& dst, const OsdImage& src, int x0, int y0, int step) {
    for (int y = 0; y < src.h / step; ++y) {
        if (y0 + y < 0 || y0 + y >= dst.h) continue;
        for (int x = 0; x < src.w / step; ++x) {
            if (x0 + x < 0 || x0 + x >= dst.w) continue;
            const uint32_t s = src.rgba[static_cast<std::size_t>(y * step) * static_cast<std::size_t>(src.w) + static_cast<std::size_t>(x * step)];
            const uint32_t sa = s >> 24;
            if (sa == 0) continue;
            uint32_t& d = dst.rgba[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(dst.w) + static_cast<std::size_t>(x0 + x)];
            if (sa == 255) {
                d = s;
                continue;
            }
            const uint32_t da = d >> 24;
            const uint32_t oa = sa + da * (255 - sa) / 255;
            if (oa == 0) continue;
            uint32_t out = oa << 24;
            for (int shift = 0; shift < 24; shift += 8) {
                const uint32_t sc = (s >> shift) & 0xFF, dc = (d >> shift) & 0xFF;
                out |= ((sc * sa + dc * da * (255 - sa) / 255) / oa & 0xFF) << shift;
            }
            d = out;
        }
    }
}

}  // namespace

bool Presenter::overlay_frame(int ww, int wh, OverlayFrame& out) {
    float opacity = 1.0f;
    const OsdImage* note = osd_.frame(ww, wh, SDL_GetTicks(), opacity);
    if (!touch_ || !touch_->visible() || ww <= 0 || wh <= 0) {
        if (note) {
            out = {note->rgba.data(), note->w, note->h, osd_.x(ww), osd_.margin(), 0, 0, osd_.serial(), opacity};
            return true;
        }
        if (!panel_) return false;
        out = {panel_->rgba.data(), panel_->w, panel_->h, panel_x_, panel_y_, 0, 0, panel_serial_ | (1ull << 62), 1.0f};
        return true;
    }
    // The controls cover the window; the notification is copied into them (at full strength: a fade would mean
    // uploading the whole window image every frame of it).
    // Large screens get the controls at half resolution, scaled up when drawn: a full-size image of a 1440p window
    // is too much to upload whenever a button changes.
    const int downscale = static_cast<int64_t>(ww) * wh > 2'400'000 ? 2 : 1;
    const OsdImage& controls = touch_->image(ww, wh, downscale);
    const int cw = controls.w, ch = controls.h;
    const uint64_t osd_key = note ? osd_.serial() : ~1ull;
    const int osd_x = note ? osd_.x(ww) : -1;
    const uint64_t panel_key = panel_ ? panel_serial_ : ~1ull;
    if (touch_->serial() != composite_touch_ || osd_key != composite_osd_ || osd_x != composite_osd_x_ || composite_.w != cw ||
        composite_.h != ch || panel_key != composite_panel_) {
        composite_ = controls;
        if (panel_) blend_into(composite_, *panel_, panel_x_ / downscale, panel_y_ / downscale, downscale);
        if (note) {
            // Nearest-neighbour into the (possibly smaller) controls image.
            const int x0 = osd_x / downscale, y0 = osd_.margin() / downscale;
            for (int y = 0; y < note->h / downscale && y0 + y < ch; ++y)
                for (int x = 0; x < note->w / downscale && x0 + x < cw; ++x)
                    composite_.rgba[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x0 + x)] =
                        note->rgba[static_cast<std::size_t>(y * downscale) * static_cast<std::size_t>(note->w) + static_cast<std::size_t>(x * downscale)];
        }
        composite_touch_ = touch_->serial();
        composite_osd_ = osd_key;
        composite_osd_x_ = osd_x;
        composite_panel_ = panel_key;
        ++composite_serial_;
    }
    out = {composite_.rgba.data(), composite_.w, composite_.h, 0, 0, ww, wh, composite_serial_ | (1ull << 63), 1.0f};
    return true;
}

std::unique_ptr<Presenter> create_presenter(const char* title, int width, int height, RendererChoice choice,
                                            const PresentOptions& options, void (*log)(const std::string&),
                                            SDL_Window*& window) {
    std::string error;
    window = nullptr;
    std::vector<dw3::GraphicsApi> apis;
    const bool software_only = choice == RendererChoice::Software;
    if (choice == RendererChoice::Vulkan) apis = {dw3::GraphicsApi::Vulkan};
    else if (choice == RendererChoice::D3D12) apis = {dw3::GraphicsApi::D3D12};
    else if (choice == RendererChoice::Metal) apis = {dw3::GraphicsApi::Metal};
    else if (!software_only) {
#if defined(_WIN32)
        apis = {dw3::GraphicsApi::D3D12, dw3::GraphicsApi::Vulkan};
#elif defined(__APPLE__)
        apis = {dw3::GraphicsApi::Metal};
#else
        apis = {dw3::GraphicsApi::Vulkan};
#endif
    }
    for (const dw3::GraphicsApi api : apis) {
        // A window that has had a swap chain of one API cannot get one of another, so every attempt has its own.
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
        flags |= SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#elif !defined(_WIN32)
        flags |= SDL_WINDOW_VULKAN;
#endif
        window = SDL_CreateWindow(title, width, height, flags);
        if (window) {
            set_window_icon(window);
            if (auto gpu = HwPresenter::create(window, api, options, error)) return gpu;
            SDL_DestroyWindow(window);
            window = nullptr;
        } else {
            error = std::string("window creation failed: ") + SDL_GetError();
        }
        const char* api_name = api == dw3::GraphicsApi::D3D12 ? "Direct3D 12" : api == dw3::GraphicsApi::Metal ? "Metal" : "Vulkan";
        log(std::string(api_name) + " renderer unavailable: " + error);
    }
    if (choice != RendererChoice::Auto && choice != RendererChoice::Software) return nullptr;
    if (!software_only) log("falling back to the software presentation path");
    SDL_WindowFlags software_flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
    software_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    window = SDL_CreateWindow(title, width, height, software_flags);
    if (!window) {
        log(std::string("window creation failed: ") + SDL_GetError());
        return nullptr;
    }
    set_window_icon(window);
    auto sw = SoftwarePresenter::create(window, options, error);
    if (!sw) {
        log(error);
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    return sw;
}

}  // namespace dw3app
