add_library(pf8_engine STATIC
    WasapiStream.cpp
    InternalClock.cpp
    MasterClock.cpp
    AudioEngine.cpp
    EngineController.cpp
    DriftController.cpp
    Bridges.cpp
)
target_link_libraries(pf8_engine PUBLIC pf8_core pf8_devices pf8_dsp pf8_routing pf8_record pf8_media PRIVATE avrt ole32)
target_include_directories(pf8_engine PUBLIC ${PROJECT_SOURCE_DIR}/src)
pf8_set_warnings(pf8_engine)
