cmake_minimum_required(VERSION 3.14)
project(uvap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(UVAP_BUILD_EXAMPLES "Build UVAP sample producers and consumers" ON)
# Face detect example needs TensorFlow Lite. Ubuntu 24.04 CI does not ship
# libtensorflow-lite-dev, so CI configures with -DENABLE_FACE_DETECTION=OFF.
option(ENABLE_FACE_DETECTION
    "Build examples/client/face_detect (requires TensorFlow Lite)"
    ON)

if(UVAP_BUILD_EXAMPLES)
    find_package(OpenCV REQUIRED)
    find_package(Threads REQUIRED)
endif()

if(UVAP_BUILD_EXAMPLES AND ENABLE_FACE_DETECTION)
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

# Stable, transport-independent UVAP contracts.
add_library(uvap_core INTERFACE)
add_library(uvap::core ALIAS uvap_core)
set_target_properties(uvap_core PROPERTIES EXPORT_NAME core)
target_include_directories(uvap_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
        $<INSTALL_INTERFACE:include/uvap>
)
target_compile_features(uvap_core INTERFACE cxx_std_20)

# Transport-independent producer → transform → sink orchestration.
add_subdirectory(pipeline)

# UVAP V4L2 producer transport (VIDEO_OUTPUT / v4l2loopback)
add_subdirectory(transport/v4l2)

if(UVAP_BUILD_EXAMPLES)
    # OpenCV capture helpers (sample consumers, not UVAP VAL)
    add_library(camera_lib utils/camera/camera.cpp)
    target_include_directories(camera_lib PUBLIC ${UVAP_UTILS_INCLUDE_DIR})
    target_link_libraries(camera_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

    # Synthetic chessboard producer → scaler → V4L2 sink
    add_subdirectory(examples/producer/chessboard)

    if(ENABLE_FACE_DETECTION)
        add_subdirectory(examples/client/face_detect)
        message(STATUS "Face detection example: ENABLED")
    else()
        message(STATUS "Face detection example: DISABLED")
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS uvap_core uvap_pipeline v4l2_transport
    EXPORT uvapTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY interfaces/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uvap
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY transport/v4l2/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uvap/v4l2
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY pipeline/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uvap/pipeline
    FILES_MATCHING PATTERN "*.h"
)
install(EXPORT uvapTargets
    FILE uvapTargets.cmake
    NAMESPACE uvap::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvap
)
configure_package_config_file(
    cmake/uvapConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/uvapConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvap
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/uvapConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/uvapConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/uvapConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvap
)
