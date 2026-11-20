// ACNH ZIP saves: one save at the root or within up to two enclosing folders.
#pragma once

#include <filesystem>
#include <string>

namespace nx::save_archive {

bool validate(const std::filesystem::path& zip, std::string* error = nullptr);
// Extracts into a caller-owned staging folder; the caller removes it on failure.
bool extract(const std::filesystem::path& zip, const std::filesystem::path& dest, std::string* error);
bool write(const std::filesystem::path& src, const std::filesystem::path& zip, std::string* error);

}  // namespace nx::save_archive
