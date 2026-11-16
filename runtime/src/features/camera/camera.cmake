add_library(nx_camera INTERFACE)
target_sources(nx_camera INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/terrain_adapter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/native.cpp
    ${CMAKE_CURRENT_LIST_DIR}/legacy.cpp
    ${CMAKE_CURRENT_LIST_DIR}/freecamera.cpp
    ${CMAKE_CURRENT_LIST_DIR}/freecamera_ao.cpp
    ${CMAKE_CURRENT_LIST_DIR}/freecamera_fog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/freecamera_fruit.cpp
    ${CMAKE_CURRENT_LIST_DIR}/uniform_adapter.cpp
)
