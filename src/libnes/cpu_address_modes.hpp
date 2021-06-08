#pragma once

#include <optional>
#include <tuple>

namespace nes
{

struct implied_address_mode {};

const auto imp = [](auto& ) {
    return implied_address_mode{};
};


template <class cpu_t>
class accumulator_address_mode
{
public:
    explicit accumulator_address_mode(cpu_t& c) : cpu_(c) {}

    [[nodiscard]]
    auto load_operand() const
    {
        return std::tuple{cpu_.a.value(), 0};
    }

    auto store_operand(uint8_t operand)
    {
        cpu_.a.assign(operand);
        return 0;
    }

private:
    cpu_t& cpu_;
};

const auto acc = [](auto& cpu) {
    return accumulator_address_mode{cpu};
};


template <class fettch_addr_t, class cpu_t>
struct memory_based_address_mode
{
    memory_based_address_mode(cpu_t& c, fettch_addr_t fetch_addr) : cpu_(c), fetch_addr_(fetch_addr) {}

    [[nodiscard]]
    auto load_operand()
    {
        fetch_address();
        auto [address, additional_cycles] = result_.value();
        return std::tuple{cpu_.read(address), additional_cycles};
    }

    auto store_operand(uint8_t operand)
    {
        fetch_address();
        auto [address, additional_cycles] = result_.value();
        cpu_.write(address, operand);
        return additional_cycles;
    }

    auto fetch_address()
    {
        if (!result_)
            result_ = fetch_addr_(cpu_);
        return result_.value();
    }

private:
    cpu_t& cpu_;
    std::optional<std::tuple<uint16_t, int>> result_;
    fettch_addr_t fetch_addr_;
};

const auto imm = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu) {
        return std::tuple{cpu.pc.advance(), 0};
    }};
};

auto zp =  [](auto& cpu)
{
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        return std::tuple{cpu.read(cpu.pc.advance()), 0};
    }};
};

const auto zpx = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto address = cpu.read(cpu.pc.advance());
        return std::tuple{(cpu.x.value() + address) % 0x100, 0};
    }};
};

const auto zpy = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto address = cpu.read(cpu.pc.advance());
        return std::tuple{(cpu.y.value() + address) % 0x100, 0};
    }};
};

const auto abs = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto address = cpu.read_word(cpu.pc.advance(2));
        return std::tuple{address, 0};
    }};
};

const auto abx = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        return index(cpu.read_word(cpu.pc.advance(2)), cpu.x.value());
    }};
};

const auto aby = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        return index(cpu.read_word(cpu.pc.advance(2)), cpu.y.value());
    }};
};

const auto ind = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto address = cpu.read_word(cpu.pc.advance(2));
        return std::tuple{cpu.read_word_wrapped(address), 0};
    }};
};

const auto izx = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto indexed = (cpu.read(cpu.pc.advance()) + cpu.x.value()) % 0x100;
        return std::tuple{cpu.read_word_wrapped(indexed), 0};
    }};
};

const auto izy = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto base = cpu.read(cpu.pc.advance());
        auto ad = cpu.read_word_wrapped(base);
        return index(ad, cpu.y.value());
    }};
};

const auto rel = [](auto& cpu) {
    return memory_based_address_mode{cpu, [](auto& cpu ) {
        auto offset = cpu.read_signed(cpu.pc.advance());
        return index(cpu.pc.value(), offset);
    }};
};

}