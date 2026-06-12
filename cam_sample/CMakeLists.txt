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
    src/camera.cpp
)

target_include_directories(camera_lib PUBLIC include)
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS})

add_executable(camera_app
    src/main.cpp
)

target_link_libraries(camera_app PRIVATE camera_lib)

set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

add_executable(unit_tests
    tests/CameraTests.cpp
)

target_include_directories(unit_tests PRIVATE include)

target_link_libraries(unit_tests PRIVATE
    camera_lib
    GTest::gtest_main
)

gtest_discover_tests(unit_tests)
