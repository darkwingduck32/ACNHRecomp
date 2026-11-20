#pragma once

#include "shovel_target.h"
#include "shovel_terrain.h"

namespace nx::acnh {

struct ShovelTraversalQuery {
    uint64_t descriptor;
    uint64_t output;
};

struct ShovelTraversalScratch {
    uint64_t alternate_corner;
    uint64_t first_corner;
    uint64_t intermediate_output;
    uint64_t descriptor;
    uint64_t local_output;
};

enum class TraversalSurface {
    initial, same_tile, goal_stage, initial_origin, initial_goal, first_corner_stage,
    first_corner_origin, first_corner_goal, alternate_corner_stage, alternate_corner_origin, alternate_corner_goal,
};
enum class TraversalPeak { initial, same_tile, first_choice, second_choice };
enum class TraversalStage { goal, first_corner, alternate_corner };
enum class TraversalLeg { first, first_to_goal, alternate, alternate_to_goal };
enum class TraversalRectangleSide { same_tile, north, east, south, west };

struct TraversalRectangle {
    HalfTile first;
    HalfTile last;
};

struct TraversalRectangleCheck {
    TraversalRectangle bounds;
    TraversalRectangleSide side;
    unsigned corner;
    bool wide;
};

template<class Memory, class Services>
class ShovelTraversal {
public:
    ShovelTraversal(ShovelTraversalQuery query, ShovelTraversalScratch scratch, Memory& memory, Services& services)
        : request_(query.descriptor), output_(query.output ? query.output : scratch.local_output),
          scratch_(scratch), memory_(memory), services_(services) {}

    bool run() {
        const auto goal = request_ + 12;
        const auto start_point = point(request_);
        const auto goal_point = point(goal);
        write(scratch_.local_output, goal_point);
        services_.heading(goal_point.x - start_point.x, goal_point.z - start_point.z);

        const auto initial_peak = sample_peak(goal, scratch_.descriptor, TraversalSurface::initial, TraversalPeak::initial);
        const auto from = tile(point(request_));
        const auto to = even(tile(point(goal)));
        if (even(from).x == to.x && even(from).z == to.z
            && services_.rectangle({{even(from), {from.x | 1, from.z | 1}}, TraversalRectangleSide::same_tile, 0, false})) {
            write_rejected(sample_peak(goal, scratch_.intermediate_output, TraversalSurface::same_tile, TraversalPeak::same_tile));
            return false;
        }
        if (blocked_boundary(even(from), to)) {
            write_rejected(initial_peak);
            return false;
        }

        if (has_stage() && different_stage(goal, TraversalSurface::goal_stage, TraversalStage::goal)) {
            if (!different_surface(TraversalSurface::initial_origin, TraversalSurface::initial_goal)) return true;
            copy_goal_to_output();
            return false;
        }
        if (!flag(0x1b)) return true;

        const auto origin = point(request_);
        const auto destination = point(goal);
        const ShovelTargetPoint first_corner{destination.x, 0.f, origin.z};
        write(scratch_.first_corner, first_corner);
        write(scratch_.intermediate_output, destination);
        if (recurse(origin, first_corner, scratch_.intermediate_output, TraversalLeg::first, flag(0x18))) {
            if (!flag(0x18)) return finish();
            if (recurse(point(scratch_.first_corner), point(goal), scratch_.intermediate_output,
                        TraversalLeg::first_to_goal, 1)) {
                if (!different_stage(scratch_.first_corner, TraversalSurface::first_corner_stage, TraversalStage::first_corner)) {
                    if (has_stage() && flag(0x18)
                        && different_surface(TraversalSurface::first_corner_origin, TraversalSurface::first_corner_goal))
                        copy_goal_to_output();
                    return finish();
                }
                write(scratch_.intermediate_output, point(scratch_.first_corner));
            }
        }

        const auto alternate_origin = point(request_);
        const auto alternate_destination = point(goal);
        const ShovelTargetPoint alternate_corner{alternate_origin.x, 0.f, alternate_destination.z};
        write(scratch_.alternate_corner, alternate_corner);
        write(scratch_.first_corner, alternate_destination);
        if (recurse(alternate_origin, alternate_corner, scratch_.first_corner, TraversalLeg::alternate, flag(0x18))) {
            if (!flag(0x18)) return finish();
            if (recurse(alternate_corner, point(goal), scratch_.first_corner, TraversalLeg::alternate_to_goal, 1)) {
                if (!different_stage(scratch_.alternate_corner, TraversalSurface::alternate_corner_stage, TraversalStage::alternate_corner)) {
                    if (has_stage() && flag(0x18)
                        && different_surface(TraversalSurface::alternate_corner_origin, TraversalSurface::alternate_corner_goal)) {
                        copy_goal_to_output();
                        return false;
                    }
                    return finish();
                }
                write(scratch_.first_corner, point(scratch_.alternate_corner));
            }
        }
        select_peak();
        return false;
    }

private:
    template<class T> T read(uint64_t address) const { return memory_.template read<T>(address); }
    template<class T> void store(uint64_t address, T value) { memory_.template write<T>(address, value); }

    uint8_t flag(uint64_t offset) const { return read<uint8_t>(request_ + offset); }
    bool has_stage() const { return flag(0x1c) != 0; }

