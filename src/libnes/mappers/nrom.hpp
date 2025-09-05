#pragma once

#include <libnes/cartridge.hpp>
#include <libnes/ppu_name_table.hpp>

#include <array>
#include <optional>
#include <vector>

namespace nes
{

class nrom final: public cartridge
{
public:
    nrom(std::vector<std::array<std::uint8_t, 16_Kb>> prg, membank<4_Kb> chr0, membank<4_Kb> chr1, name_table_mirroring mirroring)
        : prg_{std::move(prg)}
        , chr0_{chr0}
        , chr1_{chr1}
        , mirroring_{mirroring} {}

    [[nodiscard]] auto mirroring() const noexcept -> name_table_mirroring override { return mirroring_; }

    [[nodiscard]] auto chr_read(std::uint16_t addr) const noexcept -> std::uint8_t override {
        auto& chr = (addr < 0x1000) ? chr0_ : chr1_;
        return chr[addr % 0x1000];
    }

    auto write([[maybe_unused]] std::uint16_t addr, [[maybe_unused]] std::uint8_t value) -> bool override {
        return false;
    }

    void chr_write([[maybe_unused]] std::uint16_t addr, [[maybe_unused]] std::uint8_t value) noexcept override {
        // NROM boards ship CHR ROM; real hardware ignores writes to it
    }

    [[nodiscard]] auto read(std::uint16_t addr) -> std::optional<std::uint8_t> override {
        if (addr >= 0x8000 and addr <= 0xBFFF) {
            auto address = addr & 0x3FFFu;
            auto& prg = prg_.front();

            return prg[address];
        }

        if (addr >= 0x8000 and addr <= 0xFFFF) {
            auto address = addr & 0x3FFFu;
            auto& prg = prg_.back();

            return prg[address];
        }

        return std::nullopt;
    }

private:
    std::vector<std::array<std::uint8_t, 16_Kb>> prg_;
    membank<4_Kb> chr0_;
    membank<4_Kb> chr1_;
    name_table_mirroring mirroring_;
};

}// namespace nes