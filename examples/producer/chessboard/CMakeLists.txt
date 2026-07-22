add_executable(chessboard_producer
    main.cpp
    CpuBufferPool.cpp
    ChessboardProducer.cpp
    SoftwareScaler.cpp
)

target_include_directories(chessboard_producer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/interfaces
        ${CMAKE_SOURCE_DIR}/transport/v4l2
)

target_link_libraries(chessboard_producer
    PRIVATE
        v4l2_transport
        ${OpenCV_LIBS}
        Threads::Threads
)

set_target_properties(chessboard_producer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)
