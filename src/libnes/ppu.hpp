#pragma once

#include <libnes/ppu_crt_scan.hpp>
#include <libnes/ppu_name_table.hpp>
#include <libnes/ppu_object_attribute_memory.hpp>
#include <libnes/ppu_palette_table.hpp>
#include <libnes/ppu_pattern_table.hpp>

#include <libnes/screen.hpp>
#include <libnes/color.hpp>

#include <array>
#include <optional>
#include <cassert>

namespace nes {

constexpr auto VISIBLE_SCANLINES = 240;
constexpr auto VERTICAL_BLANK_SCANLINES = 20;
constexpr auto POST_RENDER_SCANLINES = 1;
constexpr auto SCANLINE_DOTS = 341;

class ppu
{
public:
    template <class container_t>
    ppu(const container_t& system_color_palette)
        : palette_table_{system_color_palette}
    {}

    ppu(const pattern_table::memory_bank& chr, const auto& system_color_palette)
        : pattern_table_{&chr}
        , palette_table_{system_color_palette}
    {}

    constexpr void connect_pattern_table(auto new_bank) noexcept { pattern_table_.connect(new_bank); }

    constexpr void nametable_mirroring(auto mirroring) noexcept { name_table_.mirroring = mirroring; }

    std::uint8_t control{0};
    std::uint8_t status{0};
    std::uint8_t mask{0};

    int scroll_latch{0};
    std::uint8_t scroll_x{0};
    std::uint8_t scroll_y{0};
    std::uint8_t scroll_x_buffer{0};
    std::uint8_t scroll_y_buffer{0};

    bool nmi{false};

    int address_latch{0};
    std::uint16_t address;
    std::uint8_t data_buffer;

    template <screen screen_t>
    constexpr void tick(screen_t& screen);

    [[nodiscard]] constexpr auto is_frame_ready() const noexcept { return scan_.is_frame_finished(); }

    [[nodiscard]] constexpr auto read(std::uint16_t addr) -> std::optional<std::uint8_t> {
        switch (addr) {
            case 0x2002: return read_stat();
            case 0x2007: return read_data();

            default: return std::nullopt;
        }
    }

    constexpr auto write(std::uint16_t addr, std::uint8_t value) {
        switch (addr) {
            case 0x2000: { write_ctrl(value); return true; }
            case 0x2003: { write_oama(value); return true; }
            case 0x2004: { write_oamd(value); return true; }
            case 0x2005: { write_scrl(value); return true; }
            case 0x2006: { write_addr(value); return true; }
            case 0x2007: { write_data(value); return true; }

            default: return false;
        }
    }

    constexpr void dma_write(auto from) {
        oam_.dma_write(from);
    }

    [[nodiscard]] constexpr auto palette_table() const -> const auto& { return palette_table_; }
    [[nodiscard]] constexpr auto oam() const -> const auto& { return oam_; }

    [[nodiscard]] constexpr static auto nametable_address(int nametable_index_x, int nametable_index_y) {
        auto index = (nametable_index_y << 1) | nametable_index_x;
        return index << 10;
    }

    [[nodiscard]] constexpr auto tile_x_scrolled(short x) {
        assert(nametable_index_x_ == 0 or nametable_index_x_ == 1);

        auto tile_x = (x + scroll_x) / 8;
        auto nametable_index_x = nametable_index_x_;

        if (tile_x >= 32) { // wrap nametable while scrolling horizontally
            tile_x %= 32;
            nametable_index_x ^= 1;
        }
        return std::tuple{nametable_index_x, tile_x};
    }

    [[nodiscard]] constexpr auto tile_y_scrolled(short y) {
        assert(nametable_index_y_ == 0 or nametable_index_y_ == 1);

        auto tile_y = (y + scroll_y) / 8;
        auto nametable_index_y = nametable_index_y_;

        if (tile_y >= 30) { // wrap nametable while scrolling vertically
            tile_y -= 30;
            nametable_index_y ^= 1;
        }
        return std::tuple{nametable_index_y, tile_y};
    }

    auto display_pattern_table(auto i, auto palette) const -> std::array<color, 128 * 128>;

    template <screen screen_t>
    void render_noise(auto get_noise, screen_t& screen) {
        // The sky above the port was the color of television, tuned to a dead channel
        for (auto x = 0; x < screen.width(); ++x) {
            for (auto y = 0; y < screen.height(); ++y) {
                auto r = get_noise();
                auto pixel = color{r << 16, r << 8, r};
                screen.draw_pixel({x, y}, pixel);
            }
        }
    }

private:
    [[nodiscard]] constexpr auto read_stat() -> std::uint8_t {
        auto d = status & 0xE0;
        status &= 0x60;     // reset vblank
        address_latch = 0;
        return d;
    }

