#pragma once
#include <cstddef>
#include <string>
#include <string_view>
namespace nx::resource_delta {
inline constexpr size_t limit = 128 * 1024 * 1024;
bool decompress(std::string_view input, std::string &output, std::string &error, size_t maximum = limit);
bool compress(std::string_view input, std::string &output, std::string &error);
bool apply(std::string_view original, std::string_view patch, std::string &output, std::string &error);
} // namespace nx::resource_delta
