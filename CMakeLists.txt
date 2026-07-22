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

set(UVAP_UTILS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/utils)

# OpenCV capture helpers (sample consumers, not UVAP VAL)
add_library(camera_lib utils/camera/camera.cpp)
target_include_directories(camera_lib PUBLIC ${UVAP_UTILS_INCLUDE_DIR})
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# UVAP V4L2 producer transport (VIDEO_OUTPUT / v4l2loopback)
add_subdirectory(transport/v4l2)

# Synthetic chessboard producer → scaler → V4L2 sink
add_subdirectory(examples/producer/chessboard)

if(ENABLE_FACE_DETECTION)
    add_subdirectory(examples/client/face_detect)
    message(STATUS "Face detection example: ENABLED")
else()
    message(STATUS "Face detection example: DISABLED")
endif()
