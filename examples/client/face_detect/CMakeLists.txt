# Optional TFLite BlazeFace webcam demo (Qt UI).
# Built when ENABLE_FACE_DETECTION=ON and TensorFlow Lite is available.

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_library(face_detector_lib FaceDetector.cpp)
target_include_directories(face_detector_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(face_detector_lib
    PUBLIC
        camera_lib
        ${OpenCV_LIBS}
        TensorFlowLite::TensorFlowLite
)

add_executable(face_detect
    main.cpp
    FaceDetectWindow.cpp
    FaceDetectWindow.h
)
set_target_properties(face_detect PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
)
target_link_libraries(face_detect
    PRIVATE
        face_detector_lib
        Qt5::Widgets
)

set(FACE_MODEL_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/models/face_detection_short_range.tflite)

if(EXISTS ${FACE_MODEL_PATH})
    add_custom_command(TARGET face_detect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/out/models
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FACE_MODEL_PATH}
            ${CMAKE_SOURCE_DIR}/out/models/face_detection_short_range.tflite
        COMMENT "Copying face detection model to out/models/"
    )
else()
    message(WARNING "Face model not found at ${FACE_MODEL_PATH}")
endif()
