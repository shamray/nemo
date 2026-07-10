#pragma once

#include <libnes/cpu_registers.hpp>

namespace nes
{

inline namespace literals
{
constexpr auto operator""_i(unsigned long long x) { return static_cast<std::uint8_t>(x); }
}

inline namespace details
{

inline auto carry(const flags_register& f) {
    return f.test(cpu_flag::carry) ? 1_i : 0_i;
}

inline auto is_page_crossed(std::uint16_t base, std::uint16_t effective_address) {
    return (base & 0xFF00u) != (effective_address & 0xFF00u);
}

inline auto index(std::uint16_t base, std::int16_t offset) {
    const std::uint16_t address = int(base) + offset;
    auto additional_cycles = is_page_crossed(base, address) ? 1 : 0;

    return std::tuple{address, additional_cycles};
}

inline auto arith_result(int x) {
    auto c = (x / 0x100) != 0;
    auto r = static_cast<std::uint8_t>(x % 0x100);

    return std::tuple{r, c};
}

inline void adc_impl(auto& result, std::uint8_t accum, std::uint8_t operand, flags_register& flags) {
    auto [r, c] = arith_result(
        accum + operand + carry(flags)
    );
    flags.set(cpu_flag::carry, c);

    auto v = ((operand ^ r) & (r ^ accum) & 0x80u) != 0;
    flags.set(cpu_flag::overflow, v);

    result = r;
}

inline void cmp_impl(std::uint8_t accum, std::uint8_t operand, flags_register& flags) {
    auto alu_result = arith_register{flags};
    alu_result.assign(accum - operand);
    flags.set(cpu_flag::carry, accum >= operand);
}

}

// Operations

const auto adc = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    adc_impl(cpu.a, cpu.a.value(), operand, cpu.p);
    return additional_cycles;
};

const auto sbc = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    adc_impl(cpu.a, cpu.a.value(), 0xFF - operand, cpu.p);
    return additional_cycles;
};

const auto cmp = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    cmp_impl(cpu.a.value(), operand, cpu.p);
    return additional_cycles;
};

const auto cpx = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    cmp_impl(cpu.x.value(), operand, cpu.p);
    return additional_cycles;
};

const auto cpy = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    cmp_impl(cpu.y.value(), operand, cpu.p);
    return additional_cycles;
};

const auto inc = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();

    auto alu_result = arith_register{cpu.p};
    alu_result.assign(operand + 1);

    am.store_operand(alu_result.value());
    return 0;
};

const auto dec = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();

    auto alu_result = arith_register{cpu.p};
    alu_result.assign(operand - 1);

    am.store_operand(alu_result.value());
    return 0;
};

const auto inx = [](auto& cpu, auto) {
    cpu.x.assign(cpu.x.value() + 1);
    return 0;
};

const auto iny = [](auto& cpu, auto) {
    cpu.y.assign(cpu.y.value() + 1);
    return 0;
};

const auto dex = [](auto& cpu, auto) {
    cpu.x.assign(cpu.x.value() - 1);
    return 0;
};

const auto dey = [](auto& cpu, auto) {
    cpu.y.assign(cpu.y.value() - 1);
    return 0;
};

const auto asl = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    am.store_operand(operand << 1);
    cpu.p.set(cpu_flag::carry, (operand & 0x80) != 0);
    return 0;
};

const auto lsr = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    am.store_operand(operand >> 1);
    cpu.p.set(cpu_flag::carry, (operand & 0x01) != 0);
    return 0;
};

const auto rol = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto carry_bit = cpu.p.test(cpu_flag::carry) ? std::uint8_t{0x01} : std::uint8_t{};
    am.store_operand((operand << 1) | carry_bit);
    cpu.p.set(cpu_flag::carry, (operand & 0x80) != 0);
    return 0;
};

const auto ror = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto carry_bit = cpu.p.test(cpu_flag::carry) ? std::uint8_t{0x80} : std::uint8_t{};
    am.store_operand((operand >> 1) | carry_bit);
    cpu.p.set(cpu_flag::carry, (operand & 0x01) != 0);
    return 0;
};

const auto ana = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.a.assign(cpu.a.value() & operand);
    return additional_cycles;
};

