#pragma once

#include <cstdint>

namespace nx::acnh {

template<class Memory>
void initialize_inline_strings(uint64_t base, uint64_t object, Memory& memory) {
    const uint64_t empty = base + 0x3f7ebb1;
    const auto word = [&](uint64_t offset, uint32_t value) {
        memory.template write<uint32_t>(object + offset, value);
    };
    const auto pointer = [&](uint64_t offset, uint64_t value) {
        memory.template write<uint64_t>(object + offset, value);
    };
    const auto zero_byte = [&](uint64_t offset) {
        memory.template write<uint8_t>(object + offset, 0);
    };
    const auto set_buffer = [&](uint64_t part) {
        pointer(part + 8, object + part + 0x14);
        word(part + 0x10, 64);
        zero_byte(part + 0x53);
    };
    const auto terminate_buffer = [&](uint64_t part) {
        if (object + part + 0x14 != empty) zero_byte(part + 0x14);
    };
    const auto clear_tail_pair = [&](uint64_t part) {
        pointer(part + 0x58, 0);
        pointer(part + 0x60, 0);
    };

    set_buffer(0);
    terminate_buffer(0);
    zero_byte(0x78);
    const uint64_t descriptor = memory.template read<uint64_t>(base + 0x4ab2bc0) + 0x10;
    clear_tail_pair(0);
    pointer(0x68, 0);
    word(0x70, 0);
    word(0x7c, UINT32_MAX);

    set_buffer(0x80);
    pointer(0, descriptor);
    terminate_buffer(0x80);
    pointer(0x80, descriptor);
    zero_byte(0xf8);
    pointer(0xe8, 0);
    word(0xf0, 0);
    word(0xfc, UINT32_MAX);

    set_buffer(0x100);
    clear_tail_pair(0x80);
    terminate_buffer(0x100);
    zero_byte(0x178);
    pointer(0x100, descriptor);
    pointer(0x168, 0);
    word(0x170, 0);
    word(0x17c, UINT32_MAX);

    set_buffer(0x180);
    clear_tail_pair(0x100);
    terminate_buffer(0x180);
    pointer(0x180, descriptor);
    zero_byte(0x1f8);
    pointer(0x1e8, 0);
    word(0x1f0, 0);
    word(0x1fc, UINT32_MAX);
    clear_tail_pair(0x180);
}

template<class Memory>
uint32_t draw_gameplay_percent(uint64_t base, Memory& memory) {
    const uint64_t state = base + 0x504cb58;
    const uint32_t a = memory.template read<uint32_t>(state);
    const uint32_t b = memory.template read<uint32_t>(state + 4);
    const uint32_t c = memory.template read<uint32_t>(state + 8);
    const uint32_t d = memory.template read<uint32_t>(state + 12);
    const uint32_t mixed = a ^ (a << 11);
    const uint32_t next = mixed ^ (mixed >> 8) ^ d ^ (d >> 19);
    memory.template write<uint32_t>(state, b);
    memory.template write<uint32_t>(state + 4, c);
    memory.template write<uint32_t>(state + 8, d);
    memory.template write<uint32_t>(state + 12, next);
    return static_cast<uint32_t>((uint64_t{next} * 100) >> 32);
}

}  // namespace nx::acnh
