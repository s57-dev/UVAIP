cmake_minimum_required(VERSION 3.10)

project(camera_app)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(camera_app
    src/main.cpp
    src/camera.cpp
)

target_include_directories(camera_app PRIVATE
    include
)

target_compile_options(camera_app PRIVATE
    -Wall
    -Wextra
    -Wno-overloaded-virtual
)

target_link_libraries(camera_app PRIVATE
    ${OpenCV_LIBS}
)

set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)