    [[nodiscard]] constexpr auto read_data() -> std::uint8_t {
        auto a = address++;
        auto r = data_read_buffer_;
        auto& b = data_read_buffer_;

        if (a >= 0x2000 and a <= 0x2FFF)        { b = name_table_.read(a & 0xFFF); }
        else if (a >= 0x3000 and a <= 0x3EFF)   { throw std::range_error("not implemented"); }
        else if (a >= 0x3F00 and a <= 0x3FFF)   { r = b = palette_table_.read(a & 0x001F); }
        else                                    { b = pattern_table_.read(a); }

        return r;
    }

    constexpr void write_ctrl(std::uint8_t value) {
        control = value;
        if (scan_.is_vblank()) {
            status |= 0x80;
            if (control & 0x80)
                nmi = true;
        }
    }

    void write_oama(std::uint8_t value) { oam_.address = value; }
    void write_oamd(std::uint8_t value) { oam_.write(value); }

    constexpr void write_scrl(std::uint8_t value) {
        if (scroll_latch == 0) {
            scroll_x_buffer = value;
            scroll_latch = 1;
        } else {
            scroll_y_buffer = value;
            scroll_latch = 0;
        }
    }

    constexpr void write_addr(std::uint8_t value) {
        if (address_latch == 0) {
            address = (address & 0x00FF) | (value << 8);
            address_latch = 1;
        } else {
            address = (address & 0xFF00) | (value << 0);
            address_latch = 0;

            address &= 0x3FFF;
        }
    }

    constexpr void write_data(std::uint8_t value) {
        if (address >= 0x2000 and address <= 0x3000) { name_table_.write(address & 0xFFF, value); }
        else if (address >= 0x3F00 and address <= 0x3FFF) { palette_table_.write(address & 0x001F, value); }
        else {
            // ppu chr write
        }
        auto inc = (control & 0x04) ? 32 : 1;
        address += inc;
    }

    constexpr void prerender_scanline() noexcept;

    std::uint8_t nametable_index_x_{0};
    std::uint8_t nametable_index_y_{0};

    [[nodiscard]] constexpr auto nametable_index_x() const { return (control >> 0) & 0x01; }
    [[nodiscard]] constexpr auto nametable_index_y() const { return (control >> 1) & 0x01; }

    [[nodiscard]] constexpr auto pattern_table_bg_index() const { return (control & 0x10) >> 4; }
    [[nodiscard]] constexpr auto pattern_table_fg_index() const { return (control & 0x08) >> 3; }

    [[nodiscard]] constexpr static auto nametable_tile_offset(auto tile_x, auto tile_y, int nametable_index) {
        return static_cast<std::uint16_t>((tile_y * 32 + tile_x) | nametable_index);
    }

    [[nodiscard]] constexpr static auto nametable_attr_offset(auto tile_x, auto tile_y, int nametable_index) {
        return static_cast<std::uint16_t>((0x3c0 + tile_y / 4 * 8 + tile_x / 4) | nametable_index);
    }

    [[nodiscard]] constexpr static auto read_tile_pixel(const auto& pattern_table, auto ix, auto tile, auto x, auto y) {
        const auto tile_offset = ix * 0x1000 + tile * 0x10;
        const auto tile_lsb = pattern_table.read(tile_offset + y + 0);
        const auto tile_msb = pattern_table.read(tile_offset + y + 8);
        const auto pixel_lo = (tile_lsb >> (7 - x)) & 0x01;
        const auto pixel_hi = (tile_msb >> (7 - x)) & 0x01;
        return static_cast<std::uint8_t>(pixel_lo | (pixel_hi << 1));
    }

    [[nodiscard]] constexpr static auto read_tile_index(const auto& name_table, auto tile_x, auto tile_y, auto nametable_index) {
        auto offset = nametable_tile_offset(tile_x, tile_y, nametable_index);
        return name_table.read(offset);
    }

    [[nodiscard]] constexpr static auto tile_palette(auto tile_x, auto tile_y, auto attr_byte) {
        if ((tile_x % 4) >> 1) {
            attr_byte = attr_byte >> 2;
        }
        if ((tile_y % 4) >> 1) {
            attr_byte = attr_byte >> 4;
        }
        return attr_byte & 0x03;
    }

    [[nodiscard]] constexpr static auto read_tile_palette(const auto& name_table, auto tile_x, auto tile_y, auto nametable_index) {
        auto attr_byte_index = nametable_attr_offset(tile_x, tile_y, nametable_index);
        auto attr_byte = name_table.read(attr_byte_index);

        return tile_palette(tile_x, tile_y, attr_byte);
    }

