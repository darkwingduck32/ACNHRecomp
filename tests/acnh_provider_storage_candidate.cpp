#include "../games/acnh/field_provider_storage.h"

#include <cstdlib>
#include <iostream>

namespace {

struct Memory {
    uint64_t address;
    uint64_t bound_storage;

    template<class T>
    T read(uint64_t input) const {
        if (input != address + 0x20) std::abort();
        return static_cast<T>(bound_storage);
    }
};

}

int main() {
    uint64_t binding, value;
    while (std::cin >> binding >> value)
        std::cout << nx::acnh::provider_bound_storage({binding}, Memory{binding, value}).address << '\n';
}
