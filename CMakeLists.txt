cmake_minimum_required(VERSION 3.10)
project(camera_app)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_library(camera_lib
    common/camera.cpp
)

target_include_directories(camera_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS})

add_executable(camera_app
    camera_app_src/main.cpp
)
target_link_libraries(camera_app PRIVATE camera_lib)

# Retain your original specific output directory configuration
set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

add_executable(virtcam_client
    virtcam_client/main.cpp
)
target_link_libraries(virtcam_client PRIVATE camera_lib)

# Send the client binary to the same /out directory as well
set_target_properties(virtcam_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

add_executable(unit_tests
    tests/CameraTests.cpp
)

target_link_libraries(unit_tests PRIVATE
    camera_lib
    GTest::gtest_main
)

gtest_discover_tests(unit_tests)