#pragma once

#include "field_queries.h"

#include <bit>
#include <cstdint>
#include <cstddef>
#include <initializer_list>

namespace nx::acnh {

struct ItemCatalog { uint64_t address; };
struct CatalogEntry { uint64_t address; };
struct ItemDescriptor { uint64_t address; };

enum class ShovelItemIdUse { catalog, alternate_catalog, copied_alternate, copied_special, metadata, fallback };

template<class Memory>
class ShovelItemQueries {
public:
    ShovelItemQueries(uint64_t base, Memory& memory) : base_(base), memory_(memory) {}

    CatalogEntry entry(ItemCatalog catalog, uint32_t id) const {
        const uint32_t index = uint16_t(id) - uint32_t(memory_.template read<uint16_t>(catalog.address + 0x1f8));
        if (index >= memory_.template read<uint32_t>(catalog.address + 0x1d8)) return {};
        const auto entries = memory_.template read<uint64_t>(catalog.address + 0x1e0);
        return {memory_.template read<uint64_t>(entries + 8ll * std::bit_cast<int32_t>(index))};
    }

    bool enabled(ItemCatalog catalog) const {
        if (!catalog.address) return false;
        for (const auto offset : {0xb0, 0x488, 0x2b0}) {
            const auto slot = memory_.template read<uint64_t>(catalog.address + offset);
            if (!slot || !memory_.template read<uint64_t>(slot + 8)) return false;
        }
        return true;
    }

    template<class Services>
    bool recognized(FieldItem item, Services& services) const {
        const auto id = memory_.template read<uint16_t>(item.address);
        if (id == 0xfffd || id == 0xfffe) return false;

        const auto catalog_ref = memory_.template read<uint64_t>(base_ + 0x4abbc68);
        const ItemCatalog catalog{memory_.template read<uint64_t>(catalog_ref)};
        if (enabled(catalog) && entry(catalog, services.item_id(item, ShovelItemIdUse::catalog)).address)
            return true;

        const ItemCatalog owner{alternate_owner()};
        const auto alternate_id = services.item_id(item, ShovelItemIdUse::alternate_catalog);
        if (owner.address && entry(alternate_catalog(owner), alternate_id).address) return true;
        return services.item_id(services.copy_special(item, id), ShovelItemIdUse::copied_special) - 0xf619u < 7;
    }

    template<class Services>
    bool blocked_by_alternate(FieldItem item, Services& services) const {
        const ItemCatalog owner{alternate_owner()};
        const auto id = services.item_id(item, ShovelItemIdUse::alternate_catalog);
        if (!owner.address) return false;
        const auto catalog = alternate_catalog(owner);
        if (!entry(catalog, id).address) return false;

        const auto descriptor = services.copy_alternate(item);
        const auto flags = memory_.template read<uint8_t>(item.address + 2);
        const auto copied_id = services.item_id({descriptor.address + 8}, ShovelItemIdUse::copied_alternate);
        if (entry(catalog, copied_id).address) {
            const ItemCatalog metadata{metadata_catalog()};
            const auto metadata_id = services.item_id(item, ShovelItemIdUse::metadata);
            if (const auto row = entry(metadata, metadata_id); row.address)
                memory_.template write<uint64_t>(descriptor.address + 0x10, row.address);
        }
        services.metadata_enabled(descriptor);
        return (flags & 4) != 0;
    }

    template<class Services>
    bool suitable(FieldItem item, Services& services) const {
        if (!services.recognized(item) || services.blocked_by_alternate(item)) return false;

        const auto slot = memory_.template read<uint64_t>(base_ + 0x4ab43c0);
        const ItemCatalog first{memory_.template read<uint64_t>(slot)};
        const auto id = services.item_id(item, ShovelItemIdUse::alternate_catalog);
        const ItemCatalog current{memory_.template read<uint64_t>(slot)};
        if (current.address && entry(alternate_catalog(first), id).address) {
            const auto descriptor = services.copy_alternate(item);
            const auto copied_id = services.item_id({descriptor.address + 8}, ShovelItemIdUse::copied_alternate);
            if (memory_.template read<uint64_t>(slot) && entry(alternate_catalog(current), copied_id).address) {
                const ItemCatalog metadata{metadata_catalog()};
                const auto metadata_id = services.item_id(item, ShovelItemIdUse::metadata);
                if (const auto row = entry(metadata, metadata_id); row.address)
                    memory_.template write<uint64_t>(descriptor.address + 0x10, row.address);
            }
            return !services.metadata_enabled(descriptor) || services.metadata_suitable(descriptor);
        }

        const ItemCatalog catalog{memory_.template read<uint64_t>(base_ + 0x5050358)};
        if (enabled(catalog) && entry(catalog, services.item_id(item, ShovelItemIdUse::catalog)).address) {
            const auto descriptor = services.describe(item);
            if (services.has_category(descriptor)) return true;
            if (services.category_flag({memory_.template read<uint64_t>(descriptor.address + 0x10)}, 4)) return true;

            const auto category = [&](size_t call) {
                return services.category(FieldItem{descriptor.address + 8},
                    CatalogEntry{memory_.template read<uint64_t>(descriptor.address + 0x10)}, call);
            };
            if (category(0) - 0xbu < 7 || category(1) == 0x2d || category(2) - 0x12u < 6) return true;
            if (category(3) - 0x18u < 7 || category(4) == 0x1f) return false;
            if (category(5) - 0x23u <= 8) return true;
            category(6);
            return true;
        }

        services.item_id(services.copy_fallback(item), ShovelItemIdUse::fallback);
        return true;
    }

private:
    uint64_t alternate_owner() const {
        return memory_.template read<uint64_t>(memory_.template read<uint64_t>(base_ + 0x4ab43c0));
    }

    ItemCatalog alternate_catalog(ItemCatalog owner) const {
        return {memory_.template read<uint64_t>(owner.address + 0x57e58)};
    }

    uint64_t metadata_catalog() const {
        return memory_.template read<uint64_t>(memory_.template read<uint64_t>(base_ + 0x4ac0428));
    }

    uint64_t base_;
    Memory& memory_;
};

}  // namespace nx::acnh
