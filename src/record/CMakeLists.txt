add_library(pf8_record STATIC
    FileSink.cpp
    SampleFormat.cpp
    WavWriter.cpp
    FlacWriter.cpp
    Journal.cpp
    Recovery.cpp
    Markers.cpp
    Session.cpp
    DiskGuard.cpp
    Recorder.cpp
)
target_link_libraries(pf8_record PUBLIC pf8_core PRIVATE pf8_juce)
target_include_directories(pf8_record PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(pf8_record PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /wd4324)
target_compile_definitions(pf8_record PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
