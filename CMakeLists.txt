cmake_minimum_required(VERSION 3.14)
project(camera_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# Face detect example needs TensorFlow Lite. Ubuntu 24.04 CI does not ship
# libtensorflow-lite-dev, so CI configures with -DENABLE_FACE_DETECTION=OFF.
option(ENABLE_FACE_DETECTION
    "Build examples/client/face_detect (requires TensorFlow Lite)"
    ON)

if(ENABLE_FACE_DETECTION)
    find_package(TensorFlowLite QUIET)
    if(NOT TensorFlowLite_FOUND)
        message(WARNING
            "ENABLE_FACE_DETECTION=ON but TensorFlow Lite was not found. "
            "Skipping face_detect example. Install libtensorflow-lite-dev and "
            "libflatbuffers-dev, or pass -DENABLE_FACE_DETECTION=OFF.")
        set(ENABLE_FACE_DETECTION OFF CACHE BOOL
            "Build examples/client/face_detect (requires TensorFlow Lite)" FORCE)
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(COMMON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

# Shared camera capture library
add_library(camera_lib common/camera.cpp)
target_include_directories(camera_lib PUBLIC ${COMMON_INCLUDE_DIR})
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# Virtual camera / checkerboard library
add_library(virtcam_lib common/virtcam.cpp)
target_include_directories(virtcam_lib PUBLIC ${COMMON_INCLUDE_DIR})
target_link_libraries(virtcam_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# UVAP V4L2 producer transport (VIDEO_OUTPUT / v4l2loopback)
add_subdirectory(transport/v4l2)

# Synthetic chessboard producer → scaler → V4L2 sink
add_subdirectory(examples/producer/chessboard)

# Webcam application
add_executable(camera_app camera_app_src/main.cpp)
target_link_libraries(camera_app PRIVATE camera_lib)
set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

# Virtual camera client (streams checkerboard to V4L2)
add_executable(virtcam_client virtcam_client/main.cpp)
target_link_libraries(virtcam_client PRIVATE virtcam_lib)
set_target_properties(virtcam_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

if(ENABLE_FACE_DETECTION)
    add_subdirectory(examples/client/face_detect)
    message(STATUS "Face detection example: ENABLED")
else()
    message(STATUS "Face detection example: DISABLED")
endif()

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
