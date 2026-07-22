set(UVAP_INTERFACES_DIR ${CMAKE_SOURCE_DIR}/interfaces)

add_library(v4l2_transport
    PixelFormatMap.cpp
    V4l2Sink.cpp
)

target_include_directories(v4l2_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${UVAP_INTERFACES_DIR}
)

target_compile_features(v4l2_transport PUBLIC cxx_std_20)
