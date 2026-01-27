#include <libnes/ppu_scroll.hpp>
#include <catch2/catch_all.hpp>

namespace {

// Equivalence proofs vs the canonical nesdev bit expressions -- compile-time,
// sweep every 15-bit value so there is nowhere for a shift/mask typo to hide.
constexpr auto attribute_address_matches_canonical_formula() -> bool
{
    for (std::uint32_t bits = 0; bits < 0x8000; ++bits) {
        auto c = nes::tile_cursor{static_cast<std::uint16_t>(bits)};
        std::uint16_t canonical = 0x23C0 | (bits & 0x0C00) | ((bits >> 4) & 0x38) | ((bits >> 2) & 0x07);
        if (c.attribute_address() != canonical) return false;
    }
    return true;
}

constexpr auto tile_address_matches_canonical_formula() -> bool
{
    for (std::uint32_t bits = 0; bits < 0x8000; ++bits) {
        auto c = nes::tile_cursor{static_cast<std::uint16_t>(bits)};
        if (c.tile_address() != (0x2000 | (bits & 0x0FFF))) return false;
    }
    return true;
}

static_assert(attribute_address_matches_canonical_formula());
static_assert(tile_address_matches_canonical_formula());
static_assert(nes::tile_cursor::from_position(120, 48).tile_address() == 0x20CF);

}

TEST_CASE("tile_cursor - field accessors") {
    SECTION("bit positions read independently") {
        CHECK(nes::tile_cursor{0x001F}.coarse_x() == 31);
        CHECK(nes::tile_cursor{0x03E0}.coarse_y() == 31);
        CHECK(nes::tile_cursor{0x0400}.nametable_x() == 1);
        CHECK(nes::tile_cursor{0x0800}.nametable_y() == 1);
        CHECK(nes::tile_cursor{0x0C00}.nametable() == 3);
        CHECK(nes::tile_cursor{0x7000}.fine_y() == 7);
    }

    SECTION("setters round-trip") {
        auto c = nes::tile_cursor{};

        c.set_coarse_x(17);
        CHECK(c.coarse_x() == 17);

        c = nes::tile_cursor{};
        c.set_coarse_y(31);
        CHECK(c.coarse_y() == 31);
        CHECK(c.raw() == 0x03E0);

        c = nes::tile_cursor{};
        c.set_nametable(2);
        CHECK(c.nametable() == 2);
        CHECK(c.nametable_x() == 0);
        CHECK(c.nametable_y() == 1);

        c = nes::tile_cursor{};
        c.set_nametable_x(1);
        CHECK(c.nametable_x() == 1);
        c.set_nametable_y(1);
        CHECK(c.nametable_y() == 1);
        CHECK(c.nametable() == 3);

        c = nes::tile_cursor{};
        c.set_fine_y(5);
        CHECK(c.fine_y() == 5);
    }

    SECTION("setters mask/truncate to field width") {
        auto c = nes::tile_cursor{};
        c.set_coarse_x(255);
        CHECK(c.coarse_x() == 31);
        CHECK(c.raw() == 0x001F);
    }

    SECTION("assign_raw masks bit 14 down to a 15-bit value") {
        auto c = nes::tile_cursor{};
        c.assign_raw(0xFFFF);
        CHECK(c.raw() == 0x7FFF);
    }
}

TEST_CASE("tile_cursor - position view") {
    SECTION("nametable corners") {
        CHECK(nes::tile_cursor::from_position(0, 0).nametable() == 0);
        CHECK(nes::tile_cursor::from_position(255, 0).nametable() == 0);
        CHECK(nes::tile_cursor::from_position(256, 0).nametable() == 1);
        CHECK(nes::tile_cursor::from_position(0, 239).nametable() == 0);
        CHECK(nes::tile_cursor::from_position(0, 240).nametable() == 2);
        CHECK(nes::tile_cursor::from_position(256, 240).nametable() == 3);
    }

    SECTION("round trip through exact (multiple-of-8, plus fine y) coordinates") {
        for (auto x : {0, 8, 120, 248, 256, 264, 504}) {
            CHECK(nes::tile_cursor::from_position(x, 0).x_pixels() == x);
        }
        for (auto y : {0, 8, 48, 232, 240, 248, 472}) {
            CHECK(nes::tile_cursor::from_position(0, y).y_pixels() == y);
        }
        CHECK(nes::tile_cursor::from_position(120, 45).y_pixels() == 45);
    }
}

