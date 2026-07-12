#pragma once

#include <libnes/ppu_crt_scan.hpp>
#include <libnes/ppu_name_table.hpp>
#include <libnes/ppu_object_attribute_memory.hpp>
#include <libnes/ppu_palette_table.hpp>
#include <libnes/ppu_scroll.hpp>

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

    scroll_registers scroll_;

    bool nmi_raised{false};
    bool nmi_seen{false};

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
                auto nmi_was_enabled = control.raise_vblank_nmi();
                control.assign(value);
                scroll_.write_control(value);

                // Enabling NMI while the vblank flag is already set fires an
                // immediate NMI on real hardware (edge-triggered on the AND
                // of the flag and the enable bit)
                if (not nmi_was_enabled and control.raise_vblank_nmi() and (status & 0x80) != 0) {
                    nmi_raised = true;
                }
                return;
            }
            case 0x2001: {
                mask = value;
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
        auto tile_x = (x + active_scroll_x()) / 8;
        auto nametable_index_x = render_nametable_x_;

        if (tile_x >= 32) {// wrap nametable while scrolling horizontally
            tile_x %= 32;
            nametable_index_x ^= 1;
        }
        return std::tuple{nametable_index_x, tile_x};
    }

    [[nodiscard]] constexpr auto tile_y_scrolled(short y) const {
        auto tile_y = (y + active_scroll_y()) / 8;
        auto nametable_index_y = render_nametable_y_;

        if (active_scroll_y() / 8 >= 30) {
            // "negative scroll" (coarse Y 30/31): the attribute rows show as
            // garbage tiles, then coarse Y wraps 31->0 into the SAME
            // nametable - hardware never flips NT on this path (see
            // tile_cursor::step_pixel_down)
            if (tile_y >= 32)
                tile_y -= 32;
        } else if (tile_y >= 30) {// wrap nametable while scrolling vertically
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
    // Scroll as the OLD renderer consumes it, latched from `staged` at the
    // hardware copy points (dot 257 for X, pre-render 280-304 for Y). The
    // old path models scroll as absolute per-frame offsets, so it must not
    // observe `current` moving mid-frame under $2006/$2007 traffic -- that
    // register doubles as the live VRAM pointer. Fine X is the exception:
    // hardware applies it at the pixel mux immediately (finding #40).
    [[nodiscard]] constexpr auto active_scroll_x() const noexcept -> int {
        return render_scroll_x_ + scroll_.fine_x;
    }
    [[nodiscard]] constexpr auto active_scroll_y() const noexcept -> int {
        return render_scroll_y_;
    }

    constexpr void latch_render_scroll_x() noexcept {
        scroll_.current.reload_column_from(scroll_.staged);
        render_scroll_x_ = scroll_.staged.coarse_x() * 8;
        render_nametable_x_ = scroll_.staged.nametable_x();
    }

    constexpr void latch_render_scroll_y() noexcept {
        scroll_.current.reload_row_from(scroll_.staged);
        render_scroll_y_ = scroll_.staged.coarse_y() * 8 + scroll_.staged.fine_y();
        render_nametable_y_ = scroll_.staged.nametable_y();
    }

    [[nodiscard]] constexpr auto read_stat() -> std::uint8_t {
        auto requested_status = static_cast<std::uint8_t>(status & 0xe0);
        status &= 0x60;// reset vblank
        scroll_.reset_write_toggle();
        return requested_status;
    }

    [[nodiscard]] constexpr auto read_data() -> std::uint8_t {
        // v is 15 bits, but only 14 reach the bus -- bit 14 (fine Y's top
        // bit) is not wired out, so a vertically-scrolled v must not leak
        // into the address decode below.
        auto a = static_cast<std::uint16_t>(scroll_.current.raw() & 0x3FFF);
        scroll_.current.advance(control.vram_address_increment());
        auto r = data_read_buffer_;
        auto& b = data_read_buffer_;

        if (a >= 0x2000 and a <= 0x3EFF) {
            // $3000-$3EFF mirrors $2000-$2EFF
            b = name_table_.read(a & 0xFFF);
        } else if (a >= 0x3F00) {
            r = b = palette_table_.read(a & 0x001F);
        } else {
            b = read_chr(a);
        }

        return r;
    }

    [[nodiscard]] constexpr auto read_chr(std::uint16_t addr) const -> std::uint8_t {
        assert(addr < 0x2000);
        return cartridge_->chr_read(addr);
    }

    constexpr void write_chr(std::uint16_t addr, std::uint8_t value) const {
        assert(addr < 0x2000);
        cartridge_->chr_write(addr, value);
    }

    constexpr void write_oama(std::uint8_t value) { oam_.address = value; }
    void write_oamd(std::uint8_t value) { oam_.write(value); }

    constexpr void write_scrl(std::uint8_t value) { scroll_.write_scroll(value); }

    constexpr void write_addr(std::uint8_t value) {
        if (not scroll_.second_write) {
            scroll_.write_address_hi(value);
        } else {
            scroll_.write_address_lo(value);
        }
    }

    constexpr void write_data(std::uint8_t value) {
        // Same 14-bit bus mask as read_data
        auto address = static_cast<std::uint16_t>(scroll_.current.raw() & 0x3FFF);
        if (address >= 0x2000 and address <= 0x3EFF) {
            // $3000-$3EFF mirrors $2000-$2EFF
            name_table_.write(address & 0xFFF, value);
        } else if (address >= 0x3F00) {
            palette_table_.write(address & 0x001F, value);
        } else {
            write_chr(address, value);
        }

        scroll_.current.advance(control.vram_address_increment());
    }

    constexpr void prerender_scanline_old() noexcept;
    constexpr void prerender_scanline() noexcept;

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

    [[nodiscard]] constexpr auto read_tile_pixel16(auto tile, auto x, auto y) const {
        const auto tile_offset = (tile & 0x1) * 0x1000 + ((tile & 0xFE) + y / 8) * 0x10;
        const auto tile_lsb = read_chr(static_cast<std::uint16_t>(tile_offset + y % 8 + 0));
        const auto tile_msb = read_chr(static_cast<std::uint16_t>(tile_offset + y % 8 + 8));
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

            auto tile_row = (y + active_scroll_y()) % 8;
            auto tile_col = (x + active_scroll_x()) % 8;

            auto nametable_addr = nametable_address(nametable_index_x, nametable_index_y);

            auto tile_index = read_tile_index(name_table_, tile_x, tile_y, nametable_addr);
            auto pixel = read_tile_pixel(control.pattern_table_bg_index(), tile_index, tile_col, tile_row);
            auto palette = read_tile_palette(name_table_, tile_x, tile_y, nametable_addr);

            auto background_shown = show_background() and (x >= 8 or show_background_leftmost());

            screen.draw_pixel({x, y}, background_shown ? palette_table_.color_of(pixel, palette) : palette_table_.color_of(0, 0));

            // Sprite 0 hit (requires both background and sprites enabled).
            // OAM Y is the sprite's top row minus 1 on real hardware, hence + 1.
            if (background_shown and show_sprites()) {
                auto s = oam_.sprites[0];
                auto height = control.sprite_size() == sprite_size::sprite8x8 ? 8 : 16;
                if (x >= s.x and x < s.x + 8 and y >= s.y + 1 and y < s.y + 1 + height /* and pixel != 0*/) {
                    auto dx = x - s.x;
                    auto dy = y - (s.y + 1);
                    auto j = (s.attr & 0x40) ? 7 - dx : dx;
                    auto i = (s.attr & 0x80) ? height - 1 - dy : dy;
                    auto sprite_pixel = control.sprite_size() == sprite_size::sprite8x8
                        ? read_tile_pixel(control.pattern_table_fg_index(), s.tile, j, i)
                        : read_tile_pixel16(s.tile, j, i);
                    if (sprite_pixel != 0) {
                        status |= 0x40;
                    }
                }
            }
        }

        // "At dot 257 of each scanline... horizontal bits are copied" -- the
        // pre-render line does the same copy, in prerender_scanline_old.
        // Hardware only performs the copy while rendering is enabled; with
        // it disabled, v is a plain VRAM pointer that games stream through
        // via $2007 (boot-time nametable clears!) and must not be touched.
        if (scan_.cycle() == 257 and rendering_enabled()) {
            latch_render_scroll_x();
        }
    }

    template <screen screen_t>
    constexpr void visible_scanline(screen_t& screen);

    template <screen screen_t>
    constexpr void postrender_scanline(screen_t& screen);

    template <screen screen_t>
    constexpr void postrender_scanline_old(screen_t& screen) noexcept {
        if (scan_.cycle() == 0 and show_sprites()) {
            for (const auto& s: oam_.sprites) {
                auto palette = static_cast<std::uint8_t>((s.attr & 0x03) + 4);

                for (auto i = 0; i < (control.sprite_size() == sprite_size::sprite8x8 ? 8 : 16); ++i) {
                    for (auto j = 0; j < 8; ++j) {
                        auto pixel = control.sprite_size() == sprite_size::sprite8x8
                            ? read_tile_pixel(control.pattern_table_fg_index(), s.tile, j, i)
                            : read_tile_pixel16(s.tile, j, i);
                        auto dx = (s.attr & 0x40) ? 7 - j : j;// flipped horizontally
                        auto dy = (s.attr & 0x80)             // flipped vertically
                            ? (control.sprite_size() == sprite_size::sprite8x8 ? 7 : 15) - i
                            : i;
                        auto screen_x = static_cast<short>(s.x + dx);
                        if (pixel and (screen_x >= 8 or show_sprites_leftmost())) {
                            // sprites appear one scanline below OAM Y, same
                            // as the sprite-0 hit check
                            screen.draw_pixel(
                                {screen_x, static_cast<short>(s.y + 1 + dy)},
                                palette_table_.color_of(pixel, palette)
                            );
                        }
                    }
                }
            }
        }
    }
    constexpr void vertical_blank_line_old() noexcept {
        // Set once, at the true start of vblank - not on every one of the 20
        // vblank scanlines, or a $2002 read here can never observe the flag
        // staying clear (the next scanline forces it back on within the
        // same vblank period). Real hardware sets it at dot 1 of this line,
        // not dot 0.
        if (scan_.line() == VISIBLE_SCANLINES + POST_RENDER_SCANLINES and scan_.cycle() == 1) {
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

    // $2001 PPUMASK
    [[nodiscard]] constexpr auto show_background() const noexcept -> bool { return (mask & 0x08) != 0; }
    [[nodiscard]] constexpr auto show_sprites() const noexcept -> bool { return (mask & 0x10) != 0; }
    [[nodiscard]] constexpr auto rendering_enabled() const noexcept -> bool { return show_background() or show_sprites(); }
    [[nodiscard]] constexpr auto show_background_leftmost() const noexcept -> bool { return (mask & 0x02) != 0; }
    [[nodiscard]] constexpr auto show_sprites_leftmost() const noexcept -> bool { return (mask & 0x04) != 0; }

private:
    // The old renderer's latched scroll view -- see active_scroll_x/y
    int render_scroll_x_{0};
    int render_scroll_y_{0};
    int render_nametable_x_{0};
    int render_nametable_y_{0};

    crt_scan scan_{SCANLINE_DOTS, VISIBLE_SCANLINES, POST_RENDER_SCANLINES, VERTICAL_BLANK_SCANLINES};

    nes::name_table name_table_{[this]() constexpr { return mirroring(); }};
    nes::palette_table palette_table_;
    nes::object_attribute_memory oam_;

    cartridge* cartridge_{nullptr};
    std::uint8_t data_read_buffer_;
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
    // Real hardware clears vblank/sprite-0/overflow at dot 1 of the
    // pre-render line, not dot 0 -- matches the dot-1 set in
    // vertical_blank_line_old, so the flag's held duration is unchanged.
    if (scan_.cycle() == 1) {
        status = 0x00;
        control.smb_hotfix();
        nmi_raised = false;
        nmi_seen = false;
    }
    // The pre-render line runs the same dot-257 horizontal copy every
    // rendering line does -- this is what makes scanline 0 of the next
    // frame start from the software-staged scroll position instead of
    // whatever was left over from the previous frame's playfield. Both
    // copies only happen while rendering is enabled (see the dot-257 note
    // in visible_scanline_old).
    if (scan_.cycle() == 257 and rendering_enabled()) {
        latch_render_scroll_x();
    }
    if (scan_.cycle() >= 280 and scan_.cycle() <= 304 and rendering_enabled()) {
        latch_render_scroll_y();
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