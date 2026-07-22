add_library(uvap_pipeline FramePipeline.cpp)
add_library(uvap::pipeline ALIAS uvap_pipeline)
set_target_properties(uvap_pipeline PROPERTIES EXPORT_NAME pipeline)

target_include_directories(uvap_pipeline
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/uvap/pipeline>
)
target_link_libraries(uvap_pipeline PUBLIC uvap::core)
target_compile_features(uvap_pipeline PUBLIC cxx_std_20)
