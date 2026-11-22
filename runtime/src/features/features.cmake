# Native feature data is build input, not a runtime Mod Manager installation.
include(FetchContent)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nx_zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR build/cmake)
FetchContent_MakeAvailable(nx_zstd)
add_library(nx_resource_data STATIC
    ${CMAKE_SOURCE_DIR}/runtime/src/common/resource_delta.cpp
    ${CMAKE_SOURCE_DIR}/runtime/src/common/artifact_cache.cpp)
target_include_directories(nx_resource_data PUBLIC ${CMAKE_SOURCE_DIR}/runtime/src)
target_link_libraries(nx_resource_data PRIVATE libzstd_static)
add_library(nx_feature_core STATIC ${CMAKE_CURRENT_LIST_DIR}/registry.cpp)
target_include_directories(nx_feature_core PUBLIC ${CMAKE_SOURCE_DIR}/runtime/src ${CMAKE_SOURCE_DIR}/runtime/include)
find_package(Python3 QUIET COMPONENTS Interpreter)
set(NX_NATIVE_ASSET_GAME "${CMAKE_SOURCE_DIR}/private/game" CACHE PATH "Reviewed native resource build inputs")
set(_native_output "${CMAKE_CURRENT_BINARY_DIR}/native-features")
if(Python3_Interpreter_FOUND AND NX_GAME STREQUAL "acnh")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build_native_features.py"
                    --game "${NX_NATIVE_ASSET_GAME}" --out "${_native_output}" --allow-missing
                    RESULT_VARIABLE _native_result)
    if(NOT _native_result EQUAL 0)
        message(FATAL_ERROR "Native feature resource generation failed; changed reviewed inputs are not accepted")
    endif()
else()
    file(MAKE_DIRECTORY "${_native_output}")
    file(WRITE "${_native_output}/index.cpp" "#include \"features/embedded_resources.h\"\nnamespace nx::features { std::span<const EmbeddedResource> embedded_resources() { return {}; } }\n")
    if(NX_GAME STREQUAL "acnh")
        message(WARNING "Python unavailable: native visual resources are not built")
    endif()
endif()
if(Python3_Interpreter_FOUND AND NX_GAME STREQUAL "acnh")
    file(GLOB _native_sources CONFIGURE_DEPENDS "${_native_output}/*.cpp")
else()
    # A reconfigured build directory must not pick up a preceding game's data.
    set(_native_sources "${_native_output}/index.cpp")
endif()
add_library(nx_embedded_resources STATIC ${_native_sources})
set_target_properties(nx_feature_core nx_resource_data nx_embedded_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(nx_feature_capabilities INTERFACE)
if(Python3_Interpreter_FOUND AND NX_GAME STREQUAL "acnh" AND
   (EXISTS "${CMAKE_SOURCE_DIR}/games/acnh/features/deltas/manifest.json" OR EXISTS "${NX_NATIVE_ASSET_GAME}/mods/freecamera/romfs"))
    target_compile_definitions(nx_feature_capabilities INTERFACE NX_NATIVE_FEATURE_RESOURCES=1)
else()
    target_compile_definitions(nx_feature_capabilities INTERFACE NX_NATIVE_FEATURE_RESOURCES=0)
endif()
target_include_directories(nx_embedded_resources PRIVATE ${CMAKE_SOURCE_DIR}/runtime/src)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/build_native_features.py" "${CMAKE_SOURCE_DIR}/tools/embed_resources.py"
    "${CMAKE_SOURCE_DIR}/tools/resource_delta.py"
    "${CMAKE_SOURCE_DIR}/games/acnh/features/deltas/manifest.json"
    "${CMAKE_SOURCE_DIR}/games/acnh/features/native_assets.lock.json"
    "${CMAKE_SOURCE_DIR}/games/acnh/features/camera/data/parameter_edits.txt"
    "${CMAKE_SOURCE_DIR}/games/acnh/features/camera/data/tree_fruit_offsets.txt"
    "${CMAKE_SOURCE_DIR}/games/acnh/features/camera/data/view_fog_shaders.txt")
