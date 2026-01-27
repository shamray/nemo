#pragma once

#include <cstdint>

namespace nes
{

// The PPU's internal position register ("v"/"t" in nesdev docs).
// A tile cursor in the 512x480 four-nametable plane; its bit layout IS the
// VRAM address of the tile under the cursor:
//
//  bit:  14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
//        │yyy │NN │  YYYYY   │   XXXXX      │
//        fine  nt   coarse Y     coarse X
//        Y     sel
class tile_cursor
{
public:
    constexpr tile_cursor() = default;
    constexpr explicit tile_cursor(std::uint16_t bits) noexcept: bits_{static_cast<std::uint16_t>(bits & 0x7FFF)} {}

    // ---- raw bit access (the $2006/$2007 path needs the flat value) -------
    [[nodiscard]] constexpr auto raw() const noexcept -> std::uint16_t { return bits_; }
    constexpr void assign_raw(std::uint16_t bits) noexcept { bits_ = static_cast<std::uint16_t>(bits & 0x7FFF); }

    // ---- named fields (bits, as hardware defines them) ---------------------
    [[nodiscard]] constexpr auto coarse_x() const noexcept -> int { return bits_ & 0x001F; }
    [[nodiscard]] constexpr auto coarse_y() const noexcept -> int { return (bits_ >> 5) & 0x001F; }
    [[nodiscard]] constexpr auto nametable() const noexcept -> int { return (bits_ >> 10) & 0x0003; }
    [[nodiscard]] constexpr auto fine_y() const noexcept -> int { return (bits_ >> 12) & 0x0007; }

    constexpr void set_coarse_x(int v) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x001F) | (v & 0x001F));
    }

    constexpr void set_coarse_y(int v) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x03E0) | ((v & 0x001F) << 5));
    }

    constexpr void set_nametable(int v) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x0C00) | ((v & 0x0003) << 10));
    }

    constexpr void set_fine_y(int v) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x7000) | ((v & 0x0007) << 12));
    }

    // set_nametable_x/y: the two nametable bits addressed separately, since
    // $2000 writes only one of them (bit 0) and $2005/$2006 sequences can
    // touch just one half via the reload_* split below.
    constexpr void set_nametable_x(int bit) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x0400) | ((bit & 1) << 10));
    }

    constexpr void set_nametable_y(int bit) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ & ~0x0800) | ((bit & 1) << 11));
    }

    [[nodiscard]] constexpr auto nametable_x() const noexcept -> int { return (bits_ >> 10) & 0x0001; }
    [[nodiscard]] constexpr auto nametable_y() const noexcept -> int { return (bits_ >> 11) & 0x0001; }

    // ---- position view: what the cursor MEANS on screen --------------------
    // Pixel coordinates of this tile's top-left corner in the 512x480 plane.
    [[nodiscard]] constexpr auto x_pixels() const noexcept -> int { return nametable_x() * 256 + coarse_x() * 8; }
    [[nodiscard]] constexpr auto y_pixels() const noexcept -> int
    {
        return nametable_y() * 240 + coarse_y() * 8 + fine_y();
    }

    // Inverse of the above -- construct a cursor from a scroll position.
    [[nodiscard]] constexpr static auto from_position(int x, int y) noexcept -> tile_cursor
    {
        const auto nt_x = (x / 256) & 1;
        const auto cx = (x % 256) / 8;
        const auto nt_y = (y / 240) & 1;
        const auto cy = (y % 240) / 8;
        const auto fy = (y % 240) % 8;

        auto cursor = tile_cursor{};
        cursor.set_coarse_x(cx);
        cursor.set_coarse_y(cy);
        cursor.set_nametable_x(nt_x);
        cursor.set_nametable_y(nt_y);
        cursor.set_fine_y(fy);
        return cursor;
    }

    // ---- address view: what the PPU FETCHES through this cursor ------------
    [[nodiscard]] constexpr auto tile_address() const noexcept -> std::uint16_t
    {
        return static_cast<std::uint16_t>(0x2000 | (bits_ & 0x0FFF));
    }

    [[nodiscard]] constexpr auto attribute_address() const noexcept -> std::uint16_t
    {
        return static_cast<std::uint16_t>(0x23C0 | (bits_ & 0x0C00) | ((coarse_y() / 4) << 3) | (coarse_x() / 4));
    }

    // ---- pipeline events: named after the timing diagram --------------------
    // Every 8th fetch dot (cycle%8==0), while rendering: advance one tile
    // right, wrapping the nametable at the screen edge.
    // nesdev: "Coarse X increment"
    constexpr void step_tile_right() noexcept
    {
        if (coarse_x() == 31) {
            set_coarse_x(0);
            set_nametable_x(nametable_x() ^ 1);
        } else {
            set_coarse_x(coarse_x() + 1);
        }
    }

    // Dot 256 of a rendering line: advance one pixel row down. Fine Y
    // increments 0->7 within a tile; on overflow, coarse Y increments,
    // wrapping 29->0 WITH a nametable flip, but 30/31->0 WITHOUT one
    // (the "attribute rows" edge case games exploit for y=240..255 scroll).
    // nesdev: "Y increment"
    constexpr void step_pixel_down() noexcept
    {
        if (fine_y() < 7) {
            set_fine_y(fine_y() + 1);
            return;
        }

        set_fine_y(0);
        const auto cy = coarse_y();
        if (cy == 29) {
            set_coarse_y(0);
            set_nametable_y(nametable_y() ^ 1);
        } else if (cy == 31) {
            set_coarse_y(0);
        } else {
            set_coarse_y(cy + 1);
        }
    }

    // Dot 257 of every rendering line (including pre-render): copy the
    // horizontal half (coarse X, nametable X) from `from`.
    // nesdev: "At dot 257 of each scanline... horizontal bits are copied"
    constexpr void reload_column_from(tile_cursor from) noexcept
    {
        set_coarse_x(from.coarse_x());
        set_nametable_x(from.nametable_x());
    }

    // Pre-render line, dots 280-304: copy the vertical half (fine Y,
    // coarse Y, nametable Y) from `from`.
    // nesdev: "During dots 280 to 304... vertical bits are copied"
    constexpr void reload_row_from(tile_cursor from) noexcept
    {
        set_fine_y(from.fine_y());
        set_coarse_y(from.coarse_y());
        set_nametable_y(from.nametable_y());
    }

    // $2007 access while rendering is OFF: flat pointer increment, +1 or
    // +32 per $2000 bit 2. (While rendering IS on, hardware increments both
    // coarse X and Y instead on any $2007 access -- a well-known glitch,
    // deliberately NOT modeled; see doc/loopy-register-design.md §7.)
    constexpr void advance(int vram_increment) noexcept
    {
        bits_ = static_cast<std::uint16_t>((bits_ + vram_increment) & 0x7FFF);
    }

    friend constexpr auto operator==(tile_cursor, tile_cursor) noexcept -> bool = default;

