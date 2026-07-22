# Optional TFLite BlazeFace webcam demo.
# Built when ENABLE_FACE_DETECTION=ON and TensorFlow Lite is available.

add_library(face_detector_lib FaceDetector.cpp)
target_include_directories(face_detector_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${COMMON_INCLUDE_DIR}
)
target_link_libraries(face_detector_lib
    PUBLIC
        ${OpenCV_LIBS}
        TensorFlowLite::TensorFlowLite
)

add_executable(face_detect main.cpp)
target_link_libraries(face_detect PRIVATE camera_lib face_detector_lib)
set_target_properties(face_detect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
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
