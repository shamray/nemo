#pragma once

#include <vector>
#include <array>

namespace nes {

template<class B>
concept ppu_bus = requires(B b, uint16_t address, uint8_t value) {
    { b.chr_write(address, value) };
    { b.chr_read(address) } -> std::same_as<uint8_t>;
};

template<ppu_bus bus_t>
class ppu {
public:
    ppu(bus_t &bus) {}

    uint8_t control;
    uint8_t status;
    uint8_t mask;
    uint8_t oam_addr;
    uint8_t oam_data;
    uint8_t scroll;
    uint8_t addr;
    uint8_t data;
    uint8_t oam_dma;

    void tick() noexcept;

    [[nodiscard]] constexpr auto scan_line() const noexcept { return scan_.line; }

    [[nodiscard]] constexpr auto scan_cycle() const noexcept { return scan_.cycle; }

    [[nodiscard]] constexpr auto is_odd_frame() const noexcept { return frame_is_odd_; }

    [[nodiscard]] constexpr auto is_frame_ready() const noexcept { return frame_is_ready_; }

    std::array<uint32_t, 256 * 240> frame_buffer;

    void render_noise(auto get_noise) {
        // The sky above the port was the color of television, tuned to a dead channel
        for (auto &&pixel: frame_buffer) {
            auto r = get_noise();
            pixel = (0xFF << 24) | (r << 16) | (r << 8) | (r);
        }
    }

private:
    void prerender_scanline() noexcept {};

    void visible_scanline() noexcept {};

    void postrender_scanline() noexcept {};

    void vertical_blank_line() noexcept {};

private:
    struct {
        int line{-1};
        int cycle{0};
    } scan_;
    bool frame_is_odd_{false};
    bool frame_is_ready_{false};
};

const auto VISIBLE_SCANLINES = 240;
const auto VERTICAL_BLANK_SCANLINES = 20;
const auto POST_RENDER_SCANLINES = 1;
const auto SCANLINE_DOTS = 341;

[[nodiscard]] constexpr auto is_prerender(auto scanline) noexcept {
    return scanline == -1;
}

[[nodiscard]] constexpr auto is_visible(auto scanline) noexcept {
    return scanline >= 0 and scanline < VISIBLE_SCANLINES;
}

[[nodiscard]] constexpr auto is_postrender(auto scanline) noexcept {
    return scanline >= VISIBLE_SCANLINES and scanline < VISIBLE_SCANLINES + POST_RENDER_SCANLINES;
}

[[nodiscard]] constexpr auto is_vblank(auto scanline) noexcept {
    return scanline >= VISIBLE_SCANLINES + POST_RENDER_SCANLINES and
           scanline < VISIBLE_SCANLINES + POST_RENDER_SCANLINES + VERTICAL_BLANK_SCANLINES;
}

template<ppu_bus bus_t>
void ppu<bus_t>::tick() noexcept {
    if (is_prerender(scan_line())) {
        prerender_scanline();
    } else if (is_visible(scan_line())) {
        visible_scanline();
    } else if (is_postrender(scan_line())) {
        postrender_scanline();
    } else if (is_vblank(scan_line())) {
        vertical_blank_line();
    }

    frame_is_ready_ = false;

    if (++scan_.cycle >= SCANLINE_DOTS) {
        scan_.cycle = 0;

        if (++scan_.line >= VISIBLE_SCANLINES + POST_RENDER_SCANLINES + VERTICAL_BLANK_SCANLINES) {
            scan_.line = -1;
            frame_is_odd_ = not frame_is_odd_;
            frame_is_ready_ = true;
        }
    }
}
}