add_executable(PodcastForge8 WIN32
    Main.cpp
    CliModes.cpp
    app.manifest
)
target_link_libraries(PodcastForge8 PRIVATE pf8_ui pf8_engine pf8_devices pf8_core pf8_juce)
target_compile_options(PodcastForge8 PRIVATE /utf-8 /MP)
target_compile_definitions(PodcastForge8 PRIVATE NOMINMAX)
target_link_options(PodcastForge8 PRIVATE /MANIFESTUAC:NO)
set_target_properties(PodcastForge8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
