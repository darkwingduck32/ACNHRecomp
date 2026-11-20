#pragma once

#include "field_provider_binding.h"

namespace nx::acnh {

struct ProviderStorage { uint64_t address; };

template<class Memory>
ProviderStorage provider_bound_storage(ProviderBinding binding, const Memory& memory) {
    return {memory.template read<uint64_t>(binding.address + 0x20)};
}

}  // namespace nx::acnh
