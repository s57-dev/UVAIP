if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "uvap::v4l2_transport requires Linux V4L2")
endif()

add_library(v4l2_transport
    PixelFormatMap.cpp
    V4l2LoopbackSession.cpp
    V4l2Sink.cpp
)
add_library(uvap::v4l2_transport ALIAS v4l2_transport)

target_include_directories(v4l2_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/uvap/v4l2>
)

target_link_libraries(v4l2_transport PUBLIC uvap::core)
target_compile_features(v4l2_transport PUBLIC cxx_std_20)
target_compile_options(v4l2_transport PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