TEST_CASE("tile_cursor - address view") {
    SECTION("from_position(120, 48) lands on the address worked out by hand") {
        // x=120 -> nt_x=0, coarse_x=15; y=48 -> nt_y=0, coarse_y=6, fine_y=0
        // bits = 15 | (6<<5) = 0x0CF -> address = 0x2000|0x0CF = 0x20CF
        CHECK(nes::tile_cursor::from_position(120, 48).tile_address() == 0x20CF);
    }
}

TEST_CASE("tile_cursor - step_tile_right") {
    SECTION("30 steps from coarse_x 0 stay within the same nametable half") {
        auto c = nes::tile_cursor{};
        for (auto i = 0; i < 30; ++i) c.step_tile_right();
        CHECK(c.coarse_x() == 30);
        CHECK(c.nametable_x() == 0);
    }

    SECTION("the 32nd step wraps coarse_x to 0 and flips nametable_x") {
        auto c = nes::tile_cursor{};
        for (auto i = 0; i < 32; ++i) c.step_tile_right();
        CHECK(c.coarse_x() == 0);
        CHECK(c.nametable_x() == 1);
    }

    SECTION("two full 32-step sweeps return to the identical raw value") {
        auto c = nes::tile_cursor::from_position(37, 91);
        const auto original = c.raw();
        for (auto i = 0; i < 64; ++i) c.step_tile_right();
        CHECK(c.raw() == original);
    }
}

TEST_CASE("tile_cursor - step_pixel_down") {
    SECTION("fine_y increments 0..6 without touching coarse_y") {
        auto c = nes::tile_cursor{};
        for (auto i = 0; i < 6; ++i) c.step_pixel_down();
        CHECK(c.fine_y() == 6);
        CHECK(c.coarse_y() == 0);
    }

    SECTION("coarse Y 29 wraps to 0 WITH a nametable flip") {
        auto c = nes::tile_cursor{};
        c.set_coarse_y(29);
        c.set_fine_y(7);
        c.step_pixel_down();
        CHECK(c.fine_y() == 0);
        CHECK(c.coarse_y() == 0);
        CHECK(c.nametable_y() == 1);
    }

    SECTION("coarse Y 30 increments to 31 without wrapping (attribute rows are unused for tiles, so only 31 wraps)") {
        auto c30 = nes::tile_cursor{};
        c30.set_coarse_y(30);
        c30.set_fine_y(7);
        c30.step_pixel_down();
        CHECK(c30.coarse_y() == 31);
        CHECK(c30.nametable_y() == 0);
    }

    SECTION("coarse Y 31 wraps to 0 without a nametable flip") {
        auto c31 = nes::tile_cursor{};
        c31.set_coarse_y(31);
        c31.set_fine_y(7);
        c31.step_pixel_down();
        CHECK(c31.coarse_y() == 0);
        CHECK(c31.nametable_y() == 0);
    }

    SECTION("crossing from y=239 lands on y_pixels()==240 with nametable flipped") {
        auto c = nes::tile_cursor::from_position(0, 239);
        c.step_pixel_down();
        CHECK(c.y_pixels() == 240);
        CHECK(c.nametable_y() == 1);
    }
}

