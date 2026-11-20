#pragma once

#include <cstdint>

namespace nx::features {

struct GuestMemberTarget {
    uint64_t receiver;
    uint64_t target;
    uint64_t dispatch_base;
    bool present;
};

template<class Memory>
GuestMemberTarget provider_member_target(uint64_t context, const Memory& memory) {
    const uint64_t receiver = memory.template read<uint64_t>(context + 8);
    if (!receiver) return {};
    const uint64_t member = memory.template read<uint64_t>(context + 0x10);
    const uint64_t encoded_adjustment = memory.template read<uint64_t>(context + 0x18);
    const bool virtual_call = (encoded_adjustment & 1) != 0;
    if (!member && !virtual_call) return {};
    const uint64_t adjustment = (encoded_adjustment >> 1) | (encoded_adjustment & (1ull << 63));
    const uint64_t adjusted = receiver + adjustment;
    if (!virtual_call) return {adjusted, member, receiver, true};
    const uint64_t vtable = memory.template read<uint64_t>(adjusted);
    return {adjusted, memory.template read<uint64_t>(vtable + member), vtable, true};
}

}  // namespace nx::features
