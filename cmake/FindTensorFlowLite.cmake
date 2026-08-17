# Finds TensorFlow Lite if it is installed. Never fails hard — callers should
# use find_package(TensorFlowLite QUIET) and check TensorFlowLite_FOUND.

find_path(TensorFlowLite_INCLUDE_DIR
    NAMES tensorflow/lite/interpreter.h
    PATHS
        /usr/include
        /usr/local/include
        "${CMAKE_BINARY_DIR}/_tflite_deps/tflite/usr/include"
)

find_library(TensorFlowLite_LIBRARY
    NAMES tensorflow-lite
    PATHS
        /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
        /usr/lib
        /usr/local/lib
)

if(NOT TensorFlowLite_LIBRARY)
    file(GLOB TensorFlowLite_LIBRARY_CANDIDATES
        LIST_DIRECTORIES false
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libtensorflow-lite.so*"
        "/usr/lib/libtensorflow-lite.so*")
    if(TensorFlowLite_LIBRARY_CANDIDATES)
        list(SORT TensorFlowLite_LIBRARY_CANDIDATES COMPARE NATURAL ORDER ASCENDING)
        list(GET TensorFlowLite_LIBRARY_CANDIDATES 0 TensorFlowLite_LIBRARY)
    endif()
endif()

find_path(FlatBuffers_INCLUDE_DIR
    NAMES flatbuffers/flatbuffers.h
    PATHS
        /usr/include
        /usr/local/include
        "${CMAKE_BINARY_DIR}/_tflite_deps/flatbuffers/usr/include"
)

# Optional: unpack apt .deb packages into the build dir when headers are missing
# but the runtime library is present (e.g. Ubuntu 26.04 without -dev installed).
function(_try_fetch_tflite_dev_headers)
    if(TensorFlowLite_INCLUDE_DIR AND FlatBuffers_INCLUDE_DIR)
        return()
    endif()

    set(_deps_root "${CMAKE_BINARY_DIR}/_tflite_deps")
    file(MAKE_DIRECTORY "${_deps_root}")

    if(NOT TensorFlowLite_INCLUDE_DIR)
        set(_tflite_marker "${_deps_root}/tflite/.extracted")
        if(NOT EXISTS "${_tflite_marker}")
            message(STATUS "TensorFlow Lite headers not found; trying apt download of libtensorflow-lite-dev...")
            execute_process(
                COMMAND apt download libtensorflow-lite-dev
                WORKING_DIRECTORY "${_deps_root}"
                RESULT_VARIABLE _tflite_download_result
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(_tflite_download_result EQUAL 0)
                file(GLOB _tflite_debs "${_deps_root}/libtensorflow-lite-dev*.deb")
                if(_tflite_debs)
                    list(GET _tflite_debs 0 _tflite_deb)
                    execute_process(
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${_deps_root}/tflite"
                        COMMAND dpkg-deb -x "${_tflite_deb}" "${_deps_root}/tflite"
                        RESULT_VARIABLE _tflite_extract_result
                    )
                    if(_tflite_extract_result EQUAL 0)
                        file(TOUCH "${_tflite_marker}")
                    endif()
                endif()
            else()
                message(STATUS "libtensorflow-lite-dev is not available from apt on this OS")
            endif()
        endif()

        find_path(TensorFlowLite_INCLUDE_DIR
            NAMES tensorflow/lite/interpreter.h
            PATHS "${_deps_root}/tflite/usr/include"
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT FlatBuffers_INCLUDE_DIR)
        set(_flatbuffers_marker "${_deps_root}/flatbuffers/.extracted")
        if(NOT EXISTS "${_flatbuffers_marker}")
            message(STATUS "FlatBuffers headers not found; trying apt download of libflatbuffers-dev...")
            execute_process(
                COMMAND apt download libflatbuffers-dev
                WORKING_DIRECTORY "${_deps_root}"
                RESULT_VARIABLE _flatbuffers_download_result
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(_flatbuffers_download_result EQUAL 0)
                file(GLOB _flatbuffers_debs "${_deps_root}/libflatbuffers-dev*.deb")
                if(_flatbuffers_debs)
                    list(GET _flatbuffers_debs 0 _flatbuffers_deb)
                    execute_process(
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${_deps_root}/flatbuffers"
                        COMMAND dpkg-deb -x "${_flatbuffers_deb}" "${_deps_root}/flatbuffers"
                        RESULT_VARIABLE _flatbuffers_extract_result
                    )
                    if(_flatbuffers_extract_result EQUAL 0)
                        file(TOUCH "${_flatbuffers_marker}")
                    endif()
                endif()
            else()
                message(STATUS "libflatbuffers-dev download failed or unavailable")
            endif()
        endif()

        find_path(FlatBuffers_INCLUDE_DIR
            NAMES flatbuffers/flatbuffers.h
            PATHS "${_deps_root}/flatbuffers/usr/include"
            NO_DEFAULT_PATH
        )
    endif()
endfunction()

if(NOT TensorFlowLite_INCLUDE_DIR OR NOT FlatBuffers_INCLUDE_DIR)
    _try_fetch_tflite_dev_headers()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TensorFlowLite
    FOUND_VAR TensorFlowLite_FOUND
    REQUIRED_VARS
        TensorFlowLite_LIBRARY
        TensorFlowLite_INCLUDE_DIR
        FlatBuffers_INCLUDE_DIR
)

if(TensorFlowLite_FOUND AND NOT TARGET TensorFlowLite::TensorFlowLite)
    add_library(TensorFlowLite::TensorFlowLite UNKNOWN IMPORTED)
    set_target_properties(TensorFlowLite::TensorFlowLite PROPERTIES
        IMPORTED_LOCATION "${TensorFlowLite_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${TensorFlowLite_INCLUDE_DIR};${FlatBuffers_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(
    TensorFlowLite_LIBRARY
    TensorFlowLite_INCLUDE_DIR
    FlatBuffers_INCLUDE_DIR
)
