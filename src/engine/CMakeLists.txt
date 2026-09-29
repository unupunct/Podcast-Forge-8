add_library(pf8_engine STATIC
    WasapiStream.cpp
    InternalClock.cpp
    MasterClock.cpp
    AudioEngine.cpp
    EngineController.cpp
    VarResampler.cpp
    DriftController.cpp
    Bridges.cpp
)
target_link_libraries(pf8_engine PUBLIC pf8_core pf8_devices PRIVATE avrt ole32)
pf8_set_warnings(pf8_engine)