TEST_CASE("tile_cursor - reload_column_from / reload_row_from") {
    auto from = nes::tile_cursor::from_position(200, 100);

    SECTION("reload_column_from touches only coarse_x and nametable_x") {
        auto to = nes::tile_cursor::from_position(10, 20);
        const auto before = to.raw();
        to.reload_column_from(from);

        CHECK(to.coarse_x() == from.coarse_x());
        CHECK(to.nametable_x() == from.nametable_x());
        CHECK((to.raw() & ~0x041F) == (before & ~0x041F));
    }

    SECTION("reload_row_from touches only fine_y, coarse_y and nametable_y") {
        auto to = nes::tile_cursor::from_position(10, 20);
        const auto before = to.raw();
        to.reload_row_from(from);

        CHECK(to.fine_y() == from.fine_y());
        CHECK(to.coarse_y() == from.coarse_y());
        CHECK(to.nametable_y() == from.nametable_y());
        CHECK((to.raw() & ~0x7BE0) == (before & ~0x7BE0));
    }
}

TEST_CASE("tile_cursor - advance") {
    SECTION("wraps at 15 bits") {
        auto c = nes::tile_cursor{0x7FFF};
        c.advance(1);
        CHECK(c.raw() == 0x0000);
    }

    SECTION("+1 increment") {
        auto c = nes::tile_cursor{0x0010};
        c.advance(1);
        CHECK(c.raw() == 0x0011);
    }

    SECTION("+32 increment") {
        auto c = nes::tile_cursor{0x0010};
        c.advance(32);
        CHECK(c.raw() == 0x0030);
    }
}

TEST_CASE("scroll_registers - write handlers") {
    SECTION("$2005,$2005 sets coarse/fine X and Y on staged only") {
        auto s = nes::scroll_registers{};
        s.write_scroll(0b10101'011);// coarse_x=21, fine_x=3
        CHECK(s.staged.coarse_x() == 21);
        CHECK(s.fine_x == 3);
        CHECK(s.second_write);
        CHECK(s.current == nes::tile_cursor{});

        s.write_scroll(0b01010'110);// coarse_y=10, fine_y=6
        CHECK(s.staged.coarse_y() == 10);
        CHECK(s.staged.fine_y() == 6);
        CHECK_FALSE(s.second_write);
        CHECK(s.current == nes::tile_cursor{});
    }

    SECTION("$2006,$2006 copies staged into current only after the second write") {
        auto s = nes::scroll_registers{};
        s.write_address_hi(0x3F);
        CHECK(s.second_write);
        CHECK(s.current == nes::tile_cursor{});
        CHECK(s.staged.raw() == 0x3F00);

        s.write_address_lo(0xD0);
        CHECK_FALSE(s.second_write);
        CHECK(s.staged.raw() == 0x3FD0);
        CHECK(s.current == s.staged);
    }

    SECTION("$2002 read mid-sequence resets the write toggle (finding #14)") {
        auto s = nes::scroll_registers{};
        s.write_address_hi(0x2A);
        CHECK(s.second_write);

        s.reset_write_toggle();
        CHECK_FALSE(s.second_write);

        // Now reinterpreted as a first write again, not a second one.
        s.write_address_hi(0x15);
        CHECK(s.second_write);
        CHECK(s.staged.raw() == 0x1500);
    }

    SECTION("four-write $2006/$2005/$2005/$2006 mid-frame repositioning trick") {
        // Konami-style: reposition `current` mid-frame via $2006, then
        // restore `staged` via $2005/$2005 so the next frame scrolls
        // correctly, without disturbing the mid-frame `current` position.
        auto s = nes::scroll_registers{};
        s.write_scroll(0b00001'010);// stage the "real" scroll: coarse_x=1, fine_x=2
        s.write_scroll(0b00010'011);// coarse_y=2, fine_y=3
        CHECK_FALSE(s.second_write);
        const auto staged_before_reposition = s.staged;

        s.write_address_hi(0x20);// reposition current mid-frame
        s.write_address_lo(0x00);
        CHECK(s.current.raw() == 0x2000);

        s.write_scroll(0b00001'010);// restore staged for next frame
        s.write_scroll(0b00010'011);
        CHECK(s.staged == staged_before_reposition);
        CHECK(s.current.raw() == 0x2000);// current untouched by the $2005 pair
    }
}
