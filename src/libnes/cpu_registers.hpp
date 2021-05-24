#pragma once

#include <bitset>
#include <cstdint>
#include <iostream>

namespace nes
{

enum class cpu_flag
{
    carry = 0,
    zero = 1,
    int_disable = 2,
    decimal = 3,
    break_called = 4,
    _ [[maybe_unused]] = 5,
    overflow = 6,
    negative = 7
};

class flags_register
{
    constexpr static auto pos(cpu_flag f) { return static_cast<size_t>(f); }

public:
    void assign(uint8_t bits) { bits_ = bits | 0x20;  }
    void set(cpu_flag f, bool value = true) { bits_.set(pos(f), value); }
    void reset(cpu_flag f) { bits_.reset(pos(f)); }

    [[nodiscard]] auto test(cpu_flag f) const { return bits_.test(pos(f)); }
    [[nodiscard]] auto value() const { return static_cast<uint8_t>(bits_.to_ulong()); }

private:
    std::bitset<8> bits_{0x20};
};

class arith_register
{
public:
    explicit arith_register(flags_register& f) : flags_{f} {}

    [[nodiscard]] auto value() const { return val_; }

    arith_register& operator=(uint8_t new_val)
    {
        assign(new_val);
        return *this;
    }

    void assign(uint8_t new_val)
    {
        val_ = new_val;
        flags_.set(cpu_flag::zero, val_ == 0);
        flags_.set(cpu_flag::negative, (val_ & 0x80) != 0);
    }

private:
    uint8_t val_{0};
    flags_register& flags_;
};

inline std::ostream& operator<< (std::ostream& s, const arith_register& r)
{
    return s << static_cast<int>(r.value());
}

class program_counter
{
public:
    program_counter() = default;
    explicit program_counter(uint16_t val) : val_{val} {}

    void assign(uint16_t val) { val_ = val; }
    auto advance(int16_t increment = 1)
    {
        auto old = val_;
        val_ += increment;
        return old;
    }

    [[nodiscard]] auto hi() const { return static_cast<uint8_t>(val_ >> 8); }
    [[nodiscard]] auto lo() const { return static_cast<uint8_t>(val_ & 0xFF); }

    [[nodiscard]] auto value() const { return val_; }

private:
    uint16_t val_{0};
};

class stack_register
{
public:
    stack_register(uint16_t stack_base, uint8_t initial_value)
            : val_(initial_value)
            , stack_base_(stack_base)
    {}

    void assign(uint8_t val) { val_ = val; }

    auto push() { return stack_base_ + val_--; }
    auto pop()  { return stack_base_ + ++val_; }

    [[nodiscard]] auto value() const { return val_; }

private:
    uint8_t val_;
    uint16_t stack_base_;
};

}