const auto ora = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.a.assign(cpu.a.value() | operand);
    return additional_cycles;
};

const auto eor = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.a.assign(cpu.a.value() ^ operand);
    return additional_cycles;
};

const auto bit = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();

    [[maybe_unused]] auto alu_result = arith_register{cpu.p};
    alu_result.assign(cpu.a.value() & operand);
    cpu.p.set(cpu_flag::overflow, operand & (1 << 6));
    cpu.p.set(cpu_flag::negative, operand & (1 << 7));

    return additional_cycles;
};

const auto lda = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.a = operand;
    return additional_cycles;
};

const auto sta = [](auto& cpu, auto address_mode) {
    return address_mode.store_operand(cpu.a.value());
};

const auto ldx = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.x = operand;
    return additional_cycles;
};

const auto stx = [](auto& cpu, auto address_mode) {
    return address_mode.store_operand(cpu.x.value());
};

const auto ldy = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.y = operand;
    return additional_cycles;
};

const auto sty = [](auto& cpu, auto address_mode) {
    return address_mode.store_operand(cpu.y.value());
};

const auto tax = [](auto& cpu, auto) {
    cpu.x.assign(cpu.a.value());
    return 0;
};

const auto tay = [](auto& cpu, auto) {
    cpu.y.assign(cpu.a.value());
    return 0;
};

const auto tsx = [](auto& cpu, auto) {
    cpu.x.assign(cpu.s.value());
    return 0;
};

const auto txa = [](auto& cpu, auto) {
    cpu.a.assign(cpu.x.value());
    return 0;
};

const auto txs = [](auto& cpu, auto) {
    cpu.s.assign(cpu.x.value());
    return 0;
};

const auto tya = [](auto& cpu, auto) {
    cpu.a.assign(cpu.y.value());
    return 0;
};

const auto pha = [](auto& cpu, auto) {
    cpu.write(cpu.s.push(), cpu.a.value());
    return 0;
};

const auto pla = [](auto& cpu, auto) {
    cpu.a = cpu.read(cpu.s.pop());
    return 0;
};

const auto php = [](auto& cpu, auto) {
    cpu.write(cpu.s.push(), cpu.p.value());
    return 0;
};

const auto plp = [](auto& cpu, auto) {
    auto flags_value = cpu.read(cpu.s.pop());
    cpu.p.assign(flags_value & 0xEF);
    return 0;
};

