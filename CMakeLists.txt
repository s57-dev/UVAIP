cmake_minimum_required(VERSION 3.14)
project(camera_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Shared camera capture library
add_library(camera_lib
    common/camera.cpp
)
target_include_directories(camera_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# Virtual camera / checkerboard library
add_library(virtcam_lib
    common/virtcam.cpp
)
target_include_directories(virtcam_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(virtcam_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# Webcam application
add_executable(camera_app
    camera_app_src/main.cpp
)
target_link_libraries(camera_app PRIVATE camera_lib)
set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

# Virtual camera client (streams checkerboard to V4L2)
add_executable(virtcam_client
    virtcam_client/main.cpp
)
target_link_libraries(virtcam_client PRIVATE virtcam_lib)
set_target_properties(virtcam_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

# Unit tests
add_executable(unit_tests
    tests/CameraTests.cpp
    tests/VirtCamTests.cpp
)
target_link_libraries(unit_tests PRIVATE
    camera_lib
    virtcam_lib
    GTest::gtest_main
)
gtest_discover_tests(unit_tests)