    ShovelTargetPoint point(uint64_t address) const {
        return {std::bit_cast<float>(read<uint32_t>(address)),
                std::bit_cast<float>(read<uint32_t>(address + 4)),
                std::bit_cast<float>(read<uint32_t>(address + 8))};
    }
    void write(uint64_t address, ShovelTargetPoint value) {
        store(address, std::bit_cast<uint32_t>(value.x));
        store(address + 4, std::bit_cast<uint32_t>(value.y));
        store(address + 8, std::bit_cast<uint32_t>(value.z));
    }
    void write_rejected(ShovelTargetPoint value) { write(output_, value); }

    void copy_goal_to_output() {
        // The output may overlap the descriptor, so each source follows the previous store.
        store(output_, read<uint32_t>(request_ + 12));
        store(output_ + 4, read<uint32_t>(request_ + 16));
        store(output_ + 8, read<uint32_t>(request_ + 20));
    }

    static HalfTile tile(ShovelTargetPoint position) {
        return {terrain_half_tile(position.x), terrain_half_tile(position.z)};
    }
    static HalfTile even(HalfTile position) { return {position.x & ~1, position.z & ~1}; }

    ShovelTargetPoint sample_peak(uint64_t source, uint64_t local, TraversalSurface height, TraversalPeak peak) {
        const auto corner = even(tile(point(source)));
        const auto y = services_.surface(source, height);
        write(local, {float(corner.x) * 5.f + 5.f, y, float(corner.z) * 5.f + 5.f});
        return services_.peak(local, peak);
    }

    bool blocked_boundary(HalfTile start, HalfTile goal) {
        const auto [x, z] = start;
        const HalfTile a{x, z}, b{x + 1, z}, c{x, z + 1}, d{x + 1, z + 1};
        if (goal.x == x && goal.z < z)
            return blocked(TraversalRectangleSide::north, {{{{x, z - 1}, a}, {{x + 1, z - 1}, b},
                                                            {{x, z - 1}, c}, {{x + 1, z - 1}, d}}}, {a, b, c, d});
        if (goal.z == z && goal.x > x)
            return blocked(TraversalRectangleSide::east, {{{a, {x + 2, z}}, {c, d}, {b, {x + 2, z}}, {d, d}}}, {a, c, b, d});
        if (goal.x == x && goal.z > z)
            return blocked(TraversalRectangleSide::south, {{{a, {x, z + 2}}, {b, {x + 1, z + 2}},
                                                            {c, {x, z + 2}}, {d, {x + 1, z + 2}}}}, {a, b, c, d});
        if (goal.z == z && goal.x < x)
            return blocked(TraversalRectangleSide::west, {{{{x - 1, z}, a}, {{x - 1, z + 1}, c},
                                                           {{x - 1, z}, b}, {{x - 1, z + 1}, d}}}, {a, c, b, d});
        return false;
    }

    bool blocked(TraversalRectangleSide side, const std::array<TraversalRectangle, 4>& edges,
                 const std::array<HalfTile, 4>& corners) {
        for (unsigned i = 0; i < edges.size(); ++i) {
            if (!services_.rectangle({{corners[i], corners[i]}, side, i, false})
                && services_.rectangle({edges[i], side, i, true}))
                return true;
        }
        return false;
    }

    bool different_stage(uint64_t address, TraversalSurface height, TraversalStage stage) {
        const auto surface = services_.surface(address, height);
        return std::fabs(surface - services_.stage(address, stage, surface)) > 0.1f;
    }

    bool different_surface(TraversalSurface origin, TraversalSurface goal) {
        const auto height = services_.surface(request_, origin);
        return std::fabs(height - services_.surface(request_ + 12, goal)) > 10.f;
    }

    bool recurse(ShovelTargetPoint from, ShovelTargetPoint to, uint64_t output, TraversalLeg leg, uint8_t split) {
        const auto descriptor = scratch_.descriptor;
        write(descriptor, from);
        write(descriptor + 12, to);
        store(descriptor + 0x18, uint32_t(split));
        store(descriptor + 0x1c, uint16_t(0));
        store(descriptor + 0x1e, flag(0x1e));
        return services_.recurse(descriptor, output, leg);
    }

    bool finish() {
        return !has_stage() || (services_.sweep(request_, output_, false) && services_.sweep(request_, output_, true));
    }

    void select_peak() {
        const auto first = services_.peak(scratch_.intermediate_output, TraversalPeak::first_choice);
        write(scratch_.intermediate_output, first);
        const auto second = services_.peak(scratch_.first_corner, TraversalPeak::second_choice);
        const auto after = point(scratch_.intermediate_output);
        if (after.y > second.y) {
            write_rejected(after);
        } else if (second.y > after.y) {
            write_rejected(second);
        } else {
            const auto origin = point(request_);
            const auto distance = [&](ShovelTargetPoint candidate) {
                const float dx = origin.x - candidate.x, dz = origin.z - candidate.z;
                return std::sqrt(std::fma(dx, dx, dz * dz));
            };
            write_rejected(distance(after) < distance(second) ? after : second);
        }
    }

    uint64_t request_;
    uint64_t output_;
    ShovelTraversalScratch scratch_;
    Memory& memory_;
    Services& services_;
};

} // namespace nx::acnh
