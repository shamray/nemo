#pragma once

#include <libnes/ppu_crt_scan.hpp>
#include <libnes/ppu_name_table.hpp>
#include <libnes/ppu_object_attribute_memory.hpp>
#include <libnes/ppu_palette_table.hpp>

#include <libnes/color.hpp>
#include <libnes/screen.hpp>

#include "cartridge.hpp"
#include "ppu_registers.hpp"
#include <array>
#include <cassert>
#include <optional>

namespace nes
{

constexpr auto VISIBLE_SCANLINES = 240;
constexpr auto VERTICAL_BLANK_SCANLINES = 20;
constexpr auto POST_RENDER_SCANLINES = 1;
constexpr auto SCANLINE_DOTS = 341;

class ppu
{
public:
    template <class container_t>
    ppu(const container_t& system_color_palette)
        : palette_table_{system_color_palette} {}

    constexpr void load_cartridge(cartridge* rom) noexcept { cartridge_ = rom; }
    constexpr void eject_cartridge() noexcept { load_cartridge(nullptr); }

    control_register control;
    std::uint8_t status{0};
    std::uint8_t mask{0};

    std::uint16_t vram_addr;
    std::uint16_t temp_addr;
    std::uint8_t fine_x;

    int scroll_latch{0};
    std::uint8_t scroll_x{0};
    std::uint8_t scroll_y{0};
    std::uint8_t scroll_x_buffer{0};
    std::uint8_t scroll_y_buffer{0};

    bool nmi_raised{false};
    bool nmi_seen{false};

    int address_latch{0};
    std::uint16_t address;
    std::uint8_t data_buffer;

    template <screen screen_t>
    constexpr void tick_old(screen_t& screen);

    template <screen screen_t>
    constexpr void tick(screen_t& screen);

    [[nodiscard]] constexpr auto is_frame_ready() const noexcept { return scan_.is_frame_finished(); }

    [[nodiscard]] constexpr auto read(std::uint16_t addr) -> std::optional<std::uint8_t> {
        switch (addr) {
            case 0x2002:
                return read_stat();
            case 0x2007:
                return read_data();

            default:
                return std::nullopt;
        }
    }

    constexpr void write(std::uint16_t addr, std::uint8_t value) {
        switch (addr) {
            case 0x2000: {
                control.assign(value);
                return;
            }
            case 0x2003: {
                write_oama(value);
                return;
            }
            case 0x2004: {
                write_oamd(value);
                return;
            }
            case 0x2005: {
                write_scrl(value);
                return;
            }
            case 0x2006: {
                write_addr(value);
                return;
            }
            case 0x2007: {
                write_data(value);
                return;
            }
            default:
                return;
        }
    }

    constexpr void dma_write(std::uint16_t from, auto read) {
        oam_.dma_write(from, read);
    }

    [[nodiscard]] constexpr auto palette_table() const -> const auto& { return palette_table_; }
    [[nodiscard]] constexpr auto oam() const -> const auto& { return oam_; }

    [[nodiscard]] constexpr static auto nametable_address(int nametable_index_x, int nametable_index_y) {
        auto index = (nametable_index_y << 1) | nametable_index_x;
        return index << 10;
    }

    [[nodiscard]] constexpr auto tile_x_scrolled(short x) const {
        assert(nametable_index_x_ == 0 or nametable_index_x_ == 1);

        auto tile_x = (x + scroll_x) / 8;
        auto nametable_index_x = nametable_index_x_;

        if (tile_x >= 32) {// wrap nametable while scrolling horizontally
            tile_x %= 32;
            nametable_index_x ^= 1;
        }
        return std::tuple{nametable_index_x, tile_x};
    }

    [[nodiscard]] constexpr auto tile_y_scrolled(short y) const {
        assert(nametable_index_y_ == 0 or nametable_index_y_ == 1);

        auto tile_y = (y + scroll_y) / 8;
        auto nametable_index_y = nametable_index_y_;

        if (tile_y >= 30) {// wrap nametable while scrolling vertically
            tile_y -= 30;
            nametable_index_y ^= 1;
        }
        return std::tuple{nametable_index_y, tile_y};
    }

    auto display_pattern_table(auto i, auto palette) const -> std::array<color, 128 * 128>;

    template <screen screen_t>
    void render_nametables(screen_t& screen);

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
        auto requested_status = static_cast<std::uint8_t>(status & 0xe0);
        status &= 0x60;// reset vblank
        address_latch = 0;
        return requested_status;
    }