const auto bpl = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (cpu.p.test(cpu_flag::negative)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bmi = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (!cpu.p.test(cpu_flag::negative)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bvc = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (cpu.p.test(cpu_flag::overflow)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bvs = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (!cpu.p.test(cpu_flag::overflow)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bcc = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (cpu.p.test(cpu_flag::carry)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bcs = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (!cpu.p.test(cpu_flag::carry)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto bne = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (cpu.p.test(cpu_flag::zero)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto beq = [](auto& cpu, auto address_mode) {
    auto [address, additional_cycles] = address_mode.fetch_address();
    if (!cpu.p.test(cpu_flag::zero)) {
        return additional_cycles;
    }
    cpu.pc.assign(address);
    return additional_cycles + 1;
};

const auto clc = [](auto& cpu, auto) {
    cpu.p.reset(cpu_flag::carry);
    return 0;
};

const auto sec = [](auto& cpu, auto) {
    cpu.p.set(cpu_flag::carry);
    return 0;
};

const auto cld = [](auto& cpu, auto) {
    cpu.p.reset(cpu_flag::decimal);
    return 0;
};

const auto sed = [](auto& cpu, auto) {
    cpu.p.set(cpu_flag::decimal);
    return 0;
};

const auto cli = [](auto& cpu, auto) {
    cpu.p.reset(cpu_flag::int_disable);
    return 0;
};

const auto sei = [](auto& cpu, auto) {
    cpu.p.set(cpu_flag::int_disable);
    return 0;
};

const auto clv = [](auto& cpu, auto) {
    cpu.p.reset(cpu_flag::overflow);
    return 0;
};

const auto jmp = [](auto& cpu, auto address_mode) {
    auto [address, _] = address_mode.fetch_address();
    cpu.pc.assign(address);
    return 0;
};

const auto jsr = [](auto& cpu, auto address_mode) {
    auto [address, _] = address_mode.fetch_address();
    auto prev_pc = nes::program_counter{static_cast<std::uint16_t>(cpu.pc.value() - 1)};
    cpu.write(cpu.s.push(), prev_pc.hi());
    cpu.write(cpu.s.push(), prev_pc.lo());
    cpu.pc.assign(address);

    return 0;
};

const auto rts = [](auto& cpu, auto) {
    auto lo = cpu.read(cpu.s.pop());
    auto hi = cpu.read(cpu.s.pop());
    auto address = static_cast<std::uint16_t>((hi << 8) | lo);
    cpu.pc.assign(address + 1);

    return 0;
};

const auto rti = [](auto& cpu, auto _) {
    plp(cpu, _);

    auto lo = cpu.read(cpu.s.pop());
    auto hi = cpu.read(cpu.s.pop());
    auto address = static_cast<std::uint16_t>((hi << 8) | lo);
    cpu.pc.assign(address);

    return 0;
};

template <class cpu_t>
struct reset_vector {
    explicit reset_vector(cpu_t& c)
        : cpu(c) {}

    cpu_t& cpu;

    auto fetch_address() {
        return std::tuple{cpu.read_word(0xFFFE), 0};
    }
};

const auto brk = [](auto& cpu, auto _) {
    jsr(cpu, reset_vector{cpu});
    php(cpu, _);
    cpu.p.set(cpu_flag::break_called);
    cpu.p.set(cpu_flag::int_disable);

    return 0;
};

const auto i_n = [](auto&, auto address_mode) {
    auto [_, additional_cycles] = address_mode.fetch_address();
    return additional_cycles;
};

const auto lax = [](auto& cpu, auto address_mode) {
    auto [operand, additional_cycles] = address_mode.load_operand();
    cpu.a = operand;
    cpu.x = operand;
    return additional_cycles;
};

const auto sax = [](auto& cpu, auto address_mode) {
    return address_mode.store_operand(cpu.a.value() & cpu.x.value());
};

const auto isc = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();

    auto alu_result = arith_register{cpu.p};
    alu_result.assign(operand + 1);

    am.store_operand(alu_result.value());

    adc_impl(cpu.a, cpu.a.value(), 0xFF - alu_result.value(), cpu.p);
    return 0;
};

const auto dcp = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();

    auto alu_result = arith_register{cpu.p};
    alu_result.assign(operand - 1);

    am.store_operand(alu_result.value());

    cmp_impl(cpu.a.value(), alu_result.value(), cpu.p);
    return 0;
};

const auto slo = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto set_carry = (operand & 0x80) != 0;

    operand <<= 1;
    am.store_operand(operand);

    cpu.a.assign(cpu.a.value() | operand);

    cpu.p.set(cpu_flag::carry, set_carry);
    return 0;
};

const auto sre = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto set_carry = (operand & 0x01) != 0;

    operand >>= 1;
    am.store_operand(operand);

    cpu.a.assign(cpu.a.value() ^ operand);

    cpu.p.set(cpu_flag::carry, set_carry);
    return 0;
};

const auto rla = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto carry_bit = cpu.p.test(cpu_flag::carry) ? std::uint8_t{0x01} : std::uint8_t{};
    auto set_carry = (operand & 0x80) != 0;

    operand = (operand << 1) | carry_bit;
    am.store_operand(operand);

    cpu.p.set(cpu_flag::carry, set_carry);
    cpu.a.assign(operand & cpu.a.value());

    return 0;
};

const auto rra = [](auto& cpu, auto address_mode) {
    auto am = address_mode;
    auto [operand, _] = am.load_operand();
    auto carry_bit = cpu.p.test(cpu_flag::carry) ? std::uint8_t{0x80} : std::uint8_t{};
    auto set_carry = (operand & 0x01) != 0;

    operand = (operand >> 1) | carry_bit;
    am.store_operand(operand);

    cpu.p.set(cpu_flag::carry, set_carry);
    adc_impl(cpu.a, cpu.a.value(), operand, cpu.p);

    return 0;
};

const auto nop = [](auto&, auto) { return 0; };

}// namespace nes