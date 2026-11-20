#include "../games/acnh/shovel_traversal_peak.h"

#include <cstdlib>
#include <iostream>

namespace {

using XYZ = std::array<uint32_t, 3>;

struct Origin {
    XYZ coordinates;

    template<class T>
    T read(uint64_t address) const {
        return static_cast<T>(coordinates.at(address / 4));
    }
};

struct Reply {
    uint32_t height;
    bool writes;
    XYZ coordinates;
};

struct Services {
    Origin& origin;
    std::array<Reply, 10> replies;
    std::array<XYZ, 10> inputs{};

    float origin_height(uint64_t address) {
        if (address != 0) std::abort();
        inputs[0] = origin.coordinates;
        if (replies[0].writes) origin.coordinates = replies[0].coordinates;
        return std::bit_cast<float>(replies[0].height);
    }

    float sample_height(nx::acnh::ShovelTargetPoint& point, size_t index) {
        inputs.at(index + 1) = {std::bit_cast<uint32_t>(point.x), std::bit_cast<uint32_t>(point.y),
                                std::bit_cast<uint32_t>(point.z)};
        const auto& reply = replies.at(index + 1);
        if (reply.writes)
            point = {std::bit_cast<float>(reply.coordinates[0]), std::bit_cast<float>(reply.coordinates[1]),
                     std::bit_cast<float>(reply.coordinates[2])};
        return std::bit_cast<float>(reply.height);
    }
};

}  // namespace

int main() {
    Origin origin{};
    while (std::cin >> origin.coordinates[0] >> origin.coordinates[1] >> origin.coordinates[2]) {
        Services services{origin};
        for (auto& [height, writes, coordinates] : services.replies) {
            if (!(std::cin >> height >> writes >> coordinates[0] >> coordinates[1] >> coordinates[2])) return 1;
        }
        const auto actual = nx::acnh::shovel_traversal_peak({0}, origin, services);
        std::cout << std::bit_cast<uint32_t>(actual.x) << ' ' << std::bit_cast<uint32_t>(actual.y)
                  << ' ' << std::bit_cast<uint32_t>(actual.z);
        for (auto component : origin.coordinates) std::cout << ' ' << component;
        for (const auto& point : services.inputs)
            for (auto component : point) std::cout << ' ' << component;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