    [[nodiscard]] constexpr auto read_data() -> std::uint8_t {
        auto a = address;
        address = ++address % 0x4000;
        auto r = data_read_buffer_;
        auto& b = data_read_buffer_;

        if (a >= 0x2000 and a <= 0x2FFF) {
            b = name_table_.read(a & 0xFFF);
        } else if (a >= 0x3000 and a <= 0x3EFF) {
            throw std::range_error("not implemented");
        } else if (a >= 0x3F00 and a <= 0x3FFF) {
            r = b = palette_table_.read(a & 0x001F);
        } else {
            b = read_chr(a);
        }

        return r;
    }

    [[nodiscard]] constexpr auto read_chr(std::uint16_t addr) const -> std::uint8_t {
        assert(addr < 0x2000);

        auto& chr = (addr < 0x1000)
            ? cartridge_->chr0()
            : cartridge_->chr1();

        return chr.at(addr % 0x1000);
    }

    constexpr void write_chr(std::uint16_t addr, std::uint8_t value) const {
        assert(addr < 0x2000);
        cartridge_->chr_write(addr, value);
    }

    constexpr void write_oama(std::uint8_t value) { oam_.address = value; }
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
        if (address >= 0x2000 and address <= 0x3000) {
            name_table_.write(address & 0xFFF, value);
        } else if (address >= 0x3F00 and address <= 0x3FFF) {
            palette_table_.write(address & 0x001F, value);
        } else if (address < 0x2000) {
            write_chr(address, value);
        }
        // else: $3001-$3EFF nametable mirror, not yet handled (tracked separately)

