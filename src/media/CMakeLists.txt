add_library(pf8_media STATIC
    Soundboard.cpp
    AudioFileLoader.cpp
    MusicPlayer.cpp
)
target_link_libraries(pf8_media PUBLIC pf8_core PRIVATE pf8_juce PUBLIC pf8_dsp)
target_include_directories(pf8_media PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(pf8_media PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /wd4324)
target_compile_definitions(pf8_media PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
