#include <libnes/literals.hpp>
#include <libnes/ppu.hpp>

#include <catch2/catch_all.hpp>

#include "libnes/cartridge.hpp"
#include <ranges>
#include <unordered_map>

using namespace nes::literals;

namespace
{

void tick(auto& ppu, auto& screen, int times = 1) {
    for (auto i = 0; i < times; ++i) {
        ppu.tick_old(screen);
    }
}

struct test_screen {
    class hasher
    {
    public:
        [[nodiscard]] constexpr auto operator()(const nes::point& p) const noexcept -> std::size_t {
            return p.x << 8 | p.y;
        }
    };

    std::unordered_map<nes::point, nes::color, hasher> pixels;

    [[nodiscard]] constexpr static auto width() -> short { return 256; }

    [[nodiscard]] constexpr static auto height() -> short { return 240; }

    void draw_pixel(nes::point where, nes::color color) {
        pixels[where] = color;
    }
};

struct test_cartridge: nes::cartridge {
    nes::membank<8_Kb> cart_chr{};
    nes::name_table_mirroring cart_mirroring{nes::name_table_mirroring::vertical};

    test_cartridge() = default;
    test_cartridge(nes::membank<8_Kb> chr, nes::name_table_mirroring mirroring = nes::name_table_mirroring::vertical)
        : cart_chr{std::move(chr)}
        , cart_mirroring{mirroring} {}

    [[nodiscard]] auto chr0() const noexcept -> const nes::membank<4_Kb>& override {
        std::copy_n(cart_chr.begin(), 4_Kb, tmp_.begin());
        return tmp_;
    }

    [[nodiscard]] auto chr1() const noexcept -> const nes::membank<4_Kb>& override {
        std::copy_n(std::next(cart_chr.begin(), 4_Kb), 4_Kb, tmp_.begin());
        return tmp_;
    }

    [[nodiscard]] auto mirroring() const noexcept -> nes::name_table_mirroring override {
        return cart_mirroring;
    }

    auto write([[maybe_unused]] std::uint16_t addr, [[maybe_unused]] std::uint8_t value) -> bool override {
        return false;
    }

    void chr_write(std::uint16_t addr, std::uint8_t value) noexcept override {
        cart_chr[addr % 8_Kb] = value;
    }

    [[nodiscard]] auto read([[maybe_unused]] std::uint16_t addr) -> std::optional<std::uint8_t> override {
        return std::nullopt;
    }

private:
    mutable nes::membank<4_Kb> tmp_;
};


template <class... args_t>
void write(std::uint16_t addr, nes::ppu& ppu, int byte, args_t... args) {
    if ((byte & 0xff) != byte)
        throw std::logic_error(std::format("{} is not byte", byte));
    ppu.write(addr, static_cast<std::uint8_t>(byte));
    write(addr, ppu, args...);
}

template <>
void write(std::uint16_t addr, nes::ppu& ppu, int byte) {
    ppu.write(addr, static_cast<std::uint8_t>(byte));
}

auto empty_pattern_table() {
    return std::array<std::uint8_t, 8_Kb>{0x00};
}

template <class input_type>
auto pattern_table(int index, input_type&& tile) {
    assert(tile.size() == 16);
    auto chr = empty_pattern_table();
    std::ranges::copy(tile, std::next(std::begin(chr), index * tile.size()));
    return chr;
}

template <class input_type, class... args_t>
auto pattern_table(int index, input_type&& tile, args_t... args) {
    assert(tile.size() == 16);
    auto chr = pattern_table(args...);
    std::ranges::copy(tile, std::next(std::begin(chr), index * tile.size()));
    return chr;
}

}// namespace