    template <screen screen_t>
    constexpr void visible_scanline(screen_t& screen) {
        auto y = scan_.line();
        auto x = static_cast<short>(scan_.cycle() - 2);

        if (x >= 0 and x < 256) {
            auto [nametable_index_x, tile_x] = tile_x_scrolled(x);
            auto [nametable_index_y, tile_y] = tile_y_scrolled(y);

            auto tile_row = (y + scroll_y) % 8;
            auto tile_col = (x + scroll_x) % 8;

            auto nametable_addr = nametable_address(nametable_index_x, nametable_index_y);

            auto tile_index = read_tile_index(name_table_, tile_x, tile_y, nametable_addr);
            auto pixel = read_tile_pixel(pattern_table_, pattern_table_bg_index(), tile_index, tile_col, tile_row);
            auto palette = read_tile_palette(name_table_, tile_x, tile_y, nametable_addr);

            screen.draw_pixel({x, y}, palette_table_.color_of(pixel, palette));

            // Sprite 0 hit
            auto s = oam_.sprites[0];
            if (x >= s.x and x < s.x + 8 and y >= s.y and y < s.y + 8 and pixel != 0) {
                auto dx = x - s.x;
                auto dy = y - s.y;
                auto j = (s.attr & 0x40) ? 7 - dx : dx;
                auto i = (s.attr & 0x80) ? 7 - dy : dy;
                auto sprite_pixel = read_tile_pixel(pattern_table_, pattern_table_fg_index(), s.tile, j, i);
                if (sprite_pixel != 0) {
                    status |= 0x40;
                }
            }
        }

        if (scan_.cycle() == 257) {
            scroll_x = scroll_x_buffer;
            nametable_index_x_ = nametable_index_x();
        }
    }

    template <screen screen_t>
    constexpr void postrender_scanline(screen_t& screen) noexcept {
        if (scan_.cycle() == 0) {
            for (const auto& s: oam_.sprites) {
                auto palette = (s.attr & 0x03) + 4;

                for (auto i = 0; i < 8; ++i) {
                    for (auto j = 0; j < 8; ++j) {
                        auto pixel = read_tile_pixel(pattern_table_, pattern_table_fg_index(), s.tile, j, i);
                        auto dx = (s.attr & 0x40) ? 7 - j : j;
                        auto dy = (s.attr & 0x80) ? 7 - i : i;
                        if (pixel) {
                            screen.draw_pixel(
                                {static_cast<short>(s.x + dx), static_cast<short>(s.y + dy)},
                                palette_table_.color_of(pixel, palette)
                                );
                        }
                    }
                }
            }

        }
    }
    constexpr void vertical_blank_line() noexcept {
        if (scan_.cycle() == 0) {
            status |= 0x80;
            if (control & 0x80)
                nmi = true;
        }
    };

private:
    crt_scan scan_{SCANLINE_DOTS, VISIBLE_SCANLINES, POST_RENDER_SCANLINES, VERTICAL_BLANK_SCANLINES};

    nes::pattern_table  pattern_table_;
    nes::name_table     name_table_;
    nes::palette_table  palette_table_;
    nes::object_attribute_memory oam_;

    std::uint8_t data_read_buffer_;
};

template <screen screen_t>
constexpr void ppu::tick(screen_t& screen) {
    if      (scan_.is_prerender())  { prerender_scanline(); }
    else if (scan_.is_visible())    { visible_scanline(screen); }
    else if (scan_.is_postrender()) { postrender_scanline(screen); }
    else if (scan_.is_vblank())     { vertical_blank_line(); }

    scan_.advance();
}

inline auto ppu::display_pattern_table(auto i, auto palette) const -> std::array<color, 128 * 128> {
    auto result = std::array<color, 128 * 128>{};

    for (std::uint16_t tile_y = 0; tile_y < 16; ++tile_y) {
        for (std::uint16_t tile_x = 0; tile_x < 16; ++tile_x) {
            auto offset = static_cast<std::uint16_t>(tile_y * 16 + tile_x);
            for (std::uint16_t row = 0; row < 8; ++row) {
                for (std::uint16_t col = 0; col < 8; col++) {
                    auto pixel = read_tile_pixel(pattern_table_, i, offset, col, row);

                    auto result_offset = (tile_y * 8 + row) * 128 + tile_x * 8 + (7 - col);
                    auto result_color = palette_table_.color_of(pixel, palette);

                    result[result_offset] = result_color;
                }
            }
        }
    }
    return result;
}

constexpr void ppu::prerender_scanline() noexcept {
    if (scan_.cycle() == 0) {
        status = 0x00;
        control &= 0xFE;
    }
    if (scan_.cycle() >= 280) {
        scroll_y = scroll_y_buffer;
        nametable_index_y_ = nametable_index_y();
    }
}
}