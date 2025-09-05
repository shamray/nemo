#pragma once

#include <libnes/literals.hpp>
#include <libnes/ppu_name_table.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace nes
{

struct ines_header {
    [[maybe_unused]] std::array<char, 4> name;
    std::uint8_t prg_rom_chunks;
    std::uint8_t chr_rom_chunks;
    std::uint8_t mapper1;
    std::uint8_t mapper2;
    [[maybe_unused]] std::uint8_t prg_ram_size;
    [[maybe_unused]] std::uint8_t tv_system1;
    [[maybe_unused]] std::uint8_t tv_system2;
    [[maybe_unused]] std::array<char, 5> unused;
};

static_assert(sizeof(ines_header) == 16);

template <std::size_t size>
using membank = std::array<std::uint8_t, size>;

class cartridge
{
public:
    virtual ~cartridge() = default;

    [[nodiscard]] virtual auto chr0() const noexcept -> const membank<4_Kb>& = 0;
    [[nodiscard]] virtual auto chr1() const noexcept -> const membank<4_Kb>& = 0;
    [[nodiscard]] virtual auto mirroring() const noexcept -> name_table_mirroring = 0;

    virtual auto write(std::uint16_t addr, std::uint8_t value) -> bool = 0;
    [[nodiscard]] virtual auto read(std::uint16_t addr) -> std::optional<std::uint8_t> = 0;

    // addr is a PPU pattern-table address, 0x0000-0x1FFF. Boards with CHR ROM
    // ignore this, matching real hardware; boards with CHR RAM store into it.
    virtual void chr_write(std::uint16_t addr, std::uint8_t value) noexcept = 0;
};

}