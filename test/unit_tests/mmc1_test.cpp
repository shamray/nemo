#include <catch2/catch_all.hpp>
#include <libnes/cartridge.hpp>

#include <libnes/mappers/mmc1.hpp>

using namespace nes::literals;

TEST_CASE("MMC1 registers") {

    SECTION("shift register") {
        nes::mmc1_shift_register sr;

        SECTION("empty") {
            CHECK(sr.get_value() == std::nullopt);
        }

        SECTION("load one bit") {
            sr.load(1);

            CHECK(sr.get_value() == std::nullopt);
            CHECK_FALSE(sr.is_reset());
        }

        SECTION("load four bits") {
            sr.load(1);
            sr.load(0);
            sr.load(1);
            sr.load(0);

            CHECK(sr.get_value() == std::nullopt);
        }

        SECTION("load five bits") {
            sr.load(1);
            sr.load(1);
            sr.load(1);
            sr.load(0);
            sr.load(1);

            CHECK(sr.get_value() == 0b10111);
        }

        SECTION("load five bits - getting value mutates state") {
            sr.load(1);
            sr.load(1);
            sr.load(1);
            sr.load(0);
            sr.load(1);

            [[maybe_unused]] auto _ = sr.get_value();

            CHECK(sr.get_value() == std::nullopt);
        }

        SECTION("load five bits - higher bits are ignored") {
            sr.load(0x7f);
            sr.load(0x7e);
            sr.load(0x7f);
            sr.load(0x7e);
            sr.load(0x7f);

            CHECK(sr.get_value() == 0b10101);
        }
    }
}

void write(nes::mmc1& cartridge, std::uint16_t address, std::uint8_t value) {
    cartridge.write(address, value);
    value >>= 1;
    cartridge.write(address, value);
    value >>= 1;
    cartridge.write(address, value);
    value >>= 1;
    cartridge.write(address, value);
    value >>= 1;
    cartridge.write(address, value);
    value >>= 1;
}

TEST_CASE("Mapper MMC1") {
    auto prg = std::vector<nes::membank<16_Kb>>{{}, {}};
    auto chr = std::vector<nes::membank<4_Kb>>{{}, {}, {}};

    prg[0][0] = 'a';
    prg[1][0] = 'b';

    chr[0][0] = 'x';
    chr[1][1] = 'y';
    chr[1][2] = 'z';

    auto cartridge = nes::mmc1{prg, chr};

    SECTION("Mirroring") {
        SECTION("At creation") {
            CHECK(cartridge.mirroring() == nes::name_table_mirroring::single_screen_lo);
        }

        SECTION("Set vertical") {
            write(cartridge, 0x8000, 0b00010);
            CHECK(cartridge.mirroring() == nes::name_table_mirroring::vertical);
        }

        SECTION("Set vertical") {
            write(cartridge, 0x8000, 0b00011);
            CHECK(cartridge.mirroring() == nes::name_table_mirroring::horizontal);
        }
    }
}

TEST_CASE("Mapper MMC1 with CHR RAM") {
    auto prg = std::vector<nes::membank<16_Kb>>{{}, {}};
    auto no_chr_rom = std::vector<nes::membank<4_Kb>>{};

    auto cartridge = nes::mmc1{prg, no_chr_rom};

    SECTION("CHR banks are available") {
        CHECK(cartridge.chr0()[0] == 0);
        CHECK(cartridge.chr1()[0] == 0);
    }

    SECTION("CHR banks stay available after bank switching") {
        write(cartridge, 0xA000, 1);
        write(cartridge, 0xC000, 1);

        CHECK(cartridge.chr0()[0] == 0);
        CHECK(cartridge.chr1()[0] == 0);
    }
}