private:
    std::uint16_t bits_{0};// shifts/masks live ONLY here
};

struct scroll_registers
{
    tile_cursor current;// "v": live cursor, sweeps during rendering, flat $2007 pointer otherwise
    tile_cursor staged;// "t": software-configured top-left corner
    std::uint8_t fine_x{0};// "x": pixel phase 0-7, NOT double-buffered
    bool second_write{false};// "w": $2005/$2006 write-pair phase

    // ---- register-write handlers: one per PPU port that touches scroll -----
    constexpr void write_control(std::uint8_t value) noexcept// $2000
    {
        staged.set_nametable(value & 0x03);
    }

    constexpr void write_scroll(std::uint8_t value) noexcept// $2005
    {
        if (not second_write) {
            staged.set_coarse_x(value >> 3);
            fine_x = static_cast<std::uint8_t>(value & 0x07);
        } else {
            staged.set_fine_y(value & 0x07);
            staged.set_coarse_y(value >> 3);
        }
        second_write = not second_write;
    }

    constexpr void write_address_hi(std::uint8_t value) noexcept// $2006, first write
    {
        staged.assign_raw(static_cast<std::uint16_t>((staged.raw() & 0x00FF) | ((value & 0x3F) << 8)));
        second_write = true;
    }

    constexpr void write_address_lo(std::uint8_t value) noexcept// $2006, second write
    {
        staged.assign_raw(static_cast<std::uint16_t>((staged.raw() & 0x7F00) | value));
        current = staged;
        second_write = false;
    }

    constexpr void reset_write_toggle() noexcept// $2002 read
    {
        second_write = false;
    }

    // Effective scroll position, for anything that still wants pixels
    // (debug views, the bridge step). Never stored -- always derived.
    [[nodiscard]] constexpr auto scroll_x() const noexcept -> int { return staged.x_pixels() + fine_x; }
    [[nodiscard]] constexpr auto scroll_y() const noexcept -> int { return staged.y_pixels(); }
};

}// namespace nes
