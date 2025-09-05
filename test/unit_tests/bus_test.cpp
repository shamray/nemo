#include <catch2/catch_all.hpp>

#include <libnes/console.hpp>

using namespace nes::literals;

struct bus_test {
    struct test_ppu {
        auto write(std::uint16_t addr, std::uint8_t value) {
            bytes_written[addr] = value;
            return false;
        }
        [[nodiscard]] auto read(std::uint16_t addr) const -> std::optional<std::uint8_t> {
            return bytes_to_read.at(addr);
        }
        constexpr void dma_write(std::uint16_t, auto) {}

        void load_cartridge(nes::cartridge* rom) noexcept { cartridge = rom; }
        void eject_cartridge() noexcept { load_cartridge(nullptr); }
        void nametable_mirroring(auto) noexcept {}

        std::unordered_map<std::uint16_t, std::uint8_t> bytes_written;
        std::unordered_map<std::uint16_t, std::uint8_t> bytes_to_read;
        nes::cartridge* cartridge{nullptr};
    };

    struct test_cartridge: nes::cartridge {
        nes::membank<4_Kb> cart_chr{};
        nes::name_table_mirroring cart_mirroring{nes::name_table_mirroring::vertical};
        std::unordered_map<std::uint16_t, std::uint8_t> bytes_written;

        [[nodiscard]] auto chr0() const noexcept -> const nes::membank<4_Kb>& override {
            return cart_chr;
        }

        [[nodiscard]] auto chr1() const noexcept -> const nes::membank<4_Kb>& override {
            return cart_chr;
        }

        [[nodiscard]] auto mirroring() const noexcept -> nes::name_table_mirroring override {
            return cart_mirroring;
        }

        auto write([[maybe_unused]] std::uint16_t addr, [[maybe_unused]] std::uint8_t value) -> bool override {
            bytes_written[addr] = value;
            return false;
        }

        void chr_write([[maybe_unused]] std::uint16_t addr, [[maybe_unused]] std::uint8_t value) noexcept override {
        }

        [[nodiscard]] auto read([[maybe_unused]] std::uint16_t addr) -> std::optional<std::uint8_t> override {
            return std::nullopt;
        }
    };

    using test_bus = nes::console_bus<test_ppu>;

    test_cartridge cartridge;
    test_ppu ppu;
    test_bus bus{ppu, &cartridge};
};


TEST_CASE_METHOD(bus_test, "Bus - create bus") {
    SECTION("create bus with no cartridge") {
        auto new_bus = test_bus{ppu};
        CHECK(new_bus.cartridge() == nullptr);
        CHECK(ppu.cartridge == nullptr);
    }

    SECTION("create bus with cartridge loaded") {
        auto new_bus = test_bus{ppu, &cartridge};
        CHECK(new_bus.cartridge() == &cartridge);
    }

    SECTION("create bus, then load cartridge") {
        auto new_bus = test_bus{ppu};

        new_bus.load_cartridge(&cartridge);

        CHECK(new_bus.cartridge() == &cartridge);
        CHECK(ppu.cartridge == &cartridge);
    }

    SECTION("create bus, eject cartridge") {
        auto new_bus = test_bus{ppu, &cartridge};
        REQUIRE(new_bus.cartridge() == &cartridge);

        new_bus.eject_cartridge();

        CHECK(new_bus.cartridge() == nullptr);
        CHECK(ppu.cartridge == nullptr);
    }
}

TEST_CASE_METHOD(bus_test, "Bus - write data") {
    SECTION("write memory") {
        // 0x0000 ... 0x1FFF
        bus.write(0x0011, 0x13);
        CHECK(bus.mem[0x11] == 0x13);

        bus.write(0x0811, 0x17);
        CHECK(bus.mem[0x11] == 0x17);

        bus.write(0x1811, 0x19);
        CHECK(bus.mem[0x11] == 0x19);
    }
    SECTION("write PPU registers") {
        // 0x2000 ... 0x3FFF
        // 0x4014

        bus.write(0x2005, 0x88);
        CHECK(ppu.bytes_written.at(0x2005) == 0x88);
    }
    SECTION("write APU registers") {
        // 0x4000 ... 0x4017
    }
    SECTION("write controller registers") {
        // 0x4016
    }
    SECTION("write cartridge RAM") {
        // 0x4018 ... 0xFFFF

        bus.write(0xC000, 0x67);
        CHECK(cartridge.bytes_written.at(0xC000) == 0x67);
    }
}