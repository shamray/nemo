#pragma once

#include <libnes/cartridge.hpp>
#include <libnes/ppu_name_table.hpp>

#include <array>
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace nes
{

class mmc1_shift_register
{
    [[nodiscard]] constexpr static auto reset(std::uint8_t v) { return (v & 0x80U) != 0; }

public:
    constexpr void load(std::uint8_t next_bit) {
        assert(count_ < 5);

        if (reset(next_bit)) {
            reset_ = true;

            value_ = 0;
            count_ = 0;

        } else {
            reset_ = false;
            count_ += 1;

            value_ >>= 1u;
            value_ |= (next_bit & 0x01U) << 4U;
        }
    }

    [[nodiscard]] constexpr auto is_reset() const -> bool { return reset_; }
    [[nodiscard]] constexpr auto get_value() -> std::optional<std::uint8_t> {
        if (count_ < 5)
            return std::nullopt;

        assert(!reset_);

        auto result = value_;
        value_ = 0;
        count_ = 0;
        return result;
    }

private:
    bool reset_{false};
    std::uint8_t value_{0};
    int count_{0};
};

class mmc1 final: public cartridge
{
public:
    mmc1(std::vector<std::array<std::uint8_t, 16_Kb>> prg, std::vector<membank<4_Kb>> chr)
        : prg_{std::move(prg)}
        , chr_{std::move(chr)} {
        if (chr_.empty()) {
            // A board with no CHR ROM carries 8Kb of CHR RAM on the same
            // bank-select lines; without banks chr0()/chr1() would divide by zero
            chr_.resize(2);
        }
    }

    [[nodiscard]] auto chr0() const noexcept -> const membank<4_Kb>& override {
        return chr_[chr_ix0_ % chr_.size()];
    }

    [[nodiscard]] auto chr1() const noexcept -> const membank<4_Kb>& override {
        return chr_[chr_ix1_ % chr_.size()];
    }

    [[nodiscard]] auto mirroring() const noexcept -> name_table_mirroring override {
        return mirroring_;
    }

    auto write(std::uint16_t addr, std::uint8_t value) -> bool override {
        if (addr < 0x8000)
            return false;

        shift_register_.load(value);
        if (auto r = shift_register_.get_value(); r.has_value()) {
            if (addr < 0xA000) {
                control_ = r.value();
                set_mirroring();
            } else if (addr < 0xC000) {
                chr_ix0_ = r.value();
            } else if (addr < 0xE000) {
                chr_ix1_ = r.value();
            } else {
                prg_ix_ = r.value();
            }

            return true;
        }

        return false;
    }

    [[nodiscard]] auto read(std::uint16_t addr) -> std::optional<std::uint8_t> override {
        auto prg_mode = (control_ & 0b01100) >> 2;

        if (prg_mode == 0 or prg_mode == 1) {
            auto address = addr & 0x3FFFu;
            auto prg_offset = (addr & 0x8000u) != 0
                ? 1
                : 0;
            auto ix = (prg_ix_ * 2) + prg_offset;
            auto& prg = prg_[ix];

            return prg[address];
        }

        if (addr >= 0x8000 and addr <= 0xBFFF) {
            auto address = addr & 0x3FFFu;
            auto& prg = prg_mode == 3
                ? prg_[prg_ix_ % prg_.size()]
                : prg_.front();

            return prg[address];
        }

        if (addr >= 0xC000 and addr <= 0xFFFF) {
            auto address = addr & 0x3FFFu;
            auto& prg = prg_mode == 3
                ? prg_.back()
                : prg_[prg_ix_ % prg_.size()];

            return prg[address];
        }

        return std::nullopt;
    }

    constexpr void set_mirroring() noexcept {
        switch (control_ & 0b00011) {
            case 0b00:
                mirroring_ = name_table_mirroring::single_screen_lo;
                break;
            case 0b01:
                mirroring_ = name_table_mirroring::single_screen_hi;
                break;
            case 0b10:
                mirroring_ = name_table_mirroring::vertical;
                break;
            case 0b11:
                mirroring_ = name_table_mirroring::horizontal;
                break;
        }
    }

private:
    std::vector<std::array<std::uint8_t, 16_Kb>> prg_;
    std::vector<membank<4_Kb>> chr_;

    mmc1_shift_register shift_register_;
    std::uint8_t control_{0x0C};
    std::uint8_t chr_ix0_{0};
    std::uint8_t chr_ix1_{0};
    std::uint8_t prg_ix_{0};

    nes::name_table_mirroring mirroring_{nes::name_table_mirroring::single_screen_lo};
};

}// namespace nes
