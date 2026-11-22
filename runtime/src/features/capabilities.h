#pragma once
#ifndef NX_NATIVE_FEATURE_RESOURCES
#define NX_NATIVE_FEATURE_RESOURCES 0
#endif
namespace nx::features {
inline constexpr bool camera_models_available = NX_NATIVE_FEATURE_RESOURCES != 0;
inline constexpr bool tool_durability_available = NX_NATIVE_FEATURE_RESOURCES != 0;
} // namespace nx::features