        address += control.vram_address_increment();
    }

    constexpr void prerender_scanline_old() noexcept;
    constexpr void prerender_scanline() noexcept;

    std::uint8_t nametable_index_x_{0};
    std::uint8_t nametable_index_y_{0};

    [[nodiscard]] constexpr static auto nametable_tile_offset(auto tile_x, auto tile_y, int nametable_index) {
        return static_cast<std::uint16_t>((tile_y * 32 + tile_x) | nametable_index);
    }

    [[nodiscard]] constexpr static auto nametable_attr_offset(auto tile_x, auto tile_y, int nametable_index) {
        return static_cast<std::uint16_t>((0x3c0 + tile_y / 4 * 8 + tile_x / 4) | nametable_index);
    }

    [[nodiscard]] constexpr auto read_tile_pixel(auto ix, auto tile, auto x, auto y) const {
        const auto tile_offset = ix * 0x1000 + tile * 0x10;
        const auto tile_lsb = read_chr(static_cast<std::uint16_t>(tile_offset + y + 0));
        const auto tile_msb = read_chr(static_cast<std::uint16_t>(tile_offset + y + 8));
        const auto pixel_lo = (tile_lsb >> (7 - x)) & 0x01;
        const auto pixel_hi = (tile_msb >> (7 - x)) & 0x01;
        return static_cast<std::uint8_t>(pixel_lo | (pixel_hi << 1));
    }

    [[nodiscard]] constexpr auto read_tile_pixel16(auto tile, auto x, auto y, bool flipped_vertically) {
        const auto first_bite = (y < 8) == !flipped_vertically;// either top half of the sprite and not flipped
                                                               // or bottom half and flipped
        const auto tile_offset = (tile & 0x1) * 0x1000 + (tile & 0xFE) * 0x10 + (first_bite ? 0 : 1);
        const auto tile_lsb = read_chr(static_cast<std::uint16_t>(tile_offset + y + 0));
        const auto tile_msb = read_chr(static_cast<std::uint16_t>(tile_offset + y + 8));
        const auto pixel_lo = (tile_lsb >> (7 - x)) & 0x01;
        const auto pixel_hi = (tile_msb >> (7 - x)) & 0x01;
        return static_cast<std::uint8_t>(pixel_lo | (pixel_hi << 1));
    }

    [[nodiscard]] constexpr static auto read_tile_index(const auto& name_table, auto tile_x, auto tile_y, auto nametable_index) -> std::uint8_t {
        auto offset = nametable_tile_offset(tile_x, tile_y, nametable_index);
        return name_table.read(offset);
    }

    [[nodiscard]] constexpr static auto tile_palette(auto tile_x, auto tile_y, auto attr_byte) -> std::uint8_t {
        if ((tile_x % 4) >> 1) {
            attr_byte = attr_byte >> 2;
        }
        if ((tile_y % 4) >> 1) {
            attr_byte = attr_byte >> 4;
        }
        return static_cast<std::uint8_t>(attr_byte & 0x03);
    }

    [[nodiscard]] constexpr static auto read_tile_palette(const auto& name_table, auto tile_x, auto tile_y, auto nametable_index) {
        auto attr_byte_index = nametable_attr_offset(tile_x, tile_y, nametable_index);
        auto attr_byte = name_table.read(attr_byte_index);

        return tile_palette(tile_x, tile_y, attr_byte);
    }

    template <screen screen_t>
    constexpr void visible_scanline_old(screen_t& screen) {
        auto y = scan_.line();
        auto x = static_cast<short>(scan_.cycle() - 2);

        if (x >= 0 and x < 256) {
            auto [nametable_index_x, tile_x] = tile_x_scrolled(x);
            auto [nametable_index_y, tile_y] = tile_y_scrolled(y);

            auto tile_row = (y + scroll_y) % 8;
            auto tile_col = (x + scroll_x) % 8;

            auto nametable_addr = nametable_address(nametable_index_x, nametable_index_y);

            auto tile_index = read_tile_index(name_table_, tile_x, tile_y, nametable_addr);
            auto pixel = read_tile_pixel(control.pattern_table_bg_index(), tile_index, tile_col, tile_row);
            auto palette = read_tile_palette(name_table_, tile_x, tile_y, nametable_addr);

            screen.draw_pixel({x, y}, palette_table_.color_of(pixel, palette));

            // Sprite 0 hit
            auto s = oam_.sprites[0];
            if (x >= s.x and x < s.x + 8 and y >= s.y and y < s.y + 8 /* and pixel != 0*/) {
                auto dx = x - s.x;
                auto dy = y - s.y;
                auto j = (s.attr & 0x40) ? 7 - dx : dx;
                auto i = (s.attr & 0x80) ? 7 - dy : dy;
                auto sprite_pixel = read_tile_pixel(control.pattern_table_fg_index(), s.tile, j, i);
                if (sprite_pixel != 0) {
                    status |= 0x40;
                }
            }
        }

        if (scan_.cycle() == 257) {
            scroll_x = scroll_x_buffer;
            nametable_index_x_ = control.nametable_index_x();
        }
    }

    template <screen screen_t>
    constexpr void visible_scanline(screen_t& screen);

    template <screen screen_t>
    constexpr void postrender_scanline(screen_t& screen);

    template <screen screen_t>
    constexpr void postrender_scanline_old(screen_t& screen) noexcept {
        if (scan_.cycle() == 0) {
            for (const auto& s: oam_.sprites) {
                auto palette = static_cast<std::uint8_t>((s.attr & 0x03) + 4);

                for (auto i = 0; i < (control.sprite_size() == sprite_size::sprite8x8 ? 8 : 16); ++i) {
                    for (auto j = 0; j < 8; ++j) {
                        auto pixel = control.sprite_size() == sprite_size::sprite8x8
                            ? read_tile_pixel(control.pattern_table_fg_index(), s.tile, j, i)
                            : read_tile_pixel16(s.tile, j, i, (s.attr & 0x80) != 0);
                        auto dx = (s.attr & 0x40) ? 7 - j : j;// flipped horizontally
                        auto dy = (s.attr & 0x80)             // flipped vertically
                            ? (control.sprite_size() == sprite_size::sprite8x8 ? 7 : 15) - i
                            : i;
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
    constexpr void vertical_blank_line_old() noexcept {

        if (scan_.cycle() == 0) {
            status |= 0x80;
            nmi_raised = control.raise_vblank_nmi();
        }
    };

    [[nodiscard]] constexpr auto mirroring() const -> std::optional<name_table_mirroring> {
        if (cartridge_)
            return cartridge_->mirroring();
        else
            return std::nullopt;
    };

private:
    crt_scan scan_{SCANLINE_DOTS, VISIBLE_SCANLINES, POST_RENDER_SCANLINES, VERTICAL_BLANK_SCANLINES};

    nes::name_table name_table_{[this]() constexpr { return mirroring(); }};
    nes::palette_table palette_table_;
    nes::object_attribute_memory oam_;

    cartridge* cartridge_{nullptr};
    std::uint8_t data_read_buffer_;

    std::uint16_t addr_;
};

template <screen screen_t>
constexpr void ppu::tick_old(screen_t& screen) {
    if (scan_.is_prerender()) {
        prerender_scanline_old();
    } else if (scan_.is_visible()) {
        visible_scanline_old(screen);
    } else if (scan_.is_postrender()) {
        postrender_scanline_old(screen);
    } else if (scan_.is_vblank()) {
        vertical_blank_line_old();
    }

    scan_.advance();
}

template <screen screen_t>
constexpr void ppu::tick(screen_t& screen) {
    if (scan_.is_prerender()) {
        prerender_scanline();
    }

    if (scan_.is_prerender() or scan_.is_visible()) {
        visible_scanline(screen);
    } else if (scan_.is_postrender()) {
        postrender_scanline(screen);
    } else if (scan_.is_vblank()) {
        vertical_blank_line_old();
    }
    scan_.advance();
}

inline auto ppu::display_pattern_table(auto i, auto palette) const -> std::array<color, 128 * 128> {
    auto result = std::array<color, 128 * 128>{};

    for (std::uint16_t tile_y = 0; tile_y < 16; ++tile_y) {
        for (std::uint16_t tile_x = 0; tile_x < 16; ++tile_x) {
            auto offset = static_cast<std::uint16_t>(tile_y * 16 + tile_x);
            for (std::uint16_t row = 0; row < 8; ++row) {
                for (std::uint16_t col = 0; col < 8; col++) {
                    auto pixel = read_tile_pixel(i, offset, col, row);

                    auto result_offset = (tile_y * 8 + row) * 128 + tile_x * 8 + (7 - col);
                    auto result_color = palette_table_.color_of(pixel, palette);

                    result[result_offset] = result_color;
                }
            }
        }
    }
    return result;
}

constexpr void ppu::prerender_scanline_old() noexcept {
    if (scan_.cycle() == 0) {
        status = 0x00;
        control.smb_hotfix();
        nmi_raised = false;
        nmi_seen = false;
    }
    if (scan_.cycle() >= 280) {
        scroll_y = scroll_y_buffer;
        nametable_index_y_ = control.nametable_index_y();
    }
}

constexpr void ppu::prerender_scanline() noexcept {
    if (scan_.cycle() == 1) {
        status = 0x00;
    }
}

template <screen screen_t>
constexpr void ppu::visible_scanline(screen_t& screen) {
    if (scan_.cycle() >= 2 and scan_.cycle() <= 257) {
        // draw pixel
    }
    if ((scan_.cycle() >= 2 and scan_.cycle() <= 255) or (scan_.cycle() >= 322 and scan_.cycle() <= 337)) {
        // update registers
        switch (scan_.cycle() % 8) {
            case 1:
                // NT
                break;
            case 2:
                // NT
                break;
            case 3:
                // AT
                break;
            case 4:
                // AT
                break;
            case 5:
                // BG lsbits
                break;
            case 6:
                // BG lsbits
                break;
            case 7:
                // BG msbits
                break;
            case 0:
                // BG msbits
                // inc horiz v
                break;
        }
    }
    if (scan_.cycle() == 1 or scan_.cycle() == 321 or scan_.cycle() == 339) {
    }
    if (scan_.cycle() == 338 or scan_.cycle() == 340) {
    }
}

template <screen screen_t>
constexpr void ppu::postrender_scanline(screen_t& screen) {
}

template <screen screen_t>
void ppu::render_nametables(screen_t& screen) {
    for (auto y: std::views::iota(short{0}, short{256 * 2})) {
        for (auto x: std::views::iota(short{0}, short{256 * 2})) {
            const auto y_of_tile = (y % 256) / 8;
            const auto x_of_tile = (x % 256) / 8;
            const auto y_in_tile = (y % 256) % 8;
            const auto x_in_tile = (x % 256) % 8;

            auto nametable_addr = nametable_address(x < 256 ? 0 : 1, y < 256 ? 0 : 1);

            auto tile_index = read_tile_index(name_table_, x_of_tile, y_of_tile, nametable_addr);
            auto pixel = read_tile_pixel(control.pattern_table_bg_index(), tile_index, x_in_tile, y_in_tile);
            auto palette = read_tile_palette(name_table_, x_of_tile, y_of_tile, nametable_addr);

            screen.draw_pixel({x, y}, palette_table_.color_of(pixel, palette));
        }
    }
}

}// namespace nes