TEST_CASE("PPU") {
    auto screen = test_screen{};
    auto ppu = nes::ppu{nes::DEFAULT_COLORS};

    static constexpr auto BLACK = nes::DEFAULT_COLORS[63];
    static constexpr auto VIOLET = nes::DEFAULT_COLORS[3];
    static constexpr auto OLIVE = nes::DEFAULT_COLORS[8];
    static constexpr auto RASPBERRY = nes::DEFAULT_COLORS[21];
    static constexpr auto ORANGE = nes::DEFAULT_COLORS[22];
    static constexpr auto BLUE = nes::DEFAULT_COLORS[33];
    static constexpr auto CYAN = nes::DEFAULT_COLORS[44];
    static constexpr auto WHITE = nes::DEFAULT_COLORS[48];

    SECTION("power up state") {
        CHECK(ppu.control.value() == 0x00);
        CHECK(ppu.mask == 0x00);
    }

    SECTION("start of the frame") {
        ppu.status |= 0x40;
        ppu.tick_old(screen);

        CHECK((ppu.status & 0x40) == 0);
    }

    SECTION("write to palette ram") {
        write(0x2000, ppu, 0x00);
        write(0x2006, ppu, 0x3F, 0x00);
        write(0x2007, ppu, 63, 3, 8, 21);

        CHECK(ppu.palette_table().read(0) == 63);
        CHECK(ppu.palette_table().read(1) == 3);
        CHECK(ppu.palette_table().read(2) == 8);
        CHECK(ppu.palette_table().read(3) == 21);
    }

    SECTION("read pattern tables") {
        auto cartridge = test_cartridge{};

        ppu.load_cartridge(&cartridge);

        cartridge.cart_chr[0x0000] = 0x01;
        cartridge.cart_chr[0x0042] = 0x42;
        cartridge.cart_chr[0x0FFF] = 0xBC;
        cartridge.cart_chr[0x1000] = 0x19;
        cartridge.cart_chr[0x1991] = 0x91;
        cartridge.cart_chr[0x1FFF] = 0xAD;

        auto read_data = [&ppu](auto hi, auto lo) {
            ppu.write(0x2006, static_cast<std::uint8_t>(hi));
            ppu.write(0x2006, static_cast<std::uint8_t>(lo));
            [[maybe_unused]] auto old_read_buf = ppu.read(0x2007);
            return ppu.read(0x2007);
        };

        // CHR 0
        CHECK(read_data(0x00, 0x00) == 0x01);
        CHECK(read_data(0x00, 0x42) == 0x42);
        CHECK(read_data(0x0F, 0xFF) == 0xBC);

        // CHR 1
        CHECK(read_data(0x10, 0x00) == 0x19);
        CHECK(read_data(0x19, 0x91) == 0x91);
        CHECK(read_data(0x1F, 0xFF) == 0xAD);
    }

    SECTION("loading sprites") {
        SECTION("OAMADDR/OAMDATA") {
            write(0x2003, ppu, 1 * sizeof(nes::sprite));

            auto sprite = nes::sprite{.y = 0, .tile = 1, .attr = 0x00, .x = 0};
            write(0x2004, ppu, sprite.y);
            write(0x2004, ppu, sprite.tile);
            write(0x2004, ppu, sprite.attr);
            write(0x2004, ppu, sprite.x);

            CHECK(ppu.oam().sprites[1] == sprite);
        }

        SECTION("DMA") {
            auto sprites = std::array<nes::sprite, 64>{};
            auto mempage = std::bit_cast<std::uint8_t*>(sprites.data());

            sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x00, .x = 0};

            ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

            CHECK(ppu.oam().sprites == sprites);
        }
    }

    SECTION("rendering frame") {
        // Palette
        write(0x2006, ppu, 0x3F, 0x00);
        write(0x2007, ppu, 63, 3, 8, 21, 63, 48, 33, 22, 63, 0, 0, 0, 63, 0, 0, 0, 63, 33, 22, 44, 63, 8, 21, 48, 63, 0, 0, 0, 63, 0, 0, 0, 63);

        // Pattern table
        auto chr = pattern_table(
            1, std::array{0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// single point in top left corner
                          0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

            42, std::array{0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// single point in the second row
                           0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

            99, std::array{0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// points in top row; colors: 3, 2, 1, 0
                           0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        );
        auto cartridge = test_cartridge{std::move(chr)};
        ppu.load_cartridge(&cartridge);

        write(0x2001, ppu, 0x1E);// show background and sprites, including leftmost 8 pixels

        SECTION("computing nametable addresses") {

            SECTION("nametable 0") {
                CHECK(ppu.nametable_address(0, 0) == 0x0000);
            }
            SECTION("nametable 1") {
                CHECK(ppu.nametable_address(1, 0) == 0x0400);
            }
            SECTION("nametable 2") {
                CHECK(ppu.nametable_address(0, 1) == 0x0800);
            }
            SECTION("nametable 3") {
                CHECK(ppu.nametable_address(1, 1) == 0x0C00);
            }
        }

        SECTION("mask register") {
            write(0x2006, ppu, 0x20, 0x00);// Nametable
            write(0x2007, ppu, 1);// tile with a point at (0,0)

            SECTION("rendering disabled shows the backdrop color") {
                write(0x2001, ppu, 0x00);

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == BLACK);
            }

            SECTION("background enabled but leftmost 8 pixels hidden") {
                write(0x2001, ppu, 0x08);// show background, no leftmost-8 bit

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == BLACK);
            }

            SECTION("sprites hidden entirely") {
                auto sprites = std::array<nes::sprite, 64>{};
                auto mempage = std::bit_cast<std::uint8_t*>(sprites.data());
                sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x00, .x = 100};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                write(0x2001, ppu, 0x0A);// show background (incl. leftmost), hide sprites

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{100, 0}) == BLACK);
            }
        }

        SECTION("rendering background") {
            SECTION("point at (0,0)") {
                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 1);

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == RASPBERRY);
                CHECK(screen.pixels.at(nes::point{0, 0}) != BLACK);

                for (auto y = 1; y < 240; ++y) {
                    for (auto x = 1; x < 256; ++x) {
                        CHECK(screen.pixels.at(nes::point{1, 1}) == BLACK);
                    }
                }
            }

            SECTION("point at (7,1)") {
                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 42);

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{7, 1}) == RASPBERRY);
            }

            SECTION("point at (15,1)") {
                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 0, 42);

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{15, 1}) == RASPBERRY);
            }

            SECTION("4 palette colors") {
                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 99);

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == RASPBERRY);
                CHECK(screen.pixels.at(nes::point{1, 0}) == OLIVE);
                CHECK(screen.pixels.at(nes::point{2, 0}) == VIOLET);
                CHECK(screen.pixels.at(nes::point{3, 0}) == BLACK);
            }

            SECTION("scroll X") {
                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 0, 42);
                write(0x2006, ppu, 0x24, 0x00);// Nametable
                write(0x2007, ppu, 42);

                SECTION("1 pixel") {
                    write(0x2005, ppu, 1, 0);
                    tick(ppu, screen, 242 * 341);// Wait one frame

                    CHECK(screen.pixels.at(nes::point{14, 1}) == RASPBERRY);
                }
                SECTION("8 pixels") {
                    write(0x2005, ppu, 8, 0);
                    tick(ppu, screen, 242 * 341);// Wait one frame

                    CHECK(screen.pixels.at(nes::point{7, 1}) == RASPBERRY);
                    CHECK(screen.pixels.at(nes::point{255, 1}) == RASPBERRY);
                }
                SECTION("201 pixels") {
                    write(0x2005, ppu, 201, 0);
                    tick(ppu, screen, 242 * 341);// Wait one frame

                    CHECK(screen.pixels.at(nes::point{62, 1}) == RASPBERRY);
                }
                SECTION("Flip nametables") {
                    tick(ppu, screen, 1 * 341);// Wait prerender scanline

                    write(0x2000, ppu, 0x01);    // Make nametable #1 base nametable
                    tick(ppu, screen, 241 * 341);// Wait one frame

                    CHECK(screen.pixels.at(nes::point{7, 1}) == RASPBERRY);
                }
            }

            SECTION("scroll Y") {
                cartridge.cart_mirroring = nes::name_table_mirroring::horizontal;

                write(0x2006, ppu, 0x20, 0x00);// Nametable
                write(0x2007, ppu, 0, 42);
                write(0x2006, ppu, 0x24, 0x00);// Nametable
                write(0x2007, ppu, 42);

                SECTION("1 pixel") {
                    write(0x2005, ppu, 0, 1);
                    tick(ppu, screen, 242 * 341);// Wait one frame

                    CHECK(screen.pixels.at(nes::point{15, 0}) == RASPBERRY);
                }
            }
        }

        SECTION("rendering sprites") {
            auto sprites = std::array<nes::sprite, 64>{};
            auto mempage = std::bit_cast<std::uint8_t*>(sprites.data());

            SECTION("single point, sprite at (0, 0)") {
                sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x00, .x = 0};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == CYAN);
            }
            SECTION("single point, sprite at (3, 2)") {
                sprites[1] = nes::sprite{.y = 2, .tile = 1, .attr = 0x00, .x = 3};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{3, 2}) == CYAN);
            }
            SECTION("single point, sprite palette #1") {
                sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x01, .x = 0};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 0}) == WHITE);
            }
            SECTION("single point, flip vertically") {
                sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x80, .x = 0};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{0, 7}) == CYAN);
            }
            SECTION("single point, flip horizontally") {
                sprites[1] = nes::sprite{.y = 0, .tile = 1, .attr = 0x40, .x = 0};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                tick(ppu, screen, 242 * 341);// Wait one frame

                CHECK(screen.pixels.at(nes::point{7, 0}) == CYAN);
            }

            SECTION("sprite 0 hit") {
                sprites[0] = nes::sprite{.y = 0, .tile = 1, .attr = 0x00, .x = 128};
                ppu.dma_write(0x0000, [mempage](auto addr) { return mempage[addr]; });

                write(0x2006, ppu, 0x20, 0x10);// Nametable
                write(0x2007, ppu, 1);

                REQUIRE((ppu.status & 0x40) == 0);
                tick(ppu, screen, 1 * 341);// Wait prerender scanline
                tick(ppu, screen, 2 + 129);// Wait for first pixel to hit sprite 0

                CHECK((ppu.status & 0x40) != 0);
            }
        }
    }
}