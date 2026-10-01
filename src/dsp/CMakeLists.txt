add_library(pf8_dsp STATIC
    Biquad.cpp
    Dynamics.cpp
    Reverb.cpp
    ChannelStrip.cpp
    Presets.cpp
    MicAnalyzer.cpp
    VarResampler.cpp
)
target_link_libraries(pf8_dsp PUBLIC pf8_core)
target_include_directories(pf8_dsp PUBLIC ${PROJECT_SOURCE_DIR}/src)
pf8_set_warnings(pf8_dsp)
