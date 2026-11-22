#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
namespace nx {
// Bounded, atomic edit composition for host-owned copies. Conflicting writers
// are rejected unless the caller explicitly opts into ordered overwrite.
class BytePatch {
  public:
    struct Edit {
        size_t offset;
        std::vector<uint8_t> bytes;
    };
    explicit BytePatch(size_t size) : size_(size) {}
    bool write(size_t offset, const void *bytes, size_t count) {
        if (offset > size_ || count > size_ - offset)
            return valid_ = false;
        Edit edit{offset, std::vector<uint8_t>(count)};
        if (count)
            std::memcpy(edit.bytes.data(), bytes, count);
        for (const auto &old : edits_)
            if (!compatible(old, edit))
                return valid_ = false;
        edits_.push_back(std::move(edit));
        return true;
    }
    template <class T> bool write(size_t offset, const T &value) {
        static_assert(std::is_trivially_copyable_v<T>);
        return write(offset, &value, sizeof(value));
    }
    bool merge(const BytePatch &next, bool ordered_overwrite = false) {
        if (!valid_ || !next.valid_ || size_ != next.size_)
            return false;
        if (!ordered_overwrite)
            for (const auto &a : edits_)
                for (const auto &b : next.edits_)
                    if (!compatible(a, b))
                        return false;
        edits_.insert(edits_.end(), next.edits_.begin(), next.edits_.end());
        return true;
    }
    bool apply(std::span<uint8_t> target) const {
        if (!valid_ || target.size() != size_)
            return false;
        for (const auto &edit : edits_)
            if (!edit.bytes.empty())
                std::memcpy(target.data() + edit.offset, edit.bytes.data(), edit.bytes.size());
        return true;
    }
    bool empty() const {
        return edits_.empty();
    }
    bool valid() const {
        return valid_;
    }

  private:
    static bool compatible(const Edit &a, const Edit &b) {
        const auto begin = std::max(a.offset, b.offset),
                   end = std::min(a.offset + a.bytes.size(), b.offset + b.bytes.size());
        for (auto p = begin; p < end; ++p)
            if (a.bytes[p - a.offset] != b.bytes[p - b.offset])
                return false;
        return true;
    }
    size_t size_;
    bool valid_ = true;
    std::vector<Edit> edits_;
};
} // namespace nx
