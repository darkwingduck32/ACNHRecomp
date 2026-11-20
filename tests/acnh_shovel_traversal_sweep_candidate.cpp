#include "../games/acnh/shovel_traversal_sweep.h"

#include <cstring>
#include <iostream>
#include <tuple>
#include <vector>

using namespace nx::acnh;
using XYZ = std::array<uint32_t, 3>;

XYZ bits(ShovelTargetPoint point) {
    return {std::bit_cast<uint32_t>(point.x), std::bit_cast<uint32_t>(point.y), std::bit_cast<uint32_t>(point.z)};
}

ShovelTargetPoint point_from_bits(XYZ point) {
    return {std::bit_cast<float>(point[0]), std::bit_cast<float>(point[1]), std::bit_cast<float>(point[2])};
}

struct Memory {
    std::array<uint8_t, 44> bytes{};
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        T value;
        std::memcpy(&value, bytes.data() + address, sizeof(T));
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        std::memcpy(bytes.data() + address, &value, sizeof(T));
        writes.emplace_back(address, sizeof(T), value);
    }
};
struct Services {
    struct HeightReply { uint32_t height; bool changed; XYZ point; };
    struct CastReply { bool accepted, blocked; XYZ point; };

    uint32_t angle;
    std::optional<HalfTile> root;
    std::array<float, 4> samples;
    std::array<HeightReply, 2> heights;
    std::array<CastReply, 19> casts;
    std::vector<uint32_t> trace;
    unsigned attempt = 0;

    void xyz(ShovelTargetPoint point) {
        const auto entry = bits(point);
        trace.insert(trace.end(), entry.begin(), entry.end());
    }
    float sample_height(ShovelTargetPoint& point, unsigned index) {
        trace.push_back(1);
        xyz(point);
        const auto& reply = heights.at(index);
        if (reply.changed) point = point_from_bits(reply.point);
        return std::bit_cast<float>(reply.height);
    }
    uint32_t heading(float dx, float dz) {
        trace.insert(trace.end(), {2, std::bit_cast<uint32_t>(dx), std::bit_cast<uint32_t>(dz)});
        return angle;
    }
    std::optional<HalfTile> prepare_casts(HalfTile tile) {
        trace.insert(trace.end(), {3, uint32_t(tile.x), uint32_t(tile.z)});
        return root;
    }
    std::array<float, 4> heading_samples(uint32_t) const { return samples; }
    ShovelSweepResult cast(const ShovelSweepCast& query) {
        trace.push_back(4);
        xyz(query.point);
        trace.push_back(bool(query.destination));
        if (query.destination)
            trace.insert(trace.end(), {uint32_t(query.destination->x), uint32_t(query.destination->z)});
        const auto& reply = casts.at(attempt++);
        return {point_from_bits(reply.point), reply.accepted, reply.blocked};
    }
};

int main() {
    bool snap, output, have_root;
    int32_t root_x, root_z;
    uint32_t heading;
    while (std::cin >> snap >> output >> have_root >> root_x >> root_z >> heading) {
        Memory memory;
        for (int i = 0; i < 32; i += 4) {
            uint32_t value;
            if (!(std::cin >> value)) return 1;
            std::memcpy(memory.bytes.data() + i, &value, sizeof(value));
        }
        Services service{};
        service.angle = heading;
        if (have_root) service.root = HalfTile{root_x, root_z};
        for (auto& sample : service.samples) {
            uint32_t value;
            if (!(std::cin >> value)) return 1;
            sample = std::bit_cast<float>(value);
        }
        for (auto& [height, changed, point] : service.heights)
            if (!(std::cin >> height >> changed >> point[0] >> point[1] >> point[2])) return 1;
        for (auto& [accepted, blocked, point] : service.casts)
            if (!(std::cin >> accepted >> blocked >> point[0] >> point[1] >> point[2])) return 1;
        const bool result = shovel_traversal_sweep({0, output ? 32u : 0u, snap}, memory, service);
        std::cout << unsigned(result) << ' ' << memory.writes.size();
        for (const auto& [address, width, value] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << value;
        for (const auto value : service.trace) std::cout << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
