set(PF8_ICON_PATH "${PROJECT_SOURCE_DIR}/resources/PodcastForge8.ico")
file(TO_NATIVE_PATH "${PF8_ICON_PATH}" PF8_ICON_PATH)
string(REPLACE "\\" "\\\\" PF8_ICON_PATH "${PF8_ICON_PATH}")
configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(PodcastForge8 WIN32
    Main.cpp
    CliModes.cpp
    E2E.cpp
    app.manifest
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_link_libraries(PodcastForge8 PRIVATE pf8_ui pf8_project pf8_media pf8_engine pf8_record pf8_devices pf8_core pf8_juce ole32)
target_compile_options(PodcastForge8 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /MP>)
target_compile_definitions(PodcastForge8 PRIVATE NOMINMAX)
target_link_options(PodcastForge8 PRIVATE /MANIFESTUAC:NO)
set_target_properties(PodcastForge8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
