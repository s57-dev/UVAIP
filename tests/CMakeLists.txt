add_executable(v4l2_transport_tests V4l2TransportTests.cpp)
target_link_libraries(v4l2_transport_tests
    PRIVATE
        uvap::pipeline
        uvap::v4l2_transport
)

add_test(NAME v4l2_transport_contracts COMMAND v4l2_transport_tests)
