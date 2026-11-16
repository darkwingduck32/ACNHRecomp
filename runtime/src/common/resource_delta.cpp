#include "resource_delta.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <zstd.h>
namespace nx::resource_delta {
namespace {
uint64_t word(std::string_view bytes, size_t offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= uint64_t(uint8_t(bytes[offset + i])) << (8 * i);
    return value;
}
bool fail(std::string &error, const char *message) {
    error = message;
    return false;
}
bool add(int64_t a, int64_t b, int64_t &result) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return false;
    result = a + b;
    return true;
}
} // namespace
bool decompress(std::string_view input, std::string &output, std::string &error, size_t maximum) {
    const auto size = ZSTD_getFrameContentSize(input.data(), input.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || (size != ZSTD_CONTENTSIZE_UNKNOWN && size > maximum))
        return fail(error, "Invalid or oversized Zstandard resource");
    // Reject concatenated frames and trailing garbage, including hidden data.
    const auto frame = ZSTD_findFrameCompressedSize(input.data(), input.size());
    if (ZSTD_isError(frame) || frame != input.size())
        return fail(error, "Invalid Zstandard frame length");
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!context || ZSTD_isError(ZSTD_DCtx_setParameter(context.get(), ZSTD_d_windowLogMax, 27)))
        return fail(error, "Unable to initialize Zstandard decoder");
    std::string candidate;
    if (size != ZSTD_CONTENTSIZE_UNKNOWN)
        candidate.reserve(size);
    ZSTD_inBuffer source{input.data(), input.size(), 0};
    char chunk[65536];
    size_t remaining = 1;
    while (remaining) {
        ZSTD_outBuffer target{chunk, sizeof(chunk), 0};
        remaining = ZSTD_decompressStream(context.get(), &target, &source);
        if (ZSTD_isError(remaining) || target.pos > maximum - candidate.size())
            return fail(error, "Zstandard decompression failed or exceeded limit");
        candidate.append(chunk, target.pos);
        if (remaining && source.pos == source.size && !target.pos)
            return fail(error, "Truncated Zstandard resource");
    }
    if (source.pos != source.size || (size != ZSTD_CONTENTSIZE_UNKNOWN && candidate.size() != size))
        return fail(error, "Invalid Zstandard output size");
    output = std::move(candidate);
    return true;
}
bool compress(std::string_view input, std::string &output, std::string &error) {
    if (input.size() > limit)
        return fail(error, "Resource exceeds compression limit");
    std::string candidate(ZSTD_compressBound(input.size()), '\0');
    const auto size = ZSTD_compress(candidate.data(), candidate.size(), input.data(), input.size(), 3);
    if (ZSTD_isError(size))
        return fail(error, "Zstandard compression failed");
    candidate.resize(size);
    output = std::move(candidate);
    return true;
}
bool apply(std::string_view original, std::string_view patch, std::string &output, std::string &error) {
    if (original.size() > limit || patch.size() < 8 || patch.substr(0, 8) != "NXDP0001")
        return fail(error, "Unknown or invalid native resource delta");
    std::string body;
    if (!decompress(patch.substr(8), body, error, 3 * limit))
        return false;
    if (body.size() < 32)
        return fail(error, "Truncated delta header");
    const uint64_t size = word(body, 0), controls = word(body, 8), diffs = word(body, 16), extras = word(body, 24);
    if (size > limit || controls > body.size() - 32 || controls % 24 || diffs > size || extras > size ||
        diffs + extras != size || controls + diffs + extras != body.size() - 32)
        return fail(error, "Invalid delta stream lengths");
    const auto diff_start = 32 + controls, extra_start = diff_start + diffs;
    std::string candidate(size, '\0');
    uint64_t position = 0, diff_pos = 0, extra_pos = 0;
    int64_t old_pos = 0;
    for (uint64_t offset = 32; offset < diff_start; offset += 24) {
        const auto count = word(body, offset), literal = word(body, offset + 8);
        const auto seek = std::bit_cast<int64_t>(word(body, offset + 16));
        if (count > diffs - diff_pos || literal > extras - extra_pos || count > size - position ||
            literal > size - position - count)
            return fail(error, "Delta instruction exceeds its resource");
        int64_t after_copy, next;
        if (!add(old_pos, int64_t(count), after_copy) || !add(after_copy, seek, next))
            return fail(error, "Delta seek overflow");
        for (uint64_t i = 0; i < count; ++i) {
            const int64_t index = old_pos + int64_t(i);
            const uint8_t base = index >= 0 && uint64_t(index) < original.size() ? uint8_t(original[index]) : 0;
            candidate[position + i] = char(uint8_t(body[diff_start + diff_pos + i]) + base);
        }
        position += count;
        diff_pos += count;
        old_pos = next;
        std::copy_n(body.data() + extra_start + extra_pos, size_t(literal), candidate.data() + position);
        position += literal;
        extra_pos += literal;
    }
    if (position != size || diff_pos != diffs || extra_pos != extras)
        return fail(error, "Incomplete native resource delta");
    output = std::move(candidate);
    return true;
}
} // namespace nx::resource_delta
