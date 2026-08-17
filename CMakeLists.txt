cmake_minimum_required(VERSION 3.14)
project(camera_app LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# Face detection needs TensorFlow Lite. Ubuntu 24.04 CI does not ship
# libtensorflow-lite-dev, so CI configures with -DENABLE_FACE_DETECTION=OFF.
option(ENABLE_FACE_DETECTION
    "Build camera_app with TensorFlow Lite face detection"
    ON)

if(ENABLE_FACE_DETECTION)
    find_package(TensorFlowLite QUIET)
    if(NOT TensorFlowLite_FOUND)
        message(WARNING
            "ENABLE_FACE_DETECTION=ON but TensorFlow Lite was not found. "
            "Disabling face detection. Install libtensorflow-lite-dev and "
            "libflatbuffers-dev, or pass -DENABLE_FACE_DETECTION=OFF.")
        set(ENABLE_FACE_DETECTION OFF CACHE BOOL
            "Build camera_app with TensorFlow Lite face detection" FORCE)
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
set(FACE_MODEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/models/face_detection_short_range.tflite)

# Shared camera capture library
add_library(camera_lib common/camera.cpp)
target_include_directories(camera_lib PUBLIC ${COMMON_INCLUDE_DIR})
target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# Virtual camera / checkerboard library
add_library(virtcam_lib common/virtcam.cpp)
target_include_directories(virtcam_lib PUBLIC ${COMMON_INCLUDE_DIR})
target_link_libraries(virtcam_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

# Webcam application
add_executable(camera_app camera_app_src/main.cpp)
target_link_libraries(camera_app PRIVATE camera_lib)
set_target_properties(camera_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

if(ENABLE_FACE_DETECTION)
    add_library(face_detector_lib common/FaceDetector.cpp)
    target_include_directories(face_detector_lib PUBLIC ${COMMON_INCLUDE_DIR})
    target_link_libraries(face_detector_lib PUBLIC ${OpenCV_LIBS} TensorFlowLite::TensorFlowLite)

    target_compile_definitions(camera_app PRIVATE CAMERA_APP_WITH_FACE_DETECTION=1)
    target_link_libraries(camera_app PRIVATE face_detector_lib)

    if(EXISTS ${FACE_MODEL_PATH})
        add_custom_command(TARGET camera_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/out/models
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FACE_MODEL_PATH}
                ${CMAKE_SOURCE_DIR}/out/models/face_detection_short_range.tflite
            COMMENT "Copying face detection model to out/models/"
        )
    else()
        message(WARNING "Face model not found at ${FACE_MODEL_PATH}")
    endif()

    message(STATUS "Face detection: ENABLED")
else()
    message(STATUS "Face detection: DISABLED")
endif()

# Virtual camera client (streams checkerboard to V4L2)
add_executable(virtcam_client virtcam_client/main.cpp)
target_link_libraries(virtcam_client PRIVATE virtcam_lib)
set_target_properties(virtcam_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

# Shared memory transport between VLC plugin and pipeline client
add_library(shared_frame_channel_lib common/shared_frame_channel.c)
target_include_directories(shared_frame_channel_lib PUBLIC ${COMMON_INCLUDE_DIR})
target_link_libraries(shared_frame_channel_lib PUBLIC rt)

# VLC playback -> shared memory -> V4L2 loopback
add_executable(vlc_pipeline_client vlc_pipeline_client/main.cpp)
target_link_libraries(vlc_pipeline_client PRIVATE shared_frame_channel_lib ${OpenCV_LIBS})
set_target_properties(vlc_pipeline_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)

option(ENABLE_VLC_PLUGIN "Build VLC frame tap plugin" OFF)
if(ENABLE_VLC_PLUGIN)
    if(NOT VLC_SOURCE_DIR)
        message(FATAL_ERROR
            "ENABLE_VLC_PLUGIN=ON requires -DVLC_SOURCE_DIR=/path/to/vlc-source (3.0.x)")
    endif()

    find_package(VLC REQUIRED)
    add_library(frame_tap_plugin MODULE vlc_plugin/frame_tap.c)
    target_include_directories(frame_tap_plugin PRIVATE
        ${COMMON_INCLUDE_DIR}
    )
    target_link_libraries(frame_tap_plugin PRIVATE shared_frame_channel_lib VLC::PluginHeaders)
    target_compile_definitions(frame_tap_plugin PRIVATE
        MODULE_NAME=frame_tap
        MODULE_STRING="frame_tap"
    )
    target_compile_options(frame_tap_plugin PRIVATE -fPIC)
    set_target_properties(frame_tap_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "libframe_tap_plugin"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out/vlc/plugins/video_filter
    )
    message(STATUS "VLC frame tap plugin: ENABLED (VLC_SOURCE_DIR=${VLC_SOURCE_DIR})")
else()
    message(STATUS "VLC frame tap plugin: DISABLED (pass -DENABLE_VLC_PLUGIN=ON)")
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
