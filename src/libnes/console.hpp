#pragma once

#include <libnes/cartridge.hpp>
#include <libnes/cpu.hpp>
#include <libnes/mappers/mmc1.hpp>
#include <libnes/mappers/nrom.hpp>
#include <libnes/ppu.hpp>
#include <memory>

namespace nes
{

template <typename T>
concept PPU = requires(T t, std::uint16_t address, std::uint8_t value, nes::cartridge* rom, nes::name_table_mirroring m) {
    { t.read(address) } -> std::same_as<std::optional<std::uint8_t>>;
    { t.dma_write(address, std::invocable<std::uint16_t> ) };
    { t.load_cartridge(rom) };
    { t.eject_cartridge() };
};

template <PPU P>
struct console_bus {
    struct controller_hack {
        std::uint8_t keys{0};
        std::uint8_t snapshot{0};
    } j1;

    explicit constexpr console_bus(P& ppu, cartridge* cartridge = nullptr)
        : ppu_{ppu} {

        load_cartridge(cartridge);
    }

    constexpr auto ppu() -> auto& {
        return ppu_.get();
    }

    constexpr void load_cartridge(cartridge* new_cartridge) {
        cartridge_ = new_cartridge;

        ppu().load_cartridge(cartridge_);
    }

    constexpr void eject_cartridge() {
        ppu().eject_cartridge();
        cartridge_ = nullptr;
    }

    [[nodiscard]] constexpr auto nmi() {
        if (not ppu().nmi_raised)
            return false;

        auto nmi_signal = ppu().nmi_raised and not ppu().nmi_seen;
        ppu().nmi_seen = true;

        return nmi_signal;
    }

    constexpr void write(std::uint16_t addr, std::uint8_t value) {
        if (addr < 0x2000) {
            mem[addr % 0x0800] = value;

        } else if (addr >= 0x2000 and addr < 0x4000) {
            // $2008-$3FFF mirrors the eight PPU registers at $2000-$2007
            ppu().write(static_cast<std::uint16_t>(0x2000 | (addr & 0x0007)), value);

        } else if (addr == 0x4014) {
            ppu().dma_write(value << 8U, [this](auto addr) { return read(addr); });

        } else if (addr == 0x4016) {
            j1.snapshot = j1.keys;
        }

        if (cartridge_ != nullptr)
            cartridge_->write(addr, value);
    }

    constexpr std::uint8_t read(std::uint16_t addr) {
        if (addr <= 0x1FFF) {
            return mem[addr & 0x07FF];
        }

        if (addr < 0x4000) {
            // $2008-$3FFF mirrors the eight PPU registers at $2000-$2007
            if (auto r = ppu().read(static_cast<std::uint16_t>(0x2000 | (addr & 0x0007))); r.has_value())
                return r.value();
            return 0;
        }

        if (addr == 0x4016) {
            auto r = (j1.snapshot & 0x80) ? std::uint8_t{1} : std::uint8_t{0};
            j1.snapshot <<= 1;
            return r;
        }

        if (addr == 0x4017) {
            return 0;
        }

        if (auto r = cartridge_->read(addr); r.has_value())
            return r.value();

        return 0;
    }

    [[nodiscard]] constexpr auto cartridge() noexcept { return cartridge_; }

    std::array<std::uint8_t, 2_Kb> mem{};

private:
    nes::cartridge* cartridge_{nullptr};
    std::reference_wrapper<P> ppu_;
};

class console
{
public:
    using bus = console_bus<ppu>;
    using cpu = nes::cpu<bus>;

    explicit console(std::unique_ptr<cartridge> rom)
        : cartridge_{std::move(rom)}
        , bus_{ppu_, cartridge_.get()} {
    }

    template <screen screen_t>
    void render_frame(screen_t& screen) {
        auto count = 0;
        for (;; ++count) {
            cpu_.tick();

            ppu_.tick_old(screen);
            if (ppu_.is_frame_ready()) break;
            ppu_.tick_old(screen);
            if (ppu_.is_frame_ready()) break;
            ppu_.tick_old(screen);
            if (ppu_.is_frame_ready()) break;
        }
        assert(count == 29780 || count == 29781);
    }

    template <screen screen_t>
    void render_nametables(screen_t& screen) {
        ppu_.render_nametables(screen);
    }

    auto display_pattern_table(auto i) const {
        return ppu_.display_pattern_table(i, 0);
    }

    void controller_input(std::uint8_t keys) {
        bus_.j1.keys = keys;
    }

    // Debug/test-only: read a byte off the CPU-visible bus without advancing
    // emulation. Used to inspect cartridge PRG-RAM (e.g. blargg test ROMs'
    // $6000/$6004 status-and-text convention).
    [[nodiscard]] auto peek(std::uint16_t addr) -> std::uint8_t {
        return bus_.read(addr);
    }

private:
    std::unique_ptr<cartridge> cartridge_;
    ppu ppu_{nes::DEFAULT_COLORS};
    bus bus_{ppu_};
    cpu cpu_{bus_};
};

}// namespace nes