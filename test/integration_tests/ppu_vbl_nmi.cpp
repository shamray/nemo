#include <catch2/catch_all.hpp>
#include <fstream>
#include <ranges>

#include <libnes/console.hpp>
#include <libnes/literals.hpp>
#include <libnes/mappers/mmc1.hpp>
#include <libnes/mappers/nrom.hpp>

using namespace nes::literals;

namespace
{

auto load_rom(const std::string& filename) -> std::unique_ptr<nes::cartridge> {
    auto romfile = std::ifstream{filename, std::ifstream::binary};
    REQUIRE(romfile.is_open());

    nes::ines_header header{};
    romfile.read(reinterpret_cast<char*>(&header), sizeof(header));

    auto mapper_ix = (header.mapper1 >> 4) | (header.mapper2 & 0xF0);

    if (mapper_ix == 0) {
        auto prg = std::vector<std::array<std::uint8_t, 16_Kb>>{};
        for (auto i = 0; i < header.prg_rom_chunks; ++i) {
            prg.emplace_back();
            romfile.read(reinterpret_cast<char*>(prg.back().data()), prg.back().size());
        }

        auto chr0 = nes::membank<4_Kb>{};
        romfile.read(reinterpret_cast<char*>(chr0.data()), chr0.size());
        auto chr1 = nes::membank<4_Kb>{};
        romfile.read(reinterpret_cast<char*>(chr1.data()), chr1.size());

        auto mirroring = (header.mapper1 & 0x01)
            ? nes::name_table_mirroring::vertical
            : nes::name_table_mirroring::horizontal;

        return std::make_unique<nes::nrom>(prg, chr0, chr1, mirroring);
    }

    if (mapper_ix == 1) {
        auto prg = std::vector<std::array<std::uint8_t, 16_Kb>>{};
        for (auto i = 0; i < header.prg_rom_chunks; ++i) {
            prg.emplace_back();
            romfile.read(reinterpret_cast<char*>(prg.back().data()), prg.back().size());
        }

        auto chr = std::vector<nes::membank<4_Kb>>{};
        for (auto i = 0; i < header.chr_rom_chunks * 2; ++i) {
            chr.emplace_back();
            romfile.read(reinterpret_cast<char*>(chr.back().data()), chr.back().size());
        }

        return std::make_unique<nes::mmc1>(prg, chr);
    }

    throw std::runtime_error("Unsupported mapper " + std::to_string(mapper_ix));
}

struct null_screen {
    [[nodiscard]] constexpr static auto width() -> short { return 256; }
    [[nodiscard]] constexpr static auto height() -> short { return 240; }
    void draw_pixel(nes::point, nes::color) {}
};

// blargg's standard test-status convention (used by ppu_vbl_nmi.nes and
// most of his later test ROMs): a status byte at $6000, a signature at
// $6001-$6003 that marks the region as valid once the ROM has initialized
// it, and a NUL-terminated human-readable message at $6004+.
constexpr std::uint8_t STATUS_RUNNING = 0x80;
constexpr std::uint8_t STATUS_NEEDS_RESET = 0x81;
constexpr std::array<std::uint8_t, 3> SIGNATURE = {0xDE, 0xB0, 0x61};

auto read_result_text(nes::console& console) -> std::string {
    auto text = std::string{};
    for (std::uint16_t addr = 0x6004; addr < 0x6004 + 512; ++addr) {
        auto byte = console.peek(addr);
        if (byte == 0)
            break;
        text += static_cast<char>(byte);
    }
    return text;
}

}// namespace

TEST_CASE("blargg ppu_vbl_nmi.nes", "[.]") {
    auto cartridge = load_rom("rom/ppu_vbl_nmi.nes");
    auto console = nes::console{std::move(cartridge)};

    auto screen = null_screen{};
    auto log = std::ofstream{"ppu_vbl_nmi.log"};

    constexpr auto max_frames = 600;// ~10s of emulated time; generous headroom
    auto status = std::uint8_t{STATUS_RUNNING};
    auto has_valid_signature = false;
    auto frame = 0;

    for (; frame < max_frames; ++frame) {
        console.render_frame(screen);

        status = console.peek(0x6000);
        has_valid_signature = console.peek(0x6001) == SIGNATURE[0] and console.peek(0x6002) == SIGNATURE[1] and console.peek(0x6003) == SIGNATURE[2];

        // Only trust the status byte once the ROM has written its
        // signature -- otherwise a still-zeroed $6000 reads as "passed".
        if (has_valid_signature and status != STATUS_RUNNING and status != STATUS_NEEDS_RESET)
            break;
    }

    log << "stopped after " << frame << " frames, status=$" << std::hex << (int) status << '\n'
        << "signature valid: " << std::boolalpha << has_valid_signature << '\n'
        << "text: " << read_result_text(console) << '\n';

    REQUIRE(has_valid_signature);
    CHECK((int) status == 0x00);
}
