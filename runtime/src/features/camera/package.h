#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
namespace nx {
void camera_package_defaults();
bool camera_package_enabled();
void camera_package_disable();
bool camera_parameter_file(const std::string &guest);
FILE *camera_parameter_overlay(FILE *source, int64_t offset, int64_t size);
std::string camera_profile_path(const char *name);
} // namespace nx
