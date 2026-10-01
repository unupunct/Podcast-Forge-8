add_library(pf8_project STATIC
    ProjectState.cpp
    Project.cpp
    Hotkeys.cpp
)
target_link_libraries(pf8_project PUBLIC pf8_engine pf8_media pf8_core PRIVATE pf8_juce)
target_include_directories(pf8_project PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(pf8_project PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /wd4324)
target_compile_definitions(pf8_project PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
